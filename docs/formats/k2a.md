# `.K2A` — K2 ASCII geometry export

Location: `data/Actors/<Name>/<Name>.K2A`, `data/Geometry/prince/*.K2A`, and (per linkmaps)
`geometry\rooms\<level>\*.k2a`. 169 files present; the room ones shipped only on CD.

Case varies on disk (`.K2A`, `.k2a`, `.K2a`) — treat lookups as case-insensitive.

**Status: confirmed.** Plain ASCII, fully readable, no reversing required.

## What it is

A 3D Studio MAX **ASE** (ASCII Scene Export) file with a K2-specific header and a few extra
tokens. The exporter identifies itself:

```
*K2_ASCIIEXPORT	105
*K2_FILETYPE	1
*COMMENT "K2 Export Version  1.05 - Fri Aug 06 11:28:43 1999"
*SCENE {
	*SCENE_FILENAME "..."
	...
}
```

Line endings are CRLF, indentation is tabs, blocks are `{ }`. Values are whitespace-separated;
strings are double-quoted.

## Keyword table — recovered from the binary

The engine's parser holds a **77-entry keyword table at `0x7BCAF0`** (named `g_k2a_keyword_table`
in the database). Each entry is `{ const char* name; int32_t length; }` — length-prefixed for fast
comparison. That table *is* the authoritative grammar; anything not in it is ignored by the
importer even if 3DS Max wrote it.

Groups, in table order:

| Group | Keywords |
|---|---|
| Node | `SHAPE` `HELPER` `NODE_NAME` `NODE_PARENT` `ROOT` `NODE_TM` `TM_ROWO` `TM_ROW1` `TM_ROW2` `TM_POS` `TM_ROTAXIS` `TM_ROTANGLE` |
| Mesh | `MESH` `MESH_NUMVERTEX` `MESH_NUMFACES` `MESH_VERTEX_LIST` `MESH_VERTEX` `MESH_FACE_LIST` `MESH_FACE` `A:` `B:` `C:` |
| UV / colour | `MESH_NUMTVERTEX` `MESH_TVERT` `MESH_TFACELIST` `MESH_TFACE` `MESH_NUMCVERTEX` `MESH_VERTCOL` |
| Format select | `GEOM_FORMAT` `MESH_FORMAT` `TRIMESH_FORMAT` |
| Stripped variant | `TRI_NUMVERTEX` `TRI_VERTLIST` `TRI_NUMCVERTEX` `TRI_CVERTLIST` `TRI_NUMTVERTEX` `TRI_TVERTLIST` `TRI_NUMSTRIPS` `TRI_NUMINDICIES` `TRI_INDEX` |
| K2 extensions | `NODE_HAS_K2_DATA` `K2_USER_DATA_BLOCKS` |
| **Traps** | `K2_TRAP_BLOCK` `K2_TRAP_CENTER` `K2_TRAP_BBOX` `K2_TRAP_PIVOT` `K2_TRAP_PIVOT_AXIS` `K2_TRAP_PIVOT_ANGLE` `K2_TRAP_NAME` `K2_TRAP_ID` `K2_TRAP_ARGC` `K2_TRAP_ARGV` `K2_TRAP_HAS_COMMENT` `K2_TRAP_COMMENT` |
| Articulation / LOD | `K2_ARTICULATED_BLOCK` `K2_ARTICULATED_PIVOT` `K2_LOD_BLOCK` `K2_LOD_DIST` |
| Effects | `K2_SPECIAL_EFFECT` `K2_BILLBOARD` `K2_FORCE` `K2_FORCE_TYPE` |
| Animated texture | `K2_ANIMATED_TEXTURE` `K2_AT_RATE` `K2_AT_DURATION` `K2_AT_FLAGS` `K2_AT_BILLBOARD` `K2_AT_BILLBOARD_MODE` `K2_AT_MATERIAL_ALPHA` `K2_AT_RGB_ALPHA` `K2_AT_OVERRIDE` `K2_AT_APPLY_MODE` `K2_AT_FILTER_MODE` `K2_AT_COLOR_MODE` |
| Camera | `CAMERA` `CAMERA_TYPE` `CAMERA_SETTINGS` |

A second small table at `0x7BCD58` (`g_k2a_option_table`) holds `DisableTextures` and
`DisableSpecialEffects`.

### A shipped bug: `TM_ROWO`

The keyword table spells the first transform row **`TM_ROWO`** — capital letter O, not zero.
Every `.K2A` in the install writes `*TM_ROW0` with a digit. So that keyword can never match, and
`TM_ROW3` is not in the table at all.

Practical consequence for a reimplementation: **do not read the row-matrix form.** Use the
decomposed transform the table does accept — `TM_POS`, `TM_ROTAXIS`, `TM_ROTANGLE` (and
`TM_SCALE`, present in files) — which is what the engine must be doing.

## Traps are embedded in room geometry

`K2_TRAP_BLOCK` appears in **263** shipped `.k2a` files. A block looks like:

```
*K2_TRAP_BLOCK
*K2_TRAP_CENTER      -20.0000  -22.7500  -115.0000
*K2_TRAP_BBOX        -26.0000  -22.7500  -121.0000  -14.0000  -22.7500  -109.0000
*K2_TRAP_PIVOT       -20.0000  -22.7500  -115.0000
*K2_TRAP_PIVOT_AXIS    0.0000    0.0000    -1.0000
*K2_TRAP_PIVOT_ANGLE   0.0000
*K2_TRAP_NAME        "Water"
*K2_TRAP_ID          -1
*K2_TRAP_ARGC        15
*K2_TRAP_ARGV0       ""
```

