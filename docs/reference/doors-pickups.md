# Doors, Apertures and Pickups

Recovered from `pop3dreal.exe` (offline IDA export; names are ours, `sub_` where the export has no
name). Companion to [trap-scripts.md](trap-scripts.md), [small-traps.md](small-traps.md) and
[trap-factory.md](trap-factory.md). Confidence tags: **[C]** read straight from the code,
**[I]** inferred.

Trap header (`Trap::Trap`, `0x454A41`, every trap): `argv0 = "<state>,<enabled>,<visible>,<collide>"`.
Field 1 -> byte `+0x158` (vtable `+0x104`, `0x4558F3`), field 2 -> `+0x159` (`0x455900`, also
culls the node), field 3 -> `+0x15A` (`0x455918`). `parse_trap_header` in `src/engine/trap.cpp`
already reads them this way. Note that `trap-scripts.md` line 165 names the second and third
fields `<visible>,<collide>`; for Fixtures that may differ, for the base header it is
enabled / visible / collide.

---

## 1. Door

### 1.1 Classes and addresses

| What | Address |
|---|---|
| `Door::Builder` ctor / registry key `"Door"` | `0x46BA2F` / `0x7C205C` |
| `Door` vtable | `0x74718C` |
| `Door::Door(Trap args)` | `0x46BB61` |
| Door message handler (vtable slot `0xE0`) | `0x46C094` |
| Door set-visible/collide forward | `0x46C050` -> `0x46C07D` |
| Door reset | `0x46C16E` |
| Door save / load | `0x46C2C1` / `0x46C344` |
| `MotivateForce` ctor (skill-driven collision body, "Force type 3") | `0x518856` |
| Force factory (`type` 0..3; "Bad force object" if the node has >1 child) | `0x501373` |
| MotivateForce skill message handler | `0x51926F` |
| MotivateForce sampler (pose at time t) | `0x5190CB` |
| `Aperture::Builder` ctor / `Aperture` ctor / vtable | `0x463CB8` / `0x463DDA` / `0x7464D4` |

### 1.2 Construction [C]

`argv1` = actor directory (default `TRAP_Door`), `argv2` = "Transparent" (bool, `0x50BC99`).

1. The actor is `Actors/<argv1>/<argv1>.act`; failure prints `"Door proto failed"`.
2. If `argv2` is true a `"DoorTrans"` property (alpha/chroma mode 1) is attached to the actor.
3. The actor instance (`0x120`-byte object) is wrapped by `sub_501373(type 3)` into a
   `MotivateForce` named `"DoorForce"` and parented to the Door's parent. The Force owns the
   collision: its shape follows the actor's animated skeleton. If the wrap fails the log says
   `"Failed to make DoorForce"` and the Door keeps the plain actor, which has **no collision**.
4. `Traps.txt`: "Doors may be converted to forces; if their geometry doesn't match the required
   geometry for forces, they'll lose their ability for collision. If their motion doesn't match
   that required by forces, they will only collide if they are at their 'home' position."

### 1.3 Messages [C unless marked]

* `reset`, `state:X`, `enable:b`, `visible:b`, `collide:b` go to the common trap handler first
  (`0x4552B7`). `visible` and `collide` are forwarded to the DoorForce (`0x46C050`), so a closed
  door that is told `collide:F` lets the Prince through.
* Any other message is a skill name. The Door drops a message that begins with its current state
  string (`strnicmp(msg, state, strlen(state))`, `0x46C127`), so repeating the message of the
  state it is already in does nothing [I: prefix, not whole-word, test]. Otherwise it stores the
  message in `+0x168` and hands it to the DoorForce (`vtable +0xE0`), which looks the name up in
  the actor's skills and starts it: `current = skill`, `start = now` (`0x51926F`).
* An unknown name logs `"Failed to find skill %s"`. A name of `enable`, `visible` or `collide`
  that reaches the skill table logs `"Doors can't handle message %s"` (it should have been
  taken by the base handler).
* The skill is sampled by `0x5190CB`: before it ends the pose is the interpolated key, after it
  ends the last key holds. There is no timer in the Door itself; open/close duration is the
  `SklK` clip length (see trap-scripts.md "Door: skill-name messages").
* Sound: the Door class has no sound code. Any door noise comes from the sound keys inside the
  `SklK` clips.
* Save: the last message is stored (`0x46C2C1`) and replayed through `SkillAt` on load
  (`0x46C344`). Reset (`0x46C16E`) steps every skill to its end and applies the authored
  initial state.

### 1.4 Gates, keys and locks

There is no key inventory and no "locked door" class in the binary. Everything is data:

