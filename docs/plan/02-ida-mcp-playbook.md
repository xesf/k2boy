# IDA MCP playbook

Concrete recipes against the `ida-mcp` tool set. Tool names are given exactly as exposed.

## Tool inventory

| Group | Tools |
|---|---|
| Navigate | `list_funcs`, `lookup_funcs`, `list_globals`, `imports`, `disasm`, `decompile`, `basic_blocks` |
| Search | `find`, `find_regex`, `find_bytes`, `search_structs` |
| Read | `get_bytes`, `get_int`, `get_string`, `get_global_value`, `read_struct` |
| Xrefs | `xrefs_to`, `xrefs_to_field`, `callees`, `callgraph` |
| Annotate | `rename`, `set_type`, `set_comments`, `declare_type`, `declare_stack`, `delete_stack`, `stack_frame` |
| Define | `define_func`, `define_code`, `undefine` |
| Modify | `patch`, `patch_asm`, `put_int` |
| Misc | `export_funcs`, `int_convert`, `debug_mode`, `hexrays_diag` |

`decompile` works on IDA **Free** — it drives the GUI F5 and scrapes the pseudocode widget, so no
Hex-Rays SDK is required. It is slower than native decompilation; batch your calls.

---

## Sanity check first

```
list_funcs   {"offset":0,"count":5}
find_regex   "K2boy"
find_regex   "NetImmerse File Format"
```

