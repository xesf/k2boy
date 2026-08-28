# `.act` — Motivate actor definition

Location: `data/Actors/<Name>/<Name>.act` (117 actor directories + a few extras, 148 files total).
Owning classes: `MfProtoActor`, `MfPoActor`, `MfPropertyTable`, `MfProperty`, `ArticulatedObject`.

**Status: header and property table confirmed and validated across all 118 files. Node layout and
write order recovered from the binary; the 20-byte child record is still unknown.**

An `.act` file is the serialized *prototype* of an actor: its name, its property table (skills,
attach points, bounds), and its articulation/joint structure. The per-animation data lives
separately in `data/Actors/<Name>/Skills/*.skl`.

## Header

```c
struct ActHeader {
    char     magic[4];     // "APFM"  — 'MFPA' read LE: Motion Factory Proto Actor
    char     type[4];      // "ActP"
    uint32_t version;      // 106 (111 files), 105 (2), 48 (2)
    uint32_t flags;        // 1 in every version-105/106 file observed
    MfString name;         // u32 length + raw bytes, no NUL
    MfString displayName;  // repeat of name in every observed file
    // ... body
};
```

`MfString` is `{ uint32_t len; char data[len]; }` — see
[00-overview.md](00-overview.md#endianness-and-conventions).

### Version distribution

| magic | type | version | count | files |
|---|---|---:|---:|---|
| `APFM` | `ActP` | 106 | 111 | the bulk of the set |
| `APFM` | `ActP` | 105 | 2 | `FIX_Swinger`, `FIX_MopingGuy` |
| `APFM` | `ActP` | 48 | 2 | `TRAP_PressurePlate`, `TRAP_Potion` |
| `" MFB"` | — | — | 3 | `arrow`, `bigaxes`, `BlankCamera` |

The `" MFB"` variant is a different, older container:

```
20 4d 46 42  02 00 23 00  41 63 74 50  00 00 00 00
" MFB"       02 00 23 00  "ActP"       00000000
```

i.e. magic `" MFB"` (`'BFM '` LE) + a 4-byte version/flags word + the `ActP` type tag. Three
leftover files; a loader must sniff both magics. Deprioritize unless `arrow` matters (it does —
arrows are a gameplay projectile, so this variant *is* loaded at runtime).

## Body — property table

**Status: confirmed.** Decoded from `MfProtoActor`'s constructor at `0x5FDD1C` and validated by
decoding **all 118** `.act` files in the install.

### Record grammar

Immediately after the two names comes a fixed float/int preamble, then a flat sequence of
property records. Each record is:

```c
struct PropertyRecord {
    uint32_t nameLen;
    char     name[nameLen];     // "MF_bndRadius", ... — no NUL
    uint32_t sentinel;          // always 0xFFFFFFFF
    char     tag[4];            // value type, from the chunk registry
    uint32_t payloadLen;
    uint8_t  payload[payloadLen];
};
```

The value `tag` is one of the registered 4CC types documented in
[`../reference/chunk-registry.md`](../reference/chunk-registry.md) — the *file byte order* form:

| Tag | Type | `payloadLen` | Payload |
|---|---|---:|---|
| `rtSV` | `VString` | 4 + n | `uint32 len` then `len` bytes (4 when empty) |
| `olFV` | `VFloat` | 4 | one `float32` |
| `3eVV` | `VVec3` | 12 | three `float32` |

Worked example — `MF_bndRadius` in `data/Actors/PICK_Gem/PICK_Gem.act`:

```
0c 00 00 00                          nameLen = 12
4d 46 5f 62 6e 64 52 61 64 69 75 73  "MF_bndRadius"
ff ff ff ff                          sentinel
6f 6c 46 56                          "olFV" = VFloat
04 00 00 00                          payloadLen = 4
06 4a 00 3e                          0.12528f
```

and `MF_eyeVector` in the same file:

```
0c 00 00 00 "MF_eyeVector" ff ff ff ff "3eVV" 0c 00 00 00
00 00 80 3f  00 00 00 00  00 00 00 00        -> (1.0, 0.0, 0.0)
```

### Validation

Decoding every `.act` in the install with the grammar above:

- **115 of 115** `APFM` files yield **exactly 11 properties**, with an **identical key set** in
  every file.
- The only 3 files that yield nothing are `arrow`, `bigaxes` and `BlankCamera` — precisely the
  three `" MFB"`-magic files described above. They use the older container and do not carry this
  block, which independently corroborates that they are a different format rather than a parse
  failure.

The 11 keys, matching the defaults installed by `MfProtoActor`'s constructor:

```
MF_graspPoseSkill  MF_graspSkill  MF_touchPoseSkill  MF_touchSkill   (VString)
MF_putPoseSkill    MF_sitSkill    MF_stopSkill       MF_locoSkill
MF_faceNode
MF_bndRadius                                                          (VFloat)
MF_eyeVector                                                          (VVec3)
```

### What the shipped data actually contains

Worth knowing before building tooling on this: **the property block is nearly inert in shipped
assets.**

- `MF_eyeVector` is `(1.0, 0.0, 0.0)` in all 115 files.
- All nine string properties are the empty string in all 115 files, with **one** exception:
  `Prince.act` sets `MF_locoSkill = "WalkForwards"`.
- `MF_bndRadius` is the only genuinely per-actor value: 98 distinct values ranging
  0.0283 to 15.1976.

So the interesting per-actor content lives in the node/geometry/joint structure, not here. The
Motivate property table is carried but left at defaults except for the bounding radius.

Two further keys exist in the binary — `MF_neckLength` and `MF_replanPriority` (`0x7DB450`,
`0x7DB460`) — but they are registered elsewhere (`0x637E01`, `0x63828A`, `0x66758B`) and never
appear in a shipped `.act`.

## The serialization interface

`MfStreamObject` is a **three-virtual interface**, not a large one. Base slots 0–2 all point at a
single stub (`0x52DFBA`, `MfStreamObject__pure_virtual_stub`); slot 3 is shared machinery. Every
serializable class overrides slots 0–2:

| Slot | Role | `MfProtoNode`'s override |
|---|---|---|
| 0 | return the 4CC tag | `0x603E2A` — `mov eax, 50724E6Fh; retn` |
| 1 | small accessor | `0x603E30` |
| 2 | **write** | `0x60418D` |

Slot 0 returning `0x50724E6F` is `'PrNo'`, which is `oNrP` in file byte order — matching the chunk
registry entry. That confirms both the slot's role and the tag mapping.

### Stream primitives

| Address | Name | Behaviour |
|---|---|---|
| `0x60531A` | `MfStream__write_u32` | pushes size 4, calls stream vtable `+4` |
| `0x6053B5` | `MfStream__write_object` | dispatches through the target's vtable `+8` |
| `0x605376` | `MfStream__write_string` | |
| `0x6054D8` | `MfStream__write_subobject` | |
| stream vtable `+4` | raw `write(size, ptr)` | the primitive all of the above bottom out in |

### `MfProtoNode` layout and field order

From the constructor (`0x602E20`) plus the writer (`0x60418D`).

**The two use different `this`.** `MfProtoNode` is multiply-inherited, with vtables at `+0x00`
(`vtbl_MfProtoNode_2`) and `+0x14` (`vtbl_MfStreamObject`, overwritten with `vtbl_MfProtoNode`).
The serialization methods are called on the `+0x14` sub-object, so **every offset in the writer is
0x14 lower than the same field in the constructor**. That mismatch is what made the write order
look unrelated to the object until now.

Constructor initialisation, in object-relative terms:

```c
struct MfProtoNode {            // 128 bytes = oNrP sizeof
    void**    vtbl_a;           // +0x00  vtbl_MfProtoNode_2
    void**    vtbl_b;           // +0x14  vtbl_MfProtoNode
    MfString  name;             // +0x18
    int32_t   field_1C;         // +0x1C
    float     pos[3];           // +0x20
    float     quat[4];          // +0x2C  w first — fld1 lands here
    void*     sub_object;       // +0x3C  cloned with operator new(0x58)
    MfDblList children;         // +0x40  vtable + three zeroed words
    void*     field_50;         // +0x50
    /* ... */                   // +0x54  constructed by 0x60AC1F
    int32_t   field_6C;         // +0x6C  = 0
    int32_t   field_70;         // +0x70  = 2
    int32_t   field_74;         // +0x74  = 1
    float     field_7C;         // +0x7C  = 1.0
};
```

`+0x20`..`+0x3C` is **the same 28-byte transform as a `.skl` joint key** — `pos[3]` then
`quat[4]` with w first, identified by `fld1` landing at `+0x2C`. See
[`skl.md`](skl.md#the-200-byte-payload--solved). The two formats share one transform type.

Write order, with writer offsets translated back to object offsets:

```
write_string(obj+0x18)              // node name
raw_write(0x1C, obj+0x20)           // the transform
write_u32(kind)                     // from obj+0x3C / +0x6C / +0x70
  kind == 2      -> write_u32(obj+0x3C)
  kind == 1 or 3 -> write_u32(*(obj+0x3C) + 8)
write_u32(obj+0x50 != 0)            // presence flag
  when set       -> write_subobject(obj+0x50)
write_u32(obj+0x4C)
for each entry in the list at obj+0x40:   // the child list
    raw_write(0x14, entry)                // 20 bytes per entry
write_object(obj+0x54)
write_u32(obj+0x6C)
```

So the child tree *is* serialized: the loop over `obj+0x40` walks the `MfDblList` and writes a
20-byte record per child. What those 20 bytes contain is the remaining unknown — child records at
that size cannot be whole nodes, so they are presumably indices or link records.

### A 20-byte prefix precedes every node name

Measured across all 1114 nodes in the 115 APFM files, the twenty bytes immediately before a node's
length prefix are:

```
u32   32          constant in 1114/1114
u32   32          constant
u32   1           constant
f32   1.0         constant
u32   N           varies: 1 (x528), 0 (x337), 2 (x57), 3 (x30), 4 (x15), 5 (x9), ...
```

Four of the five words are invariant, so this is a genuine per-node header rather than coincidence.

**`N` is not a child count.** The distribution is suggestive, but the hypothesis fails outright:
summing `N` over each file never equals `nodes - 1` (0 of 115 files), and reconstructing a tree by
treating `N` as a depth-first child count produces anatomically impossible skeletons — `LToes`
parenting `RLeg1` in Prince. Recorded here so the idea is not retried.

What `N` means, and where the 20-byte child records actually live, are both still open. The node
gaps in `Prince.act` run 140–1778 bytes and contain the rest of each node's serialized record
(the `new(0x58)` sub-object, the `obj+0x54` object, and the child list), which has not been
walked.

### Still undecoded

The preamble before the property records, and the serialized node block. Node **names** are
recoverable — each is length-prefixed and followed by four floats that decode as a rotation
quaternion (`Prime` in `PICK_Gem.act` yields `(0, 0.0615, 0, 1.0)`) — but the record stride, the
parent/child linkage and the surrounding fields are not. `src/assets/act.cpp` scans for plausible
names rather than walking a structure, and its `act_node` is marked provisional for that reason.

Joint limits *are* tagged and therefore locatable: `Prince.act` carries **11 `JLHn`**
(`MfJLHinge`, 48 bytes) records; one file carries `JLEu` (`MfJLEuler`). No `.act` contains `oNrP`,
`porP`, `baTP` or `tsLP`, so only value types and joint limits are tagged in this format.

### Functions named in the database

| Address | Name |
|---|---|
| `0x60087E` | `Act__open_and_sniff_magic` — opens the stream, compares against `APFM` |
| `0x605218` | `MfStream__read_u32` |
| `0x602E20` | `MfProtoNode__ctor` |
| `0x602F12` | `MfProtoNode__copy_ctor` — source of the layout above |
| `0x5FDD1C` | `MfProtoActor__ctor` — builds the default property table |

## How to confirm in IDA

1. Find the `MfProtoActor` RTTI type descriptor, then its complete-object locator, then its
   vtable, then xrefs to the vtable → constructor.
2. Search the string table for `"APFM"` as the 32-bit constant `0x4D465041` and for `"ActP"`
   (`0x50746341`). The comparison site is the format sniff at the top of the load function.
3. Look for a `switch` on `version` with cases `48`, `105`, `106` — that gives you the field
   deltas between revisions for free.
4. **Done** — the `MF_*` keys are registered by `MfProtoActor`'s ctor at `0x5FDD1C`
   (references at `0x5FDE99`–`0x5FE626`). It `operator new`s a 12-byte `Prop` and a 16-byte value
   object per property, matching `porP`(12) and `rtSV`/`olFV`(16) in the chunk registry.

---

# Skeleton hierarchy: what the data says, and what is still missing

Run `k2boy --bind <master.dat>` to reproduce everything below.

## `.act` transforms are local-to-parent, not bind poses

The first guess — that `.act` node transforms are world-space bind poses, so a
part's local geometry could be had by inverting them — is **wrong**. Measured on
the Prince: applying the inverse transform collapses only 5 of 26 parts toward
the origin, worst radius 1.80.

The positions say why:

```
Pelvis   act (0.000, 1.034, 0.000)     <- ~1m up: plausible as world
Mid      act (0.002, 0.185, 0.018)     <- 18cm: an offset, not a position
LHand    act (-0.013, -0.285, 0.017)   <- 29cm down the forearm
```

These are **local-to-parent offsets**. `.act` is a real skeleton, and using it
needs the parent links — which are not in the parsed data.

## The two files use different coordinate systems

| file | field | space |
|---|---|---|
| `.K2A` | `*NODE_TM` position | **world bind pose, Max Z-up** |
| `.act` | node transform | **local-to-parent, engine Y-up** |

The mapping is `engine = (x, z, -y)`, pinned by the Pelvis: `.K2A`
`(-0.002, 0.000, 1.033)` against `.act` `(0.000, 1.034, 0.000)`.

This also means the `.K2A` node transform, which the mesh builder ignores because
vertices are baked, *does* carry usable information: it is each part's world bind
position.

## Bind rotations are identity

Every `.act` bind quaternion on the Prince is exactly `[1, 0, 0, 0]`. Forward
kinematics over the bind pose is therefore pure offset addition, which is worth
knowing before writing any quaternion code.

## Deriving the hierarchy from geometry — works, but not reliably

Since `.K2A` gives world positions and `.act` gives local offsets, a parent can
be predicted: `parent_world ~= child_world - child_local`, then matched to the
nearest joint. On the Prince this produces an anatomically correct skeleton:

```
Pelvis (root) -> Mid -> ... -> LArm1 -> LArm2 -> LHand -> LFingers1 -> LFingers2
joints 26, roots 1, cycles 0, residual mean 0.0051
```

But it does **not** generalise:

| actor | joints | cycles | residual mean | FK worst |
|---|---:|---:|---:|---:|
| Prince | 26 | 0 | 0.0051 | 0.1111 |
| PG3Archer | 25 | 0 | 0.0176 | 0.1144 |
| A1 | 26 | **2** | 0.0136 | **1.5479** |

A1 comes out with cycles and 1.5 units of drift. **The hierarchy is not
recovered.** Nearest-neighbour parent matching is a heuristic, and it fails.

## Why forward kinematics drifts even when the tree is right

Some `.act` joints carry no geometry, so a geometry-derived tree cannot see
them, and their offsets get skipped or absorbed into the wrong link. Every actor
has the same handful:

| node | Prince | A1 | PG3Archer |
|---|---|---|---|
| actor root | `Prince` | `a1` | `Pg3` |
| `Prime` | yes | yes | yes |
| `DumChest` | `(0, 0.107, -0.018)` | `(0, 0.198, -0.005)` | `(0, 0.111, -0.014)` |
| grips | `LGrip`, `RGrip` | `SwordAttach` | `LGrip`, `RGrip` |

`DumChest` is the giveaway: it carries a real offset, and `LArm1`'s local
`y = 0.107` on the Prince is exactly `DumChest`'s. So the arms hang off
`DumChest`, not `Chest` — the derivation collapsed a joint it could not see, and
that is where the residual comes from.

`LGrip` / `RGrip` / `SwordAttach` are the weapon attachment points, which
cross-checks against `master.dat`'s hand list.

## Conclusion

Attaching body parts to joints and posing them needs the real parent links. The
remaining unknown is the **`.act` 20-byte child records**, still unrecovered (an
earlier child-count hypothesis failed on 0/115 files). That is the blocker for
animation, and guessing at it from geometry has now been tried and measured
rather than assumed.

## `MfProtoNode` in memory — where the children actually live

From `MfProtoNode__copy_ctor` (`0x602F12`), which touches every field:

```c
struct MfProtoNode {          // sizeof not yet pinned
/* +0x00 */ void*  vtbl_MfProtoNode_2;   // secondary base (MI)
/* +0x14 */ void*  vtbl_MfProtoNode;     // the MfStreamObject interface
/* +0x18 */ ...                          // copied by sub_616E49
/* +0x1C */ int32_t
/* +0x20 */ std::string                  // node name
/* +0x3C */ void*                        // deep-copied, new(0x58) + sub_60EEF7
/* +0x40 */ MfDblList                    // <-- doubly-linked list, 4 dwords
/* +0x50 */ void*                        // cloned through its own vtable slot 5
/* +0x54 */ ...                          // copied by sub_60AC3C
/* +0x6C */ int32_t kind;                // 1..6, selects the payload below
/* +0x70 */ int32_t
/* +0x74 */ int32_t
/* +0x78 */ void*   payload;             // sized by `kind`
/* +0x7C */ int32_t
};
```

**The list at `+0x40` holds 20-byte elements.** The copy constructor walks it
with the list's own virtuals (`+0x24` first, `+0x28` next, `+0x14` add) and for
each element does `operator new(0x14)` then copies **five dwords**. That is the
20-byte child record the earlier notes flagged as unrecovered — it is a linked
list on the node, not an array in the file.

`kind` at `+0x6C` selects a variant payload allocated at `+0x78`:

| kind | payload size | copy helper |
|---:|---:|---|
| 6 | `0x1C` (28) | `sub_60E0CD` |
| 5 | `0x8C` (140) | `sub_60DDDC` |
| 4 | `0x34` (52) | `sub_60DABC` |
| 3, 2, 1 | (not read) | |
| other | none, `payload = 0` | |

28 bytes for kind 6 matches the shared `pos[3] + quat[4]` transform already
documented.

## The type tags are runtime-only

`MfProtoNode__IsKindOfTag` (`0x602ED7`) answers to two 4CCs:

```
0x5072744E  'NtrP'   <- ProtoNode
0x54726565  'eerT'   <- Tree
```

`"Tree"` looked like the hierarchy chunk. **It is not on disk**: neither `eerT`,
`Tree`, `NtrP` nor `PrtN` appears anywhere in `Prince.act`. These are runtime
type-identity tags used by the streaming layer, not file markers. Recorded so
the next attempt does not re-follow it.

(`JLHn`, which occurs 11 times in `Prince.act`, is also a dead end — it falls in
the middle of a float run after a texture name, not on a record boundary.)

## Where this leaves animation

Known: the node layout, that children are a linked list of 20-byte records, the
variant payload table, the coordinate mapping, and that bind rotations are
identity. Unknown: **how that list is encoded in the file**. Until the on-disk
form of the `+0x40` list is found, the parent links cannot be read, and posing
body parts stays blocked.

## On-disk search for the parent links — where it is *not*

`k2boy --actnodes <file>` dumps each node record with its surroundings. On
`Prince.act` (31 nodes, 36306 bytes) the layout is:

```
u32 len, name, 28-byte transform, then either
    03 00 00 00  00 00 00 00  0b 00 00 00  NN 00 00 00   + a large payload   (25 nodes)
    all zeros                                            + a 140-byte payload ( 6 nodes)
```

The all-zero form belongs to exactly the nodes with **no geometry** —
`Prince`, `Prime`, `DumChest`, `LGrip`, `RGrip` — the same set identified from
the bind-pose work.

Three candidate locations for the tree were checked and **all are ruled out**:

| location | what it actually is |
|---|---|
| the trailing `[3, 0, 11, N]` block | `N` varies per node (8..56) but does not scale with the payload size; not a child count |
| the 140-byte payload of a meshless node | a **material block** — 1.0 and 0.5 floats, `20 00 00 00 20 00 00 00` (32x32) |
| the end of file | the `MF_` property table (`rtSV`, `3eVV`, ... `MF_locoSkill "WalkForwards"`) |

## The node order is a depth-first walk

```
Prince, Prime, Pelvis,
  LLeg1, LLeg2, LHeel, LToes,
  RLeg1, RLeg2, RHeel, RToes,
  Mid, DumChest,
    LArm1, LArm2, LHand, LThumb1, LFingers1, LFingers2, LGrip,
    RArm1, RArm2, RHand, RThumb1, RFingers1, RFingers2, RGrip,
    Neck, Head,
  Chest, Scabbard
```

`Mid -> DumChest -> LArm1` appears in exactly that order, which independently
corroborates the bind-pose finding that the arms hang off `DumChest` and not
`Chest`. So the file almost certainly encodes the tree as **traversal order plus
a per-node child or sibling count** — but the count has not been located, and
none of the fields above behaves like one.

**Status: still unrecovered.** What has changed is that the search space is much
smaller, and three plausible-looking candidates are now eliminated rather than
untested.

## The loader, traced

`sub_600A5B` is the `.act` node load:

```
600ab0  push 80h / operator new      -> sizeof(MfProtoNode) = 0x80 = 128
600ac7  call MfProtoNode__ctor
600ae0  call sub_6052BF              -> read the node from the stream
600ae8  call sub_5FEDAE              -> hand it to the actor
```

**`MfProtoActor` stores a single root node**, not a flat list: `sub_5FEDAE`
writes it to `actor+0x14`, releasing any previous one, and the node *array* at
`actor+0x18` is derived by traversing from that root. So the tree is encoded
**recursively inside the node record**, which is consistent with the depth-first
node order seen in the file.

`MfProtoNode__vf1` (`0x603E30`) is the node reader, and its order matches the
on-disk dump exactly:

```
sub_605251(stream, +0x04)          name
stream->vf0(0x1C, +0x0C)           28-byte transform   (pos[3] + quat[4])
sub_605205(stream, +0x5C)          kind                (3 for geometry nodes)
switch (kind) { ... }              kind-specific payload
sub_6052BF(stream, +0x40)          <-- the children list
```

`+0x40` is the `MfDblList` the copy constructor initialises. `MfDblList` is a
plain doubly-linked list: 12-byte links `{ void* value; next; prev }` with the
count at `list+0x0C`; vtable slot 1 is push-back, slot 3 is insert.

## Still not recovered, and why

`sub_6052BF(stream, obj)` calls `obj->vtable[1](stream)` and stores the result at
`stream+0x14`. Read literally against `vtbl_MfDblList`, slot 1 is *push-back*,
which would push the stream pointer into the list — that cannot be right, so the
`this` being passed is not the primary base.

`MfProtoNode` has two vtables (`+0x00` and `+0x14`, the documented MI pair) and
the field offsets quoted by `MfProtoNode__copy_ctor` and by
`MfProtoNode__vf1` **do not line up**, which means one of the two is working
through the adjusted `this`. Until that adjustment is pinned down, the offsets
above are provisional and the list's on-disk encoding stays unknown.

**Three attempts at this hierarchy have now failed**, each in a different way and
each recorded rather than quietly dropped:

1. a child-count hypothesis — 0/115 files;
2. geometry-derived parents — anatomically correct on the Prince, but cycles and
   1.5 units of drift on A1;
3. this pass — the recursive encoding is confirmed and the reader is located, but
   the `this`-adjustment ambiguity blocks the field mapping.

The next attempt should start by resolving which vtable `MfProtoNode__vf1`
belongs to, not by guessing at the record layout again.

## The `this`-adjust is resolved

The blocker named in the previous pass is settled. Every field quoted by
`MfProtoNode__vf1` sits exactly **0x14 below** the corresponding field in
`MfProtoNode__copy_ctor`:

| copy_ctor (primary) | vf1 | member |
|---|---|---|
| `+0x18` | `+0x04` | string |
| `+0x20` | `+0x0C` | 28-byte transform |
| `+0x40` | `+0x2C` | `MfDblList` |
| `+0x54` | `+0x40` | `MfPropertyTable` |
| `+0x6C` | `+0x58` | `kind` |
| `+0x78` | `+0x64` | kind payload |

So **`vf1` is a method on the `+0x14` subobject** (the `MfStreamObject` base),
while the constructors work from the primary base. The offsets were never
inconsistent; they were being read against two different `this` pointers.

## The reader, fully mapped

```
MfStream__ReadString(+0x18)            name
stream->vf0(0x1C, +0x20)               28-byte transform (pos[3] + quat[4])
MfStream__ReadDword(+0x6C)             kind
switch (kind) { ... }                  kind payload at +0x78
MfStream__ReadDword(&N)                <-- count
repeat N times:
    operator new(0x14)                 20-byte record
    zero [0],[4],[8] as floats, [0x10]
    stream->vf0(0x14, rec)             read 20 bytes
    MfDblList(+0x40)->push_back(rec)
MfStream__ReadObject(+0x54)            MfPropertyTable (the MF_ properties)
```

**The 20-byte records are count-prefixed and read straight from the stream.**
That is the structure the earlier notes called "the 20-byte child records", now
located in both the code and the load order.

## What the 20-byte record is not

It is **not** obviously a parent link. The reader pre-zeroes `[0]`, `[4]`, `[8]`
as *floats* and `[0x10]` as a dword, leaving `[0x0C]` untouched — so the shape is

```c
struct rec20 { float v[3]; uint32_t unknown_0C; uint32_t flags_10; };
```

A vec3 plus two dwords reads as attachment or anchor data rather than a child
reference. `[0x0C]` being the one field not pre-zeroed makes it the candidate
for a link id resolved in a later pass, in the style of `NiObject::LinkObject` —
but that is a hypothesis, not a finding.

`+0x54` was briefly a candidate for the child container; `sub_60AC3C` shows it
installs `vtbl_MfPropertyTable`, so it is the `MF_` property table this project
already parses. Ruled out.

## Where the child *nodes* are read is still unknown

`Act__LoadNode` builds one node and calls `MfProtoActor__SetRootNode`, yet
`Prince.act` has 31 nodes, and no node-creating loop appears in the node reader.
Something outside `vf1` walks the file. That is the next question, and it is now
a narrow one.

---

# The hierarchy is a recursive child list. Found.

The question left open across three previous attempts — *where are the child
nodes read* — is answered. It is the **last thing in every node record**, at the
tail of `MfProtoNode__ReadStream` (`0x603E30`):

```
604101  MfStream__ReadDword(&count)          ; child count
60410e  if (count <= 0) skip
loop count times:
60411e    operator new(0x80)                 ; a full MfProtoNode
604139    MfProtoNode__ctor
604151    ecx = child + 0x14                 ; the MfStreamObject subobject
604156    MfStream__ReadObject(child+0x14)   ; RECURSIVE - reads the whole subtree
60416a    MfProtoNode__AddChild(_, child)
604173  loop
```

So the on-disk node record is:

```
u32 len, name                     (length-prefixed, no NUL)
28 bytes                          transform: pos[3] + quat[4], w first
u32                               kind        (3 for geometry nodes)
...                               kind payload, size selected by kind
u32 N, N * 20 bytes               attachment records {vec3, u32, u32}
MfPropertyTable                   the MF_ properties
...                               version-gated fields (stream version >= 0x68)
u32 childCount
childCount * <this whole record>  <-- recursion
```

`MfProtoActor__ReadStream` (`0x600A5B`, **slot 1 of `vtbl_MfProtoActor`**) creates
exactly one `MfProtoNode` and calls `MfProtoActor__SetRootNode`, because the root
node pulls in the entire tree by itself. The flat array at `actor+0x18` is
derived afterwards by traversal, which is why counting `new(0x80)` call sites
never explained 31 nodes.

## Why this took four attempts

Worth recording, since each failure looked reasonable:

1. **child-count hypothesis** — matched 0/115 files. It assumed the count sat
   near the front of the record; it is at the *end*, after two variable-length
   sections.
2. **geometry-derived parents** — anatomically correct on the Prince, but cycles
   and 1.5 units of drift on A1. A heuristic, and it failed.
3. **`this`-adjust confusion** — the reader's offsets sit 0x14 below the
   constructor's because it runs on the `MfStreamObject` subobject. Reading them
   against the wrong base made the layout look self-contradictory.
4. **searching the data** — the trailing `[3, 0, 11, N]` block, the 140-byte
   material payload and the file tail were each checked and ruled out. All three
   were correct eliminations, but the answer was only reachable by reading the
   loader, because the recursion means the child data is not at any fixed offset.

The depth-first node order observed in `Prince.act` is exactly what a recursive
writer produces, and `Mid -> DumChest -> LArm1` falls out of it — which is why
that ordering observation was right even while the encoding was still unknown.

## Not yet done

The parser in `src/assets/act.cpp` still finds nodes by scanning for the
name+transform shape. It can now be rewritten to walk the record properly and
build the tree, which is the prerequisite for posing body parts. The
version-gated fields between the property table and the child count need pinning
down first — the reader branches on `stream[+4] >= 0x68`.
