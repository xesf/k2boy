# Guard awareness and pursuit

How guards notice the Prince, chase him, stop to fight, back off and give up. Everything here is
read from the disassembly of `data/pop3dreal.exe` (capstone on the PE; see the note at the end
about IDA). Guesses are marked. The sword-fight state machine itself is in `combat-moves.md`; the
Prince's noise values are in `locomotion.md`.

## enemy.dat loading

`EnemyDat__EnsureLoaded` (`0x4A6C4F`) opens `enemy.dat` once (flag `0x7FF108`) through the parser
vtable at `0x749BF8`. Slot +8, `EnemyDat__Parse` (`0x4A6C82`), reads nine numbers (one per
non-comment line, in file order) and then the `[Archery]` block. Comments and the key names in the
file are ignored; only the order matters.

| Global | Value | `enemy.dat` line | Used by |
|---|---|---|---|
| `0x7C5838` | 25 | distFarFromAnywhere | **nothing** (only the loader touches it) |
| `0x7C583C` | 10 | distFarFromHome | `StEnemyActive`, `StEnemyInContact` (home leash) |
| `0x7C5840` | 12 | distFarFromEnemy | `StEnemyActive` (0x4AA751), `StEnemyWatchful`, `StEnemyInContact` |
| `0x7C5844` | 10 | distApproachEnemy | default of `EnemyAI+0x110` (`0x4AA30C`) |
| `0x7C5848` | 0.15 | distFightWithEnemy | `StEnemyInContact` (0x4AAAD8) |
| `0x7C584C` | 1.1 | distTooCloseToEnemy | `StEnemyInContact` (0x4AABA5) |
| `0x7C5850` | 1.5 | distCamoflauge | `Guard__CanSeePrince` (0x4A786D) |
| `0x7C5854` | 15 | swing-while-running damage | `StSwingWhileMoving` (0x4BAB76), as an int |
| `0x7C5858` | 30 → 0.866 | peripheral vision angle | loader converts degrees to `cos(30 deg)` (`0x52CEF4` is `fcos`) |

`0x7FFB74` is the Prince actor (`g_Prince`). `0x7FF0B0` is a double, the time until which the
"charming lights" (a trap or lamp effect) hold guards (`StWatchCharmingLights`,
`StDistractedByCharm`). Guard clock: `actor+0xF0` (double, seconds).

Quirk: `StEnemyPatrolAttack` (0x4BA9C4) **overwrites `0x7C5840` with FLT_MAX** and sets the
guard's own `EnemyAI+0x110` to FLT_MAX when a fight starts. After the first such fight every
guard that uses the Active states below watches from "infinitely" far. Unintended, but it is
what the binary does.

## Structures used

| Offset | Meaning |
|---|---|
| `actor+0x158` | target actor (the Prince, set in `StEnemyPatrolInit`) |
| `actor+0x1C0` | character data; `+0xF0` attribute bits (1 walk, 2 crawl, 4 prefer crawl, 8 has weapon, `0x10` can fight, `0x20` not a valid target) |
| `actor+0x1C4` | Fight object (`combat-moves.md`); `+0x124` distance to target in XZ, `+0x128` signed angle to target, `+0x1F0` shortest attack range, `+0x1F4` longest |
| `actor+0x1CC` | guard AI object (`EnemyAI`): `+0x78` alert phase, `+0x80` double timer, `+0xCC` last known Prince position, `+0x110` approach distance, `+0x114` AI mode |
| `actor+0x1D0` | movement object: `+0x14` forward speed request (1, 0, −1), `+0x18/+0x20` home x/z, `+0x40` home radius |
| `actor+0xDA` bit 1 | "engaged", set by `StEnemyActive` → `StEnemyWatchful` |
| `actor+0xDC` bit 0 | fight mode (sword out). Set in `StPrepareAttackMode` (0x4C5FAC) and `StPrepareToCharge`; cleared in `StQuitAttackMode`, `StSheathSword` paths |
| `actor+0xD8` | state flags: bit 31 decision point, bit 30 activity ended, bit 29 blocked, bit 24 injured |

`EnemyAI+0x114` mode, chosen in the EnemyStart builder (`0x4A8A32`, `0x4A8F0C`, `0x4A91A1`) and
used by `StAssignAIMode` (`0x4AA3F4`):

| Mode | Meaning | First state |
|---|---|---|
| 0 | no AI data ("Active" guard) | `StStartEnemyStanding` → `StEnemyActive` |
| 1 | patrol (waypoints present) | `StEnemyPatrolInit` (0x4B9DC8) |
| 2 | stand guard at a spot (default) | `StEnemyPatrolInit` → `StFindGuardSpot` (0x4BA22E) |
| 3 | animatronic | `StAnimatronicInit` |
| 4 | archer | `StAIArcherInit` (0x4BECEB) |

