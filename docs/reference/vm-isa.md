# Piccolo VM — interpreter, machine model, opcode map

**Status: dispatch map confirmed. Opcodes 1–22 and 44–45 decoded individually; 66–75 decoded as a
jump band; 22–37, 38–43, 48–51 and 52–89 classified into families. Value-object layout and all
three operand encodings confirmed.**

The interpreter is a single function:

| Address | Name | Size |
|---|---|---|
| `0x6CF883` | `MfScVirtualMachine__Execute` | `0x3363` (13 155 bytes) |

Renamed in the database. Hex-Rays handles it poorly at this size — read it as disassembly.

Related strings confirming terminology: `"Illegal bytecode"`,
`"          Bytecode Offset: %08x (%d bytecodes)"`.

## Dispatch

Two jump tables, adjacent in `.rdata`, both consumed by the same function:

| Table | Address | Entries | Opcodes |
|---|---|---:|---|
| `g_VmDispatch_op1_45` | `0x6D2BE6` | 45 | 1–45 |
| `g_VmDispatch_op47_89` | `0x6D2C9A` | 43 | 47–89 |

Opcode **46 (`0x2E`)** is handled inline by a `jz` before the first table, and opcode **0** falls
through to the default (error) path. Total instruction set: **89 opcodes**, 88 distinct handlers,
all inside `MfScVirtualMachine__Execute`. No handler is shared, and none points outside the
function — so the table bounds are exact.

```asm
; primary dispatch @ 0x6CF93E
movzx eax, byte ptr [ebp-21h]   ; fetch opcode byte
cmp   eax, 2Eh
jg    dispatch_high             ; > 46 -> second table
jz    case_46                   ; == 46 -> inline
dec   eax
cmp   eax, 2Ch
ja    illegal_bytecode          ; 0 or > 45
jmp   ds:g_VmDispatch_op1_45[eax*4]

; secondary dispatch @ 0x6D10B5
cmp   eax, edi
jg    illegal_bytecode
jz    case_max
add   eax, -2Fh                 ; -47
cmp   eax, 2Ah
ja    illegal_bytecode
jmp   ds:g_VmDispatch_op47_89[eax*4]
```

## Machine model

`esi` holds the VM state object throughout. Field offsets recovered from the handlers:

| Offset | Meaning |
|---|---|
| `[esi+0x08]` | **PC** — pointer into the bytecode stream; handlers `inc` it past their operands |
| `[esi+0x0C]` | **SP** — operand stack pointer. **Stack grows downward**: push is `add [esi+0Ch], -4`, pop is `add [esi+0Ch], 4` |
| `[esi+0x10]` | **locals** base — indexed as `[base + operand*4]` |
| `[esi+0x14]` | **globals** base — null check raises error code 2 |
| `[esi+0x18]` | a single implicit slot (pushed by opcode 15; likely `self`/`this`) |
| `[esi+0x20]` | **R0** |
| `[esi+0x24]` | **R1** |
| `[esi+0x28]` | **R2** |
| `[esi+0x2C]` | **R3** |

So: a **stack machine with four auxiliary registers**, byte-sized signed operands for local and
global slot indices.

Values are **refcounted**. `DUP` increments (`inc word ptr [eax]` — a 16-bit refcount at offset 0
of the object) and every register-clearing opcode calls a release helper before overwriting. This
matches the `MfScGcThread` / `MfScGCWork` garbage-collector classes.

## Decoded opcodes

