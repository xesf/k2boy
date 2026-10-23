# Small traps: FlameThrower, Spikes, Fountain, LightSwitch, ArrowCharger, Launcher

How the original implements six trap classes, for porting. Everything here is read from the
disassembly of `data/pop3dreal.exe` (MD5 `7a89eaef1997f089fa04f7a3f4a26779`). Guesses are marked
**(guess)**. Message syntax and the messages every trap understands are in
[trap-scripts.md](trap-scripts.md). Builder keys are in [trap-factory.md](trap-factory.md).

> **IDA names.** The disassembly was read offline with `llvm-objdump`; the names in the tables
> were applied in the IDA database afterwards, except where a function already had a name
> (`0x4C73D6` stays `Prince__Poison`, `0x4C763C` stays `Prince__Heal`).

## Shared pieces

### Trap vtable slots used here

Each class overrides a handful of slots of the 69-slot `Trap` vtable.

| slot | offset | meaning (from these overrides) |
|---:|---|---|
| 0 | `+0x00` | `GetRTTI` (returns the `NiRTTI`) |
| 6 | `+0x18` | scalar deleting destructor |
| 45 | `+0xB4` | save (writes `0xDEADBEEF`, then base save, then the class fields) |
| 46 | `+0xB8` | load |
| 48 | `+0xC0` | `reset` |
| 51 | `+0xCC` | attach to the scene / collision lists |
| 52 | `+0xD0` | `CanUse(pos, rot)`: the Prince may use this trap (ArrowCharger only) |
| 56 | `+0xE0` | name of the Prince's use skill (ArrowCharger only) |
| 60 | `+0xF0` | `Update(double time)` |
| 61 | `+0xF4` | `HandleMessage(msg, sender)`. Every override calls the base `0x4552B7` first |
| 63 | `+0xFC` | `0x45594B`: sets `+0x15B` and `+0x15C` (see below) |
| 67 | `+0x10C` | `SetCollide(b, …)` `0x455918` → `+0x15A` |
| 69 | `+0x114` | use-skill event (ArrowCharger only) |

### Helpers

| address | name | what it does |
|---|---|---|
| `0x50BC99` | `ParseBool` | `t/true/yes/on/1`, `f/false/no/off/0`. Anything else gives **false** |
| `0x50BB85` | `ParseInt` | |
| `0x50BB11` | `ParseDouble` | |
| `0x50BCBD` | `CountFields(sep)` | 1 + the number of separators, 0 for an empty string |
| `0x50BD02` | `GetField(i, sep)` | untrimmed |
| `0x426960` | `String__EqualsNoCase` (guess: case-insensitive) | data uses both `On` and `on` |
| `0x47169F` | `SpecialEffect__Instance(name)` | looks up `name` in the effect library (`g_SpecialEffects` `0x801978`) |
| `0x4C73D6` | `Character__ApplyDamage(n)` | health `+0x130` −= n, clamped at 0. No death skill |
| `0x4C74E3` | `Character__Hurt(n, source, deathSkill, flag)` | `ApplyDamage`, then on death records the killer (`+0x1B4`) and the death skill (`+0x138`) |
| `0x4C763C` | `Character__Heal(n)` | health += n, clamped to max `+0x174` |
| `0x4683D0` | `Trap__PlaySkill(name, t0, t1, flag)` | actor `+0xD0` slot `0xBC` |
| `0x489F5E` | `Trap__PlaySkillAlt(name, …)` | the same through slot `0xC0` |
| `0x46845F` | `Trap__SkillDone(t)` | |

The effect library is `Geometry/Utils/SpecialEffects.sef`, a two-column `name  path.nif` list.
A name that is not in the list returns null, and the caller skips the effect.

**Hazard gate.** FlameThrower, Spikes and ArrowCharger test the Prince only when
`!g_7E9BF4 || trap+0x15C`. `g_7E9BF4` (`0x7E9BF4`) is set by `0x41E5B5`, called from `0x41B8E5`.
`+0x15C` is the second argument of slot `0xFC` (`0x45594B`), which also logs `"fms update"`.
**(guess)** This means "the trap's room is active".

