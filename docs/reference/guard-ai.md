# Guard AI: EnemyStart, patrol, fight and how our code differs

The guard state machine of `data/pop3dreal.exe` end to end, from the `EnemyStart` trap to the
death clip, and a prioritised list of where `src/main.cpp` does something else. Read together with
`guard-awareness.md` (sight, hearing, charge, the `enemy.dat` distance states) and `combat-moves.md`
(the `Fight` class: attack types, feints, counters, AI responses). This file does not repeat their
tables. It verifies them, fixes two errors, and adds what they left out: the `EnemyStart` argv, the
waypoint objects, skill and difficulty, the patrol loop, the AI attack schedule and the damage path.

Evidence: the offline exports in `.ida-export/` (`c/<ADDR>_*.c`, `pop3d.c`, `asm/`). The ida-mcp
server was down, so no IDA renames were made. "Verified" means I read the pseudocode again and,
where a number mattered, the asm. Guesses are marked.

## 1. What was verified, what was wrong

| Claim in `guard-awareness.md` / `combat-moves.md` | Result |
|---|---|
| `Guard__CanSeePrince` 0x4A777C: dy <= 2 (archers exempt), eye +2.0 / target +0.5, camo > 1.5, in front within 3 m is seen, else cos >= arg, ray mask 7 | Verified line by line. The camo and 3 m tests use the 3D eye-to-eye length, not XZ. |
| Dazed guards (`actor+0x188` later than the clock) and the charm-light timer `0x7FF0B0` block sight | Verified (first two tests of the function). |
| `StEnemyPatrolAttack` 0x4BA831 picks fight vs swing from the **Prince's** bits (`+0xDC` bit 0, `+0xDA` bit 1) | Verified: `v4 = this+0x158` is the target, the tests read `v4+220` and `v4+218`. A guard with no weapon (`character+0xF0` bit 3 clear) sets its own `+0xDC` bit 0 first. |
| `StEnemyInContact` 0x4AA95A: stop at `S - 0.15`, back off below `L - 1.1`, leave at `D > distFarFromEnemy + 1`, run when `D > 3.5`, step forward when heading error <= 10 deg | Verified against the pseudocode. |
| Skill index and difficulty adjust (`combat-moves.md`) | Verified (section 2.3). The **configured** index lives at `fight+0x1A0`; `Fight__EngageTarget`/`sub_4AB865` copies it to `fight+0x19C` (the one the skill table and `Fight__Update` read). The Prince and non-AI fighters get 10 (Proxy). |
| **Error:** swing timer "`ai+0x80 = now + 2.0`" and "set to -2^64 after a hit, so it can hit again" | Wrong. `StEnemyPatrolAttack` sets `ai+0x80 = clock + 0.3` (0x4BA831, `+0.30000001`). `StSwingWhileMoving` 0x4BAA34 hits when squared distance < 4, `ai+0x80 < clock`, |dy| < 1 and the target is in front (dot > 0, and cos > 0.7 beyond 1 m squared); the hit sets `ai+0x80 = FLT_MAX` (3.4e38 as a double), so a swing hits **once**. The +2.0 s timer is only set when the hit kills the Prince (`StWatchDyingPrince`). After the swing's activity-end bit (`actor+0xDB` bit 6) it sets `actor+0x180 = 0.1` and goes to `StSwingRecover` (`loc_4BAC5C`), then `StBeginCharge` again. |
| **Stale:** the "What the model does differently" table in `guard-awareness.md` | Most of it is now implemented in `enemy_update` (cone, 3 m, hearing radii, `|dy| <= 2`). What is still different is in section 7. |

## 2. EnemyStart: argv and the objects it builds

`EnemyStart` creates the actor-side AI in two calls (`sub_4A8FB9` 0x4A8FB9): `sub_4A92D7`
(character: skill, weapon, health, Fight object) then `sub_4A89F9` (AI object: waypoints, start
skill, prize). Traps.txt numbers its arguments from 1; the `.k2a` files and the code count from 0.

