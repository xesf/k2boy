# Scene graph — `NiAVObject` / `NiNode`

Implemented in `src/engine/node.{h,cpp}`. This is the spine: the recovered
hierarchy (`docs/reference/nirtti-hierarchy.md`) puts `LevelGroup`, `RoomGroup`,
`K2ArticulatedObject`, `k2SceneGraph`, every `Force` and — through
`CharacterObject` — the Prince, the camera and all 39 traps under `NiNode`.

## The transform layout is measured, not assumed

`NiAVObject__LoadBinary` (`0x551EC0`) reads straight into consecutive fields of
`this`:

```
551ece  call NiObject__LoadBinary        ; name + link id
551ed9  push 1 / lea eax,[ebx+2Ch]       ; +0x2C  1 byte   flags
551ee5  lea ecx,[ebx+30h]  NiStream__ReadVector3   ; +0x30  translation
551eee  lea ecx,[ebx+3Ch]  NiStream__ReadMatrix3   ; +0x3C  rotation
551ef6  push 4 / lea ecx,[ebx+60h]                 ; +0x60  scale
```

The gaps fix the sizes exactly: `0x3C - 0x30 = 12` (a vec3) and
`0x60 - 0x3C = 36` (a 3x3). So the local transform is

```c
struct NiTransform {
    float translate[3];   // +0x30
    float rotate[9];      // +0x3C, row-major
    float scale;          // +0x60, uniform
};
```

Rotation is kept as a 3x3 rather than converted to a quaternion, because that is
what the format stores and what the engine composes. Converting on load would
mean converting back to check anything against the original.

Renamed in IDA: `NiStream__ReadBytes` (`0x40A912`), `NiStream__ReadVector3`
(`0x556D90`), `NiStream__ReadMatrix3` (`0x556B20`).

## What is implemented

| type | original | note |
|---|---|---|
| `ni_node` | `NiAVObject` + `NiNode` | collapsed; the game layer only ever derives from `NiNode` |
| `level_group` | `LevelGroup` | 2 of 45 slots overridden — a plain grouping node |
| `room_group` | `RoomGroup` | likewise |
| `animation_node` | `AnimationNode` | actor root, named `"AnimNode"` as at `0x401E3C` |

`transform_compose(a, b, out)` applies `b` in `a`'s space: rotation `Ra*Rb`,
translation `Ra*(scale_a * tb) + ta`, scale `sa*sb`. It is alias-safe, so
`out` may be `a` or `b`.

`update_world()` walks the subtree composing parents into children.

## Self-test

Matrix code cannot be eyeballed, and a wrong basis would silently misplace every
actor, so `k2boy --verify` checks composition against hand-computed answers:
yaw-90 sends +X to -Z, a rotated parent turns a child's +X offset into -Z, an
identity parent is a no-op, scale multiplies down the chain and scales the
child's offset, plus child-world inheritance, find-by-name, subtree size and the
RTTI relationships.

```
scene graph self-test: 8 passed, 0 failed
```

## Current wiring

Actors are now `animation_node`s parented to the level root, carrying the
transform their spawn point specifies; rendering reads `node->world()`. Verified
pixel-identical to the previous hand-rolled matrix path via `--shot`.

Room geometry is still drawn flat rather than through `room_group`, because
`.K2A` rooms are authored in world space and sit at identity — attaching them
buys nothing until rooms need to move or be culled per-room.

---

# Draw order

Three bugs, all producing "far polygons on top of near ones".

## 1. The projection ignored the level's clip planes

`mat4_perspective` was hardcoded to `near 0.1 / far 1000` — a ratio of **10000**.
Depth precision is governed by that ratio and dominated by the near plane, so at
distance there was almost none left and far surfaces won z-fighting against near
ones. Connected rooms drew through each other.

Levels ship `[ClipPlanes]` and the original uses them:

| level | near, far | ratio |
|---|---|---:|
| prisonfix | 0.1, 50 | 500 |
| cistern | 0.5, 85 | 170 |
| ruins | 0.5, 100 | 200 |