**Low-gore mode.** When `0x7EAC40 == 1`, `Character__Hurt` replaces the death skills
`DeathDecap_G`, `DeathFeetGrinders_G` and `DeathSpikes_G` with `DeathFeetSpikes_M`.

---

## FlameThrower

Builder `0x470C3E`, vtable `0x747638`, object size `0x1D0`.

### argv

| argv | field | meaning |
|---|---|---|
| 1 | `+0x170` (current), `+0x1A0` (reset copy) | starts on (`ParseBool`) |
| 2 | `+0x174` (current), `+0x1A4` (reset copy) | on/off pattern in seconds, comma-separated. Parsed into a `double[]` at `+0x184`, count at `+0x18C` |

In the data, argv1 is always `true`. argv2 is `""` (twice), `2,2`, `2,3,5,2,1,3,2,4,1`,
`4,1,3,2,4,2,1,1,4,2` and `4,1,3,2,5,2,6,2,1`.

### Functions

| address | name |
|---|---|
| `0x470CA5` | `FlameThrower__Builder__Create` (`new 0x1D0`) |
| `0x470D60` | `FlameThrower__ctor` |
| `0x47102C` | `FlameThrower__dtor` |
| `0x4710EA` | `FlameThrower__Update` |
| `0x471206` | `FlameThrower__HandleMessage` |
| `0x4712C2` | `FlameThrower__Reset` |
| `0x471332` / `0x4713BC` | `FlameThrower__Save` / `__Load` |
| `0x4714A5` | `FlameThrower__ParsePattern` |
| `0x47155A` | `FlameThrower__DamagePrince(dt)` |

### Construction (`0x470D60`)

- The flame is the `FlameThrower` special effect (`geometry\utils\FlameThrower.nif`), at
  `+0x160`. It is attached as a child of the trap.
- Position = the placement node's translation. Rotation = node rotation × `0x555BF0(3, −π/2)`
  (`0x74761C` = 1.5707964, negated). **(guess)** This is a −90° turn about one axis, so that the
  effect's axis lines up with the nozzle.
- Hit segment: from the nozzle `+0x1B4` to nozzle + R·(0, −2, 0) (`0x743ADC` = −2.0), stored at
  `+0x1C0`. A 2D segment in x/z (`0x412577`) is built from both ends, at `+0x1CC`. **The flame
  reaches 2.0 units.**
- On: effect slot `0xBC(0)` (start), then slot `0x64(0)` (shown). Off: slot `0x64(1)` (culled).

### Timing (`0x4710EA`, arg = absolute time)

`+0x188` is the "flame burning" phase flag. It starts true and the pattern index (`+0x190`) starts
at 0, so **the pattern alternates burn, pause, burn, …, starting with burning**, and wraps around.

```
if last_time (+0x168) == 0: skip                 // first frame only records the time
if on and pattern and next (+0x198) == 0: next = t + pattern[idx]
if on and pattern and t > next:
    idx = (idx + 1) % count
    next = t + pattern[idx]
    burning = !burning
    burning ? effect.Start(0), Show : effect.Stop(), Hide   // slots 0xBC / 0xC0, 0x64
if on and (no pattern or burning) and hazard gate: DamagePrince(t - last_time)
last_time = t
```

With an empty pattern, an "on" flamethrower burns continuously.

### Damage (`0x47155A`)

1. Take the Prince position p (`g_prince` `0x7FFB74`). Test the x/z segment at `+0x1CC` against a
   circle at p.xz with the Prince's radius (`0x4C9483`, test `0x4FD8CB`). Miss → return.
2. d = distance in x/z from the nozzle to p. The flame point is nozzle + R·(0, d, 0). Its height
   y_f must satisfy `p.y <= y_f <= p.y + height` (height from `0x4C93BE`).
