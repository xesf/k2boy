# Startup: from launch to gameplay

Traced from the PE entry point. This is what has to exist before a level can run,
in the order the original does it.

## The chain

```
entry 0x530ABD              CRT startup
  _WinMain@16   0x42BD61
    g_app_name = "K2Boy"
    atexit(0x45CEE5)
    0x4347EA / 0x4FB6DA / 0x459AC1     early init, not yet traced
    0x45DA8A, 0x43229A
    g_game = new(0xB8) -> Game__ctor   0x4F8EC0
    command line:
        lpCmdLine non-empty -> CommandLineParser__DispatchFlag
        otherwise           -> read "distribution.dat" and dispatch from it
    0x45CC42(hInstance)                create the window
    0x45CB70(w, h)                     set the display mode
    CreateFontA("Arial", 24)
    AddFontResourceA("Tsunami.ttf")    the game's own UI font
    new(0x40) -> 0x42CCE8              stored in dword_7E9224
    srand-equivalent seeded from time()
    Mofac__Create                      "mofac create" - Motion Factory
    0x436101(hWnd)
    Game__PostModuleMessage(0x0C, -1, -1)   on off_74D128
    Game__EventLoop  0x42C10E          "Going to the Event Loop."
```

`Game__EventLoop` is a plain Win32 `PeekMessageA` loop with a per-frame tick:
`Game__FirstFrameInit` once, then `Game__Tick(dt)` each pass, with
`TranslateAccelerator` / `TranslateMessage` / `DispatchMessage` for input.

## Two things the game reads before anything else

* **`distribution.dat`** — when there is no command line, the game opens this
  file and feeds it to the same dispatcher that handles command-line flags. So
  the shipped build configures itself through a file of flags. It is present in
  the install.
* **`Tsunami.ttf`** — registered with `AddFontResourceA` at startup. All UI text
  depends on it. `Arial` at 24pt is created as well.

## The module message

`Game__PostModuleMessage(id, a, b)` forwards to `Message__Build`, which fills an
8-byte record in `g_module_message`:

```c
struct module_message {   // 8 bytes
    uint16_t target;      // Module__GetId(obj), or 0xFFFF when obj is null
    uint16_t arg0, arg1, arg2;
};
```

WinMain posts `0x0C` immediately before entering the loop. That is the initial
transition — the first module the game shows. **Which module `0x0C` selects has
not been confirmed**, only that this is the mechanism.

## Module construction

The five module subclasses are each built by a small constructor reached through
a 5-byte `jmp` thunk, not called directly:

| module | ctor | thunk |
|---|---|---|
| `gameModule` | `0x526213` | (17 direct callers) |
| `sMenuModuleClass` | `0x42E856` | `0x42E851` |
| `sFinaleModuleClass` | `0x5262CE` | `0x5262C9` |
| `sNISModuleClass` | `0x526600` | `0x5265FB` |
| `sTitleModuleClass` | `0x526EC7` | `0x526EC2` |

The thunks are referenced from pointer tables in `.rdata` (`0x7AFD18`,
`0x7B6D6C`, `0x7B6DBC`, `0x7B6E94`). Those tables sit among the script-binding
tables rather than a dedicated module registry, and **have not been identified**
— do not assume they are a module factory.

Note the scanning trap: these constructors have **zero `call` references**. They
are only reachable through `jmp`. A cross-reference scan that matches `E8` and
not `E9` reports them as dead code, which is what happened to
`AvatarBody__CreateAvatar` earlier.

## What this means for the reimplementation

Ordered, and honest about what is and is not known:

1. **Config** — parse `distribution.dat` through the existing command-line
   dispatcher. Cheap; the dispatcher is already partly decoded.
2. **Window, GL context, main loop** — already done in `src/platform` and
   `src/main.cpp`. The original's loop is `PeekMessage` + a `dt` tick, which the
   current loop already matches in shape.
3. **Module state machine** — the interface is recovered
   (`docs/reference/engine-structure.md`) and stubbed in `src/engine/module.*`.
   What is missing is the **message ids** and the transition table, not the
   interface.
4. **Title / intro** — `sTitleModuleClass` and `sNISModuleClass` exist and are
   named, but neither body has been read. NIS means non-interactive sequence,
   i.e. the intro movie, which needs the QuickTime/SVQ1 decoder that is
   documented but not written (`docs/formats/movies.md`).
5. **Gameplay** — `gameModule` drives level load and actor spawn, both of which
   are already ported and validated (`docs/reference/actor-spawn.md`).

The shortest path to something playable skips 4 entirely: post straight to
`gameModule`, load a level, spawn the actors. That path is already largely
built. The intro is a separate, self-contained problem gated on video decoding.

---

# Startup configuration, decoded

`distribution.dat` is 125 bytes of command-line flags:

```
;
; Sample command-line file
;
-x
;-f0
;-lx-
-warn 1
-h
;-s8
-zp
-b 24,24,24
-i menu.dat
-cmd console.dat
```

`;` comments are used both for prose and to switch flags off, so three of the
ten are inert. Implemented in `src/engine/config.{h,cpp}`; run `k2boy --config`
(or `make config`):

```
distribution.dat: 7 active flags
  -x  (no tail)
  -w  arn 1
  -h  (no tail)
  -z  p
  -b  24,24,24
  -i  menu.dat
  -c  md console.dat
  startup file (-i): menu.dat
  console file (-c): console.dat
  menu.dat contents: '5 3 1'
```

**This is independent confirmation of the single-character dispatch** documented
in `command-line.md`. Parsing on one character produces `-w` + `"arn 1"`,
`-c` + `"md console.dat"`, `-z` + `"p"` — the multi-letter spellings really are
one flag plus a handler-parsed tail, exactly as the dispatcher at `0x45D0A7`
behaves. Nothing was tuned to make this come out; it is what the shipped file
does under the recovered rule.

## What the startup files contain

| file | contents | status |
|---|---|---|
| `distribution.dat` | the flags above | **used** — read by WinMain when there is no command line |
| `menu.dat` | `5 3 1` | named by `-i`; the three numbers are **not decoded** |
| `console.dat` | `minrate 15` | **inert** — `minrate` appears nowhere in the binary (see `command-line.md`) |

The exe carries `"menu.dat"`, `"cmdline.dat"` and `"K2log.dat"` as built-in
default filenames in one string pool at `0x7C18E0`.

## Not decoded

* **`menu.dat`'s `5 3 1`.** Three integers behind the `-i` flag. The `i` handler
  has not been read, so what they select is unknown. Do not assume they are a
  module id.
* The tails of `-x`, `-h`, `-z p`, `-b 24,24,24` are captured verbatim. `-b` is
  plausibly a colour depth triple, but that is a guess and is not acted on.
