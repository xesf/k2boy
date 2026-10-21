# Locomotion and transition clips

How the Prince chooses his walking, running, stopping, turning and sidestep clips, recovered
from `data/pop3dreal.exe`. The port does not use these clips yet: it plays `Walk_L` and
`Running_L` as loops (`set_clip`, `walk_speed` in `src/main.cpp`).

Everything here is read from the disassembly. Lines marked *guess* are interpretations.

## The short version

- The player Prince never plays `Walk_L` or `Running_L` as loops. Walking and running are
  chains of **single-step clips**: each footfall is one `StepWalk*` or `StepRun*` clip. Before
  each step the activity predicts where the foot lands, probes the floor there, and picks the
  `Up`, `Down`, `Long`/`Normal` or plain variant from the height difference.
- Starting to run from a stop uses the `StartRun*Right_L` variants for the first step.
  Stopping from a run plays one `StepEndRun*` step. Stopping from a walk blends straight to
  `BasePose_L`.
- `Walk_L` / `Running_L` are played as loops only when `object+0x128 != 0`
  (`Object__IsCharacter` `0x4647F8` returns false). For the player this is 0.
- Turning in place plays `PivotLeft_G` / `PivotRight_G` once per input, repeated while the key
  is held. No angle threshold exists in code: the clip's own rotation sets the angle.
- These clips are not referenced by name anywhere in the exe: `RunAntic_L`,
  `RunningQuickTurnShort_G`, `PushWallPlate_M`, `WalkStart`/`WalkEnd`. The binary contains
  no such strings.

## Input bits (`PrinceObject +0x154`)

`Prince__OnInputEvent` (`0x4C80FC`) writes them. The value is copied from `+0x150` and ORed
with `+0x14C` each frame in `Prince__UpdateFrame` (`0x4CACB9`).

| bit | meaning | evidence |
|---|---|---|
| `0x1` / `0x2` | turn left / right | sign of the turn axis `[+0x1D0]+0x08` |
| `0x4` / `0x8` | forward / back | sign of the move axis `[+0x1D0]+0x0C` (negated into `+0x14`) |
| `0x10` | jump | event `0x4E4`. `StStanding` sends it to `StStartStandJump` |
| `0x20` | Walk key | event `0x4E5`, action "Walk" |
| `0x40` | quick turn | event `0x509`. Sends to `StStartQuickTurn` |
| `0x100` | crouch | event `0x4E7` |
| `0x200` | Sidestep Modifier | event `0x4E2` |
| `0x400` | Sidestep Left/Right key | event `0x501`, which also sets bit 1 or 2 from the sign |
| `0x800` | action | event `0x4E6` |

The event-to-action mapping (event = `0x4DC` + index in the action-name table `0x743E60`) is
*guessed* from the order of the names. The bits are confirmed by how the states use them. The
turn and move axes have a dead zone of ±0.2 (`flt_743DA0` / `flt_743AD8`) wherever the states
take their sign.

**Running is the default.** With Walk (`0x20`) held, he walks. `StStanding` mirrors bit
`0x20` into `+0xDC` bit `0x10`.

## Movement state block (`[PrinceObject+0x1D0]`)

| offset | meaning |
|---|---|
| `+0x38` | double: earliest time a running jump is allowed |
| `+0x44` | transition value passed to the new activity's vfunc `+0x14`, then cleared. It is 200.0 (`0x43480000`) or 0. *Guess:* blend time in ms |
| `+0xF0` | side sign: sidestep −1 left / +1 right. Also used by Wobble |
| `+0xF4` | Wobble request: +1 = step back on, −1 = step off, −2 = fall |
| `+0xF8` | gait: 0 stand/turn, 1 walk, 2 run, 3 landed from a running jump, −1 backing up |
| `+0xFC` | turn sign (+1/−1/0) |

`Prince__SetNoiseRadiusSq` (`0x4C7703`) stores `PrinceObject+0x178`. Values: standing 0.0,
walk, sidestep and back-up 9.0 (`kNoiseRadiusSqWalk`), run 36.0 (`kNoiseRadiusSqRun`). It is
compared against a squared distance in `0x4C76B0`, called from `PrinceFx__Guard`. *Guess:*
how far guards hear him, 3 m walking and 6 m running.

