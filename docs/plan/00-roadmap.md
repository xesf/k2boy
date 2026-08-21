# Reverse engineering roadmap

## Objective

Understand `data/pop3dreal.exe` well enough to (a) document every file format the game reads and
writes, and (b) have a labelled IDA database that a future port, mod tool, or engine
reimplementation can be built from.

## Guiding facts that shape the whole plan

1. **RTTI is intact — 818 class names.** Class recovery is a data-mining problem, not a
   reversing problem. Do this first; it labels a third of the binary before you decompile
   anything.
2. **Two of the three layers are third-party and documented.** NetImmerse 2.3 has public
   schemas (NifTools); Motion Factory Motivate has published manuals and a known object model.
   Only the Red Orb `K2` layer is genuinely unknown. Do not burn effort re-deriving `NiNode`.
3. **The binary contains a self-describing script reflection table.** `MfScClassInfo`,
   `MfScCiMethod`, `MfScCiProperty`, `MfScMethodParameterInfo` — the scripting API with parameter
   names is *stored as data*. Dumping it is the highest value-per-hour task in the project.
4. **The developers shipped their own documentation.** `data/Traps.txt`, `master.dat`, `data/enemy.dat`,
   `data/camera.dat` and `data/fight.ini` describe the data model in prose. Read
   [../formats/text-and-tuning.md](../formats/text-and-tuning.md) before starting Phase 1.
5. **Level data is missing** (`.lvl`, `.nbr`, `CONFIG.ENT`, `PiccoloRT.dll`). Plan around it:
   the 14 `linkmap`/`save.bin` pairs plus `data/prisonfix_tempActiveLog.txt` give enough ground truth
   to work without the CD.

---

## Phase 0 — Set up (½ day) — **DONE**

- [x] Load `data/pop3dreal.exe` in IDA Free. See [01-ida-setup.md](01-ida-setup.md).
- [x] Auto-analysis complete: **39 652 functions**.
- [x] `ida-mcp` confirmed attached to the POP3D database.
- [x] Build string recovered: `K2Boy build: Aug 22 1999, 17:23:42 BuildMachine - D3D`

**Exit criterion met.**

---

## Phase 1 — Free labelling from RTTI — **DONE**

The MSVC RTTI chain is `vtable[-1] → RTTICompleteObjectLocator → TypeDescriptor → name`.
Walk it in bulk.

Done **offline against the raw PE**, not through IDA xrefs. `xrefs_to` returns nothing for these
addresses because the RTTI structures are undefined data in the database. Since
`SectionAlignment == FileAlignment == 0x1000`, file offset == RVA, so the whole chain can be
walked in Python and the results pushed back via `rename` batches.

- [x] 843 `TypeDescriptor` structures located.
- [x] 909 `RTTICompleteObjectLocator`s and 909 vtables resolved (812 distinct classes;
      the extra 97 are secondary vtables from multiple inheritance).
- [x] **853 vtables renamed** in the database (56 `std::` locale facets skipped as noise).
- [x] Inheritance recovered from `RTTIClassHierarchyDescriptor` / `RTTIBaseClassArray` →
      [`../reference/class-hierarchy.md`](../reference/class-hierarchy.md).
- [ ] Constructors/destructors — partially done (trap builders only, see Phase 4).
- [ ] Import class names as IDA structs for `this`-pointer retyping.

**Exit criterion met** (853 ≥ 600).

### Notable structural findings

`Trap` and `PrinceObject` share an identical base chain:

```
NiObject -> NiAVObject -> NiNode -> NiCollisionSwitch -> CharacterObject
         -> CharacterStateTable -> { Trap | PrinceObject }
```

So traps are not a separate entity kind — every trap *is* a character with a state table. That
explains why traps can play skills and why `data/Traps.txt` lists skill names as trap parameters.

Classes not visible in the string dump but recovered here: `LevelFileParser` (the `.lvl`
loader), `NeighborParser`, `LevelGroup`, `LevelUpdate`, `RoomGroup`, `k2SceneGraph`,
`k2SceneHub`, `K2File`, `Real_File`, `K2Config`, `K2ImportSet`, `MenuParser`, `K2ImportParser`,
`M3GameClock`, `K2BoyAppWorld`, `MfAi*` family, and a full `async*` file-I/O layer
(`asyncCreateFile`, `asyncReadFile`, `asyncFindFirstFile`, …) — the CD streaming subsystem.

