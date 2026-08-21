// gfx.js — WebGPU rendering backend.
//
// The second implementation of the interface described in src/gfx/gfx.h. The
// rule that header states is kept here: the platform layer owns surface
// creation, the renderer owns only the graphics API. Nothing above this file
// touches a GPUDevice.
//
// Differences from the GL 3.3 backend that are forced by the API rather than
// chosen:
//
//   * Draws are recorded, not issued. gfx_draw() appends to a list;
//     gfx_end_frame() writes one uniform buffer and replays the list into a
//     single render pass. WebGPU has no immediate-mode uniform update, so
//     per-draw state has to be laid out before the pass is encoded.
//   * Render state is baked into pipelines rather than set on the fly, so the
//     blend / cull / depth combinations live in a small pipeline cache.
//   * Clip space z is [0, 1], handled in math.js::m_perspective.
//
// WebGPU descriptor keys are the API's own camelCase and are left alone;
// everything this file defines is snake_case.

import { SCENE_WGSL, LINE_WGSL } from './shaders.js';

export const GFX_BLEND_NONE = 0;
export const GFX_BLEND_ALPHA = 1;
export const GFX_BLEND_ADD = 2;

export const GFX_CULL_NONE = 0;
export const GFX_CULL_BACK = 1;
export const GFX_CULL_FRONT = 2;

export const GFX_DEPTH_NONE = 0;        // no test, no write
export const GFX_DEPTH_TEST = 1;        // test, no write  (alpha-blended geometry)
export const GFX_DEPTH_TEST_WRITE = 2;  // test and write  (opaque)

const COLOR_FORMAT_DEPTH = 'depth24plus';
const OBJ_STRIDE = 256;                 // minUniformBufferOffsetAlignment
const FRAME_SIZE = 128;                 // 64 + 16 + 16 + 16, padded
const SAMPLE_COUNT = 4;                 // MSAA; the content is all hard edges

let g_device = null;
let g_context = null;
let g_canvas = null;
let g_format = 'bgra8unorm';

let g_depth_view = null;
let g_msaa_view = null;
let g_width = 1, g_height = 1;

let g_scene_module = null;
let g_line_module = null;
let g_pipelines = new Map();            // state key -> GPURenderPipeline
let g_pipeline_layout = null;
let g_line_pipeline = null;
let g_line_layout = null;

let g_frame_buffer = null;
let g_frame_bind = null;
let g_frame_data = new Float32Array(FRAME_SIZE / 4);

let g_obj_buffer = null;
let g_obj_bind = null;
let g_obj_bind_layout = null;
let g_obj_data = new Float32Array(0);
let g_obj_capacity = 0;

let g_sampler = null;
let g_group2_layout = null;
let g_white_texture = 0;

let g_textures = [null];                // index 0 is GFX_INVALID_TEXTURE
let g_meshes = [null];                  // index 0 is GFX_INVALID_MESH

let g_draws = [];
let g_line_batches = [];
let g_line_buffer = null;
let g_line_capacity = 0;
let g_clear = { r: 0, g: 0, b: 0, a: 1 };

let g_stats = { draws: 0, tris: 0, pipeline_switches: 0 };

export const GFX_INVALID_TEXTURE = 0;
export const GFX_INVALID_MESH = 0;

// ------------------------------------------------------------------ lifecycle

