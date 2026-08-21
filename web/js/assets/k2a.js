// k2a.js — parser for the K2 ASCII geometry export (.K2A).
//
// Port of src/assets/k2a.cpp, which was validated on all 501 .K2A files in the
// install. The authoritative grammar is the 77-entry keyword table at 0x7BCAF0
// in pop3dreal.exe; see docs/formats/k2a.md. Behaviour is kept identical on
// purpose — where this file deliberately diverges it says so.
//
// No dependency on gfx: the asset layer stays usable as a standalone converter.

export const K2A_ALPHA_NONE = 0;
export const K2A_ALPHA_OPACITY = 1;

// ------------------------------------------------------------------ tokenizer
//
// Scans by char code and slices only when a token is actually produced. The
// biggest shipped room is a few MB of text, so this runs in tens of
// milliseconds — no need for anything cleverer.

function tok_new(text) {
    return { s: text, n: text.length, p: 0, tok: '', quoted: false };
}

function tok_next(t) {
    const s = t.s, n = t.n;
    let p = t.p;
    t.quoted = false;

    while (p < n) {
        const c = s.charCodeAt(p);
        if (c === 32 || c === 9 || c === 13 || c === 10) ++p; else break;
    }
    if (p >= n) { t.p = p; t.tok = ''; return false; }

    const c = s.charCodeAt(p);

    if (c === 34) {                       // '"'
        const start = ++p;
        while (p < n && s.charCodeAt(p) !== 34) ++p;
        t.tok = s.slice(start, p);
        if (p < n) ++p;
        t.p = p;
        t.quoted = true;
        return true;
    }

    if (c === 123 || c === 125) {         // '{' '}'
        t.tok = s[p];
        t.p = p + 1;
        return true;
    }

    const start = p;
    while (p < n) {
        const d = s.charCodeAt(p);
        if (d === 32 || d === 9 || d === 13 || d === 10 ||
            d === 123 || d === 125 || d === 34) break;
        ++p;
    }
    t.tok = s.slice(start, p);
    t.p = p;
    return true;
}

function tok_peek(t) {
    const p = t.p, tok = t.tok, q = t.quoted;
    const ok = tok_next(t);
    const out = ok ? t.tok : '';
    t.p = p; t.tok = tok; t.quoted = q;
    return out;
}

function tok_read3(t) {
    const v = [0, 0, 0];
    for (let i = 0; i < 3; ++i) { tok_next(t); v[i] = to_float(t.tok); }
    return v;
}

// "12:" and "12" both yield 12 — parseInt stops at the colon, which is what the
// *MESH_FACE "0: A: 2 B: 0 C: 1" form needs.
const to_int = (s) => { const v = parseInt(s, 10); return Number.isNaN(v) ? 0 : v; };
const to_float = (s) => { const v = parseFloat(s); return Number.isNaN(v) ? 0 : v; };

function skip_block(t) {
    if (tok_peek(t) !== '{') return;
    tok_next(t);
    let depth = 1;
    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '{') ++depth;
        else if (!t.quoted && t.tok === '}') --depth;
    }
}

// ------------------------------------------------------------------- sections

// A material body contains nested *MAP_<CHANNEL> blocks, each with its own
// *BITMAP and *ALPHA_SOURCE, so both have to be read per channel:
//
//   * `bitmap` must come from *MAP_DIFFUSE. Taking the last *BITMAP seen picks
//     up the opacity or specular map whenever a material has one.
//   * transparency is signalled by the presence of a *MAP_OPACITY block (or a
//     non-zero *MATERIAL_TRANSPARENCY), NOT by *ALPHA_SOURCE. prison3 has 105
//     *ALPHA_SOURCE Opacity lines and zero *MAP_OPACITY blocks; keying on
//     ALPHA_SOURCE flagged 105 of its 131 materials transparent and destroyed
//     the room's draw order.
function parse_material(t) {
    const m = { name: '', bitmap: '', alpha: K2A_ALPHA_NONE };
    let depth = 1;                    // '{' already consumed
    let pending = '';                 // last *MAP_* token seen
    let channel = '';
    let channel_depth = -1;
    let have_diffuse = false;

    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '{') {
            ++depth;
            if (pending.startsWith('*MAP_')) {
                channel = pending;
                channel_depth = depth;
                if (channel === '*MAP_OPACITY') m.alpha = K2A_ALPHA_OPACITY;
            }
            pending = '';
            continue;
        }
        if (!t.quoted && t.tok === '}') {
            if (depth === channel_depth) { channel = ''; channel_depth = -1; }
            --depth;
            pending = '';
            continue;
        }
        if (t.quoted) { pending = ''; continue; }

        if (t.tok === '*MATERIAL_NAME') { tok_next(t); m.name = t.tok; }
        else if (t.tok === '*MATERIAL_TRANSPARENCY') {
            tok_next(t);
            if (to_float(t.tok) > 0) m.alpha = K2A_ALPHA_OPACITY;
        } else if (t.tok === '*BITMAP') {
            tok_next(t);
            if (channel === '*MAP_DIFFUSE') { m.bitmap = t.tok; have_diffuse = true; }
            else if (!have_diffuse && !m.bitmap) m.bitmap = t.tok;
        } else {
            pending = t.tok;
            continue;
        }
        pending = '';
    }
    return m;
}

