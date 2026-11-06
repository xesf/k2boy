# Level transitions: how the game moves from one module to the next

Decoded from the offline IDA export (`.ida-export/pop3d.c`, `pop3d.lst`, `pop3d.asm`).
ida-mcp was down, so nothing here was renamed in the IDB; names below are proposed names for
the `sub_XXXXXX` entries. Evidence tags: **[C]** read in the pseudocode and the data tables,
**[I]** inferred from several [C] facts, **[U]** not traced.

Short version: a level ends when a trap or script sends the message `game,exit` (optionally
`game,exit:Win` / `game,exit:Lose`). The running `gameModule` turns that into either a cutscene
(a `NIS` module message carrying a movie number) or, for levels that chain directly, the next
level. A cutscene's end looks up the next level in a fixed table. A level is always entered
through the `title` module's loading screen. The Prince is **serialised into a global buffer when
a level is left and restored when the next one is entered**, so health, weapons and arrows carry
over. Regions inside one `.lvl` are not streamed on a trigger; all of them load together.

---

## 1. The module manager

### 1.1 State and message record [C]

All of this lives in `.data`. The names are proposed.

| address | proposed name | meaning |
|---|---|---|
| `0x7BE588` | `g_cur_msg` | the **current** module: `{u16 module_id, u16 arg0, arg1, arg2}`, 8 bytes |
| `0x7BE590` | `g_pending_msg` | the **pending** transition. `id == 0xFFFF` means none |
| `0x7BE598` | `g_prev_msg` | the module we came from (the old current) |
| `0x7BE5A0` | `g_last_game_msg` | the last *game* module and its args (set when a game module is entered, `FirstFrameInit`) |
| `0x7BE5A8` / `0x7BE5B0` | `g_menu_return_*` | for the in-game menu: who to resume to |
| `0x7BE5B8` | `g_resume_msg` | non-`-1` while resuming from the menu |
| `0x7D3638` | `g_dest_msg` | the **destination queue** of one: the module the loading screen (`title`) should send us to |
| `0x7F2A10` | `g_modules[]` | module table, sorted by name (`strcmpi`) |
| `0x7F29A8` | `g_module_count` | 20 |
| `0x7F2BC0` | history ring | 16-entry ring of past messages (`sub_434C83` push, `sub_434CD8` pop, `sub_434ADB(n)` = "go back n") |

The four message records at `0x7BE588/90/98/A0` are exactly what the savegame `module` section
stores (4 x 4 u16). `Quick Save.pop` decodes as current `{12,-1,-1,-1}`, pending `{-1,...}`,
previous `{17,1,-1,-1}`, last game `{12,-1,-1,-1}`: `prison` entered from `title` with arg 1
(loading screen). That also proves the id numbering below.

`Game__PostModuleMessage(module_obj, a0, a1, a2)` (`0x420B47`) calls `Message__Build`
(`0x434C3D`): `word0 = Module__GetId(obj)` (`obj+8`), `word1..3 = a0,a1,a2`; a null obj gives
`word0 = 0xFFFF`. It writes into `g_pending_msg`. Nothing is dispatched there; the switch happens
at the next frame start (section 1.3).

### 1.2 Module ids [C]

`module::module(name, kind)` (`sub_43475C`) inserts itself sorted by `strcmpi` and renumbers
the tail, so the id is the position in the sorted list. `this[3]` is the kind (the "type" the
code compares: 1 game, 2 title, 3 menu, 4 NIS).

| id | name | kind | .lvl (game modules) | ctor |
|---:|---|---|---|---|
| 0 | `burning` | game | DirigFinale/DirigFinale.lvl | `0x5261EF` |
| 1 | `cistern` | game | Cistern/cistern.lvl | `0x526253` |
| 2 | `cliffs` | game | cliffs/cliffs.lvl | `0x52627C` |
| 3 | `docks` | game | cityanddocks/cityanddocks.lvl | `0x5262A5` |
| 4 | `finale` | game (own vtable `sFinaleModuleClass`) | finale/finale.lvl | `0x5262CE` |
| 5 | `lower` | game | dirig1a/dirig1a.lvl | `0x526301` |
| 6 | `lower2` | game | dirig1b/dirig1b.lvl | |
| 7 | `menu` | menu | | `0x42E856` |
| 8 | `NIS` | NIS | | `0x526600` |
| 9 | `palace2` | game | palace2/palace2.lvl | |
| 10 | `palace3` | game | palace3/palace3.lvl | |
| 11 | `palace4` | game | palace4/palace4.lvl | `0x526399` |
| 12 | `prison` | game | prisonfix/prisonfix.lvl | `0x5263C2` |
| 13 | `rooftops` | game | roof1/roof1.lvl | |
| 14 | `ruins1` | game | ruins/ruins.lvl | `0x526411` |
| 15 | `stronghold1` | game | moontemple/moontemple.lvl | `0x52643A` |
| 16 | `temple` | game | solar1/solar1.lvl | `0x526463` |
| 17 | `title` | title | | `0x526EC7` |
| 18 | `tower` | game | ivorytwr/ivorytwr.lvl | |
| 19 | `upper` | game | dirig2/dirig2.lvl | `0x5264B2` |

