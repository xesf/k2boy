# Plan: boot to gameplay

The question this answers: what does the original do between launch and the
player controlling the Prince, and what is left to build.

## Solved this pass: the module table and the startup id

`module::module(name, x)` (`0x43475C`) inserts itself into `dword_7F2A10`
**sorted alphabetically by name** (`strcmpi`), storing its own index at
`[this+8]` and bumping the count at `dword_7F29A8`.

So **module ids are not constants — they are positions in the case-insensitive
sorted list of module names**, fixed at static-init time. That makes the
`0x0C` WinMain posts recoverable by reconstructing the list.

Every module is constructed with a name, and `gameModule` additionally with its
`.lvl`. Extracted from the 20 constructor call sites:

| id | name | level |
|---:|---|---|
| 0x00 | burning | `geometry\rooms\DirigFinale\DirigFinale.lvl` |
| 0x01 | cistern | `geometry\rooms\Cistern\cistern.lvl` |
| 0x02 | cliffs | `geometry\rooms\cliffs\cliffs.lvl` |
| 0x03 | docks | `geometry\rooms\cityanddocks\cityanddocks.lvl` |
| 0x04 | finale | `geometry\rooms\finale\finale.lvl` |
| 0x05 | lower | `geometry\rooms\dirig1a\dirig1a.lvl` |
| 0x06 | lower2 | `geometry\rooms\dirig1b\dirig1b.lvl` |
| 0x07 | menu | *(sMenuModuleClass)* |
| 0x08 | NIS | *(sNISModuleClass)* |
| 0x09 | palace2 | `geometry\rooms\palace2\palace2.lvl` |
| 0x0A | palace3 | `geometry\rooms\palace3\palace3.lvl` |
| 0x0B | palace4 | `geometry\rooms\palace4\palace4.lvl` |
| **0x0C** | **prison** | **`geometry\rooms\prisonfix\prisonfix.lvl`** |
| 0x0D | rooftops | `geometry\rooms\roof1\roof1.lvl` |
| 0x0E | ruins1 | `geometry\rooms\ruins\ruins.lvl` |
| 0x0F | stronghold1 | `geometry\rooms\moontemple\moontemple.lvl` |
| 0x10 | temple | `geometry\rooms\solar1\solar1.lvl` |
| 0x11 | title | *(sTitleModuleClass)* |
| 0x12 | tower | `geometry\rooms\ivorytwr\ivorytwr.lvl` |
| 0x13 | upper | `geometry\rooms\dirig2\dirig2.lvl` |

**WinMain posts `0x0C` = `prison` = `prisonfix.lvl`.** The game boots straight
into the dungeon, which is where Prince of Persia 3D opens. It does *not* boot
into `title` or `NIS`; the retail build goes directly to gameplay.

`sFinaleModuleClass` is a `gameModule` named `finale`, consistent with the vtable
diff in `engine-structure.md` showing it shares five of six slots.

The transition itself: `dword_7BE590` holds the target id, `dword_7BE588` the
previous, and `Game__FirstFrameInit` logs `"GOTO:%s FROM %s"` while switching.

## Solved this pass: where the Prince starts

Two mechanisms exist and levels use different ones.

* **`.lvl` `[Prince]` inside a `[Region]`** — `"<body> x,y,z,heading"`, heading
  in **degrees**. Live in `prisonfix`, commented `;cell`.
* **A `PrinceStart` trap** in room geometry, resolved by body name. Used by
  Cistern, whose `[Prince]` line is commented out.

**Correction.** `actor-spawn.md` previously concluded `[Prince]` was a dead
authoring leftover. That was inferred from Cistern alone and is wrong: it is the
live source in the game's first level. Both paths are now implemented, `[Prince]`
first with the trap route as fallback.

## State

| step | status |
|---|---|
| `distribution.dat` config | **done** (`src/engine/config.*`, `make config`) |
| window / GL / main loop | **done** |
| module table + startup id | **decoded**, not yet wired into `module_manager` |
| level load | **done** — `prisonfix.lvl`: 9 rooms, 6 bodies |
| enemy spawn | **done** — 10 enemies in prisonfix |
| Prince spawn | **done** — body 0 at (22, 0.01, 1.92), heading 83.25 deg |
| title / intro | not started; needs the video decoder |
| player control, animation | blocked on the `.act` hierarchy (`docs/formats/act.md`) |

## Next, in order

1. **Wire the module table into `module_manager`.** The ids and levels are known;
   build the table, honour a startup id of `0x0C`, and let `gameModule` own the
   scene. This replaces the viewer's ad-hoc load path with the original's.
2. **Region streaming.** `prisonfix` has two `[Region]`s with separate room lists
   and separate `[Prince]` entries. The loader currently flattens every region
   into one scene. The original clearly loads a region at a time.
3. **Camera.** Placement is still geometry-blind; see the open item below.
4. **`.act` child records** — the animation blocker, unchanged.
5. **Intro** — self-contained, gated on QuickTime/SVQ1.

## Implemented

1. **Module table** — `src/engine/module_table.*`, the 20 recovered entries kept
   in `strcmpi` order so an index *is* the original's id.
   `module_table_verify()` re-checks the ordering and that `0x0C` still lands on
   `prison`, rather than trusting the literal. `make modules`.
2. **Region streaming** — `load_level(..., region)`; `--region N`, default 0.
   `prisonfix` region 0 (`Parts1to3`) loads 5 of 9 rooms and 4 of 11 actors,
   instead of flattening both regions into one scene.
3. **`--boot`** — does what WinMain does: read `distribution.dat`, resolve
   `MODULE_ID_STARTUP` to its level, load region 0, spawn. No hardcoded path.

```
boot: 7 config flags, startup module 0x0C = 'prison'
boot: level geometry\rooms\prisonfix\prisonfix.lvl (region 0)
  region selected: rooms loaded 5, missing 0
  [Prince] body 0 (Prince) at (22.0, 0.0, 1.9) heading 83.2 deg  26 meshes
  actors: 4 spawned (prince index 0)
```

## Resolved: the empty first-level view

Not the camera and not region streaming. **The `[Fog]` distances were being read
as metres when they are fractions of the far clip plane**, putting fog end at 1.5
units and drowning every level in its fog colour. See `docs/formats/lvl.md`.

The same bug was behind Cistern "rendering almost nothing" — that level's fog is
green, which is why it looked like one giant untextured green mesh. Both scenes
render correctly now.

## Open
* `menu.dat`'s `5 3 1` and the trailing ints on the `gameModule` constructor
  (`"finale", lvl, 4, 0, 0, -1`) are undecoded. The ints plausibly select a
  region or start index, which would connect to point 2 — but that is a guess.