| Op | Hex | Handler | Meaning |
|---:|---|---|---|
| 1 | `0x01` | `0x6CF963` | `PUSH_LOCAL sb` — `push locals[sbyte operand]` |
| 2 | `0x02` | `0x6CF978` | `PUSH_GLOBAL` — null-checks `[esi+14h]`, raises error 2 if unset |
| 3 | `0x03` | `0x6CF99D` | `STORE_LOCAL sb` — pop into `locals[sbyte operand]`, releases old value |
| 4 | `0x04` | `0x6CF9CF` | reads one byte operand, pushes it as an argument |
| 5 | `0x05` | `0x6CF9DC` | call helper `0x6CFF2C` |
| 6 | `0x06` | `0x6CF9E4` | call helper `0x6CFF49` |
| 7 | `0x07` | `0x6CFA32` | `PUSH_R0` |
| 8 | `0x08` | `0x6CFA3A` | `PUSH_R1` |
| 9 | `0x09` | `0x6CFA42` | `PUSH_R2` |
| 10 | `0x0A` | `0x6CFA4A` | `PUSH_R3` |
| 11 | `0x0B` | `0x6CFA52` | `POP_R0` — release old R0, pop stack into R0 |
| 12 | `0x0C` | `0x6CFA73` | `POP_R1` |
| 13 | `0x0D` | `0x6CFA94` | `POP_R2` |
| 14 | `0x0E` | `0x6CFAB5` | `POP_R3` |
| 15 | `0x0F` | `0x6CFB3A` | `PUSH_SELF` — pushes `[esi+18h]` |
| 16 | `0x10` | `0x6CFAD6` | `CLEAR_R0` — release and zero R0 |
| 17 | `0x11` | `0x6CFAEF` | `CLEAR_R1` |
| 18 | `0x12` | `0x6CFB08` | `CLEAR_R2` |
| 19 | `0x13` | `0x6CFB21` | `CLEAR_R3` |
| 20 | `0x14` | `0x6CFB3F` | `SWAP` — exchanges the two slots at `[sp+4]` and `[sp+8]` |
| 21 | `0x15` | `0x6CFB56` | `DUP` — addref top of stack, push copy |
| 22 | `0x16` | `0x6CFB71` | pops two, null-checks both, then branches (comparison or send) |
| 44 | `0x2C` | `0x6CF9F7` | counted loop over a byte operand, calls `0x6CEBB6` per iteration |
| 45 | `0x2D` | `0x6CFA15` | same shape as 44 but the count comes from a helper call |

The four register groups (`PUSH_Rn` / `POP_Rn` / `CLEAR_Rn`) are byte-identical apart from the
`+0x20/24/28/2C` displacement, which is what makes the register file unambiguous.

## Value object layout — confirmed

`0x6CBC6D` is a tiny helper that initialises an integer value; it gives the object header away:

```asm
mov  eax, ecx
and  dword ptr [eax + 4], 0     ; value = 0
mov  word ptr [eax], 1          ; refcount = 1
mov  word ptr [eax + 2], 2      ; typeTag = 2
ret
```

```c
struct MfScValue {          // 16 bytes allocated by 0x6CC6E6
    uint16_t refcount;      // +0   DUP increments this
    uint16_t typeTag;       // +2   2 = integer
    uint32_t value;         // +4
};
```

That settles the refcount field inferred earlier from `DUP`. Type checks throughout the
interpreter read the **low byte of `typeTag`** and test masks `0x10` and `0x30`, so the tag is a
bitfield of type classes rather than a dense enum. Integer's tag (2) fails the `0x10` test, so
`0x10` and `0x30` select other classes; which ones is not established.

`0x6CC6E6` is the value allocator (MSVC `__EH_prolog` frame, allocates 0x10 bytes).

## Operand widths — confirmed

Three encodings, selected by opcode rather than by a prefix:

| Width | How | Reader |
|---|---|---|
| signed byte | `movsx ecx, byte ptr [PC]` inline, `PC++` | — |
| signed 16-bit | `lo = *PC++; hi = (int8)*PC++; return (hi << 8) \| lo` | `0x6D2F2A` |
| wider (24/32-bit) | same pattern, three-plus bytes | `0x6D2F4B` |

## Opcode families

Every handler was disassembled with Capstone and fingerprinted by which VM fields it touches,
its operand width, its type-mask tests and its call targets. That yields families rather than
individual mnemonics for the untouched opcodes.

### Jump band — opcodes 66–74

Nine opcodes forming a clean **3 kinds × 3 operand widths** grid. Each computes a new `PC` from a
relative offset:

| | sbyte | int16 | int32 |
|---|---|---|---|
| unconditional `JMP` | **66** | **67** | **68** |
| conditional (pops 1, null-checks; raises error 6 on null) | **69** | **70** | **71** |
| conditional (pops 1, type-mask `0x10`, then tests `value != 0`) | **72** | **73** | **74** |

Opcode 66 is the clearest: `movsx ecx, byte [PC]; lea eax, [ecx + PC - 1]` — a PC-relative branch.

### Typed operator band — opcodes 22–37

