# PlayStation Vita port

A second platform behind the same two interfaces as macOS: `src/platform/platform_vita.cpp`
(input, time, audio, files, threads) and `src/gfx/gfx_gxm.cpp` (GXM renderer). Everything above
them is the same code. VitaSDK system libraries only; no third-party code.

## Build and install

```
make vita                       # → build-vita/k2boy.vpk  (VitaSDK at $VITASDK or ~/vitasdk)
```

- Install `k2boy.vpk` with VitaShell.
- Copy the retail install to `ux0:data/k2boy/data/` (the same tree `make play` reads, about 880 MB).
- Saves, settings, `log.txt` and `err.txt` are written to `ux0:data/k2boy/`.

The LiveArea art (icon, background, start image) is made from the retail data at build time by
`tools/vita_sce_sys.py` (needs Pillow), so no game art is kept in the repository.

## Shaders

GXM runs only compiled programs (`.gxp`). The Cg source is in `gfx_gxm.cpp`: one vertex and one
fragment program, the same tint × texture × vertex colour / alpha test / fog shader as the GL
backend. The game looks for them in this order:

1. `app0:shaders/k2_v.gxp`, `k2_f.gxp`, which are packaged from `platforms/vita/shaders/*.gxp`;
2. `ux0:data/k2boy/shaders/`, compiled by an earlier run;
3. compiled now by the system's own Cg compiler, `libshacccg.suprx`, loaded from
   `ur0:data/` (where Vita homebrew keeps it) or `ux0:data/k2boy/`. The results are saved to (2).

So the first run on a Vita with `libshacccg.suprx` compiles them. Copy
`ux0:data/k2boy/shaders/*.gxp` into `platforms/vita/shaders/`, commit them, and later packages
carry them, with no compiler needed. Until then, a Vita without the module, or Vita3K, stops at
`gxm: shader programs failed` in `log.txt`.

## Renderer notes

- 960×544, three display buffers in CDRAM, presented by the display queue from `gfx::end_frame`.
  `plat::swap_buffers` does nothing on the Vita.
- GPU memory comes from two heaps carved from memblocks: 96 MB of CDRAM for textures and frame
  buffers, and 64 MB of uncached main memory for meshes and rings. newlib keeps a 192 MB heap.
- The GPU reads a frame up to two frames after it is built. Dynamic meshes therefore go to a
  per-frame arena, stream textures (movies) rotate through three slots, and freed resources are
  released three frames later.
- GXM has no clear call. `clear` draws a quad at the far plane.
- GL conventions are kept: matrices go in as the GL backend has them, the viewport maps NDC
  y-up and z −1..1, and back-face culling is `SCE_GXM_CULL_CW` through the flipped viewport.
- Textures are linear RGBA8, bilinear, without mipmaps. Power-of-two textures repeat; others clamp.

## Cores

A game gets three of the Vita's four cores. The main thread runs on core 0. `engine/jobs` starts
two workers pinned to cores 1 and 2, and each frame they pose every actor's skeleton in
parallel; sound keys found while posing are played afterwards on the main thread. The audio
mixer runs on its own high-priority thread. The pool synchronises with kernel semaphores
(`plat::sema_*`), not pthreads: the workers are kernel threads.

## Controls

| Vita | Game |
|---|---|
| Left stick / d-pad | move |
| Right stick | camera |
| Cross | jump (menus: confirm) |
| Circle | attack (menus: back) |
| Square | action (grab, climb, talk, pick up) |
| Triangle | draw / sheathe |
| L | block |
| R | run |
| Start | game menu; skips a cutscene |
| Select | motion camera on/off: tilting and turning the Vita aims the camera |
| Front touch | tap menu items; drag to look |
| Rear touch | drag to look |

On a PS TV, a DualShock's L2/R2/L3/R3 come through as well.

## newlib quirks met

- `printf` has no C99 length modifiers: `%zu` printed `zu` and shifted the arguments after it.
  `printf`, `fprintf`, `snprintf` and their `v` forms are wrapped at link time
  (`-Wl,--wrap`), and the `z` is dropped (`size_t` is `unsigned int` here).
- The standard streams exist per thread and globally. Both are pointed at the log files.

## Not done

- Not yet run on hardware. The shaders are compiled on the device and nowhere else.
- No mipmaps, so distant textures shimmer.
- Movies decode on the main thread. Moving decoding to a worker would use the third core better.