* A **key** is a Pickup with a free item name (`Key`, `Floor`/`Table`) whose `argv3` is a script,
  for example `LobbyKeyAction,state:unlocked` (Dirig2/Lobby) or
  `CityGate_KeyAction,State:Unlocked`.
* The lock is an **Action** trap with state blocks `{Locked:...};{Unlocked:...}`. In `Locked`
  its use-script plays a refusal line (`audio,L01dlogprince005.wav`); in `Unlocked` it plays the
  key animations (`KeyGetFromInventory_G`, `KeyUse_G`, `KeyFinish_G`), shows the key in the
  hand (`prince,Item:Key01,on`), and moves the gate with `move,blue,5,4,1,0`, then
  `delay,10,...,move,blue,-5,1,0,0,crush;prince,Item:key01,off`. The gate is a Force/Mover, not a Door.
* `Action::CanUse` (`0x463350`) is documented in trap-scripts.md.

### 1.5 Aperture [C]

`Aperture` (`0x463DDA`) is a portal marker: an invisible `NiCollisionSwitch` child of its parent
node, registered with the room manager (`sub_420477`, `argv0` as the key, `0x420441`). It is
neither drawn nor collided with. Only with debug flag `0x20000` does it get a translucent
`(0.4, 0.99, 0.4)` fill. A missing parent logs `"Illegal aperture"`. Rooms behind it load or
show through it; our code treats it as a volume that is skipped from geometry
(`src/main.cpp` ~3111), which matches.

---

## 2. Pickup

### 2.1 Classes and addresses

| What | Address |
|---|---|
| `Pickup::Builder` ctor / key `"Pickup"` | `0x47DC6F` / `0x7BDA88` |
| `Pickup` vtable | `0x748294` |
| `Pickup::Pickup` (parses args, builds model) | `0x47E631` |
| Model and idle-effect setup | `0x480643` |
| `CanUse` (vtable `+0xD0`) | `0x47F9F1` |
| Reach / grasp / use / finish clip getters (`+0xD4`, `+0xD8`, `+0xE0`, `+0xEC`) | `0x47FB90`, `0x47FB9B`, `0x47FBA9`, `0x4803A2` |
| Take: attach item to the hand (`+0xE4`) | `0x47FBB4` |
| Apply effect (`+0xE8`) | `0x47FC8C` |
| Re-arm tick (reload quivers) | `0x47F9B1` |
| Reset (level start / load) | `0x4803B9` |
| Prince-side states | `StStartGrasp 0x4D35BE`, `StIdleGrasp`, `StStartManipulation 0x4D392C`, `StStartFinishTrap 0x4D3A73` |
| Type tables (std::map type -> value) filled at `0x4EB9B6` | models `0x800DE0`, idle fx `0x800F28`, residual fx `0x800EF8`, sounds `0x800E28` |
| Prince-side effect setters | heal `0x4C763C`, poison `0x4C73D6`, extra life `0x4C7673`, shield `0x4C1FCA`, strength `0x4C1FE7`, jump `0x4CA4F7`, fall `0x4CA525`, camo `0x4CA2A4`, guard `0x4CA44F` |
| Generic "first usable trap" scan used by the action button | `0x44A179` |

### 2.2 Arguments [C]

`argv1` is parsed as an integer (`0x50BB44`). If it parses it is the **potion type**; if not it
is an **item name** (type 12). `argv2` and `argv3` depend on that.

| Type | Effect | Item/code made by the ctor | Sound (`.sfx`) | Residual fx (on the Prince) | Drink-finish clip |
|---|---|---|---|---|---|
| 1 | Heal | `heal:<argv2>` | `POTBlue` | `PotionHealResidual` | `DrinkFinishHeal_G` |
| 9 | FullHeal | `Fullheal:` | `POTViolet` | `PotionFullHealResidual` | `DrinkFinishFullHeal_G` |
| 2 | ExtraLife | `ExtraLife` | `POTGreenl` | `PotionLifeResidual` | `DrinkFinishLife_G` |
| 3 | Strength | `Strength:<argv2>` | `POTYellow` | `PotionStrengthResidual` | `DrinkFinishStrength_G` |
| 7 | Camouflage | `Camo:<argv2>` | `POTSilver` | `PotionCamouflageResidual` | `DrinkFinishCamouflage_G` |
| 8 | Guard | `Guard:<argv2>` | `POTGold` | `PotionGuardResidual` | `DrinkFinishGuard_G` |
| 4 | Shield (arrow immunity) | `Shield:<argv2>` | `POTRed` | `PotionImperviousResidual` | `DrinkFinishImpervious_G` |
| 10 | Poison | `Poison:<argv2>` | `POTGreend` | `PotionPoisonResidual` | `DrinkFinishPoison_G` |
| 5 | Jump | `Jump:<argv2>` | `POTOrange` | `PotionJumpResidual` | `DrinkFinishJump_G` |
| 6 | Fall (feather) | `Fall:<argv2>` | `POTWhite` | `PotionFallResidual` | `DrinkFinishFall_G` |