Sixteen opcodes sharing one shape: pop operand(s), test the type tag, call the allocator
(`0x6CC6E6`) then `0x6CBC6D`, push the result.

```asm
add  dword ptr [esi + 0xC], 4      ; pop
mov  eax, dword ptr [esi + 0xC]
mov  edi, dword ptr [eax]
test byte ptr [edi + 2], 0x30      ; type-class check
push 0 ; push 8
call 0x6CC6E6                      ; allocate result
mov  ecx, eax
call 0x6CBC6D                      ; init as integer
```

Split by type mask:

- **mask `0x30`** — 22, 23, 24, 25, 27, 35, 36, 37
- **mask `0x10`** — 26, 28, 29, 30, 31, 32, 33, 34

Opcode 28 additionally reads a signed-byte operand. The actual arithmetic/comparison lives on the
fast path past the first branch, so **which operator each opcode is remains unresolved** — the
band is identified, the individual mnemonics are not.

### Object / property / method band — opcodes 38–43, 52–65, 77–89

These do not touch `PC`, `SP`, the locals or the registers directly; they work entirely through
4–7 helper calls each. Sizes and call counts mark the heavyweights:

- **38–43** — 4–6 calls; 38 pops from the stack. The largest handlers in the interpreter.
- **52–59, 64, 77, 78, 89** — take an `int16` operand (a constant-pool or property index).
- **56, 57, 87** — 6–7 calls, the most complex in the set.

The `int16` operand strongly suggests indexed property or method access against a constant pool.
Confirming that means following the helpers, which has not been done.

### Return / halt band — opcodes 48–51, 76, 81, 88

Small handlers clustered at the far end of the function (`0x6D2569`–`0x6D28BC`). **76** and **81**
are two instructions with no calls at all; **48**, **50**, **88** make a single call. Consistent
with `RET` / `HALT` / `THROW` variants.

### Opcode 75 — `NOP`

`0x6CF917` is the **fetch-loop head**, not a handler body: it checks two abort conditions, reads
the next opcode byte, increments `PC` and re-dispatches. Opcode 75's table entry points straight
at it, so executing 75 simply continues. This was a guess in the previous revision; it is now
confirmed by disassembly.

## Complete opcode map

`pops` counts only the pops visible before the handler's first branch, so binary operators show 1;
treat it as a lower bound.

