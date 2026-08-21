// camera.js — a collision-aware follow camera, plus a free-fly debug camera.
//
// The original drives its camera from data: CameraLogic + CameraStateTable read
// data/camera.dat, and the level's rooms carry hand-placed camera hints. That
// system is what gives retail PoP3D its fixed, occasionally hostile angles.
// docs/formats/text-and-tuning.md has the file; nothing about how the states
// are selected is reversed yet.
//
// Until that is understood, this is a plain orbit rig — the thing the 1999
// camera most often fails to be. It keeps one invariant the original never
// managed: the character is always visible, because the arm shortens rather
// than letting geometry come between them.

import { clamp, damp, m_look_at, m_perspective, m_mul, mat4, v3 } from '../core/math.js';
import { coll_raycast } from './collision.js';

export const CAM_FOLLOW = 0;
export const CAM_FREE = 1;

const PITCH_MIN = -1.15;
const PITCH_MAX = 0.95;

export function camera_new() {
    return {
        mode: CAM_FOLLOW,

        yaw: 0,
        pitch: -0.18,
        distance: 4.6,
        distance_want: 4.6,

        // Smoothed look-at point; lags the character so small steps and the
        // stair-stepping of the ground probe do not shake the frame.
        focus: v3(0, 0, 0),
        eye: v3(0, 0, 0),

        fov: 60 * Math.PI / 180,
        near: 0.08,
        far: 200,

        // Free-fly state.
        free_pos: v3(0, 0, 0),
        free_speed: 6,

        view: mat4(),
        proj: mat4(),
        view_proj: mat4(),
        shake: 0,
    };
}

const HEAD_HEIGHT = 1.36;
const MOUSE_SENS = 0.0022;
const PAD_SENS = 2.6;

// How far off a wall the eye is kept, and how short the arm may get. MIN_ARM is
// deliberately larger than the near plane: an eye 8 cm from a wall technically
// renders, but what it renders is one texel of brick.
const WALL_MARGIN = 0.34;
const MIN_ARM = 1.25;

export function camera_look(cam, dx, dy, pad_x, pad_y, dt) {
    cam.yaw -= dx * MOUSE_SENS + pad_x * PAD_SENS * dt;
    cam.pitch = clamp(cam.pitch - (dy * MOUSE_SENS + pad_y * PAD_SENS * dt), PITCH_MIN, PITCH_MAX);
}

export function camera_zoom(cam, wheel) {
    if (wheel) cam.distance_want = clamp(cam.distance_want + wheel * 0.5, 1.4, 12);
}

// Follows `pl`. `dt` is the display frame time, not the sim step: the camera is
// allowed to be smooth at whatever rate the display runs.
export function camera_follow(cam, world, pl, dt) {
    const tx = pl.x;
    const ty = pl.y + HEAD_HEIGHT;
    const tz = pl.z;

    // Vertical lag is slower than horizontal. Following the character's Y at
    // full rate makes every stair and every landing punch the whole frame.
    cam.focus[0] = damp(cam.focus[0], tx, 14, dt);
    cam.focus[1] = damp(cam.focus[1], ty, pl.grounded ? 7 : 4, dt);
    cam.focus[2] = damp(cam.focus[2], tz, 14, dt);

    const cp = Math.cos(cam.pitch), sp = Math.sin(cam.pitch);
    const dir_x = Math.sin(cam.yaw) * cp;
    const dir_y = sp;
    const dir_z = Math.cos(cam.yaw) * cp;

    // Sweep the arm and pull in on the first thing it meets.
    //
    // Backing off along the ray alone is not enough. The dungeon's starting
    // cell is barely wider than the character, so the arm compresses to almost
    // nothing and the eye ends up inside a wall — the whole frame goes black
    // while the HUD still reports a healthy camera. Two things prevent that:
    // the hit is pushed back along the surface normal rather than back down the
    // ray, and the arm is never allowed below MIN_ARM.
    let want = cam.distance_want;
    const hit = coll_raycast(world, cam.focus[0], cam.focus[1], cam.focus[2],
                             -dir_x, -dir_y, -dir_z, want + WALL_MARGIN);
    if (hit) {
        // Distance from the focus to a point WALL_MARGIN off the surface,
        // measured along the arm.
        const back = (hit.nx * -dir_x + hit.ny * -dir_y + hit.nz * -dir_z);
        const along = back < -0.05 ? WALL_MARGIN / -back : WALL_MARGIN;
        want = Math.max(MIN_ARM, hit.t - Math.min(along, WALL_MARGIN * 3));
    }

    // Snap in, ease out: getting the wall out of the way must be immediate,
    // recovering the framing afterwards must not be.
    cam.distance = want < cam.distance ? want : damp(cam.distance, want, 5, dt);

    cam.eye[0] = cam.focus[0] - dir_x * cam.distance;
    cam.eye[1] = cam.focus[1] - dir_y * cam.distance;
    cam.eye[2] = cam.focus[2] - dir_z * cam.distance;

    if (cam.shake > 0) {
        const s = cam.shake;
        cam.eye[0] += (Math.random() - 0.5) * s;
        cam.eye[1] += (Math.random() - 0.5) * s;
        cam.eye[2] += (Math.random() - 0.5) * s;
        cam.shake = Math.max(0, cam.shake - dt * 0.9);
    }
}

// WASD fly, the same controls the native viewer uses (see the Makefile header).
export function camera_free(cam, inp, dt, boost) {
    const cp = Math.cos(cam.pitch);
    const fwd = [Math.sin(cam.yaw) * cp, Math.sin(cam.pitch), Math.cos(cam.yaw) * cp];
    const right = [Math.cos(cam.yaw), 0, -Math.sin(cam.yaw)];

    const speed = cam.free_speed * (boost ? 4 : 1) * dt;
    cam.free_pos[0] += (fwd[0] * inp.move_z + right[0] * inp.move_x) * speed;
    cam.free_pos[1] += (fwd[1] * inp.move_z) * speed + (inp.jump ? speed : 0) - (inp.down ? speed : 0);
    cam.free_pos[2] += (fwd[2] * inp.move_z + right[2] * inp.move_x) * speed;

    cam.eye[0] = cam.free_pos[0];
    cam.eye[1] = cam.free_pos[1];
    cam.eye[2] = cam.free_pos[2];
    cam.focus[0] = cam.eye[0] + fwd[0];
    cam.focus[1] = cam.eye[1] + fwd[1];
    cam.focus[2] = cam.eye[2] + fwd[2];
}

export function camera_matrices(cam, aspect) {
    const at = cam.mode === CAM_FREE
        ? cam.focus
        : [cam.focus[0], cam.focus[1], cam.focus[2]];

    m_look_at(cam.view, cam.eye, at, [0, 1, 0]);
    m_perspective(cam.proj, cam.fov, aspect, cam.near, cam.far);
    m_mul(cam.view_proj, cam.proj, cam.view);
    return cam.view_proj;
}
