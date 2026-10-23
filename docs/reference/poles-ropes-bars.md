# Poles, ropes and bars

How the original implements the `Pole`, `Rope` and `Bar` traps and how the Prince climbs, swings
and shimmies on them. Everything here is read from the disassembly of `data/pop3dreal.exe`
(capstone over the PE, cross-checked against the RTTI); guesses are marked. Clip lengths come from
`k2boy --skl`.

## The short version

| trap | what it is in the levels | trap code | the Prince |
|---|---|---|---|
| `Pole` | a vertical climbing rope or chain, 8.5–38 units tall, ~0.1 thick | stores the axis (bbox centre x/z, top, bottom); registers in the room's pole list | `StStartPoleMount` … `StIdlePolePushOff`: climb, slide, turn around it, push off |
| `Rope` | a swinging rope: the `TRAP_ROPE` actor (12 segments, 8 units) | plays `RopeBase` / `StartSwing` / `SwingFullRange` / `EndSwing` on the actor; flags the rope "in reach" | `StStartSwingRope` … `StStartSwingRopeRelease`: hangs on the actor's `Attach` node, lets go with the node's velocity |
| `Bar` | a horizontal bar, shimmy rope or swing bar | nothing beyond the base `Trap` (a marker class) | a ledge grab whose edge belongs to a `Bar` becomes `StStartHorizHang`: shimmy, turn around, swing, launch |

- **The grip is "jump or Walk held"** (input bits `0x10 | 0x20`, `PrinceObject +0x154`) on all
  three. Releasing both lets go. There is no separate drop key.
- **No physics.** The rope's swing is an authored animation on the rope actor, played in step
  with the Prince's clip (`StartSwing` and `SwingStart_M` are both 4719 ms; `SwingFullRange` and
  `SwingBase_M` are both 3696 ms). The pole and bar never move.
