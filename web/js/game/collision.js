// collision.js — triangle soup, BVH, and the capsule sweep the character runs on.
//
// The original had none of this in a reusable form: floors came from
// PrinceFloor / FloorInfo and walls from NiCollisionGroup volumes authored per
// room. Rebuilding that needs the whole trap and floor-generation pipeline
// (see data/prisonfix_FloorGenerationLog.txt for what it produced), which is
// still unreversed.
//
// So this takes the other route: collide against the room geometry itself. The
// .K2A meshes are already world-space and already loaded for rendering, so the
// collider costs one extra pass over data we have. It is also strictly more
// permissive than the original — every visible surface is solid — which is the
// right default for an experiment.
//
// Everything is flat typed arrays. A prisonfix region is ~100k triangles and
// the per-frame query has to stay well under a millisecond.

// Capsule bottoms start this far above the feet, so anything shorter is walked
// over rather than collided with. It is the cheapest correct-feeling stair
// handling: the dungeon is full of 15-25 cm lips that would otherwise stop a
// run dead.
export const STEP_HEIGHT = 0.35;

// cos of the steepest slope that still counts as ground. Anything steeper is a
// wall and the character slides down it.
const GROUND_COS = 0.55;

// --------------------------------------------------------------------- build

// `tri_xyz` is a Float32Array of 9 floats per triangle (three corners).
export function coll_build(tri_xyz) {
    const tri_count = (tri_xyz.length / 9) | 0;

    const normals = new Float32Array(tri_count * 3);
    const centroids = new Float32Array(tri_count * 3);
    const bounds = new Float32Array(tri_count * 6);

    for (let i = 0; i < tri_count; ++i) {
        const o = i * 9;
        const ax = tri_xyz[o], ay = tri_xyz[o + 1], az = tri_xyz[o + 2];
        const bx = tri_xyz[o + 3], by = tri_xyz[o + 4], bz = tri_xyz[o + 5];
        const cx = tri_xyz[o + 6], cy = tri_xyz[o + 7], cz = tri_xyz[o + 8];

        const e0x = bx - ax, e0y = by - ay, e0z = bz - az;
        const e1x = cx - ax, e1y = cy - ay, e1z = cz - az;
        let nx = e0y * e1z - e0z * e1y;
        let ny = e0z * e1x - e0x * e1z;
        let nz = e0x * e1y - e0y * e1x;
        const len = Math.hypot(nx, ny, nz);
        if (len > 1e-12) { nx /= len; ny /= len; nz /= len; } else { nx = 0; ny = 1; nz = 0; }
        normals[i * 3] = nx; normals[i * 3 + 1] = ny; normals[i * 3 + 2] = nz;

        centroids[i * 3] = (ax + bx + cx) / 3;
        centroids[i * 3 + 1] = (ay + by + cy) / 3;
        centroids[i * 3 + 2] = (az + bz + cz) / 3;

        bounds[i * 6] = Math.min(ax, bx, cx);
        bounds[i * 6 + 1] = Math.min(ay, by, cy);
        bounds[i * 6 + 2] = Math.min(az, bz, cz);
        bounds[i * 6 + 3] = Math.max(ax, bx, cx);
        bounds[i * 6 + 4] = Math.max(ay, by, cy);
        bounds[i * 6 + 5] = Math.max(az, bz, cz);
    }

    const world = {
        tris: tri_xyz,
        normals,
        tri_count,
        // BVH, flattened. A leaf has count > 0 and `first` indexes `order`;
        // an interior node has count == 0 and `left` names its first child,
        // the second being left + 1.
        node_min: null, node_max: null, node_left: null,
        node_first: null, node_count: null, node_used: 0,
        order: new Uint32Array(tri_count),
    };

    for (let i = 0; i < tri_count; ++i) world.order[i] = i;

    const max_nodes = Math.max(1, tri_count * 2);
    world.node_min = new Float32Array(max_nodes * 3);
    world.node_max = new Float32Array(max_nodes * 3);
    world.node_left = new Int32Array(max_nodes);
    world.node_first = new Uint32Array(max_nodes);
    world.node_count = new Uint32Array(max_nodes);

    if (tri_count > 0) {
        world.node_used = 1;
        build_node(world, 0, 0, tri_count, bounds, centroids, 0);
    }
    return world;
}

