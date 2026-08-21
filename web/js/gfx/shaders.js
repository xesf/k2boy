// shaders.js — WGSL sources.
//
// The render model is not arbitrary: it mirrors the NetImmerse 2.3 property
// classes the original scene graph carried — NiAlphaProperty, NiZBufferProperty,
// NiFogProperty, NiShadeProperty, NiVertexColorProperty, NiTextureModeProperty.
// Everything the engine can express fits in one small shader with a handful of
// uniforms; there is no PBR or shadow pipeline to reproduce. Room lighting is
// baked into vertex colours (*MESH_VERTCOL is present in every shipped file),
// which is why the default path is unlit.

export const SCENE_WGSL = /* wgsl */ `
struct frame_uniforms {
    view_proj  : mat4x4<f32>,
    fog_color  : vec4<f32>,
    fog_params : vec4<f32>,   // start, end, enabled, time
    cam_pos    : vec4<f32>,
};

struct obj_uniforms {
    model  : mat4x4<f32>,
    tint   : vec4<f32>,
    params : vec4<f32>,       // alpha_ref, has_texture, vertex_color, lit
};

@group(0) @binding(0) var<uniform> frame : frame_uniforms;
@group(1) @binding(0) var<uniform> obj   : obj_uniforms;
@group(2) @binding(0) var samp : sampler;
@group(2) @binding(1) var tex  : texture_2d<f32>;

struct vs_out {
    @builtin(position) clip     : vec4<f32>,
    @location(0)       uv       : vec2<f32>,
    @location(1)       color    : vec4<f32>,
    @location(2)       normal   : vec3<f32>,
    @location(3)       eye_dist : f32,
};

@vertex
fn vs(
    @location(0) pos    : vec3<f32>,
    @location(1) normal : vec3<f32>,
    @location(2) uv     : vec2<f32>,
    @location(3) color  : vec4<f32>,
) -> vs_out {
    let world = obj.model * vec4<f32>(pos, 1.0);
    var o : vs_out;
    o.clip     = frame.view_proj * world;
    o.uv       = uv;
    o.color    = color;
    // Normals are per-face (the .K2A format carries none), so this is only
    // meaningful for the lit path — the greybox character and debug shapes.
    o.normal   = (obj.model * vec4<f32>(normal, 0.0)).xyz;
    o.eye_dist = length(world.xyz - frame.cam_pos.xyz);
    return o;
}

@fragment
fn fs(i : vs_out) -> @location(0) vec4<f32> {
    var base = obj.tint;

    if (obj.params.y > 0.5) {
        base = base * textureSample(tex, samp, i.uv);
    }
    if (obj.params.z > 0.5) {
        base = base * i.color;
    }

    // NiAlphaProperty's threshold path. Alpha-mapped surfaces (torch flames,
    // water sheets) rely on it; without the discard they leave a rectangular
    // halo wherever the texture is transparent.
    if (base.a < obj.params.x) {
        discard;
    }

    var rgb = base.rgb;

    if (obj.params.w > 0.5) {
        let n = normalize(i.normal);
        let l = normalize(vec3<f32>(0.35, 0.86, 0.37));
        let wrap = clamp(dot(n, l) * 0.5 + 0.5, 0.0, 1.0);   // wrapped diffuse
        rgb = rgb * (0.42 + 0.58 * wrap);
    }

    // Linear fog on eye distance, matching NiFogProperty. The [Fog] rows in the
    // .lvl are fractions of the far clip plane, resolved before they get here;
    // see docs/formats/lvl.md.
    if (frame.fog_params.z > 0.5) {
        let f = clamp((frame.fog_params.y - i.eye_dist) /
                      max(frame.fog_params.y - frame.fog_params.x, 0.001), 0.0, 1.0);
        rgb = mix(frame.fog_color.rgb, rgb, f);
    }

    return vec4<f32>(rgb, base.a);
}
`;

// Debug geometry: world-space line lists with per-vertex colour, no texture, no
// fog. Used for the collision mesh, the ledge graph and trap volumes — all of
// which need to stay readable through fog rather than fade into it.
export const LINE_WGSL = /* wgsl */ `
struct frame_uniforms {
    view_proj  : mat4x4<f32>,
    fog_color  : vec4<f32>,
    fog_params : vec4<f32>,
    cam_pos    : vec4<f32>,
};

@group(0) @binding(0) var<uniform> frame : frame_uniforms;

struct vs_out {
    @builtin(position) clip  : vec4<f32>,
    @location(0)       color : vec4<f32>,
};

@vertex
fn vs(@location(0) pos : vec3<f32>, @location(1) color : vec4<f32>) -> vs_out {
    var o : vs_out;
    o.clip  = frame.view_proj * vec4<f32>(pos, 1.0);
    o.color = color;
    return o;
}

@fragment
fn fs(i : vs_out) -> @location(0) vec4<f32> {
    return i.color;
}
`;