function parse_material_list(t, out) {
    let depth = 1;
    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '}') { --depth; continue; }
        if (t.quoted) continue;

        if (t.tok === '*MATERIAL') {
            tok_next(t);
            const idx = to_int(t.tok);
            tok_next(t);
            if (t.tok !== '{') continue;
            const m = parse_material(t);
            if (idx >= 0) {
                while (out.length <= idx)
                    out.push({ name: '', bitmap: '', alpha: K2A_ALPHA_NONE });
                out[idx] = m;
            }
        } else if (t.tok === '{') {
            ++depth;
        }
    }
}

function parse_node_tm(t, n) {
    let depth = 1;
    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '{') { ++depth; continue; }
        if (!t.quoted && t.tok === '}') { --depth; continue; }
        if (t.quoted) continue;

        // TM_ROW0..3 are deliberately ignored: the engine cannot read them
        // either (its keyword table misspells row 0 as TM_ROWO and has no
        // row 3), so the decomposed form is the authoritative one.
        if (t.tok === '*TM_POS') n.pos = tok_read3(t);
        else if (t.tok === '*TM_ROTAXIS') n.rot_axis = tok_read3(t);
        else if (t.tok === '*TM_SCALE') n.scale = tok_read3(t);
        else if (t.tok === '*TM_ROTANGLE') { tok_next(t); n.rot_angle = to_float(t.tok); }
    }
}

function parse_mesh(t, m) {
    let depth = 1;
    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '{') { ++depth; continue; }
        if (!t.quoted && t.tok === '}') { --depth; continue; }
        if (t.quoted) continue;

        switch (t.tok) {
            case '*MESH_VERTEX': {
                tok_next(t);                                // index
                const v = tok_read3(t);
                m.verts.push(v[0], v[1], v[2]);
                break;
            }
            case '*MESH_FACE': {
                tok_next(t);                                // "0:"
                for (let i = 0; i < 3; ++i) {
                    tok_next(t);                            // "A:" / "B:" / "C:"
                    tok_next(t);                            // value
                    m.faces.push(to_int(t.tok));
                }
                break;
            }
            case '*MESH_TVERT': {
                tok_next(t);
                const v = tok_read3(t);
                m.tverts.push(v[0], v[1], v[2]);
                break;
            }
            case '*MESH_TFACE': {
                tok_next(t);
                for (let i = 0; i < 3; ++i) { tok_next(t); m.tfaces.push(to_int(t.tok)); }
                break;
            }
            case '*MESH_VERTCOL': {
                tok_next(t);
                const v = tok_read3(t);
                m.cverts.push(v[0], v[1], v[2]);
                break;
            }
            case '*MESH_CFACE': {
                tok_next(t);
                for (let i = 0; i < 3; ++i) { tok_next(t); m.cfaces.push(to_int(t.tok)); }
                break;
            }
            default: break;
        }
    }
}

function node_new() {
    return {
        name: '', parent: 'ROOT',
        pos: [0, 0, 0], rot_axis: [0, 0, 0], rot_angle: 0, scale: [1, 1, 1],
        material_ref: -1,
        has_mesh: false,
        mesh: { verts: [], faces: [], tverts: [], tfaces: [], cverts: [], cfaces: [] },
        has_trap: false,
        trap: {
            name: '', id: -1, center: [0, 0, 0], bbox: [0, 0, 0, 0, 0, 0],
            pivot: [0, 0, 0], pivot_axis: [0, 0, 0], pivot_angle: 0,
            argv: [], comment: '',
        },
    };
}

