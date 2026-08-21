// ledges.js — derives a grabbable-edge set from the room geometry.
//
// This is the one place where the web build deliberately does something the
// 1999 engine did not, rather than reproducing it.
//
// In the original, everything the Prince can grab is authored: the level's
// .K2A carries K2_TRAP blocks (Bar, Pole, Rope, Ledge-ish Fixture volumes) and
// PrinceFloor / FloorInfo drive the rest. That is why the retail game's
// climbing only works where a designer put a volume, and why a wall that looks
// identical to a climbable one two rooms earlier is not climbable. It is also
// the single most common complaint about how PoP3D controls.
//
// Deriving the set from geometry instead means every surface that *looks* like
// a ledge is one. The rule:
//
//   1. take every triangle whose normal points up (a walkable top surface);
//   2. find the edges of that set — an edge used by exactly one up-facing
//      triangle is the boundary of a walkable island;
//   3. keep the boundary edges that are near-horizontal and have a drop on the
//      outside. That is a ledge.
//
// Step 3 is what keeps the count sane: a room's floor mesh has thousands of
// boundary edges where it meets a wall, and almost all of them are interior
// seams with no drop beyond.

import { coll_raycast, coll_floor_under } from './collision.js';

const UP_COS = 0.65;             // steeper than this is a wall, not a top surface
const MAX_EDGE_TILT = 0.18;      // metres of rise across the edge; ~10 degrees
const MIN_DROP = 1.1;            // a lip shorter than this is a step, not a ledge
const MIN_LENGTH = 0.45;         // too short to aim at
const OUT_PROBE = 0.45;          // how far outside the edge to look for the drop
const QUANT = 1024;              // vertex weld resolution for edge matching

const CELL = 3;                  // spatial hash cell size, metres

// Fields per segment in the flat array.
const S = 8;                     // x0 y0 z0 x1 y1 z1 out_x out_z

export function ledge_build(world) {
    const t0 = performance.now();

    // --- 1. edges of the up-facing triangle set -----------------------------
    //
    // Vertices are welded on a fixed grid rather than by exact value: the .K2A
    // exporter writes six decimals and adjacent floor tiles routinely disagree
    // in the last one, which would leave every shared edge looking like a
    // boundary.
    const edges = new Map();     // key -> {count, ax..cz of one owner}

    const tris = world.tris;
    const normals = world.normals;

    for (let t = 0; t < world.tri_count; ++t) {
        if (normals[t * 3 + 1] < UP_COS) continue;
        const o = t * 9;

        for (let e = 0; e < 3; ++e) {
            const i0 = o + e * 3;
            const i1 = o + ((e + 1) % 3) * 3;
            const i2 = o + ((e + 2) % 3) * 3;

            const k0 = vkey(tris[i0], tris[i0 + 1], tris[i0 + 2]);
            const k1 = vkey(tris[i1], tris[i1 + 1], tris[i1 + 2]);
            const key = k0 < k1 ? k0 + '|' + k1 : k1 + '|' + k0;

            const hit = edges.get(key);
            if (hit) { hit.count++; continue; }

            edges.set(key, {
                count: 1,
                x0: tris[i0], y0: tris[i0 + 1], z0: tris[i0 + 2],
                x1: tris[i1], y1: tris[i1 + 1], z1: tris[i1 + 2],
                // The opposite corner tells us which side is "inside".
                ox: tris[i2], oz: tris[i2 + 2],
            });
        }
    }

    // --- 2. keep boundary edges that overhang something ---------------------

    const kept = [];

    for (const e of edges.values()) {
        if (e.count !== 1) continue;

        const dy = Math.abs(e.y1 - e.y0);
        if (dy > MAX_EDGE_TILT) continue;

        const ex = e.x1 - e.x0, ez = e.z1 - e.z0;
        const len = Math.hypot(ex, ez);
        if (len < MIN_LENGTH) continue;

        // Outward horizontal normal: perpendicular to the edge, pointing away
        // from the triangle's third corner.
        let nx = -ez / len, nz = ex / len;
        const mid_x = (e.x0 + e.x1) * 0.5, mid_z = (e.z0 + e.z1) * 0.5;
        if ((e.ox - mid_x) * nx + (e.oz - mid_z) * nz > 0) { nx = -nx; nz = -nz; }

        const y = Math.max(e.y0, e.y1);

        // Is there a drop on the outside? Sample just past the lip. `null`
        // means nothing below at all within the probe, which is still a ledge.
        const px = mid_x + nx * OUT_PROBE, pz = mid_z + nz * OUT_PROBE;
        const below = coll_floor_under(world, px, y - 0.05, pz, MIN_DROP + 0.5);
        if (below !== null && y - below < MIN_DROP) continue;

        // Reject edges with geometry directly above the lip: a ledge you cannot
        // get your hands over is not a ledge. This is what rules out the seams
        // where a floor meets a wall it also passes under.
        const head = coll_raycast(world, mid_x, y + 0.05, mid_z, 0, 1, 0, 1.0);
        if (head) continue;

        kept.push([e.x0, y, e.z0, e.x1, y, e.z1, nx, nz]);
    }

    // --- 3. flatten and index ----------------------------------------------

    const segs = new Float32Array(kept.length * S);
    for (let i = 0; i < kept.length; ++i) segs.set(kept[i], i * S);

    const grid = new Map();
    for (let i = 0; i < kept.length; ++i) {
        const o = i * S;
        const cx0 = Math.floor(Math.min(segs[o], segs[o + 3]) / CELL);
        const cx1 = Math.floor(Math.max(segs[o], segs[o + 3]) / CELL);
        const cz0 = Math.floor(Math.min(segs[o + 2], segs[o + 5]) / CELL);
        const cz1 = Math.floor(Math.max(segs[o + 2], segs[o + 5]) / CELL);
        for (let cx = cx0; cx <= cx1; ++cx) {
            for (let cz = cz0; cz <= cz1; ++cz) {
                const key = cx + ',' + cz;
                let list = grid.get(key);
                if (!list) { list = []; grid.set(key, list); }
                list.push(i);
            }
        }
    }

    return {
        segs,
        count: kept.length,
        grid,
        build_ms: performance.now() - t0,
        candidate_edges: edges.size,
    };
}