Types 1-10 use actor `TRAP_Potion`, reach `BendDownToGrab_G`, grasp `GraspPotion_M`, use
`DrinkPotion_G`. The bottle model is chosen per type in `0x480643` from
`Geometry/Utils/Potion_Healing / FullHeal / ExtraLife / Strength / Camouflage / Guard / Shield /
SuperJump / FeatherFall / Poison1..3.nif` (poison picks one of three at random). A second effect,
`Potion<Name>.NIF` (`PotionHeal`, `PotionFullHeal`, `PotionLife`, `PotionStrength`,
`PotionCamouflage`, `PotionGuard`, `PotionImpervious`, `PotionPoison`, `PotionJump`,
`PotionFall`), is attached to the pickup's `Handle` node as its **idle sparkle** while it lies
in the world.

Item pickups (type 12): `argv1` actor directory (`Key`, `PrinceSword01`, `Arrows`, `PICK_Jewel2`,
`ITEM_Strongbox`...), `argv3` a script string, and `argv2` the kind:

| `argv2` | Reach / Grasp / Use / Finish | Y offset |
|---|---|---|
| `Potion` | obsolete: logs "Obsolete potion format" | |
| `Weapon` | `BendDownToGrabObject_G` / `GraspPotion_M` / `InspectWeaponFromGound_G` / `InspectWeaponFinish_G` | 0 |
| `Reload` | as `Weapon`, plus reload rule (2.5) | 0 |
| `Floor` | `BendDownToGrabObject_G` / `GraspPotion_M` / `InspectSmallFromGround_G` / `InspectSmallFinish_G` | 0 |
| `Table` | `BendToGrabOff1m_G` / `GraspOff1m_M` / `InspectOff1m_G` / `InspectOff1mFinish_G` | 0.95 |
| anything else | `argv4..argv7` are the four clips, `argv8` the Y offset (strongbox: `.5`) | `argv8` |

Reload: `argv3 = "<arrowType>,<count>,<minimum>"`. The ctor builds the code
`Prince,Weapon:+arrow,<type>,<count>` itself and stores `type` (`+0x1DC`) and `minimum`
(`+0x1E0`). Bad arguments log `"Bad pickup arguments in %s"`.

Errors: no `argv1` -> `"Missing item in pickup %s"`; no `Handle` node in the actor ->
`"Pickup needs handle named 'Handle'"`; no `.k2a` -> `"Can't load %s"`.

### 2.3 Who can take it: CanUse (`0x47F9F1`) [C]

All of these must hold:

1. the item node exists, the trap's enabled byte `+0x158` is set (the debug flag `+0x15C` can
   override it) and the taken marker `+0x1D4` is 0 (not taken, not waiting to reload);
   **visibility is not tested**;
2. vertical: `|trapY - princeY - yOffset| <= 0.15` (`flt_7483AC`, `yOffset` = 0.95 for `Table`,
   `argv8` for custom, else 0);
3. horizontal (y ignored): distance from the Prince to the trap < **1.5** (`flt_7C2CF0`);
4. facing: take the point 0.5 in front of the Prince, make the unit vector from there to the
   trap (y ignored); its dot product with the Prince's forward vector must be **> 0.8**.

The action button scan (`0x44A179`) walks the level's trap list and takes the **first** trap
whose `CanUse` is true, not the nearest.

### 2.4 Taking it [C]

1. `StStartGrasp` (`0x4D35BE`): checks the trap is a Pickup (else `"Not a pickup item!"`).
   It builds a goal transform from the item's `Handle` offset and walks the Prince there
   (`sub_49F243`, with `PivotRight_G`/`PivotLeft_G`/`Walk_L`/`Running_L` and the reach clip as
   the arrival animation).
2. `StIdleGrasp` waits for the clip to finish, then `StStartManipulation` (`0x4D392C`):
   Take (`0x47FBB4`) hands the item node to the Prince's attachment and hides the world copy,
   then the use clip plays.
3. `StStartFinishTrap` (`0x4D3A73`): the effect is **applied here** (`0x47FC8C`), then the finish
   clip plays (`"Potion Drinking done"`).