const LEAF_SIZE = 8;
const MAX_DEPTH = 40;

function build_node(w, node, first, count, bounds, centroids, depth) {
    w.node_first[node] = first;
    w.node_count[node] = count;
    w.node_left[node] = -1;

    let mnx = Infinity, mny = Infinity, mnz = Infinity;
    let mxx = -Infinity, mxy = -Infinity, mxz = -Infinity;
    for (let i = first; i < first + count; ++i) {
        const t = w.order[i] * 6;
        if (bounds[t] < mnx) mnx = bounds[t];
        if (bounds[t + 1] < mny) mny = bounds[t + 1];
        if (bounds[t + 2] < mnz) mnz = bounds[t + 2];
        if (bounds[t + 3] > mxx) mxx = bounds[t + 3];
        if (bounds[t + 4] > mxy) mxy = bounds[t + 4];
        if (bounds[t + 5] > mxz) mxz = bounds[t + 5];
    }
    w.node_min[node * 3] = mnx; w.node_min[node * 3 + 1] = mny; w.node_min[node * 3 + 2] = mnz;
    w.node_max[node * 3] = mxx; w.node_max[node * 3 + 1] = mxy; w.node_max[node * 3 + 2] = mxz;

    if (count <= LEAF_SIZE || depth >= MAX_DEPTH) return;

    // Median split on the widest axis. A full SAH build is not worth it: this
    // runs once per level load and the geometry is axis-aligned architecture,
    // which median-splits well.
    const ex = mxx - mnx, ey = mxy - mny, ez = mxz - mnz;
    const axis = ex > ey && ex > ez ? 0 : ey > ez ? 1 : 2;
    const split = (axis === 0 ? mnx + ex * 0.5 : axis === 1 ? mny + ey * 0.5 : mnz + ez * 0.5);

    let i = first, j = first + count - 1;
    while (i <= j) {
        if (centroids[w.order[i] * 3 + axis] < split) ++i;
        else { const t = w.order[i]; w.order[i] = w.order[j]; w.order[j] = t; --j; }
    }

    const left_count = i - first;
    if (left_count === 0 || left_count === count) return;    // degenerate, stay a leaf

    const left = w.node_used;
    w.node_used += 2;
    w.node_left[node] = left;
    w.node_count[node] = 0;

    build_node(w, left, first, left_count, bounds, centroids, depth + 1);
    build_node(w, left + 1, i, count - left_count, bounds, centroids, depth + 1);
}

// --------------------------------------------------------------------- query

const g_hits = new Uint32Array(4096);
const g_stack = new Int32Array(64);

// Triangles whose bounds overlap the box. Returns a count; the indices are in
// the module-level `g_hits`, valid until the next query.
export function coll_query_aabb(w, mnx, mny, mnz, mxx, mxy, mxz) {
    if (w.tri_count === 0) return 0;

    let hit_count = 0;
    let sp = 0;
    g_stack[sp++] = 0;

    while (sp > 0) {
        const n = g_stack[--sp];
        const o = n * 3;
        if (w.node_min[o] > mxx || w.node_max[o] < mnx ||
            w.node_min[o + 1] > mxy || w.node_max[o + 1] < mny ||
            w.node_min[o + 2] > mxz || w.node_max[o + 2] < mnz) continue;

        const count = w.node_count[n];
        if (count > 0) {
            const first = w.node_first[n];
            for (let i = 0; i < count && hit_count < g_hits.length; ++i)
                g_hits[hit_count++] = w.order[first + i];
        } else if (w.node_left[n] >= 0 && sp + 2 <= g_stack.length) {
            g_stack[sp++] = w.node_left[n];
            g_stack[sp++] = w.node_left[n] + 1;
        }
    }
    return hit_count;
}

export const coll_hits = () => g_hits;

