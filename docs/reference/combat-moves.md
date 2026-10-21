# Sword-combat moves

How the original implements feints, counters, super counters, clashes, spins, follow attacks,
the blades super attack, finishing moves, dodges, taunts and the woozy idle, for the Prince and
for guards. Everything here is read from the disassembly of `data/pop3dreal.exe`; guesses are
marked. The first-pass port notes are in `combat.md`.

All fight logic lives in one class with no RTTI name, embedded in every fighter at
`actor+0x1C4` (constructor `Fight__ctor` `0x4AB2E9`, size 0x208). The Prince and guards run the
same state machine (`Fight__Update` `0x4AC845`); `fight+0x198` selects the AI branches.

## fight.ini loading

`FightParams__LoadIni` (`0x4AE10D`) reads 299 keys from `[Fight Params]`. The key names are at
`FightIni_KeyNames` (`0x749E0C`), the destination pointers at `FightIni_KeyPtrs` (`0x74A2B8`).
Keys 0–124 are floats (`atof`), 125–298 ints (`atoi`). Every global is renamed in IDA to
`g_fight_<Key>` (for example `g_fight_CounterAttackWindow` `0x7C859C`).

**`Counter Attack` (0.3667) is the only key in `data/fight.ini` the binary never reads.** Loaded
but never referenced by code: `Death Animation`, `Fall Dead Animation`, `Dying`, `Attack Advance`.

`FightParams__Init` (`0x4AE08A`) then:

- copies the Super attack entry into two extra entries (types 11 and 12, `0x7C87B8`, `0x7C87CC`)
  with damage replaced by `Super Attack Partial Damage`;
- per skill level, sets `+0x1C..+0x24` = 100 − `Double Attack Percentage`, `+0x28` = 3 ×
  `Double Attack Percentage` (done in the loader at `0x4AE2D9`), and `+0x14` = the sum of the
  five response buckets.

### Attack table (`0x7C8610`, 0x14 bytes per entry)

| `+0` | `+4` | `+8` | `+0xC` | `+0x10` |
|---|---|---|---|---|
| `<X> Attack Begin` | `<X> Damage` (int) | `Blocked <X> Recover` | `<X> Min Angle` | `<X> Max Angle` |

`Fight__AttackParams` (`0x4AB7F0`) picks the entry from the attack type `fight+0x18` and weapon:

| type | move | entry | attack clip (`fight+0x18` + 0x21) |
|---|---|---|---|
| 0 / 1 / 2 | left / center / right | `Sword`/`Staff`/`Blades` `<Dir>` by weapon | `CombatAttackLeft/Center/Right` |
| 3 | super attack (blades) | `Super Attack` | `CombatSpecialHit_G4` |
| 4 / 5 | left / right spin (staff) | `Left/Right Spin` | `CombatChainLeft/Right_G3` |
| 6 / 7 / 8 | follow to left / center / right | `Left/Center/Right Follow` | `CombatFollowRightToLeft`, `…LeftToCenter`, `…CenterToRight` |
| 9 / 10 | counter left / right | **always `<Weapon> Left Counter`** (see below) | `CombatCounterLeft/Right` |
| 11 / 12 | partial super hit | Super entry, partial damage | `CombatSpecialHitLeft/Right_G4` |

