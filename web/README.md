# k2boy web

A second runtime for the same retail install: HTML, vanilla ES modules, WebGPU.
No build step, no dependencies, no `node_modules`.

Design notes and the roadmap: [`../docs/plan/06-web-experiment.md`](../docs/plan/06-web-experiment.md).

## Run

```
make web
```

from the repository root, then open <http://localhost:8080/web/>.

Needs a WebGPU browser — Chrome 113+, Edge 113+, or Safari 18+.

`make web` regenerates `web/data-index.txt` first. That file is the VFS index:
a browser cannot list a directory, so the walk `src/platform/vfs.cpp` does with
`opendir` at mount time has to be produced ahead of time. It is derived from
`data/` and git-ignored; `make webindex` regenerates it on its own.

If the manifest is missing the page falls back to a folder picker, which works
with no server at all — point it at your own copy of the install.

## URL parameters

| | |
|---|---|
| `?module=cistern` | boot a module by name instead of the startup id (`0x0C`, `prison`) |
| `?level=<path>` | load a `.lvl` directly |
| `?region=1` | which `[Region]` to stream in (default 0) |
| `?free` | start in the free-fly camera |

## Controls

WASD move (camera relative) · Space jump, tap at a ledge to grab · Shift walk ·
mouse or drag to look · wheel for camera distance · hold R to rewind ·
F1 HUD · F2 free camera · F3 collision · F4 ledge graph · F5 respawn · T trap
volumes · P pause · H help.

## Layout

```
js/core/     vfs, math, input, hud            platform-ish, no game knowledge
js/assets/   k2a, lvl, image                  ports of src/assets/, no gfx dependency
js/gfx/      gfx, shaders                     the WebGPU backend; second implementation of src/gfx/gfx.h
js/engine/   module_table                     ported from src/engine/
js/game/     world, collision, ledges,        the part with no original to port from
             player, camera, rewind
js/main.js   boot and the frame loop
```

The dependency rule from the native side holds here too: `assets/` never
imports `gfx/`, and `gfx/` never imports anything above it.

## Debugging

Once a level is up, `k2` in the console is the live engine state — `k2.player`,
`k2.world`, `k2.camera`, `k2.rewind`, plus `k2.probe` for the collision queries
the character runs on:

```js
k2.player.y += 5                       // drop test
k2.probe.floor_under(x, y, z, 8)       // is there a floor here?
k2.probe.ledge_at(x, y, z, fx, fz)     // what would he grab from here?
k2.show_ledges = true                  // same as F4
```
