# NetImmerse RTTI (`NiRTTI`) — recovered class hierarchy

**169 classes, exact parent links, 100% of them named in the IDA database.**

NetImmerse does not rely on MSVC RTTI for its own dispatch. It carries a
hand-rolled type descriptor, `NiRTTI`, and every `NiObject` subclass exposes it
through virtual slot 0. Recovering it gives the complete inheritance graph of
both the engine and the K2 game layer — far more than the MSVC `RTTICompleteObjectLocator`
walk did, because it yields **parent links**, not just names.

## The descriptor

`NiRTTI` is 8 bytes, built by a single constructor at `0x54CC00`
(`NiRTTI__ctor` in the database):

```c
struct NiRTTI {
    char*         name;      // +0x00  strdup'd from the literal
    const NiRTTI* parent;    // +0x04  null for a root
};
```

```
54cc02  mov     esi, [esp+8+arg_0]      ; arg_0 = the name literal
54cc10  repne scasb                     ; strlen
54cc15  call    operator new(uint)
54cc26  mov     [ebx], edx              ; this->name = copy
54cc37  rep movsd                       ; copy it in
54cc42  mov     ecx, [esp+0Ch+arg_4]
54cc47  mov     [ebx+4], ecx            ; this->parent = arg_4
54cc4c  retn    8
```

Each class gets one static instance, constructed by an MSVC dynamic initializer
thunk. `CameraStateTable`'s is the canonical shape:

```
406746  push    offset NiRTTI_CharacterStateTable   ; parent
40674b  push    offset aCamerastatetab              ; "CameraStateTable"
406750  mov     ecx, offset NiRTTI_CameraStateTable ; this
406755  call    NiRTTI__ctor
```

and slot 0 of the vtable is a two-instruction getter:

```
40bb8c  mov     eax, offset NiRTTI_CameraStateTable
40bb91  retn
```

## How it was recovered

Scan the raw PE for `mov ecx, imm32 / call 0x54CC00`, then walk backwards over
the two `push imm32` operands. Name, parent and descriptor address all fall out
of the same 20 bytes. Separately, scan for `B8 <desc> C3` to find the `GetRTTI`
getters.

The two scans agree exactly: **169 descriptors, 169 getters, 1:1, no class with
zero and none with two.** That mutual check is the reason this is reported as
established rather than probable.

## Correcting an earlier reading

An earlier pass treated `CharacterStateTable` (vtable `0x74395C`, 51 slots) as
"the gameplay state machine" and read its 51 slots as states. That was wrong on
both counts:

* The 51 slots are the **`NiObject`/`NiAVObject`/`NiNode` virtual interface**,
  inherited, not states. `PrinceObject` and `CameraStateTable` override only
  **7 of the 51** — and the *same* 7: slots 0, 6, 45, 46, 47, 48, 50. Slots
  45-48 and 50 are pure virtual in the base (`0x52DFBA`); slot 0 is `GetRTTI`.
  Slot 47 is an empty body in both subclasses, COMDAT-folded onto a shared `retn`.
* The block slot 0 returns is not a table of states. It is the 8-byte `NiRTTI`
  descriptor above. The `call X; mov [abs], eax` runs nearby that first looked
  like an array being filled are **separate 11-byte initializer thunks for
  unrelated globals** that the linker happened to place adjacently.

`CharacterStateTable` is a real and central class, just not a state machine: it
is the common base of the player, the camera and every trap.

## Spine of the hierarchy

```
NiObject
  NiAVObject
    NiNode
      NiCollisionSwitch
        CharacterObject
          CharacterStateTable
            PrinceObject          <- the player
            CameraStateTable      <- the camera
            Trap                  <- 39 subclasses; see trap-factory.md
```

Depth distribution (0 = root): 2, 21, 45, 30, 17, 9, 6, 39. The 39 at depth 7
are exactly the `Trap` subclasses.

## Cross-validation against the trap factory

Every one of the 33 classes named in `docs/reference/trap-factory.md` is a
direct `Trap` subclass under `NiRTTI` — 33/33, no mismatches. This independently
confirms the factory table, which was built from the registration calls rather
than from RTTI.

