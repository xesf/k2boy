# k2boy

An open-source reimplementation of the engine of **Prince of Persia 3D** (Red Orb Entertainment,
1999), rebuilt from the original binary with IDA Pro. It plays the retail game's own data on
macOS and the PlayStation Vita, and in a browser.

The original's internal codename was **K2** (`C:\k2\K2boy\Source\...`), hence the name.

> **You need your own copy of the game.** No game data is in this repository: it is read at run
> time from a retail install that you provide. k2boy is not affiliated with or endorsed by Ubisoft,
> Broderbund, Red Orb Entertainment or Jordan Mechner.

---

## Contents

- [Status](#status)
- [Getting started](#getting-started)
- [Controls](#controls)
- [PlayStation Vita](#playstation-vita)
- [Web runtime](#web-runtime)
- [Developer tools](#developer-tools)
- [How it works](#how-it-works)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Principles](#principles)
- [Credits and licence](#credits-and-licence)

---

## Status

Playable from the title screen through the first level (the prison, *prisonfix*). It is a work in
progress: some systems follow the binary exactly, and others are stand-ins until that part is
reversed. The docs say which is which.

| Area | State |
|---|---|
| Boot | The original's module manager: Red Orb logo movie → title screen → level |
| Front end | Title screen, in-game menu, Options (sound volume, difficulty, controls), Load/Save, Cinematics, Credits, drawn from the original art with its own TrueType font |
| Levels | `.lvl` regions, every `.K2A` room, materials and textures, fog and clip planes |
| Prince | Skeleton and skills from `.act`/`.skl`, walking, running, turning, jumping (the original's launch velocities and gravity), ledges (climb, hang, shimmy, pull-up), crates (push, pull, climb on), swimming and diving, picking things up |
| Combat | Sword drawn and sheathed, attacks, blocks, injuries, deaths; guard AI and timing from `fight.ini` and `enemy.dat`; health in bottles |
| Traps and scripts | The trap message language: doors, plates, areas, movers, water raising and lowering, cutscene cameras, fixtures (prisoners, rats, levers, cranes), pickups and potions, pit spikes |
| Effects | NetImmerse 2.3 `.nif` particle effects (potions, SpecialEffect traps) |
| Audio | `.sfx` sounds, positional ambiences, skill sound keys, a software mixer |
| Cutscenes | QuickTime `.mov` with Sorenson Video 1 and QDesign Music 2, all decoded in-tree |
| Saves | Our own text format under `~/.k2boy`, not yet the original's `.pop` |

Not yet: levels after the prison, the original's save format, the bow, graphics options and key
rebinding, and some trap types.

## Getting started

### Requirements

- **The game data:** a retail *Prince of Persia 3D* install, copied to `data/` at the repository
  root (or anywhere, passed as `DATA=`). Paths inside it are matched case-insensitively.
- **macOS:** the Xcode command line tools and CMake 3.16 or later. Nothing else: there are no
  third-party libraries.

### Build and play

```sh
make play                  # optimised build, then the game from the Red Orb logo
make play MENU=0           # straight into the level, no front end
make play CONTROLS=classic # the original's tank controls
make play REGION=1         # start in another region of the level
make play TRACE=1          # print every trap message
make play ARGS="--mute"    # pass extra flags
```

`make` on its own builds the Debug configuration; `make play-debug` plays it. `make help` lists
every target.

Saves, settings and logs go to `~/.k2boy/`.

## Controls

Two schemes, switched with **Tab** in game or in Options:

- **Modern:** movement is relative to the camera, and the mouse or the right stick turns the
  camera. A full stick push runs.
- **Classic:** the original's tank controls. W and S walk along his facing, A and D turn him,
  and the camera stays behind.

| Action | Keyboard | Gamepad (Xbox / PlayStation / MFi) |
|---|---|---|
| Move | WASD / arrows | left stick, d-pad |
| Camera | mouse drag | right stick |
| Run | Shift | RB / RT |
| Jump | Space | A |
| Action: grab a crate, climb, talk, pull levers, pick up | E | X |
| Climb onto a crate | E while pushing toward it | X + forward |
| Crouch (hold); move to crawl through low gaps | C | B, with the sword away |
| Draw / sheathe sword | R | Y |
| Attack (left / right / centre by the stick) | F | B |
| Block | G | LB |
| Game menu | Esc | Start |
| Motion camera (pads with a gyroscope) | — | Select |
| Free-fly camera / trigger volumes | V / T | — |

**Ledges:** E at a ledge in reach climbs it. Jump under a higher one to catch it, then press up to
pull up, down to let go, and left or right to shimmy. **Swimming:** in deep water, move to swim, E
dives, Space rises. The bottle at the bottom left is your air.

## PlayStation Vita

A native port: a GXM renderer, both touch panels, the gyroscope, and work spread across three
CPU cores.

```sh
make vita                  # → build-vita/k2boy.vpk  (VitaSDK at $VITASDK or ~/vitasdk)
```

1. Install `k2boy.vpk` with VitaShell.
2. Copy the game data to `ux0:data/k2boy/data/`.
3. The first launch compiles the two shaders with the system's `ur0:data/libshacccg.suprx` and
   saves them to `ux0:data/k2boy/shaders/`. Packages built after those files are added to
   `platforms/vita/shaders/` carry them, and need no compiler.

| Vita | Game |
|---|---|
| Left stick / d-pad, right stick | move, camera |
| Cross / Circle / Square / Triangle | jump / attack (crouch with the sword away) / action / draw sword |
| L / R | block / run |
| Start / Select | game menu / motion camera |
| Front touch | tap menus, drag to look |
| Rear touch | drag to look |

Logs go to `ux0:data/k2boy/log.txt` and `err.txt`. Details: [`docs/plan/07-vita.md`](docs/plan/07-vita.md).

## Web runtime

`web/` holds a second runtime for the same data, in vanilla JavaScript and WebGPU, with no build
step and no dependencies:

```sh
make web                   # then open http://localhost:8080/web/
```

See [`web/README.md`](web/README.md) and [`docs/plan/06-web-experiment.md`](docs/plan/06-web-experiment.md).

## Developer tools

The binary doubles as a toolbox. Most of these run headless:

```sh
make check                 # mount the VFS, decode every image, parse every level and .K2A
make dump F=Actors/PICK_Gem/PICK_Gem.K2A
make skl F=Actors/Prince/Skills/Running_L.skl
make room R=geometry/rooms/prisonfix/prison1.k2a
make actor A=geometry/prince/master.dat
make levels | skills | actors | spawns | materials | skeletons | modules
```

Scripted, deterministic runs, used to check gameplay changes with screenshots:

```sh
./build/k2boy --boot --notrig --place -66.07,20.25,29.25,0 \
    --hold _@0.1-0.2 --hold W@1.8-2.1 --fixeddt 0.05 --frames 100 --shot out.bmp data
```

| Flag | |
|---|---|
| `--place x,y,z,yaw` | put the Prince somewhere |
| `--hold KEYS@from-to` | hold keys between two times (`_` is Space, `^` is Shift) |
| `--send "target,message"` | send a trap message, as a script would |
| `--fixeddt S --frames N --shot F.bmp` | step N frames of S seconds, then save a screenshot |
| `--movie NAME` | play a cutscene |
| `--trapmsg` | log every trap message |
| `--noclip`, `--noanim`, `--mute`, `--notrig` | collision, animation, sound and trigger volumes off |

[`docs/reference/command-line.md`](docs/reference/command-line.md) covers the original game's
own flags.

## How it works

*Prince of Persia 3D* is not a bespoke engine. It is three middleware stacks glued together by
Red Orb's K2 layer:

| Layer | Original | Here |
|---|---|---|
| Scene graph and renderer | NetImmerse 2.3 (NDL) on Direct3D | our scene graph; OpenGL 3.3 or GXM behind `gfx.h` |
| Characters | Motion Factory *Motivate* (actors, skills, planner) | `.act` / `.skl` readers, skeleton posing, the Prince's states |
| Game logic | Red Orb K2: traps, `PrinceObject`, `CameraLogic` | `engine/` and `main.cpp`, ported function by function |
| Video | QuickTime: Sorenson Video 1, QDesign Music 2 | in-tree demuxer and decoders |
| Audio | Aureal A3D 2, DirectSound | a software mixer behind `platform.h` |

The retail binary is a Visual C++ 6 build with full RTTI, so 818 class names survive and almost
every vtable can be named. The work happens in IDA through an MCP server: find the original
function, port it, rename it in the IDA database to match, and compare the behaviour in game.
Where the binary has not been traced yet, the code says that what it does is our own.

The engine is C++11, split behind two small interfaces:

- [`src/platform/platform.h`](src/platform/platform.h): window, input, time, audio, files,
  threads. Implementations: macOS (Cocoa) and Vita.
- [`src/gfx/gfx.h`](src/gfx/gfx.h): rendering, modelled on the NetImmerse property set.
  Implementations: OpenGL 3.3 and GXM.

A new platform means another implementation of each, with nothing above them changed.

## Repository layout

```
src/
  main.cpp        the game: boot, scenes, the Prince, traps, combat, camera, HUD
  engine/         scene graph, collision, trap scripts, mixer, fight tuning, job pool
  assets/         .K2A, .act, .skl, .lvl, master.dat, .sfx, .nif, BMP/TGA readers
  media/          QuickTime demuxer, inflate, SVQ1 and QDM2 decoders
  gfx/            gfx.h, OpenGL and GXM backends, TrueType rasterizer
  ui/             2D canvas, GameText, front-end screens
  platform/       platform.h, macOS and Vita backends, case-insensitive VFS
platforms/vita/   Vita CMake project, LiveArea template, packaged shaders
web/              the WebGPU runtime
docs/             reverse-engineering notes (below)
tools/            helper scripts
data/             your retail install (git-ignored)
```

## Documentation

[`docs/README.md`](docs/README.md) indexes everything. The main entry points:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): binary inventory, subsystem map, data flow
- [`docs/formats/`](docs/formats/): every file format (`.K2A`, `.act`, `.skl`, `.lvl`, `.nif`,
  `.sfx`, `.mov`, saves, text and tuning files)
- [`docs/reference/`](docs/reference/): recovered systems: class hierarchy, collision, trap
  scripts, crates, jumps, combat, the trap factory, the scripting VM
- [`docs/plan/`](docs/plan/): the roadmap, the IDA setup and the platform plans

## Principles

- **The binary is the spec.** Behaviour is taken from the disassembly. Where it has not been
  traced yet, the code says so.
- **No third-party dependencies.** Codecs, formats and the font rasterizer are written in-tree or
  ported in-tree from GPL-3 sources. Only the platform's own system libraries are used.
- **No game data in the repository.** Everything comes from the user's install at run time,
  including the Vita's LiveArea art, which is generated at build time.

## Credits and licence

k2boy is licensed under the **GNU General Public License v3.0**: see [`LICENSE`](LICENSE).

Code ported in-tree from other GPL-3 projects:

- the Sorenson Video 1 and QDesign Music 2 decoders, adapted from
  [ScummVM](https://www.scummvm.org/);
- the QuickTime demuxer, inflate and TrueType rasterizer, from the author's own *X-Files* engine
  reimplementation.

*Prince of Persia* is a trademark of Ubisoft Entertainment. The game, its data and its art belong
to their respective owners.
