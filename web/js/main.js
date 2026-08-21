// main.js — boot and the frame loop.
//
// Mirrors what WinMain does (docs/reference/boot-sequence.md): resolve the
// startup module, load its level, place the Prince, then run. The differences
// are the ones the platform forces — everything is async, and the "window" is
// a canvas that can change size at any moment.
//
// The loop is a fixed-step simulation with a variable-rate render. That split
// is not decoration: the character controller has to be deterministic for
// rewind.js to be exact, and the camera has to be smooth at 144 Hz. Running
// both off the display clock would give up one or the other.
//
// URL parameters, all optional:
//   ?module=cistern     boot a module by name instead of the startup id
//   ?level=<path>       load a .lvl directly
//   ?region=1           which [Region] to stream in (default 0)
//   ?free               start in the free-fly camera

import { fs_mount_http, fs_mount_files, fs_file_count, fs_drop_cache } from './core/vfs.js';
import {
    input_init, input_sample, input_take_mouse, input_take_wheel,
    input_consume_edges, input_key_edge, input_down,
} from './core/input.js';
import {
    hud_init, hud_progress, hud_boot_done, hud_error, hud_frame, hud_update,
    hud_toggle, hud_toggle_help, hud_ask_for_folder,
} from './core/hud.js';
import {
    gfx_init, gfx_resize, gfx_begin_frame, gfx_end_frame, gfx_draw, gfx_draw_lines,
    gfx_set_view_projection, gfx_set_fog, gfx_set_clear_color, gfx_create_mesh,
    gfx_stats, GFX_BLEND_ALPHA, GFX_DEPTH_TEST, GFX_CULL_BACK,
} from './gfx/gfx.js';
import { module_at, module_find, module_table_verify, MODULE_ID_STARTUP } from './engine/module_table.js';
import { world_load_level, world_prince_start, world_settle_spawn } from './game/world.js';
import {
    player_new, player_step, player_respawn, player_capsule_mesh,
    PLAYER_STATE_NAMES, PLAYER_HANG, P,
} from './game/player.js';
import {
    camera_new, camera_look, camera_zoom, camera_follow, camera_free,
    camera_matrices, CAM_FOLLOW, CAM_FREE,
} from './game/camera.js';
import { rewind_new, rewind_record, rewind_step, rewind_reset, rewind_seconds } from './game/rewind.js';
import { ledge_debug_lines, ledge_find } from './game/ledges.js';
import {
    coll_query_aabb, coll_hits, coll_raycast, coll_floor_under, coll_capsule_free,
} from './game/collision.js';
import { m_trs_y, mat4 } from './core/math.js';

const SIM_RATE = 60;
const SIM_DT = 1 / SIM_RATE;
const MAX_STEPS = 5;              // catch-up cap; beyond this, drop the time

const g = {
    canvas: null,
    gfx_ready: false,
    world: null,
    player: null,
    camera: null,
    rewind: null,

    capsule_mesh: 0,
    player_model: mat4(),
    volume_tints: [],

    show_collision: false,
    show_ledges: false,
    show_volumes: false,
    paused: false,

    ledge_lines: null,
    accumulator: 0,
    last_time: 0,
    sim_steps: 0,
};

// ------------------------------------------------------------------- startup

async function boot() {
    hud_init();

    g.canvas = document.getElementById('view');
    resize_canvas();
    addEventListener('resize', resize_canvas);

    const bad_table = module_table_verify();
    if (bad_table) console.warn(`module table: ${bad_table}`);

    hud_progress('initialising WebGPU', 0.02);
    try {
        await gfx_init(g.canvas);
        g.gfx_ready = true;
    } catch (e) {
        hud_error(e.message);
        return;
    }

    input_init(g.canvas);

    hud_progress('mounting the install', 0.06);
    const mounted = await mount_data();
    if (!mounted) return;

    await load_and_start();
}

async function mount_data() {
    try {
        const count = await fs_mount_http('../data/', 'data-index.txt');
        console.log(`vfs: ${count} files`);
        return true;
    } catch (e) {
        // No manifest. Either the page is being served without one, or it was
        // opened straight off the filesystem. Offer the directory picker rather
        // than dead-ending.
        hud_error(
            `${e.message}\n\n` +
            'Or point this page at a copy of the retail install directly:',
            'python3 tools/webindex.py && python3 -m http.server 8080');
        hud_ask_for_folder(async (files) => {
            const count = fs_mount_files(files);
            console.log(`vfs: ${count} files from local folder`);
            document.getElementById('boot-error').hidden = true;
            document.getElementById('boot-bar').firstElementChild.style.background = '#e8c98a';
            await load_and_start();
        });
        return false;
    }
}