Quirk: types 9 and 10 compute the index as `(type − 9) / 2`, which is 0 for both, so a right
counter uses the **left** counter's begin, damage, angles and blocked-recover. The `… Right
Counter …` keys are loaded but only reach the code through this table.

Weapon `fight+0x200`: 0 guard sword (`_G1`), 1 Prince sword (`_G2`), 2 staff (`_G3`), 3 blades
(`_G4`), 5 unarmed (`_G6`). `Fight__WeaponIndex` maps 0/1 → 0 (Sword keys), 2 → 1 (Staff), 3 → 2
(Blades). Unarmed uses a hardcoded entry at `0x7C85F8`: begin 0.8667, damage 5. The clip suffix
digit is replaced by weapon + 1 (`Fight__ResolveClipName` `0x4AE964`).

### Skill table (`0x7C8828` + level × 0x58)

Copied to `fight+0xC0` when the fight starts (`Fight__EngageTarget` `0x4AB953`). The index is
`fight+0x19C`: EnemyStart's skill digit (default 4) adjusted by `g_GameDifficulty` (easy −1, or −2
above 6; hard +2, or +1 above 6, capped at 9). Index 0 is `Skill Level 1`; 10 is `Proxy`.

| skill `+` | fight `+` | key |
|---|---|---|
| 0x00–0x10 | 0xC0–0xD0 | Late Attacks, Wrong Blocks, Did Nothings, Blocks, Sneak Attacks |
| 0x14 | 0xD4 | sum of the five |
| 0x18 | 0xD8 | Aggression (float, mean seconds between attacks) |
| 0x1C–0x28 | 0xDC–0xE8 | direction weights L, C, R, double (from Double Attack Percentage) |
| 0x2C | 0xEC | weight total (static 300) |
| 0x30 | 0xF0 | feint weight (static 100, no key) |
| 0x34 / 0x38 | 0xF4 / 0xF8 | Counterattacks / Counterattack Opportunities |
| 0x3C / 0x40 | 0xFC / 0x100 | Spin Attacks / Spin Attack Opportunities |
| 0x44 / 0x48 | 0x104 / 0x108 | Follow Attacks / Follow Attack Opportunities |
| 0x4C / 0x50 | 0x10C / 0x110 | Sword Clashes / Sword Clash Opportunities |

Every "X out of Y" roll is `rand(Y) < X` (`Fight__Rand` `0x4ADFF7`, `rand(0)` = 0). Unarmed AI
(weapon 5) ignores the table: centre attacks only, Did Nothing only, aggression 5.0.

The **Prince's** copy is zeroed instead, and his own actions increment it: attacks per
direction, feints (`+0xF0`), counters, spins, follows, clashes and how he answered each AI
attack. Every 3 s (`AI Update Period`) `Fight__UpdateProxyStats` (`0x4ADD4A`) sends this record
as the `Proxy` skill (network messages 0x4C8/0x50D). Guess: this was a multiplayer feature that
let AI opponents imitate a player.

## Fight states (`fight+0x20`)

Names at `Fight_StateNames` (`0x7C8BF4`, indexed state + 1). `Fight__SetState` (`0x4AB3C3`) picks
the clip and duration.

| # | state | clip | duration |
|---|---|---|---|
| 0 | AtReady | none | — |
| 1 | Attacking | attack clip by type | until the block point (`Begin`) |
| 2 | UnblockedAttack | types 11/12 only: `CombatSpecialHitLeft/Right` | `Begin` + `Unblocked Attack` from attack start |
| 3 | AttackRecover | none | + `<Weapon> Recover From Injuring` (`Super Attack Recover…` for dir 3) |
| 4 | Blocking | `CombatBlockLeft/Center/Right` | held |
| 5 | HitBeingBlocked | `CombatBlocked<Dir>` (clip 59 + type) | see Counter |
| 6 | StartBlock | none | AI: until `Early Block`; player: one frame |
| 7 | BlockingAHit (block end) | player `CombatEndBlock<Dir>`, AI `CombatAIEndBlock<Dir>` | `Recover From Block` / `Computer Recover From Block` |
| 8 | BeingInjured | `CombatBeingInjured<Dir>_L` + sound `gblfightstabs` (`gblfightovhit` for centre) | `<Weapon> Recover From Being Injured` |
| 9 | SuperCounter | `CombatCounterLeft/Right_G4` | `Blades Recover From Injuring` + `Super Counter Attack` |
| 10 | Feint | `CombatFeint<Dir>`, `CombatFeintSpecial_G4` for dir 3 | `<Weapon> <Dir> Feint` / `Super Feint` |
| 11 | Dead | — | — |
| 12 | Dying | `CombatDying` | until the clip ends |
| 16 | Finishing | `CombatFinishingMove<N>` | until the clip ends |
| 17 | BeingFinished | `CombatFinishingReceive<N>` | then Dead |
| 18 | Clash | `CombatClash` | until the clip ends |

13–15 (SideStepping, MovingFwd, MovingRev) exist only as names. The clip table is
`Fight_ClipNames` (`0x749C5C`, 97 entries).

## Player input

`Input__RemapControlMsg` (`0x4538BE`) turns controls into commands (control index + 0x4DC).
`Prince__HandleCommand` (`0x4C80FC`) stores fight commands at `fight+0`, `fight+4` (pressed);
`StFightingStanding` / `StFightingMoving` pass them to `Fight__HandleInput` (`0x4ADA4C`), which
calls `Fight__Command` (`0x4ABE1C`) with an action.

| input | command | action |
|---|---|---|
| Attack Left / Center / Right | 0x4E9 / 0x4EA / 0x4EB | 0 / 1 / 2 |
| Walk held + Attack L/C/R | +0x19 → 0x502–0x504 | 5–7 (feint) |
| Block press | 0x4EC | 4 |
| Block release while Blocking/StartBlock | — | state 7 |
| Attack Left + Attack Right within 400 ms | 0x507/0x508 | 3 (blades only; else stays a single L/R attack) |
| the same with Walk | 0x505/0x506 | 8 (blades only; else 5/7) |
| turn left/right double-tap within 400 ms | 0x4D2 + 2 | dodge |
| forward/back double-tap within 400 ms | 0x4D3 + 2 | quick move |

The 400 ms windows are `Input__DetectLeftRightCombo` (`0x412F77`) and `Input__DetectDoubleTap`
(`0x412EA1`); both compare event timestamps against 0x190.

A press that cannot start now is queued (`fight+0x12C` kind, `+0x138` direction) and run by
`Fight__RunQueuedRequest` (`0x4AC7D6`) on returning to AtReady. A queued request is dropped after
0.1 s while blocking.

## Moves

### Attack and block

- **Attack** (`Fight__StartAttack` `0x4AC0A9`): AtReady only. Reach `fight+0x1BC + type×4` and
  the type's Min/Max Angle decide the hit at the block point (`0x4ACE54`).
- **Blocked** if the defender is in Blocking/StartBlock with the same direction and faces the
  attacker within ±0.4 rad (the Sword Center angles). Sound `gblswordclank`, the defender
  takes `fight+0xB4` damage (untraced; guess: chip damage) and recoils `Blocking A Hit Recoil`.
- **Player block direction is automatic** (`Fight__StartBlock` `0x4AC758`): it copies the
  opponent's attack direction if he is attacking or feinting within reach. Otherwise it is
  random (0–2). It is not re-aimed while held.
- **Hit:** the target recoils `Being Injured Recoil`, enters BeingInjured and takes `Damage` +
  `fight+0xB0` + the attacker's weapon bonus (`Fight__TakeDamage` `0x4ADBA0`).

### Feint

| | |
|---|---|
| trigger | Walk + Attack L/C/R at ready (`Fight__StartFeint` `0x4AC229`); blades Walk+L+R = super feint |
| timing | `Sword/Staff/Blades <Dir> Feint`, `Super Feint` (`Fight__FeintDuration` `0x4AB7C6`) |
| clip | `CombatFeintLeft/Center/Right`, `CombatFeintSpecial_G4` |
| damage | none |

An AI defender answers a feint like an attack (`Fight__PickAIResponse`). At half the feint
duration, an AI that is blocking drops the block into state 7 with `Computer Recover From
Block` (0.3667 s) (`0x4AD858`).

AI use: at attack time, `rand(fight+0xEC) < fight+0xF0` (100 of 300 at start) and the last AI
action was not a feint (`fight+0x130 != 3`) (`0x4ACC36`).

### Counterattack

| | |
|---|---|
| opens | an L or R attack (not centre) is blocked by the player → `fight+0x9C = 1` (`0x4ACFD3`) |
| window | `Counter Attack Window` (0.333 s) from the block; closed at `0x4AD44D` |
| trigger | Attack in the **same direction** as the blocked attack, within counter reach `fight+0x1E0/0x1E4`, from Blocking (`0x4AC002`) |
| move | type 9/10 attack, begin `Sword/Staff Left Counter Attack Begin` (0.4667 / 0.5333) |
| clip | `CombatCounterLeft/Right` |
| damage | `Sword/Staff Left Counter Damage` (8 / 9), both sides (see quirk) |

The attacker stays in HitBeingBlocked until `Blocked … Recover`, `Counter Attack Window` and
`Spin Window` have all passed. A counter can be blocked like any attack.

AI use (`Fight__AITryCounter` `0x4AD975`): when an AI blocks an L/R attack and is within
counter reach, `rand(Opportunities) < c`, where c = `Counterattacks`, raised to
`Opp − (Opp − Counterattacks) × fight+0x160` by the same-direction and fast-attack penalties.

### Super counter (blades)

The counter input with weapon 3 enters SuperCounter (state 9) instead. After `Super Counter
Attack` (0.2 s) it hits without a block check: `Super Counter Damage` (6) + `fight+0xB0`, the
target recoils and enters BeingInjured (`0x4AD6AC`). Then AttackRecover for `Blades Recover From
Injuring` + `Super Counter Attack`. The AI does the same through `Fight__AITryCounter`.

### Sword clash

`Fight__Clash` (`0x4AC5FF`) closes both fighters' windows, pushes them to 1.5 apart
(`g_fight_ClashSeparation`) and sets both to Clash (`CombatClash`). No damage.

`Fight__CanClash` (`0x4AC517`) requires: the target attacking me with type 0–3, me at ready,
in my reach for that type, and both facing within ±0.4 rad.

- **AI** (`0x4AC9E8`, `0x4ACACA`): with response 0 (Late Attack) or 4 (Sneak Attack), if
  `|my Begin − time until his hit| ≤ 1/30 s` (`g_fight_ClashWindow`, hardcoded). A late
  attack waits until its own slowest swing would be late, so at 30 fps or more it usually clashes.
- **Sword Clashes / Opportunities** are rolled in `Fight__PickAIResponse` (`0x4AC301`) from the
  **attacker's** record, and only when the defender is AI, so they matter only for AI against AI.
  For the Prince the counters stay 0. All levels set Sword Clashes = 0.
- The clash check in `Fight__StartAttack` (`0x4AC179`) runs after the state is already 1, so
  `Fight__CanClash` fails there. It looks unreachable.

### Follow attack (sword, weapon 0/1)

| | |
|---|---|
| window | ±`Follow Window`/2 around the moment an unblocked L/C/R attack lands; a press in the half before it is buffered (`fight+0xA0` 2→3); cleared if blocked |
| trigger | the next direction in L → C → R → L (`Fight__FollowNextDir` `0x4AE8DE`) |
| clip | `CombatFollowRightToLeft`, `…LeftToCenter`, `…CenterToRight` |
| timing / damage | `Left/Center/Right Follow Attack Begin` 0.4, `… Follow Damage` 7 |

Follows do not chain. AI (`0x4AD3B8`): after its hit lands, if within follow reach, `rand(Follow
Attack Opportunities) < Follow Attacks`.

### Spin attack (staff, weapon 2)

| | |
|---|---|
| window | after an L or R staff attack is blocked: `fight+0xA0 = 1` for `Spin Window` (0.433 s) |
| trigger | the opposite attack: Left after Right → type 4, Right after Left → type 5 |
| clip | `CombatChainLeft_G3` / `CombatChainRight_G3` |
| timing / damage | `Left/Right Spin Attack Begin` 0.6, `Left/Right Spin Damage` 14 |

AI (`0x4AD08B`): on the block, `rand(Spin Attack Opportunities) < Spin Attacks` → spin to the
opposite side.

### Super attack (blades, weapon 3)

| | |
|---|---|
| trigger | an L or R blades attack opens `fight+0xA0` for 0.1333 s; Attack L+R within 400 ms in that time starts type 3 |
| clip | `CombatSpecialHit_G4` |
| timing / damage | `Super Attack Begin` 1.233, `Super Attack Damage` 18, recover `Super Attack Recover From Injuring` |
| blocked | a block of either side is a **partial hit** from the other side: type 11/12, `CombatSpecialHitLeft/Right_G4`, `Super Attack Partial Damage` 9 (`0x4AD17C`). Blocks are never direction 3, so it cannot be fully blocked. |

AI: the direction roll includes the double weight (3 × `Double Attack Percentage`) only for blades.

### Finishing move

`Fight__TakeDamage` caps any hit that would leave 1 HP or less at exactly 1 HP, unless the target
is already Dying (`0x4ADC0F`). BeingInjured then enters Dying (state 12, `CombatDying`) when
health is 1 (`0x4AD63B`). `Fight__SetState` lets Dying go only to AtReady, Dead or
BeingFinished, so a dying fighter cannot attack or block. The next hit is not capped and kills.

`Fight__TryFinishingMove` (`0x4AE6C5`) runs when the target is Dying, within reach and within the
attacker's angle limits:

- push both fighters to `g_fight_FinishingDistance` (1.85 sword, 1.5 staff, 1.5 blades);
- set N = weapon + 1. If the target is not the Prince, use the target actor's per-weapon number
  at `actor+0x1C0 → +0xDC + weaponIndex×4`, if not −1;
- finisher → Finishing `CombatFinishingMove<N>`, target → BeingFinished
  `CombatFinishingReceive<N>`; then the target is Dead.

| who | trigger |
|---|---|
| Prince | any attack press at ready: `Fight__StartAttack` always calls it last |
| AI | at ready, opponent Dying (`0x4ACBB4`) |

If Dying ends without a finisher: player or skill levels 7–10 return to AtReady with 1 HP, other
guards go to Dead (`0x4AD7CB`).

### Dodge and quick move

`Prince__StFightingSetSideStep` (`0x4AFF0B`) plays turn input as `CombatMoveRight/Left_L` (clip
0x55/0x56). With the double-tap flag (`actor+0xD8` bit 0x200000) it plays `CombatDodgeRight/Left_L`
(0x59/0x5A). `Prince__StFightingSetMove` (`0x4AFC5B`) does the same for forward/back:
`CombatMoveForward/Backward_L` or `CombatQuickMoveForward/Backwards_L`. Fight logic does not use
dodges (no invulnerability seen in `Fight__*`). No AI use found.

### Taunt

`CombatTaunt` (clip 0x52) plays when entering fight mode (`Fighter__StSetupForFightMode`
`0x4C604D`, also called by guard AI `0x4BA831`) and after drawing the sword
(`Prince__StDrawSwordDone` `0x4AF7FF`). Skipped when unarmed. No input triggers it.

### Woozy

`Fight__IdleAmbientOrWoozy` (`0x4AE60F`), at ready with nothing queued, after
`g_fight_IdleAnimDelay` (0.01 s), if `actor+0xDB` bit 0x40: plays `CombatWoozy` (0x4F) when health
≤ `Health__FromThirds(3)` (one bottle), else `CombatAmbient` (0x4E). It is cosmetic.

## AI responses to an attack

`Fight__PickAIResponse` (`0x4AC272`) runs on the attacker when an attack or feint starts, if the AI
defender is in threat range (`No Threat Distance` + reach) and inside the attack's angles. It
stores the choice in the attacker's `fight+0x14`.

1. Copy the defender's five buckets. Unless Proxy, scale Late/Wrong/Did-Nothing by m, which
   pushes the remainder into Blocks:
   - m = 1 − (1 − t / `Max Fast Attack Time`) × `Max Fast Attack Penalty`, where t is the time
     since the attacker last left recovery;
   - × `Same Direction Attack Penalty`, accumulated while attacks repeat a direction.
2. Roll the buckets. If Block is chosen: block delay = `AI Min Block Delay` + rand × (Max − Min),
   or 0 if any penalty applied. Against a super attack, Block becomes response 4.

How the defender acts on it in AtReady (`0x4AC962`):

| # | response | action |
|---|---|---|
| 0 | Late Attack | wait, then attack with its slowest direction once that is too late; clash if within 1/30 s |
| 1 | Wrong Block | block (dir + 1) mod 3 |
| 2 | Did Nothing | idle |
| 3 | Block | after the delay, StartBlock in the attack's direction (super → right); raised `Early Block` before impact |
| 4 | Sneak Attack | attack with its fastest direction if it lands first, else clash or block |

Own attacks: when `now ≥ fight+0x168` (`Fight__ScheduleNextAIAttack`, spacing from Aggression),
roll a direction from the weights (`0x4ACBF0`), then feint or attack. A target closer than the
fastest reach − 1.0 is attacked straight away.

## Not traced

- Who writes `fight+0xA8` (begin offset), `+0xB0` (damage bonus), `+0xB4` (block damage) and
  `+0xB8`.
- The table behind `actor+0x1C0 → +0xDC` (per-enemy finishing numbers), and `actor+0xDB` bit 0x40.
- What `sub_4AE00E` does for command 0x509 (Quick Turn, request kind 4).
- `StTauntAndGo` (`0x4BD3A4` area) and `Lvl8WaitAndTaunt_G` belong to creature scripts, not this code.
- The exact semantics of the clip-play arguments (`sub_4AEB78`): blend and queue are guesses.
