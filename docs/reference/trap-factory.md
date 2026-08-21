# Trap factory — registry keys and registration mechanism

**Status: confirmed** by decompilation of `data/pop3dreal.exe`.

## Mechanism

There is no static registration table. Each trap class has a nested `Builder` class with exactly
one global instance; the instance's constructor registers itself into a lazily-created global map
keyed by an ASCII name.

`TrapFactory::Builder::Builder(const std::string& name)` at **`0x4571C6`**:

```c
_DWORD *__thiscall TrapFactory__Builder__ctor(_DWORD *this, int name)
{
  *this = &vtbl_TrapFactory__Builder;
  if ( !g_TrapBuilderRefCount )            // 0x7F644C
  {
    if ( operator new(0x10u) )
      v3 = TrapBuilderMap__create(&v5, &v6);   // 0x45733A
    else
      v3 = 0;
    g_TrapBuilderMap = v3;                 // 0x7F6450
  }
  *(_DWORD *)TrapBuilderMap__operator_index(name) = this;   // 0x45736A
  ++g_TrapBuilderRefCount;
  return this;
}
```

Each derived builder ctor follows an identical 0x50-byte shape, e.g.
`Door::Builder::Builder()` at **`0x46BA2F`**:

```c
_DWORD *__thiscall Door__Builder__ctor(_DWORD *this)
{
  MSVC_string_ctor_cstr(aDoor);            // std::string tmp("Door")
  TrapFactory__Builder__ctor(this, tmp);
  MSVC_string_dtor(tmp);
  *this = &vtbl_Door__Builder;
  return this;
}
```

The map is a `std::map<std::string, TrapFactory::Builder*>` (16-byte control block, refcounted by
`g_TrapBuilderRefCount` so it is destroyed when the last builder unregisters).

### Named in the database

| Address | Name |
|---|---|
| `0x4571C6` | `TrapFactory__Builder__ctor` |
| `0x457256` | `TrapFactory__Builder__dtor` |
| `0x45733A` | `TrapBuilderMap__create` |
| `0x45736A` | `TrapBuilderMap__operator_index` |
| `0x7F6450` | `g_TrapBuilderMap` |
| `0x7F644C` | `g_TrapBuilderRefCount` |
| `0x404B54` | `MSVC_string_ctor_cstr` |
| `0x404E45` | `MSVC_string_dtor` |

## The 33 registry keys

**Important: the registry key is not always the class name.** Three classes register under a
shorter key, and the key is what level data uses.

| Registry key | Class | Builder vtable | Builder ctor | Key string |
|---|---|---|---|---|
| `Abyss` | `Abyss` | `0x746270` | `0x462249` | `0x7C1B14` |
| `Action` | `Action` | `0x746398` | `0x462E38` | `0x7BB79C` |
| `Aperture` | `Aperture` | `0x7464C0` | `0x463CB8` | `0x7BDAB0` |
| `Area` | `Area` | `0x7465F0` | `0x463FCA` | `0x7C1C9C` |
| `ArrowCharger` | `ArrowCharger` | `0x746718` | `0x464FAA` | `0x7C1D04` |
| `ArrowTarget` | `ArrowTarget` | `0x746844` | `0x4658C0` | `0x7C1D64` |
| **`Audio`** | `AudioMessage` | `0x74696C` | `0x466054` | `0x7BA374` |
| `Bar` | `Bar` | `0x746A94` | `0x466DF7` | `0x7BFC0C` |
| `CollapsingFloor` | `CollapsingFloor` | `0x746CD8` | `0x467889` | `0x7C1E88` |
| `Crate` | `Crate` | `0x746E08` | `0x468A54` | `0x7C1F84` |
| `CrateTarget` | `CrateTarget` | `0x746F3C` | `0x4695E7` | `0x7C1FCC` |
| `Door` | `Door` | `0x747180` | `0x46BA2F` | `0x7C205C` |
| `EnemyStart` | `EnemyStart` | `0x7473C4` | `0x46CA0C` | `0x7C213C` |
| `Fixture` | `Fixture` | `0x7474EC` | `0x46F567` | `0x7C2370` |
| `FlameThrower` | `FlameThrower` | `0x74762C` | `0x470C3E` | `0x7C2424` |
| **`Force`** | `ForceTrap` | `0x747754` | `0x471A9E` | `0x7C2478` |
| `Fountain` | `Fountain` | `0x747888` | `0x472E52` | `0x7C2660` |
| `Generic` | `Generic` | `0x747AF0` | `0x474403` | `0x7BDA90` |
| `Launcher` | `Launcher` | `0x747C1C` | `0x475A7E` | `0x7C2810` |
| `LightSwitch` | `LightSwitch` | `0x747DD8` | `0x476ED7` | `0x7C296C` |
| `Messenger` | `Messenger` | `0x747F00` | `0x4773FA` | `0x7C29B8` |
| `Mover` | `Mover` | `0x748150` | `0x47CC1F` | `0x7C2C7C` |
| `Pickup` | `Pickup` | `0x74827C` | `0x47DC6F` | `0x7BDA88` |
| `Pole` | `Pole` | `0x7483C0` | `0x48319D` | `0x7BFC10` |
| `PressurePlate` | `PressurePlate` | `0x7484E8` | `0x483803` | `0x7C323C` |
| `PrinceStart` | `PrinceStart` | `0x74872C` | `0x48706F` | `0x7C344C` |
| **`Room`** | `RoomAttributes` | `0x748854` | `0x4877B2` | `0x7C0BFC` |
| `Rope` | `Rope` | `0x74899C` | `0x487ED1` | `0x7BFBF4` |
| `SpecialEffect` | `SpecialEffect` | `0x748AC8` | `0x488BE5` | `0x7C35B4` |
| `Specials` | `Specials` | `0x748BF0` | `0x48958F` | `0x7C3608` |
| `Spikes` | `Spikes` | `0x748D18` | `0x48A539` | `0x7C36A0` |
| `WallBlade` | `WallBlade` | `0x748F68` | `0x48B287` | `0x7C3740` |
| `Water` | `Water` | `0x7490A8` | `0x48BE00` | `0x7B9CD4` |