| argv | Traps.txt name | Parsed by | Stored at | Notes |
|---|---|---|---|---|
| 0 | Body | `sub_4A92D7` 0x4A92D7 | actor name | `sub_40FCC7(actor, trap+0x100 name)` |
| 1 | DifSpecs `skill,weapon,bottles` | 0x4A92D7 | `fight+0x1A0`, weapon, `actor+0x130/0x174` | section 2.3 |
| 2 | Waypoints (comma list of WayPoint child names) | 0x4A89F9 | `ai+0xA8` count, `ai+0xAC` index array | section 2.2 |
| 3 | InhibitDeath (t/f) | 0x4A89F9 `sub_5170CE(..,3)` -> bool | `ai+0x118` (280) | `StStartDyingMode` 0x4C6391 skips the generic death clip when it is set (`ai+0x118` and `actor+0x128`); the `DeathMsg` skill must then play the death |
| 4 | DeathMsg | 0x4A89F9 `sub_5170CE(..,4)` | `ai+0x11C` (284) | run when the guard dies |
| 5 | EntrySkill | 0x4A89F9 -> `StartPoint` ctor 0x4A65F5 | `ai+0xA0` (160) | a leading `*` is dropped from the name; `*` also sets `ai+0x70/0x71 = 1` and calls actor vtable slot +0x64 with 1 (a hold/suspend call). *Unclear:* whether `*` means "wait for start" or "loop as idle"; `main.cpp:5518` reads it as a loop. The `start` string is set as the StartPoint's label (0x4A6594, "start"). |
| 6 | Pursuit Waypoint-target | 0x4A89F9 | `ai+0xBC` (188) | name of a WayPoint child; **empty in every one of the 105 shipped EnemyStarts** (checked all `data/**/*.k2a`) |