function vkey(x, y, z) {
    return `${Math.round(x * QUANT)},${Math.round(y * QUANT)},${Math.round(z * QUANT)}`;
}

// --------------------------------------------------------------------- query

// Nearest grabbable point, or null.
//
// `y` is the character's feet. A ledge is reachable when its lip sits between
// `up_min` and `up_max` above the feet, within `reach` horizontally, and the
// character is not behind it (the lip's outward normal must roughly oppose the
// approach).
export function ledge_find(lg, x, y, z, face_x, face_z, reach, up_min, up_max) {
    if (lg.count === 0) return null;

    let best = null;
    let best_score = Infinity;

    const cx0 = Math.floor((x - reach) / CELL), cx1 = Math.floor((x + reach) / CELL);
    const cz0 = Math.floor((z - reach) / CELL), cz1 = Math.floor((z + reach) / CELL);

    for (let cx = cx0; cx <= cx1; ++cx) {
        for (let cz = cz0; cz <= cz1; ++cz) {
            const list = lg.grid.get(cx + ',' + cz);
            if (!list) continue;

            for (const i of list) {
                const o = i * S;
                const ly = lg.segs[o + 1];
                const up = ly - y;
                if (up < up_min || up > up_max) continue;

                // Closest point on the (horizontal) segment.
                const ax = lg.segs[o], az = lg.segs[o + 2];
                const bx = lg.segs[o + 3], bz = lg.segs[o + 5];
                const ex = bx - ax, ez = bz - az;
                const len2 = ex * ex + ez * ez;
                let t = len2 > 1e-9 ? ((x - ax) * ex + (z - az) * ez) / len2 : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                const px = ax + ex * t, pz = az + ez * t;

                const dx = px - x, dz = pz - z;
                const dist = Math.hypot(dx, dz);
                if (dist > reach) continue;

                // The lip must face back at us: approaching from behind means
                // walking off the surface, not grabbing its edge.
                const nx = lg.segs[o + 6], nz = lg.segs[o + 7];
                const toward = -(dx * nx + dz * nz) / (dist > 1e-4 ? dist : 1);
                if (toward < 0.1) continue;

                // Prefer what the character is actually facing.
                const aim = face_x * nx + face_z * nz;      // -1 = facing the wall
                const score = dist - aim * 0.6;
                if (score < best_score) {
                    best_score = score;
                    best = { x: px, y: ly, z: pz, nx, nz, index: i, dist, up };
                }
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------- debug view

// Line-list vertex data for the F4 overlay: the lip in gold, a short outward
// tick at the midpoint so the facing direction is readable.
export function ledge_debug_lines(lg) {
    const verts = lg.count * 4;
    const buf = new ArrayBuffer(verts * 16);
    const f32 = new Float32Array(buf);
    const u32 = new Uint32Array(buf);

    const GOLD = 0xff8ac9e8 | 0;      // ABGR
    const TICK = 0xff5f9fe0 | 0;

    for (let i = 0; i < lg.count; ++i) {
        const o = i * S;
        const v = i * 16;

        f32[v] = lg.segs[o]; f32[v + 1] = lg.segs[o + 1] + 0.02; f32[v + 2] = lg.segs[o + 2];
        u32[v + 3] = GOLD;
        f32[v + 4] = lg.segs[o + 3]; f32[v + 5] = lg.segs[o + 4] + 0.02; f32[v + 6] = lg.segs[o + 5];
        u32[v + 7] = GOLD;

        const mx = (lg.segs[o] + lg.segs[o + 3]) * 0.5;
        const my = lg.segs[o + 1] + 0.02;
        const mz = (lg.segs[o + 2] + lg.segs[o + 5]) * 0.5;
        f32[v + 8] = mx; f32[v + 9] = my; f32[v + 10] = mz;
        u32[v + 11] = TICK;
        f32[v + 12] = mx + lg.segs[o + 6] * 0.3;
        f32[v + 13] = my;
        f32[v + 14] = mz + lg.segs[o + 7] * 0.3;
        u32[v + 15] = TICK;
    }

    return { data: new Float32Array(buf), vertex_count: verts };
}
