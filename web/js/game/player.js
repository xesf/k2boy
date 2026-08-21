// player.js — the character controller.
//
// This is the part with no original to port. `CharacterStateTable` is where the
// retail game's movement lives, and it is 20% reversed (docs/reference/
// coverage.md); `MfLocoPlanner`, the middleware underneath it, is at 0%. So
// rather than guess at PoP3D's state table, this implements a controller that
// works now and can be swapped for the real one later — the interface it needs
// from the rest of the engine is just "move a capsule and tell me what it hit".
//
// Where it deliberately departs from 1999:
//
//   * Analog, camera-relative movement instead of turn-then-walk. The original
//     inherited its tank controls from the 2D games' grid.
//   * Coyote time and a jump buffer. Both are invisible when they work; both
//     remove most of the "I pressed jump and he walked off the edge" failures
//     that 3D platformers of this era are full of.
//   * Ledges are geometric, not authored — see ledges.js.
//   * A hard landing costs control, not a life. With rewind available, death
//     as a teaching tool matters much less.
//
// The step is fixed-rate and depends on nothing but its arguments and the
// world, which is what makes rewind (rewind.js) exact rather than approximate.

import { clamp, damp, angle_delta } from '../core/math.js';
import { coll_move_capsule, coll_capsule_free, coll_floor_under } from './collision.js';
import { ledge_find } from './ledges.js';

export const PLAYER_GROUND = 0;
export const PLAYER_AIR = 1;
export const PLAYER_HANG = 2;
export const PLAYER_CLIMB = 3;

export const PLAYER_STATE_NAMES = ['ground', 'air', 'hang', 'climb'];

// Metres and seconds. The install's units are metres: prisonfix sets its far
// clip to 50 for a dungeon, and the [Prince] start heights are centimetre
// fractions (y = 0.01), both of which only make sense at that scale.
export const P = {
    radius: 0.32,
    height: 1.75,

    walk_speed: 1.7,
    run_speed: 4.8,
    accel: 30,
    decel: 38,
    air_accel: 14,
    air_drag: 0.8,

    gravity: 19,
    // A platformer jump is not a ballistic one. 1.2 m of clearance at this
    // gravity is the height that makes the dungeon's crate-and-ledge spacing
    // readable without turning the character into a grasshopper.
    jump_speed: Math.sqrt(2 * 19 * 1.2),
    jump_cut: 0.42,               // releasing jump early trims the arc
    terminal: 24,

    turn_rate: 14,                // rad/s toward the movement direction
    coyote: 0.12,
    jump_buffer: 0.15,

    grab_reach: 0.62,
    grab_up_min: 0.85,
    grab_up_max: 2.15,
    hang_drop: 1.48,              // feet below the lip while hanging
    shimmy_speed: 1.5,
    climb_time: 0.42,

    hard_landing_speed: 11,       // below this, landing is free
    stagger_time: 0.45,
};

export function player_new(x, y, z, yaw) {
    return {
        x, y, z,
        vx: 0, vy: 0, vz: 0,
        yaw,
        state: PLAYER_AIR,

        grounded: false,
        speed: 0,
        coyote_t: 0,
        buffer_t: 0,
        stagger_t: 0,
        air_time: 0,
        fall_peak: y,

        ledge: null,              // {x,y,z,nx,nz,index} while hanging
        climb_t: 0,
        climb_from: null,
        climb_to: null,

        spawn: { x, y, z, yaw },
        landed_hard: false,       // one-frame event flag, for audio/HUD later
        climb_block: '',          // why the last mantle attempt was refused
    };
}

export function player_respawn(pl) {
    pl.x = pl.spawn.x; pl.y = pl.spawn.y; pl.z = pl.spawn.z;
    pl.vx = pl.vy = pl.vz = 0;
    pl.yaw = pl.spawn.yaw;
    pl.state = PLAYER_AIR;
    pl.ledge = null;
    pl.stagger_t = 0;
}