Most level guards get mode 1 or 2. The distance machine of `enemy.dat` (mode 0 states) also runs
for every guard once a patrol fight begins, because `StEnemyPatrolAttack` hands over to
`StEnemyInContact`.

## Seeing: `Guard__CanSeePrince` (0x4A777C)

`this` = `EnemyAI`, one argument: a cosine threshold. Returns true when all pass, in order:

1. Prince exists with health ≥ 1.
2. `0x7FF0B0` (charming lights) is not later than the guard clock, and the guard is not dazed
   (`actor+0x188` not after the clock).
3. Vertical gap between the two actors' feet is at most **2.0** (`|dy| > 2` fails). Archers
   (`0x4C1AEB`) skip this.
4. Camouflage: if the Prince is camouflaged (`PrinceFx+0xEC` later than the clock, `0x4A796B`) and
   the distance is above **1.5**, he is not seen. If the possessed/zombie flag (`+0xDC` of the same
   object, `0x4A7964`) is set he is never seen.
5. Distance `d` is taken between guard eye (feet + 2.0 y) and Prince (feet + 0.5 y). Let `z` be
   the component of the direction to him along the guard's facing.
   - `z > 0` and `d < 3.0`: **seen, no line-of-sight test** (anything in front within 3 m).
   - otherwise `z` must be at least the argument. Patrol states pass `0.866`
     (a ±30° cone). Callers that want a full circle pass −2.
6. Ray cast between the eyes (collision mask 7). Seen only if nothing blocks it.

There is **no maximum range** other than the ray. `distFarFromEnemy` plays no part in patrol
sight. Crouching, crawling or running do not change sight at all; only the camouflage potion and
the lights do.

## Hearing: `Prince__CanHear` (0x4C76B0)

`this` = Prince, arguments: the guard's position and a scale (always 1.0). Computes the squared
distance with **y doubled** and returns true if it is below `scale × PrinceObject+0x178`.
`+0x178` is the squared noise radius set by `Prince__SetNoiseRadiusSq` (`0x4C7703`) on every
state start.

| Prince action | `+0x178` | Radius |
|---|---|---|
| standing, crouching, hanging, `StStartStanding`/`StStartCrouching`/`StStartCrawlingPose` | 0 | 0 m |
| crawling (`StStartCrawling`) | 1 | 1 m |
| walking, sidestep, back-up, climbing, swimming, jumps (`9.0` at 0x74A8E8) | 9 | 3 m |
| bounce-back | 16 | 4 m |
| running, treading water, sliding, **sword draw** (`StDrawSwordIdle`, 0x4AF81E) | 36 | 6 m |
| landing from a fall (`0x4B5BC2`) | 64 | 8 m |

The value stays until the next state start overwrites it. **Drawing the sword is a 6 m noise.**
It is applied in `StDrawSwordIdle` once `actor+0xD9` bit 1 is set (a mid-clip event, *guess:*
the draw marker). Sheathing does not set a noise (the standing state resets it to 0).

Hearing alone never triggers anything. Every use also needs `CanSeePrince(-2)` (a 360° ray test),
so a sound does not pass walls. It only decides which reaction follows (below).

## Reactions by AI mode

### Stand-guard idle: `StWaitAndWatch` (0x4BA6B0, via `StGuardFixedSpot`)

Per frame, in order: dying (`StStartDyingMode`), injured (`StInjuredOnPatrol`), charming lights
(`StWatchCharmingLights`), then
1. `CanSeePrince(0.866)` → play the "sees" voice line (`character+0x90`), remember the position
   (`0x4C7A6A`), go to `StPrepareToCharge`.
2. Else, if a target exists and `CanHear(1.0)` and `CanSeePrince(-2)` → `ai+0x80 = now + 2.0`,
   `StPrepareToCharge` ("Heard and seen").

### Patrolling or walking to the guard spot: `Guard__CheckForPrince` (0x4BDFD8)

Called from `StEnemyPatrolToSpot` (0x4BA185) and `StGoToGuardSpot` (0x4BA459) while the decision
bit is set.