```
op  handler    fields          operand  mask   pops calls   family
 1  0x6CF963   PC,LOC          sbyte           0    0       PUSH_LOCAL
 2  0x6CF978   GLB                             0    0       PUSH_GLOBAL
 3  0x6CF99D   PC,SP,LOC       sbyte           1    1       STORE_LOCAL
 4  0x6CF9CF   PC              sbyte           0    0
 5  0x6CF9DC                   int16           0    1
 6  0x6CF9E4                   int32           0    2
 7-10  0x6CFA32/3A/42/4A  R0-R3                0    0       PUSH_Rn
11-14  0x6CFA52/73/94/AB5 R0-R3                1    1       POP_Rn
15  0x6CFB3A   SELF                            0    0       PUSH_SELF
16-19  0x6CFAD6/AEF/B08/B21 R0-R3              0    1       CLEAR_Rn
20  0x6CFB3F   SP                              0    0       SWAP
21  0x6CFB56   SP                              0    0(push) DUP
22  0x6CFB71   SP                      0x30    1    3       operator
23  0x6CFE09   SP                      0x30    1    2       operator
24  0x6CFEC6   SP                      0x30    1    2       operator
25  0x6CFF7B   SP                      0x30    1    2       operator
26  0x6D008A   SP                      0x10    1    2       operator
27  0x6D0227   SP                      0x30    1    2       operator
28  0x6D028F   PC,SP           sbyte   0x10    1    2       operator
29  0x6D02E0   SP                      0x10    1    2       operator
30  0x6D0344   SP                      0x10    1    2       operator
31  0x6D03A5   SP                      0x10    1    2       operator
32  0x6D0431   SP                      0x10    1    2       operator
33  0x6D04A1   SP                      0x10    1    2       operator
34  0x6D0504   SP                      0x10    1    2       operator
35  0x6D05AE   SP                      0x30    1    2       operator
36  0x6D0693   SP                      0x30    1    2       operator
37  0x6D0778   SP                      0x30    1    2       operator
38  0x6D08AE   SP                              1    4       object/method
39  0x6D0A09                                   0    4       object/method
40  0x6D0BC1                                   0    4       object/method
41  0x6D0D79                                   0    6       object/method
42  0x6D0ECA                                   0    6       object/method
43  0x6D1020                                   0    4       object/method
44  0x6CF9F7   PC              sbyte           0    1
45  0x6CFA15                   int16           0    2
47  0x6D10D6   SP              int16           0(push) 1
48  0x6D257B                                   0    1       return/halt
49  0x6D258B                                   0    3       return/halt
50  0x6D2895                                   0    1       return/halt
51  0x6D289F                                   0    3       return/halt
52  0x6D1BEF                   int16           0    4       object/property
53  0x6D1C1C                   int16           0    4       object/property
54  0x6D1E10                   int16           0    5       object/property
55  0x6D1E42                   int16           0    5       object/property
56  0x6D1212                                   0    6       object/property
57  0x6D1381                                   0    7       object/property
58  0x6D1FF6                   int16           0    5       object/property
59  0x6D20F7                   int16           0    4       object/property
60  0x6D1566                                   0    3       object/property
61  0x6D1755                                   0    3       object/property
62  0x6D18F5                                   0    3       object/property
63  0x6D1594                                   0    3       object/property
64  0x6D1ACB                   int16           0    2       object/property
65  0x6D1B12                                   0    5       object/property
66  0x6D10F4   PC              sbyte           0    0       JMP
67  0x6D1103   PC              int16           0    1       JMP
68  0x6D1115   PC              int32           0    1       JMP
69  0x6D1121   PC,SP           sbyte           1    0       JMP_IF (null-check)
70  0x6D1166   PC,SP           int16   0x10    1    1       JMP_IF (null-check)
71  0x6D1194   PC              int32           0    1       JMP_IF (null-check)
72  0x6D11A0   PC,SP           sbyte   0x10    1    0       JMP_IF (value != 0)
73  0x6D11CF   PC              int16           0    1       JMP_IF (value != 0)
74  0x6D11DB   PC,SP           int32   0x10    1    1       JMP_IF (value != 0)
75  0x6CF917   PC                              0    0       NOP (fetch-loop head)
76  0x6D28BC                                   0    0       return/halt
77  0x6D1C42                   int16           0    4       object/property
78  0x6D1C68                   int16           0    4       object/property
79  0x6D1ADD                                   0    4       object/property
80  0x6D21AF                                   0    2       object/property
81  0x6D256F                                   0    0       return/halt
82  0x6D1C8E                                   0    2       object/property
83  0x6D1CA7                                   0    2       object/property
84  0x6D1CC8                                   0    2       object/property
85  0x6D1CE9                                   0    5       object/property
86  0x6D1E74                                   0    3       object/property
87  0x6D1E9A                                   0    6       object/property
88  0x6D2081                                   0    1       return/halt
89  0x6D208D                   int16           0    3       object/property
```

## What would finish this

Individual mnemonics for the operator band (22–37) and the object band require following each
handler's fast path past its first branch, and identifying the helper functions
`0x6D2E85` (49 call sites), `0x6CC6E6`, `0x6CBC6D`, `0x6D2DF2`, `0x6D2F2A`, `0x6D2FBC`,
`0x6D2FAD`. `0x6D2E85` being called from 49 of the 88 handlers makes it the single most
valuable target.

## Related classes

`MfScVirtualMachine` (`vtbl 0x757270`), `MfScVmEnv` (`0x757238`), `MfScVariableStack` (`0x757248`),
`MfScVmCallStackList` (`0x756FD0`), `MfScVmManager` (`0x757450`), `MfScVmException` (`0x757240`),
`MfScBytecodeRoutine` (`0x7574A8`), `MfScGcThread` (`0x7570B8`), `MfScGCWork` (`0x7570AC`).

## Next step

The `.pobj` container ([`../formats/behavior.md`](../formats/behavior.md)) holds compiled
routines. With the dispatch bounds known (opcodes 1–89, byte-sized operands for locals/globals),
a first-cut `.pobj` disassembler is now writable — but the shipped `Untitled_0.pobj` compiles to
nothing, so it cannot be tested without a `.pobj` from the CD data.
