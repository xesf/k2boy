# `.lvl` — level description

**Status: parser decoded from `LevelFileParser::Parse` at `0x441756`. No `.lvl` file is present in
this install, so nothing here is validated against real data** — everything below comes from the
code plus the one real command line preserved inside `data/Quick Save.pop`.

Referenced as `geometry\rooms\<level>\<level>.lvl` by every `*_linkmap.txt`.

## It is a text file

The decisive evidence is the class hierarchy:

```
StringParser -> FileParser -> LevelFileParser
```

That is the same base chain as every other **text** parser in the game — `CameraFileParser`
(`data/camera.dat`), `CommandLineParser` (`data/cmdline.dat`), `MenuParser` (`data/menu.dat`),
`UserTextFileParser` (`data/GameText.txt`), `PrinceMaster` (`master.dat`), `ConfigData` (`data/config.dat`),
`NeighborParser` (`.nbr`). No binary container class is involved.

> **Correction:** earlier revisions of these notes assumed `.lvl` was a binary chunk container and
> that the 16 `K2xx` tags in [`../reference/chunk-registry.md`](../reference/chunk-registry.md)
> were its object types. Both were wrong. Those tags are K2's Motivate *activity* classes.

## Structure

A `.lvl` has **two parts**: a one-line command line, then INI-style bracketed sections.

```
cistern -a2 -b -n cbors.k2a -v3.0 -k -t -w

[Bodies]
6
0 geometry\\prince\\master.dat
1 geometry\\enemies\\a1\\master.dat
...

[Rooms]
5
cisternsewer
cistern
...
```

`LevelFileParser::Parse` (vtable slot 2) handles **only the command line**. The bracketed sections
are consumed elsewhere — slot 0 (`0x441283`) or the caller at `0x441144`. That matches what was
predicted from the code: the flag dispatcher scans from the first `-` and never sees bare tokens.

### Bracketed sections

Counted over the 20 unique levels (some levels repeat a section):

| Section | Occurrences | Content |
|---|---:|---|
| `[Rooms]` | 46 | count, then one room name per line |
| `[Prince]` | 43 | start placement — `index x,y,z,heading` |
| `[Region]` | 33 | region definitions |
| `[Bodies]` | 20 | count, then `index <path to master.dat>` — the actor roster |
| `[ClipPlanes]` | 20 | near,far |
| `[Fog]` | 19 | density, r,g,b, flag |
| `[SkyColor]` | 19 | r,g,b |
| `[Enemy]` | 6 | enemy placements |
| `[Items]` | 5 | item placements |
| `[SkyDome]` | 5 | sky dome mesh |
| `[RotateSkyDome]` | 3 | animated sky |
| `[TextureAnimation]` | 3 | — |
| `[RotateTranslateSkyDome]` | 2 | — |
| `[PrinceMaterial]` | 2 | — |

