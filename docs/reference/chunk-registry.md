# Motivate / K2 chunk-type registry

**Status: confirmed.** Recovered from `data/pop3dreal.exe` by pattern-matching the static
self-registration sequence, then resolving each creator's constructor to a class via RTTI.

## Mechanism

Every serializable class registers a 4CC tag and a creator function from its own static
initializer:

```asm
push    offset <creator>      ; operator new(sizeof) + ctor
push    <4CC tag>             ; e.g. 79654B4Ah = 'JKey'
mov     ecx, offset <static>  ; the class's own registrar object
call    sub_60506F            ; Register(tag, creator)
```

`sub_60506F` is the common `Register` entry point. Each class has its **own** registrar object
(`ecx` differs per site) — there is no single shared table, same design as the trap factory in
[`trap-factory.md`](trap-factory.md). Pointers to the registration thunks live in the CRT
static-initializer array in `.data` (e.g. `0x7B8DD0` → the `JKey` thunk at `0x611A2D`).

## Reading the tags

The **Tag** column is the file byte order (`struct.pack('<I', tag)`); **Reversed** is the same
bytes flipped, which is how most of them read as English. Some are natural in file order
(`JKey`, `SKey`, `Skil`), most are not (`olFV` = `VFlo`, `AcoL` = `LocA` = LocoActivity). This is
not an extraction artefact — `olFV` and `rtSV` appear literally, in that order, inside shipped
`.act` files. **Match on the Tag column.**

`sizeof` is the argument to `operator new` inside each creator, i.e. the in-memory object size.
For `JKey` it equals the serialized record size exactly (see
[`../formats/skl.md`](../formats/skl.md)); that is **not** guaranteed for other types.

Class attribution follows the creator's ctor call and reads the vtable that ctor installs.
62 of 78 resolve; where the result is a base class (`MfStreamObject`, `MfDblList`,
`MfTypedValue`) the ctor chain installs the base vtable first and the leaf was not resolved —
those rows are marked.

## Keyframe / skill types (`.skl`, `.act`)

| Tag (file bytes) | Reversed | Class | sizeof | Creator |
|---|---|---|---:|---|
| `ActP` | `PtcA` | `MfStreamObject` *(base class — leaf not resolved)* | 252 | `0x600a0b` |
| `JKey` | `yeKJ` | `MfJointKeyframe` | 212 | `0x6119f8` |
| `LKxT` | `TxKL` | — | 32 | `0x63bd8d` |
| `LSkl` | `lkSL` | `MfDblList` *(base class — leaf not resolved)* | 188 | `0x63d350` |
| `LyeK` | `KeyL` | — | 20 | `0x61192b` |
| `MSkl` | `lkSM` | `MfDblList` *(base class — leaf not resolved)* | 236 | `0x63f48d` |
| `PlkS` | `SklP` | `MfStreamObject` *(base class — leaf not resolved)* | 96 | `0x60c1a0` |
| `SKey` | `yeKS` | `MfSoundKeyframe` | 44 | `0x64053d` |
| `Skil` | `likS` | `MfDblList` *(base class — leaf not resolved)* | 136 | `0x623c96` |
| `TKey` | `yeKT` | `MfTextureKeyframe` | 228 | `0x6400e4` |
| `fyeK` | `Keyf` | — | 12 | `0x6124ae` |
| `taPJ` | `JPat` | — | 28 | `0x640e13` |
| `vKey` | `yeKv` | `MfEventKeyframe` | 20 | `0x64079c` |

Note `vKey` is **`MfEventKeyframe`**, not a visibility or value key as previously assumed here.

## Motivate activity types

