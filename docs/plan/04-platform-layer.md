# Platform layer — macOS first, backends swappable

**Decision (this revision):** target macOS natively with Cocoa + OpenGL, behind an abstraction
that keeps two things independently replaceable — the **platform** (Cocoa ↔ SDL ↔ Win32) and the
**graphics backend** (OpenGL ↔ Metal ↔ WebGPU).

## The design constraint that matters

The temptation is one interface that does both windowing and rendering. SDL encourages this:
`SDL_GL_CreateContext` ties context creation to the windowing layer, so swapping the renderer
means touching platform code.

Keep them apart:

```
  Game
   │
   ├── Platform   window, events, time, audio sink, file access
   │              exposes: native handle, makeCurrent(), swapBuffers()
   │
   └── Gfx        pure rendering. Never includes a platform header.
```

Context creation stays on the platform side because it is inherently platform-specific
(`NSOpenGLContext` on macOS, `wglCreateContext` on Windows, `SDL_GL_CreateContext` under SDL).
The renderer only ever touches the GL API. A future Metal backend asks the platform for a
`CAMetalLayer` instead of a GL context — one new platform entry point, no renderer rewrite.

## macOS specifics worth knowing up front

| Topic | Reality |
|---|---|
| OpenGL version | **4.1 core is the maximum** on macOS. Target **3.3 core** — ample for this content, and close to WebGL2 if a web build is ever wanted. |
| Deprecation | OpenGL has been deprecated since macOS 10.14. It still works, including on Apple Silicon via translation. Fine for now; the abstraction is the hedge. |
| Language | Cocoa needs Objective-C. Platform backend files are **Objective-C++ (`.mm`)**; everything above stays plain C++11. |
| Frame pacing | `CVDisplayLink` is deprecated, `CADisplayLink` on macOS is recent. **Avoid both** — run a plain loop and get vsync from `NSOpenGLCPSwapInterval`. Fewer moving parts, no API churn. |
| HiDPI | Set `wantsBestResolutionOpenGLSurface`; drawable size ≠ window size. Report both. |
| Audio | CoreAudio `AudioUnit` render callback (`kAudioUnitSubType_DefaultOutput`). Pull-model, matches the interface below. |
| Gamepads | `GameController.framework` handles Xbox/PlayStation pads natively. |
| Packaging | `.app` bundle; resolve `data/` via `NSBundle` resource path, with a dev-mode fallback to a path relative to the executable. |

## Why the game content forces a VFS

Not optional, and worth building before anything else touches files:

- extensions vary in case **inside a single directory** — `.K2A`, `.k2a`, `.K2a` all occur
- engine-internal paths are Windows-style backslash: `geometry\rooms\prisonfix\prison3.k2a`
- `.lvl` `[Bodies]` entries use doubled backslashes: `geometry\\prince\\master.dat`
- lookups must be case-insensitive on a case-sensitive volume

So every path from game data needs normalising: backslash → slash, collapse doubles, then
case-insensitive resolution against an index built once at startup. This is also why
`std::filesystem` would not have helped, and why C++11 costs nothing here.

## Interfaces

Scaffolded in `src/platform/platform.h` and `src/gfx/gfx.h`. Deliberately small — the whole point
is that a second backend is a weekend, not a rewrite.

Rules:

- `gfx.h` must never include a platform header, and vice versa.
- No C++ in the interface beyond what C++11 gives for free: POD structs, enums, plain functions.
  Keeps a C or Objective-C implementation possible.
- No allocation in the interface. Callers own their buffers.

## Build

CMake, C++11, warnings-as-errors. macOS-only for now:

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Frameworks to link: `Cocoa`, `OpenGL`, `AudioToolbox`, `GameController`.

Keep the asset library (Track B in [`03-reimplementation.md`](03-reimplementation.md)) as a
separate target that links **neither** platform nor gfx — it must stay usable as a standalone
converter and test harness.

## What is deliberately not decided yet

- SDL2 vs SDL3 — moot while the backend is Cocoa, and reversible behind this interface.
- Metal — additive later; needs one platform entry point returning a `CAMetalLayer`.
- Windows and Linux backends — the interface is the commitment, not the port.