// Möller–Trumbore, two-sided: room walls are single-sided sheets and which way
// they face is an authoring accident, not a statement about solidity.
//
// `min_ny` filters *during* traversal rather than after. That distinction is
// the whole point of the parameter: a floor probe that takes the nearest hit
// and then rejects it for facing the wrong way reports "no floor" whenever a
// downward-facing triangle happens to sit above the real one — which in this
// geometry is most doorways and every ceiling beam. The ledge climb refused to
// mantle onto perfectly solid floors for exactly this reason.
export function coll_raycast(w, ox, oy, oz, dx, dy, dz, max_t, min_ny = -2) {
    if (w.tri_count === 0) return null;

    let best_t = max_t;
    let best_tri = -1;

    let sp = 0;
    g_stack[sp++] = 0;
    const inv_x = 1 / (dx || 1e-20), inv_y = 1 / (dy || 1e-20), inv_z = 1 / (dz || 1e-20);

    while (sp > 0) {
        const n = g_stack[--sp];
        const o = n * 3;

        // Slab test against the node.
        let t0 = (w.node_min[o] - ox) * inv_x, t1 = (w.node_max[o] - ox) * inv_x;
        let tmin = Math.min(t0, t1), tmax = Math.max(t0, t1);
        t0 = (w.node_min[o + 1] - oy) * inv_y; t1 = (w.node_max[o + 1] - oy) * inv_y;
        tmin = Math.max(tmin, Math.min(t0, t1)); tmax = Math.min(tmax, Math.max(t0, t1));
        t0 = (w.node_min[o + 2] - oz) * inv_z; t1 = (w.node_max[o + 2] - oz) * inv_z;
        tmin = Math.max(tmin, Math.min(t0, t1)); tmax = Math.min(tmax, Math.max(t0, t1));
        if (tmax < Math.max(tmin, 0) || tmin > best_t) continue;

        const count = w.node_count[n];
        if (count > 0) {
            const first = w.node_first[n];
            for (let i = 0; i < count; ++i) {
                const tri = w.order[first + i];
                if (w.normals[tri * 3 + 1] < min_ny) continue;
                const t = ray_tri(w.tris, tri * 9, ox, oy, oz, dx, dy, dz);
                if (t >= 0 && t < best_t) { best_t = t; best_tri = tri; }
            }
        } else if (w.node_left[n] >= 0 && sp + 2 <= g_stack.length) {
            g_stack[sp++] = w.node_left[n];
            g_stack[sp++] = w.node_left[n] + 1;
        }
    }

    if (best_tri < 0) return null;
    return {
        t: best_t,
        tri: best_tri,
        x: ox + dx * best_t, y: oy + dy * best_t, z: oz + dz * best_t,
        nx: w.normals[best_tri * 3], ny: w.normals[best_tri * 3 + 1], nz: w.normals[best_tri * 3 + 2],
    };
}

function ray_tri(tris, o, ox, oy, oz, dx, dy, dz) {
    const ax = tris[o], ay = tris[o + 1], az = tris[o + 2];
    const e0x = tris[o + 3] - ax, e0y = tris[o + 4] - ay, e0z = tris[o + 5] - az;
    const e1x = tris[o + 6] - ax, e1y = tris[o + 7] - ay, e1z = tris[o + 8] - az;

    const px = dy * e1z - dz * e1y;
    const py = dz * e1x - dx * e1z;
    const pz = dx * e1y - dy * e1x;
    const det = e0x * px + e0y * py + e0z * pz;
    if (Math.abs(det) < 1e-12) return -1;

    const inv = 1 / det;
    const tx = ox - ax, ty = oy - ay, tz = oz - az;
    const u = (tx * px + ty * py + tz * pz) * inv;
    if (u < -1e-6 || u > 1 + 1e-6) return -1;

    const qx = ty * e0z - tz * e0y;
    const qy = tz * e0x - tx * e0z;
    const qz = tx * e0y - ty * e0x;
    const v = (dx * qx + dy * qy + dz * qz) * inv;
    if (v < -1e-6 || u + v > 1 + 1e-6) return -1;

    const t = (e1x * qx + e1y * qy + e1z * qz) * inv;
    return t > 1e-5 ? t : -1;
}

// ----------------------------------------------------- closest-point helpers

const g_cp = new Float32Array(3);

