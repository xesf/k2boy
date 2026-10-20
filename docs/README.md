# Prince of Persia 3D — Reverse Engineering Notes

Working documentation for reverse engineering *Prince of Persia 3D* (Red Orb / Broderbund, 1999)
from this retail install, paired with IDA Pro Free + the `ida-mcp` MCP server.

## What this game is, technically

POP3D is **not** a bespoke engine. It is three third-party stacks glued together by Red Orb code
whose internal codename is **K2** (source tree was `C:\k2\K2boy\Source\...`):

| Layer | Technology | Evidence |
|---|---|---|
| Renderer / scene graph | **NetImmerse 2.3** (Numerical Design Ltd) | `.nif` header string; ~40 `Ni*` RTTI classes |
| Character middleware | **Motion Factory "Motivate"** (actor / skill / planner) | ~400 `Mf*` RTTI classes |
| Scripting | **Piccolo** VM — linked in but **never executed** by the retail game | see [ARCHITECTURE §5.1](ARCHITECTURE.md#51-the-retail-game-runs-no-hfsms-and-no-scripts) |
| Game logic | Red Orb **K2** layer | `k2*` / `K2*` classes, `Trap`, `PrinceObject`, `CameraLogic` |
| Audio | Aureal A3D 2 + DirectSound + WaveMix | `data/a3dapi.dll`, `A3DSource`, `DS3DSource`, `data/mohawk.w32` |
| Video | QuickTime `.mov` (compressed `moov`, zlib) | `data/Movies/*.mov` |

The main binary is a Visual C++ 6.0 build **with full RTTI retained** — 818 mangled class names
survive in `.data`. This is the single biggest lever for this project: nearly every vtable in the
binary can be attributed to a named C++ class without guessing.

## Repository layout

```
docs/          these notes — the tracked content
src/           engine source (platform + gfx interfaces scaffolded)
web/           a second runtime: vanilla JS + WebGPU, playable (see plan/06)
data/          the retail install (git-ignored; not redistributable)
```

Every path in these docs is written relative to the repository root, so `data/pop3dreal.exe`,
`data/Actors/Prince/Skills/`, and so on. Engine-internal paths keep their original backslash
form (`geometry\rooms\prisonfix\prison3.k2a`) because that is what the binary contains.

## Contents

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — binary inventory, subsystem map, class taxonomy, data flow
- [`formats/`](formats/) — file format specifications
  - [`00-overview.md`](formats/00-overview.md) — extension census + confidence table
  - [`act.md`](formats/act.md) — `.act` Motivate actor definition (`APFM`)
  - [`skl.md`](formats/skl.md) — `.skl` Motivate skill / animation (`kSFM`)
  - [`k2a.md`](formats/k2a.md) — `.K2A` ASCII geometry export (ASE dialect)
  - [`movies.md`](formats/movies.md) — `.mov` cutscenes: QuickTime + SVQ1 + QDM2
  - [`nif.md`](formats/nif.md) — `.nif` NetImmerse 2.3 scene
  - [`lvl.md`](formats/lvl.md) — `.lvl` level description + `.nbr` neighbour data (both text)
  - [`sfx.md`](formats/sfx.md) — `.sfx` PCM sound with loop points
  - [`behavior.md`](formats/behavior.md) — `.bhvr` HFSM text + `.pobj` Piccolo bytecode (`MJSP`)
  - [`linkmap-save.md`](formats/linkmap-save.md) — `*_linkmap.txt` + `*_save.bin` object persistence pair
  - [`savegame.md`](formats/savegame.md) — `.pop` savegame / `data/NIS.sav`
  - [`text-and-tuning.md`](formats/text-and-tuning.md) — `data/GameText.txt`, `data/Traps.txt`, `data/fight.ini`, `data/enemy.dat`, `data/camera.dat`, `*.dat` CLI files
- [`plan/`](plan/) — the RE plan
  - [`00-roadmap.md`](plan/00-roadmap.md) — phased plan with exit criteria
  - [`01-ida-setup.md`](plan/01-ida-setup.md) — which binary to load, how to prep the database
  - [`02-ida-mcp-playbook.md`](plan/02-ida-mcp-playbook.md) — concrete MCP tool recipes
  - [`03-reimplementation.md`](plan/03-reimplementation.md) — porting strategy and recommended tech stack
  - [`04-platform-layer.md`](plan/04-platform-layer.md) — macOS + OpenGL backend, swappable platform/gfx split
  - [`05-boot-to-gameplay.md`](plan/05-boot-to-gameplay.md) — what happens between launch and player control
  - [`06-web-experiment.md`](plan/06-web-experiment.md) — the `web/` runtime: WebGPU, derived ledges, playable
- [`reference/`](reference/)
  - [`coverage.md`](reference/coverage.md) — what is reversed, by format and subsystem
  - [`class-hierarchy.md`](reference/class-hierarchy.md) — 909 vtables with recovered inheritance
  - [`collision.md`](reference/collision.md) — the pick primitive behind every floor and wall test
  - [`trap-scripts.md`](reference/trap-scripts.md) — the message language that drives level progression
  - [`crates.md`](reference/crates.md) — push and pull: the step, the floor probe, the clips
  - [`jumps.md`](reference/jumps.md) — jump states, the tuning block and its defaults
  - [`trap-factory.md`](reference/trap-factory.md) — the 33 trap registry keys + registration mechanism
  - [`script-api.md`](reference/script-api.md) — the Piccolo scripting API: 29 classes, 250 methods
  - [`vm-isa.md`](reference/vm-isa.md) — the Piccolo VM: interpreter, machine model, 89-opcode map
  - [`command-line.md`](reference/command-line.md) — flag dispatcher; why there is no console command table
  - [`chunk-registry.md`](reference/chunk-registry.md) — all 78 serializable 4CC types with sizes
  - [`rtti-classes.txt`](reference/rtti-classes.txt) — raw mangled class names from `data/pop3dreal.exe`

## Status

The **format** docs were derived by static inspection of the install directory; each field is
marked **Confirmed** (validated across many files) or **Inferred** (single-sample guess). None of
the Inferred fields has been checked against decompiled loader code yet — that is Phase 2.

The **reference** docs are recovered from `data/pop3dreal.exe` itself and are confirmed:

- `class-hierarchy.md` — 909 vtables, 812 classes, full inheritance (Phase 1 complete;
  853 renamed in the IDA database)
- `trap-factory.md` — all 33 trap registry keys, registration mechanism decompiled
- `script-api.md` — 29 script classes, 250 methods, 241 parameters, 83 properties
- `vm-isa.md` — interpreter at `0x6CF883`, 89 opcodes mapped, machine model decoded
- `chunk-registry.md` — 78 registered 4CC chunk types with creator, `sizeof` and class

Savegames (`.pop`, `NIS.sav`) are **big-endian** throughout — integers, floats and the UTF-16
name. Every asset format is little-endian. See [`formats/savegame.md`](formats/savegame.md).

Build identified: `K2Boy build: Aug 22 1999, 17:23:42 BuildMachine - D3D`, 39 652 functions.
