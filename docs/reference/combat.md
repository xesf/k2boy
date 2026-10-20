# Combat (first pass)

Guards patrol, notice the Prince, charge and fight. The Prince can pick up his sword, draw it,
attack in three directions and block. The port is the combat section of `src/main.cpp` and
`src/engine/fight.*`.

## What comes from the game

| item | source |
|---|---|
| attack timing: when a swing lands (`Sword <Dir> Attack Begin`), the unblocked window, recover after a block | `data/fight.ini`, 300 keys |
| AI distances: watch 10, charge 5, peripheral vision 30° | `data/enemy.dat`; the first number on each line is the live value |
| clips: Prince `_G2` with the sword (`CombatAtReady`, `CombatAttack{Left,Center,Right}`, `CombatBlockingCenter`, `CombatDrawWeaponBack`, `CombatStoreWeaponBack`, `CombatBeingInjured*_L2`, `CombatDeathGeneric_G2`, `DeathGeneric_G` unarmed); guards `_G1` / `_L1` | the actors' Skills folders |
| guard waypoints | EnemyStart argv3, node names resolved to positions |
| the sword | `P4_Sword` Pickup, argv2 `weapon` |
| health potions | Pickup argv1 `1`, argv2 the amount |

## What is ours

The original's `CharacterStateTable` and `EnemyAIObject` logic is not traced. These values are
placeholders until it is:

- **Health and damage:** Prince health 100. Guard health is 50 + 25 × the EnemyStart difficulty
  digit. A guard's hit does 12 damage, the Prince's does 25.
- **Reach:** a swing connects within 2.2 units and a 70° cone. Guards stop to fight at 1.6.
- **Guard behaviour:** a guard blocks 35% of the Prince's swings, and attacks every 1.4–2.6 s.
- **Blocking:** any block stops any attack; blocks are not matched to the attack's direction.
- **Respawn:** after 3.5 s at the level start, and guards return home.
- **Steering:** guards steer straight at their target with collision but no pathfinding. A guard
  locked in a cell (`P3PG11`, released by `P3PG11,start`) presses against the bars.

## Checked

- **Unarmed.** `P3PG2` notices the Prince from 6.3 units, charges and fights him. Health goes
  100 → 76 → 4 as the injured clips play.
- **Armed** (`--armed`). Holding block keeps health at 100 through 6 s of attacks. Two
  centre swings defeat the guard: health 50 → 25 → 0, then his dying clip, held.

Headless runs used `--armed`, `--hold KEYS@T0-T1` and `--place`.