Now honoured, both at startup and on resize. **20x more depth precision** on
prisonfix. The far plane is deliberately *not* widened to fit the scene: the
original relies on region streaming plus fog, and fog saturates at 1.5x far, so
anything past it is solid fog colour anyway.

## 2. The transparent pass was unsorted

Alpha surfaces draw with depth-test on but depth-**write** off. Without sorting,
a distant alpha surface submitted later paints straight over nearer geometry.
Now sorted back to front by squared distance from the camera each frame.

## 3. Actors drew after the transparent pass

Actors are opaque and belong in the depth buffer *before* anything is blended
over them. They were drawn last, so they painted over transparent surfaces that
were in front of them. Pass order is now:

```
opaque room geometry  ->  actors (opaque, per-node transform)
                      ->  transparent, back to front
                      ->  trigger volumes (wireframe overlay)
```

## A measurement note

The first check after these changes *looked* like a regression — the room
appeared black. It was not: 180 opaque items were still being submitted, and
comparing mean frame brightness against the previous capture gave 22.3 vs 19.2,
a 14% change consistent with alpha surfaces now blending in the correct order.
The apparent darkness was the trigger wireframe overlay plus eyeballing two
screenshots taken from different states. `--notrig` confirmed the geometry was
fine. Compare numbers, not impressions.

## Verifying draw order objectively

Guessing at ordering bugs from screenshots wasted a pass. These checks are cheap
and decisive; run them before changing anything.

**Is there a depth buffer at all?** `gfx::init` now prints what the default
framebuffer actually has, not what the pixel format requested:

```
gl: AMD Radeon Pro 560X OpenGL Engine | 4.1 ATI-7.0.24 | depth bits 24
```

**Is depth deciding visibility?** `--revdraw` reverses opaque submission order.
With a working depth buffer the frame must be near-identical:

```
k2boy --boot --notrig --cam "0,10,30,0,-0.25" --shot fwd.bmp
k2boy --boot --notrig --revdraw --cam "0,10,30,0,-0.25" --shot rev.bmp
-> pixels sampled 230400, differing >24: 0 (0.00%)
```

**0.00%.** Opaque geometry is ordered by depth, not by submission. Any remaining
ordering artefact is therefore *not* in the opaque pass.

## Far plane: a deliberate deviation

The level's far value is tuned for portal-culled region streaming. This loads a
whole region at once, so taking prisonfix's far of 50 literally risks slicing
rooms mid-wall. The far plane is now extended to cover the loaded bounds:

```
clip planes: near 0.10 far 185.52 (level far 50.00, ratio 1855)
```

Precision is dominated by *near*, so keeping the level's near retains most of the
gain — 10000 -> ~1855 rather than -> 500, still five times better than the old
hardcoded pair.

## What is still unverified

Per-triangle order *within* a single alpha mesh. The transparent pass is sorted
per mesh, which cannot fix a mesh whose own faces overlap in view. That is the
one ordering case left that these tests do not cover.

## Uninitialised render state (fixed)

Symptom: **enabling trigger volumes made room textures disappear.**

`render_state` was a bare POD and the frame loop set only the fields it cared
about, leaving `wireframe` unwritten. The debug trigger pass sets
`wireframe = true`, and the next frame's `render_state` — a fresh automatic on
the same stack slot — read that stale `true`. The *opaque* pass then drew the
whole room in wireframe.

It only appeared with triggers on, which is what made it look like a trigger
problem rather than an initialisation one.

`render_state` now has a constructor giving every field a defined value, so a
field the caller forgets cannot inherit the previous frame's. The frame loop
sets only what actually differs from the defaults:

```cpp
gfx::render_state rs;          // constructor gives every field a value
rs.cull = gfx::CULL_NONE;      // .K2A winding not yet verified
rs.fog  = have_level && lv.fog.size() >= 2;
```

Verified by rendering the same view with triggers on and off; mean frame
brightness 22.36 vs 22.28, i.e. identical apart from the wireframe lines
themselves. Before the fix the triggers-on frame was the whole room in wireframe.