// Ericson, Real-Time Collision Detection §5.1.5. Writes into g_cp.
function closest_pt_triangle(px, py, pz, ax, ay, az, bx, by, bz, cx, cy, cz) {
    const abx = bx - ax, aby = by - ay, abz = bz - az;
    const acx = cx - ax, acy = cy - ay, acz = cz - az;
    const apx = px - ax, apy = py - ay, apz = pz - az;

    const d1 = abx * apx + aby * apy + abz * apz;
    const d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0 && d2 <= 0) { g_cp[0] = ax; g_cp[1] = ay; g_cp[2] = az; return; }

    const bpx = px - bx, bpy = py - by, bpz = pz - bz;
    const d3 = abx * bpx + aby * bpy + abz * bpz;
    const d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0 && d4 <= d3) { g_cp[0] = bx; g_cp[1] = by; g_cp[2] = bz; return; }

    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const v = d1 / (d1 - d3);
        g_cp[0] = ax + abx * v; g_cp[1] = ay + aby * v; g_cp[2] = az + abz * v;
        return;
    }

    const cpx = px - cx, cpy = py - cy, cpz = pz - cz;
    const d5 = abx * cpx + aby * cpy + abz * cpz;
    const d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0 && d5 <= d6) { g_cp[0] = cx; g_cp[1] = cy; g_cp[2] = cz; return; }

    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const w = d2 / (d2 - d6);
        g_cp[0] = ax + acx * w; g_cp[1] = ay + acy * w; g_cp[2] = az + acz * w;
        return;
    }

    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        const w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        g_cp[0] = bx + (cx - bx) * w; g_cp[1] = by + (cy - by) * w; g_cp[2] = bz + (cz - bz) * w;
        return;
    }

    const denom = 1 / (va + vb + vc);
    const v = vb * denom, w = vc * denom;
    g_cp[0] = ax + abx * v + acx * w;
    g_cp[1] = ay + aby * v + acy * w;
    g_cp[2] = az + abz * v + acz * w;
}

// Closest point on segment [ay0, ay1] (vertical, x/z fixed) to a point. The
// capsule axis is always vertical here, which collapses the general
// segment-point case to a clamp.
const clamp_seg = (y, y0, y1) => (y < y0 ? y0 : y > y1 ? y1 : y);

// ------------------------------------------------------------- capsule move
//
// Vertical capsule, `pos` at the feet. The lower sphere centre sits at
// pos.y + STEP_HEIGHT + radius so short obstacles pass underneath; the ground
// probe below is what actually puts the character on top of them.
//
// Discrete depenetration rather than a swept solve: substep short enough that
// nothing is missed, push out of whatever overlaps, then strip the velocity
// component heading into each contact. Swept capsule-vs-soup is more accurate
// and much fussier at the seams between two coplanar triangles, which this
// geometry is entirely made of.

const MAX_CONTACTS = 16;

