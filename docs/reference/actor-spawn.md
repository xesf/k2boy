# Adding actors to the scene — the New Game path

Traced from `pop3dreal.exe`. This is the sequence that puts the Prince and the
enemies into a level, and the list of functions a reimplementation has to
provide. Names below are the ones now in the IDA database.

## The chain

```
Game__RequestLevelLoad            0x42093A   "Loading:%s FROM %s"
  LevelLoadRequest__ctor          0x440FA8   defaults: pos 0,0,0  scale 1,1,1
  LevelFileParser__ParseFile      0x4410F2   ctor + drive the parse
    LevelFileParser__Parse        0x441756   section dispatcher (already named)
      LevelFileParser__BeginBodies    0x442CF8   "[Bodies]": read count, alloc table
      LevelFileParser__ParseBodyLine  0x442D7F   one "<index> <path>" line
      ...                                        "[Traps]" builds the trap objects

  (trap construction, during the parse)
  PrinceStart__ctor               0x487191   the [Traps] entry that spawns the player
  EnemyStart__ctor                0x46CB7C   same for each enemy
    Trap__GetArgv                 0x45556B   pull the body name out of the trap argv
    LevelGroup__SpawnActor        0x445A1D   <-- the one that matters
      AvatarBody__CreateAvatar_byIndex 0x4A5EDF   g_avatar_bodies[i]->CreateAvatar()
        AvatarBody__CreateAvatar       0x4C545F
          AnimationNode__ctor          0x401E3C   new(0x120), the actor's scene node
          PrinceObject__ctor           0x4C692D   new(0x1E8)
          Actor__AttachGeometry        0x435C04
      CharacterObject__SetWorldPlacement 0x4C706D  x, y, z, yaw
      LevelGroup__AttachPrince       0x44590F   or
      LevelGroup__AttachEnemy        0x445966
```

## `LevelGroup__SpawnActor` — the signature

```c
// __thiscall on the LevelGroup; retn 18h => 6 dwords of arguments.
Actor* LevelGroup__SpawnActor(LevelGroup* self,
                              int   is_enemy,     // 0 = the Prince
                              int   body_index,   // index into g_avatar_bodies
                              float x, float y, float z,
                              float yaw);
```

Body, in order:

1. `is_enemy == 0 && g_prince != 0` -> `"making a second prince!"`, return null.
2. `AvatarBody__CreateAvatar_byIndex(body_index, is_enemy)`; null ->
   `"failed to load our boy's playmate"`.
3. `CharacterObject__SetWorldPlacement(actor, x, y, z, yaw, 1)`.
4. `actor[0x1AC] = -1`.
5. `sub_50B64E(actor, 0)` — registration, not yet traced.
6. Prince branch: attach, `sub_48D10F`, clear `actor[0x128]`/`[0x12C]`,
   `g_prince = actor`, `sub_4046EA`. Enemy branch: `LevelGroup__AttachEnemy`.
7. `sub_4C174F(actor[0x1C8])`, then `sub_458D47(actor)`.

## Known sizes and globals

| symbol | value |
|---|---|
| `sizeof(PrinceObject)` | `0x1E8` = 488 |
| `sizeof(AnimationNode)` | `0x120` = 288 |
| `sizeof(AvatarBody)` | `0xF4` = 244 |
| `g_avatar_bodies` | `0x7FEFA4`, `void*[g_avatar_body_count]` |
| `g_avatar_body_count` | `0x7FEFEC`, first number after `[Bodies]` |
| `g_prince` | `0x7FFB74`, the singleton |

## How a spawn point finds its body

`PrinceStart__ctor` and `EnemyStart__ctor` do not carry a path. They carry a
**body name** in the trap argv, and search `g_avatar_bodies` linearly, comparing
against each body's name; failure prints `"can't find body %s"`. The Prince
falls back to the literal name `"Prince"`.

So the `.lvl` supplies the cast (`[Bodies]`), the `.K2A` trap blocks supply the
placement (`PrinceStart` / `EnemyStart`), and the name is the join key. Both
halves are already parsed by this project — they have simply never been
connected.

`PrinceStart__ctor` also enforces the singleton itself: `"Can't have multiple
princes"`.

## What this project already has

| step | status |
|---|---|
| `.lvl` parse incl. `[Bodies]` | done (`src/assets/lvl.*`) |
| `master.dat` body/part manifest | done (`src/assets/master.*`) |
| `.act` skeleton nodes | done (`src/assets/act.*`) |
| `.K2A` geometry + trap blocks | done (`src/assets/k2a.*`) |
| `.skl` animation | loader done, playback not |
| `PrinceStart` / `EnemyStart` placement | parsed, unused |

