# Reimplementation strategy

Written against the state of the analysis in `docs/` as of this revision. Opinionated, with the
reasoning stated so it can be argued with.

---

## 0. What is actually missing — almost nothing

Two earlier revisions of this document were wrong about this, in opposite directions. Setting the
record straight:

`data/` is a **complete retail install** (6 543 files, 949 MB). Level data is present — 20 `.lvl`
levels and 8 `.nbr` files, both formats decoded *and* validated against the real files.

The only genuinely absent things are `CONFIG.ENT` (a minor tuning file) and the Motivate
behaviour data — and the behaviour data turns out **not to be needed**.

## 1. The retail game does not use Motivate HFSMs or Piccolo scripts

This is the most consequential finding for a reimplementation, and it inverts the risk assessment
in earlier revisions of this plan.

The evidence, all from the shipped binary and install:

| Check | Result |
|---|---|
| `.lvl` `-f` flag (loads FMS behaviour data) | **No shipped level uses `-f`.** Flags used across all 20 levels: `a b k l n t v w` |
| `"FMSdata.fms"` (`0x7BFE14`) | referenced only from `0x44192A` — inside the `-f` handler, which is never reached |
| `*.fms` files on disk | **zero** |
| `Actors/*/Behaviors/` directories | 14 exist, **13 are empty**; only `BlankCamera` has content |
| `.bhvr` files | 1, trivial (`camera_setting{...}`, 76 bytes) |
| `.pobj` files | 1, 128 bytes, compiles to nothing |
| `"PiccoloRT.dll"` (`0x7D2BF4`) | **zero code references** — a dead string, never `LoadLibrary`d |

So the Motivate authoring pipeline — HFSMs in `.bhvr`, compiled to `.fms`, executed by the Piccolo
VM — is **present in the engine but unused by the shipped game**. Red Orb used Motivate for its
actor, skill and planner machinery and wrote the actual game logic in C++.

Where the logic really is:

- **`CharacterStateTable`** — a native state table. Its vtable is installed from `0x40BB1D`,
  `0x44EF5B` and `0x44F014`, all in the low K2 game-code range, not the Motivate range. Both
  `PrinceObject` and `Trap` derive from it.
- **The `K2*Activity` family** — `K2LocoActivity`, `K2ActivityRun`, `K2SwimActivity`,
  `K2WadeActivity`, `K2SlideActivity`, `K2AttachActivity`, `K2TurnGesture`, `FallToFloorActivity`
  and the rest, all registered as chunk types with creators at `0x439406`–`0x4A3292` — again game
  code. See [`../reference/chunk-registry.md`](../reference/chunk-registry.md).
- **`EnemyAIObject`**, the `Trap` subclasses, `PrinceMotion`, `PrinceFloor`.

### What this means

**The gameplay logic is in the executable, in C++, and is therefore recoverable by exactly the
reverse engineering these docs already do.** It is not locked in a middleware data format we
cannot read.

Consequences for the plan:

- The Piccolo VM work ([`../reference/vm-isa.md`](../reference/vm-isa.md)) is **not** on the
  critical path. It documents a subsystem the retail game never runs. Interesting for
  completeness; not required to ship.
- Decoding `.fms` is likewise **not required**. Deprioritise it.
- The scripting-language question in §3 is **moot** — there is no script layer to replace.
- The real work is reversing `CharacterStateTable` and the `K2*Activity` set, plus resolving the
  `.skl` animation channels. Both are ordinary RE against code and data we have.

The remaining hard part is unchanged in nature but smaller in scope: the Motivate **planner**
(`MfLocoPlanner`, `MfManipActivity`, and the two largest objects in the registry —
`MfHoldActivity` at 1096 bytes and `MfCompliantActivity` at 1084) still sits underneath the K2
activities and still has to be understood behaviourally. But it is middleware being *driven* by
K2 code you can read, not a black box executing data you cannot.

## 2. Three tracks, honestly costed

### Track A — Compatibility layer. Weeks. Do this first.

Keep `pop3dreal.exe` and shim the OS layer. The analysis says this is unusually tractable:

- **DirectDraw4 + Direct3D immediate mode** (`"Render create failed: DirectDraw4 Query failed"`),
  the generation `dgVoodoo2` was built for.
- Only **2 DDRAW, 2 DINPUT, 1 DSOUND** imports — everything else is COM-created through `ole32`.
- Explicit 3dfx detection (`Voodoo`, `Voodoo2`, `Voodoo3`, `Banshee`, `Voodoo Rush`) with a HEL
  path that deliberately fails: `"Software DirectDraw has no 3D capabilities"`. So it *requires*
  hardware 3D and will not fall back to software.
- `pop3dreal.exe` already bypasses SafeDisc, so no protection work is needed.