Data census over `data/**/*.k2a` (105 EnemyStarts): argv1 first number 0..8 only (3 x21, 4 x18,
6 x15, 1 x11, 2 x8, 7 x8, 5 x6, 8 x6, 0 x2; 11 also give a weapon/health field); argv2 present on 24
(route lengths 1..6); argv3 `true` on 3 (two archers and `TKPart3a`'s TowerGuardB).

### 2.1 AI mode (`ai+0x114`)

`sub_4A89F9` sets mode 2 (stand guard), 4 for archers (`sub_4C1AEB(character)`), and promotes 2 to 1
(patrol) when the route list is non-empty (`ai+0xA8 > 0`). `StAssignAIMode` (0x4AA3F4) and the
reset `sub_4A91C9` repeat the same rule. Modes 0 and 3 are as in `guard-awareness.md`. A missing
perimeter (`sub_4A8387(...) < 1`) logs "Enemy %s has no perimeter described!" and carries on.

### 2.2 Waypoints

The trap's **child traps** are read. Children named `WayPoint` (case-insensitive) become `WayPoint`
objects (0x2C bytes, ctor 0x4A665A, position from the child's pivot, name = child's trap name in
`+0x14`, `+0x18` = its own argv0 as a string, `+0x10` an animation hook). Any other child is the
"prize" trap the guard drops (at most one; second one logs "Can't have two prizes", unknown type
"Illegal prize trap"). Waypoint array layout: **slot 0 is the `StartPoint`** (the guard's own spawn
and EntrySkill), slots 1..n are the WayPoints in child order.

`argv2` is split on commas; each name is looked up (case-insensitive) in that array and the slot
index stored, so a route is a list of slot indices and may repeat slots and use slot 0. A name not
found logs "Failed to find waypoint %d (%s) on trap %s" and **cancels the whole route** (count = 0,
the guard stands guard). So the spawn can be part of the loop by naming the guard's own start point
(its trap name).

Patrol behaviour (`StEnemyPatrolInit` 0x4B9DC8, `StEnemyPatrolToSpot` 0x4B9FDE):

1. `ai+0xB8` (184) is the current route index. Init sets alert phase `ai+0x78 = 1` if it was -1.
2. If the guard's `+0xDC & 3` (fight bits) is set: `StBeginCharge` at once. Mode 2: `StFindGuardSpot`
   (0x4BA22E). If the alert phase is 2 (a sound was heard): `StInvestigates` (0x4BD704), which walks
   to `ai+0xCC`. Otherwise: walk to the target of route[idx] with `Walk_L` at speed -0.5
   (`sub_49F046`), state `StEnemyPatrolToSpot`.
3. When the walk activity ends (`actor+0xD8` bit 2): idx++ and wrap at `ai+0xA8`; "patrol at target"
   and "heading to point" are debug prints. **There is no pause and no animation at a waypoint**:
   the waypoint animation hook (`+0x10`) only reaches a stubbed print ("would play anim %s").
4. Each decision point (`actor+0xDB` bit 7) calls `Guard__CheckForPrince` (0x4BDFD8, in
   `guard-awareness.md`).
5. Injured by a non-sword source (`actor+0xD8` bit 24): `StInjuredOnPatrol` (0x4BDA2F) plays
   `CombatBeingInjured` (clip 57), then `StBeginCharge` if fight bits are set, else `StResumePatrol`.
   Health < 1: `StDieOnPatrol` (0x4BD8A9) -> `StStartDyingMode`.
6. `StResumePatrol` (0x4BDAF1 -> `sub_4BE642`): take the pursuit index `ai+0xC0` if >= 0 (else the
   current idx), and if that waypoint is visible from the guard (`sub_4FE7B6`) go there; otherwise
   scan the array **from the last slot down**, taking the first slot that is slot 0 or visible and
   reachable (`sub_4A849E` sets the target `ai+0xCC`). So after losing the Prince a guard returns to
   the **last visible waypoint in the array**, not the one he left, and not necessarily the next.
7. Spotting the Prince with `ai+0xBC >= 0`: `StFacePursuitWaypoint` (0x4BD56A) then
   `StHeadToPursuitWaypoint` (0x4BD590): walks to `array[ai+0xBC]` at -1.5 speed units (clip
   `Running_L`) and re-enters `StEnemyPatrolToSpot` with idx = -1. Unused in shipped data.

`StFindGuardSpot` / `StGuardFixedSpot` / `StWaitAndWatch` (mode 2) are in `guard-awareness.md`; the
ambient idle clip (`StGuardAmbient` 0x4BDDA4) is a random `CombatAmbient`-class clip. *Not traced:*
its period.

### 2.3 Skill, weapon, health, difficulty

`sub_4A92D7` (the EnemyStart path; `sub_4A8913` is the same for actors created without a trap,
with the difficulty adjust applied after the table lookup instead of before):

```
skill = atoi(argv1[0]);  if (skill < 0 || skill > 10) skill = 4;           // empty: atoi fails -> 4
difficulty g = dword_7EAC3C   (0 easy, 1 normal [default], 2 hard)
easy:   skill = max(skill - (skill > 6) - 1, 0)
hard:   skill = min(skill - (skill > 6) + 2, 9)
weapon  = argv1[1]: "Sword"=0 "Staff"=1 "Double"=2 "Bow"=3, or a number; empty = -1 (default for the character)
bottles = word_7C5820[skill]   // { 2,3,3,4,5,5,6,7,8,10,10 }, indexed by the ADJUSTED skill
          or argv1[2] (atoi; a bad value logs "Illegal health" and uses 1)
easy:   bottles = max(bottles - 1, 2)      hard: bottles = min(bottles + 2, 10)
health  = sub_422A5F(3 * bottles)          // thirds of a bottle x4 = 12 HP per bottle
fight+0x1A0 = skill                        // 0-based; "Skill Level skill+1" keys
```

On Normal this is what `combat_init` does (`main.cpp:5660-5683`). On Easy/Hard the bottle count is
adjusted **twice** (once through the skill index, once by +-1/+2), and an explicit argv bottle
count is adjusted too. The table is the same as `combat_tuning::SKILL_BOTTLES`.

The skill index is also the archer type (section 5) and the "gets up from Dying" rule (skill index
6..9, `Fight__Update` case 12 at 0x4AD7CB: `v62 >= 6 && v62 <= 9`).

## 3. The guard state machine

Names are the string table entries (`aSt...`); a state function runs once to set its sub-state
and pointers (`sub_44EEB8(slot, fn, name)`), then per-frame via the CharacterStateTable. `(PA)` =
see `guard-awareness.md`.

```
 spawn --> StEnemyStanding/StStartEnemyStanding (mode 0) --> StEnemyActive (PA)
       \-> StEnemyPatrolInit 0x4B9DC8 (modes 1,2,4->archer)
              |-- mode 2 --> StFindGuardSpot 0x4BA22E --> StGoToGuardSpot 0x4BA459 --> StGuardFixedSpot 0x4BA666
              |                                              \--> StWaitAndWatch 0x4BA6B0 (idle, sees/hears)
              |-- mode 1 --> StEnemyPatrolToSpot 0x4B9FDE  (route loop, section 2.2)
              |-- phase 2 --> StInvestigates 0x4BD704 --> StEnemyPatrolToSpot
              \-- fight bits set --> StBeginCharge
 notice (CheckForPrince 0x4BDFD8 / StWaitAndWatch)
     sees          --> voice line, store pos ai+0xCC, [pursuit waypoint?] --> StPrepareToCharge 0x4BADE7
     heard+seen360 --> Manhattan < 8 : as "sees";  >= 8 : StStopToListen 0x4BACA5 --> StInvestigateSound / stay
     charm lights  --> StDistractedByCharm 0x4BD901 --> StWatchCharmingLights
 StPrepareToCharge: set fight bits, draw sword (taunt) --> StBeginCharge 0x4BAE8F
 StBeginCharge: Manhattan < 5 --> StEnemyPatrolAttack 0x4BA831
                else run (Running_L) --> StChargePrince 0x4BB467 / StTryToReachPrince 0x4BBB97 / StMoveTowardPrince
                no path --> StCantReachPrince 0x4BB878 (taunts)    bow drawn by Prince --> StFindHidingPlace 0x4BCFB6
 StEnemyPatrolAttack 0x4BA831:
     Prince sword sheathed AND not engaged --> StSwingWhileMoving 0x4BAA34 (one swing, 15) --> StSwingRecover --> StBeginCharge
     else (sword out / engaged)           --> side slot StSetupForFightMode 0x4C604D (taunt)
                                              + main StEnemyInContact 0x4AA95A  (duel, Fight__Update)
 StEnemyInContact: D > 13 or |dy| > 1.5 --> StEnemyActive --> StEnemyWatchful 0x4AA83C --> StEnemyApproach --> StBeginCharge
 give up (StTryToReachPrince, Manhattan >= 5 and no 360 sight): sheathe --> StResumePatrol 0x4BDAF1 --> StEnemyPatrolToSpot
 Prince health < 1: StFightMgrMode 0x4C6183 --> StVictoryMode 0x4C6746 --> StSheathSword --> StWatchDyingPrince 0x4BB78E
 guard health < 1:  StDieOnPatrol 0x4BD8A9 / Fight state 11 --> StStartDyingMode 0x4C6391 (DeathMsg, death clip)
```

Fight sub-machine inside `StEnemyInContact`/`StFightMgrMode`: `Fight__Update` 0x4AC845 (states
AtReady, Attacking, UnblockedAttack, AttackRecover, Blocking, HitBeingBlocked, StartBlock,
BlockingAHit, BeingInjured, SuperCounter, Feint, Dead, Dying, Finishing, BeingFinished, Clash) is
described in `combat-moves.md` and is the same code for the Prince and for guards.

## 4. The fight AI, quantitatively

Everything below runs in `Fight__Update` for a fighter with `fight+0x198 == 1` (AI). Verified
against the pseudocode (`pop3d.c` 256228-256933) unless marked.

### 4.1 Starting an attack (AtReady, case 0)

Conditions (0x4AC845, `case 0`): the target is in `fight+0x120` and its `+0x120/+0x40` agree, not
Dying/BeingFinished, and `No Threat Distance (0.4) + fight+0x1F4 (longest range L) > fight+0x124
(distance D)`. Then, if `now >= fight+0x168` (the next-attack time):

1. Direction: `rand(weights)` over the three direction weights `fight+0xDC..0xE4` (each
   `100 - Double Attack Percentage`) plus `fight+0xE8` (`3 x Double Attack Percentage`), **but the
   double weight is subtracted from the range unless the weapon is blades** (`fight+0x200 == 3`). For
   a sword guard the three directions are equal, one third each. `Double Attack Percentage` has no
   effect on sword or staff fighters.
2. Feint or attack: attack if the last AI action was a feint (`fight+0x130 == 3`) or
   `rand(fight+0xEC = 300) >= fight+0xF0 (100)`; else feint (state 10, `fight+0x130 = 3`).
   Feint probability 1/3, never twice in a row. Attack sets `fight+0x130 = 1`.
3. A target closer than `reach[dir] - 1.0` (`fight+0x1BC+4*dir`) is attacked at once without the
   schedule (`0x4ACC..`); an unarmed AI (weapon 5) uses centre attacks only.
4. After the attack/feint starts, `sub_4AE7FD` (`Fight__ScheduleNextAIAttack`) sets the next time:

```
if Aggression (fight+0xD8) == 0:  next = FLT_MAX
else:
  if (budget >= 0x8000) { budget = 0; base = now; next0 = now }
  budget += (int)(Aggression * 100);
  next = rand16(budget) * 0.01 + base;          // sub_414CCF = uniform [0, budget)
  if (next < now) { budget = max(0, (base - now) * 100 + budget); base = now; }
```

   So the gap between attacks is not "Aggression +- 25%": it is a self-correcting random process
   with a long-run mean of one attack per Aggression seconds and a spread from nearly zero to a
   few times Aggression. With skill 1 (3.5 s) and skill 10 (0.667 s) two swings can come almost
   back to back at any skill.

   *Interpretation note:* `budget` and `base` are `fight+0x180` and `+0x178`; the loop is read from
   the pseudocode, not run.

### 4.2 Answering the Prince's attack (`Fight__PickAIResponse` 0x4AC272)

Runs on the Prince's fight object when he starts an attack or feint, if the defender is AI, in
range (`No Threat Distance` + reach) and inside the attack's angle window. Verified, with these
specifics:

* Buckets (`fight+0xC0..0xD0`): Late Attack, Wrong Block, Did Nothing, Block, Sneak Attack, total
  `fight+0xD4`. The roll is `rand(total) + 1` against the five in that order.
* Unless the fight is Proxy (skill 10): `m = 1` ; if the attacker left recovery less than `Max Fast
  Attack Time` (1.5 s) ago, `m = 1 - (1 - elapsed/1.5) * 0.25` (0.75 at once, 1.0 after 1.5 s);
  if the attacker repeats the defender's recorded last direction, `fight+0x15C *= 0.4` (it
  accumulates, and resets to 1 when the direction changes) and `m *= fight+0x15C`.