// Snapshot / restore, used by rewind and by the level loader. Keep in sync with
// the fields the step actually reads.
export const PLAYER_SNAPSHOT_FLOATS = 12;

export function player_save(pl, out, o) {
    out[o] = pl.x; out[o + 1] = pl.y; out[o + 2] = pl.z;
    out[o + 3] = pl.vx; out[o + 4] = pl.vy; out[o + 5] = pl.vz;
    out[o + 6] = pl.yaw;
    out[o + 7] = pl.state;
    out[o + 8] = pl.coyote_t;
    out[o + 9] = pl.climb_t;
    out[o + 10] = pl.stagger_t;
    out[o + 11] = pl.ledge ? pl.ledge.index : -1;
}

export function player_load(pl, src, o, lg) {
    pl.x = src[o]; pl.y = src[o + 1]; pl.z = src[o + 2];
    pl.vx = src[o + 3]; pl.vy = src[o + 4]; pl.vz = src[o + 5];
    pl.yaw = src[o + 6];
    pl.state = src[o + 7];
    pl.coyote_t = src[o + 8];
    pl.climb_t = src[o + 9];
    pl.stagger_t = src[o + 10];

    const li = src[o + 11];
    if (li >= 0 && lg && li < lg.count) {
        const s = li * 8;
        pl.ledge = {
            x: pl.x, y: pl.y + P.hang_drop, z: pl.z,
            nx: lg.segs[s + 6], nz: lg.segs[s + 7], index: li,
        };
    } else {
        pl.ledge = null;
    }
}

// ---------------------------------------------------------------------- step

const g_move = {
    x: 0, y: 0, z: 0, vx: 0, vy: 0, vz: 0,
    grounded: false, ground_ny: 0, hit_wall: false, hit_ceiling: false, floor_y: 0,
};

// `inp` is an input_sample(); `cam_yaw` orients the stick.
export function player_step(pl, world, lg, inp, cam_yaw, dt) {
    pl.landed_hard = false;

    // Camera-relative stick. Screen-up is away from the camera.
    const cs = Math.cos(cam_yaw), sn = Math.sin(cam_yaw);
    let wish_x = inp.move_x * cs - inp.move_z * sn;
    let wish_z = inp.move_x * sn + inp.move_z * cs;
    const wish_mag = Math.hypot(wish_x, wish_z);
    if (wish_mag > 1e-4) { wish_x /= wish_mag; wish_z /= wish_mag; }

    // Jump buffer: an edge is remembered for a moment so a press just before
    // landing still fires. The edge itself comes from input_sample and is only
    // cleared once the simulation has stepped, so nothing is lost between
    // display frames either.
    pl.buffer_t = inp.jump_edge ? P.jump_buffer : Math.max(0, pl.buffer_t - dt);
    pl.stagger_t = Math.max(0, pl.stagger_t - dt);

    switch (pl.state) {
        case PLAYER_HANG: step_hang(pl, world, lg, inp, wish_x, wish_z, wish_mag, dt); break;
        case PLAYER_CLIMB: step_climb(pl, dt); break;
        default: step_move(pl, world, lg, inp, wish_x, wish_z, wish_mag, dt); break;
    }

    pl.speed = Math.hypot(pl.vx, pl.vz);
    return pl;
}