`;` starts a comment; commented-out entries are common (e.g. disabled Prince start points).
`[Bodies]` resolves straight to the `master.dat` character descriptors documented in
[`text-and-tuning.md`](text-and-tuning.md#geometryprinceenemiesmasterdat--character-descriptor).

## Command-line syntax

`LevelFileParser::Parse` reads the file into a line buffer, then repeatedly does
`strchr(buf, '-')` and switches on the character that follows. Tokens are split on the delimiter
set `" ,\n\t\r"` (`asc_74CA34`). So the file is a free-form **command line**, exactly like
`master.dat` (`-v 5 -n Prince`) and `data/camera.dat` (`-v 2`) — whitespace and newlines are
interchangeable.

If no lines are read the parser logs `"failed to read any lines from file %s\n"`.

## Options

Recovered from the dispatch at `0x4417FE` (`'a'`–`'n'`) and `0x441A01` (`'p'`–`'w'`).
Field offsets are into the `LevelFileParser` object (`ebx`).

| Option | Argument | Effect |
|---|---|---|
| `-a <uint>` | unsigned | → `+0x30`; 0 if the number fails to parse |
| `-b` | none | flag → `+0x34` = 1 |
| `-c[+]` | optional `+` | flag set when followed by `+` |
| `-f[+ \| #<int>] [file]` | filename | **load FMS data** — see below |
| `-k[g]<0..3>` | digit | input file format selector → `+0x38`; a `g`/`G` before the digit sets `+0x3F` = 1. Values ≥ 4 are rejected with `"Failed to read input file format -k in ..."` |
| `-l` | none | flag → `+0x3D` = 1 (shares its handler with plain `-w`) |
| `-n[# \| +] <file>` | filename | **neighbour data** → name stored at `+0x50`; `#` sets `+0x45` = 1, no prefix sets `+0x44` = 1. Missing name → `"Filename required for neighbor data"` |
| `-p` | none | clears `+0x3E` |
| `-s shadow` | literal | `+0x40` = 3 and sets a byte on the attached object |
| `-s<uint>` | unsigned | stored on the attached object at `+0x1C` |
| `-t` | none | flag → `+0x3C` = 1 |
| `-v<maj>[.<min>]` | version | major → `+0x18`, minor → `+0x1C` |
| `-w` | none | flag → `+0x3D` = 1 |
| `-wp*` | literal `*` | `+0x48` = 0 and `+0x4C` = 1 |
| `-wp<uint>` | unsigned | → `+0x48`, also copied to the attached object at `+0x74` |

### `-f` — FMS data

The most interesting option, because it names a format not otherwise visible in this install:

- an optional `+` prefix, or `#<int>` giving a numeric parameter (a malformed `#` yields
  `"Bad option on FMS line"`)
- then a filename token; if it has no `.` extension, **`.fms` is appended**
- if no filename follows (next char is NUL or `-`), the default **`FMSdata.fms`** is used
- an empty resulting name gives `"Filename required for FMS-load data"`

`FMS` is Motion Factory's finite-state-machine database. The engine carries a matching class
family: `MfFSMManager`, `MfFSMList`, `MfFSMMutable`, `MfFSMNote`, `MfFSMNoteContents`,
`MfFSMPlayBack`, `MfFSMCameraSettings`, `MfFSMSpaceToBhvr`, plus `MfFiniteStateMachine` and the
whole `MfState*` / `MfTrans*` set. So `.fms` is the compiled behaviour database that the `.bhvr`
HFSM sources build into — see [`behavior.md`](behavior.md).

No `.fms` file ships in this install either.

## Validation against the one real command line

`data/Quick Save.pop` preserves the exact invocation used to build the `prisonfix` level
(see [`savegame.md`](savegame.md)):

```
prisonfix Test -a 2 -n prisonfix.nbr -b -v3.0 -k -w -t
```

Every token is accounted for by the table above:

| Token | Reading |
|---|---|
| `prisonfix` | bare token — level name |
| `Test` | bare token — second positional argument, purpose unknown |
| `-a 2` | `+0x30` = 2 |
| `-n prisonfix.nbr` | neighbour file, `+0x44` = 1 |
| `-b` | `+0x34` = 1 |
| `-v3.0` | version major 3, minor 0 |
| `-k` | no digit → `strtoul` returns 0 → input format 0 |
| `-w` | `+0x3D` = 1 |
| `-t` | `+0x3C` = 1 |

Nothing in that line is unexplained, and no option in the parser is contradicted by it. That is
the strongest check available without a real `.lvl`.

## What is still unknown

- The two leading positional tokens (`prisonfix`, `Test`). The parser only ever scans from the
  first `-`, so bare tokens must be consumed elsewhere — probably by the caller at `0x441283`
  (vtable slot 0) or `0x441144`.
- The meaning of each flag byte. Names above are offsets, not semantics.
- Whether room geometry (`*.k2a`) is listed inside the `.lvl` or discovered from the `.nbr`. The
  `resource list strings` section of `data/Quick Save.pop` lists nine `prison*.k2a` paths alongside
  `prisonfix.lvl`, so the association exists at runtime, but which file declares it is not
  established.

## How to continue

1. `LevelFileParser` vtable is at `0x74570C` (7 entries). Slot 2 is `Parse` (`0x441756`);
   slots 0 (`0x441283`) and 4–6 are inherited/other entry points — read `0x441283` for the
   positional-argument handling.
2. Constructor sites: `0x441144`, `0x4412FD`, `0x4413D1`, `0x441431`.
3. `sub_44438D` is the shared "read a token into a buffer" helper — its callers mark every place
   a filename or identifier is accepted.
4. `NeighborParser` — **done**, see below.

---

# `.nbr` — room neighbour / visibility data

**Status: validated against real data.** Decoded from `NeighborParser::Parse` at `0x4447A3`, then
confirmed two independent ways: **16/16** shipped `.nbr` files parse with the grammar below, and
the result agrees exactly with the serialised form in `data/Quick Save.pop`.

Loaded via the `.lvl` option `-n <file>` (e.g. `-n prisonfix.nbr`). Also text; same
`StringParser -> FileParser -> NeighborParser` chain.

## Syntax

```
#<roomName>
<neighbourName>
*<neighbourName>
#<roomName>
...
```

- A line starting with `#` opens a new room block.
- Every following line names one neighbour of that room, until the next `#`.
- A neighbour name may carry a leading `*`, which sets a per-neighbour boolean flag. **No shipped
  `.nbr` uses it** — consistent with every flag byte being 0 in the savegame.
- `;` starts a comment. Commented-out neighbours are very common (60 across the 8 unique files) —
  the level designers pruned visibility lists by commenting entries rather than deleting them.
- Room and neighbour names may be full paths with extensions — the parser strips the directory
  (`strrchr('\\')`) and truncates at the first `.`, so `geometry\rooms\prisonfix\prison3.k2a`
  and `prison3` are equivalent.

The parser skips the first character of every line before tokenising. Real files indent neighbour
names with spaces, so that skipped character is whitespace; for `#` lines it is the `#`.

## Algorithm

```c
n   = LevelGroup__GetRoomCount(level);      // 0x447CB8, on level+0xEC
buf = new Entry[n];                          // Entry is 8 bytes

while (ReadLine()) {
    if (line[0] == '#') {
        if (room && count) LevelGroup__CommitNeighbors(buf, count);  // 0x448C89
        count = 0;
        name  = StringParser__GetToken(line + 1);   // 0x519502
        if (!name)              error("missing room name");
        strip_dir_and_ext(name);
        room = LevelGroup__FindRoom(level, name);   // 0x446576
        if (!room)              error("failed to find room in level");
        if (room->field_3C)     error("Already have neighbors for room");
    } else {
        name = StringParser__GetToken(line + 1);
        if (!name)              error("missing neighbor room name");
        star = (name[0] == '*');
        if (star) name++;
        strip_dir_and_ext(name);
        nb = LevelGroup__FindRoom(level, name);
        if (!nb)                error("failed to find room in level");
        buf[count].ptr  = nb->field_14;
        buf[count].flag = star;
        if (++count == n)       error("Too many neighbors");
    }
}
LevelGroup__CommitNeighbors(buf, count);
delete[] buf;
```

`Entry` is 8 bytes: a pointer taken from the neighbour room's `+0x14`, then the `*` flag byte
(plus 3 bytes padding).

A room's neighbour list is capped at the **total number of rooms in the level** — exceeding it
raises `"Too many neighbors"`. `room->field_3C` is the already-populated marker, so each room may
appear as a `#` header only once.

The `"failed to find room in level"` error is suppressed when `level->field_1E4` is non-zero,
i.e. there is a lenient mode that tolerates unknown room names.

## Relationship to savegames

`data/Quick Save.pop` contains nine consecutive `neighbors for room 0` … `neighbors for room 8`
sections, and the same savegame lists nine `prison*.k2a` room files. So the runtime form of this
data is serialised per room into the savegame — see [`savegame.md`](savegame.md). Decoding those
sections is the way to *validate* this parser without a CD image, and is the obvious next step.

## Functions named in the database

| Address | Name |
|---|---|
| `0x4447A3` | `NeighborParser__Parse` |
| `0x441756` | `LevelFileParser__Parse` |
| `0x519502` | `StringParser__GetToken` |
| `0x446576` | `LevelGroup__FindRoom` |
| `0x447CB8` | `LevelGroup__GetRoomCount` |
| `0x448C89` | `LevelGroup__CommitNeighbors` |
| `0x44438D` | `FileParser__ReadTokenInto` |
