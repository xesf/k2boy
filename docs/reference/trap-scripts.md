# Trap scripts: the message language

Level progression in Prince of Persia 3D is a small message language stored in trap
arguments (`K2_TRAP_ARGV<n>` in the room `.K2A` files). Areas, pressure plates and Action
traps run scripts. Other traps receive messages by name. This page describes the language as
`data/pop3dreal.exe` executes it. The port is in `src/engine/script.*`.

## Example

The `IntroAnim` Area in `prisonfix/prison1.K2A` (argv1, its enter message):

```
prince,weapon:-all;IntroAnim,enable:F;prince,snooze:16.2;
prince,SkillAt:Lvl1BeingThrownInCell_M,IntroNISPH;
P1_chamber4_floor03,SkillAt:Lvl1ThrowPrinceInCell_M,IntroNISPH;
delay,.9,camera,tripod,(@IntroNISCam01)X;
delay,7.8,P1_chambergate04,move,blue,-2.5,.25,0,0;
delay,8.2,camera,tripod,(@IntroNISCam02)X;
delay,8.5,P1_chamber4_floor03,SkillAt:BasePose_L,PG1RescuePH;
delay,16.2,camera,DDX;
```

## Grammar

```
script  := { element ";" }
element := block | item
block   := "{" state ":" script "}"          runs only if `state` == the trap's current state
item    := target "," message { "|" message } sends each message to `target`
```

| rule | where | detail |
|---|---|---|
| items are split on `;` | `0x454DE5` | no trimming; an empty tail ends the script |
| only the first matching block runs | `0x454DE5` | later blocks are skipped even if they match |
| block state test | `0x454FE2` | compares the text before `:` with the trap's state, `Trap +0x148` |
| target ends at the first comma | `0x4550BD` | the rest is the message; commas inside it are kept |
| `|` fans out | `0x4550BD` | `target,a|b` sends `a` and then `b` |
| an empty target is the sender | `0x455198` | |
| a target is every trap with that name | `0x455198` | a name→trap multimap at `unk_7F63F0`. Names are node names: `P1_chambergate04`, not `Force` |

A trap's state starts as InitialState, the first field of its argv[0]. `state:X` changes it.
This is how one-shot and multi-step scripts work, as in `p1talker`:

```
{zero:prince,snooze:6;...;p1talker,state:nomore};{nomore:}
```

## Messages every trap understands (`0x4552B7`)

| message | effect |
|---|---|
| `reset` | virtual slot `0xC0` |
| `state:X` | slot `0x100` |
| `enable:b` | slot `0x104` |
| `visible:b` | slot `0x108` → `+0x159` |
| `collide:b` | slot `0x10C` → `+0x15A` |

Booleans use the reader at `0x50BBB6` (see [collision.md](collision.md)). Only a message with
exactly two `:`-fields is considered.

## Special targets

| target | class | messages seen in prisonfix |
|---|---|---|
| `delay` | `DelayMessage` (`0x469E98`), a trap named "Delay" | `t,target,message`. It is queued and sent through `0x4550BD` once `t` seconds have passed (`0x46A014`) |
| `prince` | the Prince | `snooze:t` (no input), `SkillAt:skill,node`, `weapon:-all` |
| `camera` | CameraLogic | `tripod,(@Cam)X`, `follow,(...)(...)`, `DD`, `DDX` |
| `audio` | AudioMessage | a sound name |

## camera (`CameraLogic`, parser `0x40855D`)

| message | effect |
|---|---|
| `D…` (`DD`, `DDX`, `Default`) | back to the default chase camera |
| `tripod,(@Node)` | fixed at a camera node, looking at its `.Target` |
| `tripod,(x,y,z)(x,y,z)` | fixed at a point, looking at a point (engine space) |
| `follow,(…)(…)` | rides along with the Prince at an offset (our reading) |
| `look,…`, `security,…` | not ported |

Camera nodes are `*OBJECT CAMERA` blocks with `CAMERA_TYPE Target`. They have two `NODE_TM`s:
the camera, then `<name>.Target`. Node transforms are in Max space (z up), so engine =
`(x, z, −y)`. This was measured on `IntroAnim`, whose `TM_POS` (20.34, −1.53, 0) belongs to a
volume centred at engine (20.34, y, 1.53). Trailing letters (`X`) are flags (`0x409975`). Their
transition is not modelled: every change is a cut.

## Actors: `SkillAt` and `snooze`

An actor answers to the name of the `EnemyStart` node that spawned it (`P1_chamber4_floor03`).
The Prince answers to `prince`. The original routes these through the spawn trap:
`EnemyStart__ctor` references `"SkillAt"`.

| message | effect |
|---|---|
| `snooze:t` | no player input for t seconds. The player has no control during this time, camera included |
| `SkillAt:skill[,placeholder[,REMOVE]]` | plays one of the actor's skills once. With a placeholder, it starts from that node's position and facing. Root motion carries the actor on all three axes. `REMOVE` hides the actor afterwards |

A placeholder's facing comes from its node rotation. ASE stores `TM_ROTANGLE` with the opposite
sign to its axis, so yaw = −angle·axis.z. Checked on `IntroNISPH`: with that sign, the throw
lands the Prince inside the cell (x 20.56); with the other sign he lands outside it (x 16.17).

## Area (`0x4640EC`, `0x4643AD`)