async function load_and_start() {
    const params = new URLSearchParams(location.search);

    let level = params.get('level');
    let label = level;
    if (!level) {
        const name = params.get('module');
        const id = name ? module_find(name) : MODULE_ID_STARTUP;
        const mod = module_at(id >= 0 ? id : MODULE_ID_STARTUP);
        if (!mod || !mod.level) {
            hud_error(`module "${name}" has no level (it is a menu or cutscene module)`);
            return;
        }
        level = mod.level;
        label = `${mod.name} (id 0x${id.toString(16).padStart(2, '0')})`;
    }

    const region = parseInt(params.get('region') ?? '0', 10) || 0;

    hud_progress(`loading ${label}`, 0.1);
    try {
        g.world = await world_load_level(level, region, (text, f) =>
            hud_progress(text, 0.1 + f * 0.85));
    } catch (e) {
        hud_error(`level load failed: ${e.message}`);
        console.error(e);
        return;
    }

    // The source text is tens of megabytes and everything is on the GPU now.
    fs_drop_cache();

    const spawn = world_settle_spawn(g.world, world_prince_start(g.world));
    g.player = player_new(spawn.x, spawn.y, spawn.z, spawn.yaw ?? 0);
    g.camera = camera_new();
    g.camera.yaw = g.player.yaw + Math.PI;
    g.camera.free_pos.set([spawn.x, spawn.y + 2, spawn.z + 5]);
    if (params.has('free')) g.camera.mode = CAM_FREE;

    g.rewind = rewind_new(SIM_RATE);

    const capsule = player_capsule_mesh();
    g.capsule_mesh = gfx_create_mesh(capsule.vertices, capsule.indices);
    g.ledge_lines = ledge_debug_lines(g.world.ledges);

    g.volume_tints = g.world.trap_types.map((_, i) => {
        const c = hue_rgb(i / Math.max(1, g.world.trap_types.length));
        return new Float32Array([c[0], c[1], c[2], 0.22]);
    });

    const s = g.world.stats;
    console.log(
        `world: ${s.rooms} rooms (${s.rooms_missing} missing), ${s.nodes} nodes, ` +
        `${s.tris | 0} tris, ${s.textures} textures (${s.textures_failed} failed), ` +
        `${s.draw_batches} batches`);
    console.log(
        `ledges: ${g.world.ledges.count} from ${g.world.ledges.candidate_edges} boundary ` +
        `edges in ${g.world.ledges.build_ms.toFixed(0)} ms`);
    console.log(`prince: ${spawn.source} at ` +
        `(${spawn.x.toFixed(2)}, ${spawn.y.toFixed(2)}, ${spawn.z.toFixed(2)})`);

    if (g.world.sky) gfx_set_clear_color(...g.world.sky);
    else if (g.world.fog) gfx_set_clear_color(...g.world.fog.color);

    hud_boot_done();

    // Everything the loop touches, reachable from the console. Being able to
    // type `k2.player.y = 20` or read `k2.world.ledges.count` while the game
    // runs is most of what a debug console would have given us, for one line.
    globalThis.k2 = g;
    // The queries the character runs on, callable by hand. Asking the console
    // "is there footing here" beats adding a print and reloading.
    globalThis.k2.probe = {
        raycast: (x, y, z, dx, dy, dz, t) => coll_raycast(g.world.collision, x, y, z, dx, dy, dz, t),
        floor_under: (x, y, z, d) => coll_floor_under(g.world.collision, x, y, z, d),
        capsule_free: (x, y, z) => coll_capsule_free(g.world.collision, x, y, z, P.radius, P.height),
        ledge_at: (x, y, z, fx, fz) => ledge_find(g.world.ledges, x, y, z, fx, fz, P.grab_reach,
                                                  P.grab_up_min, P.grab_up_max),
    };

    g.last_time = performance.now() / 1000;
    requestAnimationFrame(frame);
}

// Device pixels, capped at 2x. A 3x phone display would ask for nine times the
// fragments of a 1x one for no visible gain at this texture resolution.
function resize_canvas() {
    const dpr = Math.min(devicePixelRatio || 1, 2);
    const w = Math.max(1, Math.round(innerWidth * dpr));
    const h = Math.max(1, Math.round(innerHeight * dpr));
    if (g.canvas.width === w && g.canvas.height === h) return;
    g.canvas.width = w;
    g.canvas.height = h;
    if (g.gfx_ready) gfx_resize(w, h);
}

// --------------------------------------------------------------------- frame

