# Camera subsystem and the scripted-skill commands

Reversed from `.ida-export` (Hex-Rays and listing only; ida-mcp was down, so nothing was renamed in
IDA). Confidence tags: **[sure]** read straight off the code, **[likely]** consistent but one
operation was inferred from its use, **[guess]** shape only.

## 1. Two classes, not one

The script target `camera` is **not** `CameraLogic`. The doc elsewhere says "CameraLogic, parser
0x40855D"; the parser's `this` is the singleton `dword_7E9344`:

| object | class (RTTI) | size | role |
|---|---|---|---|
| `dword_7E9344` | `CameraStateTable` (vftable 0x7437F8, ctor `0x406782`, `new 0x350`) | 0x350 | script/command layer. A `Trap` with a state machine at `+0xF8`. Parses messages, owns the 18 camera slots, chooses subject |
| `this+0x128` (`+296`) | `CameraLogic` (vftable 0x743A4C, ctor `0x40BD03`) | | the physical camera: gauges, LOS search, springs, look-at |
| `dword_7E9448` | input mapper made by `0x406F6D` | | camera-control input (stick/keys) |

`0x409DA9` builds all three. Both a script message and the game call into the same entry:
`0x409D99(str)` = `0x40855D(dword_7E9344, str)`, and `0x409D62(slot)` = "game asks for a slot".

## 2. Slots (the table `camera.dat` fills)

`CameraStateTable` has 18 slots. Per slot `n`:

| offset | content |
|---|---|
| `+0x190 + 4n` (400) | flag word (see 2.2) |
| `+0x1D8 + 12n` (472) | look offset vector |
| `+0x2B0 + 4n` (688) | gauge array pointer (24 bytes each, count at `-4`) |
| `+0x2F8 + 4n` (760) | gauge count |

Slot numbers (letters from `camera.dat` and from the `D<letter>` message):

| slot | letter | meaning (camera.dat comments) |
|---|---|---|
| 0-8 | `0`..`8` | debug/keypad views (numpad 1-9 select them through `0x5059EE`) |
| 9 | `D` | default chase camera |
| 10 | `F` | fighting camera (flag word defaults to `8` = G) |
| 11 | `A` | aiming (bow) camera |
| 12 | `X` | death camera |
| 13 | `U` | "look for ledge" |
| 14 | (script) | the slot every `follow/tripod/security/look/#P` message fills. Never in the file |
| 15 | `L` | look-around (free look). Special-cased, see 3.5 |
| 16 | `W` | swimming (below and behind the Prince) |
| 17 | `T` | treading water (above, looking down) |
| 18-24 | pseudo | commands, never slots (see 3.4) |

### 2.1 camera.dat (`0x40607B` CameraFileParser::Parse, `0x407265` load)

* Header `-v 2`. Five spring values, one per line, in the order written to `+312,+316,+320,+324,+328`
  of the manager, then pushed to the camera by `0x40825C` as
  `SetSpring(idx, value)`:

  | file line | name in comment | value (shipped) | camera spring idx |
  |---|---|---|---|
  | 1 | ORBIT_ELEV | 5 | 2 |
  | 2 | ORBIT_Y | 5 | 3 |
  | 3 | PAN_X | 8 | 0 (elevation error) |
  | 4 | PAN_Y | 30 | 1 (azimuth error) |
  | 5 | DISTANCE | 10 | 4 (distance error) |

  The comment's names are loose: from `0x40DDBB`, idx 1 acts on the horizontal angle, idx 0 on the
  vertical angle, idx 4 on the arm length. Idx 2 and 3 are only set up; the orbit code that reads
  them was not traced [guess].
* `[<letter or digit>]` then a count, then one gauge per line:
  `x,y,z  [minLen]  lx,ly,lz`. The `lx,ly,lz` triple, when present, becomes the slot's **look offset**
  (the last one seen wins, `0x40495A` into `+472+12n`). Without a `[minLen]` the minimum is -1.
* Per gauge the loader stores (24 bytes): `dir = normalize(pos - lookOffset)`, `+12 maxLen = |pos -
  lookOffset|`, `+16 minLen` (the bracket value, else maxLen), `+20 curLen = maxLen`.
  So a gauge is an **arm**: unit direction, a longest and a shortest length.