If `K2boy` misses, you are on the wrong database. See
[01-ida-setup.md](01-ida-setup.md#verify-the-mcp-server-is-on-the-right-database).

---

## Bulk RTTI recovery

The core Phase 1 loop. MSVC RTTI layout:

```
vtable[-1]  -> RTTICompleteObjectLocator
                 +0x00 signature (0)
                 +0x04 offset
                 +0x08 cdOffset
                 +0x0C pTypeDescriptor      -> TypeDescriptor
                 +0x10 pClassHierarchyDescriptor
TypeDescriptor
                 +0x00 pVFTable (type_info vtable)
                 +0x04 spare
                 +0x08 name[]  ".?AVMfPoActor@@"
```

Recipe:

1. Find every type descriptor name:
   ```
   find_regex "\\.\\?AV[A-Za-z0-9_?$@]+@@"   limit 500        (repeat with offset)
   ```
   Or work from the pre-extracted [`../reference/rtti-classes.txt`](../reference/rtti-classes.txt).

2. For a given name string at address `S`, the TypeDescriptor starts at `S - 8`. Find who points
   at it:
   ```
   xrefs_to  <S-8>
   ```
   → the `RTTICompleteObjectLocator` (field `pTypeDescriptor`).

3. Find who points at the locator:
   ```
   xrefs_to  <locator_addr>
   ```
   → the word immediately **before** the vtable. Vtable address = `locator_ref + 4`.

4. Name it and find the constructors:
   ```
   rename    <vtable_addr>  "vtbl_MfPoActor"
   xrefs_to  <vtable_addr>
   ```
   Every xref that writes the vtable into `[ecx]` is a constructor or destructor.

5. Declare the class so you can retype `this`:
   ```
   declare_type "struct MfPoActor { void **vtbl; };"
   set_type     <ctor_addr>  "void __thiscall MfPoActor__ctor(MfPoActor *this);"
   ```

Do steps 1–4 in a loop over all 818 names. Expect ~600+ to resolve; template instantiations and
`std::` facets account for most misses.

### Inheritance

`RTTICompleteObjectLocator + 0x10` → `RTTIClassHierarchyDescriptor`:

```
+0x00 signature   +0x04 attributes   +0x08 numBaseClasses   +0x0C pBaseClassArray
```

`read_struct` / `get_int` over `pBaseClassArray` gives `numBaseClasses` pointers to
`RTTIBaseClassDescriptor`, each of which points at a TypeDescriptor. That is the full base list,
in order. Dump it to `docs/reference/class-hierarchy.txt`.

---

## Format-loader recipes

### Find a loader from its magic constant

Magics are 4-byte little-endian integers. Use `find_bytes` (byte pattern) or `find` (immediate):

| Format | ASCII | LE constant | Search |
|---|---|---|---|
| `.act` | `APFM` | `0x4D465041` | `find_bytes "41 50 46 4D"` |
| `.act` alt | `" MFB"` | `0x42464D20` | `find_bytes "20 4D 46 42"` |
| `.act` type | `ActP` | `0x50746341` | `find_bytes "41 63 74 50"` |
| `.skl` | `kSFM` | `0x4D46536B` | `find_bytes "6B 53 46 4D"` |
| `.skl` kinds | `Skil`/`MSkl`/`LSkl` | — | `find_bytes "53 6B 69 6C"` etc. |
| `.skl` chunks | `JKey`/`SKey`/`vKey` | `0x79654B4A` … | `find_bytes "4A 4B 65 79"` |
| `.pobj` | `MJSP` | `0x50534A4D` | `find_bytes "4D 4A 53 50"` |
| `.sfx` | `WI` (u16) | `0x4957` | `find "0x4957"` |

Then:

```
xrefs_to <hit>       -> the comparison site
decompile <caller>   -> the sniff + dispatch
```

The dispatch on `.skl` chunk tags is a single function; decompiling it yields the whole format.

### Find a loader from a format string

Usually faster than magic hunting.

```
find_regex "linkmap size"                       -> linkmap writer
find_regex "obj %d trap type"                   -> tempActiveLog writer, gives Trap layout
find_regex "game header"                        -> savegame driver
find_regex "NIS\\.sav"                          -> cutscene bitmask
find_regex "MF_bndRadius"                       -> .act property table registration
find_regex "Bad audio data in"                  -> audio .dat parser
find_regex "Could not open Piccolo Object file" -> .pobj loader
find_regex "Error compiling behavior Piccolo"   -> .bhvr compiler
find_regex "mf_hfsm"                            -> .bhvr keyword table
find_regex "%s\\\\%s\\\\%s\\.act"               -> actor path builder
```

Then `xrefs_to` the string address, `decompile` each caller.

---

## Recovering the trap factory table

The single highest-yield structural find. Every trap type has a `Builder@<Type>` RTTI class,
which means a factory registration table exists.

```
find_regex "\\.\\?AVBuilder@"            # ~34 hits
```

For each, resolve the vtable (RTTI recipe above), then:

```
xrefs_to <vtbl_Builder@Door>
```

All 34 builder vtables will be referenced from the same initialization function — that function
*is* the registration table. `decompile` it once and you have the ASCII-name → class mapping for
the whole trap system, matching the `<Type>` strings in `data/prisonfix_tempActiveLog.txt`.

Cross-check the parameter slots against
[`../formats/text-and-tuning.md`](../formats/text-and-tuning.md#trapstxt--trap-parameter-reference-design-doc).

---

## Dumping the script reflection table

`MfScClassInfo`, `MfScCiMethod`, `MfScCiProperty`, `MfScMethodParameterInfo`,
`MfScFunctionRegistry`, `MfScFunctionRegistryEntry`, `MfScFunctionRegistryModule` all carry RTTI.
The strings `"String hfsmName"` and `"HFSM.SetChoiceTransition()"` look like entries from it.

```
find_regex "HFSM\\.SetChoiceTransition"
xrefs_to <hit>
```

If the xref lands in a **data** array rather than code, the table is static data and can be walked
with `read_struct` / `get_string` — no decompilation needed. Determine the record stride from two
adjacent entries, then dump the whole array to `docs/reference/script-api.md`.

If it lands in code, find `MfScFunctionRegistry::Register`-shaped functions and enumerate their
callers instead.

---

## Finding the console command table

```
find_regex "minrate"
xrefs_to <hit>
```

**This recipe does not work, and the reason is instructive.** `minrate` is *not* in the binary at
all — `data/console.dat` is a leftover. There is no `{ name, handler }` array to dump: the game's
console is reached from Piccolo script. Command-line flags are matched one character at a time,
so they leave no string literals either. See
[`../reference/command-line.md`](../reference/command-line.md).

---

## Locating the command-line parser

```
find_regex "^-zp$"
find_regex "^-warn$"
lookup_funcs "CommandLineParser"      # after Phase 1 naming
```

The parser is a chain of `strcmp`s or a table. Recovering it documents every undocumented flag in
[`../formats/text-and-tuning.md`](../formats/text-and-tuning.md#command-line-dat-files).

---

## Working efficiently

- **Batch independent calls.** `xrefs_to` on 20 addresses in one turn, not 20 turns.
- **`disasm` before `decompile`.** Disassembly is cheap; the GUI-driven decompiler is not. Only
  decompile when you actually need pseudocode.
- **`export_funcs`** to snapshot the function list to disk after each phase, so progress is
  diffable and recoverable.
- **`set_comments` liberally.** Record *why* you named something, not just what. Six weeks later
  the reasoning is what you will have lost.
- **Verify with data, not reading.** Every hypothesis about a format should be checked by running
  a Python decoder over all files of that type. 2363 `.skl` files is a strong test set; one file
  is not.
- **`patch` / `patch_asm` are for experiments only.** Keep patches in a separate database copy, or
  the "real" database stops matching the shipped binary and every later offset is suspect.
- **`debug_mode`** — if `data/pop3dreal.exe` runs under Wine, live debugging beats static reading for
  the `.skl` keyframe question and the save-format question. Try this early.

---

## Test vectors

Smallest useful specimen per format — use these as the fixtures for both IDA breakpoints and
Python decoders:

| Format | File | Size |
|---|---|---:|
| `.skl` | `data/Actors/FIX_IceBlock/Skills/Melt.skl` | 756 |
| `.act` | `data/Actors/PICK_Gem/PICK_Gem.act` | 2 420 |
| `.act` (alt magic) | `data/Actors/arrow/arrow.act` | — |
| `.sfx` one-shot | `data/Audio/sfx/A1deathv001.sfx` | 30 198 |
| `.sfx` looping | `data/Audio/sfx/L01cranehoist001.sfx` | 65 994 |
| linkmap + save | `data/solar1_linkmap.txt` + `data/solar1_save.bin` | 391 objs / 8 312 |
| `.pop` | `data/Quick Save.pop` | 14 017 |
| `.pobj` | `data/Actors/BlankCamera/Behaviors/Untitled_0.pobj` | 128 |
| `.bhvr` | `data/Actors/BlankCamera/Behaviors/Untitled_0.bhvr` | 76 |
| ground truth | `data/prisonfix_tempActiveLog.txt` | 9 583 |