export function coll_move_capsule(w, out, pos, vel, radius, height, dt) {
    let px = pos[0], py = pos[1], pz = pos[2];
    let vx = vel[0], vy = vel[1], vz = vel[2];

    const seg_lo = STEP_HEIGHT + radius;
    const seg_hi = Math.max(seg_lo + 0.01, height - radius);

    let grounded = false;
    let ground_ny = 0;
    let hit_wall = false;
    let hit_ceiling = false;

    // Substep so a fast fall cannot tunnel through a floor.
    const dist = Math.hypot(vx * dt, vy * dt, vz * dt);
    const steps = Math.min(8, Math.max(1, Math.ceil(dist / (radius * 0.75))));
    const h = dt / steps;

    for (let s = 0; s < steps; ++s) {
        px += vx * h; py += vy * h; pz += vz * h;

        for (let iter = 0; iter < 4; ++iter) {
            const n = coll_query_aabb(
                w,
                px - radius, py + STEP_HEIGHT, pz - radius,
                px + radius, py + height, pz + radius);
            if (n === 0) break;

            let pushed = false;
            for (let i = 0; i < n; ++i) {
                const tri = g_hits[i];
                const o = tri * 9;

                // Closest point on the capsule axis to the triangle's centre,
                // then the closest point on the triangle to that. One
                // refinement pass is enough for the axis-aligned architecture
                // this runs on.
                const gy = clamp_seg(
                    (w.tris[o + 1] + w.tris[o + 4] + w.tris[o + 7]) / 3,
                    py + seg_lo, py + seg_hi);
                closest_pt_triangle(px, gy, pz,
                    w.tris[o], w.tris[o + 1], w.tris[o + 2],
                    w.tris[o + 3], w.tris[o + 4], w.tris[o + 5],
                    w.tris[o + 6], w.tris[o + 7], w.tris[o + 8]);

                const qy = clamp_seg(g_cp[1], py + seg_lo, py + seg_hi);
                let dx = px - g_cp[0], dy = qy - g_cp[1], dz = pz - g_cp[2];
                let d = Math.hypot(dx, dy, dz);

                if (d >= radius) continue;

                if (d < 1e-5) {
                    // Dead centre on the surface: push along the face normal.
                    dx = w.normals[tri * 3]; dy = w.normals[tri * 3 + 1]; dz = w.normals[tri * 3 + 2];
                    d = 1;
                } else {
                    dx /= d; dy /= d; dz /= d;
                }

                const depth = radius - d;
                px += dx * depth; py += dy * depth; pz += dz * depth;
                pushed = true;

                if (dy > GROUND_COS) {
                    grounded = true;
                    if (dy > ground_ny) ground_ny = dy;
                } else if (dy < -0.5) {
                    hit_ceiling = true;
                } else {
                    hit_wall = true;
                }

                // Remove the component of velocity heading into the contact.
                const into = vx * dx + vy * dy + vz * dz;
                if (into < 0) { vx -= dx * into; vy -= dy * into; vz -= dz * into; }
            }
            if (!pushed) break;
        }
    }

    // Ground probe. Two jobs: keep the character attached when running down a
    // slope or a stair (without it every step edge launches a small hop), and
    // report the floor height for the ledge logic.
    let floor_y = -Infinity;
    if (vy <= 0.01) {
        const probe = grounded ? STEP_HEIGHT + 0.05 : 0.02;
        const hit = coll_raycast(w, px, py + STEP_HEIGHT + 0.02, pz, 0, -1, 0,
                                 STEP_HEIGHT + 0.02 + probe, GROUND_COS);
        if (hit) {
            floor_y = hit.y;
            py = hit.y;
            grounded = true;
            if (hit.ny > ground_ny) ground_ny = hit.ny;
            if (vy < 0) vy = 0;
        }
    }

    out.x = px; out.y = py; out.z = pz;
    out.vx = vx; out.vy = vy; out.vz = vz;
    out.grounded = grounded;
    out.ground_ny = ground_ny;
    out.hit_wall = hit_wall;
    out.hit_ceiling = hit_ceiling;
    out.floor_y = floor_y;
    return out;
}

// Height of the floor under a point, or null. Used by the spawn code, which
// has to drop the character onto whatever the .lvl's [Prince] line is above,
// and by the ledge climb to confirm a landing spot.
export function coll_floor_under(w, x, y, z, max_drop = 8) {
    const hit = coll_raycast(w, x, y + 0.2, z, 0, -1, 0, max_drop + 0.2, GROUND_COS);
    return hit ? hit.y : null;
}

// Is there room for a standing capsule here? The ledge climb uses it to refuse
// a mantle into a space too low to stand in.
export function coll_capsule_free(w, x, y, z, radius, height) {
    const n = coll_query_aabb(w, x - radius, y + 0.05, z - radius,
                                 x + radius, y + height, z + radius);
    for (let i = 0; i < n; ++i) {
        const tri = g_hits[i];
        const o = tri * 9;
        const gy = clamp_seg((w.tris[o + 1] + w.tris[o + 4] + w.tris[o + 7]) / 3,
                             y + radius, y + Math.max(radius + 0.01, height - radius));
        closest_pt_triangle(x, gy, z,
            w.tris[o], w.tris[o + 1], w.tris[o + 2],
            w.tris[o + 3], w.tris[o + 4], w.tris[o + 5],
            w.tris[o + 6], w.tris[o + 7], w.tris[o + 8]);
        const qy = clamp_seg(g_cp[1], y + radius, y + Math.max(radius + 0.01, height - radius));
        if (Math.hypot(x - g_cp[0], qy - g_cp[1], z - g_cp[2]) < radius * 0.9) return false;
    }
    return true;
}
