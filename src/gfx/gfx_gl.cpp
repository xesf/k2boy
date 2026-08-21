// gfx_gl.cpp — OpenGL 3.3 core implementation of gfx.h.
//
// Deliberately contains no platform headers. The context is already current
// when init() runs.
//
// One shader covers everything the original scene graph could express. That is
// not a simplification: NetImmerse 2.3 is fixed-function era, its property set
// is small (alpha / z / fog / shade / dither / vertex-colour / texture-mode),
// and .K2A geometry carries baked vertex colours. See docs/reference/class-hierarchy.md.

#include "gfx/gfx.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#error "No GL header for this platform yet"
#endif

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace k2 {
namespace gfx {

namespace {

const char* VERT_SRC =
    "#version 330 core\n"
    "layout(location=0) in vec3 a_pos;\n"
    "layout(location=1) in vec3 a_nrm;\n"
    "layout(location=2) in vec2 a_uv;\n"
    "layout(location=3) in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat4 u_model;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "out float v_viewz;\n"
    "void main() {\n"
    "    vec4 world = u_model * vec4(a_pos, 1.0);\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    v_viewz = gl_Position.w;\n"
    "}\n";

const char* FRAG_SRC =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "in float v_viewz;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int   u_has_tex;\n"
    "uniform int   u_vertex_color;\n"
    "uniform int   u_alpha_test;\n"
    "uniform float u_alpha_ref;\n"
    "uniform int   u_fog;\n"
    "uniform float u_fog_start;\n"
    "uniform float u_fog_end;\n"
    "uniform vec3  u_fog_color;\n"
    "uniform vec4  u_tint;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    vec4 c = u_tint;\n"
    "    if (u_has_tex != 0) c *= texture(u_tex, v_uv);\n"
    "    if (u_vertex_color != 0) c *= v_col;\n"
    "    if (u_alpha_test != 0 && c.a < u_alpha_ref) discard;\n"
    "    if (u_fog != 0) {\n"
    "        float f = clamp((u_fog_end - v_viewz) / max(u_fog_end - u_fog_start, 0.0001), 0.0, 1.0);\n"
    "        c.rgb = mix(u_fog_color, c.rgb, f);\n"
    "    }\n"
    "    frag = c;\n"
    "}\n";

// Fullscreen quad: separate trivial shader, keeps the movie/2D path off the mesh API.
const char* QUAD_VERT_SRC =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    v_uv = vec2(p.x, 1.0 - p.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char* QUAD_FRAG_SRC =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() { frag = texture(u_tex, v_uv); }\n";

struct mesh_rec {
    GLuint  vao, vbo, ebo;
    int32_t index_count;
    bool    dynamic;
};

std::unordered_map<mesh_id, mesh_rec> g_meshes;
mesh_id g_next_mesh = 1;

GLuint g_prog = 0, g_quad_prog = 0, g_quad_vao = 0;

struct {
    GLint mvp, model, tex, has_tex, vertex_color, alpha_test, alpha_ref;
    GLint fog, fog_start, fog_end, fog_color;
    GLint tint;
} g_u;

float g_view[16], g_proj[16], g_model[16];
float g_fog_start = 0.0f, g_fog_end = 1000.0f;
float g_fog_col[3] = {0.0f, 0.0f, 0.0f};
float g_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
texture_id g_bound_tex = INVALID_TEXTURE;

void mat4_identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_mul(float* out, const float* a, const float* b) {
    float t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            t[c * 4 + r] = s;
        }
    std::memcpy(out, t, sizeof(t));
}

GLuint compile(GLenum stage, const char* src) {
    GLuint s = glCreateShader(stage);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        std::fprintf(stderr, "k2boy: shader compile failed:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        std::fprintf(stderr, "k2boy: program link failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void setup_attribs() {
    const GLsizei stride = sizeof(vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(vertex, nx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(vertex, u));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)offsetof(vertex, r));
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool init(gl_proc_loader load) {
    (void)load;   // macOS links GL 3.3 core directly; see platform_macos.mm

    g_prog = link_program(VERT_SRC, FRAG_SRC);
    if (!g_prog) return false;

    g_quad_prog = link_program(QUAD_VERT_SRC, QUAD_FRAG_SRC);
    if (!g_quad_prog) return false;

    glGenVertexArrays(1, &g_quad_vao);   // required even for attribute-less draws

    g_u.mvp          = glGetUniformLocation(g_prog, "u_mvp");
    g_u.model        = glGetUniformLocation(g_prog, "u_model");
    g_u.tex          = glGetUniformLocation(g_prog, "u_tex");
    g_u.has_tex      = glGetUniformLocation(g_prog, "u_has_tex");
    g_u.vertex_color = glGetUniformLocation(g_prog, "u_vertex_color");
    g_u.alpha_test   = glGetUniformLocation(g_prog, "u_alpha_test");
    g_u.alpha_ref    = glGetUniformLocation(g_prog, "u_alpha_ref");
    g_u.fog          = glGetUniformLocation(g_prog, "u_fog");
    g_u.fog_start    = glGetUniformLocation(g_prog, "u_fog_start");
    g_u.fog_end      = glGetUniformLocation(g_prog, "u_fog_end");
    g_u.fog_color    = glGetUniformLocation(g_prog, "u_fog_color");
    g_u.tint         = glGetUniformLocation(g_prog, "u_tint");

    mat4_identity(g_view);
    mat4_identity(g_proj);
    mat4_identity(g_model);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Depth ordering silently degrades to submission order if the default
    // framebuffer has no depth attachment, so report what we actually got
    // rather than what the pixel format asked for.
    {
        GLint bits = -1;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH,
                                              GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,
                                              &bits);
        const GLenum e = glGetError();          // clear any error from the query
        std::printf("gl: %s | %s | depth bits %d%s\n",
                    (const char*)glGetString(GL_RENDERER),
                    (const char*)glGetString(GL_VERSION),
                    bits, e != GL_NO_ERROR ? " (query unsupported)" : "");
        std::fflush(stdout);
    }
    return true;
}

void shutdown() {
    for (std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.begin();
         it != g_meshes.end(); ++it) {
        glDeleteBuffers(1, &it->second.vbo);
        glDeleteBuffers(1, &it->second.ebo);
        glDeleteVertexArrays(1, &it->second.vao);
    }
    g_meshes.clear();
    if (g_quad_vao)  glDeleteVertexArrays(1, &g_quad_vao);
    if (g_prog)      glDeleteProgram(g_prog);
    if (g_quad_prog) glDeleteProgram(g_quad_prog);
    g_prog = g_quad_prog = g_quad_vao = 0;
}

void resize(int32_t width, int32_t height) {
    glViewport(0, 0, width, height);
}

void begin_frame() {
    glUseProgram(g_prog);
    glUniform1i(g_u.tex, 0);
    // Tint is per-draw state; reset each frame so a debug pass that sets it
    // cannot leak into the next frame's geometry.
    set_tint(1.0f, 1.0f, 1.0f, 1.0f);
}

void end_frame() {}

void set_clear_color(float r, float g, float b) { glClearColor(r, g, b, 1.0f); }

void clear(bool color, bool depth) {
    GLbitfield m = 0;
    if (color) m |= GL_COLOR_BUFFER_BIT;
    if (depth) { m |= GL_DEPTH_BUFFER_BIT; glDepthMask(GL_TRUE); }
    if (m) glClear(m);
}

// ---------------------------------------------------------------- resources

texture_id create_texture(int32_t width, int32_t height, const uint8_t* pixels) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (pixels) glGenerateMipmap(GL_TEXTURE_2D);
    return texture_id(t);
}

texture_id create_stream_texture(int32_t width, int32_t height) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture_id(t);
}

void update_texture(texture_id id, const uint8_t* pixels) {
    if (id == INVALID_TEXTURE || !pixels) return;
    glBindTexture(GL_TEXTURE_2D, GLuint(id));
    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void destroy_texture(texture_id id) {
    if (id == INVALID_TEXTURE) return;
    GLuint t = GLuint(id);
    glDeleteTextures(1, &t);
}

mesh_id create_mesh(const vertex* verts, int32_t vert_count,
                    const uint16_t* indices, int32_t index_count) {
    mesh_rec m;
    m.index_count = index_count;
    m.dynamic     = false;

    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);

    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, vert_count * sizeof(vertex), verts, GL_STATIC_DRAW);

    glGenBuffers(1, &m.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint16_t),
                 indices, GL_STATIC_DRAW);

    setup_attribs();
    glBindVertexArray(0);

    mesh_id id = g_next_mesh++;
    g_meshes[id] = m;
    return id;
}

