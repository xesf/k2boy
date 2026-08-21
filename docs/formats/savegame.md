# `.pop` savegame and `NIS.sav`

## `NIS.sav` — cutscene progress

**Status: confirmed.** 16 bytes total.

```
00000000  4e 49 53 2e 73 61 76 00  00 00 00 04 00 00 00 01
          "NIS.sav\0"             00 00 00 04  00 00 00 01
```

```c
struct NisSav {
    char     magic[8];   // "NIS.sav\0"
    uint32_t a;          // big-endian 4
    uint32_t b;          // big-endian 1
};
```

The two words are **big-endian**. That is not an anomaly: the `neighbors for room N` sections of
`.pop` savegames use big-endian `uint32` as well (see below, verified across 9 sections). So the
savegame writer emits integers MSB-first throughout, while every asset format in the game is
little-endian. Read save data big-endian.

An earlier revision of this page speculated these bytes were a little-endian bitmask of watched
cutscenes. With the byte order settled they simply read as the integers 4 and 1.

**Verify** by watching a cutscene and re-reading the file, or by xrefing the literal `"NIS.sav"`.

---

## `Quick Save.pop` — savegame

**Status: container framing confirmed for all 20 sections. `game header`, `prince location info`,
`enemy location info`, `rooms info` and `neighbors for room N` decoded; `stream buffer`
partially decoded; `module`, `resource list` and `prince body info` are not.**

14 017 bytes. A sequence of NUL-padded ASCII section names followed by binary payloads.

### Container framing — confirmed

Every section has the same shape:

```c
struct Section {
    char      name[];    // NUL-terminated ASCII
    uint32_be length;
    uint8_t   data[length];
};
```

Verified: `payload == 4 + length` for **all 20 sections** of `data/Quick Save.pop`, with no
exceptions. Sections run back-to-back with no padding, so the file can be walked
name-by-name without any index.

All integers, and the `game header` string, are **big-endian** — see the note under `NIS.sav`
above. Floats are big-endian IEEE-754 too.

### `game header` — confirmed

```c
struct GameHeader {          // length = 54
    uint16_be version;       // 2
    char16_be name[26];      // UTF-16BE, NUL-padded — "Quick Save"
};
```

The display name is **UTF-16 big-endian**, not little-endian as an earlier revision of this page
stated. It is a fixed 52-byte buffer NUL-padded to the end of the section, unlike the
length-prefixed 8-bit `MfString` used in the asset formats.

### Section table (in file order)

| offset | tag | owning class |
|---:|---|---|
| 0x0000 | `game header` | `HeaderSaveList` |
| 0x0046 | `module` | `moduleManager::SaveListClass` |
| 0x0071 | `K2BoyBodies` | — |
| 0x0082 | `resource list` | `RsrcListManager::SaveListClass` |
| 0x0103 | `region parameters` | `RegionParamsSaveListClass` |
| 0x011D | `prince location info` | `PrinceLocSaveListClass` |
| 0x0152 | `enemy location info` | — |
| 0x0282 | `prince body info` | `PrinceBodySaveListClass` |
| 0x029D | `rooms info` | `RoomsSaveListClass` |
| 0x03D3 | `resource list strings` | `RsrcListSaveStrings::SaveListClass` |
| 0x0801 | `stream buffer` | `StreamBufferSaveListClass` |
| 0x3572 | `neighbors for room 0..8` | `NeighborParser` — **decoded**, see below |

The section tags map **exactly** onto the nested `SaveListClass` RTTI names. That correspondence
alone tells you the writer is a loop over a registered list of savers, each emitting
`<name>\0<payload>`.

### `prince location info` / `enemy location info` — confirmed

**Both sections use the same 28-byte record.**

```c
struct ActorLocation {       // 28 bytes, big-endian
    uint32_be id;
    float     x, y, z;
    float     heading;       // radians
    uint32_be flag;
    uint32_be state;
};
```

`prince location info` holds exactly one record (`id = 1`); `enemy location info` holds ten
(`280 / 28 = 10` exactly, `id` running 11 down to 2). So the level has 11 tracked actors sharing
one id space, prince at 1.

The stride is confirmed by the clean decode: every record yields a plausible position and a
**round heading**. Angles come out as exactly 0.0°, 180.0°, 95.0°, -90.0°, 91.7°, 41.5° and
250.0° for the prince — degree-authored values stored as radians. A wrong stride would not
produce round degrees ten times over.

