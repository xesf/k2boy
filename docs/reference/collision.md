# Collision: the pick primitive

How the Prince finds the floor and the walls. Recovered from `data/pop3dreal.exe`;
ported in `src/engine/collision.*`.

## There is no navmesh

`PrinceFloor`, `FloorInfo` and the empty `data/prisonfix_FloorGenerationLog.txt` suggest a
generated floor graph. The movement code does not use one. Every ground, wall and ledge
test in the Prince's motion code (`sub_4CE0F0`, `sub_4CFB3E`, `sub_4D0980`, and others; 24
call sites) goes through a single raycast:

```c
// 0x512183
bool pick(int mask, vec3* hit, const vec3* origin, const vec3* dir, bool front_only);
```

`sub_4CE0F0` is typical. It casts with mask 6, then accepts the hit only if
`|hit - origin|` is under a limit. Its failure path prints
`"INTERSECT FAILED (Checking Down)!!!!!\nProbable problem: Missing a floor."`.

## The chain

| address | role |
|---|---|
| `0x512183` | copies `origin` and `dir`, calls `0x51215A`, copies the hit point out |
| `0x51215A` | resolves `mask` to a node set (`0x41F7ED`), then picks |
| `0x511F96` | configures an `NiPick` (see below), calls `0x511E58`, builds the result |
| `0x511E58` | loops over the node set, picks each, and keeps the **nearest** hit. The running minimum is `flt_89F600`, seeded with `FLT_MAX` (`0x7F7FFFFF`) |
| `0x5120ED` | hit normal, computed from the hit triangle's three vertices |

`0x511F96` calls eight `NiPick` setters in a row: `(0, 0, 1, 0, front_only, 0, 0, 0)`. In NetImmerse
2.3 member order these read as pick type, sort type, intersect type = triangles, coordinate
type, **front only**, observe app-cull flag, return texture, return normal. The front-only reading
is inferred from that order. It is consistent with the data: in every room tested, a front-only
down-cast from the spawn point finds the floor.

## Masks select node categories

`0x41D8C9` maps `mask` to calls of `0x41D9E9(list, flag_bits, category, rings, room)`. Each
call collects one category from the current room (`LevelGroup +0x1E8`):

| mask | categories | rings | used for |
|---:|---|---:|---|
| 0 | 0, 2 | 1 | |
| 1 | 0, 2 | 2 | |
| 2 | 0, 1 (flag bits 2) | 3 | |
| 3, 6 | 0, 2 | 3 | floor and wall probes (`sub_4CE0F0` uses 6) |
| 5 | 0, 1 | 3 | |
| 4 | everything (`0x41E55D`) | — | |

The per-category node arrays live on the `LevelGroup`:

| category | nodes | per-node flags |
|---:|---|---|
| 0 | `+0xF4` | `+0x10C` |
| 1 | `+0xFC` | `+0x110` |
| 2 | `+0x104` | `+0x114` |

Each room keeps four index lists per category: `+0x1FC..` for category 0, `+0x21C..` for 1 and
`+0x23C..` for 2. `0x4184C2` walks the first `rings` of those four lists and adds every node whose
flag word has a bit in common with `flag_bits`. These are presumably the room itself, then wider
neighbour sets.

Each room fills those lists in `0x4152EE` → `0x4153D0`, once per ring. Rings are distance
shells, not graph neighbours. The room's bound is grown by a per-ring margin and collided against
the level-wide array for that category (horizontal and vertical margins: ring 0 `5, 0.5`; ring 1
`5, -2`; ring 2 `8, -2`; ring 3 `10, 10`). Every node it touches is kept.

### Which node lands in which category

The level-wide arrays are copied (`0x41AC53`) from a load-time collector (`dword_7E9BF0`) that
`LevelFileParser__Parse` creates. Three adders feed it:

| adder | list | category | called for |
|---|---|---:|---|
| `0x41FF5D` → `0x41FF71` | `+0x04` | 0 | the `RoomGroup`'s imported `.k2a` (`0x449546`, node `+0xD0`); a Generic trap that is Visible and Collideable |
| `0x420266` → `0x420293` | `+0x0C` | 1 | a Generic trap that is Visible but not Collideable |
| `0x42015E` → `0x42018C` | `+0x14` | 2 | a Generic trap that is Collideable but not Visible |

`0x41FF71` recurses through `NiNode`s and files every `NiTriShape` leaf, without filtering. The
room's alpha leaves are split into `+0xDC` and go to `+0x1C` (`0x42032E`), a list no mask picks.
So blended room geometry is never collided with.

Visible and Collideable are fields 2 and 3 of every trap's `argv[0]`. Traps.txt documents the
whole field as `InitialState, ResetEnabled, Visible, Collideable`. The Trap base (`0x454A41`)
splits it on commas (`0x50BD02`, untrimmed) and parses each flag with `0x50BBB6`:
`t/true/yes/on/1` and `f/false/no/off/0`, case-insensitive. Anything else keeps the default, which is
true. Across every shipped room there are 1684 Generic traps:

| argv[0] | count | category |
|---|---:|---|
| `,true,true,false` | 965 | 1: visible decoration, not solid |
| `,true,false,true` | 540 | 2: invisible collision box |
| `,true,true,true` | 109 | 0: visible and solid |
| `,true,false,false` | 70 | none |

A Generic whose `argv[2]` is true is a mover. It goes to a separate list (`0x4203C5`, collector
`+0x24`) instead, and isn't covered here.

## What the port does

`collision_world` holds every static room triangle, in world space, in an XZ grid. It answers
`pick(origin, dir, max_t, front_only)` with the nearest hit and the face normal, as above.

What it holds is mask 6's set: room geometry without its blended (alpha) materials, plus every
Generic trap that is Collideable, visible or not. Generic traps with Visible false are not drawn.

Differences from the original:

- one node set stands in for every mask. The rings are not modelled, so the whole loaded region
  is pickable.
- other trap types with real geometry (doors, crates) use the same Collideable flag. How the
  original picks those (collector `+0x24`, movers) is not traced.
- actor bodies and trigger volumes are left out

The movement in `main.cpp` (`ground_under`, `move_horizontal`, gravity) is a stand-in built on
the primitive. The original's `PrinceMotion` is animation-driven and much larger. The step
height, radius and slope limit are our values, not the game's.

Evidence: in `prisonfix` region 0, 5314 triangles are collected (5786 before the Collideable
flag was honoured). The down-probe at the spawn
point finds the floor at the spawn height (y = 0.010). Walking into the first cell wall stops at
x = 25.35 and slides along it; with `--noclip`, the same input walks to x = 43.15, outside the
level.