`src/engine/module_table.cpp` already has this order, verified against the savegame
(`prison` = 12, `title` = 17).

### 1.3 The frame-start switch: `Game__FirstFrameInit` (`0x43481A`) [C]

Called once per pass of `Game__EventLoop`, before `Game__Tick`. Pseudocode in prose:

1. If `g_pending_msg.id == 0xFFFF`, do nothing.
2. Debug log `"GOTO:%s FROM %s"` (only when `flags & 0x40000`; the original's log call is a no-op
   stub in retail).
3. `byte_7F2C4C = 1` (a "transitioning" flag, cleared at the end).
4. If the pending module is a **game** module (kind 1): copy pending to `g_last_game_msg`
   (`0x7BE5A0`) and `0x7BE5B0`.
5. If `g_resume_msg == -1` (a normal transition):
   * call the new module's **slot 5** (`+20`): for a game module that is `Game__RequestLevelLoad`
     (`0x42093A`), see 3.2;
   * **if the old module is a game module and the new one is not the menu (kind 3), call
     `sub_4C9804` = capture the Prince** (see section 4).

   Otherwise (resuming from the menu) call `g_resume_msg`'s module slot 1 instead.
6. Call the old module's **slot 1** (`+4`), "leave". For a game module this tears the world down
   (`sub_4208EF`, 3.3).
7. Shuffle messages: `g_prev = g_cur`, `g_cur = g_pending`, push the pending onto the history
   ring, `g_pending = {-1}`.
8. Call the new module's **slot 0**, "enter".
9. If the new module is a game module: if a savegame is being loaded (`dword_7F38B4`) run the
   post-load fixups; otherwise, **unless the previous module was the menu (and `dword_7F3840` is
   clear), call `sub_4C98AE` = restore the Prince**. Then drain input messages (`sub_45443E`) and
   clear `byte_7F2C4C`, `dword_7F3840`.

### 1.4 Module vtable slots [C]

Shared by all five classes (`module::vftable` at `0x744D44`; slots 4-5 have no-op bases).

| slot | offset | role | evidence |
|---:|---:|---|---|
| 0 | `+0` | enter | called last in `FirstFrameInit` |
| 1 | `+4` | leave (old module) | called before the swap; default `sub_434806` re-sends the module's current message to slot 2 |
| 2 | `+8` | message handler `(a0,a1,a2)` | `sub_434BEB` calls it; Menu's `sub_42FDB4` switches on `a0` |
| 3 | `+12` | window event `(self, hwnd, msg, wparam, lparam, &handled)` | `sub_434A9A` forwards events only when nothing is pending |
| 4 | `+16` | per-frame tick | `sub_434A73` runs it when a module is current and nothing is pending |
| 5 | `+20` | pre-load (new module, before the old one is torn down) | only game modules implement it |

`sub_434A9A(0, 1294, 0, 0, &flag)` is how window-style commands reach the current module; the
message number **1294 (`0x50E`) is "this level is finished"**.

---

## 2. What ends a level

### 2.1 The `game` script target [C]

`GameMessage` (the script target named `game`, vtable `off_7479D4`, handler `sub_473D5D`)
splits its argument on `:` and handles, case-insensitively:

| command | effect |
|---|---|
| `Exit[:Win]` | `dword_8A0038 = (arg == "win")`, then `sub_434A9A(0, 1294, ...)` = end of level |
| `Level:Room,<name>,<true|false>` | `sub_446F8B`: sets the room's `+64` byte (`= !flag`) on `LevelGroup` (`dword_7F3CA8`). It only shows or hides a room |
| `Clip:near,<f>` / `Clip:far,<f>` | clip planes |
| `SkyDome:<name>:on|off` | the sky dome (`CityAndDocks/Section3a`) |