| argv | field | meaning |
|---|---|---|
| 1 | `+0x18C` | enter: runs when a character first stands in the volume |
| 2 | `+0x1E0` | exit delay in seconds. A negative value means the Area never exits; this also disables argv4 |
| 3 | `+0x19C` | exit: runs after the character has been out for the delay |
| 4 | `+0x1AC` | action: once per visit, on the character's action input (flag `0x8000000`). Dialogue uses this |
| 5 | `+0x1BC` | repeat: runs on entry and then every argv6 seconds while inside |
| 6 | `+0x1D0` | repeat interval. -1 when argv5 is empty |

"A character" is any object whose `+0x128` is zero, which covers the Prince and enemies but not
traps. A disabled Area does nothing.

## Trigger volumes (`0x47517A`, `0x4753D3`)

Areas and pressure plates do not test a box. The volume is the footprint of the trap mesh's
**up-facing** triangles (`0x47526C` keeps faces whose normal has y > 0), extruded over the
mesh's height:

- the point's y must lie in the mesh's y range, exactly (`0x47536F`)
- its xz must fall inside any kept face (`0x47553B`, margin 0)

## PressurePlate (`0x483925`, states `0x483DD2` … `0x483F04`)

| argv | field | meaning |
|---|---|---|
| 1 | | actor whose `On` / `Off` skills animate the plate (default `TRAP_PressurePlate`, 33 ms each) |
| 2 | `+0x168` | on message |
| 3 | `+0x190` | release delay in seconds. Negative means it stays down for good |
| 4 | `+0x178` | off message |

The plate is a small state machine. Its update calls through a function pointer at `+0x160`:

```
Idle --(a character on it)--> play On --(On ends)--> run on message
     --> Held (delay >= 0) | stuck (delay < 0)
Held --(nobody on it)--> wait `delay` --(stepped on again: back to Held)
     --> play Off --(Off ends)--> run off message --> Idle
```

A disabled plate feels nothing (`0x483D53`).

## Door: skill-name messages

A Door's messages are the names of its actor's skills: `openslow`, `close`, `pivot90`, and so on.
The actor is argv1, defaulting to `TRAP_Door`, which has 54 skills. The original rejects anything
else with `"Doors can't handle message %s"`. The skills are `SklK` keyframe clips, sampled with
`JKey::Interpolate` (`0x612278`):

- The joint's pose is **`xf[0]`** (`+0x10`). It is absolute relative to the node's rest pose:
  `Open` goes y 0 → 4, `Close` 4 → 0, and `Pivot90` turns 90° about y.
- `u` is eased by `3s²u·a.ease_out + 3su²·b.ease_in + u³` (s = 1 − u), which is linear for the
  shipped 1/3 and 2/3.
- Mode `+0xD0` = 2 holds the key. Otherwise `xf[0]` is lerped and slerped. `xf[3]` is a separate
  Bézier channel (`0x613F05`, with tangent constants 2/3 and 1/3 in `0x611D00`) that doesn't
  drive the node.

The rotation is about the trap's `K2_TRAP_PIVOT`.

## Fixture: actors placed by a trap

```
argv0  "<state>,<visible>,<collide>,<?>"   ",true,true,false", "Empty,true,true,false", "1"
argv1  actor directory under Actors        Prisoner3, PRISONER_TALKY, rat, Lever01, FIX_Crane
argv2  idle skills, comma separated        "BasePose_L,...,AmbientType_G6", "Breathe"
```

The actor has no master.dat: its skeleton is `Actors/<argv1>/<argv1>.act`
and its body the `.K2A` beside it. It answers scripts under the trap's node
name. Messages seen in prisonfix are its own skill names
(`Prisoner3,LookUp`, `Moper2,Talktoprince`, `P4FIX_Crane01,lower4m`,
`P5_CrushLever01,RunFromOff`) plus `enable:T` and `Visible:T`. A `false` or
`F` visible field starts it hidden (the box rats, the strongbox).

Ours: idles are picked at random from argv2. A named state in argv0 is posed
and held. A skill sent by a script returns to the idles if there are any;
otherwise its last frame is held. The prisoners' meshes are built around the
pelvis (y −1.02 … 1.12), so the body is lifted by that much off the floor
found under the trap. Not traced in the binary: the idle selection and
timing, and what `<collide>` and the fourth field do.

## `move` (Force, Mover; TranslateMotion)

```
move,<axis>,<distance>,<T>,<Ta>,<Td>[,Loop|Crush|HalfCrush...]
```

The axis is named by a colour, in the exporter's frame: `red` +x, `blue` +y (up), `green` −z
(`0x47AC58`). The node travels `distance` in `T` seconds along a trapezoid profile: it
accelerates for `Ta`, cruises, then decelerates for `Td`. The cruise speed is set at start
(`0x47AF7F`) as `v = D / (T − (Ta + Td) / 2)`. Position at time t (`0x47B107`):

```
t < Ta          s = v/Ta · t²/2
t < T − Td      s = v·t − v·Ta/2
otherwise       s = D − v/Td · (T−t)²/2
```

`target` in place of the axis moves toward a named node. It is not ported yet.

## Status of the port

Working: the grammar, state blocks, every base message, `delay`, Area, PressurePlate, the
trigger-volume test, Door skills, camera tripod/default, actor `SkillAt`, TranslateMotion on any
trap with geometry, and `prince,snooze`. `--trapmsg` prints each delivery in the original's
debug format, `TRAP MSG [<sender> to <target>]: <msg>`, and the exit summary lists what nothing
acted on.

Not yet: camera `look`/`security` and transitions, audio, Mover/Action behaviour, Water `raise`, the plate's own animation,
Rotate, Oscillate and the other motion types, and testing enemies against Areas.