## What is missing

In dependency order:

1. **`avatar_body`** — one per `[Bodies]` line: name, `master.dat` path, and the
   parsed manifest. Mirrors the 244-byte object.
2. **`avatar_body::create_avatar()`** — build the actor: assemble the body-part
   geometry from `master.dat`, attach it to the `.act` skeleton.
3. **`animation_node`** — the actor's scene-graph node. `NiNode`-derived; the
   node the geometry hangs off. See `nirtti-hierarchy.md`.
4. **`scene::spawn_actor(is_enemy, body_index, x, y, z, yaw)`** — the port of
   `LevelGroup__SpawnActor`, including the two singleton guards.
5. **Spawn-point extraction** — walk the `.K2A` trap blocks for `PrinceStart`
   and `EnemyStart`, take the centre and pivot angle as the placement, and the
   argv as the body name.
6. **Attach and draw** — actor meshes into the render list.

Steps 1, 5 and 6 need no new reverse engineering. Step 2 is the real work, and
step 3 depends on how much of `NiNode` gets modelled.

## Not established

* `sub_50B64E`, `sub_48D10F`, `sub_4046EA`, `sub_4C174F`, `sub_458D47` are
  called by the spawn but have not been read. None look like they gate the
  actor appearing in the scene, but that is an assumption.
* The flag bits tested at `LevelGroup+0x180` (`& 3` for the Prince, `& 0xC` for
  enemies) are unidentified; they gate a call to `sub_4ED3B1`.
* `AvatarBody__CreateAvatar_alt` (`0x4C5524`) is a second, shorter variant.
  Which one runs for enemies has not been checked.
* The `.lvl` `[PossessBody]` section (`LevelFileParser__ParsePossessBody`,
  `0x442E38`) also touches the body table; its role is untraced.

---

# Implementation and validation

Implemented in `src/engine/actor.{h,cpp}` (body table + spawn rule, no gfx
dependency) and driven from `src/main.cpp` (geometry attach + draw).
`k2boy --spawns` runs the whole path headlessly over every shipped level.

## Result over the 21 shipped levels

```
levels 21, clean 17
princes 7, enemies 134, unresolved names 8, refused spawns 1
```

**134 of 142 spawn points resolve to a loaded body by name**, with no code
changes needed beyond case-insensitive comparison.

## The `.lvl [Prince]` section is not the live spawn

`lvl_prince_start` parses as `<body_index> x,y,z,heading`, which matches
`LevelGroup__SpawnActor`'s signature exactly and looks like the obvious source.
It is not: in Cistern the only entry is **commented out**

```
[Prince]
;0 25,-22,0,0  ;beginning
```

The live spawn comes from the `PrinceStart` trap in the room geometry, resolved
by name. That is consistent with the code, which reaches `SpawnActor` from
`PrinceStart__ctor` during trap construction. Treat `[Prince]` as a disabled
authoring convenience.

Only **7 of 21** levels contain a `PrinceStart` trap at all. The rest presumably
inherit the Prince across a level transition; that has not been traced.

## Findings in the shipped data

* **`palace3` has two `PrinceStart` traps**, in two different rooms. The second
  is refused with exactly the original's condition (`"making a second prince!"`).
  Not a loader bug: each room declares one, and the level lists each room once.
* **Rooms are shared across level directories.** `palace3` loads
  `MajorGarden.k2a`, which lives in `Geometry/Rooms/Palace4/`. Room lookup must
  be by basename, not by the level's directory.
* **8 unresolved body names**, all in levels that are development leftovers
  (`cliffs_ropetest`, `cliffsbu`, `copy of ...`) plus one each in `cliffs`,
  `dirig2`, `dirigfinale` and `palace3`. The names (`A2ARCHER`, `W1`,
  `PG3ARCHER`, `manrat`) have no entry in those levels' `[Bodies]` lists. The
  original would print `"can't find body %s"` and skip them, which is what this
  does.

## Still missing

The body is currently attached as **one `.K2A`**, taken from `master.dat`'s
geometry line. That is what the shipped data provides and it draws correctly,
but it is not what `AvatarBody__CreateAvatar` does: the original builds an
`AnimationNode` and hangs individual body parts off the `.act` skeleton, which
is what makes animation possible. Until that exists, actors are static meshes at
their spawn placement.

