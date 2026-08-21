# `.mov` — cutscenes

**Status: container and codecs identified across all 14 files.**

Location: `data/Movies/*.mov`, 14 files, ~410 MB total.

## Container

Standard QuickTime. Top-level atoms are `moov`, `free`, `wide`, `mdat`.

Every file uses a **compressed movie header**: the `moov` contains `cmov`, which holds
`dcom` = `zlib` and `cmvd` = `[uint32 uncompressed size][deflate stream]`. Inflate that to get the
real `moov` atom tree before parsing anything else.

```
moov
 └── cmov
      ├── dcom  "zlib"
      └── cmvd  uint32 raw_size, then the deflate stream
```

Watch the offset: the compressed payload starts **12 bytes** after the start of the `cmvd` atom
(4 size + 4 type + 4 uncompressed-size), not 16.

## Codecs

| Stream | Codec | Files |
|---|---|---|
| Video | **SVQ1** (Sorenson Video 1) | **14 / 14** |
| Audio | `twos` — raw big-endian signed 16-bit PCM | 10 |
| Audio | **QDM2** (QDesign Music 2) | 4 |

Resolutions are 640×387 (twelve files), 640×376 (`cs03`, `cs08`) and 640×480 (`Broder`, the
publisher logo).

```
Broder.mov    1.6MB  640x480  SVQ1 + QDM2
cs01.mov     40.2MB  640x387  SVQ1 + twos
cs03.mov      5.2MB  640x376  SVQ1 + QDM2
cs05.mov     13.9MB  640x387  SVQ1 + QDM2
cs08.mov     12.8MB  640x376  SVQ1 + QDM2
...          remaining files  SVQ1 + twos
```

## Decoding in-engine

Playing the original files needs three pieces, and all three are tractable:

1. **QuickTime demuxer** — atom walk, `stsd`/`stts`/`stsc`/`stco`/`stsz` sample tables. Ordinary
   parsing work.
2. **SVQ1 decoder** — the significant one. Sorenson Video 1 is publicly documented and has
   long-standing open implementations.
3. **QDM2 decoder** — needed for only 4 of 14 files. `twos` needs no decoder at all: it is raw
   big-endian PCM, byte-swap and feed straight to the audio callback.

This is a genuinely lucky draw. Had the videos been **SVQ3**, the picture would be much worse —
SVQ3 is a considerably harder decoder and is not part of the ScummVM codec set.

### On borrowing from ScummVM

ScummVM carries a QuickTime demuxer plus SVQ1 and QDM2 decoders, which covers this game exactly.
Reusing it is the pragmatic route.

**Licence consequence, stated plainly:** ScummVM is **GPLv2-or-later**. Incorporating its code
makes this engine GPLv2+ as well. That is a normal and defensible choice for a preservation
project — DevilutionX, OpenMW and others sit in the same place — but it is a project-level
decision to make deliberately, not a detail. If a more permissive licence is wanted, the
alternatives are a clean-room SVQ1/QDM2 implementation from the format documentation, or
FFmpeg's LGPL build used as a shared library.

Whichever route: attribution and licence text must ship with the result.

## Why not transcode offline

An earlier revision of the plan recommended converting the videos with FFmpeg and shipping modern
files instead. That was the wrong call for this project: it means the engine can no longer play an
unmodified retail install, which is precisely what a preservation effort should be able to do.
Decode in-engine, from the original `.mov`.

## Renderer interface

`gfx.h` exposes `create_stream_texture` / `update_texture` for per-frame video upload, and
`draw_fullscreen_quad` to present it. No mesh path is involved.