```
id  position (x, y, z)              heading   flag  state
 1  ( -50.14,  20.25,   11.22)      250.0°      -1      0     <- prince
11  (  -5.41,  19.25,    4.74)        0.0°       0      0
10  (  -1.88,  22.50,  -22.86)       95.0°       0      2
 9  (   7.08,  19.25,   -0.65)      180.0°       0      0
 8  ( -58.73,  19.76,   66.04)        0.0°       0      2
 7  ( -36.03,  18.30,  102.10)      180.0°       0      0
 6  (  -3.40,  19.25,   43.97)       41.5°       0      1
 5  ( -13.09,  23.26,   76.65)        0.0°       1      0
 4  ( -61.30,  20.30,   13.43)        0.0°       0      0
 3  ( -62.29,  20.28,   38.83)       91.7°       0      0
 2  (  20.21,   0.00,    1.92)      -90.0°       0      0
```

`y` clusters around 18–23 (floor heights) with one actor at 0.0. `state` takes values 0/1/2 and
`flag` is 0/1 for enemies and -1 for the prince; neither is decoded further.

### `rooms info` — partially confirmed

297 bytes = **81 + 216**, i.e. a 9-byte record per room followed by a 24-byte record per room.

```c
struct RoomHeader {          // 9 bytes x 9 rooms = 81
    uint8_t   flag;          // 0 for every room in this save
    uint32_be roomIndex;     // 0..8, sequential
    uint32_be neighbourCount;
};
// then 9 x 6 big-endian floats = 216 bytes
```

**Validated against the adjacency graph:** `roomIndex` is sequential 0–8 and `neighbourCount`
equals the entry count of the corresponding `neighbors for room N` section for **all nine rooms**
(1, 2, 2, 2, 4, 4, 1, 3, 3). Two independently-parsed sections agreeing on all nine values is
strong evidence the split is right.

The trailing 6 floats per room are **not** a min/max bounding box — in most rows the first triple
is greater than the second on at least one axis, so that reading is ruled out. They are in the
same coordinate space and magnitude range as the actor positions above. Meaning unknown.

### `resource list strings` — the most legible section

Plain paths, revealing the whole level asset manifest and, incidentally, the level build command:

```
prisonfix Test -a 2 -n prisonfix.nbr -b -v3.0 -k -w -t
geometry\rooms\prisonfix\prisonfix.lvl
Parts1to3
Parts4to6
Prince
geometry\\prince\\master.dat
geometry\\enemies\\PG1\\master.dat
geometry\\enemies\\PG2\\master.dat
geometry\\enemies\\PG3\\master.dat
geometry\\enemies\\PG4\\master.dat
geometry\\enemies\\PG4\\master_ContestWinner.dat
geometry\rooms\prisonfix\prison1.k2a
geometry\rooms\prisonfix\prison2.k2a
geometry\rooms\prisonfix\prison3.k2a
geometry\rooms\prisonfix\prison3_hall.k2a
geometry\rooms\prisonfix\prison3_junction.k2a
geometry\rooms\prisonfix\prison4_hall.k2a
geometry\rooms\prisonfix\prison4.k2a
geometry\rooms\prisonfix\prison5.k2a
geometry\rooms\prisonfix\prison6.k2a
```

Two findings worth acting on:

1. **`.nbr` — a neighbour/visibility file.** `prisonfix.nbr` is passed as `-n` to the level
   compiler and `NeighborParser` exists in the RTTI. This is the room-adjacency / PVS data. Not
   present in this install; expect it inside or beside the `.lvl`.
2. **Double backslashes** in the `master.dat` paths but single in the `.k2a` paths — two different
   path-building code paths, one of which escapes. Harmless, but it fingerprints which function
   wrote which string.

`PG4Contest` and `master_ContestWinner.dat` are a shipped Easter egg: a contest winner's likeness
as a palace guard skin.

### `stream buffer` — partially decoded

11 615 bytes, the bulk of the file. Owned by `RsrcListManager::StreamBufferSaveListClass`.

**This section is little-endian**, unlike everything else in the savegame. The sentinel
`0xDEADBEEF` appears as the bytes `EF BE AD DE`, and the prince's position decodes correctly only
when read LSB-first. The reason is structural: this is a verbatim dump of a Motivate
`MfStream` / `MfMemoryStream` buffer — engine-native, therefore little-endian like every asset
format — embedded inside the big-endian save container. **Switch byte order when you enter this
section.**

#### Layout

```
0x0000 .. 0x0ADE   12 actor blocks
0x0AF0 .. end      state region: length-prefixed HFSM / skill / sound names
```

`0xDEADBEEF` occurs **172 times** and acts as a chunk sentinel. Twelve of those are preceded by a
**doubled `uint32` length** — that is the per-block payload chunk:

```c
uint32_le size;      // repeated twice, identical
uint32_le size;
uint32_le 0xDEADBEEF;
uint8_t   payload[size];
```