State changes go through the "decision point" flag `+0xD8` bit 31. The idle states act on
input only when it is set. Other `+0xD8` bits: `0x20` contour failed (no floor, so fall),
`0x20000000` blocked, `0x40000000` activity ended.

## States

| state | address | clip(s) | activity |
|---|---|---|---|
| `StStartStanding` | `0x4B0D78` | `BasePose_L` | `K2GestActivity` (9, 0x10) |
| `StStanding` | `0x4B0F1B` | — | input dispatch (below) |
| `StStartLocomote` | `0x4B1B80` | walk / run step sets | `K2Loco8Activity` / `K2ActivityRun` |
| `StIdleLocomote` | `0x4B200E` | — | stop, switch, quick turn, jump |
| `StStartRunStop` / `StIdleRunStop` | `0x4B23C8` / `0x4B26B8` | `StepEndRun*` | `K2ActivityRunStop` |
| `StStartSideStep` / `StIdleSideStep` | `0x4B27F5` / `0x4B2975` | `SidestepLeft_L` / `SidestepRight_L` | `K2LocoActivity` |
| `StStartBackup` / `StIdleBackup` | `0x4B2BCD` / `0x4B2D90` | `WalkBackwards_L1` or `_L2` | `K2LocoActivity` |
| `StStartTurning` / `StIdleTurning` | `0x4B5CF9` / `0x4B5E89` | `PivotLeft_G` / `PivotRight_G` | `K2GestActivity` (6, 0) |
| `StStartQuickTurn` / `StIdleQuickTurn` | `0x4B6186` / `0x4B62E2` | `QuickTurnAround_G` | `K2GestActivity` (6, 0) |
| `StStartMovingQuickTurn` / `StIdleMovingQuickTurn` | `0x4B361A` / `0x4B3745` | `RunningQuickTurn_L` | `K2GestActivity` (6, 0) |
| `StStartWobble` / `StIdleWobble` | `0x4B1845` / `0x4B197A` | `Correct{Left,Right}{On,Off}_G` | `K2GestActivity` (6, 0) |
| `StStartStepBack` | `0x4B4CC8` | — | climb check, else stand |

The exe names the quick-turn clip `QuickTurnAround_G`. The file on disk is
`QuickTurnaround_G.skl`.

## From standing (`StStanding` `0x4B0F1B`)

The checks run in this order. Probes are a collision test (`0x4117B7`) of the body moved 0.1
(`kStandMoveProbeDist`, `0x74AB50`) in the given direction.

| input | result |
|---|---|
| quick turn `0x40` | `StStartQuickTurn` |
| jump `0x10` | `StStartStandJump` (see [jumps.md](jumps.md)) |
| (`0x200` or `0x400`) and turn L/R | sidestep probe to that side. If clear: `StStartSideStep`, blend 200 |
| forward `0x4` | forward probe. Clear: `StStartLocomote`. Blocked and Walk held: pole mount if one is in reach, else `StStartStepBack`. Blocked and running: nothing happens |
| back `0x8` | back probe. If clear: `StStartBackup`, blend 200 |
| turn L/R, no sidestep bits | `StStartTurning` |
| crouch `0x100` | `StStartStandToCrouch` |
| action `0x800` | trap interaction (grasp, lever, arrow charger, crate) |
| nothing for 7.0 s (`kIdleAmbientDelay`) | `StStartAmbient` |

Before input is read: when `|[+0x1D0]+0xF4| == 1`, the state sends him to `StStartWobble`
(after a 0.2 s guard, `dbl_74AB58`).

## Starting to move (`StStartLocomote` `0x4B1B80`)

Walk is chosen when bit `0x20` is held, unless gait `+0xF8` is 3 (a landed running jump forces
a run). At the end, gait is set to 1 (walk) or 2 (run).

### Walk: `K2Loco8Activity`

The activity is built by `K2Loco8Activity__Create` (`0x49B7CF`) with these 8 clips, in index
order:

| index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| clip | `StepWalkUpLeft_L` | `StepWalkUpRight_L` | `StepWalkLeft_L` | `StepWalkRight_L` | `StepWalkDownLeft_L` | `StepWalkDownRight_L` | `StepWalkLongLeft_L` | `StepWalkLongRight_L` |