3. Damage = `max(1, (int)(25.0 × dt))` (`0x747620` = 25.0), through
   `Character__Hurt(n, this, "", 0)`. **That is 25 health per second, at least 1 per frame.** No
   death skill is given, so the Prince plays his default death.

The trap plays no sound. `Audio/sfx/MULflamejet001.sfx` exists, but nothing in the class plays
it. **(guess)** It is triggered from the effect `.nif` or from a level `AudioMessage`.

### Messages (`0x471206`)

| message | effect |
|---|---|
| `on` | on = true, start and show the effect. The pattern position is kept |
| `off` | on = false, idx = 0, burning = true, next = 0, stop and hide the effect |
| anything else (not a base message) | **replaces the pattern**: the message text is copied to `+0x174` and re-parsed, which resets idx = 0, burning = true, next = 0 |
| `reset` (slot `0xC0`, `0x4712C2`) | restores on and the pattern from the argv copies, last_time = 0 |

---

## Spikes

Builder `0x48A539`, vtable `0x748D24`.

### argv

| argv | meaning |
|---|---|
| 1 | actor name. Empty → `TRAP_Spikes` (`0x7C36C0`) |

All four shipped Spikes use argv0 `,true,true,true` and an empty argv1. The actor is loaded as
`actors\<name>\<name>.k2a` when `0x4FA5EF(8)` (a bit in `0x801D50`) is set. Otherwise it is loaded
by name (`0x40550F`). It is wrapped in an `AnimationNode` (`0x401E3C`) at the node transform. The
`TRAP_Spikes` skills are `StartPosition`, `Strike`, `Reset` and `!Hit`.

### Functions

| address | name |
|---|---|
| `0x48A65B` | `Spikes__ctor` |
| `0x48A98E` | `Spikes__Update` |
| `0x48A9C1` | `Spikes__Reset` |
| `0x48AA32` | `Spikes__PrinceInTriggerRange` |
| `0x48AAC7` | `Spikes__CheckKill` |
| `0x48AB63` | `Spikes__KillCharacter` |
| `0x48ABD3` | `Spikes__StInit` |
| `0x48AC05` | `Spikes__StArmed` |
| `0x48AC3B` | `Spikes__StStriking` |
| `0x48AC5C` | `Spikes__StUp` |
| `0x48AC92` | `Spikes__StRetracting` |
| `0x48ACC7` / `0x48ACBA` | `Spikes__SetState` / `__RunState` (function pointer at `+0x160`) |

### Volumes

The volumes are vertical cylinders around the trap origin `+0x168` (the node translation).
dy = prince.y − trap.y.

| test | dy range | radius² (radius) |
|---|---|---|
| trigger `0x48AA32` | −0.05 < dy < 2.0 | < 1.5625 (1.25) |
| kill `0x48AAC7` | −0.05 < dy < 0.3 | < 0.5625 (0.75) |

The Prince position is his world translation (`0x40A263`), so it is his feet. Both tests check only
the Prince (`g_prince`), and both pass through the hazard gate.

### Behaviour

`Update` (`0x48A98E`) runs every frame, in this order:

1. `SetCollide(false)` (slot `0x10C`): **spikes are never solid**.
2. If the hazard gate passes, `CheckKill`.
3. Run the state function.

**The kill check does not depend on the animation state.** A Prince whose feet are inside the kill
cylinder dies, even while the spikes are retracted. The strike animation is only a visual and
proximity response.

| state | action | next |
|---|---|---|
| Init (`0x48ABD3`) | armed flag `+0x164` = 0, play `StartPosition` | Armed |
| Armed | Prince in the trigger cylinder → play `Strike` | Striking |
| Striking | `Strike` finished | Up |
| Up | Prince **left** the trigger cylinder → play `Reset` | Retracting |
| Retracting | `Reset` finished: `+0x164` = 0 | Armed |

Reset (slot `0xC0`) and load both go back to Init.

