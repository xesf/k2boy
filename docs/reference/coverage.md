# Reverse-engineering coverage

Regenerate with `python3 tools/coverage.py`. Subsystem percentages are judgement calls, not measurements; the function counts are exact.

## Functions in `pop3dreal.exe`

| Bucket | Count | Share |
|---|---:|---:|
| Total | 39,695 | 100.0% |
| Named by this project | 793 | 2.0% |
| Named by IDA / FLIRT (library code) | 1,379 | 3.5% |
| Still `sub_*` | 37,523 | 94.5% |

The headline number is deliberately unflattering. Most of the binary is NetImmerse and Motivate middleware that does not need naming to be reimplemented - see `docs/plan/03-reimplementation.md`. Progress is better read per subsystem below.

## File formats

| Format | Status | Notes |
|---|---|---|
| `.K2A` | **confirmed** | 77-keyword grammar recovered from the binary |
| `.act` | **confirmed** | property table validated on 115/115 APFM files |
| `.bhvr` | **confirmed** | trivial; dormant in retail |
| `.lvl` | **confirmed** | command line + 14 bracketed sections, 20/20 files |
| `.mov` | **confirmed** | QuickTime + SVQ1 + twos/QDM2 identified |
| `.nbr` | **confirmed** | 16/16 files; cross-checked against savegame |
| `.sef` | **confirmed** | effect name -> nif table |
| `.sfx` | **confirmed** | header + loop points, validated on every file |
| `linkmap / loadmap` | **confirmed** | text object table |
| `text / tuning` | **confirmed** | GameText, Traps.txt, master.dat, fight.ini, enemy.dat, camera.dat |
| `.pobj` | partial | container only; dormant in retail |
| `.pop savegame` | partial | framing + 5 of 11 sections; stream buffer partial |
| `.skl` | partial | JKey record confirmed; 7-float channel meaning open |
| `save.bin` | partial | link table understood, state stream not |
| `.nif` | external | NetImmerse 2.3, use NifTools schema |
| `.fms` | unknown | absent from the install; dormant in retail |

**10 of 16 formats confirmed** (62%).

## Subsystems

```
RTTI class recovery          [####################] 100%  909/909 vtables resolved, 853 renamed
NiRTTI hierarchy             [####################] 100%  169 classes with exact parent links; 169/169 GetRTTI named
NiObject root interface      [####################] 100%  all 7 virtuals identified from their bodies; 503 overrides named
NIF streaming (code side)    [########............]  40%  the 5-method protocol is understood; the NiStream container is not
Trap factory                 [####################] 100%  all 33 registry keys + mechanism
Chunk registry               [####################] 100%  78 types with creator and sizeof
Piccolo script API           [####################] 100%  29 classes, 250 methods (dormant subsystem)
K2A keyword grammar          [####################] 100%  77 keywords incl. K2_TRAP_* and TRI_* variant
K2A loader (engine code)     [####################] 100%  parser + mesh build; 501/501 parse; per-channel material reading (MAP_DIFFUSE bitmap, MAP_OPACITY = blended)
LVL + NBR loaders            [####################] 100%  all 21 levels parse; 18 resolve every path; fog factors are far-clip fractions
master.dat loader            [####################] 100%  42/42 parse, 0 unresolved geometry or actor
ACT skeleton hierarchy       [#############.......]  65%  transforms are local-to-parent, bind rotations identity, this-adjust resolved (0x14); full reader field order mapped; count-prefixed 20-byte records located; node-walk still unknown
ACT loader                   [###############.....]  75%  properties 145/145; node layout + write order recovered
Mf serialization iface       [##################..]  90%  interface, tags, write order, and the +0x14 MI this-adjust
Image loaders                [####################] 100%  BMP + TGA, 1035/1035 decode
Platform layer (macOS)       [################....]  80%  window, GL 3.3, input, audio sink, VFS; gamepad stubbed
Renderer (GL 3.3 core)       [#########...........]  45%  meshes, textures, state, fullscreen quad; no materials yet
Level / neighbour parsers    [##################..]  90%  validated; bracketed-section readers not traced
Piccolo VM                   [########............]  40%  dispatch + machine model; operator mnemonics open
Command-line dispatcher      [#######.............]  35%  tree mapped, ~6 of 18 handlers decoded
Savegame                     [#########...........]  45%  framing + 5 sections
Animation (.skl)             [###################.]  95%  loader done: 2363 files, 0 failures, 99.89% quats valid
CharacterStateTable          [####................]  20%  placed in the hierarchy; base of Prince, camera and Trap. Only 7 of its 51 slots are overridden - 51 slots are the inherited NiNode interface, not states
Boot sequence / WinMain      [##################..]  90%  entry -> WinMain -> event loop traced; distribution.dat + Tsunami.ttf; distribution.dat parsed; module table decoded - ids are alphabetical, 0x0C = prison = prisonfix.lvl
Module state machine         [###############.....]  75%  6-virtual interface, 5 subclasses, slot 3 = event handler; 20 modules with names + levels, table + startup id implemented and verified
Scene graph (NiNode)         [############........]  60%  transform layout measured from NiAVObject__LoadBinary; 8/8 self-test
Actor spawn / New Game       [##############......]  70%  chain traced; ported and validated: 134/142 spawn points resolve
K2*Activity family           [#...................]   5%  identified and named only
Motivate planner             [....................]   0%  MfLocoPlanner / MfManipActivity - not started
```

Unweighted mean across subsystems: **73%**.

## What the numbers say about ordering

The three zeroes - `CharacterStateTable`, the Motivate planner, and the renderer - are the actual game. Everything at 100% is scaffolding that makes those three tractable. Formats are close to done; behaviour has barely started.