| Check | Result |
|---|---|
| charming lights | `StDistractedByCharm` |
| `CanSeePrince(0.866)` | "sees" voice line. With a pursuit waypoint (`ai+0xBC ≥ 0`) → `StFacePursuitWaypoint`. Otherwise store the position, play the notice clip (`CombatAmbient`-class clip 0x4E/0x52 through `Fight__ResolveClipName`, picked by whether his sword is out and by Manhattan distance below/above 100) and go to `StPrepareToCharge` |
| `CanHear(1.0)` and `CanSeePrince(-2)` | Manhattan XZ distance (`0x4BA3CC`, |dx|+|dz|) < **8** → treat as seen. Otherwise `ai+0x80 = now + 0.2`, `ai+0x78 = 2`, `StStopToListen` (0x4BACA5) |

`StStopToListen`: when the turn clip ends, if `CanSeePrince(0.866)` → "Heard and seen:
charge" → `StPrepareToCharge`; else if the 0.2 s window has not passed → `StInvestigateSound`
(walk to the heard position); otherwise it stays put (*guess:* a dead end in the binary).

### Charge sequence (modes 1 and 2)

| State | Address | What it does |
|---|---|---|
| `StPrepareToCharge` | 0x4BADE7 | when the clip ends: if the target is alive and the lights are off, set `actor+0xDC` bit 0 and `+0xD8` bit 17, `StDrawSword`, then `StBeginCharge`; else back to `StEnemyPatrolInit` |
| `StBeginCharge` | 0x4BAE8F | if Manhattan distance < **5** → `StEnemyPatrolAttack`. If he can walk straight to the Prince, play `CombatTaunt` (0x52, or `CombatAmbient` 0x4E when unarmed) and start a run (`Running_L`) to `StChargePrince`; if the Prince is behind him ("behind me") turn first (`StTryToReachPrince`); if a straight path is not possible, `StMoveTowardPrince` |
| `StChargePrince` | 0x4BB467 | tracks the Prince's live position (no search delay). Manhattan < 4, vertical gap in (−0.5, 1] → `StEnemyPatrolAttack`. Target dead or gone → `StSheathSword`, `StResumePatrol`. Within 3 of the last known spot, he "tries to get close" at speed −1.5 |
| `StTryToReachPrince` | 0x4BBB97 | re-plans the path every decision point. Manhattan < 4, vertical in (−1, 0.5), sees him → `TurnToChop` → `StEnemyPatrolAttack` |
| `StCantReachPrince` | 0x4BB878 | no path (gap, ledge): idle with taunts (`StCantReachIdle`), taunt clip when the Prince is within 4 m (squared 16), else the base pose |

**Give-up rule (the only one in the patrol branch)**, in `StTryToReachPrince` (0x4BBF55): if the
Manhattan distance is ≥ **5** and `CanSeePrince(-2)` fails ("can't see him anymore"), he clears
the engaged flags, sheathes (`StSheathSword`) and goes to `StResumePatrol` (0x4BDAF1), which walks
back to the patrol point or `StFindGuardSpot`. There is no distance limit and no timer: while he
keeps line of sight he never stops. While the Prince holds a drawn bow (`PrinceObject+0xDC & 3`
with `0x4C1AEB`), the guard goes to `StFindHidingPlace` (0x4BCFB6) / `StRunToHidingPlace` instead.

### Contact: `StEnemyPatrolAttack` (0x4BA831)

Entered at Manhattan < 4 or 5 (above). Decision (0x4BA89E):

| Condition on the Prince | Result |
|---|---|
| fight mode (`actor+0xDC` bit 0, sword out) or engaged (`+0xDA` bit 1) | `BeginningPatrolAttack`: the quirk above, `StSetupForFightMode` (`0x4C604D`, plays `CombatTaunt`) in the side slot and `StEnemyInContact` as the main state |
| sheathed | `StSwingWhileMoving` (0x4BAA34): a running swing with clip `CombatAttackRight`; timer `ai+0x80 = now + 2.0` |

`StSwingWhileMoving` damages the Prince by **15** (the enemy.dat value, `Prince__TakeDamage`
`0x4C760C`) when the vector to him is shorter than **2 m** (squared < 4), vertical within ±1,
he is in front, and `ai+0x80` has passed (`ai+0x80 < now`). After a hit that leaves him alive the
timer is set to −2^64, so this timer does not stop a second hit; the activity end
(`actor+0xDB` bit 6) leaves via `StSwingRecover` → `StBeginCharge`. *Unverified at runtime:* the
2 s head start before the first hit can land.

A guard without a weapon (`character+0xF0` bit 3 clear) sets his own fight bit here and fights
unarmed.

### Prince dying or dead