function step_move(pl, world, lg, inp, wish_x, wish_z, wish_mag, dt) {
    const target = (inp.walk ? P.walk_speed : P.run_speed) * Math.min(1, wish_mag) *
                   (pl.stagger_t > 0 ? 0.35 : 1);

    if (pl.grounded) {
        // Ground control: accelerate toward the wish velocity, decelerate hard
        // when the stick is released. Two rates rather than one because a
        // shared value makes the character either slippery or unresponsive.
        const want_x = wish_x * target, want_z = wish_z * target;
        const rate = wish_mag > 0.01 ? P.accel : P.decel;
        pl.vx = approach(pl.vx, want_x, rate * dt);
        pl.vz = approach(pl.vz, want_z, rate * dt);
    } else if (wish_mag > 0.01) {
        pl.vx = approach(pl.vx, wish_x * target, P.air_accel * dt);
        pl.vz = approach(pl.vz, wish_z * target, P.air_accel * dt);
    } else {
        const k = Math.exp(-P.air_drag * dt);
        pl.vx *= k; pl.vz *= k;
    }

    // Face the direction of travel. Turning is rate-limited rather than
    // instant so a flick of the stick does not teleport the character's facing.
    if (wish_mag > 0.05) {
        const want = Math.atan2(wish_x, wish_z);
        pl.yaw += angle_delta(pl.yaw, want) * (1 - Math.exp(-P.turn_rate * dt));
    }

    // --- jump ---------------------------------------------------------------
    const can_jump = pl.grounded || pl.coyote_t > 0;
    if (pl.buffer_t > 0 && can_jump && pl.stagger_t <= 0) {
        pl.vy = P.jump_speed;
        pl.grounded = false;
        pl.coyote_t = 0;
        pl.buffer_t = 0;
        pl.state = PLAYER_AIR;
    }

    // Variable height: cutting the jump short is the difference between one
    // jump arc and a controllable set of them.
    if (!inp.jump && pl.vy > 0) pl.vy *= Math.exp(-(1 / Math.max(P.jump_cut, 1e-3)) * dt * 2);

    pl.vy = Math.max(-P.terminal, pl.vy - P.gravity * dt);

    // --- move ---------------------------------------------------------------
    const was_grounded = pl.grounded;
    coll_move_capsule(world, g_move,
        [pl.x, pl.y, pl.z], [pl.vx, pl.vy, pl.vz], P.radius, P.height, dt);

    pl.x = g_move.x; pl.y = g_move.y; pl.z = g_move.z;
    pl.vx = g_move.vx; pl.vy = g_move.vy; pl.vz = g_move.vz;

    if (g_move.hit_ceiling && pl.vy > 0) pl.vy = 0;

    // --- ledge grab ---------------------------------------------------------
    //
    // Only while airborne and not rising fast: grabbing on the way up turns
    // every jump under an overhang into an unintended mantle.
    if (!g_move.grounded && pl.vy < 1.6) {
        const face_x = Math.sin(pl.yaw), face_z = Math.cos(pl.yaw);
        const hit = ledge_find(lg, pl.x, pl.y, pl.z, face_x, face_z,
                               P.grab_reach, P.grab_up_min, P.grab_up_max);
        if (hit) {
            enter_hang(pl, hit);
            return;
        }
    }

    // --- landing ------------------------------------------------------------
    if (g_move.grounded) {
        if (!was_grounded) {
            const impact = pl.fall_peak - pl.y;
            const speed = Math.abs(pl.vy);
            if (speed > P.hard_landing_speed || impact > 4.5) {
                pl.stagger_t = P.stagger_time;
                pl.landed_hard = true;
            }
        }
        pl.grounded = true;
        pl.state = PLAYER_GROUND;
        pl.coyote_t = P.coyote;
        pl.air_time = 0;
        pl.fall_peak = pl.y;
    } else {
        pl.grounded = false;
        pl.state = PLAYER_AIR;
        pl.coyote_t = Math.max(0, pl.coyote_t - dt);
        pl.air_time += dt;
        if (pl.y > pl.fall_peak) pl.fall_peak = pl.y;
    }
}

function enter_hang(pl, hit) {
    pl.state = PLAYER_HANG;
    pl.ledge = hit;
    pl.vx = pl.vy = pl.vz = 0;
    pl.grounded = false;
    // Face into the wall.
    pl.yaw = Math.atan2(-hit.nx, -hit.nz);
    snap_to_ledge(pl);
}

function snap_to_ledge(pl) {
    const l = pl.ledge;
    pl.x = l.x + l.nx * (P.radius * 0.85);
    pl.z = l.z + l.nz * (P.radius * 0.85);
    pl.y = l.y - P.hang_drop;
}

