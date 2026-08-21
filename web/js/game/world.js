// world.js — turns a .lvl into something the renderer and the character can use.
//
// Follows the native path in src/main.cpp (load_level -> append_scene), with
// the same classification rules, and adds two things that only exist here:
// draw batching, and a collision mesh.
//
// Batching matters more in a browser than it did natively. A prisonfix region
// is ~900 .K2A nodes; issuing 900 draws per frame through WebGPU's validation
// layer costs more than the triangles do. Room geometry is world-space (the
// exporter bakes node transforms into the vertices — main.cpp draws it all
// under one identity matrix), so meshes sharing a texture can simply be
// concatenated. That takes the frame from ~900 draws to ~60.

import { fs_read, fs_read_text, path_basename, normalise } from '../core/vfs.js';
import { k2a_parse, k2a_build_mesh, K2A_ALPHA_OPACITY, K2A_VERTEX_STRIDE } from '../assets/k2a.js';
import { lvl_parse, lvl_room_filename, lvl_fog_range } from '../assets/lvl.js';
import { image_load, image_build_mips, image_resolve_ifl } from '../assets/image.js';
import { gfx_create_mesh, gfx_create_texture, gfx_free_all, GFX_INVALID_TEXTURE } from '../gfx/gfx.js';
import { coll_build, coll_floor_under } from './collision.js';
import { ledge_build } from './ledges.js';