* Optional clause after the bracket: `-C:{A|N|R},T:{A|R}[X][G]` (never used by the shipped file).
  Bits: `C:A`=1, `C:N`=2, `C:R`=0, `T:A`=4, `X`=0x20, `G`=8. Without the clause, slot `F` gets
  flags `8`; every other slot gets 0. A slot defined twice logs "duplicate camera data".
* "[L]" ("old look camera, not used any more") is read into slot 15.

### 2.2 The flag word (`0x40C1A6`, written to CameraLogic)

| bit | field | meaning |
|---|---|---|
| 0x01 | `+74` | **absolute camera**: no subject pivot, no rotation by the subject; the arm vector *is* the world position |
| 0x02 | `+75` | arm not rotated by the subject (world axes) but the pivot still follows him |
| 0x04 | `+73` | **look point is absolute**: `+40` (the B vector) is a world point, not an offset from the subject |
| 0x08 | `+72` | **G**: a candidate camera position must also have a clear line to the Prince's head (+1.0 up), `0x40CEF1` |
| 0x10 | `+76` | pivot is `subject + 2*(a-b)` style offset from the skeleton (`0x40C47A`) [guess]; set by `0x40C284` |
| 0x20 | | **X**: snap (see 4.1) |
| 0x40 | | **F**: no fallback gauges (see 3.2). Only meaningful in the message flags |

## 3. Script messages (parser `0x40855D`)

`+0xD8` (216) is the **pending command** word. A second message in the same frame logs
"Trying to do multiple camera commands" and overwrites. The command runs at the next state
update in `0x4077DD`. `+302` is cleared at the start of every message.

Dispatch:

1. First char `#` -> `0x408C4F` (3.4).
2. Else if the string has a comma, switch on the first letter (`toupper - 'A'`):
   `A`nimated (3 chars), `F`ollow (3), `L`ook (4), `S`ecurity (3), `T`ripod (3). Matching is
   `strnicmp` on the first 3-4 chars. Anything else: "Illegal camera command:%s at %s".
3. No comma: `Water` (5 chars) -> environment 2; `Air` (3) -> environment 1
   (`0x44647B(world, n)`, loads fog/colour set n: 1 air, 2 "WaterFog"); first letter `D` -> 3.1.

Vectors (`0x409135`), written `(…)`: either `x,y,z` (three `strtod`) or `Name`, `Name+x,y,z`,
`Name-x,y,z` where `Name` is looked up with `0x435E82` (any named object) and its world position
is the base. `)` ends the group. `(@Node)` is a camera node from the list at `+0x158` (344),
`0x407AE7`: position at node `+0`, its `.Target` at `+12`. A bare `(@)` after `security,` means
"the camera's present position" (`0x40A263`, `[manager+16]+8`).