`dword_8A0038` is the **win flag**. It is read only by the NIS module (2.3).

### 2.2 Who sends `game,exit` [C]

From `K2_TRAP_ARGV` in `data/Geometry/Rooms/*`:

| level | trap / file | message |
|---|---|---|
| prison | `prison6.K2A:11699` | `prince,snooze:3; delay,1,P6_room4_exitdoor,move...; delay,2,game,exit; camera,tripod,(@exitcam)` |
| tower | `TKFinish.k2a:6174` | `Game,Exit` (area trap) |
| cistern | `B_PalB1.K2A:13601` | `Game,Exit` |
| palace2 | `B_palG.k2a:27277` | `game,exit` |
| palace3 | `GardenTrans.k2a:5631` | `Prince,Snooze:6; Camera,Tripod,...; delay,6,Game,Exit` |
| palace4 | `LosBlock2.k2a:8510` | `Game,Exit` (area trap `Losblock2_GameExit_AREA`) |
| rooftops | `LastHall.K2A:5411` | `Game,Exit` |
| docks | `Section5.K2a:30001` | `Game,Exit` |
| lower (dirig1a) | `flr3_intwalls.k2a:1036`, `flr3_EXITintwalls.K2A:4049` | `game,exit` |
| lower2 (dirig1b) | `Dirig1_6.K2A:26088` | `game,exit` |
| upper (dirig2) | `Captower.k2a:16563` | `game,exit` |
| burning | `DirigFinale/CapTower.k2a:16930` | `game,exit` (ARGV4 of an `EnemyStart`: on that enemy's death) |
| ruins | `weirdarena.k2a:14602` | `delay,.75,game,exit` |
| cliffs | `cliffsH.k2a:5292` | `cliffsbridge,closegate; ...; delay,4,game,exit` |
| temple | `S_Skylightroom.k2a:10583` | `game,exit` |
| stronghold | `TilesOfDeath.k2a:12024` | `Game,Exit` |
| finale | `Finale.K2a:37816` | `EnemyStart GoldPawFinal`, on death: `game,exit:Win` |
| finale | `Finale.K2a:38186` | `delay,150,game,exit:Lose;endingtimer,enable:F` |

So a level ends in one of three ways: an area trap the Prince walks into, a scripted sequence's
last `delay,n,game,exit`, or the death of a boss. The only `:Win` / `:Lose` pair is in `finale`.

### 2.3 What the exit does: `gameModule` event handler `sub_420ABE` (`0x420ABE`) [C]

`gameModule` fields set by its ctor `sub_526213(this, name, lvl, a4, next, msg, flags)`:
`+16 a4` (a per-level index, 0..17 with 7 skipped; never read on the paths traced), `+20 flags`
(always -1: "which engine services to create/destroy", see 3.3), `+24 next` (the module that
follows), `+28 msg` (pointer to 3 u16 args, or 0), `+32 lvl` path.

On message 1294:

```
next = this->next;  args = this->msg ? this->msg : {0xFFFF,0xFFFF,0xFFFF};
if (next->kind == game) {                   // chained levels
    g_dest_msg = Message(next, args[0])     // Message__Build(&7D3638, next, args[0])
    PostModuleMessage(title, 1, -1, -1)     // loading screen
} else {                                    // next is NIS
    PostModuleMessage(next, args[0], args[1], args[2])
}
```

`sFinaleModuleClass` replaces slot 3 (`sub_5264D6`): on 1294 it posts `NIS(11, -1, -1)`
directly (the ending movie) and ignores `next`.

### 2.4 Debug commands [I]

The same 1294 is also posted by a handler in `sub_5059EE` (`0x5059EE`, around `pop3d.c:352803`)
that looks like a developer-menu command id (it also posts `menu(7)` = in-game menu). I did not
trace whether it is reachable in retail.

---

## 3. The level graph

### 3.1 Per-level successor table (gameModule ctors) [C]

| level (id) | `next` (+24) | NIS msg (+28) | comment |
|---|---|---|---|
| prison (12) | NIS | **1** | |
| tower (18) | cistern | none | direct, through the loading screen |
| cistern (1) | NIS | **2** | |
| palace2 (9) | palace3 | none | |
| palace3 (10) | palace4 | none | |
| palace4 (11) | NIS | **3** | |
| rooftops (13) | docks | none | |
| docks (3) | NIS | **4** | |
| lower (5) | lower2 | none | |
| lower2 (6) | upper | none | |
| upper (19) | NIS | **5** | |
| burning (0) | NIS | **6** | |
| ruins1 (14) | NIS | **7** | |
| cliffs (2) | NIS | **8** | |
| temple (16) | NIS | **9** | |
| stronghold1 (15) | NIS | **10** | |
| finale (4) | none (own vtable) | **11** | win/lose decided by `dword_8A0038` |

### 3.2 NIS module (`sNISModuleClass`, vtable `0x74D178`) [C]

Enter `sub_5266B4`, leave `sub_526920`, tick `sub_526A0B`, event `sub_526ADE`.

* `a0` of the message (`HIWORD(g_cur_msg)`) is the movie index. `off_74D12C[a0]`:

  | NIS | movie | | NIS | movie |
  |---:|---|---|---:|---|
  | 0 | `cs01` | | 7 | `cs09` |
  | 1 | `cs02` | | 8 | `cs08` |
  | 2 | `cs03` | | 9 | `cs10` |
  | 3 | `cs04` | | 10 | `cs11` |
  | 4 | `cs05` | | 11 | `cs12` if `dword_8A0038 == 0`, else `cs13` (`off_74D160[win]`) |
  | 5 | `cs06` | | 12 | `Broder` (publisher logo) |
  | 6 | `cs07` | | | |

  The file is `movies\<name>.mov` (`sub_51AC96(buf, 20, "%s%s", name, ".mov")`). Note 7 and 8
  are swapped: ruins plays `cs09` and cliffs plays `cs08`.
* For `a0 != 12`, `sub_432504(a0 + 1)` records progress in `NIS.sav` (raises `dword_7EC340`).
  With the win flag set and `a0 == 11` the value is 12 (`cs13` unlocked).
* If `byte_7D3550 == 0` (movies disabled by the `n` command-line letter, or QuickTime failed
  to init) enter returns straight into `sub_526A23` (no movie).
* The movie ends on the tick (`sub_52B757`) or on any key / mouse press (`sub_526ADE`).
  Both call `sub_526A23` (**NIS__Finished**, `0x526A23`):

  ```
  if (pending is non-empty and is not "menu(255)")  go back 1 in the history ring   // viewer from the menu
  else if (nis_next[a0])  {                                                         // table below
      if (g_dest_msg is empty) g_dest_msg = Message(nis_next[a0], 255);
      Post(title, 1);                                                               // loading screen
  } else if (a0 != 12 || byte_7D3630)  Post(menu, 4);                              // end of game, or after the logo
  else                                  Post(NIS, 0);                              // logo then intro (no-menu mode)
  ```

* `nis_next[]` is `dword_8A0004[]`, filled by `sub_526628` (`0x526628`) [C]:

  | NIS a0 | next level |
  |---:|---|
  | 0 | prison |
  | 1 | tower |
  | 2 | palace2 |
  | 3 | rooftops |
  | 4 | lower |
  | 5 | burning |
  | 6 | ruins1 |
  | 7 | cliffs |
  | 8 | temple |
  | 9 | stronghold1 |
  | 10 | finale |
  | 11, 12 | none |

  `dword_8A0030` and `dword_8A0034` (slots 11, 12) are zeroed in the same function.

### 3.3 Title module and the loading screen [C]

`sTitleModuleClass` (vtable `0x74D1BC`, ctor `0x526EC7`). Message arg0 `0` is the "End Demo"
screen, `1` is the loading screen. Enter (`sub_526EEA`) draws the "Loading" bitmap and, for
arg 1, immediately calls `sub_5270FE`:

```
if (g_pending is empty && g_dest_msg non-empty) g_pending = g_dest_msg;   // sub_434B9B
g_dest_msg = {-1}
```

so on the **next frame** `FirstFrameInit` switches to the game module in `g_dest_msg`, with the
loading bitmap still on screen while the (blocking) level load runs. A key or mouse press on
the End Demo screen calls the same routine.

Game module enter `sub_4208C1`: unless the previous module was the menu, `sub_42C449(flags)`
creates the engine services (`"about to create the world"`). Flag `0x40` runs `sub_4130A8` (create
3DWorld and build the `LevelGroup` from `dword_7F33B8` via `sub_44750A`). Game module leave
`sub_4208EF` calls `sub_42C562(flags = -1)` which, for `0x40`, runs `sub_4131B1` ("Destroying
world"), deletes `g_prince` (`dword_7FFB74`), `dword_7FFB5C`, the trap list (`sub_4A5E99`), etc.,
**unless the pending module is the menu** (that is how the in-game menu keeps the level alive).

`Game__RequestLevelLoad` (slot 5, `0x42093A`) builds a `LevelLoadRequest` (`dword_7F33B8`) from the
command-line level string (`config+120`, one-shot, used by `-l` style flags), else the module's
own `.lvl` (`this+32`), else `fightfloor.lvl`.

### 3.4 The sequence [C]

```
WinMain: Post(NIS, 12)                       Broder logo
  NIS 12 ends -> Post(menu, 4)               main menu       (byte_7D3630 = 1, retail)
Menu New Game (sub_43F636, from sub_42ED44): reset save lists, Post(NIS, 0), dword_7F3840 = 1
  NIS 0 (cs01 intro)  -> Title(1) -> prison
prison  -- game,exit -> NIS 1 (cs02)  -> Title(1) -> tower
tower   -- game,exit --------------------------> Title(1) -> cistern
cistern -- game,exit -> NIS 2 (cs03)  -> Title(1) -> palace2
palace2 -- game,exit --------------------------> Title(1) -> palace3
palace3 -- game,exit --------------------------> Title(1) -> palace4
palace4 -- game,exit -> NIS 3 (cs04)  -> Title(1) -> rooftops
rooftops-- game,exit --------------------------> Title(1) -> docks
docks   -- game,exit -> NIS 4 (cs05)  -> Title(1) -> lower
lower   -- game,exit --------------------------> Title(1) -> lower2
lower2  -- game,exit --------------------------> Title(1) -> upper
upper   -- game,exit -> NIS 5 (cs06)  -> Title(1) -> burning
burning -- game,exit -> NIS 6 (cs07)  -> Title(1) -> ruins1
ruins1  -- game,exit -> NIS 7 (cs09)  -> Title(1) -> cliffs
cliffs  -- game,exit -> NIS 8 (cs08)  -> Title(1) -> temple
temple  -- game,exit -> NIS 9 (cs10)  -> Title(1) -> stronghold1
stronghold1 -- game,exit -> NIS 10 (cs11) -> Title(1) -> finale
finale  -- game,exit:Win  -> NIS 11 (cs13) -> menu(4)
finale  -- game,exit:Lose -> NIS 11 (cs12) -> menu(4)
```

The prison-to-finale order matches the order already hard-coded at `src/main.cpp:8337`.

Other entries: `-m` clears `byte_7D3630` and presets `g_dest_msg` to `prison` (or the module
whose `.lvl` equals `config+120`, via `sub_526519`/`sub_42C403`), so `Broder -> NIS 0 -> Title(1)
-> that level` replaces the menu. Menu command `0x9CA5` calls the same `sub_42C403` then
`Post(title, 1)`. The menu's movie viewer posts `NIS(min(n-1, 11))`; a viewed movie ends with
"go back 1" (the history ring), returning to the menu.

---

## 4. What the Prince carries across levels

### 4.1 The mechanism [C]

* **Capture** `sub_4C9804` (proposed `Prince__SnapshotForNextLevel`, `0x4C9804`): called in
  `FirstFrameInit` step 5, i.e. when a game module is left for anything but the menu (so on the
  way to a cutscene or to the loading screen). It calls `PrinceObject` vtable slot 45
  (`+180` = `sub_4C948A`, the **save** virtual) into a buffer with `byte_7CD424 = 0`, and keeps the
  bytes in `dword_7FFBE4`, length `dword_7FFBA0`.
* **Restore** `sub_4C98AE` (`Prince__RestoreFromSnapshot`, `0x4C98AE`): called in step 9 once
  the new level has spawned its own fresh Prince. Calls vtable slot 46 (`+184` = `sub_4C95BB`, the
  **load** virtual). If `dword_7F3840` (set by New Game) it instead frees the buffer so the Prince
  stays fresh. If no buffer exists yet, it captures the freshly spawned Prince as the baseline.
* The buffer is also embedded in a full `.pop` savegame (same virtuals with `byte_7CD424 = 1`).

So the Prince is always rebuilt from level data and then overwritten from the buffer. There is
no cross-level global other than that buffer (and difficulty `dword_7EAC3C`, which the ctor uses).

### 4.2 What is in the blob (`byte_7CD424 == 0`) [C]

The save virtual `sub_4C948A` writes, in order (every integer 4 bytes; markers `0xDEADBEEF`):

1. marker
2. `sub_4AEDF5(prince+452)`: four dwords at `+416`, `+512`, `+504`, `+508` of the 0x208-byte
   object created in the ctor by `sub_4AB2E9` (a Prince sub-controller; fields not named) [U]
3. `sub_4C29B5(prince+456)` = **the Inventory** (`Inventory`, 0xB8 bytes, ctor `sub_4C1482`):
   * ten dwords at `inv+20..+56`: counts. `Inventory__ArrowCount(type)` (`0x4C1EAA`) is
     `inv[type + 5]`, and `Inventory__AddArrows` (`0x4C1EBC`) adds to the same slot, so these are
     the arrow counts per arrow type (types 0..8; slot 9 is the "any" bucket)
   * `inv+68` (the weapon/arrow presence mask; `Inventory__AddArrows` ORs `1 << (type + 4)`),
     `+72`, `+80`, `+88`, `+92`, `+96`, `+104`, `+128`
   * the index (0..3) of the weapon in hand among the four weapon slots `inv+4..+16`
     ("Failed to find weapon" if absent)
   * two named lists (`inv+60` right hand, `inv+64` left hand), via `sub_4C1230`: for each held
     item its name string and an int, but only items that still exist in the new level
     (`sub_445C1C` world lookup, `sub_4CC10E` global weapon lookup, or the literal
     `"DoubleHub"`)
   * two bools: a hand-item present flag and a "draw state" flag
4. marker, then `prince+304` (health, `+0x130`) and `prince+372` (max health, `+0x174`)
5. marker

The ctor initialises both health slots to `sub_422A5F(3 * (4 - difficulty))`. `PrinceStart`
argv1 (optional) overrides it for the level's first spawn: `max = 3 * argv1`, then the damage and
heal helpers (`0x4C7673`, `0x4C763C`).

With `byte_7CD424 == 1` (full savegame) the blob additionally holds position/state blocks
(`prince+460`, `+464`), `prince+220/216/212`, `+428`, `+472`.

### 4.3 Consistency check against level data [I]

* `prisonfix/prison1.K2A:10003` runs `prince,weapon:-all` at the start of the game and
  `Dirig1B/Dirig1_6.K2A:26167` runs it again at the end of `lower2`. Both would be unnecessary if
  the inventory did not travel.
* Weapons are mostly given by in-level traps (`weapon:+bow`, `+arrow,type,count`,
  `+sword` in `prison4` and in `Ruins/lamasutemple`) which fire in the level where the player
  would otherwise lack them, so a level can still be played from a cold start (savegame or
  `--level`).
* `PrinceStart` exists in only 7 of 21 `.lvl` (the others use `[Prince]` in the `.lvl`): that is
  unrelated to carry-over. Both only decide *where* the fresh Prince spawns, and only in the start
  region (section 5).

### 4.4 Not carried [C]

Everything else dies with the world: `sub_4131B1` destroys the `3DWorld`, the `LevelGroup`
(rooms, actors, enemies, items), the trap list, the camera state. Enemy state, room state,
opened doors, picked-up items, music/ambience are rebuilt from the `.lvl` / `.k2a`.

---

## 5. Regions inside one level [C for the parser, I for the conclusion]

`[Region]` in a `.lvl` opens a block that contains its own `[Prince]`, `[Rooms]`, `[Enemy]`
etc. (prisonfix: `Parts1to3`, `Parts4to6`; cliffs: ten).

* The `LevelLoadRequest` (`dword_7F33B8`, ctor `0x440FA8`, parsed by `LevelFileParser__ParseFile`
  `0x4410F2` with parser `+44 = 1`) carries an array at `+108` of 8-byte region entries
  `{ char* name; u8 +4; u8 +5; u8 +6; u8 +7 }` and a count at `+32`. The entry ctor
  `sub_43D614` sets all four flags to 1. Also `+28` = start region index (default 0) and `+36` =
  "spawn the Prince" (default 1).
* The second pass (`LevelFileParser` `+44 = 0`, from `sub_444DA6` building the `LevelGroup`)
  counts `[Region]` blocks in `this+92`; for each it calls `sub_442331`:
  * `byte_7C2114 = region.+5`. It gates `EnemyStart__ctor` (`cmp byte_7C2114, bl` just after the vtable store in the EnemyStart ctor; `pop3d.asm:262677`).
  * `byte_7C3420 = (region_index == request.+28) && request.+36`. It gates `PrinceStart__ctor`
    (`if (byte_7C3420)`) and the `.lvl` `[Prince]` spawn (`sub_4426C1` in the section handler).
  * `sub_4422F8` (region entry `+4 == 0`) makes the parser skip that region's `[Rooms]` etc.
* Nothing in the module manager, the traps or the `game` script target switches region at run
  time. There is no `Region` trap name. The only runtime room control is `game,Level:Room,...`,
  and no shipped `.k2a` uses it.

**Conclusion [I]:** in the retail flow all region flags are 1, so **every region's rooms are
loaded at once** and the Prince spawns in region 0 only (`[Prince]` of later regions, and
`PrinceStart` in them, are authoring conveniences that the start-region test skips). The
`Parts1to3` -> `Parts4to6` move in prison is walking from one room group into the next (rooms
become active through neighbour/aperture visibility), not a streamed region swap. What I did
**not** find: any writer of region entry bytes `+4..+7` other than the copy in
`LevelLoadRequest__assign` (`0x43DEA8`) and the savegame `region parameters` section (4 bytes
`01 01 01 01` in `Quick Save.pop`). A dev tool or savegame could still turn regions off.

`Quick Save.pop` `region parameters`, `prince body info` (6 zero bytes: zero tracked bodies in
that save) and the `prince location info` are savegame-only; they do not take part in level
transitions.

---

## 6. Differences from our code

1. **`src/engine/module_table.h:8-9, 42` and `module_table.cpp:80-86`, `src/main.cpp:602`:**
   claim "WinMain posts 0x0C, and 0x0C is `prison`". Wrong reading of `Message__Build`.
   WinMain calls `Game__PostModuleMessage(dword_89FFE8 /*NIS*/, 12, -1, -1)`: target module
   is **NIS (id 8)**, `arg0 = 12` is the **Broder** logo. That 12 is also the id of `prison` is a
   coincidence. `MODULE_ID_STARTUP` should be the NIS module with a `startup_arg = 12`, and
   `module_table_verify` should no longer require it to resolve to `prison`.
2. **`src/engine/module.h:69-71`:** says `0x7BE590` is the target and `0x7BE588` the previous.
   It is `0x7BE588` = current, `0x7BE590` = pending, `0x7BE598` = previous. Also `goto_module`
   (`module.cpp:18-23`) takes a module instance and logs, but there is no message record, no
   destination queue, no history. Slot roles are now established (1.4): rename `vf0..vf5` to
   `enter`, `leave`, `message`, `event`, `tick`, `preload`.
3. **`src/main.cpp:8333-8366` (`game_msgs.exit_requested`):** the interim hack is a fixed
   17-entry list plus `relaunch`. The chain it encodes is right, but: it skips the NIS
   cutscenes (`cs02`..`cs11` after the right levels, `cs12/cs13` after the finale), loses the
   `Win`/`Lose` argument, ends the game at `finale` with only a log line, and restarts the
   process, so nothing is carried.
4. **`src/main.cpp:8085-8087`:** `game_target::receive` only matches the exact text `exit`.
   The original parses `Exit[:Win]` on `:` and `Level:Room,name,bool`. `finale` sends
   `exit:Win`/`exit:Lose` (`Finale.K2a:37816/38186`), which currently falls through.
5. **`src/main.cpp:7752-7757`:** the front end plays `Broder`, the menu, and for New Game
   `cs01` and then `--boot`. This is the right shape (logo, menu, `cs01`, prison), but it is
   hard-wired and done before the engine loop. It should be the same `NIS` module code that
   later cutscenes use.
6. **No loading screen.** The original always passes through `title(1)` and shows the "Loading"
   bitmap while the next level builds; we load inline.
7. **`src/main.cpp:2325-2341` (`weapon`) and the `actor_inst` fields (`armed`, `has_bow`,
   `arrows`, `health`, `max_health`):** the carried state exists but is only persisted by the
   menu's own save (`ui::save_game`: `health`, `max_health`, `armed`; no bow, arrows, items).
8. **`src/main.cpp:7365, 3906-3946`:** `load_level` already loads every region and ignores
   `region` (comment at `:3920`). That matches the retail default (section 5); `--region N` is
   the only place the start-region logic exists, and PrinceStart/`[Prince]` selection should key
   on "region index == start region" rather than "first region that has one".

### Suggested design

Keep it in two layers so the monolithic `main.cpp` does not have to change shape first.

**Layer A, data and routing (new `src/engine/transition.{h,cpp}`, pure, unit-testable).**

```cpp
struct msg { uint16_t id, a0, a1, a2; };           // -1 = none
struct level_link { int32_t next; int8_t nis; };   // from section 3.1, by module id
extern const level_link  k_link[20];               // game modules; others unused
extern const int32_t     k_nis_next[13];           // section 3.2 table, -1 = none
extern const char* const k_nis_movie[13];          // cs01.. Broder
class router {
 public:
    msg cur, pending, prev, dest;                  // 7BE588/90/98, 7D3638
    bool win;                                      // dword_8A0038
    void post(uint16_t id, uint16_t a0=0xFFFF, uint16_t a1=0xFFFF, uint16_t a2=0xFFFF);
    void level_finished();                         // gameModule 1294 handler (2.3)
    void movie_finished();                         // NIS__Finished (3.2)
    void loading_shown();                          // sub_5270FE (3.3)
    bool step(msg& entered);                       // FirstFrameInit: false if nothing pending
};
```

`step()` returns the module being entered; the caller does the work of steps 5-9. A headless
test can drive `post(NIS,12)`, `movie_finished()`, ... and compare the whole list in 3.4
against the table, with no gfx.

**Layer B, in `main.cpp`.**

* Replace `relaunch` + `order[]` (`:8333-8366`) with: `game_target` sets `router.win` from `:Win`
  and calls `router.level_finished()`; each frame call `router.step()`.
* Step action by entered module kind: `game`: tear down (`scene_reset`, which `game_module::vf0`
  already does) and `load_level` + `spawn_prince_from_level` + `spawn_level_actors`;
  `NIS`: `play_movie(k_nis_movie[a0])`, then `router.movie_finished()` (skip with key/mouse as
  now, if `play_movie` already supports it); `title(1)`: draw the "Loading" image once, call
  `loading_shown()`; `menu`: existing front end.
* Extract the per-level set-up that now runs once before the frame loop (around
  `main.cpp:8030-8110`: combat, camera, script targets, pickups) into a function called from the
  `game` branch, so the level can be rebuilt in-process. Until that refactor lands, Layer A can
  still drive transitions by calling `relaunch` with `--level <next>` and a `--movie` list
  (the existing `movies_to_play` at `:7349/8036`) so the order and the cutscenes are right
  immediately; that is a smaller step than the original but not wrong.
* **Prince carry**: add `struct prince_carry { int health, max_health; int arrows[10]; uint32_t
  weapon_mask; bool armed, has_bow; std::string right, left; }` captured from the `actor_inst`
  at `level_finished()` time, when the next module is not the menu, and applied after
  `spawn_prince_from_level`. A fresh New Game clears it (`dword_7F3840`). While still using
  `relaunch`, serialise it like `--health`/`--armed` already are (`:8403-8410`).
  Do not carry anything else.
* Add `MODULE_ID_STARTUP` fix (difference 1) and make `--boot` start at `NIS(12)` with the
  menu/`-m` behaviour of 3.4.

---

## 7. Not traced

* The `+452` object's four carried dwords (`sub_4AEDF5`) and the meaning of `inv+68..+128`
  beyond the arrow mask and the weapon index.
* Who writes region entry bytes `+4..+7` other than the savegame, and whether `request+28`
  (start region) can be set from the command line.
* Whether the `0x5059EE` end-level command (2.4) is reachable in retail.
* The Menu's message numbers 2, 3, 4 beyond "4 is posted after the logo and after the ending",
  and which UI action runs `sub_42ED44` (assumed New Game; it calls `sub_43F636`).
* Where `byte_7D3630`'s clearing flag letters belong in the command-line group (the letters are
  `m`= no menu, `n` = no movies; the group prefix was not identified).
* The exact semantics of `LevelFileParser` `+44` pass 1 vs. pass 2 beyond what 5 describes.
