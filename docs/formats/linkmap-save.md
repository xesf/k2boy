# `*_linkmap.txt` + `*_save.bin` — level object persistence

Fourteen matched pairs sit in the game root, one per shipped level:

```
cityanddocks  cliffs  dirig1a  dirig1b  dirig2  dirigfinale  ivorytwr
moontemple    palace2 palace3  palace4  prisonfix  roof1  solar1
```

These are **generated at runtime**, not shipped content — their timestamps (21–22 Aug 1999) are
one day after the build, and `prisonfix_*` files carry a 2025 mtime because that level was loaded
most recently in this install. They are the engine's object-graph snapshot mechanism.

Owning classes: `RsrcListManager` (+ nested `SaveListClass`, `RoomsSaveListClass`,
`PrinceBodySaveListClass`, `PrinceLocSaveListClass`, `RegionParamsSaveListClass`,
`StreamBufferSaveListClass`, `StartClass`, `DataStreamClass`), `diskioSaveList`,
`diskioBufferedSaveList`, `diskioStreamedBufferedSaveList`, `HeaderSaveList`, `Link`,
`NiTArray<Link*>`.

---

## `*_linkmap.txt`

**Status: confirmed** (plain text).

```
linkmap size = 391
linkmap checksum = 75635040
object index 0 = SceneGraph
object index 1 = mScene
object index 2 = 3DWorld
object index 3 = geometry\rooms\solar1\solar1.lvl
object index 4 = geometry\rooms\solar1\S_burninggear_cha.k2a
object index 5 = UpdateHub
object index 6 = UpdateCollide
object index 7 = gear_SideLow
...
object index 390 = BillboardGeometry
```

Grammar:

```
"linkmap size = " <int> CRLF
"linkmap checksum = " <int32, may be negative> CRLF
( "object index " <int> " = " <name> CRLF )*
```

- `size` equals the number of `object index` lines (verified: solar1 391/391,
  cliffs 1773/1773, prisonfix 2381/2381).
- `checksum` is a signed 32-bit value (`prisonfix` = `-1484244416`). Algorithm unknown —
  candidate for confirmation via `HeaderSaveList`.
- Names are **not unique**: `Pivot`, `none`, `WaterLevel`, `BillboardGeometry`, `ForceCollider`
  repeat many times. The *index* is the identity; the name is a label.
- Index 0 is always `SceneGraph`, index 1 always `mScene`.
- Entries with a path (`geometry\rooms\<lvl>\<lvl>.lvl`, `...\*.k2a`) mark resource roots; the
  entries that follow are the nodes unpacked from that resource.
- `none` marks a null/placeholder slot.

This file is a **symbol table for the binary snapshot**: `save.bin` refers to objects by index,
`linkmap.txt` gives each index a name.

### `*_loadmap.txt`

`data/prisonfix_loadmap.txt` uses the identical grammar and is the *read-back* counterpart written when
the level is loaded (vs `linkmap` written when saved). Comparing the two for the same level is a
free consistency check on any decoder you write.

---

## `*_save.bin`

**Status: partial.**

```
solar1_save.bin      8 312 bytes    linkmap has   391 objects
cliffs_save.bin     43 492 bytes    linkmap has 1 773 objects
prisonfix_save.bin  56 540 bytes    linkmap has 2 381 objects
```

Head of `data/solar1_save.bin`:

```
00000000  01 00 00 00  00 00 00 00  2d 00 00 00  ff ff ff ff
          1            0            45           -1
00000010  ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff ff
...       (runs of -1)
00000034               1b 01 00 00  ff ff ff ff  ff ff ff ff
                       283
00000044  ff ff ff ff  1a 01 00 00  ff ff ff ff  16 01 00 00
                       282                       278
00000054  ff ff ff ff  0f 01 00 00  ...
                       271
```

Reading: after a 3-word header (`1`, `0`, `45`) the file is an array of `int32` object indices with
`-1` as the null marker. The values (283, 282, 278, 271, 267, 247, 246, 240, 183, 152, 151, 145,
105, 82 …) are strictly decreasing and all fall within `0..390`, i.e. **valid linkmap indices**.

So the leading region is an index→index link table: slot *i* holds the linkmap index of the object
that slot references, or `-1` for none. This matches the `Link` / `NiTArray<Link*>` classes and
explains the name "linkmap" — the text file exists purely to make this binary table debuggable.

Beyond the link table the file becomes per-object state (trap enabled/disabled flags, positions,
open/closed door states). Size scales roughly with object count but not linearly
(8312/391 ≈ 21 B/obj, 56540/2381 ≈ 24 B/obj), so records are variable-length or typed.

### Ratio check

`8312 = 4 * 2078`. `391` objects. If the link table were one `int32` per object it would be
1564 bytes, leaving 6748 bytes of state. Consistent with a fixed link table followed by a
variable-length state stream. **Unconfirmed — decode the header words first (`1`, `0`, `45`).**

---

## `*_tempActiveLog.txt` — the Rosetta stone for traps

`data/prisonfix_tempActiveLog.txt` (9583 bytes) is a leftover debug dump:

```
obj 0 trap type ForceTrap     name P6_room4_exitdoor.     triname P6_room4_exitdoor
obj 1 trap type EnemyStart    name P6PG2.                 triname EnemyIndicator
obj 2 trap type Pickup        name MoHealth.              triname MoHealth
obj 3 trap type Launcher      name Shooter01.             triname Shooter01
obj 4 trap type PressurePlate name NGon01.                triname NGon01
obj 5 trap type Area          name Endofthelevel.         triname Endofthelevel
obj 7 trap type Door          name P6_room4gate02.        triname P6_room4gate02
obj 12 trap type Action       name P6_AssanMouthAction.   triname P6_AssanMouthAction
obj 13 trap type Generic      name BoxPieces.             triname BoxPieces
obj 14 trap type AudioMessage name AnotherAmb.            triname AnotherAmb
```

Grammar: `obj <n> trap type <Type> name <Name>. triname <TriggerName>`

The `<Type>` values map 1:1 onto the `Builder@<Type>` RTTI classes listed in
[../ARCHITECTURE.md §3.4](../ARCHITECTURE.md#34-gameplay-objects--the-trap-entity-system).
`triname` is the trigger/geometry node name — `EnemyIndicator` for enemy spawns, otherwise the
object's own name.

This gives ground truth for a whole level's trap layout, which makes it the ideal validation set
when you decode `save.bin`: your decoder's output must agree with this file for `prisonfix`.

`data/prisonfix_FloorGenerationLog.txt` is empty apart from its title line (`Floor Generation Log:`) —
`PrinceFloor` / `FloorInfo` navmesh generation produced no diagnostics that run.

---

## How to confirm in IDA

1. Xref the format strings `"linkmap size = "`, `"linkmap checksum = "`, `"object index %d = %s"`
   (or however it is spelled) — that function writes the text file and sits next to the binary
   writer.
2. Xref `"obj %d trap type %s name %s. triname %s"` for the tempActiveLog writer; it iterates the
   active trap list and will hand you the `Trap` base-class layout (type tag, name, trigger name
   offsets) directly.
3. `RsrcListManager`'s nested `SaveListClass` variants each serialize one category. Enumerate
   their vtables to get the section order inside `save.bin`.
4. Test vector: `solar1` (391 objects, 8 KB) is the smallest pair — use it for every experiment.