Subject (`0x408F3C`, `0x408FEE`, `0x40837A`): `follow/look/security,<Actor>[\nodeA[\nodeB]],…`.
No actor -> the Prince (`dword_7FFB74`). Sets `+352` (subject), `+360`/`+356` (the two node
indices on his skeleton, default the actor's own `vcall+128`), `+216 = 73` (0x49 = `D` slot|0x40).
Unknown actor: "failed to find target actor %s".

Trailing flags (`0x409975`), parsed after the last `)`:

| text | effect |
|---|---|
| `X` | flag bit 0x20 = **snap/cut** (4.1) |
| `X` then `A` / `W` | also switch the camera to air / water environment (`0x44647B` 1 / 2) |
| `G` | bit 0x08 (line of sight to the Prince required) |
| `!` | `+302 = 1`: **sticky**. While set, `0x409D62` (game-driven slot changes: swim, fight, aim, ledge, death) is ignored |
| `F` | bit 0x40 |

### 3.1 `D…` default / slot message

`D`, optional `efault` (7 chars), then one letter: `A`=11 `D`=9 `F`=10 `L`=15 `T`=17 `U`=13 `W`=16
`X`=12, or `0`..`8`. A lone `D` is *illegal* (logged), it is `DD` that returns to the chase
camera. `DDX`, `DefaultDX`, `DW` (water), `DX` (death), `DF` (fight) all go through this path.
Then the flags, stored into the slot's flag word (`+400+4n`, so `X` is *remembered* for that slot),
and the command `n | 0x8040` is queued (0x8000 is a marker only; 0x40 = engage the follow state).

### 3.2 `follow,[Actor,](A)(B)[flags]`

* `0x409F11(config+744, A)`: gauge = `dir = A/|A|`, `maxLen = minLen = curLen = |A|`.
  `B` goes to `+640` (slot 14's look offset).
* `0x40BC30(world, 0)` clears `world+472`; command `0x804E` (slot 14 | 0x40 | 0x8000).
* **Without `F`**: command `0x844E` (adds 0x400 -> `0x40D222` appends the slot-9 `D` gauges after
  this one as fallbacks) and, if `minLen == maxLen`, `minLen = max(0.25*maxLen, 1.5)` (only when
  that is less than maxLen). So a bare `follow` may slide closer or fall back to the default
  arms when blocked; `F` pins it.
* Flags to the camera: `v54` as parsed (no 1/4 bits), so the pivot follows the subject and
  the arm is rotated by the subject's world rotation (`0x40CB19` -> `0x40C380`).

### 3.3 `tripod`, `security`, `look`

| verb | bits OR-ed | first group A | second group B | behaviour |
|---|---|---|---|---|
| `tripod,(A)(B)` | 1 \| 4 | camera world position (or `@Node`: its position, B defaults to its Target) | world look point (`(@Other)` = *that node's Target*) | fixed pose. `world+472 = 1` |
| `security,[Actor,](A)(B)` | 1 | camera world position, or `(@)` = hold the current position | **offset added to the subject's position** (world axes) | fixed position, aims at `subject + B` every frame |
| `look,[Actor,](A)(B)` | 4 | arm: offset from the subject | world look point | camera rides with the subject, in the frame whose +z points from the subject to B (`0x40CB19` builds the rotation taking (0,0,1) to `normalize(B - subjectPos)`) |

All three go to command `0x804E`. `tripod`/`security` call `world+472 = 1`, `look`/`follow` 0
[+472 meaning not traced; it follows the "camera is a fixed shot" idea].

### 3.4 `#` messages (`0x408C4F`)

Chain of `#<letter>` tokens, building the command word `v` (starts `0x40`):

| token | effect |
|---|---|
| `#0`..`#8` | `v |= digit` |
| `#D` `#F` `#A` `#X` `#U` `#L` | `v |= 9, 0xA, 0xB, 0xC, 0xD, 0xF` |
| `#P<spec>` | slot 14, position spec by `0x4096C5` (below); `v |= 0xE` |
| `#K[Actor[,nodeA[,nodeB]]]` | subject (no name = Prince). Clears bit 0x100 |
| `#C` | `v |= 0x1000`: **cut** to the slot's first gauge now (`0x407DD1`) |
| `#!` | `v |= 0x4000`: sticky (`+301 = 1`) |
| `#@` | `+301 = 0` (unlock) |
| `#S0` / `#S1` | `world+472` = 0 / 1 |
| `#W<…>` | `+308` = a timed condition object (`0x4EFE4D`); freed by `0x4076EE` when it reports done |
| `#Z` | clear bit 0x40; if nothing else is set, command 22 (freeze) |
| `#G` | command 18 (re-engage previous camera, no transition) |

`#P`: `@Name[,B]` (camera node position as the arm start, B or the node's target) or
`A,B` vectors, then optional `-C:{A|N|R},T:{A|R}[XA|XW][G][!][F]` which fills the flag word.

Uses in data: `Camera,#P@TheChase_Camera01` (2), `Camera,#C#D#K}` (2, "cut to default, subject back
to the Prince").

### 3.5 Commands run by `0x4077DD`

Command `c`, `slot = c & 0x3F`:

| c | effect |
|---|---|
| 0 | nothing |
| 15 (`L`) | free-look: sets the camera with arm `(0,1.5,0.1)`, spring 3.0, limits (deg) yaw `360` (`75` if the Prince's state is 3), pitch `110` (`95`), third `66` (`0x40C013`, setters `0x40F6F6/09/1C` store radians in `+300/+304/+308`); mode `+844 = 2` |
| 18 | `c = +220 (last) \| 0x40 with 0x1000 cleared`: back to the previous camera, no transition |
| 19 | arm `(0,2.2,0)`, spring 2.0, mode 2 (used when a character picks up/attaches, `0x4C2048`) |
| 21 | `c = +220 \| 0x1000`, 0x40 cleared |
| 22 | freeze: camera stays where it is (`StFixedCamera`), `+844 = 0` |
| 23 | freeze and lock (`+301 = 1`) |
| 24 | clears the lock (only command honoured while `+301`) |
| else | `slot >= 18` -> "Camera pointing" warning, slot 0. Null gauge array -> "Problem with current camera %d". Bit `0x1000`: `0x407DD1` cut. Unless `0x2000`, `+220 = c` is remembered. If `0x40`: bit `0x4000` sets `+301`; `0x40BC5A(cam, 0)` un-pauses; bit `0x80` -> mode 3 (`StMonitorActivity`) else mode 1 + `0x4081FF` (apply slot) + bit `0x400` -> append default gauges + state `StFollowTarget` |

`+301` is the lock: while set, all commands except 24 are dropped.

`0x4081FF` = `0x40BFA7(cam, subject, nodeB, nodeA, count, gauges, lookOffset, flags)` then
`0x40825C` (push the five springs, then `SetDamping(0.0)`, enable, un-pause).

State functions: `0x40734F` InitialState -> `StWaitForTarget` (`0x40738E`) -> when `+352` exists:
`StFixedCamera` (`0x4074DD`, command `0x1049`=`D`) or `StFollowTarget` (`0x40748F`). Per frame
(`0x4073F3`, `0x40748F`, `0x4074DD`): `0x4074FD` (copy the camera node's pose into the scene
node, NaN guarded with "Camera pos is not a number"), vcall `+0x38` (CameraLogic update),
`0x4077DD`, `0x407687`, `0x4076EE`.

`0x407687`: if the Prince is swimming (`+472`) and the camera is lower than his water line
(`+476`), switch to the water environment (2), else back to air (1).

Game-driven slots (`0x409D62`, skipped while `+302`): 16 (enter water), 9 (default, most
transitions), 10/11 (combat start, bow aimed), 12 (death), 13/14 (debug keys), 19 (attach), 22
(hang/ledge grab frozen).

## 4. CameraLogic, per frame (`0x40DDBB`, `now` is absolute time)

Fields (`ctor 0x40BD03`): `+8` busy, `+13` paused, `+16` last update time, `+24` first-frame,
`+32` subject, `+40` look vector B, `+72..79` flags (2.2), `+216` spring enable, `+220` desired
arm vector, `+256` damping mode (0.4), `+264` smoothed position, `+360` gauge count, `+364`
chosen gauge, `+368` gauges, `+376` multi-gauge, `+380` ?, springs at `+88 + 16*i`.

1. `+13` paused -> return.
2. `R = 0x40CB19`: identity for absolute/world-axes cameras or no subject; for flag 4 the rotation
   taking +z to `normalize(B - subjectPos)`; else the subject character's world rotation
   (`0x40C380`, vcall `+0x7C` -> `+0x2C`).
3. `dt = now - last` (`0x40C8DF`, clamped so it is never negative). **Nothing happens below
   dt = 0.01 s**, so the camera runs at most 100 Hz.
4. `pivot = 0x40C47A`: subject position + B (B added in world axes; swimming state shifts z to
   -0.1). Absolute cameras use 0.
5. `0x40D341` picks the gauge. For each candidate (preferred order = file order; the last good
   one is `+364`, sticky) it casts from the pivot along `R*dir` for `maxLen`:
   * `0x41193A` ray against the room collision: if it hits closer than `maxLen`, length =
     `hit*… - 0.1*maxLen` clamped to `>= minLen`; if `minLen` can't be kept the gauge fails.
   * `0x4117B7` sphere/LOS test; `0x40CE04`/`0x40CD0B` verify the end point can see the pivot with
     tolerance `0.95^2`; `G` adds `0x40CEF1` to the head.
   * First gauge that works wins; if all fail the best partial is used and `+79` (snap) is
     raised. If the subject hasn't moved (`|Δpivot|² < 0.0001`) the last answer is kept.
6. Desired position `= pivot + R*dir*curLen` -> `+220`. Error `e` between it and the smoothed
   position (`+264`).
7. If snap (`+79`) or `|e|² < 0.003` the camera jumps there. Otherwise **three critically damped
   springs** (`0x40C71C`, exact exponential step, deadzone `+88+16i`): distance (idx 4, rate 10),
   azimuth (idx 1, 30), elevation (idx 0, 8). The offset from the pivot is rotated about world Y by
   the azimuth step and about its right axis by the elevation step, the length corrected by the
   distance step. (`SetSpring` stores `k` at `+92+16i` and `k^2*(1-damp)/4` at `+96+16i`;
   `0x40C30C` sets damp.) Looking straight down caps the elevation at ±π/2.
8. `0x40C8FA` builds the look-at orientation (forward = `normalize(pivot - cam)`, up (0,1,0)) with
   a minimum distance of 0.1, writes `+264` and the scene node. `+24`, `+79` cleared,
   `+216 = 1`, `+16 = now`.

### 4.1 What X, F, G, `!` do

* **X** = bit 0x20 in the flag word: `0x40C1A6` sets `+216 = 0` and `+79 = 1`, and zeroes the
  eight spring velocities. First `Update` therefore places the camera exactly on the desired pose
  with no glide; at its end `+216 = 1` and `+79 = 0` so the springs run again.
  Without `X` the camera glides from wherever it is (tripod, security too).
* `XA` / `XW` additionally switches the fog/colour set air/water.
* **F** = no fallback gauges (3.2).
* **G** = candidate must see the Prince's head.
* **!** = sticky against game-driven slot changes.

### 4.2 FOV

No code in `CameraStateTable`/`CameraLogic` touches the field of view. `+300/+304/+308` are
angle limits for free look, not a FOV. The projection comes from the level's clip/FOV data
(not part of this subsystem).

## 5. Actor-script commands

Two message tables, same names: `EnemyStart` (`0x46CB7C` area, handlers `0x46D…`) and
`PrinceMessage` (`dword_7FD868`, `0x484B…`/`0x485…`). Each splits argv by `,`.

| command | EnemyStart | Prince |
|---|---|---|
| `SkillAt` | `0x46D8DF` -> `0x46D592(..., walk=0)` | `0x484C67` |
| `WalkAt` | `0x46D8F1` -> `0x46D592(..., walk=1)` | not present |
| `Goto` | `0x46D906` (a waypoint name in the actor's graph) | `0x4855F1` = `Teleport` |
| `Snooze` | **no-op** (`nullsub_138`) | `0x484F77` |
| `Item` | `0x46DC51` | `0x4856C6` |
| `EffectAt` | `0x46DDF3` | `0x485907` |
| others | hurt, kill, play, draw, start, remove, weapon | + attach, potionclear |

### 5.1 Placeholders (`0x455881` -> `0x455610`, yaw by `0x51A828`)

Argument 1 of `SkillAt`/`WalkAt`/`Goto`/`Teleport`:

* empty, or the literal ValueName: the trap itself (the object that received the message).
* `x,y,z,degrees`: yaw is `deg*pi/180`.
* a name (`0x45550E` finds the node/trap by name: EnemyStart and Generic nodes both): its
  position `+0x4043A8`, and yaw from the node's rotation (`0x42819F`); sign flipped when the
  rotation axis y is positive. A non-zero yaw with a non-zero horizontal offset warns
  "wobbly location".
* Prince version also: `Prince` -> the Prince's pose, `-yaw` (`0x51A828` returns `2*acos(w)` signed); `*`
  -> the same but not negated, and `*+n` / `*-n` / `*=n` adjust the Y by n (error "illegal y
  offset").

### 5.2 `SkillAt:skill[,placeholder[,mode[,cameraCmd]]]`

* mode (Prince): `DEAD`=1, `STANDING`=2 (default), `FALLING`=3, `SWIMMING`=4.
  mode (EnemyStart): `DEAD`=1, `STANDING`=2, `REMOVE`=5. Anything else logs "illegal mode".
* **Argument 4 is a camera command** passed straight to `0x409D99` = `0x40855D`. None of the shipped
  scripts use it (verified by search of `data`), but it is the intended way to cut with the skill.
* Store: `0x4C7343` (Prince) / `0x4C74E3` (enemy): skill name, `x,y,z,yaw` at `+76..+88` of the
  skill player, mode at `+72`, then state `StPlaySpecial`. An enemy already running one is
  ignored while `v5[76] >= 1`.
* Note a fixed `Death…` substitution: with `dword_7EAC40 == 1` the three skills `DeathDecap_G`,
  `DeathFeetGrind`, `DeathSpikes_G` are all replaced by `DeathFeetSpike`.

### 5.3 `WalkAt:walk|run,placeholder[,mode]`

First arg must be `Walk` (walking flag) or `Run`, else "bad SkillAt/Move" error. The rest as 5.2.
`0x4C4144(..., "", pos+yaw, mode, walkFlag)` then `StPlayWalking`.

### 5.4 `Snooze:t[,camera[,mask]]` (Prince only)

* `t < 0` -> infinite (`FLT_MAX`).
* if `t > 0` and (argv1 absent/unparsable, or true): `0x4095B8(camera, 1)` adds the two
  camera-control inputs (`+832`, `+836`) to the input stack so the player cannot move the camera.
  `0x4095B8(camera, 0)` removes them (done at level load, `0x406C42`).
* argv2 (default true; forced false when the Prince's state is 5): `0x4C7710(prince, t,
  mask?0x130:0)`: input mask `0x130` retained, `+384 = t`.
* Booleans: `t true on yes 1` / `f false off no 0` (`0x50BBB6`).

### 5.5 `Item:Name[,on|off][,L|R]`

* argv1 not a bool: treated as **on**, error only if non-empty. argv2: a bool is "left"; `L` = left,
  `R`/empty = right, else error.
* Lookup: `0x445C1C(world, name)` (a named world object), else `0x4CC10E(name)` (a global item/weapon
  by name), else "Can't find item %s". The Prince variant first ensures the mesh has a
  `0x41E9A6` attachment record.
* `on`: `0x4C18E3(inventory, item, left)` attaches into slot `+0x3C` (right) or `+0x40` (left);
  "Trying to attach…" if the slot is null. `off`: `0x4C194D` removes. `inventory = [actor+0x1C8]` for
  an EnemyStart (`+456`), `[prince+0x1C8]` for the Prince.

## 6. Differences from our code

Priorities: P1 visible in shipped cutscenes, P2 correctness of a decoded feature, P3 later.

1. **P1 - `src/main.cpp:2624` `receive`: any `D…` is `DEFAULT`.** Fix: parse per 3.1. Keep `DD*`
   and `Default*` as chase; map `DW`, `DT` (water), `DX` (death), `DF`, `DA`, `DU`, `DL` to the same
   chase for now but remember the slot so that (a) the `X` flag is stored per slot, (b) later slot
   cameras from `camera.dat` can be wired. A lone `D` should log "Illegal camera command".
2. **P1 - flags are dropped (`src/main.cpp:2616` comment).** `X` means snap, no `X` means a glide;
   ours always cuts. Add `bool snap` to `camera_target`, and when false run
   `cam = cam + (want - cam)*(1 - exp(-k*dt))` with k = 10 for the arm length (and 30/8 for the
   angular part if you do it in polar form), using `want` from the scripted block. First frame
   after the message with `X`: jump. Also parse `!` (sticky) and `G`, `F`, `XA`, `XW`.
3. **P1 - `look` (src/main.cpp:10191).** Ours puts the camera at `prince + A` in world axes. The
   original rotates `A` by the rotation taking +z to `normalize(B - prince)` (3.3). Suggested:
   `f = normalize(B - p)`, `r = normalize(cross(up, f))`, `u = cross(f, r)`,
   `cam = p + r*A.x + u*A.y + f*A.z`.
4. **P2 - `follow` pivot (src/main.cpp:10209-10216).** The original is `P = subject + B` (world axes),
   `cam = P + R*A`, look at `P`. Ours is `cam = subject + R*A`, look `= subject + R*B`. Identical for
   every shipped `follow` (`(0,3,-4)(0,0,0)`, 17 uses) but wrong for the second group in general.
   Change `at` to `o + B` and `cam` to `o + B + R*A`. Doc says the original rotated B; it doesn't.
   [likely]
5. **P2 - `security,(@)(0,2,0)` (src/main.cpp:2670 `paren`).** `(@)` with no name fails the lookup
   and the whole message is rejected. Original: "keep the camera where it is". Fix: in `paren`,
   when `body == "@"` return the current `cam.pos` (pass it into `camera_target` each frame or
   capture it in `receive`).
6. **P2 - `tripod` second group `(@Other)`** should take `Other`'s *Target*, not its position
   (src/main.cpp:2670, `out` vs `tgt`). Only matters when the second group is a node.
7. **P2 - vector syntax.** `Name`, `Name+x,y,z`, `Name-x,y,z` are valid groups (3 vec parser
   `0x409135`); `paren` only handles `@Node` and `x,y,z`. Resolve `Name` through `sc.actors`
   script names / `sc.points`.
8. **P2 - `#` syntax** (`#P@Node`, `#C#D#K`, `#!`, `#Z`, `#G`). Today only `#d`. `Camera,#C#D#K` is
   "cut to chase, subject back to Prince"; map it to `mode = DEFAULT` and clear `subject`. `#P@Node`
   is `tripod` with the node's own Target.
9. **P2 - snooze** (`src/main.cpp:2250`). Negative duration should be infinite (ours becomes a past
   time, so no snooze at all). Argument 2 (camera input lock, default true) and 3 (mask) are
   ignored; honour arg 2 by also freezing camera-orbit input (`input_locked` currently covers the
   Prince only, check the mouse look). `Snooze:10,T,F` appears 8 times.
10. **P2 - EnemyStart `snooze`** must be ignored. `actor_target::receive` (`src/main.cpp:2250`) sets
    `snooze_until` on every actor. Harmless today, but it should be gated on `is_prince(a)`.
11. **P2 - `SkillAt` 4th argument** (camera command) is ignored. Add: after `skill_at` succeeds, if
    `f.size() > 3`, forward `f[3]` to `camera_msgs.receive`. Not used by shipped data.
12. **P3 - `SkillAt` modes** (`src/main.cpp:2445`): the Prince's `FALLING`/`SWIMMING`, and an enemy's
    `REMOVE` as mode 5 are covered; `STANDING` (explicit) and `FALLING` for the Prince are not
    distinguished.
13. **P3 - `item` (`src/main.cpp:2348`):** `Item:X,on,R` etc. follow the original's L/R. The
    original treats an unparsable argv1 as `on` (ours requires the literal `on`, so `Item:X` is
    on already; `Item:X,L` would be read as off). Use `equals_nocase(f[1],"off") ||
    "f"/"false"/"no"/"0"` for off and anything else as on. And fix the hand: arg 2 can also be a
    `true`/`1` bool meaning left.
14. **P3 - chase camera (`src/main.cpp:10100-10150`).** It is a hand-made stand-in. The original
    chooses among five `[D]` gauges from `data/camera.dat` (arm = `pos - lookOffset`, min
    length in brackets), keeps the last good one, slides the arm down to `minLen` against
    collision, and eases with three springs (10/30/8). If ported: parse `[D]` into `{dir, maxLen,
    minLen}`; pivot = `body + (0,1.7,0.75)`; for each gauge, `R*dir` cast; pick the first that
    reaches `>= minLen`; smooth with `x += (want-x)*(1-exp(-dt*k))`. Fight cam `[F]` is
    selected by combat (slot 10/11) and has `G`.
15. **P3 - underwater switch** (`0x407687`). If the Prince is swimming and the camera is below his
    water line, use the water fog; ours has no per-camera environment.

## 7. Not traced

* The exact semantics of `+76` (flag 0x10) pivot variant (`0x40C47A` first branch) and `+78`
  (relative-to-base rotation in `0x40C380`).
* Spring indices 2 and 3 (ORBIT_ELEV, ORBIT_Y): set but I did not find their consumer, probably the
  player-driven orbit in `0x40E71F`/`0x40EDE9`.
* `world+472` (`0x40BC30`) consumer.
* `#W` timed condition object class (`0x4EFE4D`).
* `0x407DD1` blend formula (cut to gauge 0): it composes slot look offset, target position and
  `+364` with flags 0x100/0x200; the operation names (`0x613E83`, `0x613368`) were taken as vector
  add.
* Whether B is rotated by R when added to the pivot (`0x40C47A` adds it as given).
* Mode value `+844` meanings beyond 0 fixed / 1 follow / 2 special / 3 monitor.
* The input mapper `dword_7E9448` (`0x406F6D`) beyond "owns camera controls".
