// math.js — the small amount of linear algebra this engine needs.
//
// Matrices are column-major Float32Array(16), which is what WGSL's mat4x4<f32>
// expects in a uniform buffer, so a matrix can be written straight into a UBO
// with no transpose.
//
// One thing differs from the native GL backend: WebGPU's clip space has
// z in [0, 1], not [-1, 1]. `m_perspective` below is the zero-to-one form;
// using the GL form gives a depth buffer that only ever fills half its range
// and z-fights badly at distance.
//
// Naming follows the C++ side: snake_case functions, no allocation in the hot
// path — every function writes into a caller-owned `o`.

export const DEG = Math.PI / 180;

// ------------------------------------------------------------------- vec3

export const v3 = (x = 0, y = 0, z = 0) => new Float32Array([x, y, z]);

export function v3_set(o, x, y, z) { o[0] = x; o[1] = y; o[2] = z; return o; }
export function v3_copy(o, a) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; return o; }
export function v3_add(o, a, b) { o[0] = a[0] + b[0]; o[1] = a[1] + b[1]; o[2] = a[2] + b[2]; return o; }
export function v3_sub(o, a, b) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; return o; }
export function v3_scale(o, a, s) { o[0] = a[0] * s; o[1] = a[1] * s; o[2] = a[2] * s; return o; }

export function v3_add_scaled(o, a, b, s) {
    o[0] = a[0] + b[0] * s; o[1] = a[1] + b[1] * s; o[2] = a[2] + b[2] * s; return o;
}

export const v3_dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const v3_len = (a) => Math.hypot(a[0], a[1], a[2]);
export const v3_len2 = (a) => a[0] * a[0] + a[1] * a[1] + a[2] * a[2];

export const v3_dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);

export function v3_cross(o, a, b) {
    const x = a[1] * b[2] - a[2] * b[1];
    const y = a[2] * b[0] - a[0] * b[2];
    const z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
    return o;
}

export function v3_normalize(o, a) {
    const l = Math.hypot(a[0], a[1], a[2]);
    if (l > 1e-8) { o[0] = a[0] / l; o[1] = a[1] / l; o[2] = a[2] / l; }
    else { o[0] = 0; o[1] = 0; o[2] = 0; }
    return o;
}

export function v3_lerp(o, a, b, t) {
    o[0] = a[0] + (b[0] - a[0]) * t;
    o[1] = a[1] + (b[1] - a[1]) * t;
    o[2] = a[2] + (b[2] - a[2]) * t;
    return o;
}

// ------------------------------------------------------------------- mat4

export function mat4() {
    const m = new Float32Array(16);
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}

export function m_identity(m) {
    m.fill(0);
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}

// o = a * b, both column-major.
export function m_mul(o, a, b) {
    for (let c = 0; c < 4; ++c) {
        const b0 = b[c * 4], b1 = b[c * 4 + 1], b2 = b[c * 4 + 2], b3 = b[c * 4 + 3];
        o[c * 4 + 0] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
        o[c * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
        o[c * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
        o[c * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
    }
    return o;
}

// Zero-to-one depth range. See the header note.
export function m_perspective(m, fov_y, aspect, near, far) {
    const f = 1 / Math.tan(fov_y * 0.5);
    m.fill(0);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = far / (near - far);
    m[11] = -1;
    m[14] = (near * far) / (near - far);
    return m;
}

export function m_look_at(m, eye, at, up) {
    let fx = at[0] - eye[0], fy = at[1] - eye[1], fz = at[2] - eye[2];
    let l = Math.hypot(fx, fy, fz);
    if (l < 1e-8) { fx = 0; fy = 0; fz = -1; l = 1; }
    fx /= l; fy /= l; fz /= l;

    let sx = fy * up[2] - fz * up[1];
    let sy = fz * up[0] - fx * up[2];
    let sz = fx * up[1] - fy * up[0];
    l = Math.hypot(sx, sy, sz);
    if (l < 1e-8) { sx = 1; sy = 0; sz = 0; l = 1; }   // looking straight up/down
    sx /= l; sy /= l; sz /= l;

    const ux = sy * fz - sz * fy;
    const uy = sz * fx - sx * fz;
    const uz = sx * fy - sy * fx;

    m[0] = sx; m[4] = sy; m[8] = sz; m[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
    m[1] = ux; m[5] = uy; m[9] = uz; m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] = fx * eye[0] + fy * eye[1] + fz * eye[2];
    m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
    return m;
}

// Translation * rotation-about-Y * uniform scale, the only composition the
// gameplay layer builds.
export function m_trs_y(m, pos, yaw, scale) {
    const c = Math.cos(yaw), s = Math.sin(yaw);
    m[0] = c * scale; m[1] = 0; m[2] = -s * scale; m[3] = 0;
    m[4] = 0; m[5] = scale; m[6] = 0; m[7] = 0;
    m[8] = s * scale; m[9] = 0; m[10] = c * scale; m[11] = 0;
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; m[15] = 1;
    return m;
}

// .K2A stores rotation decomposed as axis + angle (*TM_ROTAXIS / *TM_ROTANGLE),
// because the engine's keyword table cannot read the row-matrix form — it
// misspells row 0 as TM_ROWO and lacks row 3 entirely. See docs/formats/k2a.md.
export function m_compose_trs(m, pos, axis, angle, scale) {
    const l = Math.hypot(axis[0], axis[1], axis[2]);
    let r = [1, 0, 0, 0, 1, 0, 0, 0, 1];
    if (l > 1e-8 && Math.abs(angle) > 1e-8) {
        const x = axis[0] / l, y = axis[1] / l, z = axis[2] / l;
        const c = Math.cos(angle), s = Math.sin(angle), t = 1 - c;
        r = [
            t * x * x + c, t * x * y + s * z, t * x * z - s * y,
            t * x * y - s * z, t * y * y + c, t * y * z + s * x,
            t * x * z + s * y, t * y * z - s * x, t * z * z + c,
        ];
    }
    m[0] = r[0] * scale[0]; m[1] = r[1] * scale[0]; m[2] = r[2] * scale[0]; m[3] = 0;
    m[4] = r[3] * scale[1]; m[5] = r[4] * scale[1]; m[6] = r[5] * scale[1]; m[7] = 0;
    m[8] = r[6] * scale[2]; m[9] = r[7] * scale[2]; m[10] = r[8] * scale[2]; m[11] = 0;
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2]; m[15] = 1;
    return m;
}

// ----------------------------------------------------------------- scalars

export const clamp = (x, lo, hi) => (x < lo ? lo : x > hi ? hi : x);
export const lerp = (a, b, t) => a + (b - a) * t;

// Frame-rate independent exponential smoothing. `rate` is roughly the fraction
// of the remaining distance covered per second; the usual `lerp(a, b, k)` in an
// update loop is only correct at one timestep.
export const damp = (a, b, rate, dt) => b + (a - b) * Math.exp(-rate * dt);

// Shortest signed difference between two angles, in (-pi, pi].
export function angle_delta(from, to) {
    let d = (to - from) % (Math.PI * 2);
    if (d > Math.PI) d -= Math.PI * 2;
    if (d < -Math.PI) d += Math.PI * 2;
    return d;
}

export function move_toward_angle(from, to, max_step) {
    const d = angle_delta(from, to);
    if (Math.abs(d) <= max_step) return to;
    return from + Math.sign(d) * max_step;
}

export function move_toward(from, to, max_step) {
    const d = to - from;
    if (Math.abs(d) <= max_step) return to;
    return from + Math.sign(d) * max_step;
}
