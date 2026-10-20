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

**Not yet traced:** the code that puts a node into category 0, 1 or 2 at room load. Category 0 is
almost certainly static room geometry. The `'!' / '$' / '#'` markers (collidable but invisible,
and so on) found near this code belong to a scene-dump legend (`0x510CB3`). They are not a
node-naming convention, and no shipped `.K2A` node name uses them.

## What the port does

`collision_world` holds every static room triangle, in world space, in an XZ grid. It answers
`pick(origin, dir, max_t, front_only)` with the nearest hit and the face normal, as above.

Differences from the original, all because of the untraced categories:

- one node set stands in for every mask
- blended materials (flame sheets, water, grates) are left out, on the same reasoning as the
  `web/` experiment
- actor bodies and trigger volumes are left out

The movement in `main.cpp` (`ground_under`, `move_horizontal`, gravity) is a stand-in built on
the primitive. The original's `PrinceMotion` is animation-driven and much larger. The step
height, radius and slope limit are our values, not the game's.

Evidence: in `prisonfix` region 0, 5786 triangles are collected. The down-probe at the spawn
point finds the floor at the spawn height (y = 0.010). Walking into the first cell wall stops at
x = 25.35 and slides along it; with `--noclip`, the same input walks to x = 43.15, outside the
level.