mesh_id create_dynamic_mesh(int32_t max_verts, int32_t max_indices) {
    mesh_rec m;
    m.index_count = 0;
    m.dynamic     = true;

    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);

    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, max_verts * sizeof(vertex), NULL, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, max_indices * sizeof(uint16_t),
                 NULL, GL_DYNAMIC_DRAW);

    setup_attribs();
    glBindVertexArray(0);

    mesh_id id = g_next_mesh++;
    g_meshes[id] = m;
    return id;
}

void update_mesh(mesh_id id, const vertex* verts, int32_t vert_count,
                 const uint16_t* indices, int32_t index_count) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    glBindBuffer(GL_ARRAY_BUFFER, it->second.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vert_count * sizeof(vertex), verts);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_count * sizeof(uint16_t), indices);
    it->second.index_count = index_count;
}

void destroy_mesh(mesh_id id) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    glDeleteBuffers(1, &it->second.vbo);
    glDeleteBuffers(1, &it->second.ebo);
    glDeleteVertexArrays(1, &it->second.vao);
    g_meshes.erase(it);
}

// ---------------------------------------------------------------- drawing

void set_view_projection(const float* view16, const float* proj16) {
    std::memcpy(g_view, view16, 16 * sizeof(float));
    std::memcpy(g_proj, proj16, 16 * sizeof(float));
}