* The first three buckets (Late, Wrong, Did Nothing) are multiplied by `m` (truncated, never above
  the remaining total); **the Block bucket takes whatever is left**. Sneak Attack is untouched.
* Result 3 (Block): block delay = `AI Min Block Delay + rand * (Max - Min)` (0.15..0.433 s) only if
  `m >= 1`; with any penalty applied the block is **instant** (delay 0). A super attack turns Block
  into response 4.
* Response 2 (Did Nothing) does nothing. Response 1 blocks the wrong side `(dir + 1) % 3`.

Acting on the choice, in AtReady (0x4AC962, `combat-moves.md`): Late Attack waits until its own
swing would be late and clashes if within 1/30 s; Block raises `Early Block` (0.133 s) before the
block point and sets state 4; the AI holds the block until the hit, then state 7
`BlockingAHit` with the clip `CombatAIEndBlock<Dir>` and duration **`Computer Recover From Block`
(0.3667 s)** (the player gets `Recover From Block` 0.0667; `Fight__SetState` case 7 picks
`dword_7C8598` when `fight+0x198` is set).

### 4.3 Hit resolution and damage

At the block point (`Begin` after the attack started): hit if the target is in the reach of that
attack type (`fight+0x1BC + 4*type`), inside the attack's Min/Max angle (sword: left `[-1.571,
0.4]`, centre `[-0.4, 0.4]`, right `[-0.4, 1.571]` rad, measured from the attacker's facing),
and not blocking the same direction while facing within +-0.4 rad. Unblocked: after `Unblocked
Attack` (0.0667 s) the target takes the damage.

`Fight__TakeDamage` 0x4ADBA0 (`dmg`, attacker fight): `dmg += attacker.actor+0x1C8 +0x48` when > 0
(a strength bonus, `potion strength` in our code); if the target is not Dying (12) / -1 and
`health - dmg <= 1`, `dmg = health - 1` (when `dword_7EAC40 == 0`, the default, and neither side is
unarmed; otherwise `dmg = health`), so a normal blow never kills; then `Prince__TakeDamage`
(0x4C760C -> 0x4C73D6) subtracts it (clamped at 0) and sets the hurt bit. Health below 1 ->
state 11 (Dead) and `Fight__Disengage`. `dword_7EAC40` is the second saved option next to the
difficulty (default 0); its UI is not traced.

Damage keys: `Sword Left/Center/Right Attack Damage` 14/18/10, `Blades` 13/15/13, `Staff` 8/10/12,
counters 8 (sword) 9 (staff), follows 7, spins 14, `Super Attack Damage` 18 (9 partial), `Super
Counter Damage` 6, unarmed 5 (hard-coded), the sheathed-Prince swing 15 (`enemy.dat`).

### 4.4 The rest

Counter, follow, spin, clash, super attack, finishing move and Dying are as in `combat-moves.md`;
checked here: `Fight__AITryCounter` 0x4AD975 (`rand(Opportunities) < c` after a block of an L/R
attack, c shrunk by `fight+0x160`), the Dying exit (skill index 6..9 returns to AtReady with 1 HP,
else Dead), and the finishing move trigger. The `fight.ini` key `Counter Attack` (0.3667) and
`Dying` / `Death Animation` / `Fall Dead Animation` are never read.

## 5. Archers (mode 4)

`StAIArcherInit` (0x4BECEB) sets the fight weapon to 4 (bow), looks up the shooter row
`row = enemy.dat[Archery]` table `[skill_index]` (24-byte rows at `0x7FF050`: name, range,
accuracy, rate of fire, arrow-type bits, health; **the EnemyStart skill digit is the row index**,
clamped to row 0 with a log if >= 5), and takes the first arrow type flag (`Normal` 0x10, `Blue`
0x20, `Magic` 0x400, `Death` 0x800). The loader stores accuracy as `acc * 0.001 + 0.9`.
`StAIArcherWait` wakes on `CanSeePrince(cos 30 deg)` or a hit (`actor+0xDB & 1`), draws the bow
(`StDrawBow` 0x4BF028), and `StArcherStanding` (0x4BF2A5) fires while the Prince is within the row's
range (squared horizontal distance < range^2), turning first if the facing dot is < 0.8; it
does nothing when he is within 3 m and |dy| < 2.5 (it lets the Prince be "too close"); every 7 s
of idle it plays `BowDrawAmbient`. Arrow constants (`enemy.dat` [Archery]): speed 60, normal damage
12, blue 30, swarm 24, magic 30, stealer 15, possession 40 s, death-black 10 s. *Not traced:* the
accuracy roll and the ROF timer in the shooting states (0x4BFA50..0x4C00B3), and whether the
bow-drawn Prince check in the other states uses the row.

## 6. Tuning keys read

| File | Key / line | Read by | Meaning |
|---|---|---|---|
| `enemy.dat` | line 1..9 (see `guard-awareness.md`) | 0x4A6C82 | by order; names ignored. `distFarFromAnywhere` unused. |
| `enemy.dat` | `[Archery]` block | 0x4A6C82 | arrow constants, shooter rows |
| `fight.ini` | 299 keys | 0x4AE10D | `combat-moves.md`; AI keys: `Skill Level 1..10 <bucket / Aggression / Counterattacks / Spin / Follow / Clashes>` + `Proxy` |
| `fight.ini` | `Max Fast Attack Time/Penalty`, `Same Direction Attack Penalty` | 0x4AC272 | response penalties (section 4.2) |
| `fight.ini` | `AI Min/Max Block Delay`, `Early Block`, `Computer Recover From Block`, `No Threat Distance` | `Fight__Update` | block timing |
| code | `word_7C5820` | 0x4A92D7 | bottles per skill |
| code | `dword_7EAC3C` / `dword_7EAC40` | 0x4A92D7, 0x4ADBA0 | difficulty (0/1/2), second option |

`parse_enemy_dat` (`src/engine/fight.cpp:49`) names each value by the first word after it:
`distFarFromEnemy`, `distApproachEnemy`, `distCamoflauge`, `swing`, `peripheral`. The `0.15` and
`1.1` lines are named `distFightWithEnemy`/`distTooCloseToEnemy` after the `=`.

## 7. Differences from our code

Ordered by what a player notices. `main.cpp` lines are as of this commit.

1. **A sheathed Prince is dueled, not swung at** (`main.cpp:6123-6146`, `CHARGE` -> `FIGHT` with no
   check of `cs.prince_drawn`). Binary: at Manhattan < 4/5 the guard runs `StEnemyPatrolAttack`; if
   the Prince's sword is not out (and he is not "engaged") the guard does **one** running swing
   (clip `CombatAttackRight`, hit after 0.3 s if within 2 m, |dy| < 1, in front and within about 45
   deg) for **15** damage (`enemy.dat` "swing"), recovers 0.1 s, charges again; no block, no
   counter, no dying cap. Suggested change: add a `SWING` state. In `CHARGE`, when the contact test
   passes and `!cs.prince_drawn`, play the clip, set `hit.at = now + 0.3`, resolve once with
   `fp.enemy("swing", 15)` straight through `prince_damage` (not `prince_fight_damage`), then
   `busy_until = clip end + 0.1`. Drawing the sword mid-fight (the binary's "engaged" bit) should
   switch the next contact to the duel.
2. **A guard who blocks recovers in 0.0667 s, not 0.3667 s** (`main.cpp:6263`, `fp.fight("Recover
   From Block", 0.0667)`). The AI uses `Computer Recover From Block` (0.3667, state 7 at 0x4AB3C3
   case 7 with `fight+0x198` set). Change the key. Today the guard is free to swing 0.3 s before
   the Prince has recovered from the block (`6264`), which makes blocks a free counter.
