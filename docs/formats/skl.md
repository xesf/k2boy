# `.skl` — Motivate skill (animation + event track)

Location: `data/Actors/<Name>/Skills/*.skl` — 2363 files. `data/Actors/Prince/Skills/` alone holds 439.
Owning classes: `MfSkill`, `MfProtoSkill`, `MfPoSkill`, `MfLocoSkill`, `MfManipSkill`,
`MfJointKeyframe`, `MfJointKeyList`, `MfSoundKeyframe`, `MfTextureKeyframe`, `MfKeyList`.

**Status: the whole file is decoded as the object graph the readers build, and all 2363 files
parse to exactly EOF.** The one exception is the manipulation block at the end of the 135 `MSkl`
files, which playback does not need. See [Object graph](#object-graph--the-real-structure).
`JKey` field layout validated at 99.89%; `MfJointPath` rotations at 100% (1 942 461 keys).

A "skill" in Motivate is an animation clip plus its attached event tracks (sound triggers,
visibility toggles, texture swaps). It is the unit the locomotion planner schedules.

## Header

```c
struct SklHeader {
    char     magic[4];   // "kSFM"  — 'MFSk' LE: Motion Factory Skill
    char     kind[4];    // "Skil" | "MSkl" | "LSkl"
    uint32_t version;    // 106 (632 of 638 sampled), 105 (6)
    char     chunk[4];   // "SklK" | "SklS"
    uint32_t unk;        // 1 in all samples
    MfString name;       // u32 length + bytes, matches the filename stem
    // ... chunk stream
};
```

### `kind` — skill class

| tag | meaning | naming convention |
|---|---|---|
| `Skil` | generic skill | `Melt.skl`, `Open12.skl` |
| `MSkl` | **manipulation** skill — `MfManipSkill` | filenames end `_M` (`Push_M`, `LedgeGrabHoriz_M`) |
| `LSkl` | **locomotion** skill — `MfLocoSkill` | filenames end `_L`, `_L2`..`_L4` (`Running_L`, `CombatMoveLeft_L3`) |

The `_G` suffix (e.g. `Stand_G`, `AmbientType_G1`) appears on both `MSkl` and `Skil` files, so it
is a *content* convention (gesture / general), not a container discriminator. The numeric suffix on
locomotion skills (`_L2`, `_L3`, `_L4`) tracks combat stance level.

### `chunk` — first chunk tag

**Resolved.** The tag picks how joints are stored, read into `MfSkill +0x08`:

| tag | joints stored as | files |
|---|---|---:|
| `SklK` | `MfJointKeyList` per joint: `JKey` spline keyframes | 318 |
| `SklS` | `MfJointPath` per joint: one sampled key per frame | 2045 |

Every Prince locomotion clip is `SklS`. `SklK` is used by props and traps.

## Object graph — the real structure

The file is not a flat chunk stream. It is one `MfStream` object, read field by field, and the
4CC tags only appear where a reader calls `MfStream__get_object_maybe` (`0x60541F`). Reading it as
a tag scan cannot see `SklS` joint data at all, because `MfJointPath` records carry no tag.

```
"kSFM"                              file magic
"Skil" | "LSkl" | "MSkl"            object tag -> creator

MfSkill base  (0x60C1ED)
  u32   version                     into MfStream +0x04; gates every later field
  4cc   chunk                       "SklK" | "SklS"                       +0x08
  u32                               +0x14
  str   name                        u32 length + bytes                    +0x10
  u32                               v >= 0x66                             +0x0C
  u32   joint_count                 = the actor's .act node count         +0x18
  SklK: joint_count x { u32 present; if present: MfJointKeyList }
  28 B  root transform              pos[3] + quat[4], v > 0x24            +0x20
  u32   interpolate                 v > 0x25; the sampler's blend flag     +0x44
  u32                               v > 0x25                              +0x48
  u32   has_refs                    v >= 0x66; 0 in every shipped file
  16 B                              v >= 0x67                             +0x4C

Skil  (0x623CE6)
  SklS: joint_count x { u32 present; if present: MfJointPath }           +0x7C
  u32 present; MfKeyList                          sound keys              +0x60
  u32 present; MfKeyList                          v > 40, event keys      +0x68
  u32 n; n x MfTextureKeyList                     str, u32, u32, MfKeyList
  u32   duration_ms                 v > 25                                +0x80
  u32                               v >= 34, else 1                       +0x84

LSkl  (0x63D3A0) — after Skil
  v >= 0x17: f32 x5 (+0x98..+0xA8), u32 n (+0xAC), n x 8 bytes (+0xB0)
  u32 n2; v >= 0x11: u32 (+0xB4), u32 (+0xB8)
  n2 x record: 0x14C bytes (v > 0x17), 0xFC (v 0x11..0x17), 0x0C (older)

MSkl  (0x63F4DD) — after Skil: manipulation targets. Not decoded.
```

`MfKeyList` (`0x61196A`): `u32 count`, then `count` tagged objects. A tag of `-2`, or any tag
without a registered creator (`0` in practice), is a null entry and consumes only the tag. `-1`
escapes to a real tag followed by a size word.

### `MfJointPath` — the `SklS` joint ("`JPat`", reader `0x640E59`)

```
u32  interval_ms     +0x04   33 in 13 676 of 14 555 paths (the ctor default 0x21)
u32  npos            +0x08   position keys
u32  nrot            +0x0C   rotation keys
u32  frames          +0x10   clip length in frames
npos  keys, run-length encoded: { i16 repeat; f32 pos[3]  }
nrot  keys, run-length encoded: { i16 repeat; f32 quat[4] }   w first
```

Before version `0x20` a single `frames` word stands for all three counts.

The writer runs a compaction pass first (`0x640CF8`): it drops trailing keys equal to the last
one (positions within a squared distance, rotations by `sub_6128D4`). So a track can be shorter
than `frames`, and a track of one key is a constant. Six paths in the install are longer than
their clip.

The joint index is the actor's `.act` node index. For the Prince that is 31 slots; joint 0, the
`Prince` root node, carries the root motion — `Running_L` travels 3.6 units along +Z in 24
frames, `Walk_L` 1.77 units in 34.

### Sampling (`0x640B01`)

```c
frame = time_ms / interval_ms;                   // unsigned
ri = min(frame, nrot - 1);  pi = min(frame, npos - 1);
if (!interpolate) return { pos[pi], rot[ri] };
t = (float)(time_ms % interval_ms) / interval_ms;
rot = (t != 0 && ri != nrot - 1) ? slerp(rot[ri], rot[ri + 1], t) : rot[ri];
pos = (t != 0 && pi != npos - 1) ? lerp(pos[pi], pos[pi + 1], t)  : pos[pi];
```

`time_ms == 0` returns key 0. The slerp (`0x612B97`) takes the short arc (negating the second
key when the dot product is below 0), falls back to a linear blend when `1 - cos <= 1e-11`, and
renormalises. Every caller passes the skill's `+0x44` as the `interpolate` flag. A joint with no
path keeps its own node transform (`MfNode +0x28`, see `0x63BA81`).

The locomotion set is named in a table at `0x749C30`: `BasePose_L`, `Running_L`, `Walk_L`,
`AmbientType_G`. The last is a stem that gets a random suffix (guards use 1..6, `0x4BDDA4`).

## Chunk stream (historical tag survey)

Kept for the counts. The framing described here was a tag scan and is superseded by the object
graph above. Tags observed across all
2363 files (counts over a 638-file sample):

| tag | count | meaning |
|---|---:|---|
| `JKey` | 10 026 | joint keyframe list (`MfJointKeyframe`) |
| `vKey` | 977 | visibility / value key |
| `SKey` | 649 | sound key (`MfSoundKeyframe`) |

No other tags occur in shipped `.skl` data. `TKey` (`MfTextureKeyframe`, 228 bytes) and `LKxT`
(`MfTextureKeyList`, 32 bytes) **are** registered by the engine — see
[`../reference/chunk-registry.md`](../reference/chunk-registry.md) — they are simply unused by
this game's animation set.

Records appear to be preceded by a `uint32_t count`, then the tag, then the payload. In
`FIX_IceBlock/Skills/Melt.skl` (756 bytes):

```
0x0038  02 00 00 00  "JKey"      count=2, then joint-key payload
0x0110  "JKey" 50 0a 00 00       second joint key, time = 0x0a50 = 2640
0x0280  01 00 00 00  "SKey"      count=1, sound key
0x02C4  01 00 00 00  "vKey"      count=1, visibility key
```

### `SKey` — sound key (best-understood record)

```
0x0284  "SKey"
0x0288  00000000              time / frame (0 = start)
0x028C  00000000
0x0290  00000080              -0.0f
0x0294  00000000  00000000
0x029C  ffffffff              -1 (sentinel: "no channel" / "no loop")
0x02A0  00000000
0x02A4  00000001              1
0x02A8  00000000
0x02AC  10000000              string length = 16
0x02B0  "Iceblockmelt.wav"    sound name — note ".wav", resolved to Audio/sfx/*.sfx
0x02C0  00000001  00000001
```

The `.wav` extension in the payload versus the `.sfx` files on disk means the loader does an
extension swap or an extension-insensitive lookup. Worth confirming — it affects any asset
repacker.

### `vKey`

```
0x02C8  "vKey"
0x02CC  00000000
0x02D0  00000001
0x02D4  00000000
0x02D8  0000000c  "Iceblockmelt"   target node name (len 12)
0x02E8  00000000
0x02EC  00000a50                    time = 2640 (same as the 2nd JKey → end of clip)
0x02F0  00000001
```

**Correction.** The reader (`0x6407E9`) is the `MfKeyframe` base (time, flag), then a `u32` value
and the target name, and nothing more. The record ends at the name. The `0, 0x0A50, 1` that follow
belong to the enclosing `Skil`: an empty texture-key-list count, then **`duration_ms` = 2640**, then
`+0x84`. An earlier revision read 2640 as the event's time. The unit is milliseconds: `SklS` clips
confirm it: `duration_ms == (frames - 1) * interval_ms` in 2021 of 2045 `SklS` skills (for example
`Running_L`: 24 x 33 = 792). The 24 exceptions are short clips whose paths use 8 or 16 ms intervals.

The `00000001 00000001` after the `SKey` name likewise belongs to `Skil`: the "event list present"
flag, then that list's count.

### `JKey` — joint keyframe

**Status: framing confirmed from the binary and verified against data. Field *meaning* still
open.**

The record is written by `MfJointKeyframe`'s slot-2 override (`0x6123F7`), which is short enough
to quote in full:

```asm
MfJointKeyframe__write:
    call  MfKeyframe__write          ; 0x61250B — base class
    push  this+0x0C
    push  0C8h                       ; 200 bytes
    call  [stream_vtable+4]          ; raw write
    call  MfStream__flush            ; 0x6051AD
```

and the base class writes exactly two words:

```asm
MfKeyframe__write:
    push  [this+0x04]
    push  [this+0x08]
    call  MfStream__write_u32        ; writes this+0x08
    call  sub_605331                 ; writes this+0x04
```

So the on-disk record is:

```c
struct JKeyRecord {          // 4 + 4 + 4 + 200 = 212 bytes
    char     tag[4];         // +0x00  "JKey", written by the container
    uint32_t time;           // +0x04  from object +0x08
    uint32_t flag;           // +0x08  from object +0x04 — 0 or 1 in all records
    uint8_t  raw[200];       // +0x0C  a straight memory dump of object +0x0C..+0xD4
};
```

212 matches `sizeof(MfJointKeyframe)` = `0xD4` and the record size measured across all 11 566
records in the install. `+0x08` is 0 in 11 037 records and 1 in 529 — a boolean, not a float.

**Correction.** An earlier revision of this page placed the float payload at `+0x08` and described
it as `block[6][7]` starting there. That was off by four bytes: `MfKeyframe` writes *two* words
before the raw block, so the payload begins at **`+0x0C`**. Any offset arithmetic taken from the
previous version needs redoing.

#### The 200-byte payload — **solved**

Because the payload is written as one raw `memcpy`, the in-memory layout *is* the file layout, so
the answer was a struct definition rather than a decoder. It came from `MfJointKeyframe`'s
constructor (`0x611A48`) plus the constructor of the 28-byte element it builds arrays of.

The constructor allocates two arrays with an `eh vector constructor iterator`, **2 elements of
0x1C bytes** each, and that element has its own two-part constructor:

```asm
sub_404A0E:                     ; the 28-byte element
    call sub_404924             ; 3 floats at +0x00  <- zeroed
    lea  ecx, [esi+0Ch]
    call sub_404973             ; fld1 at +0x0C, then 3 zeros
```

So the element is:

```c
struct transform {      // 28 bytes
    float pos[3];       // +0x00
    float quat[4];      // +0x0C — w, x, y, z.  W IS FIRST
};
```

`sub_404973` writing `fld1` into the *first* slot is what settles the component order: the
identity quaternion is stored `(1, 0, 0, 0)`, not `(0, 0, 0, 1)`.

#### Verified layout

```c
struct JKeyRecord {          // 212 bytes on disk
    char      tag[4];        // +0x00  "JKey"
    uint32_t  time;          // +0x04  (object +0x08)
    uint32_t  flag;          // +0x08  (object +0x04) — 0 or 1
    uint32_t  field_0C;      // +0x0C
    transform xf[6];         // +0x10, +0x2C, +0x48, +0x64, +0x80, +0x9C
    float     vec3[3];       // +0xB8
    uint32_t  field_C4;      // +0xC4
    float     ease_in;       // +0xC8  0.666667
    float     ease_out;      // +0xCC  0.333333
    uint32_t  field_D0;      // +0xD0  0 or 1
};
```

**Validation: the quaternion at `xf[k].quat` is unit-norm in 69 318 of 69 396 channels — 99.89%**,
across all 11 566 `JKey` records in the install. Per transform slot:

```
transform 0   99.70%      transform 3   99.68%
transform 1  100.00%      transform 4  100.00%
transform 2   99.97%      transform 5   99.97%
```

The 78 exceptions (0.11%) are presumably degenerate or uninitialised keys.

Sample from `swing.skl`, a pendulum, showing exactly what you would expect — a fixed offset with
rotation varying frame to frame about a single axis:

```
pos=(0.000, -7.034, 0.000)   quat w=0.99936  xyz=(0, -0.03566, 0)
pos=(0.000, -7.034, 0.000)   quat w=0.99943  xyz=(0, -0.03381, 0)
pos=(0.000, -7.034, 0.000)   quat w=1.00000  xyz=(0, -0.00074, 0)
```

#### Why the earlier attempts failed

Worth recording, because the failure mode was subtle. Three readings were tried and rejected by
bulk test before this one:

| Attempt | Groups at | Quaternion taken as | Result |
|---|---|---|---|
| 1 | `+0x08` | `float[3..6]` | boundary wrong by 4 — `-1.0935` looked like a bad quat component |
| 2 | `+0x0C` | `float[3..6]` | 47% / 30% |
| 3 | `+0x10` | `float[0..3]` | 11.6% |
| **4** | **`+0x10`** | **`float[3..6]`** | **99.89%** |

Attempt 2 was close enough to be tempting and still wrong. The lesson is that `sizeof` and record
stride can be right while the internal boundary is off, and only the constructor settles it —
guessing from the data alone kept producing plausible-but-false readings.

#### What the six transforms are

Not established. The constructor groups them as *(standalone, array[2])* twice — `+0x10` then
`+0x2C..+0x64`, and `+0x64` then `+0x80..+0xB8` — so the shape is two sets of
`{ value, tangent, tangent }`. Combined with `ease_in` / `ease_out` at `+0xC8`/`+0xCC` this reads
as spline control data, but which set is which, and what the trailing `vec3` at `+0xB8` is, are
open.

### The 8-byte trailer — resolved

The `(1, N)` pairs between `JKey` records are `SklK` framing: the next joint's `present` flag,
then its `MfJointKeyList` count. They belong to the per-joint loop in `0x60C1ED`, not to `JKey`.

## How to confirm in IDA

1. `MfJointKeyframe`, `MfJointKeyList`, `MfSoundKeyframe`, `MfKeyList`, `MfKeyframe` all carry
   RTTI. Locate their vtables → constructors → the stream-read function each calls.
2. **Done** — the tag→creator table is recovered in
   [`../reference/chunk-registry.md`](../reference/chunk-registry.md). Note `"kSFM"`
   (`0x4D46536B`) has **no** code site: the file magic is never compared as an immediate. The
   per-chunk tags do: `JKey` `0x79654B4A` at `0x611A33`, `SKey` `0x79654B53` at `0x640575`,
   `vKey` `0x79654B76` at `0x6407D4`, `Skil` at `0x623CD1`, `LSkl` at `0x63D38B`,
   `MSkl` at `0x63F4C8` — all inside static self-registration thunks, not a dispatch switch.
3. Compare a `LSkl` and an `MSkl` read path; the divergence tells you what locomotion-specific
   fields (stride length, root motion delta) are appended.
4. `data/Actors/FIX_IceBlock/Skills/Melt.skl` (756 bytes, 2 JKey + 1 SKey + 1 vKey) is the smallest
   useful specimen — use it as the debugger test vector.