| Tag (file bytes) | Reversed | Class | sizeof | Creator |
|---|---|---|---:|---|
| `Apd3` | `3dpA` | `Mf3dPathActivity` | 236 | `0x6870b0` |
| `AotA` | `AtoA` | `MfAtomicActivity` | 168 | `0x68ef41` |
| `AulP` | `PluA` | `MfAtomicActivity` | 172 | `0x6886d3` |
| `AttA` | `AttA` | `MfAttachActivity` | 228 | `0x6895da` |
| `AtiB` | `BitA` | `MfBitArray` | 24 | `0x696f50` |
| `ApmC` | `CmpA` | `MfCompliantActivity` | 1084 | `0x675f84` |
| `AseG` | `GesA` | `MfGestureActivity` | 220 | `0x66d078` |
| `AloH` | `HolA` | `MfHoldActivity` | 1096 | `0x6767ec` |
| `AcoL` | `LocA` | `MfLocoActivity` | 256 | `0x674252` |
| `ApnM` | `MnpA` | `MfManipActivity` | 240 | `0x675058` |
| `AirO` | `OriA` | `MfOrientActivity` | 136 | `0x6758ce` |
| `AlvO` | `OvlA` | `MfOverlayActivity` | 112 | `0x66b980` |
| `AdnR` | `RndA` | `MfRandomActivity` | 120 | `0x681eda` |
| `AqeS` | `SeqA` | `MfSequentialActivity` | 124 | `0x682182` |

`MfHoldActivity` (1096 bytes) and `MfCompliantActivity` (1084) are the two largest objects in the
whole registry — the ledge-hang and compliant-contact logic.

## K2 (Red Orb) activity types

These carry a `K2xx` prefix (reversed) and their creators sit in the game-code address range
`0x439406`–`0x4A3292`, separate from the Motivate range. All 16 resolve to a `K2*Activity`
class whose name matches the reversed tag exactly, so this group is high-confidence.

**Correction:** an earlier revision of this document claimed these were the chunk types of the
missing `.lvl` container. That was wrong. `.lvl` is a **text** format — see
[`../formats/lvl.md`](../formats/lvl.md). These tags are the K2-authored Motivate activities
(run, swim, wade, slide, ledge-attach, …), i.e. the player and enemy movement repertoire.

| Tag (file bytes) | Reversed | Class | sizeof | Creator |
|---|---|---|---:|---|
| `SD2K` | `K2DS` | `DumpSkillActivity` | 44 | `0x439406` |
| `FF2K` | `K2FF` | `FallToFloorActivity` | 36 | `0x495114` |
| `D32K` | `K23D` | `K2Activity3D` | 236 | `0x48e20a` |
| ` R2K` | `K2R ` | `K2ActivityRun` | 140 | `0x48f6c7` |
| `SR2K` | `K2RS` | `K2ActivityRunStop` | 152 | `0x491f4a` |
| `tA2K` | `K2At` | `K2AttachActivity` | 136 | `0x493927` |
| `EL2K` | `K2LE` | `K2EnemyLoco` | 108 | `0x494544` |
| `XF2K` | `K2FX` | `K2FixtureActivity` | 140 | `0x4956cc` |
| `AG2K` | `K2GA` | `K2GestActivity` | 216 | `0x49622e` |
| `8L2K` | `K2L8` | `K2Loco8Activity` | 172 | `0x49a3f7` |
| `1L2K` | `K2L1` | `K2LocoActivity` | 148 | `0x49bc7e` |
| `TM2K` | `K2MT` | `K2MoveToActivity` | 160 | `0x49d784` |
| `LS2K` | `K2SL` | `K2SlideActivity` | 148 | `0x49fa8b` |
| `WS2K` | `K2SW` | `K2SwimActivity` | 112 | `0x4a0e6b` |
| `EG2K` | `K2GE` | `K2TurnGesture` | 120 | `0x4a2435` |
| `DW2K` | `K2WD` | `K2WadeActivity` | 108 | `0x4a3214` |

## Value, property and remaining types

