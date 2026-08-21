# IDA setup

## Which binary to load

**`data/pop3dreal.exe`.** Not `data/pop3d.exe`, not `data/pop3d.icd`.

| Binary | Load it? | Why |
|---|---|---|
| `data/pop3dreal.exe` | **yes** | The complete, unprotected game. 3.4 MB of code, full RTTI. |
| `data/pop3d.icd` | no | Same build, SafeDisc-encrypted. Identical code ±0x42 bytes. |
| `data/pop3d.exe` | only if studying SafeDisc | 280 KB copy-protection loader, MSVC 5, sections `.txt`/`.text`/`.txt2`. |
| `data/a3dapi.dll` | no | Third-party Aureal A3D runtime. |

If you ever *do* need to know what SafeDisc changed, diff `data/pop3dreal.exe` against `data/pop3d.icd`
rather than unpacking anything — they are the same build 1h32m apart.

## Load settings

```
Processor      metapc (x86, 32-bit)
File type      Portable executable for 80386 (PE)
Image base     0x400000
Loading offset 0
Entry point    0x530abd
```

Convenient property: **`SectionAlignment == FileAlignment == 0x1000`**, so for `data/pop3dreal.exe`
file offset `== RVA` exactly. `VA = 0x400000 + file_offset`. You can hexdump the file and compute
IDA addresses by adding `0x400000`, with no section-table arithmetic.

```
.text   VA 0x401000 .. 0x74244a
.rdata  VA 0x743000 .. 0x7ae328
.data   VA 0x7af000 .. 0x8ba71c
.rsrc   VA 0x8bb000 .. 0x8be410
```

Enable in *Options → Compiler*: `Visual C++`, calling convention `__thiscall` default for
methods. This matters — most of the binary is C++ methods and getting `ecx` treated as `this`
saves enormous time in the decompiler.

## Before you start analysing

1. Let auto-analysis run to completion and **save the database**. 3.4 MB of x86 takes a while.
2. `File → Load file → FLIRT signature file` and apply **MSVC 6** runtime signatures
   (`vc32rtf`, `vc32mfc` if it resolves). This removes several hundred CRT/iostream functions
   from your working set immediately. The RTTI list shows heavy `std::` iostream and `locale`
   facet use, so the win is large.
3. Run *Options → General → Analysis → Reanalyze program* once after signatures are applied.

## Verify the MCP server is on the right database

`ida-mcp` talks to whichever IDA instance is running. Sanity-check before doing any work:

```
list_funcs {"offset":0,"count":5}          -> should show addresses ≥ 0x401000
find_regex "K2boy"                          -> should return C:\k2\K2boy\Source\... hits
find_regex "NetImmerse"                     -> should hit
```

If `find_regex "K2boy"` returns nothing, you are pointed at a different database.

> **Known gotcha:** at the time these notes were written the attached database was a
> *different game* — a Watcom-compiled binary with `hqr_init_resource`, `pak_unpack_legacy_lz`,
> `palette_set_range`, `body_render_or_walk`. If you see those names, the MCP server is
> connected to the wrong IDA instance. Close it and reopen with `data/pop3dreal.exe`.

## Naming convention

Adopt one and stick to it, because RTTI recovery generates thousands of names:

```
vtbl_<Class>                    vtable
<Class>::<method>               recovered method  (IDA accepts :: in names)
<Class>::ctor / ::dtor          constructors/destructors
<Class>::Load / ::Save          serializers
g_<name>                        globals
sub_XXXXXX                      leave unnamed rather than guessing
```

Prefix speculative names with `maybe_` so a later pass can find and confirm them:
`maybe_TrapFactory::Build`.

## Useful ranges to bookmark

| What | Where |
|---|---|
| RTTI type descriptors | `.data`, search for `.?AV` |
| String literals | `.rdata` |
| Import thunks | IAT at RVA `0x343000`, size `0x4d4` |
| Entry point | `0x530abd` |
| Source paths | search `.data` for `C:\k2\K2boy\Source` |

## Companion artefacts already extracted

- [`../reference/rtti-classes.txt`](../reference/rtti-classes.txt) — all 818 class names, sorted.
  Use it as a checklist while doing Phase 1.
- Regenerate the raw string dump any time with:
  ```
  strings -n 6 data/pop3dreal.exe > /tmp/pop3d_strings.txt
  ```
