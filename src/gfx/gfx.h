// gfx.h — rendering interface.
//
// MUST NOT include platform.h or any windowing header. The backend receives an
// already-current context and a GL loader function; it never creates one.
// Swapping to Metal or WebGPU means writing a second implementation of this
// header, with no change above it and none in the platform layer beyond one
// entry point returning a native surface.
//
// Target: OpenGL 3.3 core. macOS caps out at 4.1 core, and the content is
// fixed-function era, so 3.3 is ample and stays close to WebGL2.
//
// C++11. POD types only, no allocation in the interface, no exceptions.
// Naming: snake_case types and functions, UPPER_CASE enum constants.

#ifndef K2_GFX_H
#define K2_GFX_H

#include <stddef.h>
#include <stdint.h>

namespace k2 {
namespace gfx {

typedef uint32_t texture_id;
typedef uint32_t mesh_id;

const texture_id INVALID_TEXTURE = 0;
const mesh_id    INVALID_MESH    = 0;

// ---------------------------------------------------------------- render state
//
// This set is not arbitrary: it mirrors the NetImmerse 2.3 property classes the
// original scene graph carried — NiAlphaProperty, NiZBufferProperty,
// NiFogProperty, NiShadeProperty, NiDitherProperty, NiVertexColorProperty,
// NiTextureModeProperty. Everything the engine can express fits in one small
// shader with a handful of uniforms; there is no PBR or shadow pipeline to
// reproduce. See docs/reference/class-hierarchy.md.

enum blend_mode {
    BLEND_NONE = 0,     // opaque
    BLEND_ALPHA,        // src alpha, inv src alpha
    BLEND_ADD,
    BLEND_MODULATE      // "MULTALPHA" / "MULTIPLY" appear in the shipped data
};

enum cull_mode { CULL_NONE = 0, CULL_BACK, CULL_FRONT };

enum depth_mode {
    DEPTH_NONE = 0,     // no test, no write
    DEPTH_TEST,         // test, no write  (alpha-blended geometry)
    DEPTH_TEST_WRITE    // test and write  (opaque)
};

struct render_state {
    blend_mode blend;
    cull_mode  cull;
    depth_mode depth;
    bool       alpha_test;      // NiAlphaProperty threshold path
    float      alpha_ref;       // [0,1]
    bool       vertex_color;    // NiVertexColorProperty — baked lighting
    bool       fog;             // NiFogProperty
    bool       wireframe;       // NiWireframeProperty (debug)

    // Defaults matter: this used to be left uninitialised at the top of each
    // frame, and callers set only the fields they cared about. The debug
    // trigger pass sets wireframe = true, the next frame's state landed on the
    // same stack slot with wireframe still true, and the *opaque* pass drew in
    // wireframe - room textures vanished whenever trigger volumes were on.
    // A forgotten field must not be able to inherit last frame's value.
    render_state()
        : blend(BLEND_NONE), cull(CULL_NONE), depth(DEPTH_TEST_WRITE),
          alpha_test(false), alpha_ref(0.5f), vertex_color(true),
          fog(false), wireframe(false) {}
};

// ---------------------------------------------------------------- vertices
//
// One format covers the shipped content: .K2A meshes always carry vertex
// colours (*MESH_VERTCOL and *MESH_CFACE are present in every file) and a
// single UV set (*MESH_TVERT).

struct vertex {
    float   x, y, z;
    float   nx, ny, nz;
    float   u, v;
    uint8_t r, g, b, a;
};

// ---------------------------------------------------------------- lifecycle

typedef void* (*gl_proc_loader)(const char* name);

// The context must already be current. `load` resolves GL entry points —
// pass plat::gl_get_proc_address from main, keeping this header platform-free.
bool init(gl_proc_loader load);
void shutdown();

// Drawable size in pixels. Call on EVENT_RESIZE.
void resize(int32_t width, int32_t height);

void begin_frame();
void end_frame();

void set_clear_color(float r, float g, float b);
void clear(bool color, bool depth);

// ---------------------------------------------------------------- resources

// `pixels` is RGBA8, `width * height * 4` bytes. Copied; caller keeps ownership.
texture_id create_texture(int32_t width, int32_t height, const uint8_t* pixels);
void       destroy_texture(texture_id id);

// Video frames: re-uploaded every frame from the SVQ1 decoder. Same storage as
// create_texture but flagged for streaming. See docs/formats/movies.md.
texture_id create_stream_texture(int32_t width, int32_t height);
void       update_texture(texture_id id, const uint8_t* pixels);

// Static mesh upload. Indices are 16-bit: no shipped .K2A approaches 65k verts.
mesh_id create_mesh(const vertex* verts, int32_t vert_count,
                    const uint16_t* indices, int32_t index_count);
void    destroy_mesh(mesh_id id);

// Dynamic mesh, re-uploaded per frame. Skinned characters land here until the
// .skl channel semantics are resolved and skinning can move to the GPU.
mesh_id create_dynamic_mesh(int32_t max_verts, int32_t max_indices);
void    update_mesh(mesh_id id, const vertex* verts, int32_t vert_count,
                    const uint16_t* indices, int32_t index_count);

// ---------------------------------------------------------------- drawing

// Column-major 4x4, matching GL convention.
void set_view_projection(const float* view16, const float* proj16);
void set_model(const float* model16);

void set_render_state(const render_state& state);
void set_texture(texture_id id);        // INVALID_TEXTURE = untextured
void set_fog(float start, float end, float r, float g, float b);

// Flat colour multiplied into every fragment. White is the identity and is
// restored at begin_frame, so ordinary geometry never has to set it. Used by
// the debug passes (trigger volumes) to give untextured meshes a colour rather
// than rendering them black.
void set_tint(float r, float g, float b, float a);

void draw(mesh_id id);
void draw_range(mesh_id id, int32_t index_offset, int32_t index_count);

// Fullscreen textured quad in normalised device coords. Used for the movie
// player and 2D menu art; avoids threading a whole 2D path through the mesh API.
void draw_fullscreen_quad(texture_id id);

// Reads the current colour buffer back, top-row-first, RGBA8.
// `pixels` must hold width * height * 4 bytes. Debug/verification only.
void read_pixels(int32_t width, int32_t height, uint8_t* pixels);

} // namespace gfx
} // namespace k2

#endif // K2_GFX_H
