# Plan: the web experiment

A second runtime for the same data: `web/`, in HTML, vanilla JavaScript modules
and WebGPU, with no build step and no dependencies. It boots the retail install,
renders `prisonfix`, and is playable.

This is not a port of `src/`. It shares the file formats and nothing else, and
it exists to answer a different question. `src/` asks *what did the original
do*. This asks *what should the game be*, using the same data as ground truth.

---

## 1. Why a second runtime at all

The native viewer is at the point where the remaining work is behavioural, not
structural: `CharacterStateTable` is 20% recovered and `MfLocoPlanner` is at 0%
(`docs/reference/coverage.md`). Everything below that — formats, scene graph,
boot, spawning — is done or nearly done.

That leaves a gap. The formats are proven by *parsing*, not by *playing*, and
there is no way to find out whether the level geometry actually supports a
character until something walks on it. This runtime is that test, and it turned
up things a parser never would: the `[Fog]` factors matter more than they look,
the prison's starting cell is too small for a follow camera, and the walkways
have crates sitting on the exact lips a climb has to target.

Three properties make it worth maintaining alongside the C++:

- **Zero install.** A URL, a browser, and a copy of the install the user already
  owns. No compiler, no SDK, no platform layer.
- **Inspectable.** `k2.player.y = 20` in the console, live. `k2.probe.floor_under(x, y, z)`
  answers a collision question without a rebuild. The 1999 build shipped a whole
  debug UI (`PMW_*`, `AConsole`) that we cannot re-enable yet; this gets the
  useful half of it for one line of code.
- **Disposable.** It is explicitly an experiment. Nothing in `src/` depends on it.

## 2. What is implemented

| Layer | File | State |
|---|---|---|
| VFS (case-insensitive, backslash paths) | `js/core/vfs.js` | port of `vfs.cpp`, + a manifest and a folder-picker backend |
| `.K2A` parser + mesh build | `js/assets/k2a.js` | port of `k2a.cpp`; 582/582 nodes in prisonfix region 0 |
| `.lvl` parser | `js/assets/lvl.js` | port of `lvl.cpp`; matches the native loader field for field |
| BMP / TGA / `.ifl` | `js/assets/image.js` | port of `image.cpp`; 40/40 textures in region 0 |
| Module table | `js/engine/module_table.js` | the 20 entries, still in `strcmpi` order, still self-checking |
| Renderer | `js/gfx/gfx.js`, `js/gfx/shaders.js` | WebGPU, MSAA 4x, mips, fog, alpha pass |
| Collision | `js/game/collision.js` | BVH over the room triangles, capsule sweep, ray queries |
| Ledges | `js/game/ledges.js` | derived from geometry, not authored |
| Character | `js/game/player.js` | ground / air / hang / climb |
| Camera | `js/game/camera.js` | collision-aware orbit rig + free fly |
| Rewind | `js/game/rewind.js` | 12 s ring buffer over the character state |

Measured on `prisonfix` region 0 (5 rooms), a 2018 Radeon Pro 560X:

```
parse + upload   ~1.4 s      582 meshes, 7 140 tris, 40 textures
collision        5 786 tris, 679 BVH nodes, 9 ms
ledges           488 from 2 488 up-facing boundary edges, 13 ms
frame            60 fps, 46 draw calls, 3 pipelines
```

Verified end to end by driving a headless Chrome over CDP: the character spawns
at the `.lvl`'s `[Prince]` point (22.00, 0.01, 1.92), walks, jumps 1.1 m, falls
at exactly `g·t`, grabs a derived ledge at y = 11.25, mantles onto it, and
rewinds back up through the fall with velocities restored.

## 3. The three decisions worth arguing with

### 3.1 Parsers in JavaScript, not a baked asset pack

The alternative was a `k2boy --bake` step emitting GPU-ready buffers, reusing
the already-validated C++ parsers. It would load faster and duplicate no format
code.

It was rejected because it makes the web runtime a *consumer* of the C++ one.
Re-deriving the parsers in a second language is the strongest available check
that the format documentation is complete rather than merely sufficient for one
implementation — and it caught real things: the `.ifl` indirection, the two
compounded V-axis corrections, the fact that `*ALPHA_SOURCE` is not what marks a
material transparent.

Cost: ~800 lines of parser that must be kept in step with `src/assets/`. The
mitigation is that both are ports of a written spec in `docs/formats/`, not of
each other.

### 3.2 Collide against the visible geometry

The original's collision comes from `PrinceFloor` / `FloorInfo` and authored
`NiCollisionGroup` volumes; `data/prisonfix_FloorGenerationLog.txt` is the
output of a floor-generation pass we have not reversed.

So this collides against the room meshes themselves. Every visible surface is
solid, except blended materials — flames, water sheets and grates, which are
excluded on the grounds that solid ones would wall off half the dungeon.

