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