- The Prince is driven by a `K2AttachActivity`: the clip plays relative to an attach transform
  (`[PrinceObject+0x1D0]+0x7C` for poles and bars) or a node (the rope's `Attach` node). The
  states move the attach transform; the clips carry the body.
- `StStartHang`, `StStartPoleHang` and `StStartSwingRope` set the collider shape with
  `PrinceCollider__SetShape(3)` (`0x4103FE`, *guess* at the name). The hanging states set the
  noise radius to 0, and climbing or shimmying sets it to 9.0.

## Trap data

All three traps take only the common argv fields. Every instance in the shipped rooms has
`K2_TRAP_ARGV0 ",true,true,true"` (one `Pole` has `",false,false,false"`, three have `""`), so
`data/Traps.txt`'s "Rope 1 Type actor" is always empty and the default actor is used.

The trap descriptor holds the `K2_TRAP_BBOX` at `+0xE8..+0xFC` as min x, y, z, max x, y, z
(engine y up; the `.k2a` lists it in that order). `+0x100` is the actor and `+0x104` the node
whose transform the trap copies (the `K2_TRAP_PIVOT`, *guess* from the rope instances).

### `Pole` (`Pole__ctor` `0x4832BF`, size 0x170)

| field | value |
|---|---|
| `+0x160` | (min x + max x) × 0.5: axis x |
| `+0x164` | (min z + max z) × 0.5: axis z |
| `+0x168` | max y: top |
| `+0x16C` | min y: bottom |

The vtable (`0x7483CC`) overrides only the dtor, the class-info getter (`0x7FD6D0`, `Pole`) and
slot 51, `Pole__AddToRoom` (`0x4833E8`). That adds the pole to the room's list at `room+0xF8`
(`0x44A86D`). The floor generator reads that list (`0x41C009`, `0x41C60A`: a node at top − 0.5,
links 2 units up and down). *Guess:* AI navigation; not traced.

### `Bar` (`Bar__ctor` `0x466F19`, size 0x160)

No fields. The vtable (`0x746AA0`) overrides only the dtor, the class-info getter (`0x7FBA70`,
`Bar`) and slot 51 (`0x466FF2`, base plus collision registration). The bar's mesh is ordinary
collision geometry; the Prince recognises it by the class of the object a pick hits.

### `Rope` (`Rope__ctor` `0x487FF3`, size 0x170)

| field | meaning |
|---|---|
| `+0x160` | the actor: argv "Type actor", default `TRAP_Rope` (`data/Actors/TRAP_ROPE`) |
| `+0x164` | the actor's `Attach` node (name at `0x748994`) |
| `+0x168` | byte: grabbed by the Prince |
| `+0x16C` | the current update function |

The ctor builds the body, puts it at the trap node, makes the trap non-collideable
(`Trap__SetCollideable(0)`, vfunc `+0x10C` = `0x455918`), and sets the update to
`Rope__UpdateIdle`. The `TRAP_ROPE` actor has 12 segment nodes `Box01..Box12` (0.66 apart, about
8 units) and `Attach`. In the rooms, the rope placeholder's pivot is at its bottom end
(`tallroom_ropeswing`: pivot y = bbox min y).

| function | address | does |
|---|---|---|
| `Rope__Reset` (vtable slot 48) | `0x48848E` | clear `+0x168` and `g_RopeGrabbed` (`0x7FDCF0`), play `RopeBase`, update = idle |
| `Rope__UpdateIdle` | `0x48851E` | if no rope is grabbed and `Rope__IsPrinceInReach`: `[g_Prince+0x1D4]+0x5C` = this rope. If `+0x168`: play `StartSwing`, update = swing |
| `Rope__UpdateSwing` | `0x488575` | when the clip ends: still grabbed, play `SwingFullRange` again; released, play `EndSwing` (2838 ms), update = end |
| `Rope__UpdateEndSwing` | `0x4885D0` | when the clip ends, update = idle (`RopeBase` is not replayed) |
| `Rope__IsPrinceInReach` | `0x48835A` | below |

`Rope__IsPrinceInReach`, with `r` = the trap node's world position and `p` = the Prince's root:

```
r.y - 2.0 < p.y < r.y + 0.5            // 0x7438C8, 0x7438C4
(r.x - p.x)^2 + (r.z - p.z)^2 < 0.61^2   // 0x748ABC
```

So the rope is caught near its bottom end at rest, from below up to 2 units under it. The test
uses the static trap node, not the swinging `Attach` node.

## Pole

### Reach

The Prince notices a pole by **walking into it**. His collision callback (`0x4105D1`) calls
`Prince__CollidePoleFilter` (`0x411F52`) for every contact:

- contact with the pole he holds (`[+0x1D4]+0x60`): ignored;
- contact with any other `Pole`: `[+0x1D4]+0x58` = that pole, and the contact still blocks.

`+0x58` is cleared at the start of every collision pass (`0x410BF8`, `0x410E0E`, "Clearing a
POLE!!!!").

`Prince__CanGrabPole` (`0x4D11E4`) then needs `+0x58` set, and the pole inside a 45.6° cone:

```
back  = root - facing * 0.5,  y = 0
dir   = normalize((pole.x, 0, pole.z) - back)
ok    = dot(dir, facing) > 0.7                       // 0x7461D4 ("Pole failed: cos = %f")
```

On success, the attach transform `+0x7C` becomes: position (pole.x, root.y + 2.25, pole.z)
(`0x74AF68`), rotation turning +z to `dir`. The hands hold the axis 2.25 above the feet.

| from | condition | gait `+0xF8` |
|---|---|---|
| `StStanding` (`0x4B14EA`) | forward, forward probe blocked, Walk held | 0 |
| `StIdleLocomote` (`0x4B20EE`) | blocked (`0x20000000`), Walk held | 0 |
| `StIdleFall` (`0x4B5594`) | blocked or activity ended, grip possible (below), gait > 0 | unchanged (> 0) |

All three call `Prince__StopActivity` (`0x4C7A6A`), log "Reachable Pole YAHOO! Start Poling!",
copy `+0x58` into `+0x60` (the held pole), and go to `StStartPoleMount`.

**Grip possible in a fall** (`StIdleFall` `0x4B5117`, flag `[ebp-0xD]`): jump or Walk held, the
Prince alive (`+0x130 > 0`), and the fall so far (`[+0x1D0]+0x64`.y − root.y) shorter than
`FallDeathHeight` (12.0, scaled by (`[+0x1D0]+0xB8` + 1) × 0.5 while `+0xC0` is in the future),
or a timed window `[+0x1D0]+0xC8` still open (`0x48F3E2`; *guess*: a low-gravity effect, which
also uses gravity −2 in `K2Activity3D__Create`).

### States

| state | address | clip | play |
|---|---|---|---|
| `StStartPoleMount` | `0x4D3C0E` | `VerticalRopeFromJump_M` (gait > 0) or `VerticalRopeAntic_M` | once |
| `StStartPoleHang` | `0x4D3D6E` | `VerticalRopeBaseLH_M` (hand < 0) or `VerticalRopeBaseRH_M` | loop |
| `StIdlePoleHang` | `0x4D3EBC` | — | input |
| `StStartPoleMove` | `0x4D4140` | climb / slide clips (below) | once, slide loop |
| `StIdlePoleMove` | `0x4D441C` | — | turning, sliding, end of move |
| `StStartPolePushOff` | `0x4D4624` | `VerticalRopePushOffLH_M` (hand < 0) or `…RH_M` | once |
| `StIdlePolePushOff` | `0x4D4762` | — | then `StStartFall` |

"play" is the attach activity's mode: 6 = once, 10 = loop (`K2AttachActivity__SetLooping`
`0x48D8B3`).

The mount turns the pole's collision off (`Trap__SetCollideable(0)`), sets the hand sign
`[+0x1D0]+0xF0` = +1 and goes to `StIdlePoleMove`; when the clip ends, `StStartPoleHang`. The
push-off turns collision back on and clears `+0x60`.

### Hanging (`StIdlePoleHang`)

| input | result |
|---|---|
| neither jump nor Walk held | if the body fits 0.85 behind the pole (`0x74AFA0`) at root.y − 0.5: "Let go from Pole!", `StStartPolePushOff`. If not, he keeps holding |
| left / right (bits `0x1`/`0x2`), at a decision point | turn around the pole: rotate the attach transform about +y by elapsed × π/3 (`0x74AF8C`, `0x749444`), negative for left: **60°/s**. Applied only if `Prince__CanMoveOnPole(t, 0)` |
| forward / back (bits `0x4`/`0x8`) | "Move request while on Pole", `StStartPoleMove` |

### Climbing and sliding (`StStartPoleMove`, move code `[+0x1D0]+0xF4`)

| code | from | clip (hand `+0xF0` ≥ 0 / < 0) | dy checked | attach y += |
|---|---|---|---|---|
| +1 climb | forward held | `VerticalRopeClimbUpRH_M` / `…LH_M` (627/594 ms), then the hand flips | +0.393 (`0x74AAD0`) | +0.393 |
| −1 start slide | back held | `VerticalRopeStartSlideDownLH_M` / `…RH_M` (297 ms) | −0.4723 (`0x74AAD4`) | 0 |
| −2 slide | after −1, back still held | `VerticalRopeSlideDownLH_M` / `…RH_M` (loop) | −0.4723 | −0.4723 |
| −3 end slide | after −1 with back released, or after −2 | `VerticalRopeEndSlideDownLH_M` / `…RH_M` (495/528 ms) | −0.4723 after −1, 0 after −2 | the same |

The slide clips pick the hand the other way round from the base, climb and push-off clips.
Climbing alternates hands: after `ClimbUpRH` the base is `BaseLH`.

During the −2 loop, `StIdlePoleMove` moves the attach transform down by **3.0 units/s**
(`0x74AB64`) each decision tick while back is held and `Prince__CanMoveOnPole` allows it. When
back is released or the move fails ("Done sliding, need to stop sliding...!"), the activity stops
and `StStartPoleMove` plays the end clip. Turning works during moves as while hanging. When a
once-clip ends: code −1 continues to the next move; anything else clears the code and returns to
`StStartPoleHang`. A failed check in `StStartPoleMove` also returns to hanging (and logs "Can't
Pole from a MUST pole state" when the code was −2 or −3).

`Prince__CanMoveOnPole` (`0x4D141C`, attach transform `t`, step `dy`):

```
y = t.y + dy
dy >= 0: fail if y > pole.top                    // +0x168
dy <  0: fail if y - 2.25 + 0.25 < pole.bottom   // +0x16C; feet stay 0.25 above the bottom
then the body must fit at t * (0, dy - 1.9, -0.17)   // 0x74AB60, 0x74AF80; probe 0x4117B7
```

So the hands go up to the pole's top, and the feet down to 0.25 above its bottom.

### Letting go

`StIdlePolePushOff`, when the push-off clip ends ("Let go from pole hang!"): blend 200, motion
fields `+0xF0/+0xF4/+0xFC` = 0, gait `+0xF8` = −2, then `StStartFall`. **No launch velocity:**
the push-off clip moves him away, and the fall starts from rest. He cannot jump off a pole in a
chosen direction.

## Rope

### Grab

Only from the air. In `StIdleFall`, at a decision point, with the fall grip possible (see Pole),
`Prince__CanGrabRope` (`0x4D1541`) is tried **before** the ledge check:

1. The candidate is `[+0x1D4]+0x5C` (set by `Rope__UpdateIdle`). `Rope__IsPrinceInReach` must
   still hold, else the candidate is cleared.
2. With `v` = root − `Attach` world position: `dot(facing, v) <= 0`, so the rope is ahead of or
   level with him, not behind.
3. The `Attach` node's world rotation is set to the Prince's: **the rope swings in the plane he
   faces.**

Then "Reachable Rope YAHOO! Start SwingingRope!": `+0x60` = the rope, rope `+0x168` = 1,
`g_RopeGrabbed` = 1, gait = 0, `StStartSwingRope`.

### States

| state | address | clip | play |
|---|---|---|---|
| `StStartSwingRope` | `0x4D586F` | gait 0: `SwingStart_M` (4719 ms). Gait 1: `SwingBase_M` (3696 ms) | once / loop |
| `StIdleSwingRope` | `0x4D5ACA` | — | |
| `StStartSwingRopeRelease` | `0x4D5C4A` | `SwingReleaseFar_G` (660 ms), ballistic | |

`StStartSwingRope` attaches to the rope's `Attach` node (`K2AttachActivity` by node, `0x494273`).
On the first clip it also places the camera (`0x408C11` on `g_Camera` `0x7E9344`, *guess*) at
`Attach` × (−2.39, 3.0, 0.763), looking at (0, 1.25, 0). It resets the "last attach position"
`[+0x1D0]+0x70` to (−FLT_MAX, …).

`StIdleSwingRope`:

- start clip finished: gait = 1, "Done with start anim, now do looping!", `StStartSwingRope`
  again (now `SwingBase_M`, looped);
- every tick, when the `Attach` position changed: `[+0x1D0]+0x70` = it, `+0x38` = now;
- neither jump nor Walk held: "Let go from Swing Rope!", `StStartSwingRopeRelease`.

There is no input to pump the swing, climb the rope or turn on it.

### Release

`StStartSwingRopeRelease` waits until the `Attach` node has moved since the last sample, then:

```
dt   = now - [+0x1D0]+0x38
v    = (Attach.pos - [+0x1D0]+0x70) / dt      // the node's velocity
yaw  = facing flattened to the horizontal; reversed when gait == 1 (looping clip)
K2Activity3D__Create(SwingReleaseFar_G, gravity (0, FallGravity, 0), v, 5)   // 0x48F2B2
```

`FallGravity` is −17.0 (−2.0 inside the timed window). Then: motion fields 0, gait = 2, rope
`+0x168` = 0, `g_RopeGrabbed` = 0, `+0x60` = 0, the fall start `+0x64` and `+0x70` = his position,
`StIdleFall`. **The launch velocity is whatever the rope animation's `Attach` node is doing at
that frame**, so the distance depends on when he lets go. The rope finishes its current
`SwingFullRange` cycle, plays `EndSwing` and stops.

## Bar

### Grab: a ledge on a `Bar`

Bars are grabbed by the ledge-hang code. `StStartHang` (`0x4B3837`) first calls
`Prince__IsLedgeOnBar` (`0x4D0602`): a pick (mask 6) from the attach point + (0, −0.02, 0)
(`0x743DD4`, down vector `0x89F8E0`) along the attach facing for 0.2 (`0x743DA0`). If the hit
object is a `Bar` (`0x4128BB` with class `0x7FBA70`): "Reachable Bar! Start Hanging!" and an
immediate switch to `StStartHorizHang`.

`StStartHang` is entered from `StIdleFall` (ledge in reach, grip possible: gait becomes 1, or
stays 2 after a running jump), from `StIdleBackup` (backing off an edge with Walk: gait −1), from
`StStartBounceBack`, and from the predictive stand jump (`0x4B73B8`: grip held and
`Prince__CanHangOnBarAt(t, 0)`: gait 1). How the ledge and its attach transform are found
(`0x4CF65C`) is not traced (see [jumps.md](jumps.md)).

### States

| state | address | role |
|---|---|---|
| `StStartHorizHang` | `0x4D4818` | gait ≠ 0: copy gait into the move code `+0xF4`, gait = 0, `StStartHorizHangMove` (the catch). Else `HorizHangBase_M` (once), camera mode 9, all motion fields 0 |
| `StIdleHorizHang` | `0x4D49C6` | input, below |
| `StStartHorizHangMove` | `0x4D4AEB` | plays one move clip from the motion fields |
| `StIdleHorizHangMove` | `0x4D515C` | chains moves when a clip ends |
| `StStartHorizSwing` | `0x4D52F8` | `HorizSwingBase_M` (1749 ms), looped |
| `StIdleHorizSwing` | `0x4D542B` | keep swinging, stop, or let go |
| `StStartHorizLaunch` | `0x4D554F` | the dismount jump |

`StIdleHorizHang`:

| input | result |
|---|---|
| neither jump nor Walk held | "Let go from horiz hang!": `StStartFall`, all motion fields 0 (a plain drop) |
| forward | "Start horizontal swinging": `+0xF8` = 1 |
| left / right | "Move while horizontal hang": `+0xF0` = −1 / +1 |
| quick turn (`0x40`) | "Quickturn while horizontal hang": `+0xFC` = 1 |

Each non-drop row goes to `StStartHorizHangMove`. Back does nothing.

### Move clips (`StStartHorizHangMove`, checked in this order)

| field | value | clip | length |
|---|---|---|---:|
| `+0xF4` (catch) | 1 | `LedgeGrabNoMomentum_M` | 759 |
| | 2 | `LedgeGrabHoriz_M` (camera mode 0x16) | 792 |
| | 3 | `LedgeGrabHorizStop_M` | 957 |
| `+0xFC` | 1 | `HorizHangTurnAround_M` | 1023 |
| `+0xF8` (swing) | 1 | `HorizSwingStart_M` (camera mode 0x16) | 1419 |
| | −1 | `HorizSwingStop_M` | 1815 |
| `+0xF0` (shimmy) | −1 / +1 | `HorizStartMovingLeft_M` / `…Right_M` | 429 |
| | −2 / +2 | `HorizMovingLeft_M` / `…Right_M` | 1023 |
| | −3 / +3 | `HorizEndMovingLeft_M` / `…Right_M` | 627 |
| none | | "Invalid mMotionVector in HorizHangMove!", `StStartHorizHang` | |

All play once. When a clip ends, `StIdleHorizHangMove` chains:

| finished | next |
|---|---|
| catch 1 or 3 | `StStartHorizHang` |
| catch 2 (with momentum) | forward held: `StStartHorizSwing`. Else catch 3 (`LedgeGrabHorizStop_M`) |
| swing start | `StStartHorizSwing` |
| shimmy ±1 or ±2 | that direction still held: ±2 (another step). Else ±3 |
| shimmy 0 or ±3, swing stop | `StStartHorizHang` |

### Shimmy

Each `HorizMoving*_M` step moves the attach transform sideways by **0.5717** (`0x74AB04`)
along its local x (`g_VecX`): −0.5717 × sign, where sign is +1 for right. *Guess:* local +x is
his left, so each step goes the way he is moving. Before the step,
`Prince__CanHangOnBarAt(t, −0.5717 × sign)` must pass. If it fails, the step becomes the end clip
(±3) with no shift. Start and end clips do not shift the transform; their root motion (if any)
is the clip's.

`Prince__CanHangOnBarAt` (`0x4D019F`, transform `t`, lateral offset `s`), with `d` = 0.09
(0.1 × 0.9, `0x74AF0C`, `0x743F9C`) and `e` = 0.0225 (`d` × 0.25):

1. `p` = t.pos + right × `s`.
2. A pick down from `p` + up × `d` + facing × `e` must hit within 2`d` = 0.18 vertically.
3. A pick along the facing from `p` + down × `e` − facing × `d` must hit within 0.18.
4. The two hit normals must differ (an edge, not one flat face).
5. The body must fit at `p` (`0x4117B7`).

### Turning around

`HorizHangTurnAround_M` puts him on the other side of the bar. The code probes forward from the
grip (0.1 ahead, 0.02 down) for the near face within 0.3, then from that hit (or from 1.0 ahead)
back along the facing for the far face. The new attach transform is the old one turned 180° about
+y, at the far hit + 0.02 up. It must pass `Prince__CanHangOnBarAt(t, 0)`, else he stays
(`StStartHorizHang`).

### Swinging and the dismount

`StStartHorizSwing` plays `HorizSwingBase_M` looped. `StIdleHorizSwing`:

- grip held: `K2AttachActivity__SetLooping(forward held)` (`Prince__SetSwingLoop` `0x4D21F7`).
  Releasing forward lets the loop run out. When it ends, `+0xF8` = −1 and `HorizSwingStop_M`
  returns him to the hang;
- grip released: camera mode 9, then `Prince__CanHorizLaunch` (`0x4D095D`): the swing clip's
  phase (elapsed ÷ length, `0x4D217D`) must be in **(0.43, 0.75)** (`0x74AF70`, `0x743D64`), that
  is 752–1312 ms of 1749. Inside: "Launch from horiz swing!", blend 200, `StStartHorizLaunch`.
  Outside: "Let go from horiz swing!", a plain drop (`StStartFall`).

`StStartHorizLaunch`:

```
f    = normalize(body facing)
pos  = grip + f * 1.61, y = root.y          // 0x74AFA4; the body is snapped there
vel  = f * fHorizLaunchZ + up * fHorizLaunchY
K2Activity3D__Create(HorizSwingReleaseFar_G, gravity (0, FallGravity, 0), vel, 6)
```

| tuning key (block `dword_801C2C`) | offset | default |
|---|---|---:|
| `fHorizLaunchY` | `+0xAC` | 4.5 |
| `fHorizLaunchZ` | `+0xB0` | 5.2 |

With gravity −17 this rises 0.6 and comes back to launch height after 0.53 s, 2.75 units ahead
(4.36 units from the bar). Then: `+0xF4` = 1, gait = 2, `+0xF0/+0xFC` = 0, the fall start =
his position, `StIdleFall` (which can catch another bar, rope or pole).

## Functions and globals

Names used above, applied in the IDA database (functions that were still `sub_*`, and the globals).

| address | name |
|---|---|
| `0x4832BF` / `0x466F19` / `0x487FF3` | `Pole__ctor` / `Bar__ctor` / `Rope__ctor` |
| `0x4833E8` | `Pole__AddToRoom` |
| `0x48835A` | `Rope__IsPrinceInReach` |
| `0x48848E` | `Rope__Reset` |
| `0x48851E` / `0x488575` / `0x4885D0` | `Rope__UpdateIdle` / `Rope__UpdateSwing` / `Rope__UpdateEndSwing` |
| `0x4885FE` / `0x4885F1` | `Rope__SetUpdate` / `Rope__RunUpdate` |
| `0x455918` | `Trap__SetCollideable` |
| `0x411F52` | `Prince__CollidePoleFilter` |
| `0x4D11E4` | `Prince__CanGrabPole` |
| `0x4D141C` | `Prince__CanMoveOnPole` |
| `0x4D1541` | `Prince__CanGrabRope` |
| `0x4D0602` | `Prince__IsLedgeOnBar` |
| `0x4D019F` | `Prince__CanHangOnBarAt` |
| `0x4D095D` | `Prince__CanHorizLaunch` |
| `0x4D20F1` / `0x4D21E0` / `0x4D217D` / `0x4D21F7` | `Prince__GetAttachActivity` / `Prince__SetAttachTarget` / `Prince__GetAttachPhase` / `Prince__SetSwingLoop` |
| `0x4C7A6A` | `Prince__StopActivity` |
| `0x494129` / `0x494273` / `0x4941CE` | `K2AttachActivity__CreateAtTransform` / `…AtNode` / `…AtTransform2` |
| `0x48D8B3` | `K2AttachActivity__SetLooping` |
| `0x48F2B2` | `K2Activity3D__Create` (clip, gravity, velocity, flags) |
| `0x4D3C0E` … `0x4D4762` | `Prince__StStartPoleMount`, `StStartPoleHang`, `StIdlePoleHang`, `StStartPoleMove`, `StIdlePoleMove`, `StStartPolePushOff`, `StIdlePolePushOff` |
| `0x4D4818` … `0x4D554F` | `Prince__StStartHorizHang`, `StIdleHorizHang`, `StStartHorizHangMove`, `StIdleHorizHangMove`, `StStartHorizSwing`, `StIdleHorizSwing`, `StStartHorizLaunch` |
| `0x4D586F` / `0x4D5ACA` / `0x4D5C4A` | `Prince__StStartSwingRope` / `StIdleSwingRope` / `StStartSwingRopeRelease` |
| `0x4B3837` / `0x4B5117` | `Prince__StStartHang` / `Prince__StIdleFall` |
| `0x409D62` / `0x4103FE` | `Camera__SetMode` / `PrinceCollider__SetShape` (*guesses*) |
| `0x7FDCF0` / `0x7FFB74` / `0x7E9344` | `g_RopeGrabbed` / `g_Prince` / `g_Camera` |
| `0x89F8E0` / `0x89F918` / `0x89F968` / `0x89F988` / `0x89F958` | `g_VecDown` (0,−1,0) / `g_VecUp` / `g_VecForward` (0,0,1) / `g_VecX` (1,0,0) / `g_VecGravity` (0,−9.8,0) |
| `0x74AA98..0x74AB20` | the clip-name pointer table for these states |

`[PrinceObject+0x1D4]` fields used here: `+0x58` pole touched this frame, `+0x5C` rope in reach,
`+0x60` the pole or rope held.

## Not traced

- How a ledge and its attach transform are found (`0x4CF65C`, `0x4D0980`), so the exact bar grab
  height and reach.
- How `K2AttachActivity` combines the attach transform with the clip's root track.
- What gait −2 after a pole push-off changes in `StStartFall`, and what flag 5 vs 6 means in
  `K2Activity3D__Create`.
- The timed window `[+0x1D0]+0xC8` (low gravity, longer grab) and the fall-height scale
  `+0xB8` / `+0xC0`.
- What the floor generator does with the room's pole list.
- Rope vtable slots 45/46 (`0x4884CF`, `0x4884FA`; *guess*: save and load).
