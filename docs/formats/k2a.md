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

## `*ALPHA_SOURCE` does not mean "transparent"

**This was a real bug and it broke draw order in every room but the first.**

A material body contains nested `*MAP_<CHANNEL>` blocks, each with its own
`*BITMAP` and `*ALPHA_SOURCE`. The parser originally scanned the whole material
to brace depth 0, which got both fields wrong:

* **transparency** was set from any `*ALPHA_SOURCE Opacity`. That token only says
  where a *map's* alpha comes from; a plain wall's diffuse map routinely says
  `Opacity` while the material is fully opaque. Every shipped `prisonfix`
  material has `*MATERIAL_TRANSPARENCY 0.0000`.
* **`bitmap`** took the *last* `*BITMAP` seen, so a material with an opacity or
  specular map got that map's image as its diffuse texture.

Measured on the prisonfix rooms:

| room | materials | `*MAP_OPACITY` | `*ALPHA_SOURCE Opacity` |
|---|---:|---:|---:|
| prison1 | 132 | 1 | 3 |
| prison2 | 61 | 2 | 39 |
| prison3 | 131 | **0** | **105** |

prison3 has *no* transparent materials at all, yet 105 of its 131 were being
flagged. Transparent draws with depth-write **off**, so most of the room lost
depth ordering and far polygons painted over near ones. prison1 happened to be
mostly opaque, which is exactly why "the first room is fine, the others are not".

### The correct rule

A material is alpha-blended when it has a **`*MAP_OPACITY` block**, or
`*MATERIAL_TRANSPARENCY > 0`. `bitmap` comes from `*MAP_DIFFUSE`, falling back to
the first bitmap when there is no explicit diffuse channel.

Item counts before and after, per room:

```
prison1            opaque 180 -> 192   transparent 15 -> 3
prison2            opaque  25 ->  64   transparent 42 -> 3
prison3            opaque  64 -> 156   transparent 92 -> 0
prison3_hall       opaque  19 ->  31   transparent 12 -> 0
prison3_junction   opaque  11 ->  26   transparent 15 -> 0
```

which matches the `*MAP_OPACITY` counts exactly.

### Validated install-wide

`k2boy --materials` (or `make materials`) sweeps every `.K2A`:

```
materials: 501 files, 28119 materials
  name a bitmap   27062
  bitmap resolves 27061
  unresolved      1
  blended (MAP_OPACITY or TRANSPARENCY>0) 624 (2.2%)
    missing: D:\3DSMAX2.5\Scenes\ripple.ifl
```

**27061 of 27062 texture references resolve.** The single miss is an authoring
path that was never shipped, so the per-channel change did not cost a texture
anywhere. 2.2% blended is a plausible figure for flames, water and grates —
compare the old rule, which flagged 105 of prison3's 131 materials alone.

## Triangle winding is CCW, so backface culling is safe

`k2boy --winding` (or `make winding`) measures it rather than guessing. For a
closed mesh the divergence theorem gives a signed volume from the triangles
alone; with counter-clockwise outward winding — OpenGL's default front face —
that volume is positive. Actor body parts are watertight, so they are what gets
measured:

```
winding over 1201 actor meshes
  positive volume (CCW outward) 1028   total 12.245
  negative volume (CW outward)  157   total -0.167
  degenerate / open             16
  verdict: CCW front faces (86.8% agree)
```

By count it is 87%, but **by volume it is decisive**: the negative meshes total
-0.167 against +12.245, i.e. they are near-degenerate slivers rather than
genuinely inverted geometry.

`CULL_BACK` is now the default (`--cull none|back|front` overrides). Verified:

| view | none vs back |
|---|---|
| inside a room | identical (mean 22.28, 38.4% near-black, both) |
| level default | 0.001% of pixels differ |
| from *outside* a room | 7.5% differ — culling correctly reveals the interior instead of the outer wall's back face |

The third row is the point: the difference is not an artefact, it is culling
doing its job for a camera the original would never have placed there.