void set_model(const float* model16) {
    std::memcpy(g_model, model16, 16 * sizeof(float));
}

void set_render_state(const render_state& s) {
    switch (s.blend) {
        case BLEND_NONE:     glDisable(GL_BLEND); break;
        case BLEND_ALPHA:    glEnable(GL_BLEND);
                             glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        case BLEND_ADD:      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
        case BLEND_MODULATE: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
    }

    switch (s.cull) {
        case CULL_NONE:  glDisable(GL_CULL_FACE); break;
        case CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
        case CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
    }

    switch (s.depth) {
        case DEPTH_NONE:       glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); break;
        case DEPTH_TEST:       glEnable(GL_DEPTH_TEST);  glDepthMask(GL_FALSE); break;
        case DEPTH_TEST_WRITE: glEnable(GL_DEPTH_TEST);  glDepthMask(GL_TRUE);  break;
    }

    glUniform1i(g_u.vertex_color, s.vertex_color ? 1 : 0);
    glUniform1i(g_u.alpha_test,   s.alpha_test ? 1 : 0);
    glUniform1f(g_u.alpha_ref,    s.alpha_ref);
    glUniform1i(g_u.fog,          s.fog ? 1 : 0);
    glUniform4fv(g_u.tint, 1, g_tint);
    glUniform1f(g_u.fog_start,    g_fog_start);
    glUniform1f(g_u.fog_end,      g_fog_end);
    glUniform3f(g_u.fog_color,    g_fog_col[0], g_fog_col[1], g_fog_col[2]);

    // GL 3.3 core has no glPolygonMode(GL_FRONT) — GL_FRONT_AND_BACK only.
    glPolygonMode(GL_FRONT_AND_BACK, s.wireframe ? GL_LINE : GL_FILL);
}

void set_texture(texture_id id) {
    g_bound_tex = id;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, GLuint(id));
    glUniform1i(g_u.has_tex, id == INVALID_TEXTURE ? 0 : 1);
}

void set_tint(float r, float g, float b, float a) {
    g_tint[0] = r; g_tint[1] = g; g_tint[2] = b; g_tint[3] = a;
    // Applied immediately rather than at the next set_render_state, so a pass
    // can change colour per draw without re-submitting the whole state block.
    glUniform4fv(g_u.tint, 1, g_tint);
}

void set_fog(float start, float end, float r, float g, float b) {
    g_fog_start  = start;
    g_fog_end    = end;
    g_fog_col[0] = r;
    g_fog_col[1] = g;
    g_fog_col[2] = b;
}

void draw(mesh_id id) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    draw_range(id, 0, it->second.index_count);
}

void draw_range(mesh_id id, int32_t index_offset, int32_t index_count) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end() || index_count <= 0) return;

    float vp[16], mvp[16];
    mat4_mul(vp, g_proj, g_view);
    mat4_mul(mvp, vp, g_model);

    glUniformMatrix4fv(g_u.mvp, 1, GL_FALSE, mvp);
    glUniformMatrix4fv(g_u.model, 1, GL_FALSE, g_model);

    glBindVertexArray(it->second.vao);
    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT,
                   (void*)(intptr_t)(index_offset * sizeof(uint16_t)));
    glBindVertexArray(0);
}

void read_pixels(int32_t width, int32_t height, uint8_t* pixels) {
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // GL returns bottom-up; flip so callers get a top-down image.
    const size_t stride = size_t(width) * 4;
    std::vector<uint8_t> row(stride);
    for (int32_t y = 0; y < height / 2; ++y) {
        uint8_t* a = pixels + size_t(y) * stride;
        uint8_t* b = pixels + size_t(height - 1 - y) * stride;
        std::memcpy(&row[0], a, stride);
        std::memcpy(a, b, stride);
        std::memcpy(b, &row[0], stride);
    }
}

void draw_fullscreen_quad(texture_id id) {
    glUseProgram(g_quad_prog);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, GLuint(id));
    glUniform1i(glGetUniformLocation(g_quad_prog, "u_tex"), 0);
    glBindVertexArray(g_quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(g_prog);
}

} // namespace gfx
} // namespace k2