It also receives the two foot-node names (`[+0x1C0]+0x54`, `+0x58`) and these parameters:
up threshold 0.15 (`kWalkStepUpMin`, at `+0x90`), down threshold −0.15 (`kWalkStepDownMax`,
`+0x94`), turn radius `fWalkTurnRadius` 1.0 (`+0x48`), and max step 0.55
(`kWalkMaxStepHeight`, `+0x4C`).

`K2Loco8Activity__ChooseNextStep` (`0x49B280`) runs before every step:

1. Toggle the foot `+0x84` (0 = left, 1 = right).
2. Predict the foot's landing point from the per-clip foot offsets (`+0x6C`). Probe the floor
   there (`0x411D56`, ±0.55). `dh` = floor − predicted. With no floor, `dh` = 1.55.
3. Pick the clip:

| `dh` | clip | returns |
|---|---|---|
| > 0.55 or < −0.55 | `StepWalk{L,R}` | fail |
| > 0.15 | `StepWalkUp{L,R}` | ok |
| −0.15 … 0.15 | `StepWalkLong{L,R}` if the previous step was the other foot's plain or Long step and the Long stride also lands within ±0.15 of this height. Otherwise `StepWalk{L,R}` | ok |
| −0.55 … −0.15 | `StepWalkDown{L,R}` | ok |

On success, `dh` is added to the root offset's y (`+0x50`). The engine lifts or drops the body
by the measured step. The step's horizontal travel comes from the clip itself: the stride table
is built from the clips' foot nodes. *Guess:* this is the clip's root motion.

### Run: `K2ActivityRun`

The activity is built by `K2ActivityRun__Create` (`0x490EA1`). Its parameters: run set
(`+0x70`), `fMinRunClimbHeight` 0.3 (`+0x78`), `fMaxRunNormalHeight` 0.3 (`+0x7C`),
`fMaxRunHeightDiff` 0.2 (`+0x80`), `fRunTurnRadius` 1.75 (`+0x48`), and max step 0.65
(`kRunMaxStepHeight`, `+0x4C`). The tuning keys are in the `dword_801C2C` block (see
[jumps.md](jumps.md)).

The set is chosen from the previous gait `+0xF8`:

| previous gait | set (`K2RunCachedSpec`, 12 slots) | built by | run-jump delay added to `+0x38` |
|---|---|---|---|
| 2 (already running) | `g_RunSpecStepRun` `0x7FE528` | `0x490F31` | 0.0 |
| 3 (landed a running jump) | `g_RunSpecJumpLand` `0x7FE608` | `0x491072` | 0.8 |
| 0 (standing) | `g_RunSpecStartRun` `0x7FE6E0` | `0x4911B3` | 0.8 |
| other (walking) | `g_RunSpecStartRun` | | 0.8 |

Slots 0–7 are the same in all three sets: `StepRunUpLeft`, `StepRunUpRight`, `StepRunLeft`,
`StepRunRight`, `StepRunDownLeft`, `StepRunDownRight`, `StepRunNormalLeft`,
`StepRunNormalRight` (all `_L`). Slots 8–11 are the **first step**:

| slot | role | StartRun set | StepRun set | JumpLand set |
|---|---|---|---|---|
| 8 | flat | `StartRunNormalRight_L` | `StepRunNormalRight_L` | `StepRunJumpLandLeft_L` |
| 9 | fallback | `StartRunRight_L` | `StepRunRight_L` | `StepRunJumpLandLeft_L` |
| 10 | up | `StartRunUpRight_L` | `StepRunUpRight_L` | `StepRunUpJumpLandLeft_L` |
| 11 | down | `StartRunDownRight_L` | `StepRunDownRight_L` | `StepRunDownJumpLandLeft_L` |

The flag at `+0x30` (1 for StartRun/StepRun, 0 for JumpLand) sets the first foot: right or
left. *Guess:* this matches the clip names.

`K2ActivityRun__ChooseNextStep` (`0x49023F`) uses `tol` = 0.65, or 0.715 on the first step
(×1.1, `kRunFirstStepTolScale`). A pick (`CollisionWorld__Pick`, mask 6) from the body to the
predicted foot that hits something counts as "no floor".