export async function world_load_level(path, region_index, on_progress) {
    const report = on_progress ?? (() => {});

    const text = await fs_read_text(path);
    if (!text) throw new Error(`level not found: ${path}`);

    const lvl = lvl_parse(text);
    if (!lvl.regions.length) throw new Error(`${path} has no [Region] or [Rooms]`);

    const region = lvl.regions[Math.min(region_index, lvl.regions.length - 1)];

    const world = {
        path,
        lvl,
        region,
        region_index: Math.min(region_index, lvl.regions.length - 1),

        batches: [],          // merged opaque geometry, one per texture
        transparent: [],      // kept per node so it can be depth-sorted
        volumes: [],          // trigger volumes, debug only
        trap_types: [],
        spawns: [],

        fog: lvl_fog_range(lvl),
        sky: lvl.has_sky_color
            ? [lvl.sky_color[0] / 255, lvl.sky_color[1] / 255, lvl.sky_color[2] / 255]
            : null,

        collision: null,
        ledges: null,

        min: [Infinity, Infinity, Infinity],
        max: [-Infinity, -Infinity, -Infinity],

        stats: {
            rooms: 0, rooms_missing: 0, nodes: 0, tris: 0,
            textures: 0, textures_failed: 0, draw_batches: 0,
        },
    };

    // Texture cache is level-wide: the prisonfix rooms share most of their
    // wall bitmaps, and each is a separate GPU upload otherwise.
    const tex_cache = new Map();          // normalised basename -> texture id

    // Accumulators for the merge, keyed by texture id.
    const opaque = new Map();             // tex id -> {chunks, indices, vert_count, index_count}
    const coll_chunks = [];
    let coll_tri_count = 0;

    for (let r = 0; r < region.rooms.length; ++r) {
        const file = lvl_room_filename(region.rooms[r]);
        report(`room ${r + 1}/${region.rooms.length}: ${file}`,
               (r / region.rooms.length) * 0.8);

        const room_text = await fs_read_text(file);
        if (!room_text) {
            ++world.stats.rooms_missing;
            console.warn(`world: missing room ${file}`);
            continue;
        }

        let k2a;
        try {
            k2a = k2a_parse(room_text);
        } catch (e) {
            ++world.stats.rooms_missing;
            console.warn(`world: ${file}: ${e.message}`);
            continue;
        }
        ++world.stats.rooms;

        // Resolve this file's materials to textures.
        const mat_tex = new Array(k2a.materials.length).fill(GFX_INVALID_TEXTURE);
        const mat_blend = new Array(k2a.materials.length).fill(false);
        for (let m = 0; m < k2a.materials.length; ++m) {
            mat_blend[m] = k2a.materials[m].alpha === K2A_ALPHA_OPACITY;
            mat_tex[m] = await load_texture(k2a.materials[m].bitmap, tex_cache, world.stats);
        }

        for (const node of k2a.nodes) {
            if (node.has_trap &&
                (node.trap.name === 'PrinceStart' || node.trap.name === 'EnemyStart')) {
                world.spawns.push({
                    kind: node.trap.name,
                    x: node.trap.center[0], y: node.trap.center[1], z: node.trap.center[2],
                    argv: node.trap.argv,
                    name: node.name,
                });
            }

            if (!node.has_mesh) continue;
            const mesh = k2a_build_mesh(node);
            if (!mesh) continue;
            ++world.stats.nodes;
            world.stats.tris += mesh.index_count / 3;

            // A trap block does not by itself mean "invisible trigger". Nodes
            // like TRAP_Showerdoor carry a trap *and* real textured geometry.
            // The pure trigger volumes are the ones with no material at all.
            const is_volume = node.has_trap && node.material_ref < 0;
            const tex = node.material_ref >= 0 && node.material_ref < mat_tex.length
                ? mat_tex[node.material_ref] : GFX_INVALID_TEXTURE;
            const blended = node.material_ref >= 0 && node.material_ref < mat_blend.length
                ? mat_blend[node.material_ref] : false;

            if (is_volume) {
                let type = world.trap_types.indexOf(node.trap.name);
                if (type < 0) { type = world.trap_types.length; world.trap_types.push(node.trap.name); }
                world.volumes.push({
                    mesh_id: gfx_create_mesh(mesh.vertices, mesh.indices),
                    trap_type: type,
                    name: node.trap.name,
                });
                continue;                       // never in bounds or collision
            }

            grow_bounds(world, mesh.min, mesh.max);

            if (blended) {
                // Alpha surfaces stay individual: they are drawn back to front,
                // and merging them by texture would destroy the ordering.
                world.transparent.push({
                    mesh_id: gfx_create_mesh(mesh.vertices, mesh.indices),
                    texture: tex,
                    center: [(mesh.min[0] + mesh.max[0]) / 2,
                             (mesh.min[1] + mesh.max[1]) / 2,
                             (mesh.min[2] + mesh.max[2]) / 2],
                });
                // Deliberately not collidable. Every blended material in the
                // shipped rooms is a flame, a water sheet or a grate — solid
                // ones would wall off half the dungeon.
                continue;
            }

            let batch = opaque.get(tex);
            if (!batch) {
                batch = { chunks: [], indices: [], vert_count: 0, index_count: 0 };
                opaque.set(tex, batch);
            }
            batch.chunks.push(mesh.vertices);
            batch.indices.push({ src: mesh.indices, base: batch.vert_count });
            batch.vert_count += mesh.vert_count;
            batch.index_count += mesh.index_count;

            coll_chunks.push(mesh);
            coll_tri_count += mesh.index_count / 3;
        }
    }

    // --- upload the merged batches ------------------------------------------

    report('uploading geometry', 0.82);
    for (const [tex, b] of opaque) {
        const verts = new Uint8Array(b.vert_count * K2A_VERTEX_STRIDE);
        let off = 0;
        for (const c of b.chunks) { verts.set(c, off); off += c.length; }

        const indices = new Uint32Array(b.index_count);
        let io = 0;
        for (const part of b.indices) {
            for (let i = 0; i < part.src.length; ++i) indices[io + i] = part.src[i] + part.base;
            io += part.src.length;
        }

        world.batches.push({
            mesh_id: gfx_create_mesh(verts, indices),
            texture: tex,
            index_count: b.index_count,
        });
    }
    world.stats.draw_batches = world.batches.length;

    // --- collision ----------------------------------------------------------

    report('building collision', 0.88);
    {
        const tri_xyz = new Float32Array(coll_tri_count * 9);
        let o = 0;
        for (const mesh of coll_chunks) {
            const f32 = new Float32Array(mesh.vertices.buffer, mesh.vertices.byteOffset,
                                         mesh.vertices.byteLength / 4);
            for (let i = 0; i < mesh.index_count; ++i) {
                const v = mesh.indices[i] * 9;
                tri_xyz[o++] = f32[v];
                tri_xyz[o++] = f32[v + 1];
                tri_xyz[o++] = f32[v + 2];
            }
        }
        world.collision = coll_build(tri_xyz);
    }

    report('deriving ledges', 0.94);
    world.ledges = ledge_build(world.collision);

    report('ready', 1);
    return world;
}