function parse_object(t, n) {
    let depth = 1;                       // '{' already consumed
    while (depth > 0 && tok_next(t)) {
        if (!t.quoted && t.tok === '}') { --depth; continue; }
        if (t.quoted) continue;

        const tok = t.tok;
        if (tok === '*NODE_NAME') { tok_next(t); n.name = t.tok; }
        else if (tok === '*NODE_PARENT') { tok_next(t); n.parent = t.tok; }
        else if (tok === '*MATERIAL_REF') { tok_next(t); n.material_ref = to_int(t.tok); }
        else if (tok === '*NODE_TM') { tok_next(t); if (t.tok === '{') parse_node_tm(t, n); }
        else if (tok === '*MESH') {
            tok_next(t);
            if (t.tok === '{') { n.has_mesh = true; parse_mesh(t, n.mesh); }
        }
        // --- trap block: flat fields, no braces
        else if (tok === '*K2_TRAP_BLOCK') n.has_trap = true;
        else if (tok === '*K2_TRAP_CENTER') n.trap.center = tok_read3(t);
        else if (tok === '*K2_TRAP_PIVOT') n.trap.pivot = tok_read3(t);
        else if (tok === '*K2_TRAP_PIVOT_AXIS') n.trap.pivot_axis = tok_read3(t);
        else if (tok === '*K2_TRAP_PIVOT_ANGLE') { tok_next(t); n.trap.pivot_angle = to_float(t.tok); }
        else if (tok === '*K2_TRAP_BBOX') {
            for (let i = 0; i < 6; ++i) { tok_next(t); n.trap.bbox[i] = to_float(t.tok); }
        } else if (tok === '*K2_TRAP_NAME') { tok_next(t); n.trap.name = t.tok; }
        else if (tok === '*K2_TRAP_ID') { tok_next(t); n.trap.id = to_int(t.tok); }
        else if (tok === '*K2_TRAP_COMMENT') { tok_next(t); n.trap.comment = t.tok; }
        else if (tok === '*K2_TRAP_ARGC') { tok_next(t); }
        else if (tok.startsWith('*K2_TRAP_ARGV')) {
            // The slot index is appended to the token: *K2_TRAP_ARGV0, ARGV1, ...
            const slot = to_int(tok.slice(13));
            tok_next(t);
            if (slot >= 0) {
                while (n.trap.argv.length <= slot) n.trap.argv.push('');
                n.trap.argv[slot] = t.tok;
            }
        } else if (tok === '{') ++depth;
    }
}

// ----------------------------------------------------------------- entry point

export function k2a_parse(text) {
    const out = { version: 0, materials: [], nodes: [] };

    if (!text || text.length < 16) throw new Error('empty or too small');
    if (!text.startsWith('*K2_ASCIIEXPORT')) throw new Error('missing *K2_ASCIIEXPORT header');

    const t = tok_new(text);
    while (tok_next(t)) {
        if (t.quoted) continue;

        if (t.tok === '*K2_ASCIIEXPORT') {
            tok_next(t);
            out.version = to_int(t.tok);
        } else if (t.tok === '*MATERIAL_LIST') {
            tok_next(t);
            if (t.tok === '{') parse_material_list(t, out.materials);
        } else if (t.tok === '*OBJECT') {
            tok_next(t);                    // "GEOM" / "SHAPE" / "HELPER" / "CAMERA"
            tok_next(t);
            if (t.tok !== '{') continue;
            const n = node_new();
            parse_object(t, n);
            out.nodes.push(n);
        } else if (t.tok === '*SCENE' || t.tok === '*GROUP') {
            skip_block(t);
        }
    }
    return out;
}

// ------------------------------------------------------------------ mesh build
//
// .K2A uses separate index streams for position, UV and colour, so one GPU
// vertex is a unique (pos, uv, colour) triple. Corners are welded where all
// three agree. Normals are generated per face — the format carries none.
//
// Coordinates need no conversion: the K2 exporter already emits engine space
// (Y-up), verified against savegame actor positions. See docs/formats/k2a.md.
//
// Two deliberate divergences from k2a.cpp:
//
//   * Indices are 32-bit. The native path caps at 0xFFFF and silently drops the
//     tail of any mesh that exceeds it; WebGPU takes uint32 indices at no cost,
//     so the cap is gone.
//   * The weld key is exact (a two-level Map) rather than a 64-bit XOR hash.
//     The hash can collide, which merges two corners that should stay distinct.

export const K2A_VERTEX_STRIDE = 36;   // 3f pos + 3f normal + 2f uv + 4x u8 colour