export async function gfx_init(canvas) {
    if (!navigator.gpu) {
        throw new Error(
            'WebGPU is not available in this browser.\n' +
            'Chrome 113+, Edge 113+ or Safari 18+ on a machine with a GPU.');
    }

    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('no WebGPU adapter — the GPU may be blocklisted');

    g_device = await adapter.requestDevice();
    g_device.lost.then((info) => {
        console.error('gfx: device lost —', info.message);
    });
    g_device.addEventListener?.('uncapturederror', (e) => {
        console.error('gfx: uncaptured error —', e.error?.message ?? e.error);
    });

    g_canvas = canvas;
    g_context = canvas.getContext('webgpu');
    g_format = navigator.gpu.getPreferredCanvasFormat();
    g_context.configure({ device: g_device, format: g_format, alphaMode: 'opaque' });

    g_scene_module = g_device.createShaderModule({ code: SCENE_WGSL, label: 'scene' });
    g_line_module = g_device.createShaderModule({ code: LINE_WGSL, label: 'line' });

    const group0 = g_device.createBindGroupLayout({
        entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                    buffer: { type: 'uniform' } }],
    });
    const group1 = g_device.createBindGroupLayout({
        entries: [{ binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
                    buffer: { type: 'uniform', hasDynamicOffset: true, minBindingSize: 96 } }],
    });
    g_group2_layout = g_device.createBindGroupLayout({
        entries: [
            { binding: 0, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } },
            { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float' } },
        ],
    });

    g_pipeline_layout = g_device.createPipelineLayout({
        bindGroupLayouts: [group0, group1, g_group2_layout],
    });
    g_line_layout = g_device.createPipelineLayout({ bindGroupLayouts: [group0] });

    g_frame_buffer = g_device.createBuffer({
        size: FRAME_SIZE,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        label: 'frame uniforms',
    });
    g_frame_bind = g_device.createBindGroup({
        layout: group0,
        entries: [{ binding: 0, resource: { buffer: g_frame_buffer } }],
    });

    g_obj_bind_layout = group1;
    obj_reserve(512);

    // The install's textures tile, and every wall UV relies on it (prison1
    // spans V -20.6..12.4). Anisotropy matters more here than usual because
    // almost every surface is viewed at a grazing angle.
    g_sampler = g_device.createSampler({
        addressModeU: 'repeat',
        addressModeV: 'repeat',
        magFilter: 'linear',
        minFilter: 'linear',
        mipmapFilter: 'linear',
        maxAnisotropy: 8,
    });

    g_white_texture = gfx_create_texture(1, 1, new Uint8Array([255, 255, 255, 255]));

    g_line_pipeline = g_device.createRenderPipeline({
        layout: g_line_layout,
        vertex: {
            module: g_line_module,
            entryPoint: 'vs',
            buffers: [{
                arrayStride: 16,
                attributes: [
                    { shaderLocation: 0, offset: 0, format: 'float32x3' },
                    { shaderLocation: 1, offset: 12, format: 'unorm8x4' },
                ],
            }],
        },
        fragment: {
            module: g_line_module,
            entryPoint: 'fs',
            targets: [{
                format: g_format,
                blend: {
                    color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
                },
            }],
        },
        primitive: { topology: 'line-list' },
        depthStencil: {
            format: COLOR_FORMAT_DEPTH,
            depthWriteEnabled: false,
            depthCompare: 'less-equal',
        },
        multisample: { count: SAMPLE_COUNT },
        label: 'lines',
    });

    gfx_resize(canvas.width, canvas.height);
    return { vendor: adapter.info?.vendor ?? '', architecture: adapter.info?.architecture ?? '' };
}

// The per-object uniforms live in one buffer indexed by dynamic offset, so it
// has to be big enough for every draw in the frame. Growing means a new buffer
// and a new bind group; the layout is captured once in gfx_init.
function obj_reserve(count) {
    if (count <= g_obj_capacity) return;
    g_obj_capacity = Math.max(count, g_obj_capacity * 2, 512);

    g_obj_buffer?.destroy();
    g_obj_buffer = g_device.createBuffer({
        size: g_obj_capacity * OBJ_STRIDE,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        label: 'object uniforms',
    });
    g_obj_data = new Float32Array(g_obj_capacity * OBJ_STRIDE / 4);
    g_obj_bind = g_device.createBindGroup({
        layout: g_obj_bind_layout,
        entries: [{ binding: 0, resource: { buffer: g_obj_buffer, size: 96 } }],
    });
}