| `dh` | later steps | first step |
|---|---|---|
| > `tol`, < −`tol`, or no floor | `StepRun{L,R}` (fallback) | slot 9 |
| > 0.3 | `StepRunUp{L,R}` | slot 10 |
| −`tol` … −0.3 | `StepRunDown{L,R}` | slot 11 |
| −0.3 … 0.3 | `StepRunNormal{L,R}`, after a second probe with the Normal stride (`dhn`) succeeds: `|dhn|` < `tol`, and if `|dhn|` > 0.3, `|dhn − dh|` ≤ 0.2 (`fMaxRunHeightDiff`). Otherwise the fallback | slot 8, same test |

So **plain `StepRunLeft/Right` is the fallback**, used when the floor is lost, and `Normal` is
the flat-ground step. For walking it is the other way round: plain is flat and `Long` is the
extra stride.

For `Up` steps (indices 0, 1, 10) the code passes `0.5 − dh` to vfunc `+0x48`. For `Down`
steps (4, 5, 11) it passes `−0.5 − dh`. *Guess:* the up and down clips are authored for a
0.5 m step, and the code corrects for the measured height. `dh` is also added to the root
offset y, as for walking.

### Scripted actors

When `+0x128 != 0`, the state builds a plain `K2LocoActivity` (`0x49D281`) on
`Walk_L`/`Running_L` instead: param 0.5, the turn radius, and 10 (walk) or 0 (run).

## While moving (`StIdleLocomote` `0x4B200E`)

Without the decision flag, nothing changes ("Keep Going"). On the flag:

| condition | result |
|---|---|
| contour failed | running: `StStartRunFall`. Walking: `StStartFall` |
| blocked (`0x20000000`) | Walk held and a pole in reach: pole mount. Otherwise "Just stop": `StStartStepBack`, which climbs if Walk is held and a ledge is reachable, else stands (blend 200). No `PushWall*` clip is played |
| activity ended | `StStartStanding` |
| jump, running, time > `+0x38` | `StStartRunJump`, blend 200 |
| jump while walking | `StStartStanding`, blend 200. Then a standing jump |
| quick turn, gait 2 (running) | `StStartMovingQuickTurn` |
| quick turn, walking | `StStartStanding`, blend 200 |
| forward held, no sidestep bits, Walk state ≠ gait | restart `StStartLocomote` ("Recalculate"): switches walk and run |
| forward released (or sidestep held), running | `StStartRunStop`, blend 200 |
| forward released, walking | `StStartStanding`, blend 200 |

A running jump is possible only 0.8 s after starting to run from a stand, a walk or a landing
(`kRunJumpDelay*`, `0x74A8D4..DC`). When he was already running, it is possible at once.

## Stopping a run (`StStartRunStop` `0x4B23C8`)

The state snaps him to the floor (probe ±0.55) and builds `K2ActivityRunStop`
(`0x49340F`). The clips are `StepEndRunUpLeft`, `StepEndRunUpRight`, `StepEndRunLeft`,
`StepEndRunRight`, `StepEndRunDownLeft`, `StepEndRunDownRight` (all `_L`), plus the foot-node
names. Parameters: the starting foot (from `Prince__RunStopFirstFoot`, `0x4CDD87`, *guess*:
the forward foot), `fMinRunClimbHeight` 0.3 (`+0x90`), turn radius (`fRunTurnRadius`, or 0
when a sidestep bit is held) and max step 0.65. Gait stays 2.

`K2ActivityRunStop__ChooseNextStep` (`0x492D97`) toggles the foot and probes:

| `dh` | clip |
|---|---|
| > 0.3 and ≤ 0.65 | `StepEndRunUp{L,R}` |
| −0.65 … −0.3 | `StepEndRunDown{L,R}` |
| otherwise | `StepEndRun{L,R}` |

`dh` goes into the root offset y. `StIdleRunStop`: contour failed sends him to
`StStartRunFall`. Blocked sends him to `StStartStepBack`. Ended sends him to
`StStartStanding` with no blend.

## Turning