A whole performance-reporting subsystem is also linked in: `K2TriStats`, `K2TriReport`,
`K2FrameStats`, `K2FrameReport`, `K2TextureStats`, `K2D3DTextureReport`, `K2StatsReport`,
`K2DAGReport` (+ nested `K2SummedStats`, `K2SummedTextureStats`, `K2CAPStats`, `K2BoundStats`).

---

## Phase 2 — Format loaders (1 week)

For each format, the pattern is the same: **magic constant → xref → the sniff function → the
reader**. All the magics are in [../formats/00-overview.md](../formats/00-overview.md#magic-number-quick-reference).

Priority order (highest confidence gain per hour first):

| # | Target | Entry point | Doc to update |
|---|---|---|---|
| 1 | `.sfx` | `SFXFile` vtable; `"WI"` = `0x4957` | [sfx.md](../formats/sfx.md) — confirm only |
| 2 | `.act` | **property table decoded + validated on all 118 files** → [act.md](../formats/act.md). Open: node / articulation data | [act.md](../formats/act.md) |
| 3 | `.skl` | **JKey record confirmed** → [skl.md](../formats/skl.md); per-channel float meaning still open | — |
| 4 | trap factory | **DONE** → [`../reference/trap-factory.md`](../reference/trap-factory.md) | — |
| 5 | `save.bin` / linkmap | `"linkmap size = "` xref; `RsrcListManager::SaveListClass` | [linkmap-save.md](../formats/linkmap-save.md) |
| 6 | `.pop` | **container framing confirmed** (all 20 sections); `game header`, prince/enemy location, `rooms info`, `neighbors` decoded. All ints/floats/strings **big-endian**. Open: `module`, `resource list`, `prince body info`, `stream buffer` | [savegame.md](../formats/savegame.md) |
| 7 | `.lvl` + `.nbr` | **both parsers decoded** → [lvl.md](../formats/lvl.md). Both are **text**. Remaining: `.fms` | [lvl.md](../formats/lvl.md) |
| 8 | text/tuning parsers | `"Bad audio data in %s line %d"`, `CommandLineParser`, `UserTextFileParser`, `CameraFileParser` — `NeighborParser` **done** | [text-and-tuning.md](../formats/text-and-tuning.md) |

For each: write a Python decoder, run it over **every** file of that type in the install, and
assert no leftover bytes. A decoder that round-trips 2363 `.skl` files is proof; one that parses a
single sample is a hypothesis.

**Exit criterion:** every "Inferred" marker in `docs/formats/` is either promoted to "Confirmed"
or explicitly recorded as still-unknown with the reason.

---

## Phase 3 — The script VM and reflection table — **done except opcode mnemonics**

This is where the game's actual logic lives.

- [x] **VM interpreter + opcode map recovered** → [`../reference/vm-isa.md`](../reference/vm-isa.md).
      `MfScVirtualMachine__Execute` at `0x6CF883` (13 155 bytes), two adjacent dispatch tables
      covering **89 opcodes** / 88 distinct handlers. Machine model decoded: stack machine
      (stack grows **down**), four registers R0–R3, byte-sized signed operands for local and
      global slots, 16-bit refcount on every value. Opcodes 1–22 and 44–45 decoded; the rest are
      mapped by handler address only.
- [x] **Scripting API recovered** → [`../reference/script-api.md`](../reference/script-api.md).
      29 script-visible classes, 250 methods, 241 parameter descriptors, 83 properties.
      The table is *not* static data — it is built at runtime by one registrar function per class
      (`Script_Register_<Class>`, all 28 findable ones now named). Recovery was done by scanning
      `.text` for `push imm32` operands pointing at the reflection strings and grouping by push
      order, not by decompiling.
      All 30 registrars are now defined and named — `Transform` (`0x6C6491`) and `World`
      (`0x655F6A`) needed manual `define_func` with explicit bounds because they use the MSVC
      `__EH_prolog` calling pattern (`mov eax, handler; call __EH_prolog`) rather than a
      `push ebp; mov ebp, esp` prologue, so IDA's auto-analysis had skipped them.
- [ ] Decode the `.pobj` container fully now that you know what a compiled routine looks like.
- [ ] Recover the `.bhvr` grammar from `MfStateCompiler`'s keyword table.
- [ ] Determine whether `PiccoloRT.dll` is actually loaded or is a dead `LoadLibraryA` path.

- [x] Value-object layout, all three operand encodings, and the full 89-opcode family
      classification → [`../reference/vm-isa.md`](../reference/vm-isa.md).

**Exit criterion partly met:** `script-api.md` is done. A `.pobj` disassembler is now writable at
the family level (jumps, operand widths and the fetch loop are all confirmed) but individual
operator mnemonics are not resolved, and no non-trivial `.pobj` ships in this install to test
against.

---

## Phase 4 — Gameplay systems (open-ended)

Ordered by likely interest:

- [ ] **Trap system.** `TrapFactory::Builder` dispatch table → all 34 trap classes.
      Cross-reference every parameter slot against `data/Traps.txt`, and complete the entries the
      original developers left blank. This produces a better document than shipped.
- [ ] **Prince controller.** `PrinceObject`, `PrinceMaster`, `PrinceMotion`, `PrinceFloor`,
      `CharacterStateTable`. How ledge-grab / hang / pull-up decisions are made — the locomotion
      planner (`MfLocoPlanner`, `MfLocoPlanReqData`, `MfLocoPlanResultData`) is the interesting
      part and is genuinely novel for a 1999 game.
- [ ] **Combat.** `WeaponParameters` + `data/fight.ini` + `CharacterStateTable`.
- [ ] **Camera.** `CameraLogic`, `CameraStateTable`, `data/camera.dat`.
- [ ] **Enemy AI.** `EnemyAIObject` and its nested `AnimatronicState` array + `data/enemy.dat`.

---

## Phase 5 — Re-enable the developer tooling (high leverage, do opportunistically)

Everything below is **still compiled into the retail binary**:

- `AConsole` — in-game console. `data/console.dat` proves `minrate` is a valid command. Find the
  command table, dump it, find the key that opens the console (or force it open with a patch).
- `PMW_3dDisplayWindow`, `PMW_GeometryWindow`, `PMW_MemoryWindow`, `PMW_DutyCycleWindow`,
  `PMW_ClientWindow` — runtime inspector windows.
- `MfLoggerWindow`, `MfLoggerFrame`, `MfLoggerFrameUI`, `MfLoggerOptions`, `MfLogMessageDialog` —
  the Motivate logger UI.
- `MfScBreakpointManager`, `MfScDebugInfo`, `MfScSourceResolver`, `MfPdbScriptProvider` — a
  script debugger.
- `DebugMemoryUsage`, `DebugTextManager`, `DumpSkillActivity`, `checkMemoryAction`.
- The `-warn`, `-d`, `-cmd` command-line flags.

Getting the console open changes the economics of every later phase: you can test hypotheses at
runtime instead of by static reading. **If it is cheap, do this during Phase 2, not Phase 5.**

---

## Phase 6 — Deliverables

- [ ] `docs/formats/` complete, every field confirmed or explicitly open.
- [ ] `docs/reference/class-hierarchy.txt`, `script-api.md`, `trap-parameters.md`.
- [ ] A Python package that reads `.act`, `.skl`, `.sfx`, `.K2A`, `linkmap`/`save.bin`, `.pop`.
- [ ] The IDA database, with a naming convention documented so a second person can contribute.

---

## Risks and unknowns

| Risk | Impact | Mitigation |
|---|---|---|
| `.lvl` / `.nbr` / `CONFIG.ENT` / `PiccoloRT.dll` absent | Can't validate the level loader | Source a CD image; meanwhile use linkmap/save pairs as ground truth |
| `.skl` joint payload not a plain pos+quat | Animation converter blocked | Confirm against `MfJointKeyframe` before writing any converter |
| Motivate is proprietary middleware | Some structures may be opaque | Motivate shipped commercially with documentation; search for the SDK/manual before reversing |
| Running the game to validate | Wine on macOS, DirectDraw/DirectInput era | `data/pop3dreal.exe` bypasses SafeDisc; test early whether it launches at all |
| Scope creep into NetImmerse internals | Weeks lost | Hard rule: if a class starts with `Ni`, look it up, don't reverse it |