export function gfx_resize(width, height) {
    g_width = Math.max(1, width | 0);
    g_height = Math.max(1, height | 0);

    g_depth_view?.texture?.destroy?.();

    const depth = g_device.createTexture({
        size: [g_width, g_height],
        format: COLOR_FORMAT_DEPTH,
        sampleCount: SAMPLE_COUNT,
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
        label: 'depth',
    });
    g_depth_view = depth.createView();
    g_depth_view.texture = depth;

    const msaa = g_device.createTexture({
        size: [g_width, g_height],
        format: g_format,
        sampleCount: SAMPLE_COUNT,
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
        label: 'msaa colour',
    });
    g_msaa_view?.texture?.destroy?.();
    g_msaa_view = msaa.createView();
    g_msaa_view.texture = msaa;
}

export const gfx_width = () => g_width;
export const gfx_height = () => g_height;
export const gfx_stats = () => g_stats;

// ------------------------------------------------------------------ pipelines

function pipeline_for(blend, cull, depth) {
    const key = blend * 16 + cull * 4 + depth;
    const hit = g_pipelines.get(key);
    if (hit) return hit;

    let blend_state;
    if (blend === GFX_BLEND_ALPHA) {
        blend_state = {
            color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
        };
    } else if (blend === GFX_BLEND_ADD) {
        blend_state = {
            color: { srcFactor: 'src-alpha', dstFactor: 'one', operation: 'add' },
            alpha: { srcFactor: 'zero', dstFactor: 'one', operation: 'add' },
        };
    }

    const pipe = g_device.createRenderPipeline({
        layout: g_pipeline_layout,
        vertex: {
            module: g_scene_module,
            entryPoint: 'vs',
            buffers: [{
                arrayStride: 36,
                attributes: [
                    { shaderLocation: 0, offset: 0, format: 'float32x3' },   // pos
                    { shaderLocation: 1, offset: 12, format: 'float32x3' },  // normal
                    { shaderLocation: 2, offset: 24, format: 'float32x2' },  // uv
                    { shaderLocation: 3, offset: 32, format: 'unorm8x4' },   // colour
                ],
            }],
        },
        fragment: {
            module: g_scene_module,
            entryPoint: 'fs',
            targets: [{ format: g_format, blend: blend_state }],
        },
        primitive: {
            topology: 'triangle-list',
            // .K2A actor meshes measure as CCW-outward (see main.cpp's
            // check_winding), which is WebGPU's default front face. Room walls
            // are frequently single-sided sheets, so rooms are drawn with
            // culling off regardless.
            cullMode: cull === GFX_CULL_BACK ? 'back' : cull === GFX_CULL_FRONT ? 'front' : 'none',
        },
        depthStencil: {
            format: COLOR_FORMAT_DEPTH,
            depthWriteEnabled: depth === GFX_DEPTH_TEST_WRITE,
            depthCompare: depth === GFX_DEPTH_NONE ? 'always' : 'less-equal',
        },
        multisample: { count: SAMPLE_COUNT },
        label: `scene b${blend} c${cull} d${depth}`,
    });

    g_pipelines.set(key, pipe);
    return pipe;
}

// ------------------------------------------------------------------ resources

// `rgba` is width * height * 4 bytes, top row first. `mips` is an optional
// chain from image_build_mips; without it the texture gets a single level.
export function gfx_create_texture(width, height, rgba, mips = null) {
    const levels = mips ?? [{ width, height, rgba }];

    const tex = g_device.createTexture({
        size: [width, height],
        format: 'rgba8unorm',
        mipLevelCount: levels.length,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });

    for (let i = 0; i < levels.length; ++i) {
        const l = levels[i];
        g_device.queue.writeTexture(
            { texture: tex, mipLevel: i },
            l.rgba,
            { bytesPerRow: l.width * 4, rowsPerImage: l.height },
            [l.width, l.height]);
    }

    const bind = g_device.createBindGroup({
        layout: g_group2_layout,
        entries: [
            { binding: 0, resource: g_sampler },
            { binding: 1, resource: tex.createView() },
        ],
    });

    g_textures.push({ tex, bind, width, height });
    return g_textures.length - 1;
}

export function gfx_destroy_texture(id) {
    const t = g_textures[id];
    if (!t) return;
    t.tex.destroy();
    g_textures[id] = null;
}

export const gfx_white_texture = () => g_white_texture;