| Tag (file bytes) | Reversed | Class | sizeof | Creator |
|---|---|---|---:|---|
| `rloC` | `Colr` | `MfColor` | 28 | `0x604906` |
| `ArgM` | `MgrA` | `MfDblList` *(base class — leaf not resolved)* | 108 | `0x669454` |
| `siLV` | `VLis` | `MfDblList` *(base class — leaf not resolved)* | 32 | `0x6021a6` |
| `baTP` | `PTab` | `MfDictionary` | 24 | `0x60af26` |
| `JLHn` | `nHLJ` | `MfJLHinge` | 48 | `0x709ec7` |
| `lXLJ` | `JLXl` | `MfJLLinear` | 44 | `0x70abaa` |
| `bXLJ` | `JLXb` | `MfJLimit` | 28 | `0x70afba` |
| `dmrP` | `Prmd` | `MfProtoManipData` | 60 | `0x60b8ba` |
| `2MoC` | `CoM2` | `MfStreamObject` *(base class — leaf not resolved)* | 88 | `0x60fe16` |
| `DrPP` | `PPrD` | `MfStreamObject` *(base class — leaf not resolved)* | 28 | `0x5fdc2c` |
| `KErP` | `PrEK` | `MfStreamObject` *(base class — leaf not resolved)* | 16 | `0x60b633` |
| `KSrP` | `PrSK` | `MfStreamObject` *(base class — leaf not resolved)* | 28 | `0x60b524` |
| `KTrP` | `PrTK` | `MfStreamObject` *(base class — leaf not resolved)* | 220 | `0x60b433` |
| `itcA` | `Acti` | `MfStreamObject` *(base class — leaf not resolved)* | 92 | `0x668875` |
| `oNrP` | `PrNo` | `MfStreamObject` *(base class — leaf not resolved)* | 128 | `0x603dd7` |
| `pamb` | `bmap` | `MfStreamObject` *(base class — leaf not resolved)* | 28 | `0x60b2dd` |
| `2rTV` | `VTr2` | `MfTransform2Value` | 24 | `0x601d2d` |
| `3rTV` | `VTr3` | `MfTransform3Value` | 40 | `0x601e7d` |
| `2SrT` | `TrS2` | `MfTriangleSet` | 64 | `0x6089b5` |
| `2maC` | `Cam2` | `MfTypedValue` *(base class — leaf not resolved)* | 52 | `0x60dc00` |
| `2taM` | `Mat2` | `MfTypedValue` *(base class — leaf not resolved)* | 204 | `0x604e95` |
| `2tiL` | `Lit2` | `MfTypedValue` *(base class — leaf not resolved)* | 140 | `0x60df76` |
| `rtSV` | `VStr` | `MfTypedValue` *(base class — leaf not resolved)* | 16 | `0x601bda` |
| `3eVV` | `VVe3` | — | 24 | `0x601ca8` |
| `DArP` | `PrAD` | — | 8 | `0x60b807` |
| `DFrP` | `PrFD` | — | 16 | `0x60b6f0` |
| `DHrP` | `PrHD` | — | 8 | `0x60b794` |
| `JLEu` | `uELJ` | — | 28 | `0x70a419` |
| `JLim` | `miLJ` | — | 4 | `0x709e40` |
| `ilpS` | `Spli` | — | 28 | `0x60ecb2` |
| `olFV` | `VFlo` | — | 16 | `0x601a47` |
| `porP` | `Prop` | — | 12 | `0x6029d1` |
| `rrAV` | `VArr` | — | 20 | `0x6024ba` |
| `tnIV` | `VInt` | — | 16 | `0x6018ac` |
| `tsLP` | `PLst` | — | 20 | `0x60aafe` |

## How to extend this

```python
pat = re.compile(rb'\x68(....)\x68(....)\xb9(....)\xe8(....)', re.S)
# push creator ; push tag ; mov ecx, registrar ; call Register
```

filtered to hits where the tag's four bytes are printable ASCII and the creator lands in `.text`.
`sizeof` comes from the first `push imm / call operator new` inside each creator; the class comes
from the vtable installed by the ctor that the creator calls.

