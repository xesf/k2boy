# .nif — NetImmerse 2.3 effects

The visual effects (potion bursts and auras, beams, splashes, showers) are
NetImmerse 2.3 scene files. The engine links NetImmerse whole, so the layout
here is read off its own loaders; 107 of the 111 shipped files read through to
"End Of File" (`k2boy --nifcheck <install>`).

## File

Four text lines (`NetImmerse File Format, Version 2.3`, the NDL credit,
copyright, rights), then blocks until `End Of File`:

```
u32 len, char type[len]          block type name
object                           per the type below
```

`Top Level Object` (as a type name) marks the next block as a root. Blocks
refer to each other by the pointer value each was saved under (its first
field), not by index.

## Common parts

| Part | Loader | Fields |
|---|---|---|
| NiObject | 0x552BF0 | u32 id, string name, u32 n, n × (type string, inline extra data) |
| NiStringExtraData (inline) | 0x580F00 | u32 size, string ("loop") |
| Ni3dsPropAnimExtraData (inline) | 0x5FC070 | u32 size, u32 |
| NiAVObject | 0x551EC0 | NiObject, u8 flags, vec3 translate, mat3 rotate (row-major), f scale, vec3 velocity, u32 n + n property ids, u32, u32 has-bound |
| NiNode | 0x54F3B0 | NiAVObject, u32, u32, u8, u32 n + child ids, u32 n + effect ids |
| NiProperty | 0x554C50 | NiObject, u8 flags |
| 3ds anim header | 0x5FB420 | u32, 4 × u8, 6 × f (last: length in seconds) |

`k2PostSceneNode`, `k2AnimationPropertyHub`, `k2AnimatedTextureHub` and
`NiMarkerNode` are plain NiNodes on disk.

## Geometry

NiGeometry (0x55A690): NiAVObject, u16 nverts, u32 has + verts, u32 has +
normals, bound (vec3, f). NiTriBasedGeom (0x55C820): u16 ntris, u16 nuvsets,
u32 has + uvw × nverts × nuvsets (three floats each), u32 has + rgba ×
nverts, u32 has + 16 bytes × ntris. NiTriShape (0x5788E0) then 3 × u16 per
triangle. NiParticles (0x56FD10): f radius, u16 active, u32 has + f size per
vertex; NiAutoNormalParticles (0x570440): u32 has + vec3 per vertex.

## Properties and images

| Type | Fields after NiProperty |
|---|---|
| NiAlphaProperty 0x5682A0 | u8 enable, u32 src, u32 dst (6/7 = alpha, dst 0 = additive) |
| NiMaterialProperty 0x55D250 | 4 × rgb (ambient, diffuse, specular, emissive), f gloss, f alpha |
| NiTextureProperty 0x5615B0 | u32, u32 n + n image ids |
| NiTextureModeProperty 0x568F00 | 3 × u32 |
| NiVertexColorProperty 0x56AB30 | u32 |
| NiSpecularProperty 0x580A10 | u8 |
| NiZBufferProperty 0x584AB0 | u8, u8 |

NiImage (0x560010): NiObject, u8 external, then a file name or the id of a
NiRawImageData, u32. NiRawImageData (0x55DA40, sizes from 0x55DAD0):
NiObject, u32 width, u32 height, u32 format — 0: 256 × RGBA palette and
8-bit indices, 1: RGB, 2: RGBA.

## Animation

Ni3dsAnimationNode (0x5E14F0): NiNode, anim header, then four tracks, each
`i32 n` and, if n > 0, `u32 type` and n keys:

* rotation (readers at 0x8B2D68): type 1 = 44 bytes (time, u32, axis, quat
  w x y z, 2 f), 2 = 64, 3 (TCB) = 96, 4 (Euler) = 44 + u16 + three float
  tracks;
* position (0x8B280C): 1 = time + vec3 (16), 2 (Bézier) = 64, 3 (TCB) = 76;
* scale, float keys (0x8B2A1C): 1 = time + value (8), 2 = 16, 3 = 28;
* n × (u32, u8) visibility.

Ni3dsParticleSystem (0x5E8C50), a 3ds Max SuperSpray: NiNode, anim header,
u32 max particles, 2 × u8, 13 × f, vec3, 6 × f, vec3, 7 × f, u32 has + (u32
m, m × u32), u32 has + 56 bytes per particle of saved state, u32, two
counted u32 lists, a position track, 2 × u32. In the 13 floats: [0] size,
[1] speed, [3] life, [5] birth rate (our reading).

Also read: Ni3dsAlphaAnimator, Ni3dsColorAnimator, Ni3dsPathController,
NiGravity. Not yet: NiFlipTextures, SimpleTrap (the game's own trap node).

## Playing them

The game places effects with **SpecialEffect** traps (argv1 the .nif, shown
while Visible and "on"), potions (`Potion<Name>.NIF` on drinking,
`Potion<Name>Residual.NIF` while it lasts, names from 0x4EB3D4) and
`EffectAt` messages. Ours plays shapes with their node animation and turns
each particle system into camera-facing sprites; emission spread, speeds and
sprite fading are approximations.