3. **No penalty for repeating or rushing** (`enemy_react`, `main.cpp:6300-6326`). Add per guard:
   `last_dir`, `same_dir_mul` (`*= 0.4` when the Prince's direction equals `last_dir`, else 1),
   `last_ready_at`. Compute `m` as in section 4.2, scale Late/Wrong/Did Nothing by `m`, give Block the
   remainder, make the block delay 0 when `m < 1`. Add the Sneak Attack bucket (skill 6+: 9..12%);
   today the four-bucket roll renormalises the sum without it, so a skill 8 guard blocks 36% not 32%
   and Sneak is never rolled. Effect for the player: spamming one direction or attacking again
   within 1.5 s is punished in the original; in ours it is not.
4. **The guard stands still during the duel** (`main.cpp:6147-6150`). The binary keeps `D` at
   `S - 0.15` by stepping forward and `D >= L - 1.1` by stepping back (`StEnemyInContact`), per
   weapon (`S`, `L` from `master.dat`), follows him out to `D > 13` (not `1.75 x 1.6`), and runs
   (`Running_L`) when `D > 3.5`. Ours has one `FIGHT_DIST` (1.75) and one `REACH` (2.2); the
   Prince can step back to 2.2..2.8 m, where a guard swing no longer connects, and the guard does
   not follow. Suggested: read `S` and `L` from the weapon (`fight_params`/master weapon ranges),
   step forward at walk speed while `D > S - 0.15` and the heading error <= 10 deg, step back at
   `D < L - 1.1`, drop to CHARGE only past 13 m or `|dy| > 1.5`.
5. **Attack cadence is the wrong distribution, and Double Attack misapplied**
   (`main.cpp:6190-6211`). Replace `aggr * (0.75 + 0.5 * frand())` with the budget scheme of section
   4.1. Remove the `Double Attack Percentage` "go again at once" roll (`6210`): in the binary that
   key only weights the blades' super attack. Direction is uniform for swords (`6190` is right).
6. **Sight has a 25 m cap, camo is state-dependent, hearing is not what the binary does**
   (`main.cpp:6087`, `6070`, `6094-6097`). The binary has no range limit (the ray only); camo
   (> 1.5 m, 3D) applies whenever `CanSeePrince` runs, including during a chase, and a "possessed"
   Prince is never seen. Noise: the 6 m for a drawn sword is applied once by `StDrawSwordIdle` and
   stays until the next Prince state start overwrites it (standing/crouching reset it to 0);
   ours uses 6 m for as long as the sword is out. A heard-and-seen Prince at Manhattan >= 8 makes
   the guard turn and `StInvestigateSound` for 0.2 s, not charge. Suggested: drop `d < 25`,
   gate `camo` on the clip only, make `cs.prince_drawn` noise last until the next locomotion
   clip change, add an `INVESTIGATE` state for Manhattan >= 8.
7. **Give-up and return** (`main.cpp:6126`, `PATROL` at 6102). Binary: the only give-up is
   Manhattan >= 5 and no 360 deg sight, tested at path decision points, then sheathe and
   `StResumePatrol` to the **last visible waypoint scanning from the end** (and slot 0 as the
   fallback). Ours also gives up at `d > 25` and resumes at the stored `e.wp`. Suggested: keep the
   Manhattan rule, drop `d > 25`, on give-up choose the highest waypoint index (including a slot 0 =
   home) with a clear line, as in section 2.2 point 6. Also: the whole route is cancelled when one
   name does not resolve; `combat_init` (`5686-5698`) silently skips unknown names.
8. **Hit geometry is a single cone and a single reach** (`main.cpp:6020-6022`, `6227`, `6308`).
   Binary: reach per attack type (1.7..3.05 by weapon) and the L/C/R angle windows above. A
   guard's left/centre/right swings should test `[-1.571, 0.4]`, `[-0.4, 0.4]` and `[-0.4, 1.571]`
   rad of the signed angle to the target, not `|angle| < 1.2`. The Prince's block also needs him to
   face the attacker within 0.4 rad (`6025` tests direction only).
9. **Difficulty is not modelled** (`main.cpp:5660-5683`, skill, bottles). Normal matches. When a
   difficulty setting exists, apply the two adjustments of section 2.3 (note the double adjust of
   bottles) and the second option `dword_7EAC40`.
10. **InhibitDeath (argv3) is ignored** (`main.cpp:5655-5702`, `enemy_damage` `5849-5857`).
    When true, do not play `CombatDeathGeneric` / `CombatDying`; the `DeathMsg` skill (argv4) plays
    the death (the three shipped cases are the two cliff/tower archers and TowerGuardB). Today both
    clips run.
11. **Archers** (`archer_update`, `main.cpp:5902-5953`): ours has a fixed 40 m range, 3 s wait, and a
    skill-based miss. The binary reads the shooter row for the skill digit (range 35/40/50/50/99,
    accuracy 0.95/0.96/0.975/0.99/0.999, rate 8/7/6/10/5), wakes on the 30 deg sight cone (not a
    ray from anywhere), turns when his facing dot is < 0.8, ignores a Prince within 3 m, and uses the
    arrow type of the row. Suggested: load `[Archery]` in `fight_params` and drive range, accuracy
    and the wait from the row.
12. **Minor.** Ours taunts once per guard (`taunted`), the binary on every entry to fight mode
    (`StSetupForFightMode`); the "sees" voice line is `character+0x90` per guard, not `PG1dlg`;
    the 3 m wake radius for held guards (`main.cpp:5989`) is ours; the woozy idle at <= 1 bottle is
    right; the ambient guard idle (`StGuardAmbient`) is not played in `PATROL` for mode-2 guards that
    have no start skill; EntrySkill `*` semantics (section 2, argv5) need a runtime check.

## 8. Not traced

* The "engaged" bit `actor+0xDA` bit 1 on the **Prince** (decides fight vs swing in
  `StEnemyPatrolAttack`): who sets it for him. Treat "sword out" as the only trigger until traced.
* Period of the guard ambient idle, the mode 3 (animatronic) states, `StTryToReachPrince`'s
  path planner (`sub_4A7C21`, `sub_4A8546`) and walk speed values (`sub_49F046` argument -0.5 and
  `sub_49F3AF`'s run parameters).
* `ai+0x70/0x71/0x6A` ("held until start") and what the actor vtable slot +0x64 does.
* The archer shooting states' accuracy roll and rate-of-fire timer.
* Who writes `fight+0xA8/0xB0/0xB4/0xB8` (see `combat-moves.md`), and the UI of `dword_7EAC40`.
