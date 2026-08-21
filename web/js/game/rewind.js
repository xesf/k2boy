// rewind.js — a ring buffer of simulation states, played backwards on demand.
//
// Two reasons this exists, one of them not about the player at all.
//
// As a feature: PoP3D predates Sands of Time by four years, and its difficulty
// is built on repetition after death. Rewind is the mechanic that turns that
// into experimentation, and it costs almost nothing here — the character's
// entire state is twelve floats.
//
// As a tool: it makes a physics bug reproducible. Being able to back up two
// seconds and watch the same fall again, frame by frame, is the difference
// between fixing a collision problem and guessing at it. That is worth having
// even if the feature never ships.
//
// It works only because player_step is a pure function of (state, input,
// world): no accumulated floating-point drift outside the snapshot, nothing
// read from the render frame.

import { PLAYER_SNAPSHOT_FLOATS, player_save, player_load } from './player.js';

const SECONDS = 12;

export function rewind_new(sim_rate) {
    const frames = Math.ceil(SECONDS * sim_rate);
    return {
        frames,
        stride: PLAYER_SNAPSHOT_FLOATS,
        data: new Float32Array(frames * PLAYER_SNAPSHOT_FLOATS),
        head: 0,          // next write slot
        count: 0,         // valid entries
        active: false,
        used: 0,          // how far back the current rewind has gone
    };
}

export function rewind_record(rw, pl) {
    player_save(pl, rw.data, rw.head * rw.stride);
    rw.head = (rw.head + 1) % rw.frames;
    if (rw.count < rw.frames) ++rw.count;
}

// Steps one frame back. Returns false when the buffer runs dry, which is the
// signal to hand control back to the simulation.
export function rewind_step(rw, pl, lg) {
    if (rw.count <= 1) return false;

    rw.head = (rw.head - 1 + rw.frames) % rw.frames;
    --rw.count;
    ++rw.used;

    player_load(pl, rw.data, rw.head * rw.stride, lg);
    return true;
}

export function rewind_reset(rw) {
    rw.head = 0;
    rw.count = 0;
    rw.active = false;
    rw.used = 0;
}

export const rewind_seconds = (rw, sim_rate) => rw.count / sim_rate;