Six `Trap` subclasses have **no** factory builder: `CameraMessage`,
`DelayMessage`, `EnemyMessage`, `GameMessage`, `PrinceMessage`, `VideoMessage`.
`AudioMessage` does have one, under the key `Audio`. Whether the other six are
constructed only by the messaging system at runtime is **not yet established** —
it is the obvious reading, but it has not been checked against the code.

## Composition of the 169

| Group | Count |
|---|---|
| NetImmerse core (`Ni*`) | 78 |
| K2 game layer | 91 |
| — of which `Trap` subclasses | 39 |

## Full tree

```
NiExtraData                                    0x8A0C00
  Ni3dsPropAnimExtraData                       0x8B2D90
  NiCloneExtraData                             0x8A1E10
  NiStringExtraData                            0x8A1A28
NiObject                                       0x8A0B90
  ImageRef                                     0x89F6E8
  K2ImportSet                                  0x89F218
  Ni3dsLightAnimator                           0x8B2CF8
    Ni3dsLightColorAnimator                    0x8B2D40
  Ni3dsPropertyAnimator                        0x8B2890
    Ni3dsAlphaAnimator                         0x8B2CA8
    Ni3dsColorAnimator                         0x8B2CD0
  NiA3dSoundSystem                             0x8B8C88
  NiAVObject                                   0x8A0B60
    ForceCollider                              0x803708
    K2DummyObject                              0x7EAA88
    NiA3dListener                              0x8B8CD0
    NiA3dSource                                0x8B8C40
    NiCamera                                   0x8A0FC8
    NiGeometry                                 0x8A0DE0
      NiLines                                  0x8A18B8
      NiTriBasedGeom                           0x8A0E20
        NiParticles                            0x8A1440
          NiAutoNormalParticles                0x8A14B0
        NiTriShape                             0x8A16E8
          Ni3dsEnvMorphShape                   0x8B2958
          Ni3dsMorphShape                      0x8B2910
          Ni3dsSkin                            0x8B2C20
          NiEnvMappedTriShape                  0x8A1748
          NiMarkerTriShape                     0x7E9C00
        NiTriStrip                             0x8A1B28
        NiTriangles                            0x8A1AC8
    NiLight                                    0x8A0D30
      NiProjectedLight                         0x8A19A8
      NiShadowLight                            0x8A1608
    NiNode                                     0x8A0AF0
      AnimationNode                            0x7E9270
      BlurController                           0x7FFF20
      Force                                    0x803720
        LinearForce                            0x89F888
        MotivateForce                          0x89F860
        RadialForce                            0x89F740
        VolumetricForce                        0x89F878
      Gauge                                    0x7E9C78
      K2ArticulatedObject                      0x801BD8
      LevelGroup                               0x7F3CB0
      Ni3dsAnimationNode                       0x8B2738
        Ni3dsBone                              0x8B2BC8
      Ni3dsParticleSystem                      0x8B2878
      Ni3dsPathController                      0x8B27B0
      NiBSPNode                                0x8A15C0
      NiBillboardNode                          0x8A1530
      NiCollisionSwitch                        0x8A0D68
        CharacterObject                        0x7E95E0
          CharacterStateTable                  0x7F3F48
            CameraStateTable                   0x7E93F0
            PrinceObject                       0x7FFB90
            Trap                               0x7F6378
              Abyss                            0x7FB198
              Action                           0x7FB2A0
              Aperture                         0x7FB458
              Area                             0x7FB4B8
              ArrowCharger                     0x7FB5D0
              ArrowTarget                      0x7FB6E0
              AudioMessage                     0x7FB9B0
              Bar                              0x7FBA70
              CameraMessage                    0x7FBB28
              CollapsingFloor                  0x7FBDA0
              Crate                            0x7FBE90
              CrateTarget                      0x7FBFB0
              DelayMessage                     0x7FC180
              Door                             0x7FC1D0
              EnemyMessage                     0x7FC410
              EnemyStart                       0x7FC4E8
              Fixture                          0x7FC5C8
              FlameThrower                     0x7FC6D8
              ForceTrap                        0x7FC858
              Fountain                         0x7FC950
              GameMessage                      0x7FCBB0
              Generic                          0x7FCC98
              Launcher                         0x7FCD88
              LightSwitch                      0x7FCFE0
              Messenger                        0x7FD030
              Mover                            0x7FD0F0
              Pickup                           0x7FD538
              Pole                             0x7FD6D0
              PressurePlate                    0x7FD758
              PrinceMessage                    0x7FD7F8
              PrinceStart                      0x7FDAF0
              RoomAttributes                   0x7FDB90
              Rope                             0x7FDC80
              SpecialEffect                    0x7FDDB8
              Specials                         0x7FDF28
              Spikes                           0x7FDFA0
              VideoMessage                     0x7FE068
              WallBlade                        0x7FE210
              Water                            0x7FE370
      NiMarkerNode                             0x7F2EB0
        k2AnimatedTextureHub                   0x800BA8
        k2AnimationPropertyHub                 0x800BE8
      NiSwitchNode                             0x8A17D8
        NiFltAnimationNode                     0x8A1888
        NiLODNode                              0x8A1958
      RoomGroup                                0x7F3E20
      SimpleTrap                               0x89F7D8
      SingleSpurt                              0x7F3EA8
      SingleSpurtPuddle                        0x7F3E88
      k2FileEffectController                   0x800738
        k2SpecialEffectController              0x800AB0
          k2MoveableController                 0x800810
            k2ArrowTrail                       0x800C58
      k2PostSceneNode                          0x8012F0
      k2SceneGraph                             0x7F39B0
      k2WeatherNode                            0x801618
    k2LinkerAVObject                           0x89F438
  NiAction                                     0x8A0DA8
    NiCameraAction                             0x8A10C0
    NiFlipTextures                             0x8A1798
    NiUserAction                               0x8A1080
    k2AnimationAction                          0x800290
      k2AnimationCoreAction                    0x8002B0
        k2AnimationUpdateAction                0x8002E8
          k2ParticleSystemAction               0x800368
            k2PSChadAction                     0x8003F0
            k2PSExtraStepAction                0x8004B0
      k2NetImmerseAction                       0x800320
        k2TextureAction                        0x800598
    k2SpecialEffectManager                     0x800B48
  NiDisplay                                    0x8A1380
  NiForce                                      0x8B2AC8
    NiGravity                                  0x8B2AF0
    NiMeshCollider                             0x8B2B78
    NiParticleBomb                             0x8B2B20
    NiPlanarCollider                           0x8B2BA8
    NiSphericalCollider                        0x8B2B38
  NiImage                                      0x8A0F28
    NiDDImage                                  0x8B2140
      NiDDBufferImage                          0x8B2158
  NiPalette                                    0x8B21E8
  NiProperty                                   0x8A0C20
    NiAlphaProperty                            0x8A1118
    NiCorrectionProperty                       0x8A1630
    NiDitherProperty                           0x8A1670
    NiFogProperty                              0x8A1130
    NiMaterialProperty                         0x8A0E40
    NiMultiTextureProperty                     0x8A11B0
    NiShadeProperty                            0x8A19D8
    NiSpecularProperty                         0x8A1A00
    NiTextureModeProperty                      0x8A1178
    NiTextureProperty                          0x8A0F88
    NiTransparentProperty                      0x8A1A40
    NiVertexColorProperty                      0x8A11D0
    NiWireframeProperty                        0x8A1B60
    NiZBufferProperty                          0x8A1BA0
  NiRawImageData                               0x8A0EA0
  NiRender                                     0x8A1248
    NiD3DRender                                0x8B2190
  NiScreenPolygon                              0x8A1050
    Indicator                                  0x803988
  NiUser                                       0x8A13E0
  PropertyBase                                 0x7EAA18
    PropArtic                                  0x7EA098
    PropForce                                  0x7EA108
    PropLOD                                    0x7EA118
    PropPortal                                 0x7EA190
    PropTrap                                   0x7EA1E8
    SpecialEffectProp                          0x7EA158
      BillboardProp                            0x7EA0D8
  WeaponParameters                             0x7FFE30```