4. After apply:
   * the sound for the type plays at the pickup (`POT*.sfx`, table `0x800E28`; items have none);
   * the type's residual NIF is attached to the Prince (`0x800EF8`). Camouflage uses a different
     attach (parent = world, `0x7F39BC`);
   * the trap is disabled (`+0x104(0)`);
   * if it is not a reload pickup it is hidden and marked `-1`: **gone for good, never respawns**;
   * then, for non-numeric items, `argv3` is run as a script message (`0x454DE5`).

No text is shown on screen for any pickup (`GameText.txt` has none).

### 2.5 Effects (apply, `0x47FC8C`) [C]

The code string `X:arg` is split at `:` (58). `now` is the world clock `dbl_7F2CC0`; a duration
`> 0` becomes `now + d`.

| Code | Effect |
|---|---|
| `heal:n` | `health += n`, clamped to max health (`0x4C763C`). `heal:all` and `Fullheal:` heal by max health. |
| `ExtraLife` | `delta = HP(bottles+1, capped at 10) - maxHealth`, scaled `delta * (4 - difficulty) / 3` (integer division, `dword_7EAC3C`), added to max health, **capped at 120**; then full heal (`0x4C7673`). |
| `Poison:n` | `n < 0` heals. Otherwise health `-= n` (floor 0) when the Prince is damageable; at 0 and not already dying the death sequence starts (`sub_51F838(1)`); a red tint flag is set. |
| `Strength:n[,t]` | **adds** `n` to the strength bonus (`+0x48`, stacks); expiry `now + t` for `t >= 0`, none for no `t` (`0x4C1FE7`). |
| `Shield:t` | arrow immunity: expiry at `+0x68`; `t < 0` is forever. Tested only by the arrow hit routine (`0x4A4E26` via `0x4C1FB3`), which skips the hit entirely. |
| `Jump:rate,t` | jump scale and expiry (`0x4CA4F7`), `t < 0` forever. |
| `Fall:t` | feather fall expiry (`0x4CA525`). |
| `Camo:t` | spawns the camouflage mesh and `PrinceAlpha` fade (fades over the last third); guards see him only inside 1.5 (guard-awareness.md). Only if not already camouflaged. |
| `Guard:t` | **zombie disguise**: if none is active, sets the Prince's `zombie` body (`"No ZOMBIE defined!"` when the level has none) and flag `+0xDC`; guards never see him (guard-awareness.md step 4). |
| other | sent as a script message (key, sword, arrows: `prince,weapon:+sword`, `+arrow,type,count`, `+bow`, `+double`, `+staff`). |

### 2.6 Respawn and reload [C]

* Normal pickups never respawn in a level.
* A **Reload** pickup is created enabled with `+0x1D8 = 1` and reset (`0x4803B9`) to
  `taken = 1`, hidden and disabled. Each tick (`0x47F9B1`) it re-appears when
  `arrows(type) < minimum`: `minimum = 0` (most quivers) never re-arms, `minimum = 1` re-arms
  when the Prince is out of that arrow type. Taking it again gives `count` more.
* `0x4803B9` is also the level (re)start reset.

### 2.7 Guard prizes [C]

A Pickup parented to an EnemyStart node is that guard's **prize** (`0x4A89F9`: "Illegal prize
trap on %s", "Can't have two prizes"). The EnemyStart creates it itself (builder `+0x4`), resets
it (`0x47F98F`: enabled and visible cleared) and it is dropped when the guard dies. In the data
26 of 127 Pickups are prizes, e.g. `Droppedhealth` (parent `P4G2`), `Gatehouse_PG1_Potion`,
`healthbottle01`, `BowPickup`, `CliffsG_Arrow_PICK`.

---

## 3. Differences: binary vs our code (by what the player notices)

Our code: `src/main.cpp` `pickup_seq`, `pickup_apply`, `potion_state`, the `E` handler (~9296)
and `pickup_live` (~6985). Doors: `scene_trap::handle` (~1885) plus the generic skill player.

### High

1. **Guard prizes are lying in the world from the start.** We spawn every Pickup at its authored
   spot (`main.cpp` ~3037) including the 26 that belong to a guard and only exist once he dies
   (`Droppedhealth` in prison4, the stairs potions in Palace2, the wizard-tower bottles...).
   Needs: hidden and disabled until the parent EnemyStart's guard dies, then enabled at his
   position.
2. **The pickup test is much looser.** We take any pickup within 1.6 in xz and +-2.0 in y, in
   any facing, nearest first. The binary needs |dy| <= 0.15, xz < 1.5 and the item within 0.8
   dot of straight ahead (2.3). Players currently collect items from behind, from the floor
   below, or through walls.