### Kill (`0x48AB63`)

`CheckKill` stores its result in `+0x164` every frame, so the kill runs once per entry into the
cylinder. On a kill:

- the spike actor plays `!Hit` through `PlaySkillAlt` (slot `0xC0`)
- `Character__Hurt(0x7FFFFFFF, this, "DeathSpikes_G", 1)`: instant death, death skill
  `DeathSpikes_G` (`DeathFeetSpikes_M` in low-gore mode)

Spikes add no message handler beyond the base messages (slot 61 is not overridden).

---

## Fountain

Builder `0x472E52`, vtable `0x747894`, object size about `0x2978`. A **purely cosmetic** water
particle system. It never touches the Prince: no healing, no damage, no collision change.

### argv

| argv | meaning |
|---|---|
| 0 | the base field (`InitialState,ResetEnabled,Visible,Collideable`). **On = (InitialState == "on")**, compared at `0x472FCC`, stored at `+0x2970` |

The two shipped fountains use `On,true,true,false` and `on,true,true,true`. No other argv is read.

### Functions

| address | name |
|---|---|
| `0x472F74` | `Fountain__ctor` |
| `0x473161` | `Fountain__BuildParticles` |
| `0x4733FC` | `RandRange(a, b)` = a + rand()·(b − a)/32767 |
| `0x47312B` | `Fountain__Update` |
| `0x47344F` | `Fountain__Simulate` |
| `0x4737E8` | `Fountain__HandleMessage` |
| `0x47383A` | `Fountain__Reset` |

### Geometry (`0x473161`)

- An `NiAutoNormalParticles` (vtable `0x74F01C`) with 512 points at `+0x160`
- Properties: an `NiFogProperty`, an `NiVertexColorProperty` (mode 1), and the texture
  `Geometry/Utils/WaterDrop.tga`
- Each drop starts at local (0, 2.5, 0), with colour (0.6, 0.6, 0.88, 1.0)
- Velocities are kept at `+0x164` (512 × vec3) and timers at `+0x1968` (512 doubles). Every timer
  starts at −1, meaning "free"
- Initial velocity: x and z in [−1.25, 1.25], y in [−0.625, −3.125] (2.5 × the factors ±0.5,
  −0.25, −1.25)

### Simulation (`0x47344F`, every frame)

It uses a **fixed step of 0.04 per frame** and ignores the real dt. Per drop:

- **Emit.** If on and the frame's budget (16) is not used up, a free drop (timer < 0) gets
  timer = 0 (alive).
- **Expire.** If timer > 0, timer −= 0.04. When it drops below 0, the drop respawns at
  (0, 2.5, 0) with a new colour and a new random velocity. Its timer becomes 0 while on and −1
  while off.
- **Alive** (timer ≥ 0):
  - r and g fade by 0.01 per frame
  - pos += vel × 0.04, then vel += (0, −25, 0) × 0.04. Gravity −25 is at `0x7FC910`
  - When pos.y < 0 (the node origin), the drop bounces: vy ×= −0.25 and its colour becomes
    (1, 1, 1, 1). If timer == 0, timer = RandRange(0.25, 0.5) (it lingers, then respawns)
- The bounding radius is the largest |pos|. The particle node is shown only while at least one
  drop is alive.

### Messages

| message | effect |
|---|---|
| `on` / `off` | sets `+0x2970`. Live drops finish their arcs |
| `reset` | on = (state == "on"); shows or culls the particle node |

---

## LightSwitch

Builder `0x476ED7`, vtable `0x747DE4`. **This is not something the Prince uses**: it has no
proximity test and no action input. Scripts toggle it by message. It darkens or restores the
lighting of the scene node it belongs to.

### argv

| argv | field | meaning |
|---|---|---|
| 1 | `+0x160` | starts on (`ParseBool`, so an empty value means off) |

