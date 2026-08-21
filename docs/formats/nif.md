# `.nif` — NetImmerse 2.3 scene

Location: `data/Geometry/Utils/*.nif` (151 files), `data/Geometry/Enemies/`, `data/Geometry/prince/`.
Case varies (`.NIF` / `.nif`) — lookups must be case-insensitive.

**Status: externally documented.** Do not reverse this from scratch.

## Header

Every file begins with a NUL-terminated ASCII banner:

```
NetImmerse File Format, Version 2.3\n
Numerical Design Limited, Chapel Hill, NC 27514\n
Copyright (c) 1996-1999\n
All Rights Reserved\n
```

followed by a `uint32_t` version word and the object stream.

## Why this matters

NIF is the most thoroughly documented format in this game. The **NifTools** project
(`nifxml` / NifSkope) has a machine-readable schema covering version 2.3 (`0x02030000`), which is
one of the earliest shipping NetImmerse versions. Recommended approach:

1. Try NifSkope / `pyffi` / `nifly` on `data/Geometry/Utils/Upr_Beam.NIF` first.
2. Only fall back to reversing if POP3D turns out to use custom `NiExtraData` blocks — and it
   does: the RTTI list includes non-standard-looking classes worth checking against the schema
   (`NiAVInstance`, `NiGeometryInstance`, `NiMarkerNode`, `NiMarkerTriShape`, `NiCollPick`,
   `NiFastAlphaAccumulator`, `NiFlipTextures`, `NiUserAction`, `Ni3dsAlphaAnimator`,
   `Ni3dsPropertyAnimator`, `Ni3dsAnimationCore`).

The `Ni3ds*` family are the NetImmerse 3ds-max animation importers; `Ni3dsMorphShape`,
`Ni3dsEnvMorphShape`, `Ni3dsParticleSystem`, `Ni3dsPathController` and `Ni3dsAnimationNode` all
appear inside `k2NetImmerseList<T>` template instantiations, so the engine keeps typed lists of
each and ticks them per frame.

## Class hierarchy present in this build

```
NiObject
├── NiAVObject
│   ├── NiNode
│   │   ├── NiBillboardNode
│   │   ├── NiCollisionSwitch
│   │   └── NiMarkerNode
│   └── NiGeometry
│       └── NiTriBasedGeom
│           ├── NiTriShape
│           └── NiMarkerTriShape
├── NiProperty
│   ├── NiAlphaProperty        NiZBufferProperty      NiShadeProperty
│   ├── NiFogProperty          NiDitherProperty       NiWireframeProperty
│   ├── NiTextureModeProperty  NiVertexColorProperty  NiTransparentProperty
├── NiExtraData
├── NiAction ──> NiUserAction
├── NiRawImageData
└── NiScreenPolygon
```

Support types: `NiBoundingVolume` / `NiSphereBV`, `NiCollisionGroup`, `NiCollPick`,
`NiAccumulator` / `NiFastAlphaAccumulator`, `Ni3DDeviceDesc` / `NiD3DDeviceDesc`,
`NiTArray<T>`, `NiTMap<K,V>`, `NiTStringMap<V>`, `NiPointer<T>` (intrusive refcount),
`NiStream` (the serializer — its `Load`/`Save` pair is the entry point if you do need to reverse).

## The `.sef` companion

`data/Geometry/Utils/SpecialEffects.sef` is a tab-separated name → path table that maps effect names to
`.nif` files:

```
ArrowNormal<TAB><TAB>...geometry\utils\ArrowNormal.nif
ArrowNormalImpact<TAB>...geometry\utils\ArrowNormalImpact.nif
ArrowFire<TAB>...geometry\utils\ArrowFire.nif
```

Blank lines separate groups. Parsed by `k2SpecialEffectFactory` / `k2FileEffectController`.
See [behavior.md](behavior.md).

## Where NIF meets K2

`data/Geometry/Utils/` is effects and props (`CSFX_AirBubbles.nif`, `ArrowSwarmResidual.NIF`,
`Upr_Beam.NIF`). Characters and rooms come in as `.K2A` instead. So the split is roughly:

- **`.nif`** — authored in the NetImmerse toolchain, particle/effect content, small props
- **`.K2A`** — authored in 3DS Max via the K2 exporter, characters and level rooms

Both end up as `NiNode` trees at runtime.