| Where | Behaviour |
|---|---|
| `StSwingWhileMoving`, hit drops him to < 1 | `StSetupThreateningAmbients` (plays clip 0x4E once the current clip ends) then `StWatchDyingPrince` (0x4BB78E): every 2 + rand(0..7) s plays `CombatAmbient`; stays until `StDeadMode` of the Prince is handled elsewhere |
| `StChargePrince`, target dead | sheathe, `StResumePatrol` |
| `StFightMgrMode` (0x4C6183), target health < 1 | sets `actor+0xDC` bit 2, `StVictoryMode` (0x4C6746): clears fight bits, and for modes 1 and 2 `ai+0x80 = now + 2`, `StSheathSword`, `StWatchDyingPrince` |
| `StEnemyActive` | does nothing while the Prince's health ≤ 0 |

### Guard-to-guard alerting

None found. Each guard senses the Prince on its own (`actor+0x158` is always the Prince for
guards). No code looks at another guard's fight state or broadcasts a target. The only shared
state is the `0x7C5840` overwrite above and the lights timer `0x7FF0B0`. The Prince's own
auto-target scan (`0x4C5AFA`: nearest valid actor within |dy| ≤ 2, ≤ 8 m, needs `dot/len ≥ 0.8`
once farther than 8 m, plus a ray test) is for the Prince's lock-on, not the guards.

## Active (mode 0) states: the `enemy.dat` distances

Used by guards with no EnemyStart AI, and by every guard after a patrol fight begins.

| State | Address | Behaviour |
|---|---|---|
| `StEnemyActive` | 0x4AA5CB | `d` = 3D distance to the Prince. Health < 1 → `StEnemyDead`. Home leash: XZ distance to home (`movement+0x18/0x20`) above `movement+0x40` (default `distFarFromHome` 10) → `StEnemyTurnHome` → `StEnemyGoHome` (walks home at forward speed 1.0). Else if `d ≤ distFarFromEnemy` (12), |dy| ≤ 2, and the character can fight (`+0xF0` bit `0x10`) → set `+0xDA` bit 1, `StEnemyWatchful` |
| `StEnemyWatchful` | 0x4AA83C | no angle or line-of-sight test. `d > distFarFromEnemy + 1` (13) → back to `StEnemyActive`. `d < approach` (`EnemyAI+0x110`, 10) → `StEnemyInContact`. `approach ≤ d < approach + 3` → `StBeginCharge` (run) |
| `StEnemyInContact` | 0x4AA95A | the stop-and-fight logic below |
| `StEnemyApproach` | 0x4AAC00 | zero the move request, then `StBeginCharge` |
| `StEnemyBackOff` | 0x4AAC6A | declared, empty |
| `StEnemyGoHome` | 0x4AACC3 | keeps walking while farther than 3 m from home; inside 3 m it returns to `StEnemyActive` unless it is still heading within 0.1 rad of home and farther than 1 m |

`StEnemyInContact` (per frame, `D` = `Fight+0x124`, XZ distance to the Prince):

| Test | Action |
|---|---|
| |dy| > 1.5, or `D > distFarFromEnemy + 1`, or target gone | clear engaged bit, `StEnemyActive` |
| `D > approach` | stop moving |
| patrol/stand mode, `D > 2.5`, path ok (`0x4A7C21`) | `StBeginCharge` again |
| `D > S − distFightWithEnemy` | move forward: outside the home radius → stop; `D > 3.5` → `StEnemyApproach` (run); else if the heading error is within 10° set move flag 4, speed +1.0 (a fighting step), otherwise speed 0 (turn first) |
| `D ≤ S − 0.15` and `D < L − distTooCloseToEnemy` | move flag 8, speed −1 (back up) |
| otherwise | stand and fight |

`S` and `L` are `Fight+0x1F0/0x1F4`: the **smallest and largest of the eight range numbers** of
the guard's own weapon, copied by `Fight__LoadWeaponRanges` (`0x4ABB6D`) when the fight engages
(`0x4AB953`). The eight numbers are `master.dat [Weapons]` "left, center, right, counter left,
counter right, special left, center, right" (`weapon+0x34..0x50`). Distances are center to center.
So "relative to shortest attack range" means: fight from `S − 0.15`; "relative to longest" means
back off below `L − 1.1`.

| Weapon (examples) | S | L | Fight at (≤ S − 0.15) | Back off (< L − 1.1) |
|---|---|---|---|---|
| PrinceSword&A1, PalaceGuard1&4, PalaceGuard2, BlackMarket1, GoldPaw | 1.9 | 2.1 | 1.75 | 1.0 |
| Assassin2L, EnemyStaffL, BlackMarket2L | 2.0 | 2.1 | 1.85 | 1.0 |
| Assassin3, A4DoubleBlade, Weird3 | 1.7 | 1.9 | 1.55 | 0.8 |
| Dirigible4L (D4) | 2.2 | 2.5 | 2.05 | 1.4 |
| ManRat, Grease, MegaMuck | 2.55 | 2.55 | 2.4 | 1.45 |
| MuckCreature | 3.05 | 3.05 | 2.9 | 1.95 |