function grow_bounds(world, mn, mx) {
    for (let a = 0; a < 3; ++a) {
        if (mn[a] < world.min[a]) world.min[a] = mn[a];
        if (mx[a] > world.max[a]) world.max[a] = mx[a];
    }
}

async function load_texture(bitmap, cache, stats) {
    if (!bitmap) return GFX_INVALID_TEXTURE;

    const resolved = await image_resolve_ifl(bitmap);
    // Materials name absolute authoring paths from the artist's machine
    // (C:\3dsmax\...), so only the basename is usable.
    const key = normalise(path_basename(resolved));

    const hit = cache.get(key);
    if (hit !== undefined) return hit;

    const buf = await fs_read(resolved);
    if (!buf) {
        ++stats.textures_failed;
        cache.set(key, GFX_INVALID_TEXTURE);
        return GFX_INVALID_TEXTURE;
    }

    try {
        const img = image_load(buf);
        const mips = image_build_mips(img.width, img.height, img.rgba);
        const id = gfx_create_texture(img.width, img.height, img.rgba, mips);
        ++stats.textures;
        cache.set(key, id);
        return id;
    } catch (e) {
        ++stats.textures_failed;
        console.warn(`world: ${key}: ${e.message}`);
        cache.set(key, GFX_INVALID_TEXTURE);
        return GFX_INVALID_TEXTURE;
    }
}

// ------------------------------------------------------------------- spawning

// Where the Prince starts. Two mechanisms exist and levels use different ones —
// see docs/plan/05-boot-to-gameplay.md:
//
//   * a [Prince] line inside a [Region] ("<body> x,y,z,heading", degrees). Live
//     in prisonfix, the game's first level.
//   * a PrinceStart trap in room geometry. Used by Cistern, whose [Prince] line
//     is commented out.
//
// [Prince] first, trap as the fallback, matching the native loader.
export function world_prince_start(world) {
    const from_lvl = world.region.prince_starts[0];
    if (from_lvl) {
        return {
            x: from_lvl.x, y: from_lvl.y, z: from_lvl.z,
            yaw: from_lvl.heading * Math.PI / 180,
            source: '[Prince]',
        };
    }

    const trap = world.spawns.find((s) => s.kind === 'PrinceStart');
    if (trap) return { x: trap.x, y: trap.y, z: trap.z, yaw: 0, source: 'PrinceStart trap' };

    // Nothing authored: drop into the middle of the bounds and let gravity
    // sort it out. Better than refusing to load the level.
    return {
        x: (world.min[0] + world.max[0]) / 2,
        y: world.max[1],
        z: (world.min[2] + world.max[2]) / 2,
        source: 'bounds centre (no start found)',
    };
}

// The .lvl start heights are authored against the original floor system and
// sit a centimetre above it (prisonfix writes y = 0.01). Against a collision
// mesh built from the visible geometry that can be just inside a floor tile,
// so the spawn is lifted and dropped.
export function world_settle_spawn(world, spawn) {
    const floor = coll_floor_under(world.collision, spawn.x, spawn.y + 2.5, spawn.z, 12);
    if (floor !== null) spawn.y = floor + 0.02;
    return spawn;
}

export function world_free(world) {
    gfx_free_all();
    if (!world) return;
    world.batches.length = 0;
    world.transparent.length = 0;
    world.volumes.length = 0;
}