Only one exists in the data: `TorchChamber_LowLIGHTS` (`roof1/TorchChamberLow.k2a`), argv1
`false`. An Area script turns it on: `…;delay,4.5,TorchChamber_LowLIGHTS,on;…`.

### Functions

| address | name |
|---|---|
| `0x476FF9` | `LightSwitch__ctor` |
| `0x4771E7` | `LightSwitch__SetOn(bool)` |
| `0x4771BC` | `LightSwitch__HandleMessage` |
| `0x477260` | `LightSwitch__Reset` |

### What it does

The constructor builds two properties:

- `+0x164`: an `NiMaterialProperty` (vtable `0x74EB48`) whose colour at `+0x1C` is set to
  (0.1, 0.1, 0.1) (`0x41E938`). **(guess)** This is the ambient colour.
- `+0x168`: an `NiVertexColorProperty` with mode 3 (`0x41EA08`)

`SetOn(on)` (`0x4771E7`), with N = the trap's `+0x128` object. **(guess)** N is the room or
parent scene node.

- N`+0x175` = !on, always (`0x477253`). **(guess)** This is a "room is dark" flag.
- If `on` differs from the current state `+0x161` (initially 1):
  - **off**: attach both properties to N (`0x405D44`). The room renders dark.
  - **on**: detach both (`0x4228F7`). Normal lighting returns.

### Messages (`0x4771BC`)

Any message that is not a base message goes through `ParseBool` and then `SetOn`. So `on`, `T`,
`true` and `1` turn it on. Anything else, including a typo, turns it **off**. `reset` restores
argv1.

---

## ArrowCharger

Builder `0x464FAA`, vtable `0x746724`. A shrine that turns one normal arrow into a special arrow.
The Prince uses it with the action input.

### argv

| argv | field | meaning |
|---|---|---|
| 1 | `+0x160` | arrow type, an **integer** (`ParseInt`). See the table below |
| 2 | `+0x168` | duration in seconds (double) |
| 3 | `+0x170` | on (`ParseBool`). Reset sets on = (InitialState == "on") |

The arrow type index comes from the weapon name table at `0x745C04` (9 arrow entries,
`0x745BEC` = 9). The bit used in the Arrow hit switch is `1 << (type + 4)`.

| type | table name | Traps.txt name | bit |
|---:|---|---|---|
| 0 | `arrows` | (normal arrow) | `0x10` |
| 1 | `BlueFire` | Fire | `0x20` |
| 2 | `Charm` | Charm | `0x40` |
| 3 | `Swarm` | Swarm | `0x80` |
| 4 | `Possess` | Possession | `0x100` |
| 5 | `Freeze` | Freeze | `0x200` |
| 6 | `Magic` | Exploding | `0x400` |
| 7 | `Death` | Death | `0x800` |
| 8 | `Stealer` | LifeStealer | `0x1000` |

Shipped chargers:

| node | room | argv1 / argv2 / argv3 |
|---|---|---|
| `CliffsD_Arrows_PICK` | `cliffs/cliffsDinner` | 1 (BlueFire) / 60 / true |
| `posess_ARROWS` | `Ruins/possessarea` | 4 (Possess) / 40 / true |
| `Towercenter_Shrine_CHARGE` | `Palace4/TowerCenter` | 6 (Magic) / 75 / false |

### Functions

| address | name |
|---|---|
| `0x4650CC` | `ArrowCharger__ctor` |
| `0x465241` | `ArrowCharger__CanUse(pos, rot)` (slot `0xD0`) |
| `0x4653A2` | `ArrowCharger__GetUseSkill` (slot `0xE0`): returns `UseArrowCharger_M` |
| `0x4653A8` | `ArrowCharger__HandleMessage` |
| `0x465486` | `ArrowCharger__OnUseEvent(char)` (slot `0x114`) |
| `0x4C1EBC` | `Inventory__AddArrows(type, count, duration)` (Prince `+0x1C8`) |
| `0x4C1EAA` | `Inventory__ArrowCount(type)` |