## Speeds

There are no speed numbers in the AI. Movement is clip driven.

| Situation | Clip | Source |
|---|---|---|
| patrol walk | `Walk_L` | `StEnemyPatrolToSpot` call to `0x49F3AF` (`0x74A90C`) |
| charge, approach, return to the fight | `Running_L` (`0x74A908`) | `StBeginCharge`, `StTryToReachPrince` |
| turning in place | `PivotLeft_G` / `PivotRight_G` | `0x74AA7C/0x74AA80` |
| fighting step forward / back | speed request +1 / −1 on the movement object, flags 4 / 8 on `actor+0x154` | `StEnemyInContact` |

Walk and run speed follow the clips' root motion (`locomotion.md` method).

## What the model does differently (`enemy_update` in `src/main.cpp`)

| Topic | Binary | Current model |
|---|---|---|
| Notice range | no distance limit; ±30° cone + ray; any direction within 3 m in front, even through walls | 10 m (`distFarFromEnemy`) and 360° inside 5 m (`distApproachEnemy`) |
| Vertical | |dy| ≤ 2 m (archers exempt) | `same_floor` |
| Hearing | 0/1/3/4/6/8 m by Prince action, y doubled, needs 360° ray; sword draw 6 m | none |
| Camouflage | unseen beyond 1.5 m, possessed never seen | camo only on `PATROL`, same 1.5 |
| Charge start | Manhattan < 5 or 4 to attack; chase uses live position | switches at `FIGHT_DIST` 1.6 |
| Sheathed Prince | guard does a running swing for 15 and repeats, no duel | always duels |
| Stop distance | `S − 0.15` (1.75 for a 1.9/2.1 weapon), per weapon | `FIGHT_DIST` 1.6 |
| Attack reach | the weapon's ranges (1.7 to 3.05) | `REACH` 2.2 |
| Back off | `D < L − 1.1` (1.0), speed −1 | none |
| Give up | patrol branch: lost line of sight at Manhattan ≥ 5, then sheathe and resume. Active: `D > 13` or the home leash 10 | `d > watch × 1.5` |
| Prince dying | guard sheathes and idles with ambient clips | finishing move |
| Other guards | independent, no alerting | independent |

## Names and IDA

The IDA instance on port 13337 during this pass held a different database (`0x4AC845` was
`_iscntrl`; `0x4C604D` and `0x4BA831` were not functions), so **no renames were made**. The
analysis is offline. Suggested names, to apply when the right database is open:

`EnemyDat__EnsureLoaded` 0x4A6C4F, `EnemyDat__Parse` 0x4A6C82, `Guard__CanSeePrince` 0x4A777C,
`Prince__CanHear` 0x4C76B0, `Guard__CheckForPrince` 0x4BDFD8, `Actor__ManhattanXZ` 0x4BA3CC,
`Actor__AngleDistToPoint` 0x4C7DB5, `Fight__LoadWeaponRanges` 0x4ABB6D, `Prince__TakeDamage`
0x4C760C, `Prince__TargetNearestEnemy` 0x4C5AFA, `Fight__Disengage` 0x4ABC25. Globals:
`g_enemy_distFarFromAnywhere` 0x7C5838, `…FarFromHome` 0x7C583C, `…FarFromEnemy` 0x7C5840,
`…ApproachEnemy` 0x7C5844, `…FightWithEnemy` 0x7C5848, `…TooCloseToEnemy` 0x7C584C,
`…Camoflauge` 0x7C5850, `g_enemy_swingDamage` 0x7C5854, `g_enemy_peripheralCos` 0x7C5858,
`g_CharmLightsUntil` 0x7FF0B0, `g_Prince` 0x7FFB74.

## Open questions

- The meaning of `actor+0xD9` bit 1 (when the draw sword noise starts) and of `0x4C8896` in
  `StEnemyWatchful`.
- Whether `StSwingWhileMoving` can really land a hit only after its 2 s timer.
- The hearing voice line (`[Audio]` slot 1) is not played anywhere in the AI paths read; slot 2
  ("sees") is, at the three notice points above. Slots 3 and 4 ("within reach", "out of reach") are
  not played from the AI states read either (*guess:* the fight code).