Block starts (offsets into the section):

```
0x0004  0x00C5  0x01B0  0x029B  0x0386  0x0471
0x055C  0x0647  0x0732  0x081D  0x0908  0x09F3
```

Each block opens with `0xDEADBEEF`, ~108 bytes of mostly-zero fields, then two length-prefixed
strings — both `"END"` in every block, evidently the terminator of a property list — then the
sentinel + doubled-length payload chunk described above. Payload sizes seen are 36 and 24 bytes.

#### Block 0 is the prince — confirmed

Block 0's payload begins with three little-endian floats:

```
ce 8c 48 c2   c0 01 a2 41   49 99 33 41
  -50.1375       20.2509       11.2249
```

which is **exactly** the position in `prince location info` (stored there big-endian). That single
correspondence pins both the byte order and the block-0 identity.

The remaining 11 blocks do **not** put a position at the same offset — their payloads differ in
size and content — so the blocks are not a uniform array, and the block↔actor mapping beyond
block 0 is not established.

#### State region

From `0x0AF0` to the end, immediately after the last block, sit length-prefixed names — skills,
HFSM states and sound cues:

```
BasePose_L  AmbientType_G1  AmbientType_G4  AmbientType_G5  AmbientType_G6
Breathe  breathe  nexttime  once  onetime  zero  nosword  boxinplace  nobox
Empty  EmptyJaw  Hungry  Unit  swing  swing2  l_ar  PG4Contest  mulgateslams001
```

`BasePose_L` and `AmbientType_G*` are `.skl` filenames from `data/Actors/Prince/Skills/`;
`mulgateslams001` is an `data/Audio/sfx/` name; the lowercase words are HFSM state and variable
names, exactly what an `MfPoHFSM` snapshot would contain. So the game serialises each actor's
current state-machine state **by name**, not by index — which is why this section is so large.

#### Still unknown

The ~108-byte block header fields, the block↔actor mapping for blocks 1–11, and the framing of
the state region (the other 160 sentinels are in there and do not use the doubled-length form).

### `neighbors for room N` — **decoded and validated**

Nine sections, `room 0` through `room 8`, one per `prison*.k2a` room listed in
`resource list strings`. Each is the runtime form of the `.nbr` data — see
[`lvl.md`](lvl.md#nbr--room-neighbour--visibility-data).

```c
struct NeighborSection {          // follows "neighbors for room N\0"
    uint32_be length;             // == 5 * neighbourCount
    struct {
        uint32_be roomIndex;
        uint8_t   flag;           // the '*' marker from the .nbr source
    } entry[length / 5];
};
```

Verified on `data/Quick Save.pop`: the length header equals the actual payload size for **all
nine** sections, and every length is an exact multiple of 5.

The recovered adjacency graph for `prisonfix`:

```
room 0: 1
room 1: 0, 2
room 2: 1, 3
room 3: 2, 4
room 4: 3, 5, 7, 8
room 5: 4, 6, 7, 8
room 6: 5
room 7: 4, 5, 8
room 8: 4, 5, 7
```

**The graph is fully symmetric** — every `A -> B` has a matching `B -> A`, with no exceptions.
That is a strong independent check that the record size and byte order are right: a wrong stride
or endianness would not produce a symmetric relation by chance. Shape-wise it is a corridor
`0-1-2-3` opening into a fully-connected hub `{4,5,7,8}` with `6` hanging off `5`, which is what
you would expect of a prison level.

All `flag` bytes are 0 in this save, so the `*` marker is unexercised here; its meaning remains
unknown.

This section is what **validates the `.nbr` parser** decoded in [`lvl.md`](lvl.md): the 8-byte
in-memory `Entry {ptr, flag}` built by `NeighborParser__Parse` serialises to this 5-byte
`{roomIndex, flag}` record.

---

## How to confirm in IDA

1. Xref the literal `"game header"`. It will land in the save/load driver, which iterates a linked
   list of `SaveListClass` instances — walk that list to get the canonical section order and each
   section's reader/writer pair.
2. Each nested `SaveListClass` has RTTI (`SaveListClass@RsrcListManager`,
   `PrinceBodySaveListClass@RsrcListManager`, …). Vtable → `Save`/`Load` methods.
3. Xref `"NIS.sav"` for the cutscene bitmask.
4. `diskioBufferedSaveList` / `diskioStreamedBufferedSaveList` wrap the raw I/O; understanding
   their buffering is a prerequisite to byte-accurate offsets (there may be chunk length prefixes
   this static read did not surface).
5. Test vector: make two quicksaves that differ in exactly one thing (e.g. pick up one item) and
   diff them. Faster than reading the writer.
