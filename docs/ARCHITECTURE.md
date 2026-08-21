# Architecture

## 1. Binary inventory

| File | Size | Timestamp (PE) | Role |
|---|---|---|---|
| `data/pop3d.exe` | 280 339 | 1999-08-07 13:27:12 | **SafeDisc v1 loader.** Linker 5.0, sections `.txt` / `.text` / `.txt2`, subsystem GUI. Decrypts and launches `data/pop3d.icd`. |
| `data/pop3d.icd` | 4 120 672 | 1999-08-23 01:56:49 | Encrypted payload consumed by the loader. |
| `data/pop3dreal.exe` | 4 120 627 | 1999-08-23 00:24:08 | **The real game, unprotected.** ← *This is the RE target.* |
| `data/a3dapi.dll` | 286 720 | 1999-07-27 23:43:11 | Aureal A3D 2.0 runtime (third-party, not a target). |

Hashes:

```
7a89eaef1997f089fa04f7a3f4a26779  pop3dreal.exe
ff1f69dd7a8b95796617d839dc253d61  pop3d.icd
ee0cc0cd17899ed3ca7bde37b130bace  pop3d.exe
0d7da5bc7f0d0968cd9c470a8dc95a0c  a3dapi.dll
```

### `data/pop3dreal.exe` layout

```
ImageBase   0x400000        Linker 6.0 (MSVC 6)   Subsystem 2 (GUI)
Entry       0x530abd
SectionAlign 0x1000   FileAlign 0x1000   (file offset == RVA — convenient)

.text   VA 0x401000  vsize 0x34144a   raw 0x001000/0x342000
.rdata  VA 0x743000  vsize 0x06b328   raw 0x343000/0x06c000
.data   VA 0x7af000  vsize 0x10b71c   raw 0x3af000/0x03b000   (bss-heavy)
.rsrc   VA 0x8bb000  vsize 0x003410   raw 0x3ea000/0x004000
```

No `.reloc`, no export table, no PDB reference. Debug directory present but stripped of
symbol path. **RTTI is intact.**

`data/pop3d.icd` is the same build ~1h32m later (`.text` differs by 0x42 bytes); diffing the two is a
cheap way to isolate the copy-protection stub if that ever matters.

### Imports (15 DLLs, all by name)

`KERNEL32` (149), `USER32` (92), `GDI32` (26), `ADVAPI32` (5), `COMCTL32` (4), `VERSION` (3),
`ole32` (3), `comdlg32` (2), `DINPUT` (2), `DDRAW` (2), `IMM32` (2), `WINMM` (1), `DSOUND` (1),
`DPLAYX` (1), `SHELL32` (1).

Notable: only **2** DirectDraw and **2** DirectInput imports, **1** DirectSound — the engine
COM-creates everything else through `ole32`. `PiccoloRT.dll` and `MOHAWK95.VXD` are referenced by
string but loaded dynamically (`LoadLibraryA`), so they will not show in the import table.

## 2. Source tree layout (recovered from RTTI type descriptors)

Local-class RTTI names leak the original paths:

```
C:\k2\K2boy\Source\Traps\Abyss.cpp        -> class AbyssAction
C:\k2\K2boy\Source\Traps\Launcher.cpp     -> class Dart, class Fireball
C:\k2\K2boy\Source\K2Audio\Audio.cpp      -> class IndirectSource
C:\k2\K2boy\Source\K2Audio\Aureal.cpp     -> class A3DSource
C:\k2\K2boy\Source\K2Audio\DS3D.cpp       -> class DS3DSource
C:\k2\K2boy\Source\K2Audio\SFX.cpp        -> class SFXFile
```

