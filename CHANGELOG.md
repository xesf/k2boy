# Changelog

One line per change, newest first within each date.

## 2026-11-03
- Vita: LiveArea icon, startup and background now come from the Prince of Persia 3D art in `platforms/vita/art`.

## 2026-10-26
- Guards notice the Prince as the original does: range, view cone, line of sight, footsteps and a drawn sword; they lose him out of sight.
- Vita build keeps a margin after the code segment for `vita-elf-create`.
- In a fight the camera swings behind the Prince along the line to the guard.
- Each guard uses his own clip set, so guards die and get hurt with their own animations.
- The sheathed sword rides in the scabbard; drawing and sheathing end at the clips' own markers.
- Door and platform animations turn about the placement's axes; hidden Kill/Trigger boxes are no longer drawn.

## 2026-10-23
- Walk key (Shift / pad LT) for careful steps, back steps and sidesteps; running is the default.
- Small drops fall quietly, clips slide along walls, guards keep to the floor.
- Collision tests moving geometry node by node; `K2_PROFILE` prints casts and milliseconds per frame.
- Saves keep the level's state: traps, doors, crates, pickups, dead guards, props, bow and arrows.
- Prince climbs and slides poles, catches and swings on bars, and grabs swinging ropes.
- Flamethrowers, floor spikes, fireball launchers, arrow chargers and darts at the traced speed and damage.
- Solid fixtures (carts, dropped crates) collide; Movers start their motions when the level opens.
- Keyframed clips leave unkeyed joints posed; the Prince rides moving platforms.
- `Prince,kill:<clip>` script message and `K2_SCRIPTAUDIT` listing unhandled script targets.

## 2026-10-21
- Prince runs with start/stop steps and quick turns, from the traced locomotion states.
- Combat has aimed blocks, counters, feints, clashes, finishing moves, taunts and queued presses.
- Bow and arrows, archers, and deaths by cause (drowning, falls, blades, grinders, crushers).
- Action traps: levers, cranes, the cart, the crusher, Assan's mouth.
- Collapsing floors, dart launchers, water currents, Messenger state machines and the Abyss.
- Falls hurt and walking off an edge falls.
- The level's exit leads to the next level; saves, loads and restarts keep their level.
- Script messages for Prince hurt, weapons, enemy forwarding, WalkAt, and security and look cameras.
- Pickups have reach, drink/inspect and finish clips; only the sword arms him.
- Guards start at their trap's pivot; crates fire CrateTarget scripts.

## 2026-10-20
- Skills read as the original's object graph; locomotion, collision and chase camera work.
- Trap message scripts run: areas, delays, gates, pressure plates, doors with keyframe skills.
- Push and pull crates; jumping with the original's launch velocities and gravity; ledges.
- Gamepad support and Modern/Classic control schemes.
- Game sounds play: scripts, ambiences and skill keys.
- Guards fight and the Prince takes up the sword; fight timing from `fight.ini`.
- Health as potion bottles; pickups and potion effects; Item messages carry objects.
- Swimming on the surface, diving and under water on one bottle of air.
- Fixture actors (prisoners, rats, props) and keyframed (SklK) skills.
- NetImmerse 2.3 `.nif` reader and the visual effects it drives.
- TrueType rasterizer for the game's UI font.
- Cutscene playback: QuickTime demuxer, Sorenson Video 1 and QDesign Music 2 decoders, `--movie NAME`.
- Title screen, game menu, options, load/save, cinematics and credits.
- Crouch and crawl through low gaps; shower spray falls.
- PlayStation Vita platform: GXM renderer, touch, motion, three cores.
- Parallel level loading, frustum culling, worker-core poses and a loading screen.
- Movies stream from disk; file lookups on demand; cached file index; faster number parsing.
- Level loading includes every region's rooms.
- README describing the project, building, controls, Vita and tools.

## 2026-08-28
- Boot driven through the module manager.
- Skeleton hierarchy recovered; body parts bound to joints and posed; forward kinematics reproduces the bind pose.
- Prince walks with a chase camera.
- Level parser fixes for section headers with trailing text and quoted bodies.

## 2026-08-21
- Initial commit and web experiment.