function step_hang(pl, world, lg, inp, wish_x, wish_z, wish_mag, dt) {
    const l = pl.ledge;
    if (!l) { pl.state = PLAYER_AIR; return; }

    // Drop: pulling away from the wall, or the dedicated down key.
    const away = wish_x * l.nx + wish_z * l.nz;
    if (inp.down || away > 0.55) {
        pl.state = PLAYER_AIR;
        pl.ledge = null;
        pl.vy = -0.5;
        pl.vx = l.nx * 1.2; pl.vz = l.nz * 1.2;
        return;
    }

    // Climb up: jump, action, or pushing into the wall.
    const into = -(wish_x * l.nx + wish_z * l.nz);
    if (pl.buffer_t > 0 || inp.action_edge || into > 0.6) {
        const landed = find_mantle_target(world, l);

        if (landed) {
            pl.state = PLAYER_CLIMB;
            pl.climb_t = 0;
            pl.climb_from = { x: pl.x, y: pl.y, z: pl.z };
            pl.climb_to = landed;
            pl.buffer_t = 0;
            pl.climb_block = '';
            return;
        }
        // Nothing to stand on up there. Recorded rather than silently ignored:
        // "he refuses to climb" is otherwise impossible to tell apart from
        // "the input never arrived".
        pl.climb_block = 'no footing above the lip';
        pl.buffer_t = 0;
    }

    // Shimmy along the lip. Movement is projected onto the edge, so the stick
    // means "left/right along this ledge" regardless of camera angle.
    const s = l.index * 8;
    const ax = lg.segs[s], az = lg.segs[s + 2];
    const bx = lg.segs[s + 3], bz = lg.segs[s + 5];
    let ex = bx - ax, ez = bz - az;
    const len = Math.hypot(ex, ez);
    if (len > 1e-4 && wish_mag > 0.05) {
        ex /= len; ez /= len;
        const along = wish_x * ex + wish_z * ez;
        let t = ((l.x - ax) * ex + (l.z - az) * ez) + along * P.shimmy_speed * dt;
        // Stay a body's width inside the ends, so the character never hangs off
        // a corner by one hand.
        t = clamp(t, P.radius, Math.max(P.radius, len - P.radius));
        l.x = ax + ex * t;
        l.z = az + ez * t;
        snap_to_ledge(pl);
    }
}

// Where the character ends up after pulling over a lip, or null.
//
// Two things make this less trivial than "the floor just inside the edge".
//
// One fixed inset is too brittle: a lip with a railing, or a sloped top,
// rejects the first sample while a spot 30 cm further in is perfectly
// standable — and the character then refuses to climb a ledge he is holding.
//
// Ledges also have things on them. The prison's walkways carry crates and
// beams, so the floor query returns the walkway while the space above it is
// occupied. Giving up there is wrong; the right answer is to stand on the
// crate. So each candidate is raised in small steps until the body fits.
const MANTLE_INSETS = [P.radius + 0.12, P.radius + 0.45, P.radius + 0.85];
const MANTLE_RISE = 0.12;
const MANTLE_MAX_RISE = 1.0;

function find_mantle_target(world, l) {
    for (const inset of MANTLE_INSETS) {
        const tx = l.x - l.nx * inset;
        const tz = l.z - l.nz * inset;

        let ty = coll_floor_under(world, tx, l.y + 0.45, tz, 1.4);
        if (ty === null) continue;
        if (ty < l.y - 0.7) continue;              // that is a different, lower floor

        for (let rise = 0; rise <= MANTLE_MAX_RISE; rise += MANTLE_RISE) {
            const y = ty + rise;
            if (coll_capsule_free(world, tx, y, tz, P.radius, P.height))
                return { x: tx, y, z: tz };
        }
    }
    return null;
}

