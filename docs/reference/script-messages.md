# Script messages: the Prince and the unhandled audit entries

Sources: `PrinceMessage` table builder `0x4845A7`, dispatcher `0x4849EF`, handlers `0x484AB3-0x485907`.
String pointers that IDA shows as `off_...` are three-letter strings read as code: `0x7C3300` = "Add",
`0x7C32FC` = "Use", `0x7C21DC` = "Die", `0x6C6C61` = "all", `0x657355` = "Use".

## Dispatch (`0x4849EF`)

A message is split on the first `:` (char 58): verb, then argument string. The verb is looked up
in a name-to-handler map (`this+88`, lookup `0x48599E`, names compared case-insensitively by
the string map). The argument string is split on `,` (char 44) with `0x50BD02`; booleans are read
by `0x50BBB6`. An unknown verb logs "Unknown Prince Message action!" and is dropped.
Handlers get `(args, sender)`. `dword_7FFB74` is the Prince.

## Verbs the Prince accepts

| verb | handler | semantics |
|---|---|---|
| `Water:b` | `0x484AB3` | Obsolete. `off` (false) clears Prince `+0x1D8` (byte) and `+0x1DC` (float) to 0 (`0x4D0D81`). `on` (true) only logs "Use water traps now, this has gone away!". The audit entry `Prince,Water:off,-999` (cliffsF drown area) therefore resets that water state. Extra args are ignored. |
| `Add`, `Use` | `nullsub_141/142` | accepted, do nothing |
| `Heal:n` | `0x484B0A` | `0x4C763C(prince, int(arg0))`: add `n` health |
| `Hurt:n[,anim]` | `0x484B58` | `0x4C74E3(prince, int(arg0), sender, arg1, 0)`: damage `n`; arg1 names the death skill if it kills |
| `Kill:anim[,b]` and `Die:` | `0x484BD0` | same call with damage `0x7FFFFFFF`, arg0 = death skill, arg1 = bool (the last param of `0x4C74E3`) |
| `Play:skill` | `0x484C4F` | `0x4C769B(prince, skill)`: play a skill |
| `Snooze:t[,lock,b]` | `0x484F77` | no input for `t` s; `t<0` means forever (FLT_MAX). If `t>0` and arg1 is true or unparsable, `0x4095B8(dword_7E9344,1)` (camera lock). arg2 false/missing sets flag `0x130`; it is forced to 0 when the Prince is in state 5. Stored by `0x4C7710` (`+0x150` time, `+0x14C`/`+0x150` mask) |
| `Attach:actor` | `0x4850AD` | puts the named (or sender) actor in the Prince's right hand: node "Rthumb1", flags 0/1/0 |
| `Remove:actor` | `0x4851C9` | detaches it again |
| `Weapon:[+/-]name[,type,count]` | `0x485247` | see below |
| `Teleport:...`, `Goto:...` | `0x485407` (`Goto` forwards) | move to a named place |
| `SkillAt:skill,node[,mode[,x]]` | `0x484C67` | skill at a placeholder; mode Dead/Standing/Falling/Swimming = 1/2/3/4 |
| `Draw:b` | `0x485601` | true: `0x4C877C(0)` + `0x4C8739` draw weapon; false: `0x4C8754` sheathe |
| `Item:name,b[,L/R]` | `0x4856C6` | gives item `name` (master item table `dword_7F3CA8`, else `0x4CC10E`); b = bool (a non-bool second arg that is a name selects the other path); arg2 `L` = left hand (default right). "Can't find item %s" if unknown |
| `PotionClear` | `0x4858CC` | no args. On the Prince's potion block (`+0x1D0`): `+0xB8` = 0 (effect id), `+0xC0` = 0.0 and `+0xC8` = 0.0 (timers). Ends any potion effect (speed, health bottle) at once. cliffsF `CliffsG_potionclearer` sends `prince,potionclear` |
| `EffectAt:...` | `0x485907` | spawns an effect at a node (`0x4E43C9`) |

### `Weapon` (`0x485247`)

The first char `+` or none = add, `-` = remove (`0x4C8896(prince, mask, add)` edits the weapon-mask word
`+0x44` of the weapon object `+0x1C8`; adding the first weapon also equips it).

| name | mask |
|---|---|
| `sword` | 1 |
| `staff` | 2 |
| `double` | 4 (the double blades, the Palace4 shredder and cliffsDinner) |
| `bow` | 8 (string at `0x7C2304`, read as "bow") |
| `all` | 0xF |
| `arrow,type,count` | `0x4C8877(prince, type, count)`. Count defaults to 1; `-` negates it |