3. **Visible is wrongly required.** `pickup_live` needs visible and enabled. The binary needs only
   enabled. `P4_Sword` (`",true,false,false"`: enabled, hidden) is a normal take in the original;
   scripts toggle `Enable`, not `Visible`, for the gate.
4. **Shield potion blocks everything.** `prince_damage` and `prince_fight_damage` ignore all
   damage while it lasts (swords, falls, spikes). The binary tests it only in the arrow hit
   routine (`0x4A4E26`): arrows do nothing, other damage is normal.
5. **Guard potion is modelled as "always block".** In the binary it makes the Prince a zombie
   for the guards: they never see him (no pursuit, no attack), and a zombie body is attached.
   Ours (`main.cpp` ~6025) auto-blocks one hit type and leaves AI unchanged.
6. **Walking and clips.** The binary walks the Prince to the item's `Handle` offset with the
   reach clip, the item attaches to his hand at the manipulation step and the effect applies
   after the use clip but before the finish clip. We turn on the spot and queue clips. Missing
   clips by kind: `GraspPotion_M` (potions and items), the full four-clip set for
   `Weapon` (we use two), the `Floor` set (`InspectSmallFromGround_G`/`InspectSmallFinish_G`),
   and the `Table` set (`BendToGrabOff1m_G`, `GraspOff1m_M`, `InspectOff1m_G`,
   `InspectOff1mFinish_G`, with the 0.95 y offset). Keys, the flute, the jewel and the
   bracelet are `Table`/`Floor` items and currently use `BendDownToGrabObject_G` only.

### Medium

7. **Sound.** The binary plays `POT<Colour>.sfx` per potion type at apply (table in 2.2) and
   nothing for items. We play `PRNpickup`, which does not exist in `Audio/sfx`, so pickups are
   silent.
8. **Reload quivers (done: they re-arm below their minimum).** We treat `Reload` as a one-off. The binary re-arms when
   `arrows(type) < minimum` (2.6). Only `minimum >= 1` quivers (`TKPart3a/ArrowReload`,
   `TKHiddenPoolRoom/arrowstash02`) ever return; the `0,N,0` ones are single-use there too.
   Also we add `count` to one arrow counter regardless of `type` (`pickup_apply` ~7062).
9. **Potion visuals.** The binary swaps in one of 12 bottle models by type (`Potion_*.nif`) and
   shows `Potion<Name>.NIF` as an idle sparkle on the bottle. We show one bottle (the
   `TRAP_Potion` mesh) and play `Potion<Name>.NIF` as a burst at drink. The drink plays only the
   Residual NIF.
10. **Strength stacks** in the binary (`bonus += n`); ours replaces it
    (`g_potions.strength = v0`). A strength potion without a time is permanent; with a time the
    expiry is `now + t`.
11. **Extra life** is scaled by difficulty and capped at 120 / 10 bottles in the binary, and
    refills health. Ours adds one bottle (12) and refills; the difficulty scale is not applied.
    (Mapping of `dword_7EAC3C` to our `prefs().difficulty` is not traced.)
12. **Poison** heals for a negative amount and cannot kill if the Prince is already dying; ours
    always calls `prince_damage`.

### Low

13. **Door messages** that repeat the current state are dropped (prefix test); ours restarts the
    clip. Door save/load replays the last message; ours has no door persistence across a
    save.
14. **Door collision is a MotivateForce.** Ours moves a bounding box (`m_base`); the original's
    collision follows the animated skeleton, and a Door whose wrap fails has none at all.
    `visible:`/`collide:` on a Door are forwarded to the Force; check that `collide:F` on a
    closed door lets the Prince pass in ours.
15. **Camouflage fade** (the `PrinceAlpha` fade over the last third) and the guard-visibility
    rule are only partly present; see guard-awareness.md.
16. Camera and key behaviours (`prince,Item:Key01,on/off`) are already in
    [camera-logic.md](camera-logic.md) section 5.5; the Action lock is data-driven and
    works through the existing state blocks. No key inventory exists in the binary, so none
    should be added.

### Not traced

* Exact pickup-walk goal transform (`sub_49F243` arguments) and the `GraspPotion_M` event
  timing inside `StIdleGrasp`.
* `HP(n)` table `dword_7E9FCC` used by Extra Life and the difficulty variable `dword_7EAC3C`.
* How an EnemyStart enables its prize on death (the death path calls Pickup vtable `+0xCC`).
* Door reset and the exact meaning of the Door's `+0x148` string (taken as the state).