function frame(now_ms) {
    requestAnimationFrame(frame);

    const now = now_ms / 1000;
    let dt = now - g.last_time;
    g.last_time = now;
    // A tab that was in the background hands back a huge delta; simulating it
    // would teleport the character through the level.
    if (dt > 0.25) dt = 0.25;
    hud_frame(dt);

    handle_toggles();

    const inp = input_sample();
    const mouse = input_take_mouse();
    camera_zoom(g.camera, input_take_wheel());
    camera_look(g.camera, mouse.dx, mouse.dy, inp.look_x, inp.look_y, dt);

    // --- simulate -----------------------------------------------------------

    g.accumulator += dt;
    g.sim_steps = 0;

    const rewinding = inp.rewind && !g.paused;
    if (rewinding) {
        g.rewind.active = true;
        while (g.accumulator >= SIM_DT && g.sim_steps < MAX_STEPS) {
            g.accumulator -= SIM_DT;
            ++g.sim_steps;
            if (!rewind_step(g.rewind, g.player, g.world.ledges)) break;
        }
        input_consume_edges();
    } else {
        if (g.rewind.active) {
            // Coming out of a rewind: drop the velocity the restored frame had,
            // so releasing R at the top of a fall does not immediately replay it.
            g.rewind.active = false;
            g.rewind.used = 0;
            g.player.vx = g.player.vy = g.player.vz = 0;
        }
        while (g.accumulator >= SIM_DT && g.sim_steps < MAX_STEPS) {
            g.accumulator -= SIM_DT;
            ++g.sim_steps;
            if (!g.paused && g.camera.mode === CAM_FOLLOW) {
                player_step(g.player, g.world.collision, g.world.ledges, inp, g.camera.yaw, SIM_DT);
                rewind_record(g.rewind, g.player);
            }
            input_consume_edges();
        }
        // Discard whatever could not be simulated, rather than carrying a debt
        // that makes the next frames worse.
        if (g.accumulator > SIM_DT * MAX_STEPS) g.accumulator = 0;
    }

    // --- camera -------------------------------------------------------------

    if (g.camera.mode === CAM_FREE) camera_free(g.camera, inp, dt, inp.walk);
    else camera_follow(g.camera, g.world.collision, g.player, dt);

    render(now);
    hud_update(now_ms, hud_lines());
}

function handle_toggles() {
    if (input_key_edge('F1')) hud_toggle();
    if (input_key_edge('KeyH')) hud_toggle_help();
    if (input_key_edge('F2')) {
        g.camera.mode = g.camera.mode === CAM_FREE ? CAM_FOLLOW : CAM_FREE;
        if (g.camera.mode === CAM_FREE) g.camera.free_pos.set(g.camera.eye);
    }
    if (input_key_edge('F3')) g.show_collision = !g.show_collision;
    if (input_key_edge('F4')) g.show_ledges = !g.show_ledges;
    if (input_key_edge('KeyT')) g.show_volumes = !g.show_volumes;
    if (input_key_edge('F5')) { player_respawn(g.player); rewind_reset(g.rewind); }
    if (input_key_edge('KeyP')) g.paused = !g.paused;
}

const TINT_HANG = new Float32Array([0.95, 0.79, 0.45, 1]);
const TINT_BODY = new Float32Array([0.82, 0.74, 0.62, 1]);

function render() {
    const aspect = g.canvas.width / Math.max(1, g.canvas.height);
    const view_proj = camera_matrices(g.camera, aspect);

    gfx_begin_frame();
    gfx_set_view_projection(view_proj, g.camera.eye);

    if (g.world.fog) {
        const f = g.world.fog;
        gfx_set_fog(true, f.start, f.end, f.color[0], f.color[1], f.color[2]);
        g.camera.far = Math.max(60, f.end * 1.1);
    } else {
        gfx_set_fog(false, 0, 1, 0, 0, 0);
    }

    // Opaque first, writing depth. Room geometry is world-space, so no model
    // matrix — main.cpp draws the whole level under one identity too.
    for (const b of g.world.batches) gfx_draw(b.mesh_id, { texture: b.texture });

    // The character. Lit rather than vertex-coloured: it is a placeholder shape
    // with no baked lighting of its own, and it has to read as solid against
    // rooms that do have it.
    m_trs_y(g.player_model, [g.player.x, g.player.y, g.player.z], g.player.yaw, 1);
    gfx_draw(g.capsule_mesh, {
        model: g.player_model,
        cull: GFX_CULL_BACK,
        vertex_color: false,
        lit: true,
        tint: g.player.state === PLAYER_HANG ? TINT_HANG : TINT_BODY,
    });

    // Transparent last of the lit passes, back to front. These draws do not
    // write depth, so without sorting a distant alpha surface submitted later
    // paints straight over nearer geometry.
    if (g.world.transparent.length) {
        const eye = g.camera.eye;
        g.world.transparent.sort((a, b) => {
            const da = (a.center[0] - eye[0]) ** 2 + (a.center[1] - eye[1]) ** 2 + (a.center[2] - eye[2]) ** 2;
            const db = (b.center[0] - eye[0]) ** 2 + (b.center[1] - eye[1]) ** 2 + (b.center[2] - eye[2]) ** 2;
            return db - da;
        });
        for (const t of g.world.transparent) {
            gfx_draw(t.mesh_id, {
                texture: t.texture,
                blend: GFX_BLEND_ALPHA,
                depth: GFX_DEPTH_TEST,
                alpha_ref: 0.02,
            });
        }
    }

    // One colour per trap type, spread over the hue circle — the same legend
    // the native viewer prints for its T overlay. The tint arrays are built
    // once at load: gfx packs uniforms at end_frame, so a shared scratch array
    // would give every volume the last colour written to it.
    if (g.show_volumes) {
        for (const v of g.world.volumes) {
            gfx_draw(v.mesh_id, {
                blend: GFX_BLEND_ALPHA,
                depth: GFX_DEPTH_TEST,
                vertex_color: false,
                tint: g.volume_tints[v.trap_type],
            });
        }
    }

    if (g.show_ledges && g.ledge_lines)
        gfx_draw_lines(g.ledge_lines.data, g.ledge_lines.vertex_count);

    if (g.show_collision) {
        const lines = collision_debug_lines();
        if (lines) gfx_draw_lines(lines.data, lines.vertex_count);
    }

    gfx_end_frame();
}