The constructor sets `SetCollide(false)`.

### When the Prince can use it (`0x465241`)

Every condition must hold:

- the hazard gate, and trap flag `+0x158`
- |prince − charger|² < 2.7² (`0x7C1CC4`): **within 2.7 units**
- the Prince faces the charger. With f = R·forward (`0x8A0CD8`), take
  dir = normalize(charger − (pos + f·0.5)). The test is dot(f, dir) > 0.8 (`0x743F94`), about
  ±37°
- the charger is on
- the Prince holds at least one **normal** arrow (`Inventory__ArrowCount(0)`)

### The Prince's states

The Prince's dispatcher at `0x4B175E` picks this path when his use target (`prince+0x1B4`) has the
ArrowCharger RTTI (`0x7FB5D0`).

| state | address | what it does |
|---|---|---|
| `StStartArrowCharger` | `0x4D3142` | builds a goal transform from the charger, then walks there ("Go to the pickup!") |
| `StIdleArrowCharger` | `0x4D3353` | waits for arrival (`+0xDB & 0x40`) |
| `StStartUseCharger` | `0x4D33B7` | plays `GetUseSkill()` = `UseArrowCharger_M` |
| `StIdleUseCharger` | `0x4D3477` | on each skill event (`+0xD8 & 0x200`), passes the event character to the charger's slot `0x114`. Ends when the skill ends (`0x40000000`) |

### Skill events (`0x465486`)

| event | effect |
|---|---|
| `'1'` | `AddArrows(0, −1, 0)`: one normal arrow is consumed. The `arrow` weapon model (`0x4CC10E`) is attached to the Prince's hand (`0x4C18E3`) |
| `'4'` | `AddArrows(type, +1, duration)`: one special arrow is gained. The model is removed from the hand (`0x4C194D`) |

`AddArrows` (`0x4C1EBC`), for a positive count:

- Difficulty `0x7EAC3C`: 0 gives count + 1 (**2 arrows**), 1 gives count, 2 or more gives count − 1
  with a minimum of 1.
