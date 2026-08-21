// hud.js — the boot screen, the debug overlay and the error panel.
//
// Kept out of main.js so the loop file stays about the loop. All of it is plain
// DOM; there is no reason for a UI framework in a page with three panels.

const el = (id) => document.getElementById(id);

let g_boot, g_boot_status, g_boot_bar, g_boot_error;
let g_hud, g_hud_perf, g_hud_state, g_help;

let g_frame_times = new Float32Array(60);
let g_frame_head = 0;
let g_last_update = 0;

export function hud_init() {
    g_boot = el('boot');
    g_boot_status = el('boot-status');
    g_boot_bar = el('boot-bar').firstElementChild;
    g_boot_error = el('boot-error');
    g_hud = el('hud');
    g_hud_perf = el('hud-perf');
    g_hud_state = el('hud-state');
    g_help = el('help');
}

export function hud_progress(text, fraction) {
    g_boot_status.textContent = text;
    g_boot_bar.style.width = `${Math.round(fraction * 100)}%`;
}

export function hud_boot_done() {
    g_boot.classList.add('done');
    setTimeout(() => { g_boot.hidden = true; }, 600);
    g_hud.hidden = false;
    g_help.hidden = false;
}

// `hint` is optional markup shown under the message — used for the two failures
// that have an obvious fix (no WebGPU, no data manifest).
export function hud_error(message, hint) {
    g_boot.classList.remove('done');
    g_boot.hidden = false;
    g_boot_bar.style.width = '100%';
    g_boot_bar.style.background = '#b4472f';
    g_boot_status.textContent = 'failed';
    g_boot_error.hidden = false;
    g_boot_error.textContent = message;
    if (hint) {
        const code = document.createElement('code');
        code.textContent = hint;
        g_boot_error.appendChild(code);
    }
    console.error(message);
}

export function hud_toggle() { g_hud.hidden = !g_hud.hidden; }
export function hud_toggle_help() { g_help.hidden = !g_help.hidden; }

export function hud_frame(dt) {
    g_frame_times[g_frame_head] = dt;
    g_frame_head = (g_frame_head + 1) % g_frame_times.length;
}

// Called every frame; only touches the DOM ten times a second, because writing
// textContent every frame is enough to show up in a profile.
export function hud_update(now, lines) {
    if (g_hud.hidden || now - g_last_update < 100) return;
    g_last_update = now;

    let sum = 0, worst = 0;
    for (const t of g_frame_times) { sum += t; if (t > worst) worst = t; }
    const avg = sum / g_frame_times.length;

    g_hud_perf.textContent =
        `${(1 / Math.max(avg, 1e-6)).toFixed(0)} fps   ${(avg * 1000).toFixed(1)} ms` +
        `  (worst ${(worst * 1000).toFixed(1)})`;
    g_hud_state.textContent = lines.join('\n');
}

// A one-line picker shown when the manifest is missing: lets the user point the
// page at a local copy of the install instead of serving one.
export function hud_ask_for_folder(on_pick) {
    const input = document.createElement('input');
    input.type = 'file';
    input.webkitdirectory = true;
    input.multiple = true;
    input.style.display = 'none';
    document.body.appendChild(input);

    const button = document.createElement('button');
    button.textContent = 'choose your data/ folder…';
    button.style.cssText =
        'margin-top:14px;padding:8px 14px;background:#241d17;color:#e8c98a;' +
        'border:1px solid #4a3d2c;font:inherit;cursor:pointer';
    button.onclick = () => input.click();
    g_boot_error.appendChild(button);

    input.onchange = () => {
        if (input.files.length) on_pick(input.files);
    };
}