// Wireframe of the triangles near the character. Drawing the whole collision
// mesh is useless at this density — the interesting question is always "what is
// the capsule actually touching".
let g_coll_buf = null;
function collision_debug_lines() {
    const w = g.world.collision;
    const r = 3.5;

    const n = coll_query_aabb(w, g.player.x - r, g.player.y - r, g.player.z - r,
                                 g.player.x + r, g.player.y + r + P.height, g.player.z + r);
    if (n === 0) return null;

    const verts = n * 6;
    if (!g_coll_buf || g_coll_buf.length < verts * 4)
        g_coll_buf = new Float32Array(Math.max(verts * 4, 4096 * 4));

    const f32 = g_coll_buf;
    const u32 = new Uint32Array(f32.buffer);
    const hits = coll_hits();

    for (let i = 0; i < n; ++i) {
        const t = hits[i];
        const o = t * 9;
        const up = w.normals[t * 3 + 1];
        const color = up > 0.55 ? 0xff70d070 : up < -0.5 ? 0xff7070d0 : 0xffd0a070;

        for (let e = 0; e < 3; ++e) {
            const a = o + e * 3;
            const b = o + ((e + 1) % 3) * 3;
            const v = (i * 6 + e * 2) * 4;
            f32[v] = w.tris[a]; f32[v + 1] = w.tris[a + 1]; f32[v + 2] = w.tris[a + 2];
            u32[v + 3] = color;
            f32[v + 4] = w.tris[b]; f32[v + 5] = w.tris[b + 1]; f32[v + 6] = w.tris[b + 2];
            u32[v + 7] = color;
        }
    }
    return { data: f32, vertex_count: verts };
}

function hue_rgb(h) {
    const k = (n) => (n + h * 6) % 6;
    const f = (n) => Math.max(0, Math.min(1, Math.min(k(n), 4 - k(n), 1)));
    return [f(5), f(3), f(1)];
}

function hud_lines() {
    const p = g.player;
    const s = gfx_stats();
    const w = g.world.stats;
    return [
        `pos    ${p.x.toFixed(2)} ${p.y.toFixed(2)} ${p.z.toFixed(2)}`,
        `state  ${PLAYER_STATE_NAMES[p.state]}${p.grounded ? ' grounded' : ''}` +
            `${p.stagger_t > 0 ? ' stagger' : ''}`,
        `speed  ${p.speed.toFixed(2)} m/s   vy ${p.vy.toFixed(2)}`,
        `cam    ${g.camera.mode === CAM_FREE ? 'free' : 'follow'}  ` +
            `d ${g.camera.distance.toFixed(1)}`,
        `draw   ${s.draws} calls  ${(s.tris / 1000).toFixed(0)}k tris  ` +
            `${s.pipeline_switches} pipelines`,
        `level  ${w.rooms} rooms  ${g.world.ledges.count} ledges  ` +
            `${(g.world.collision.tri_count / 1000).toFixed(0)}k coll tris`,
        `rewind ${rewind_seconds(g.rewind, SIM_RATE).toFixed(1)}s buffered` +
            `${g.rewind.active ? '  REWINDING' : ''}`,
        g.paused ? 'PAUSED (P)' : '',
    ].filter(Boolean);
}

boot();
