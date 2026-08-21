# Format overview

## Extension census (whole install tree)

| Count | Ext | Format | Doc | Confidence |
|---:|---|---|---|---|
| 2363 | `.skl` | Motivate skill/animation, binary, magic `kSFM` | [skl.md](skl.md) | header confirmed, body partial |
| 1255 | `.sfx` | PCM audio + loop points, magic `WI` at 0x12 | [sfx.md](sfx.md) | **confirmed** |
| 424 | `.bmp`/`.BMP` | Windows BMP, 8-bit paletted textures | — | standard |
| 148 | `.act`/`.ACT` | Motivate actor definition, magic `APFM`/` MFB` | [act.md](act.md) | header + property table **confirmed**; node data partial |
| 169 | `.K2A`/`.k2a`/`.K2a` | K2 ASCII geometry export (3DS Max ASE dialect) | [k2a.md](k2a.md) | **confirmed** (text) |
| 95 | `.NIF`/`.nif` | NetImmerse 2.3 scene | [nif.md](nif.md) | externally documented |
| 42 | `.tga`/`.TGA` | Targa, mostly 32-bit with alpha | — | standard |
| 49 | `.dat`/`.DAT` | mixed: CLI arg files, tuning tables, actor master lists | [text-and-tuning.md](text-and-tuning.md) | **confirmed** (text) |
| 23 | `.txt` | GameText, Traps doc, level linkmaps, dev logs | [linkmap-save.md](linkmap-save.md), [text-and-tuning.md](text-and-tuning.md) | **confirmed** |
| 14 | `.mov` | QuickTime, zlib `cmov`; **SVQ1** video + `twos`/**QDM2** audio | [movies.md](movies.md) | **confirmed** |
| 14 | `.bin` | `<level>_save.bin` object persistence | [linkmap-save.md](linkmap-save.md) | partial |
| 2 | `.exe` + 1 `.icd` | PE32 | [../ARCHITECTURE.md](../ARCHITECTURE.md) | confirmed |
| 1 | `.sef` | Special-effect name → nif path table (TSV) | [behavior.md](behavior.md) | **confirmed** |
| 1 | `.pobj` | Piccolo compiled bytecode, magic `MJSP` | [behavior.md](behavior.md) | header only |
| 1 | `.bhvr` | HFSM behavior source, text `mf_hfsm` | [behavior.md](behavior.md) | **confirmed** |
| 1 | `.pop` | Savegame | [savegame.md](savegame.md) | partial |
| 1 | `.sav` | `data/NIS.sav` cutscene-seen bitmap | [savegame.md](savegame.md) | **confirmed** |
| 1 | `.ini` | `data/fight.ini` combat timing table | [text-and-tuning.md](text-and-tuning.md) | **confirmed** |
| 1 | `.w32` | `data/mohawk.w32`, INI-syntax audio driver config | [text-and-tuning.md](text-and-tuning.md) | **confirmed** |
| 1 | `.ttf`, `.cur`, `.ico`, `.isu` | font / cursor / icon / uninstaller | — | standard |

**Present (added after the initial survey):** `.lvl` (20 levels) and `.nbr` (8 files) — see
[lvl.md](lvl.md), both validated against real data.

**Absent but referenced by the engine:**

| Ext | Format | Doc |
|---|---|---|
| `.fms` | Motivate finite-state-machine database (default `FMSdata.fms`) | [lvl.md](lvl.md), [behavior.md](behavior.md) |
| `CONFIG.ENT` | referenced by `data/config.dat`'s comments | [text-and-tuning.md](text-and-tuning.md) |

See [ARCHITECTURE.md §5](../ARCHITECTURE.md#5-important-gap-in-this-install).

## Magic-number quick reference

```
"NetImmerse File Format, Version 2.3\n"  .nif
"*K2_ASCIIEXPORT\t105\r\n"               .k2a
"APFM" "ActP" <u32 version>              .act   (versions 48, 105, 106)
" MFB" 02 00 23 00 "ActP"                .act   (older variant, 3 files)
"kSFM" "Skil"|"MSkl"|"LSkl" <u32 ver>    .skl   (versions 105, 106)
"MJSP"                                    .pobj
"mf_hfsm <name>\r\n{"                     .bhvr
<WAVEFORMATEX 18 bytes> "WI"              .sfx
"game header\0"                           .pop
"NIS.sav\0"                               NIS.sav
"linkmap size = N"                        *_linkmap.txt / *_loadmap.txt
```

## Endianness and conventions

All binary formats are **little-endian**, x86, 32-bit. Floats are IEEE-754 single.

Strings in `.act` / `.skl` / `.pop` use a **length-prefixed** convention:

```c
struct MfString {
    uint32_t len;      // byte count, NOT including a terminator
    char     data[len];// no NUL terminator
};
```

An empty string is stored as `len = 0` with no bytes following. This is consistent across
every file inspected and is the key to walking these formats.