---

# The `NiObject` root interface

`NiObject`'s vtable is at `0x74E8C4` and is **exactly 7 slots** — index 7 holds a
float constant, so the boundary is unambiguous. Those 7 virtuals propagate
through all 169 classes, which is why the same slot indices kept showing up as
"always overridden" in earlier diffs.

| slot | method | base | evidence |
|---:|---|---|---|
| 0 | `GetRTTI` | `0x552970` | `mov eax, <descriptor>; retn` |
| 1 | `LoadBinary` | `0x552BF0` | reads a 4-byte link id, registers `this` in the stream's pointer map, loads the name into `this+8` |
| 2 | `LinkObject` | `0x552D50` | walks the `this+0x0C` list calling **slot 2** on each node |
| 3 | `RegisterStreamables` | `0x552D80` | same walk calling **slot 3** |
| 4 | `SaveBinary` | `0x552E50` | calls `GetRTTI`, writes `->name`, then writes 4 bytes |
| 5 | `IsEqual` | `0x552EF0` | null operand → `false`; otherwise compares both `GetRTTI` results |
| 6 | *scalar deleting dtor* | `0x552980` | `retn 4`; tests `al & 1`, calls `operator delete(this)`, returns `this` |

Slots 1-5 are the NIF serialization protocol. Slot 6 is the MSVC scalar deleting
destructor, which is why **every** class overrides it — nothing semantic is
implied by that.