Route: Wine/Proton + DXVK, or `dgVoodoo2` on Windows, or Wine + dgVoodoo2 on macOS/Linux.
Audio: `a3dapi.dll` (Aureal A3D) is dead hardware — force the DirectSound path via `mohawk.w32`
(`[DirectSound] fEnable = TRUE`).

**Value:** playable now, zero reverse-engineering risk. **Limit:** preserves the binary, not the
knowledge. Does nothing for maintainability.

### Track B — Asset toolchain. Months. Highest preservation value.

A library + CLI that reads every format, independent of any engine. We are already most of the
way there:

| Format | State | Work left |
|---|---|---|
| `.sfx` | **confirmed**, validated 100% | none — write the converter |
| `.K2A` | **confirmed** (3DS Max ASE dialect) | parse `*NODE_HAS_K2_DATA`, `*GEOM_FORMAT` |
| `.act` | property table **confirmed**, validated on 118 files | node / articulation block |
| `.skl` | `JKey` record **confirmed** (212 B, validated on 11 566 records) | **the 7-float channel meaning** |
| `.nif` | externally documented | use `pyffi`/`nifly`; check custom `NiExtraData` |
| `.pop` / `NIS.sav` | framing + 5 of 11 sections confirmed | `stream buffer`, `module`, `resource list` |
| linkmap / save.bin | linkmap **confirmed** | `save.bin` state stream |
| `.lvl` / `.nbr` | **confirmed**, validated on 20 + 16 files | none |
| `.fms` | unknown | everything |

**The blocking item is the `.skl` 7-float channel.** Until that is resolved you cannot play a
single animation, and animation is the entire game. It is also the cheapest remaining win:
`MfJointKeyframe`'s deserializer, or brute-force fitting against a known pose in a `.K2A`
skeleton.

**Value:** survives any engine decision, enables a viewer, forces the remaining RE. **Do this
regardless of which endgame you pick.**

### Track C — Native reimplementation. Years. Only after B.

Rebuild the K2 layer over a modern renderer, reusing Motivate *semantics* rather than its code.
Gated on: complete CD data, `.skl` channels decoded, `.fms` decoded, and enough of
`MfLocoPlanner` understood to make the prince move like the prince.

Static recompilation (the N64-recomp approach) is **not** viable here: 39 652 functions of MSVC 6
x86 against three middleware layers, with the game logic in data rather than code. It would
reproduce the bugs without producing anything maintainable.

## 3. Recommended stack

Optimising for: **playable on modern hardware, maintainable for decades, and archivable.**

### Language — C++11 is enough

**Yes, C++11 suffices.** The recovered model is a C++ class hierarchy
(`docs/reference/class-hierarchy.md`, 909 vtables with inheritance), and mirroring those names
keeps code and documentation mutually checkable — a reader can hold
`PrinceObject : CharacterStateTable : CharacterObject : NiCollisionSwitch : NiNode` in both places
at once. C++11 expresses that as well as C++17 does.

What later standards would have given, and what it costs to do without:

| Feature | Standard | Actually needed here? |
|---|---|---|
| `std::make_unique` | 14 | 6-line helper |
| `std::string_view` | 17 | nice for binary parsing; `const char* + size_t` works |
| `std::optional` / `std::variant` | 17 | you want a hand-rolled tagged union anyway, to mirror `MfTypedValue` / `MfVariant` field-for-field |
| structured bindings, `if constexpr` | 17 | convenience only |
| `std::filesystem` | 17 | **the only real loss** — and see below |

`std::filesystem` turns out not to matter, because this game's data forces a custom VFS regardless:
extensions vary in case within a single directory (`.K2A`, `.k2a`, `.K2a`), engine-internal paths
are Windows-style backslash (`geometry\rooms\prisonfix\prison3.k2a`), and lookups must be
case-insensitive on case-sensitive filesystems. You are writing path normalisation by hand either
way.

**The preservation argument favours C++11**: it builds on GCC 4.8+, Clang 3.3+, MSVC 2015+, and
every toolchain since. A codebase that compiles on a 12-year-old compiler will still compile in
20 years. Pick C++11, hand-roll the three helpers, and keep the dialect boring: no exceptions in
engine code, no template metaprogramming, no RTTI.

### Platform layer — abstract it yourself; macOS + Cocoa first

The question of native backends versus SDL is really a question of **how much platform surface you
are willing to maintain for the next two decades**. Going native means four backends, not one:

| Platform | Native cost |
|---|---|
| Windows | Win32 window + WGL + XAudio2/WASAPI + XInput/RawInput |
| Linux | **X11 *and* Wayland**, ALSA/PulseAudio/PipeWire, evdev gamepads |
| macOS | Cocoa in Objective-C++, and **OpenGL is deprecated** — realistically Metal or MoltenVK |
| Web | Emscripten, which effectively expects an SDL-shaped API anyway |

That is a large amount of code that has nothing to do with Prince of Persia, and it is precisely
the code most likely to rot: Wayland, audio backends and macOS graphics APIs churn far faster than
a 1999 game's file formats do.

