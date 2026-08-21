# `.bhvr` / `.pobj` / `.sef` — behaviors, scripts, effect tables

## Overview

POP3D embeds Motion Factory's **Piccolo** scripting language and its **HFSM** (hierarchical finite
state machine) behavior system. Two file types back it:

- **`.bhvr`** — human-readable HFSM source
- **`.pobj`** — compiled Piccolo object file (bytecode)

Only one of each survives in this install (`data/Actors/BlankCamera/Behaviors/Untitled_0.*`); the rest
shipped inside the missing `.lvl` containers or were compiled into the CD data. Both are worth
specifying now because the *runtime* for them is fully present in `data/pop3dreal.exe`.

Supporting evidence in the binary:

```
PiccoloRT.dll                PICCOLOPATH               PICCOLOLIB
PiccoloCompilerI             mf_hfsm                   UnloadHFSM
HFSM::SetChoiceTransition    HFSM.SetChoiceTransition()
"Error compiling behavior Piccolo code"
"Could not open Piccolo Object file."
"Cannot retrieve functions from a Piccolo Object file."
"An attempt was made to replace a Piccolo Object ..."
"' the output of the Piccolo ..."
"String hfsmName"
```

`PiccoloRT.dll` is **not in this install** and is not in the import table — it is loaded via
`LoadLibraryA`. Either it shipped on CD or the runtime is statically linked and the DLL path is a
dead fallback. Determining which is a Phase 2 task.

---

## `.bhvr` — HFSM source

**Status: confirmed** (format is text; the sample is complete and tiny).

```
mf_hfsm Untitled_0
{
	camera_setting{22.9008 79.3650 1000.00 1000.00}
}
```

Grammar as far as the sample shows:

```
file      := "mf_hfsm" WS name CRLF "{" CRLF body "}" CRLF
body      := statement*
statement := ident "{" args "}"
args      := token (WS token)*
```

Tabs for indentation, CRLF line endings, no comments observed. `camera_setting` takes four
floats — for `BlankCamera` these read as FOV/near/far-ish values (`22.9008 79.3650 1000.00
1000.00`).

The real grammar is much larger: `MfStateCompiler`, `MfState`, `MfStateTransition`,
`MfStateEntryAction`, `MfStateExitAction`, `MfStateEntryPort`, `MfStateExitPort`,
`MfStateChoicePoint`, `MfStateInitVar`, `MfStateVariables`, `MfStateFunctionDef`,
`MfTransTriggeredBy`, `MfTransEnterBy`, `MfTransExitBy`, `MfTransChoicePt`, `MfTransDestState`,
`MfTransInit`, `MfTransTrigAction`, `MfTransitionType`, `MfTransitionProperty`,
`MfStateBeginHistory`, `MfStateDisplayPos` all have RTTI. Each of those classes corresponds to a
production in the `.bhvr` grammar. **Recovering the full grammar = enumerating the keyword table
that `MfStateCompiler` dispatches on.** That is the single highest-value target in this format
family.

---

## `.pobj` — Piccolo object file

**Status: header only.**

```
00000000  4d 4a 53 50  00 00 0a 02  24 00 00 00  30 00 00 00
          "MJSP"       version?     0x24         0x30
00000010  3c 00 00 00  ff ff ff ff  ff ff ff ff  ff ff ff ff
          0x3c         -1           -1           -1
00000020  ff ff ff ff  01 00 00 00  0c 00 00 00  00 00 00 00
00000030  02 00 00 00  0c 00 00 00  00 00 00 00  03 00 00 00
00000040  44 00 00 00  01 00 00 00  38 00 00 00  01 00 00 00
00000050  00 00 00 00  eb 95 85 34  b0 b0 b0 b0  b0 b0 b0 b0
00000060  b0 b0 ...  (0xb0 fill to EOF at 0x80)
```

Reading:

```c
struct PobjHeader {
    char     magic[4];      // "MJSP"  ('PSJM' LE)
    uint32_t version;       // 0x020a0000
    uint32_t offTableA;     // 0x24
    uint32_t offTableB;     // 0x30
    uint32_t offTableC;     // 0x3c
    int32_t  reserved[4];   // all -1
    // 0x24: { id=1, size=0x0c, 0 }
    // 0x30: { id=2, size=0x0c, 0 }
    // 0x3c: { id=3, size=0x44, count=1, off=0x38, 1, 0 }
    // 0x54: uint32 0x348595eb   — checksum / build stamp
    // 0x58..0x80: 0xB0 padding
};
```

`0xB0` fill (rather than `0x00` or `0xCC`) is distinctive — grep for it when identifying other
`.pobj` blobs embedded in `.lvl` files.

The file is 128 bytes and essentially empty (`Untitled_0` compiles to nothing), so this tells us
the *container* shape but nothing about the **bytecode instruction set**. That must come from
`MfScVirtualMachine` / `MfScBytecodeRoutine` in the binary — see the plan.

---

## `.sef` — special effect table

**Status: confirmed.** `data/Geometry/Utils/SpecialEffects.sef`, 3958 bytes, tab-separated, CRLF.

```
<effectName>\t\t\t\t\tgeometry\utils\<file>.nif\r\n
```

Padding tabs align the second column; a parser must collapse runs of tabs. Blank lines group
related effects (`ArrowNormal` + `ArrowNormalImpact`, then `ArrowFire`, …).

Consumed by `k2SpecialEffectFactory`, `k2SpecialEffectController`, `k2FileEffectController`,
`k2SpecialEffectGroup<k2SpecialEffectCreator,6>` — note the template constant `6`, meaning effects
are bucketed into 6 groups.

Related trap type: `SpecialEffect` (with its `Builder@SpecialEffect` factory), plus
`PrecipEffect`, `SpurtManager`, `SingleSpurt`, `SingleSpurtPuddle`, `Swarm`, `k2ArrowTrail`,
`BlurController`, `WeatherObject`.

---

## How to confirm in IDA

1. **`.bhvr` grammar** — xref `"mf_hfsm"` in `.rdata`. The function that compares it is the parser
   entry; the keyword table sits nearby. Also xref `"Error compiling behavior Piccolo code"`.
2. **`.pobj` loader** — xref `"Could not open Piccolo Object file."` and
   `"Cannot retrieve functions from a Piccolo Object file."`. Both land in
   `MfScPiccoloObjectFile`'s load path, which walks the tables above.
3. **Bytecode ISA** — find `MfScVirtualMachine`'s vtable, then its execute loop. It will be a
   large `switch` or a jump table indexed by opcode; that table *is* the ISA. `MfScVmEnv`,
   `MfScVariableStack`, `MfScVmCallStackList` give you the machine model (stack VM with call
   frames), and `MfScGcThread` / `MfScGCWork` confirm it is garbage-collected.
4. **Script API surface** — every `MfPo*` class is a script-visible type. `MfScFunctionRegistry`,
   `MfScFunctionRegistryEntry`, `MfScFunctionRegistryModule`, `MfScCiMethod`, `MfScCiProperty`,
   `MfScClassInfo`, `MfScClassInfoText` and `MfScMethodParameterInfo` mean the binary contains a
   **self-describing reflection table with parameter names and types**. Dumping that table
   recovers the complete scripting API — names, signatures, and all — without reading a single
   line of decompiled code. Do this early; it is the cheapest big win in the project.
5. `"String hfsmName"` and `"HFSM.SetChoiceTransition()"` look like entries from that same
   reflection table, which is corroborating evidence that it is dumpable as data.