## Viewer behaviour

Two things that were wrong when actors first went in:

* **`make run` showed no actors.** It loads a single room via `--room`, and
  spawning was wired only into the `--level` path. A lone room has spawn points
  but no `[Bodies]` list — that lives in the `.lvl`. Fixed by indexing every
  `master.dat` in the install by its `-n` name (31 entries) and resolving
  against that. It is the same name lookup the original does, over a wider set.
  prison1 now spawns its one `EnemyStart` (PG1).
* **The camera started in the middle of the bounding box.** For a level like
  Cistern, 400 units across, that is nowhere near anything. `frame_scene` now
  starts at the player's spawn looking along its facing, falling back to the
  first actor, and only then to the box.

## Body geometry is world-space, not node-relative

`k2a_build_mesh` ignores a node's `pos`/`rot`/`scale` and uses the vertices
directly. That is safe for rooms, and it turns out to be safe for bodies too:
every spawned actor reports a local extent of roughly

```
x[-0.4, 0.4]  y[0.0, 2.1]  z[-0.2, 0.4]
```

— a humanoid standing on `y = 0`, with the parts already in place. Had the
exporter positioned parts by node transform instead, they would all collapse to
the origin, and this measurement is printed on every spawn so that stays visible.

---

# Two bugs found by actually looking at the frame

Both were reported as "no actors visible" and a crash. Neither was in the spawn
code.

## 1. `NSWindow` over-release — segfault on quit

`platform_macos.mm` builds the window under ARC, but `NSWindow` defaults
`releasedWhenClosed` to `YES`. That predates ARC and conflicts with it: closing
the window — by the close button, or by `-close` in `shutdown()` — releases an
object ARC is still holding through `g_window`. The next `-isVisible` in
`poll_event` reads freed memory.

```
EXC_BAD_ACCESS (SIGSEGV) at 0x18
0  libobjc.A.dylib  objc_msgSend   (selector: isVisible)
1  k2boy            main + 4680 (main.cpp: poll_event)
```

Fix: `[g_window setReleasedWhenClosed:NO]` at creation.

That stopped the crash but revealed a second bug hiding behind it: **the close
button did not quit the app**. Previously "closing" crashed, which looked like
quitting. Two things were wrong:

* There was no window delegate at all. Quit detection polled `-isVisible`, which
  the close button does not reliably drive. Added `K2WindowDelegate`
  implementing `-windowShouldClose:`; it records the request and returns `NO`, so
  the window stays alive and the main loop tears down in order with the GL
  context still valid.
* `EVENT_QUIT` is edge-triggered but the close condition is level-triggered. With
  the flag left set, *every* poll pushed another `EVENT_QUIT`, so main's inner
  `while (poll_event(e))` never drained and the outer `while (running)` was never
  re-tested. The app spun forever instead of exiting. Fixed with a `g_quit_sent`
  latch.

Verified by driving `-performClose:` — the same path the close button takes —
and watching the full teardown run: delegate fires, loop ends, gfx down, audio
closed, platform down, exit 0.

## 2. The camera was burying itself in geometry

Actors were rendering correctly the whole time. The camera was placed 6 units
back along the spawn's facing, which is geometry-blind — it put the viewpoint
**through a cell wall** in prison1 and **through the ceiling** in Cistern, so the
first thing the viewer showed was the inside of a wall.

A spawn point only guarantees open space immediately around itself. The offset
is now 2.5 back and 1.6 up, which was *measured* on both scenes rather than
assumed.

## The lesson: `--shot`

The engine can now render a frame and write it to a BMP:

```
k2boy --room <room> --shot out.bmp [--cam x,y,z,yaw,pitch] [--notrig]
make shot S=out.bmp
```

Every camera claim above was wrong until a frame was actually rendered and
looked at. Counts, bounds and draw-call totals all said the actor was fine —
and it was. Use `--shot` before concluding anything about what is on screen.

## Open, and unrelated to actors

**Cistern renders almost nothing.** With triggers off and the camera above the
water line, the level shows one large untextured green mesh against the clear
colour, with the rest of the room geometry missing, despite all five rooms
parsing (24k vertices) and reporting `0 fail` on every texture. Every
material-less mesh in `cistern.k2a` is a trap volume — including `Current1` at
77x12x8 and `SmallerCurrent01` at 5x12x33 — so the green is *not* a trigger.
This predates the actor work and has not been diagnosed.