// `vertices` is a Uint8Array in the 36-byte layout k2a_build_mesh produces,
// `indices` a Uint32Array.
export function gfx_create_mesh(vertices, indices) {
    // Buffer sizes must be a multiple of 4 for writeBuffer / mappedAtCreation.
    const vsize = (vertices.byteLength + 3) & ~3;
    const isize = (indices.byteLength + 3) & ~3;

    const vbuf = g_device.createBuffer({
        size: vsize, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    const ibuf = g_device.createBuffer({
        size: isize, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });

    g_device.queue.writeBuffer(vbuf, 0, vertices.buffer, vertices.byteOffset, vertices.byteLength);
    g_device.queue.writeBuffer(ibuf, 0, indices.buffer, indices.byteOffset, indices.byteLength);

    g_meshes.push({ vbuf, ibuf, index_count: indices.length });
    return g_meshes.length - 1;
}

export function gfx_destroy_mesh(id) {
    const m = g_meshes[id];
    if (!m) return;
    m.vbuf.destroy();
    m.ibuf.destroy();
    g_meshes[id] = null;
}

export function gfx_free_all() {
    for (let i = 1; i < g_meshes.length; ++i) gfx_destroy_mesh(i);
    for (let i = 2; i < g_textures.length; ++i) gfx_destroy_texture(i);  // keep white
    g_meshes.length = 1;
    const white = g_textures[g_white_texture];
    g_textures = [null, white];
    g_white_texture = 1;
}

// -------------------------------------------------------------------- frame

export function gfx_set_clear_color(r, g, b) { g_clear = { r, g, b, a: 1 }; }

export function gfx_begin_frame() {
    g_draws.length = 0;
    g_line_batches.length = 0;
    g_stats = { draws: 0, tris: 0, pipeline_switches: 0 };
}

// `view_proj` is column-major 4x4; `cam_pos` a vec3.
export function gfx_set_view_projection(view_proj, cam_pos) {
    g_frame_data.set(view_proj, 0);
    g_frame_data[24] = cam_pos[0];
    g_frame_data[25] = cam_pos[1];
    g_frame_data[26] = cam_pos[2];
}

export function gfx_set_fog(enabled, start, end, r, g, b) {
    g_frame_data[16] = r; g_frame_data[17] = g; g_frame_data[18] = b; g_frame_data[19] = 1;
    g_frame_data[20] = start;
    g_frame_data[21] = end;
    g_frame_data[22] = enabled ? 1 : 0;
}

// One draw. `p` is a plain options object; every field has a default that
// matches the common case (opaque, textured, vertex-coloured room geometry).
//
// Defaults matter here for the same reason they did in gfx.h's render_state:
// leaving a field to inherit the previous draw's value is exactly how the
// native backend once ended up rendering whole rooms in wireframe.
export function gfx_draw(mesh_id, p) {
    const m = g_meshes[mesh_id];
    if (!m) return;

    g_draws.push({
        mesh: m,
        texture: p.texture ?? GFX_INVALID_TEXTURE,
        model: p.model ?? null,             // null = identity (world-space geometry)
        tint: p.tint ?? null,               // null = white
        blend: p.blend ?? GFX_BLEND_NONE,
        cull: p.cull ?? GFX_CULL_NONE,
        depth: p.depth ?? GFX_DEPTH_TEST_WRITE,
        alpha_ref: p.alpha_ref ?? 0,
        vertex_color: p.vertex_color ?? true,
        lit: p.lit ?? false,
        first_index: p.first_index ?? 0,
        index_count: p.index_count ?? m.index_count,
    });
}

// `data` is a Float32Array/ArrayBuffer view in the 16-byte line layout:
// 3 floats position then 4 unorm8 colour, two vertices per line.
export function gfx_draw_lines(data, vertex_count) {
    if (vertex_count > 0) g_line_batches.push({ data, vertex_count });
}

const IDENTITY = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
const WHITE = new Float32Array([1, 1, 1, 1]);

export function gfx_end_frame() {
    obj_reserve(g_draws.length);

    // Pack every draw's uniforms into one buffer, then bind by dynamic offset.
    for (let i = 0; i < g_draws.length; ++i) {
        const d = g_draws[i];
        const o = i * (OBJ_STRIDE / 4);
        g_obj_data.set(d.model ?? IDENTITY, o);
        g_obj_data.set(d.tint ?? WHITE, o + 16);
        g_obj_data[o + 20] = d.alpha_ref;
        g_obj_data[o + 21] = d.texture !== GFX_INVALID_TEXTURE ? 1 : 0;
        g_obj_data[o + 22] = d.vertex_color ? 1 : 0;
        g_obj_data[o + 23] = d.lit ? 1 : 0;
    }
    if (g_draws.length) {
        g_device.queue.writeBuffer(g_obj_buffer, 0, g_obj_data.buffer, 0,
                                   g_draws.length * OBJ_STRIDE);
    }
    g_device.queue.writeBuffer(g_frame_buffer, 0, g_frame_data.buffer, 0, FRAME_SIZE);

    // Debug lines go into one shared buffer, appended per batch.
    let line_total = 0;
    for (const b of g_line_batches) line_total += b.vertex_count;
    if (line_total > g_line_capacity) {
        g_line_buffer?.destroy();
        g_line_capacity = Math.max(line_total, g_line_capacity * 2, 4096);
        g_line_buffer = g_device.createBuffer({
            size: g_line_capacity * 16,
            usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
            label: 'debug lines',
        });
    }
    {
        let offset = 0;
        for (const b of g_line_batches) {
            g_device.queue.writeBuffer(g_line_buffer, offset * 16, b.data.buffer,
                                       b.data.byteOffset, b.vertex_count * 16);
            b.offset = offset;
            offset += b.vertex_count;
        }
    }

    const encoder = g_device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
        colorAttachments: [{
            view: g_msaa_view,
            resolveTarget: g_context.getCurrentTexture().createView(),
            clearValue: g_clear,
            loadOp: 'clear',
            storeOp: 'discard',      // only the resolve target is kept
        }],
        depthStencilAttachment: {
            view: g_depth_view,
            depthClearValue: 1,
            depthLoadOp: 'clear',
            depthStoreOp: 'discard',
        },
    });

    let bound_pipeline = null;
    let bound_texture = -1;
    let bound_vbuf = null;
    let bound_ibuf = null;

    for (let i = 0; i < g_draws.length; ++i) {
        const d = g_draws[i];
        const pipe = pipeline_for(d.blend, d.cull, d.depth);
        if (pipe !== bound_pipeline) {
            pass.setPipeline(pipe);
            pass.setBindGroup(0, g_frame_bind);
            bound_pipeline = pipe;
            bound_texture = -1;
            bound_vbuf = null;
            bound_ibuf = null;
            ++g_stats.pipeline_switches;
        }

        pass.setBindGroup(1, g_obj_bind, [i * OBJ_STRIDE]);

        const tex = g_textures[d.texture] ?? g_textures[g_white_texture];
        if (d.texture !== bound_texture) {
            pass.setBindGroup(2, tex.bind);
            bound_texture = d.texture;
        }

        if (d.mesh.vbuf !== bound_vbuf) { pass.setVertexBuffer(0, d.mesh.vbuf); bound_vbuf = d.mesh.vbuf; }
        if (d.mesh.ibuf !== bound_ibuf) { pass.setIndexBuffer(d.mesh.ibuf, 'uint32'); bound_ibuf = d.mesh.ibuf; }

        pass.drawIndexed(d.index_count, 1, d.first_index, 0, 0);
        ++g_stats.draws;
        g_stats.tris += d.index_count / 3;
    }

    if (line_total > 0) {
        pass.setPipeline(g_line_pipeline);
        pass.setBindGroup(0, g_frame_bind);
        pass.setVertexBuffer(0, g_line_buffer);
        for (const b of g_line_batches) pass.draw(b.vertex_count, 1, b.offset, 0);
    }

    pass.end();
    g_device.queue.submit([encoder.finish()]);
}
