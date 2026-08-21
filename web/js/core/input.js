// input.js — keyboard, mouse and gamepad, reduced to one poll-able state.
//
// The original read DirectInput directly in the game loop (2 DINPUT imports;
// see docs/ARCHITECTURE.md §1). The shape here is the same: the gameplay code
// asks for the current stick and button state, it never subscribes to events.
//
// Two things the 1999 build could not do and this one should:
//
//   * analog movement. PoP3D's keyboard control is one of the reasons the game
//     feels stiff; a gamepad stick and a smoothed keyboard vector both land in
//     the same move_x / move_z pair.
//   * buffered edges. `just_pressed` survives until the fixed-step simulation
//     consumes it, so a jump issued between two sim steps is never dropped.
//     At 60 Hz sim and 144 Hz display that is otherwise a real input loss.

const KEY_MAP = {
    KeyW: 'forward', ArrowUp: 'forward',
    KeyS: 'back', ArrowDown: 'back',
    KeyA: 'left', ArrowLeft: 'left',
    KeyD: 'right', ArrowRight: 'right',
    Space: 'jump',
    ShiftLeft: 'walk', ShiftRight: 'walk',
    KeyR: 'rewind',
    KeyE: 'action',
    KeyQ: 'down',
};

const g_down = new Set();          // action names currently held
const g_pressed = new Set();       // edges since the last input_consume_edges
const g_released = new Set();

let g_mouse_dx = 0, g_mouse_dy = 0;
let g_wheel = 0;
let g_pointer_locked = false;
let g_dragging = false;
let g_canvas = null;

const g_toggles = new Map();       // raw KeyboardEvent.code -> edge flag

export function input_init(canvas) {
    g_canvas = canvas;

    addEventListener('keydown', (e) => {
        if (e.repeat) {
            // Still swallow the browser default (space scrolls the page).
            if (KEY_MAP[e.code]) e.preventDefault();
            return;
        }
        g_toggles.set(e.code, true);

        const a = KEY_MAP[e.code];
        if (!a) return;
        e.preventDefault();
        if (!g_down.has(a)) g_pressed.add(a);
        g_down.add(a);
    });

    addEventListener('keyup', (e) => {
        const a = KEY_MAP[e.code];
        if (!a) return;
        g_down.delete(a);
        g_released.add(a);
    });

    // Losing focus with a key held leaves it stuck down forever otherwise —
    // alt-tabbing mid-run used to make the character sprint into a wall.
    addEventListener('blur', () => {
        g_down.clear();
        g_dragging = false;
    });

    canvas.addEventListener('mousedown', (e) => {
        if (e.button === 0) {
            g_dragging = true;
            // Pointer lock is the good path, but it needs a user gesture and
            // can be refused; dragging is the fallback, not the other way round.
            canvas.requestPointerLock?.();
        }
    });

    addEventListener('mouseup', () => { g_dragging = false; });

    addEventListener('mousemove', (e) => {
        if (g_pointer_locked) {
            g_mouse_dx += e.movementX;
            g_mouse_dy += e.movementY;
        } else if (g_dragging) {
            g_mouse_dx += e.movementX;
            g_mouse_dy += e.movementY;
        }
    });

    canvas.addEventListener('wheel', (e) => {
        e.preventDefault();
        g_wheel += Math.sign(e.deltaY);
    }, { passive: false });

    document.addEventListener('pointerlockchange', () => {
        g_pointer_locked = document.pointerLockElement === canvas;
    });

    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
}

// --------------------------------------------------------------- query state

export const input_down = (action) => g_down.has(action);
export const input_pressed = (action) => g_pressed.has(action);
export const input_released = (action) => g_released.has(action);
export const input_pointer_locked = () => g_pointer_locked;

// One-shot check on a raw key code, for UI toggles that are not gameplay
// actions (F1..F5, H). Consumes the edge.
export function input_key_edge(code) {
    if (!g_toggles.get(code)) return false;
    g_toggles.set(code, false);
    return true;
}

// Mouse delta since the last call, in pixels. Reading clears it.
export function input_take_mouse() {
    const d = { dx: g_mouse_dx, dy: g_mouse_dy };
    g_mouse_dx = 0; g_mouse_dy = 0;
    return d;
}

export function input_take_wheel() {
    const w = g_wheel;
    g_wheel = 0;
    return w;
}

// Called once per simulation step, after the step has read the edges.
export function input_consume_edges() {
    g_pressed.clear();
    g_released.clear();
}

// ------------------------------------------------------------------ gamepad

const DEADZONE = 0.22;

function apply_deadzone(x, y) {
    const m = Math.hypot(x, y);
    if (m < DEADZONE) return [0, 0];
    // Rescale so the stick still reaches 1.0 at the rim rather than jumping
    // from 0 to 0.22 the instant it leaves the deadzone.
    const s = (m - DEADZONE) / (1 - DEADZONE) / m;
    return [x * s, y * s];
}

// Returns { move_x, move_z, look_x, look_y, jump, walk } with move/look already
// deadzoned and clamped to the unit disc. Keyboard and pad are merged, pad
// winning when it is actually deflected.
export function input_sample() {
    let mx = 0, mz = 0;
    if (g_down.has('left')) mx -= 1;
    if (g_down.has('right')) mx += 1;
    if (g_down.has('forward')) mz += 1;
    if (g_down.has('back')) mz -= 1;

    // Normalise the keyboard diagonal so W+D is not 41% faster than W alone.
    const km = Math.hypot(mx, mz);
    if (km > 1) { mx /= km; mz /= km; }

    let lx = 0, ly = 0;
    let jump = g_down.has('jump');
    let jump_edge = g_pressed.has('jump');
    let walk = g_down.has('walk');
    let rewind = g_down.has('rewind');
    let action = g_down.has('action');
    let action_edge = g_pressed.has('action');

    const pads = navigator.getGamepads?.() ?? [];
    for (const pad of pads) {
        if (!pad) continue;

        const [ax, ay] = apply_deadzone(pad.axes[0] ?? 0, pad.axes[1] ?? 0);
        if (ax || ay) { mx = ax; mz = -ay; }

        const [rx, ry] = apply_deadzone(pad.axes[2] ?? 0, pad.axes[3] ?? 0);
        lx += rx; ly += ry;

        const btn = (i) => pad.buttons[i]?.pressed ?? false;
        if (btn(0)) jump = true;                       // A / cross
        if (btn(2)) { action = true; }                 // X / square
        if (btn(10) || (pad.buttons[6]?.value ?? 0) > 0.4) walk = true;
        if (btn(1)) rewind = true;                     // B / circle

        // Edges from a pad need remembering across frames, which the browser
        // does not do for us.
        if (btn(0) && !g_pad_prev_jump) jump_edge = true;
        if (btn(2) && !g_pad_prev_action) action_edge = true;
        g_pad_prev_jump = btn(0);
        g_pad_prev_action = btn(2);
        break;                                          // first connected pad only
    }

    return {
        move_x: mx, move_z: mz,
        look_x: lx, look_y: ly,
        jump, jump_edge, walk, rewind, action, action_edge,
    };
}

let g_pad_prev_jump = false;
let g_pad_prev_action = false;