export function k2a_build_mesh(node) {
    const m = node.mesh;
    const faces = (m.faces.length / 3) | 0;
    if (faces <= 0) return null;

    const have_uv = m.tfaces.length >= faces * 3;
    const have_col = m.cfaces.length >= faces * 3;
    const vert_cap = m.verts.length / 3;

    // Upper bound: every corner unique. Trimmed at the end.
    const buf = new ArrayBuffer(faces * 3 * K2A_VERTEX_STRIDE);
    const f32 = new Float32Array(buf);
    const u32 = new Uint32Array(buf);
    const indices = new Uint32Array(faces * 3);

    const weld = new Map();          // vi -> Map(uv/colour key -> vertex index)
    let vert_count = 0;
    let index_count = 0;

    let min_x = Infinity, min_y = Infinity, min_z = Infinity;
    let max_x = -Infinity, max_y = -Infinity, max_z = -Infinity;

    for (let f = 0; f < faces; ++f) {
        const ia = m.faces[f * 3], ib = m.faces[f * 3 + 1], ic = m.faces[f * 3 + 2];

        // Face normal, since the format carries none.
        let nx = 0, ny = 1, nz = 0;
        if (ia >= 0 && ib >= 0 && ic >= 0 &&
            ia < vert_cap && ib < vert_cap && ic < vert_cap) {
            const ax = m.verts[ia * 3], ay = m.verts[ia * 3 + 1], az = m.verts[ia * 3 + 2];
            const e0x = m.verts[ib * 3] - ax;
            const e0y = m.verts[ib * 3 + 1] - ay;
            const e0z = m.verts[ib * 3 + 2] - az;
            const e1x = m.verts[ic * 3] - ax;
            const e1y = m.verts[ic * 3 + 1] - ay;
            const e1z = m.verts[ic * 3 + 2] - az;
            const cx = e0y * e1z - e0z * e1y;
            const cy = e0z * e1x - e0x * e1z;
            const cz = e0x * e1y - e0y * e1x;
            const len = Math.hypot(cx, cy, cz);
            if (len > 1e-8) { nx = cx / len; ny = cy / len; nz = cz / len; }
        }

        for (let k = 0; k < 3; ++k) {
            const vi = m.faces[f * 3 + k];
            if (vi < 0 || vi >= vert_cap) continue;
            const ti = have_uv ? m.tfaces[f * 3 + k] : -1;
            const ci = have_col ? m.cfaces[f * 3 + k] : -1;

            let inner = weld.get(vi);
            if (inner === undefined) { inner = new Map(); weld.set(vi, inner); }
            const key = (ti + 1) * 1048576 + (ci + 1);
            const found = inner.get(key);
            if (found !== undefined) { indices[index_count++] = found; continue; }

            const o = vert_count * 9;
            const x = m.verts[vi * 3], y = m.verts[vi * 3 + 1], z = m.verts[vi * 3 + 2];
            f32[o] = x; f32[o + 1] = y; f32[o + 2] = z;
            f32[o + 3] = nx; f32[o + 4] = ny; f32[o + 5] = nz;

            let u = 0, v = 0;
            if (ti >= 0 && ti * 3 + 1 < m.tverts.length) {
                u = m.tverts[ti * 3];
                // V needs two corrections, which compose to (1 + stored):
                //   1. the exporter stores V negated (U lies in [0,1], V in
                //      [-1,0] across the shipped geometry), so true = -stored;
                //   2. Max puts V=0 at the bottom of the image while decoded
                //      textures are top-left origin, so the axis inverts again.
                // v = 1 - (-stored) = 1 + stored. Written as one expression on
                // purpose: for tiled UVs (prison1 spans V -20.6..12.4) the two
                // half-fixes are not congruent modulo 1, so repeat addressing
                // does not hide a partial correction.
                v = 1 + m.tverts[ti * 3 + 1];
            }
            f32[o + 6] = u; f32[o + 7] = v;

            let r = 255, g = 255, b = 255;
            if (ci >= 0 && ci * 3 + 2 < m.cverts.length) {
                r = clamp_255(m.cverts[ci * 3] * 255 + 0.5);
                g = clamp_255(m.cverts[ci * 3 + 1] * 255 + 0.5);
                b = clamp_255(m.cverts[ci * 3 + 2] * 255 + 0.5);
            }
            u32[o + 8] = (255 << 24) | (b << 16) | (g << 8) | r;

            if (x < min_x) min_x = x; if (x > max_x) max_x = x;
            if (y < min_y) min_y = y; if (y > max_y) max_y = y;
            if (z < min_z) min_z = z; if (z > max_z) max_z = z;

            inner.set(key, vert_count);
            indices[index_count++] = vert_count;
            ++vert_count;
        }
    }

    if (vert_count === 0 || index_count === 0) return null;

    return {
        vertices: new Uint8Array(buf, 0, vert_count * K2A_VERTEX_STRIDE),
        indices: indices.subarray(0, index_count),
        vert_count,
        index_count,
        min: [min_x, min_y, min_z],
        max: [max_x, max_y, max_z],
    };
}

const clamp_255 = (x) => (x < 0 ? 0 : x > 255 ? 255 : x | 0);