- Swarm (type 3) is capped: if you already hold one, nothing is added.
- When the duration is non-zero, the type's bit is set in `+0x5C` and the duration goes to
  `+0x60`. **(guess)** The special arrows expire after that many seconds. The expiry test is
  `0x4C1FB3` (compares `+0x68` with the owner's `+0xF0`), not traced further.

### Messages

| message | effect |
|---|---|
| `on` / `off` | sets `+0x170` |
| `reset` | on = (state == "on") |

### What each arrow does on hit (`Arrow__OnHitCharacter` `0x4A4E26`)

The `[Archery]` section of `data/enemy.dat` sets the values. The first number on each line is
the one used. The binary's defaults are at `0x7C5640…`. Before applying the switch, the code skips
a victim whose inventory check `0x4C1FB3` returns true.

| type | effect |
|---|---|
| normal | `ApplyDamage(kArrowNormalDamage)` (12) |
| BlueFire | `ApplyDamage(kArrowBlueFireDamage)` (30 in `enemy.dat`, default 24) |
| Charm | `ApplyDamage(20)`, then charm lights at the hit point for 5 s (`0x4A86CC`, `0x745BB0` = 5.0). Guards react with `StWatchCharmingLights` / `StCharmedByLights` |
| Swarm | `ApplyDamage(kArrowSwarmDamage)` (24), then spawns a swarm (`0x4A51BC`) that flies to the nearest enemy |
| Possess | `ApplyDamage(normal × 0.5)`. If the victim lives, the Prince possesses it for `kArrowPossessionTime` (40) (`0x4C7733`) |
| Freeze | clears `0x7FEF04`, then freezes the victim (`0x4A86F0`, `geometry\utils\freezeblock.k2a`). At most one freeze arrow at a time |
| Magic | area damage to every enemy (`0x4A5134`): `kArrowMagicDamage − |d|²·kArrowMagicDamageScale` (30 − d²), when positive |
| Death | `BlackArrowAction` (`0x4A537C`, turns the victim black over `kArrowDeathBlackTime` = 10), then `ApplyDamage(full health)` |
| Stealer | `ApplyDamage(kArrowStealerDamage)` (15) to the victim, and `Heal(15)` to the shooter when the shooter is the Prince |

The flight and impact effects come from per-type name maps (`0x800F38` flight, `0x800E18` impact).
They name entries in `SpecialEffects.sef`, such as `ArrowFire` / `ArrowFireImpact` and
`ArrowDestruct` (Magic). The impact sound is `mularrowhitop005`.

---

## Launcher type 2: Fireball (compared with darts)

Builder `0x475A7E`, vtable `0x747C28`. argv1 − 1 → `+0x160`: 1 = dart (0), 2 = fireball (1). The
data has 23 dart launchers and 18 fireball launchers. The only message is `fire`
(`0x475D64`, case-insensitive compare). It calls `Launcher__Fire` (`0x475D97`), which creates the
projectile at the launcher's own world transform (`+0x12C`). It runs the projectile (`0x439ACF`)
and logs `"Projectile launched!"`. Launchers have no timer: scripts send `fire`.

Both projectiles derive from `Projectile` (base ctor `0x43981C`, an `NiAction`). The velocity is
the same for both: R·forward (`0x8A0CD8`) × **15.0** (`0x747D3C`).

| | Dart (`0x747D44`) | Fireball (`0x747D8C`) |
|---|---|---|
| ctor | `0x475E57` (size `0xE0`) | `0x476215` (size `0xD8`) |
| body | model 0 of `geometry/utils/projectile.k2a` (`g_ProjectileData` `0x7FCF18`) | the `FireBall` effect (`geometry\utils\FireBall.nif`), rotated to the launcher and started |
| launch effect | `DartFire`: **not in `SpecialEffects.sef`**, so nothing is drawn | `FireBallIgnite`: **not in the .sef** (`FireBallIgnite.nif` exists but is not registered), so nothing is drawn |
| trail | `DartTrail` effect attached to the dart | none (the body is the effect) |
| launch sound | `MULdartshoot001` (id `0x1388`) | **none** |
| hit a character (slot 12) | `0x475FAE`: `DartImpact` effect, `MULdarthitz001` (id `0x1389`), removed, `ApplyDamage(20)` | `0x476358`: stops `FireBall`, `FireBallHit` effect (**not in the .sef**), removed, `ApplyDamage(0x7FFFFFFF)`: **instant death** |
| hit something else (slots 14, 16) | `0x476058` / `0x4760F8`: `DartImpact`, `MULdarthitz001`, removed | `0x4763CD` / `0x476445`: `FireBallHit` (nothing drawn), then the base handler (`0x439F44` / `0x43A10D`), which removes it |
| fireball debris | | `FireBall_debree_20/30/50meters` are in the .sef but not referenced by the Launcher code. **(guess)** They are used by level `SpecialEffect` traps |

Both use `ApplyDamage`, not `Hurt`, so neither passes a death skill. A fireball kill plays the
default death.

---

## Untraced

- `g_7E9BF4` and `Trap+0x15C`: what they mean exactly (the hazard gate).
- `0x555BF0(3, angle)`: which axis index 3 is, which fixes the flame direction.
- `Trap+0x128` on a LightSwitch: the class of the object, and who reads its `+0x175`.
- The caller of ArrowCharger slot `0xD0`. **(guess)** The Prince's action-target search.
- Special-arrow expiry (`0x4C1FB3`), and the type→effect-name maps at `0x800E18` / `0x800F38`.
- The Charm, Swarm, Possess and Freeze sub-behaviours (`0x4A86CC`, `0x4A4426`, `0x4C7733`,
  `0x4A86F0`).
- Projectile movement and collision (`Projectile` base, `0x439xxx`): gravity, lifetime, what
  counts as a hit.