## Object layout, so far

```c
struct NiObject {
    void*  vtable;      // +0x00
    // +0x04 unknown
    char*  name;        // +0x08  allocated in LoadBinary, freed in the dtor
    void*  list_head;   // +0x0C  next at +0x10 on each node; +0x14 written during load
};
```

`NiObject::~NiObject` also decrements `g_NiObject_live_count` (`0x8A0BAC`), a
global instance counter.

## Who overrides what

503 overrides were named mechanically: for each class, any slot 1-6 entry that
differs from its parent's is that class's override. No two classes shared a body,
so there were no ambiguous cases.

| method | classes overriding |
|---|---:|
| scalar deleting dtor | 148 |
| `LoadBinary` | 71 |
| `LinkObject` | 71 |
| `RegisterStreamables` | 71 |
| `SaveBinary` | 71 |
| `IsEqual` | 71 |

The five streaming methods are overridden by **exactly the same 71 classes** —
they are always implemented as a set, never piecemeal. Those 71 are the classes
that can appear in a `.nif` file.

## The K2 scene classes barely extend NiNode

Diffing the game's `NiNode` subclasses over the 45 slots they share:

| class | overrides |
|---|---:|
| `NiCollisionSwitch` | 10 / 45 |
| `K2ArticulatedObject` | 7 / 45 |
| `k2SceneGraph` | 6 / 45 |
| `LevelGroup` | 2 / 45 |
| `RoomGroup` | 2 / 45 |

`LevelGroup` and `RoomGroup` override only `GetRTTI` and the destructor — they
are plain grouping nodes with no behaviour of their own. That is good news for
the reimplementation: the level and room hierarchy needs no special node type
beyond a transform and a child list.

## Vtable recovery method

Slot 0 is always `GetRTTI`, and each `GetRTTI` body is a unique
`mov eax, <descriptor>; retn`. Scanning the data sections for each getter's
address therefore locates that class's vtable, **including for the NetImmerse
classes that carry no MSVC RTTI**. 160 of 169 resolved to exactly one address;
9 were ambiguous and were left alone.

Cross-check: `NiCollisionSwitch` resolves to `0x743BB8` by this method, which is
the same address the independent MSVC-RTTI walk produced.

## Not established

* **Vtable lengths.** These classes have no `vtable[-1]` locator, so a vtable's
  end can only be bounded by where the next one starts — accurate to about one
  slot. The K2 scene classes measure 45 slots while `NiNode` measures 46-47; that
  is inconsistent with inheritance and is unresolved. The slot *diffs* above do
  not depend on it.
* **`NiNode`'s own vtable** has two candidates, `0x74E758` and `0x745518`. The
  former is used here because the latter is only 45 slots. Which class owns
  `0x745518` is unknown.
* **`NiStream`**, the container the streaming quintet reads from and writes to,
  has not been examined. That, not the protocol, is what `.nif` loading needs.
