# `.skl` — Motivate skill (animation + event track)

Location: `data/Actors/<Name>/Skills/*.skl` — 2363 files. `data/Actors/Prince/Skills/` alone holds 439.
Owning classes: `MfSkill`, `MfProtoSkill`, `MfPoSkill`, `MfLocoSkill`, `MfManipSkill`,
`MfJointKeyframe`, `MfJointKeyList`, `MfSoundKeyframe`, `MfTextureKeyframe`, `MfKeyList`.

**Status: `JKey` fully decoded — framing from the writer, field layout from the constructor,
validated at 99.89% across every record in the install.**

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

`SklK` (309 of 638) or `SklS` (329 of 638). Correlation with `kind` is not 1:1, so treat this as an
independent variant field. **Unresolved — resolve in IDA.**

## Chunk stream

After the name, the file is a flat sequence of 4CC-tagged records. Tags observed across all
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

So `vKey` binds a named scene node to a time-tagged state change. 2640 in the same units as the
`JKey` time field; if that unit is milliseconds the clip is 2.64 s, which is plausible for an ice
block melting. **Unit unconfirmed.**

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

### The 8-byte trailer

1796 of the 11 275 inter-record gaps are 220 rather than 212, and a handful are 228/236/244/252/276
— always 212 + a multiple of 8. The extra bytes are pairs of `uint32`:

```
(1, 1) x353   (1, 2) x312   (1, 4) x204   (1, 6) x200
(1, 5) x141   (1, 3) x131   (1, 7) x67    (1, 8) x53
```

Always `(1, N)` with small `N`. This is a count-prefixed list — almost certainly a joint or
channel index emitted by the owning `MfJointKeyList` between keyframes, not a field of `JKey`
itself. It does **not** correlate with the `flag` at +0xD0 (gap 220 occurs with flag 0 and flag 1
in the same proportion as gap 212).

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