This is more permissive than the original and will disagree with it wherever the
designers made something walk-through. When the floor system is reversed, this
becomes the fallback rather than the primary.

### 3.3 Ledges derived, not authored

The single biggest departure. In the retail game, everything grabbable is a
hand-placed volume, which is why a wall that looks climbable often is not — the
most common complaint about how PoP3D plays.

`ledge_build` instead takes every up-facing triangle, finds the boundary of that
set, and keeps the near-horizontal boundary edges that have a drop outside and
headroom above. On prisonfix that is 488 ledges from 2 488 candidate edges, and
they land where a player would expect them: walkway lips, wall tops, sills.

The risk is the mirror image of the original's: *too much* is climbable, and the
level's intended route stops being the only route. That is a tuning problem
(`MIN_DROP`, `MAX_EDGE_TILT`, headroom) rather than a design one, and it is the
right way round for an experiment.

## 4. Bugs this shook out, and what they cost

Recorded because each was invisible until something moved through the level.

**`coll_raycast` returned the nearest hit and the caller filtered it.** A floor
probe that takes the closest triangle and then rejects it for facing the wrong
way reports "no floor" whenever a downward-facing triangle sits above the real
one — every doorway and every ceiling beam. The character refused to mantle onto
solid floors. Fixed by filtering during traversal (`min_ny`), not after.

**The camera arm compressed to nothing.** Backing off along the ray leaves the
eye inside the wall in a cell barely wider than the character; the frame goes
black while the HUD reports a healthy camera. Fixed by pushing off the surface
normal and flooring the arm length.

**A shared tint array.** `gfx_draw` records draws and packs uniforms at
end-of-frame, so a reused scratch `Float32Array` gives every trap volume the
last colour written. The kind of bug the deferred-recording design invites, and
worth remembering elsewhere in that file.

**Ledges with crates on them.** The mantle target has to rise above whatever is
standing on the lip rather than give up, which is why `find_mantle_target`
sweeps upward instead of sampling once.

## 5. Where this goes next

In rough order of value per unit of work.

1. **Region streaming and level transitions.** `?region=1` already loads the
   other half of prisonfix. The connection between them is `.nbr` neighbour data
   (decoded, `docs/formats/lvl.md`) and the `Area` traps. Making a doorway
   actually stream is the difference between a room and a level.

2. **Traps as behaviour, not wireframes.** 107 volumes load in region 0 and 33
   trap types are registered (`docs/reference/trap-factory.md`), with
   `data/Traps.txt` documenting the parameter slots. `PressurePlate`, `Door`,
   `Spikes` and `Mover` are self-contained and would make the dungeon a level
   rather than a diorama. This is the highest-value gameplay work available.

3. **The Prince's actual body.** Blocked on the `.act` node hierarchy (65%,
   `docs/formats/act.md`) and the `.skl` channel semantics (95%). When those
   land, `player_capsule_mesh` is replaced and nothing else in the controller
   changes — which was the point of keeping the controller ignorant of the mesh.

4. **Audio.** `.sfx` is fully decoded raw PCM with loop points; WebAudio takes
   it directly. Footsteps and ambience are worth more to how the dungeon feels
   than any renderer work listed here.

5. **A ledge *graph*.** Shimmying is currently confined to one segment; the
   character cannot round a corner. Joining segments whose ends meet turns the
   ledge set into a graph and makes traversal continuous.

6. **Combat.** `data/fight.ini` and `data/enemy.dat` are decoded
   (`docs/formats/text-and-tuning.md`) and `EnemyAIObject` is named but not
   reversed. This is the largest remaining piece of the actual game and should
   not start until 1-3 are done.

7. **Renderer, only when it pays.** Texture animation (28 `.ifl` files, torch
   flames), then the sky dome. Neither changes what the game *is*.

### Deliberately not doing

- **Shadows, PBR, post-processing.** The content is baked-vertex-colour,
  fixed-function era. Modern lighting on it looks worse, not better.
- **A scripting layer.** The retail game runs no scripts — no `.fms`, 13 of 14
  `Behaviors` directories empty, `PiccoloRT.dll` never loaded
  (`docs/plan/03-reimplementation.md` §1). Adding Lua would invent an
  architecture the original did not have.
- **A bundler.** The whole runtime is ES modules served as files. It loads in
  under a second and every line is readable in the browser's own debugger. That
  is worth more here than a minified byte count.

## 6. Running it

```
make web            # regenerate the manifest, serve at :8080
```

then open `http://localhost:8080/web/`. Needs a WebGPU browser (Chrome 113+,
Edge 113+, Safari 18+).

The browser cannot list a directory, so the index `vfs.cpp` builds with
`opendir` is generated ahead of time into `web/data-index.txt` by
`tools/webindex.py` (`make webindex`). It is derived from the install and is
git-ignored. Without it the page offers a folder picker instead, which needs no
server at all.

URL parameters: `?module=cistern`, `?level=<path>`, `?region=1`, `?free`.