**In place** (`StStartTurning`). Turn bit `0x1` plays `PivotLeft_G`, otherwise `PivotRight_G`,
once. `StIdleTurning` acts at the decision point:

- forward or back held and the 0.1 probe clear: stand (blend 200), which then starts moving
- a sidestep bit, or the turn key released: stand
- turn direction changed: restart the turn
- clip finished (`+0xDB` bit `0x40`): play the pivot again

The turned angle is whatever the pivot clip rotates. No degree threshold is in code.

**180° from standing** (`StStartQuickTurn`): `QuickTurnAround_G` once, then `StStartStanding`
with no blend (blend 200 if it hits a wall).

**180° while running** (`StStartMovingQuickTurn`): `Prince__CanRunningQuickTurn` (`0x4D18D9`)
needs floor 1.0 and 2.0 ahead, within 0.01 (`dbl_743AD0`) of the current height, and a clear
body probe at both points. If the check passes, he plays `RunningQuickTurn_L` ("Long
Quickturn"). If it fails, he goes to `StStartRunStop`. At the end: forward held goes to
`StStartLocomote` (gait is still 2, so the StepRun set), else `StStartStanding` with blend
200. `RunningQuickTurnShort_G` is never named in the exe.

## Sidestep (`StStartSideStep` `0x4B27F5`)

Bit `0x1` plays `SidestepLeft_L` with side sign −1. Otherwise he plays `SidestepRight_L` with
+1. Both use a looping `K2LocoActivity` (0, 0.5, 10, 0x20); gait is 0. `StIdleSideStep` keeps
it going while the side key is held **and** Walk, Sidestep Modifier or a Sidestep key is held.
It stands (blend 200) on jump, on forward/back without sidestep bits, on releasing the
direction, on contour failure or on being blocked.

## Backing up (`StStartBackup` `0x4B2BCD`)

`"WalkBackwards_L"` + `RandomOneTo(2)` (`0x4A9FC5`) gives `WalkBackwards_L1` or `_L2`. It is
played as a looping `K2LocoActivity` with radius −`fWalkTurnRadius` (×−1 when `byte_7EAC48`
is set), 0.5, 9 and 0. Gait −1.

## Edge correction (`StStartWobble` `0x4B1845`)

When a gesture ends with one foot over nothing ("WATCHWOBBLE" in
`K2GestActivity__CheckFootContact` `0x496B31`), the gesture checks for room:

- room to step back on: `+0xF4` = +1
- room to step off: `+0xF4` = −1
- neither: `+0xF4` = −2, and he falls

`+0xF0` = ±1 from which foot node is off. `StStartWobble` then plays:

| `+0xF4` | `+0xF0` = 1 | otherwise |
|---|---|---|
| 1 | `CorrectRightOn_G` | `CorrectLeftOn_G` |
| otherwise | `CorrectRightOff_G` | `CorrectLeftOff_G` |

## Other

`SlideForwardDownRamp_G` is referenced only from `StStartSliding` (`0x4B90FC`, via pointer
`0x74AB44`). Not traced further.

## Suffixes

The code does not decode suffixes. From usage (*guess*):

| suffix | used for |
|---|---|
| `_L` | locomotion: loops (`K2LocoActivity`) and step chains (`K2Loco8Activity`, `K2ActivityRun*`), which move him by the clip |
| `_G` | one-shot gestures in place (`K2GestActivity`): pivots, quick turns, corrections. `BasePose_L` and `RunningQuickTurn_L` are also played as gestures, so the rule is not strict |
| `_M` | `_M` clips are climbing, hanging and scripted moves. They are not used by the states in this document |

## Not traced

- How the activities turn the clip's root track into world motion, and how the turn radius
  (`+0x48`) bends the path.
- The meaning of vfunc `+0x14` (gets the 200/0 value) and vfunc `+0x48` (gets `±0.5 − dh`).
- Which foot the first walk step uses (`K2Loco8Activity +0x84` is not set in its constructor).
- What `Prince__RunStopFirstFoot` (`0x4CDD87`) measures.
- The wall check at the start of `K2ActivityRun__ChooseNextStep` (vfunc `+0x38`, 0.3 limit,
  then vfunc `+0x30(0x18)`).
- Which objects have `+0x128 != 0` and use the plain loops.