After any weapon verb `0x4C174F` and `0x452138` refresh the weapon HUD. An unknown name does nothing.

## The audit entries

| message | verdict |
|---|---|
| `prince,potionclear` | implement (above) |
| `Prince,Water:off` | implement as a reset of the water override (above) |
| `Prince,weapon:+double` | add mask 4 (`Palace4/ShredderControls.k2a:4357`, `cliffs/cliffsDinner.k2a:14175`) |
| `bat_trap_area,off` | **a no-op in the retail game too.** It is sent by the Area `Bat_trap_Area` (cliffsF) to itself: argv1 `Bat_trap,swarm;bat_trap_area,off;`. The bare verb `off` is read only by `ArrowCharger` (`0x4653A8`, vtable `0x746818`: `on`/`off` set `+0x170`). `Area` has no handler for it. Base traps (`0x4552B7`) only take `reset`, `state:`, `enable:`, `visible:`, `collide:` (two `:` fields). So the Area never turns itself off and the bats swarm each time. The trap exists and is found; the message is just unhandled. Ignore it quietly (or map it to `enable:F` only if the bats are seen to repeat; retail did not) |
| `Rat_trap,scurry` | `Rat_trap*` are `Fixture` traps (rat actor, argv1 `rat`); a Fixture answers a message by playing the skill of that name: `Actors/rat/Skills/Scurry.skl` exists. Sent by the Area `Rat_trap_Area` (cliffsDinner, with `;`) and `Scurry` areas. The other copy (line 1972) uses `:` between items: `Rat_trap,scurry:Rat_trap01,scurry:...` is one message `scurry:Rat_trap01,scurry:...` to `Rat_trap`. It is not a skill name and not a valid `state:` message, so retail ignores it too. The audit line is that data typo. Implement: Fixture skill `Scurry`; do not split on `:` |
| `MsgShrink,nextroom` | `MsgShrink` is a `Messenger` in `Ruins/wizardtower.k2a:10368`, sent from Area `Growbigagain` in `bigwizard.k2a:12410` (same level). Messenger grammar: `state:event:{script}event:{script}`. State `normal`: `shrink` runs the shrink cutscene and sets `state:tiny`; state `tiny`: `expire` and `nextroom` both unshrink the Prince (`SkillAt:Lvl12Unshrink,NextRoomPlaceHolder`, camera `@NextRoomCam`) and set `state:normal`. If the Prince is not shrunk the state is `normal` and `nextroom` matches no block, so nothing happens: that is the retail result. Check our messenger parses the several events after one state and keeps the state across rooms |
| `Wierdlair_Button_MSG,Killed` | the `DeathMsg` (argv4) of EnemyStart `Wierdlair_W2_START` (cliffsI:11212). The target is a `Messenger`: `WierdOut:Killed:{Wierdlair_Button_ACTION01,Enable:T;...;Wierdlair_Button_MSG,state:WeirdDead}`. It is only valid in state `WierdOut`, set when the player presses the second button. Our `DeathMsg` skill (argv4) must be sent as a message to the named trap when the guard dies (`0x4C6391`); the audit shows it is not. Note the guard's InhibitDeath is `false` here, so the generic death clip plays as well |

## What to implement in `src/`

1. `PrinceMessage` verbs missing in `receive`: `potionclear`, `water:` (off = clear water override; on = ignore), `heal:n`, `play:`, `draw:b`, `attach/remove`, `effectat`, `die:` (= `kill:`), `add`/`use` (accept, no-op).
2. `weapon()`: add `staff` (2) and `double` (4), `all` = 0xF, optional sign; arrow count defaults to 1 and `-` negates.
3. EnemyStart `DeathMsg`: send the argv4 text through the script world when the guard dies, so `Wierdlair_Button_MSG,Killed` reaches the Messenger.
4. Messenger: events are `state:event:{script}event:{script}...`; an event that matches no block is a silent no-op (so `MsgShrink,nextroom` outside state `tiny` is correct).
5. Fixture: a message equal to a skill name plays that skill (`scurry`); a message with an embedded `:` and `,` is not a skill.
6. Silence the audit for `bat_trap_area,off` and the `:`-joined Rat_trap string; retail drops both. Optionally warn about `Area` + bare `off`.