**Decision: define your own thin platform interface; implement it natively on macOS first.**
See [`04-platform-layer.md`](04-platform-layer.md) for the design and
`src/platform/platform.h` + `src/gfx/gfx.h` for the interfaces. SDL remains a drop-in
alternative behind the same header if the native backend ever becomes a burden.
The interface is small — roughly:

```c
bool     plat_init(const char* title, int w, int h);
void     plat_swap(void);
bool     plat_poll(Event* out);          // keyboard, mouse, gamepad, quit, resize
void     plat_audio_open(int rate, AudioCallback cb, void* user);
double   plat_time_seconds(void);
void*    plat_gl_proc(const char* name);
```

Six functions. Everything above them is engine code that never sees SDL. This gives you:

- portability on day one, including the Emscripten target
- **no lock-in** — if SDL is ever a problem, you replace one file, not the codebase
- the option to add a genuinely native Win32 or Cocoa backend later for a specific reason
  (e.g. a Metal path on macOS) without touching the game

**SDL2 vs SDL3 is deferred.** While the backend is Cocoa the question does not arise, and behind
this interface it stays reversible — which was the point of having the interface.

The same reasoning applies to rendering, with one design rule that matters: **the platform layer
owns context creation, the renderer owns only the GL API.** Conflating them — as
`SDL_GL_CreateContext` encourages — means swapping the renderer forces changes in platform code.
Kept apart, a Metal backend is one new platform entry point returning a `CAMetalLayer` plus a
second implementation of `gfx.h`, with nothing above either one touched.

### Audio — platform callback + OpenAL Soft

`.sfx` is raw 16-bit PCM with explicit loop points and a fully decoded 32-byte header. Nothing
exotic needed — feed it through the `plat_audio_open` callback above. Use OpenAL Soft for 3D
positioning; A3D is dead and its HRTF is not worth recreating. Do **not** replicate the
`WaveMix` / `MOHAWK95.VXD` path.

### Video — decode in-engine from the original `.mov`

**Corrected from an earlier revision, which recommended transcoding with FFmpeg.** That would
mean the engine could not play an unmodified retail install — the opposite of what a preservation
project should be able to do.

All 14 files are QuickTime with a zlib-compressed `moov`, **SVQ1** video, and either raw
big-endian PCM (`twos`, 10 files) or **QDM2** (4 files) audio. ScummVM carries a QuickTime
demuxer plus SVQ1 and QDM2 decoders — exactly this set — so borrowing is viable.

**Note the licence consequence:** ScummVM is GPLv2+, so reusing its code makes this engine
GPLv2+. Normal for a preservation project, but a deliberate project-level choice. See
[`../formats/movies.md`](../formats/movies.md).

### Scripting — not needed

**Do not add a scripting language.** Per §1, the retail game runs no scripts: there are no `.fms`
files, 13 of 14 `Behaviors` directories are empty, and `PiccoloRT.dll` is never loaded. Adding Lua
would be inventing an architecture the original did not use.

If you later want the Piccolo VM for completeness, the groundwork is in
[`../reference/vm-isa.md`](../reference/vm-isa.md) — dispatch, value layout and operand encodings
are all confirmed.

### Build & project

CMake, vendored dependencies (SDL2 included as a submodule), no package manager. Targets:
Windows, Linux, macOS, and **Emscripten**. C++11, warnings-as-errors. Keep the asset toolchain (Track B) as a separate library the game links, so it
stays usable standalone.

## 4. Concrete next steps, in order

1. **Resolve the `.skl` 7-float channel.** The single highest-value unknown. Without it there is
   no animation, and with it a viewer becomes possible immediately.
2. **Write the asset library** for the confirmed formats (`.sfx`, `.K2A`, `.act`, `.nif`,
   linkmap). Validate every parser across every file, as done in these docs.
3. **Build a viewer** — load a `.K2A` skeleton, apply a `.skl` clip, render with GL 3.3. This is
   the proof that the format work is right, and it is a preservation artifact on its own.
4. **In parallel, stand up Track A** so the game is playable while the rest proceeds.
5. **Reverse `CharacterStateTable` and the `K2*Activity` set** — this is the gameplay logic, and
   it is native C++ in the exe. With the class hierarchy already recovered, this is ordinary RE.
6. **Characterise `MfLocoPlanner` behaviourally** — the remaining genuine unknown.
7. Only then consider Track C.

## 5. What would make this fail

- Starting with the engine instead of the formats. The formats are the preservation artifact; an
  engine without them is a tech demo.
- Assuming the renderer is the hard part. It is the easy part.
- Committing to a scripting approach before `.fms` is understood.
- Chasing `.fms` or the Piccolo VM. Both are real subsystems the retail game never executes.
  Time spent there buys completeness, not a playable game.