This closes a loop across three independently-recovered sources:

- `K2_TRAP_NAME` is the **factory registry key** from
  [`../reference/trap-factory.md`](../reference/trap-factory.md) — `"Water"` here, and the
  short-key cases (`Audio`, `Force`, `Room`) apply, so match on the registry key, not the class
  name.
- `K2_TRAP_ARGC` / `ARGV<n>` are the **parameter slots** documented in the developers' own
  `Traps.txt` — see [`text-and-tuning.md`](text-and-tuning.md#trapstxt--trap-parameter-reference-design-doc).
- The names match the `*_tempActiveLog.txt` dump for `prisonfix`.

So level content is: `.lvl` names the rooms, each room `.k2a` carries both its geometry **and**
its trap instances inline.

## Coordinate system — no conversion needed

3DS Max is Z-up; the engine is Y-up. The K2 exporter **already converts**, so `.K2A` vertex data
is in engine space and needs no swizzle. Verified by comparing `prisonfix` room vertex ranges
against actor positions decoded from the savegame:

```
prison1..3 .K2A   x[-73.1, 25.8]  y[ -7.1, 32.0]  z[-92.7, 51.8]
engine actors     x[-62.3, 20.2]  y[  0.0, 23.3]  z[-22.9,102.1]
```

Y is the vertical axis in both, with the same magnitudes. Load vertices as-is.

## Token set

Standard ASE tokens carried over unchanged:

```
*SCENE  *SCENE_FILENAME  *MATERIAL_LIST  *MATERIAL  *MATERIAL_REF
*GEOMOBJECT  *NODE_NAME  *NODE_PARENT  *NODE_TM  *TM_ROW0..*TM_ROW3
*TM_POS  *TM_ROTAXIS  *TM_ROTANGLE  *TM_SCALE  *TM_SCALEAXIS  *TM_SCALEAXISANG
*INHERIT_POS  *INHERIT_ROT  *INHERIT_SCL  *TIMEVALUE
*MESH  *MESH_NUMVERTEX  *MESH_NUMFACES  *MESH_VERTEX_LIST  *MESH_VERTEX
*MESH_FACE_LIST  *MESH_FACE  *MESH_NUMTVERTEX  *MESH_TVERT  *MESH_TFACE
*MESH_NUMCVERTEX  *MESH_CVERTLIST  *MESH_VERTCOL  *MESH_NUMCVFACES
*MESH_CFACELIST  *MESH_CFACE
*PROP_CASTSHADOW  *PROP_RECVSHADOW  *PROP_MOTIONBLUR
*UVW_U_OFFSET  *UVW_V_OFFSET  *UVW_U_TILING  *UVW_V_TILING
*UVW_NOISE_LEVEL  *UVW_NOISE_SIZE  *UVW_NOISE_PHASE  *UVW_BLUR_OFFSET  *UVW_NOUSE_AMT
*WIREFRAME_COLOR
```

K2 additions:

```
*K2_ASCIIEXPORT     exporter version (105)
*K2_FILETYPE        1
*NODE_HAS_K2_DATA   flags a node carrying engine-specific attributes
*GEOM_FORMAT        vertex format / channel mask
```

`*NODE_HAS_K2_DATA` and `*GEOM_FORMAT` are the only tokens that need engine knowledge; everything
else parses with any off-the-shelf ASE reader.

## Per-file shape

Example — `data/Actors/TRAP_CeilingBlade/TRAP_CeilingBlade.K2A`: 4 `*GEOMOBJECT` nodes
(4 × `*NODE_NAME`, `*NODE_TM`, `*MESH`), 100 vertices, 74 faces, 84 texture vertices,
50 texture faces, 100 vertex colours, 74 colour faces.

Actor `.K2A` files are the **modelling-time** geometry. At runtime the engine consumes them
through `K2ImportOptions` / `ObjectBundle` / `MaterialBundle` and hands the result to NetImmerse,
which is why both `.k2a` and `.nif` paths appear in the same linkmaps.

## Relationship to the other formats

```
<Name>.K2A          geometry + node hierarchy + materials   (ASCII)
<Name>.act          actor prototype, property table          (binary)
Skills/*.skl        animation clips bound to the node names  (binary)
Behaviors/*.bhvr    HFSM referencing skill names             (ASCII)
Textures/*.bmp      materials referenced by *MATERIAL        (BMP)
```

Node names in `.K2A` are the join key: `.skl` `vKey` records and `*_linkmap.txt` entries both
address scene objects by these exact names (e.g. `gear_SideLow`, `Pivot`, `S_burning_Plat1`).

## Notes for tooling

- The header timestamps are real export dates (Aug–Nov 1998/1999) and are a useful way to order
  assets by production stage.
- `*MESH_CFACE` / `*MESH_VERTCOL` are always present — this engine leans on baked vertex colour
  rather than lightmaps.
- No skinning tokens appear in the actor `.K2A` files; character deformation is driven by the
  joint hierarchy from `.act` + `.skl`, not by per-vertex weights in the mesh file.