So the game code lives in `C:\k2\K2boy\Source\{Traps,K2Audio,...}`. Any additional local class
found later extends this map for free — grep the string table for `?%C:\k2\`.

## 3. Class taxonomy

Full list: [`reference/rtti-classes.txt`](reference/rtti-classes.txt). The 818 names cluster into
five families.

### 3.1 `Ni*` — NetImmerse 2.3 renderer (~40 classes)

`NiObject` → `NiAVObject` → `NiNode` / `NiGeometry` → `NiTriBasedGeom` → `NiTriShape`.
Properties: `NiAlphaProperty`, `NiZBufferProperty`, `NiFogProperty`, `NiShadeProperty`,
`NiTextureModeProperty`, `NiVertexColorProperty`, `NiWireframeProperty`, `NiDitherProperty`,
`NiTransparentProperty`. Collision: `NiCollisionGroup`, `NiCollisionSwitch`, `NiCollPick`,
`NiSphereBV`, `NiBoundingVolume`. Containers: `NiTArray<T>`, `NiTMap`, `NiTStringMap`, `NiPointer<T>`.

These are **documented publicly** (NIF format is well studied by the NifTools project). Naming
them in IDA costs nothing and immediately explains a third of the binary.

### 3.2 `Mf*` — Motion Factory Motivate runtime (~400 classes, the largest family)

Sub-clusters:

- **`MfSc*` (~60)** — the **Piccolo** script VM: `MfScVirtualMachine`, `MfScBytecodeRoutine`,
  `MfScDataObject`, `MfScThread`, `MfScGcThread`, `MfScHandleDict`, `MfScPiccoloObjectFile`,
  `MfScFunctionRegistry`, `MfScBreakpointManager`, `MfScDebugInfo`, `MfScClassInfo`.
  This is a complete garbage-collected scripting language embedded in the game.
- **`MfPo*` (~40)** — the Piccolo **object model / script bindings**: `MfPoActor`, `MfPoSkill`,
  `MfPoHFSM`, `MfPoCamera`, `MfPoWorld`, `MfPoSpace`, `MfPoSound`, `MfPoTransform`, `MfPoVector3`.
  Every `MfPo*` class is a script-visible type — enumerating their method tables recovers the
  entire scripting API.
- **`MfState*` / `MfTrans*`** — HFSM machinery: `MfState`, `MfStateTransition`,
  `MfStateCompiler`, `MfTransChoicePt`, `MfStateEntryAction`. Backs the `.bhvr` files —
  **dormant in the retail game**, see §5.1.
- **`MfSkill` / `MfLocoSkill` / `MfManipSkill` / `MfLocoPlanner` / `MfManipActivity`** —
  the animation-driven locomotion + manipulation planner. Backs the `.skl` files.
- **`MfPmh*`** — proto message handlers (`MfPmhCollision`, `MfPmhProximity`, `MfPmhKeyboard`, …).
- **`MfLogger*`, `PMW_*`** — the developer's runtime inspector windows
  (`PMW_MemoryWindow`, `PMW_GeometryWindow`, `PMW_DutyCycleWindow`, `PMW_3dDisplayWindow`).
  **These are still linked in.** High-value target: re-enabling them gives a live debug UI.

### 3.3 `k2*` / `K2*` — Red Orb engine layer

`K2Activity`, `K2ActivityRun`, `K2ActivityXSkill`, `K2ArticulatedObject`, `K2AttachActivity`,
`k2AnimationAction`, `k2ActionFactory`, `k2SpecialEffectFactory`, `k2ArrowTrail`,
`k2AnimatedTextureHub`, `K2ImportOptions`, `K2DummyObject`, `K2Point4`.

### 3.4 Gameplay objects — the trap/entity system

Two parallel lists that must be kept in sync when reversing:

**Trap types** (each also has a nested `Builder@X` factory class):

```
Abyss  Action  Aperture  Area  ArrowCharger  ArrowTarget  AudioMessage  Bar
CollapsingFloor  Crate  CrateTarget  DelayMessage  Door  EnemyStart  Fixture
FlameThrower  ForceTrap  Fountain  Generic  Launcher  LightSwitch  Messenger
Mover  Pickup  Pole  PressurePlate  PrinceStart  RoomAttributes  Rope
SpecialEffect  Specials  Spikes  WallBlade  Water
```

The presence of `Builder@<Name>` for each is a **factory-table fingerprint**: find
`TrapFactory::Builder` and you get a dispatch table mapping the ASCII type name (as it appears in
`*_tempActiveLog.txt`) to a constructor. That single table names ~34 classes at once.

`data/Traps.txt` in the game root is the original design document listing each trap's parameter slots —
it is effectively a partial header file for these classes. Cross-reference it constantly.

**Prince / character**: `PrinceObject`, `PrinceMaster`, `PrinceMotion`, `PrinceFloor`,
`PrinceEventController`, `CharacterObject`, `CharacterStateTable`, `EnemyAIObject`
(with nested `AnimatronicState`), `WeaponParameters`, `Projectile`, `Arrow`.

**Camera**: `CameraLogic`, `CameraStateTable`, `CameraFileParser`, `CamUserActionObject`,
`MfOrbitCameraActivity`, `MfPanCameraActivity`, `MfSplineCameraActivity`. Driven by `data/camera.dat`.

**Physics/forces**: `Force`, `ForceCollider`, `RadialForce`, `VolumetricForce`, `MotivateForce`,
`CollisionObject`, `FloorInfo`, `Movement`, `Oscillator`, `SteadyRotation`, `RotateMotion`,
`TranslateMotion`, `StallMotion`, `GenericMotion`, `GrabberMotion`.

**Persistence**: `RsrcListManager` with nested `SaveListClass`, `PrinceBodySaveListClass`,
`PrinceLocSaveListClass`, `RoomsSaveListClass`, `RegionParamsSaveListClass`,
`StreamBufferSaveListClass`, plus `diskioSaveList`, `diskioBufferedSaveList`,
`diskioStreamedBufferedSaveList`, `HeaderSaveList`. This is the `*_save.bin` / `.pop` machinery.

**Modules (screens)**: `module` → `gameModule`, `sMenuModuleClass`, `sTitleModuleClass`,
`sNISModuleClass` (non-interactive sequence = cutscene), `sFinaleModuleClass`, `moduleManager`.

### 3.5 Runtime library

MSVC 6 iostreams + `std::locale` facets, `MfString`, `MfRegularExpression`. Ignorable, but useful:
recognising the `std::` facet vtables lets you exclude ~80 classes from analysis immediately.

## 4. Data flow at load time

```
pop3d.exe (SafeDisc)
   └─> pop3d.icd  ==  pop3dreal.exe
         ├── cmdline.dat / distribution.dat   -> CommandLineParser
         │      -x -f1 -h -zp -b 24,24,24 -i menu.dat -cmd console.dat
         ├── console.dat  ("minrate 15")      -> AConsole
         ├── menu.dat     ("5 3 1")           -> sMenuModuleClass
         ├── config.dat   (fall damage tuning)-> ConfigData
         ├── camera.dat   (camera splines)    -> CameraFileParser
         ├── enemy.dat / fight.ini            -> EnemyAIObject / WeaponParameters
         ├── GameText.txt (#textID + string)  -> UserTextFileParser
         └── level load:
               geometry\rooms\<lvl>\<lvl>.lvl      <-- MISSING from this install
               geometry\rooms\<lvl>\*.k2a
               Actors\<Name>\<Name>.act
               Actors\<Name>\Skills\*.skl
               Actors\<Name>\Behaviors\*.bhvr + *.pobj
               Actors\<Name>\Textures\*.bmp
               Geometry\Utils\*.nif  (+ SpecialEffects.sef name->nif table)
               Audio\sfx\*.sfx
            then writes  <lvl>_linkmap.txt  and  <lvl>_save.bin
```

Path templates found verbatim in the string table: `%s\%s\%s.act`, `%s\%s\%s.nif`, `%s\%s\%s.k2a`.

## 5. What is and is not present

`data/` holds a **complete retail install**, 6 543 files, 949 MB. 20 unique levels under
`data/Geometry/Rooms/**/` (`data/Rooms/` is a byte-identical duplicate tree), each with a `.lvl`
and, for 8 of them, a `.nbr`. Both formats are decoded and validated — see
[formats/lvl.md](formats/lvl.md).

Absent: `CONFIG.ENT` (minor tuning file), and the Motivate behaviour data — which is not needed,
per §5.1.

### 5.1 The retail game runs no HFSMs and no scripts

Despite the `MfState*` / `MfTrans*` / `MfFSM*` families and a complete Piccolo VM being linked in,
**none of it executes in the shipped game**:

| Check | Result |
|---|---|
| `.lvl` `-f` flag (loads FMS behaviour data) | no shipped level uses it; flags used are `a b k l n t v w` |
| `"FMSdata.fms"` (`0x7BFE14`) | referenced only from `0x44192A`, inside the unreachable `-f` handler |
| `*.fms` on disk | zero |
| `Actors/*/Behaviors/` | 14 directories, **13 empty**; only `BlankCamera` has content |
| `.bhvr` / `.pobj` | one each, both trivial |
| `"PiccoloRT.dll"` (`0x7D2BF4`) | **zero code references** — never loaded |

The engine supports the Motivate authoring pipeline; Red Orb did not use it. Gameplay logic is
native C++:

- `CharacterStateTable` — vtable installed from `0x40BB1D`, `0x44EF5B`, `0x44F014`, all in the K2
  game-code range. Both `PrinceObject` and `Trap` derive from it.
- the `K2*Activity` family, creators at `0x439406`–`0x4A3292` — see
  [reference/chunk-registry.md](reference/chunk-registry.md)
- `EnemyAIObject`, the `Trap` subclasses, `PrinceMotion`, `PrinceFloor`

**Consequence:** the game's logic is in the executable and recoverable by ordinary reverse
engineering, not locked in an unreadable middleware data format. The Piccolo VM documented in
[reference/vm-isa.md](reference/vm-isa.md) is a dormant subsystem.

## 6. Developer leftovers worth mining

- `data/prisonfix_loadmap.txt`, `data/prisonfix_tempActiveLog.txt`, `data/prisonfix_FloorGenerationLog.txt` —
  debug dumps from the last dev session, left in the shipped build. `tempActiveLog` gives
  `obj N trap type <Type> name <Name>. triname <Name>` for every trap in the level, i.e. a
  ground-truth decoding key for the trap system.
- `data/Traps.txt` — internal design doc, with the authors' own TODOs (`"I don't know the details
  myself to all these; if we want this to be complete, we need others to fill it in..."`).
- `PMW_*` inspector windows and `MfLoggerWindow` / `MfLogMessageDialog` still linked.
- `MfScBreakpointManager`, `MfScDebugInfo`, `MfPdbScriptProvider`, `DumpSkillActivity`,
  `DebugMemoryUsage`, `DebugTextManager` — a full script debugger is compiled in.
- `AConsole` + `data/console.dat` — an in-game console exists. Finding its command table is a
  Phase 2 objective and unlocks most experimentation.