function step_climb(pl, dt) {
    pl.climb_t += dt;
    const t = clamp(pl.climb_t / P.climb_time, 0, 1);

    // Up first, then in. Interpolating straight to the target pulls the
    // character through the lip; this traces the shape of a real mantle.
    const up = Math.min(1, t / 0.6);
    const fwd = Math.max(0, (t - 0.4) / 0.6);
    const ease = (v) => v * v * (3 - 2 * v);

    const a = pl.climb_from, b = pl.climb_to;
    pl.x = a.x + (b.x - a.x) * ease(fwd);
    pl.z = a.z + (b.z - a.z) * ease(fwd);
    pl.y = a.y + (b.y - a.y) * ease(up);

    if (t >= 1) {
        pl.state = PLAYER_GROUND;
        pl.grounded = true;
        pl.ledge = null;
        pl.vx = pl.vy = pl.vz = 0;
        pl.coyote_t = P.coyote;
        pl.fall_peak = pl.y;
    }
}

function approach(v, target, max_step) {
    const d = target - v;
    if (Math.abs(d) <= max_step) return target;
    return v + Math.sign(d) * max_step;
}

// ------------------------------------------------------------- debug shape
//
// The Prince's real mesh needs the .act node hierarchy, which is 65% recovered
// and the current blocker (docs/formats/act.md). Until then the character is a
// capsule: it has to read as a body with a facing, and nothing more.

export function player_capsule_mesh(radius = P.radius, height = P.height, rings = 8, seg = 16) {
    const verts = [];
    const indices = [];

    const push = (x, y, z, nx, ny, nz, u, v) => {
        verts.push(x, y, z, nx, ny, nz, u, v);
        return verts.length / 8 - 1;
    };

    const cy_lo = radius, cy_hi = height - radius;
    const rows = [];

    for (let i = 0; i <= rings; ++i) {          // lower hemisphere
        const a = (Math.PI * 0.5) * (i / rings);
        rows.push({ y: cy_lo - Math.cos(a) * radius, r: Math.sin(a) * radius, ny: -Math.cos(a) });
    }
    for (let i = 0; i <= rings; ++i) {          // upper hemisphere
        const a = (Math.PI * 0.5) * (i / rings);
        rows.push({ y: cy_hi + Math.sin(a) * radius, r: Math.cos(a) * radius, ny: Math.sin(a) });
    }

    const base = [];
    for (let r = 0; r < rows.length; ++r) {
        const row = [];
        for (let s = 0; s <= seg; ++s) {
            const th = (s / seg) * Math.PI * 2;
            const cx = Math.cos(th), sz = Math.sin(th);
            const nr = Math.sqrt(Math.max(0, 1 - rows[r].ny * rows[r].ny));
            row.push(push(cx * rows[r].r, rows[r].y, sz * rows[r].r,
                          cx * nr, rows[r].ny, sz * nr,
                          s / seg, r / (rows.length - 1)));
        }
        base.push(row);
    }

    for (let r = 0; r + 1 < base.length; ++r) {
        for (let s = 0; s < seg; ++s) {
            const a = base[r][s], b = base[r][s + 1];
            const c = base[r + 1][s], d = base[r + 1][s + 1];
            indices.push(a, c, b, b, c, d);
        }
    }

    // Pack into the 36-byte vertex layout the renderer expects.
    const count = verts.length / 8;
    const buf = new ArrayBuffer(count * 36);
    const f32 = new Float32Array(buf);
    const u32 = new Uint32Array(buf);
    for (let i = 0; i < count; ++i) {
        const s = i * 8, o = i * 9;
        f32[o] = verts[s]; f32[o + 1] = verts[s + 1]; f32[o + 2] = verts[s + 2];
        f32[o + 3] = verts[s + 3]; f32[o + 4] = verts[s + 4]; f32[o + 5] = verts[s + 5];
        f32[o + 6] = verts[s + 6]; f32[o + 7] = verts[s + 7];
        u32[o + 8] = 0xffffffff;
    }

    return { vertices: new Uint8Array(buf), indices: new Uint32Array(indices) };
}