Base class `TrapFactory::Builder` vtable: `0x7460D0` (does not self-register).

### The two-name discovery

`data/prisonfix_tempActiveLog.txt` prints `trap type AudioMessage` and `trap type ForceTrap`, but the
factory keys are `Audio` and `Force`. So each trap class carries **two** names:

- the **registry key** used by level authoring / `.lvl` data (table above)
- a **display name** returned by a virtual, used by the debug log

Evidence for the second: `ForceTrap`'s RTTI region holds both strings adjacently —

```
0x7C2460  ".?AVBuilder@ForceTrap@@\0"
0x7C2478  "Force\0\0\0"          <- registry key
0x7C247E  "ForceTrap\0"          <- display name
```

`Audio`'s key at `0x7BA374` sits inside an unrelated category string table
(`Skill`, `A.I.`, `Tool`, `Audio`, `Level`, `Forces`, `Textures`, `Wind`) — the linker pooled it.
That pooling is why the key looks out of place; it is nonetheless the string passed to the ctor
(verified by decompiling `0x466054`).

**Consequence for tooling:** a `.lvl` parser must match on the registry key, not the class name.
A log parser must match on the display name. Do not assume they are the same.

## Trap types actually present in `prisonfix`

From `data/prisonfix_tempActiveLog.txt` (16 of the 33 types used in that level):

```
Action  Area  AudioMessage  CollapsingFloor  Crate  Door  EnemyStart  Fixture
ForceTrap  Generic  Launcher  Mover  Pickup  PressurePlate  SpecialEffect  Specials
```

## Cross-reference

`data/Traps.txt` in the game root documents the parameter slots for most of these types — see
[../formats/text-and-tuning.md](../formats/text-and-tuning.md#trapstxt--trap-parameter-reference-design-doc).
Its headings use the **class** names (`ForceTrap`, `AudioMessage`), not the registry keys.

## Next step

Decompile `TrapFactory::Build` (the consumer of `g_TrapBuilderMap`) to see how a level record
selects a builder and how parameter slots are passed. Start from xrefs to `g_TrapBuilderMap`
(`0x7F6450`).

## Trap names in shipped data vs. recovered classes

Every `*K2_TRAP_NAME` across the whole install: **34 distinct values, 31 of which
resolve to a `Trap` subclass** in the `NiRTTI` hierarchy (after the three
key→class aliases above). Independent confirmation of both the factory table and
`docs/reference/nirtti-hierarchy.md`.

Three do not resolve:

| name | instances | exact string in `pop3dreal.exe` |
|---|---:|---|
| `WayPoint` | 301 | yes |
| `Trigger` | 25 | **no** |
| `Kill` | 25 | yes |

`WayPoint` is by far the most common trap name in the data yet is not a `Trap`
subclass — most likely consumed by a different subsystem (AI navigation) that
reuses the `K2_TRAP_BLOCK` syntax as a placement mechanism. `Trigger` does not
appear in the binary at all, so it is presumably authoring-only and silently
ignored at load. **Neither reading has been verified against the code** — they
are the obvious explanations, not established facts.
