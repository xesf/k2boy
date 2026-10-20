// gfx_gxm.cpp — PlayStation Vita GXM implementation of gfx.h.
//
// The same single shader as gfx_gl.cpp (tint x texture x vertex colour,
// alpha test, linear fog), in Cg for the Vita's SGX543. GXM only runs
// compiled programs (.gxp): they are loaded from app0:shaders/ when the
// package carries them, and otherwise compiled once on the device by the
// system's own compiler module, libshacccg.suprx (ur0:data/, where Vita
// homebrew keeps it), and written to ux0:data/k2boy/shaders/ - copy those
// into platforms/vita/shaders/ and the next package carries them. See
// docs/plan/07-vita.md.
//
// This backend owns the screen: three display buffers in CDRAM, presented
// through the display queue from end_frame (platform swap_buffers is a
// no-op on the Vita). One scene per frame, begin_frame to end_frame.
//
// Memory: GPU-visible heaps carved from two memblocks, CDRAM for textures
// and the frame buffers, uncached main memory for meshes and the rings.
// The GPU reads a frame up to two frames after the CPU wrote it, so
// anything rewritten or freed while a frame may be in flight is triple
// buffered or released three frames later.

#include "gfx/gfx.h"

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/shacccg.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

namespace k2 {
namespace gfx {

namespace {

// ---------------------------------------------------------------- shaders

const char* VERT_CG =
    "void main(float3 a_pos, float3 a_nrm, float2 a_uv, float4 a_col,\n"
    "          uniform float4x4 u_mvp,\n"
    "          float4 out v_pos : POSITION,\n"
    "          float2 out v_uv : TEXCOORD0,\n"
    "          float4 out v_col : COLOR,\n"
    "          float  out v_viewz : TEXCOORD1)\n"
    "{\n"
    "    v_pos = mul(float4(a_pos, 1.0f), u_mvp);\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    v_viewz = v_pos.w;\n"
    "}\n";

// u_flags: x textured, y vertex colour, z alpha test, w alpha reference.
// u_fog: x on, y start, z end.
const char* FRAG_CG =
    "float4 main(float2 v_uv : TEXCOORD0, float4 v_col : COLOR, float v_viewz : TEXCOORD1,\n"
    "            uniform sampler2D u_tex,\n"
    "            uniform float4 u_tint,\n"
    "            uniform float4 u_flags,\n"
    "            uniform float4 u_fog,\n"
    "            uniform float4 u_fog_color) : COLOR\n"
    "{\n"
    "    float4 c = u_tint;\n"
    "    if (u_flags.x > 0.5f) c *= tex2D(u_tex, v_uv);\n"
    "    if (u_flags.y > 0.5f) c *= v_col;\n"
    "    if (u_flags.z > 0.5f && c.a < u_flags.w) discard;\n"
    "    if (u_fog.x > 0.5f) {\n"
    "        float f = saturate((u_fog.z - v_viewz) / max(u_fog.z - u_fog.y, 0.0001f));\n"
    "        c.rgb = lerp(u_fog_color.rgb, c.rgb, f);\n"
    "    }\n"
    "    return c;\n"
    "}\n";

// ---------------------------------------------------------------- memory

const int W = 960, H = 544, STRIDE = 960;
const int DISPLAY_BUFFERS = 3;
const int FRAMES_IN_FLIGHT = 3;         // display queue holds two, plus the one being built

inline size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

void* block_alloc(SceKernelMemBlockType type, size_t size, SceGxmMemoryAttribFlags attr, SceUID* uid) {
    size = align_up(size, type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? 256 * 1024 : 4 * 1024);
    *uid = sceKernelAllocMemBlock("k2 gpu", type, size, 0);
    if (*uid < 0) return 0;
    void* p = 0;
    if (sceKernelGetMemBlockBase(*uid, &p) < 0) return 0;
    if (sceGxmMapMemory(p, size, attr) < 0) { sceKernelFreeMemBlock(*uid); return 0; }
    return p;
}

// First-fit sub-allocator over one GPU-mapped block.
struct heap {
    uint8_t* base;
    size_t   size;
    std::map<size_t, size_t> free_list;     // offset -> length
    std::unordered_map<void*, size_t> used; // pointer -> length

    heap() : base(0), size(0) {}
    bool init(SceKernelMemBlockType type, size_t bytes) {
        SceUID uid;
        base = static_cast<uint8_t*>(block_alloc(type, bytes, SCE_GXM_MEMORY_ATTRIB_RW, &uid));
        if (!base) return false;
        size = bytes;
        free_list[0] = bytes;
        return true;
    }
    void* alloc(size_t bytes, size_t al) {
        bytes = align_up(bytes, 16);
        for (std::map<size_t, size_t>::iterator it = free_list.begin(); it != free_list.end(); ++it) {
            const size_t start = align_up(it->first, al);
            const size_t pad = start - it->first;
            if (it->second < pad + bytes) continue;
            const size_t off = it->first, len = it->second;
            free_list.erase(it);
            if (pad) free_list[off] = pad;
            if (len > pad + bytes) free_list[start + bytes] = len - pad - bytes;
            void* p = base + start;
            used[p] = bytes;
            return p;
        }
        return 0;
    }
    bool owns(void* p) const { return p >= base && p < base + size; }
    void release(void* p) {
        std::unordered_map<void*, size_t>::iterator u = used.find(p);
        if (u == used.end()) return;
        size_t off = size_t(static_cast<uint8_t*>(p) - base), len = u->second;
        used.erase(u);
        std::map<size_t, size_t>::iterator next = free_list.lower_bound(off);
        if (next != free_list.end() && next->first == off + len) { len += next->second; free_list.erase(next); }
        std::map<size_t, size_t>::iterator prev = free_list.lower_bound(off);
        if (prev != free_list.begin()) {
            --prev;
            if (prev->first + prev->second == off) { off = prev->first; len += prev->second; free_list.erase(prev); }
        }
        free_list[off] = len;
    }
};

heap g_vram;        // CDRAM: textures, frame buffers
heap g_ram;         // uncached main memory: meshes, overflow textures

void* gpu_alloc(size_t bytes, size_t al, bool prefer_vram) {
    void* p = prefer_vram ? g_vram.alloc(bytes, al) : 0;
    if (!p) p = g_ram.alloc(bytes, al);
    if (!p && !prefer_vram) p = g_vram.alloc(bytes, al);
    if (!p) std::printf("gxm: out of GPU memory (%u bytes)\n", unsigned(bytes));
    return p;
}

void gpu_free_now(void* p) {
    if (!p) return;
    if (g_vram.owns(p)) g_vram.release(p);
    else if (g_ram.owns(p)) g_ram.release(p);
}

// Released once the frames that may read them have finished.
std::vector<void*> g_graveyard[FRAMES_IN_FLIGHT];
unsigned g_frame = 0;

void gpu_free(void* p) { if (p) g_graveyard[g_frame % FRAMES_IN_FLIGHT].push_back(p); }

// Per-frame arena for dynamic meshes, three of them, one per frame in flight.
struct arena { uint8_t* base; size_t size, used; };
arena g_arena[FRAMES_IN_FLIGHT];
const size_t ARENA_BYTES = 4 * 1024 * 1024;

void* arena_alloc(size_t bytes) {
    arena& a = g_arena[g_frame % FRAMES_IN_FLIGHT];
    const size_t off = align_up(a.used, 16);
    if (!a.base || off + bytes > a.size) return 0;
    a.used = off + bytes;
    return a.base + off;
}

// ---------------------------------------------------------------- state

struct display_data { void* address; };

SceGxmContext*        g_ctx = 0;
SceGxmRenderTarget*   g_rt = 0;
SceGxmShaderPatcher*  g_patcher = 0;
void*                 g_display[DISPLAY_BUFFERS];
SceGxmColorSurface    g_surface[DISPLAY_BUFFERS];
SceGxmSyncObject*     g_sync[DISPLAY_BUFFERS];
SceGxmDepthStencilSurface g_depth;
int                   g_back = 0, g_front = 0;
bool                  g_in_scene = false;

SceGxmShaderPatcherId g_vid, g_fid;
const SceGxmProgram*  g_vgxp = 0;
const SceGxmProgram*  g_fgxp = 0;
SceGxmVertexProgram*  g_vprog = 0;
enum { FP_NONE, FP_ALPHA, FP_ADD, FP_MODULATE, FP_NO_COLOR, FP_COUNT };
SceGxmFragmentProgram* g_fprog[FP_COUNT];
const SceGxmProgramParameter* g_u_mvp = 0;
const SceGxmProgramParameter* g_u_tint = 0;
const SceGxmProgramParameter* g_u_flags = 0;
const SceGxmProgramParameter* g_u_fog = 0;
const SceGxmProgramParameter* g_u_fog_color = 0;

struct tex_rec {
    SceGxmTexture t;
    int32_t w, h, stride;
    void* slot[FRAMES_IN_FLIGHT];       // a stream texture rotates through these
    int   slots, cur;
};
std::unordered_map<texture_id, tex_rec> g_tex;
texture_id g_next_tex = 1;
texture_id g_white = INVALID_TEXTURE;

struct mesh_rec {
    void* vbuf;
    void* ibuf;
    int32_t index_count;
    int32_t max_verts, max_indices;
    bool dynamic;
};
std::unordered_map<mesh_id, mesh_rec> g_meshes;
mesh_id g_next_mesh = 1;
mesh_id g_screen_quad = INVALID_MESH;

float g_view[16], g_proj[16], g_model[16];
float g_fog_start = 0.0f, g_fog_end = 1000.0f;
float g_fog_col[3] = { 0, 0, 0 };
float g_tint[4] = { 1, 1, 1, 1 };
float g_clear[3] = { 0, 0, 0 };
render_state g_state;
texture_id g_bound = INVALID_TEXTURE;

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

void display_callback(const void* data) {
    const display_data* d = static_cast<const display_data*>(data);
    SceDisplayFrameBuf fb;
    std::memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    fb.base = d->address;
    fb.pitch = STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = W;
    fb.height = H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

// Shader patcher callbacks.
void* host_alloc(void*, SceSize size) { return std::malloc(size); }
void  host_free(void*, void* mem) { std::free(mem); }
void* buffer_alloc(void*, SceSize size) { return gpu_alloc(size, 16, false); }
void  buffer_free(void*, void* mem) { gpu_free_now(mem); }

void* usse_alloc(bool fragment, size_t size, unsigned* offset) {
    SceUID uid;
    size = align_up(size, 4096);
    const SceKernelMemBlockType t = SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE;
    uid = sceKernelAllocMemBlock("k2 usse", t, size, 0);
    if (uid < 0) return 0;
    void* p = 0;
    sceKernelGetMemBlockBase(uid, &p);
    const int r = fragment ? sceGxmMapFragmentUsseMemory(p, size, offset)
                           : sceGxmMapVertexUsseMemory(p, size, offset);
    return r < 0 ? 0 : p;
}
void* vusse_alloc(void*, SceSize size, unsigned* off) { return usse_alloc(false, size, off); }
void* fusse_alloc(void*, SceSize size, unsigned* off) { return usse_alloc(true, size, off); }
void  usse_free(void*, void*) {}        // shader programs live as long as the game

// ---------------------------------------------------------------- shader loading

uint8_t* read_file(const char* path, size_t& size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    size = size_t(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    uint8_t* d = static_cast<uint8_t*>(std::malloc(size));
    if (d && std::fread(d, 1, size, f) != size) { std::free(d); d = 0; }
    std::fclose(f);
    return d;
}

SceShaccCgSourceFile g_source;

SceShaccCgSourceFile* open_source(const char*, const SceShaccCgSourceLocation*,
                                  const SceShaccCgCompileOptions*, const char**) {
    return &g_source;
}

bool g_compiler_loaded = false;

bool load_compiler() {
    if (g_compiler_loaded) return true;
    static const char* paths[] = { "ur0:data/libshacccg.suprx", "ux0:data/k2boy/libshacccg.suprx", 0 };
    for (int i = 0; paths[i]; ++i) {
        if (sceKernelLoadStartModule(paths[i], 0, 0, 0, 0, 0) >= 0) {
            sceShaccCgSetDefaultAllocator(std::malloc, std::free);
            g_compiler_loaded = true;
            std::printf("gxm: shader compiler %s\n", paths[i]);
            return true;
        }
    }
    return false;
}

// Compiles a Cg source with libshacccg; the result is malloc'd.
uint8_t* compile(const char* name, const char* src, bool fragment, size_t& size) {
    if (!load_compiler()) return 0;
    g_source.fileName = name;
    g_source.text = src;
    g_source.size = SceUInt32(std::strlen(src));
    SceShaccCgCompileOptions opt;
    sceShaccCgInitializeCompileOptions(&opt);
    opt.mainSourceFile = name;
    opt.targetProfile = fragment ? SCE_SHACCCG_PROFILE_FP : SCE_SHACCCG_PROFILE_VP;
    opt.entryFunctionName = "main";
    opt.locale = SCE_SHACCCG_ENGLISH;
    opt.useFx = 1;
    opt.optimizationLevel = 3;
    opt.useFastmath = 1;
    opt.useFastint = 1;
    SceShaccCgCallbackList cb;
    sceShaccCgInitializeCallbackList(&cb, SCE_SHACCCG_TRIVIAL);
    cb.openFile = open_source;
    const SceShaccCgCompileOutput* out = sceShaccCgCompileProgram(&opt, &cb, 0);
    if (!out) return 0;
    for (int i = 0; i < out->diagnosticCount; ++i)
        std::printf("gxm: %s line %u: %s\n", name,
                    out->diagnostics[i].location ? unsigned(out->diagnostics[i].location->lineNumber) : 0u,
                    out->diagnostics[i].message);
    uint8_t* data = 0;
    if (out->programData && out->programSize) {
        size = out->programSize;
        data = static_cast<uint8_t*>(std::malloc(size));
        std::memcpy(data, out->programData, size);
    }
    sceShaccCgDestroyCompileOutput(out);
    return data;
}

// The packaged .gxp, or one compiled here and saved for packaging.
const SceGxmProgram* load_program(const char* name, const char* src, bool fragment) {
    char path[128];
    size_t size = 0;
    std::snprintf(path, sizeof(path), "app0:shaders/%s.gxp", name);
    uint8_t* d = read_file(path, size);
    if (!d) {
        std::snprintf(path, sizeof(path), "ux0:data/k2boy/shaders/%s.gxp", name);
        d = read_file(path, size);
    }
    if (!d) {
        std::snprintf(path, sizeof(path), "%s.cg", name);
        d = compile(path, src, fragment, size);
        if (d) {
            sceIoMkdir("ux0:data/k2boy", 0777);
            sceIoMkdir("ux0:data/k2boy/shaders", 0777);
            std::snprintf(path, sizeof(path), "ux0:data/k2boy/shaders/%s.gxp", name);
            if (FILE* f = std::fopen(path, "wb")) { std::fwrite(d, 1, size, f); std::fclose(f); }
            std::printf("gxm: compiled %s (%u bytes), saved to %s\n", name, unsigned(size), path);
        }
    }
    if (!d) {
        std::printf("gxm: no %s.gxp in the package and no libshacccg.suprx to compile it\n", name);
        return 0;
    }
    const SceGxmProgram* p = reinterpret_cast<const SceGxmProgram*>(d);
    if (sceGxmProgramCheck(p) < 0) { std::printf("gxm: %s is not a valid program\n", name); std::free(d); return 0; }
    return p;       // kept for the life of the game; the patcher refers to it
}

bool make_programs() {
    g_vgxp = load_program("k2_v", VERT_CG, false);
    g_fgxp = load_program("k2_f", FRAG_CG, true);
    if (!g_vgxp || !g_fgxp) return false;
    if (sceGxmShaderPatcherRegisterProgram(g_patcher, g_vgxp, &g_vid) < 0) return false;
    if (sceGxmShaderPatcherRegisterProgram(g_patcher, g_fgxp, &g_fid) < 0) return false;

    // Vertex layout: gfx::vertex. a_nrm is unused and compiled out.
    SceGxmVertexAttribute attrs[4];
    unsigned n = 0;
    struct { const char* name; unsigned offset; SceGxmAttributeFormat fmt; unsigned comps; } want[4] = {
        { "a_pos", offsetof(vertex, x),  SCE_GXM_ATTRIBUTE_FORMAT_F32, 3 },
        { "a_nrm", offsetof(vertex, nx), SCE_GXM_ATTRIBUTE_FORMAT_F32, 3 },
        { "a_uv",  offsetof(vertex, u),  SCE_GXM_ATTRIBUTE_FORMAT_F32, 2 },
        { "a_col", offsetof(vertex, r),  SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4 },
    };
    for (int i = 0; i < 4; ++i) {
        const SceGxmProgramParameter* p = sceGxmProgramFindParameterByName(g_vgxp, want[i].name);
        if (!p) continue;
        attrs[n].streamIndex = 0;
        attrs[n].offset = uint16_t(want[i].offset);
        attrs[n].format = uint8_t(want[i].fmt);
        attrs[n].componentCount = uint8_t(want[i].comps);
        attrs[n].regIndex = uint16_t(sceGxmProgramParameterGetResourceIndex(p));
        ++n;
    }
    SceGxmVertexStream stream;
    stream.stride = sizeof(vertex);
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    if (sceGxmShaderPatcherCreateVertexProgram(g_patcher, g_vid, attrs, n, &stream, 1, &g_vprog) < 0) return false;

    SceGxmBlendInfo blend[FP_COUNT];
    std::memset(blend, 0, sizeof(blend));
    for (int i = 0; i < FP_COUNT; ++i) {
        blend[i].colorMask = SCE_GXM_COLOR_MASK_ALL;
        blend[i].colorFunc = SCE_GXM_BLEND_FUNC_ADD;
        blend[i].alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    }
    blend[FP_ALPHA].colorSrc = blend[FP_ALPHA].alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blend[FP_ALPHA].colorDst = blend[FP_ALPHA].alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend[FP_ADD].colorSrc = blend[FP_ADD].alphaSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blend[FP_ADD].colorDst = blend[FP_ADD].alphaDst = SCE_GXM_BLEND_FACTOR_ONE;
    blend[FP_MODULATE].colorSrc = blend[FP_MODULATE].alphaSrc = SCE_GXM_BLEND_FACTOR_DST_COLOR;
    blend[FP_MODULATE].colorDst = blend[FP_MODULATE].alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    blend[FP_NO_COLOR].colorMask = SCE_GXM_COLOR_MASK_NONE;
    blend[FP_NO_COLOR].colorSrc = blend[FP_NO_COLOR].alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
    blend[FP_NO_COLOR].colorDst = blend[FP_NO_COLOR].alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    for (int i = 0; i < FP_COUNT; ++i)
        if (sceGxmShaderPatcherCreateFragmentProgram(g_patcher, g_fid, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                SCE_GXM_MULTISAMPLE_NONE, i == FP_NONE ? 0 : &blend[i], g_vgxp, &g_fprog[i]) < 0)
            return false;

    g_u_mvp = sceGxmProgramFindParameterByName(g_vgxp, "u_mvp");
    g_u_tint = sceGxmProgramFindParameterByName(g_fgxp, "u_tint");
    g_u_flags = sceGxmProgramFindParameterByName(g_fgxp, "u_flags");
    g_u_fog = sceGxmProgramFindParameterByName(g_fgxp, "u_fog");
    g_u_fog_color = sceGxmProgramFindParameterByName(g_fgxp, "u_fog_color");
    return g_u_mvp != 0;
}

// ---------------------------------------------------------------- drawing helpers

void apply_raster_state(const render_state& s) {
    switch (s.depth) {
        case DEPTH_NONE:
            sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
            sceGxmSetFrontDepthWriteEnable(g_ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
            break;
        case DEPTH_TEST:
            sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
            sceGxmSetFrontDepthWriteEnable(g_ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
            break;
        case DEPTH_TEST_WRITE:
            sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
            sceGxmSetFrontDepthWriteEnable(g_ctx, SCE_GXM_DEPTH_WRITE_ENABLED);
            break;
    }
    // GL's counter-clockwise front faces, seen through the y-flipped viewport.
    switch (s.cull) {
        case CULL_NONE:  sceGxmSetCullMode(g_ctx, SCE_GXM_CULL_NONE); break;
        case CULL_BACK:  sceGxmSetCullMode(g_ctx, SCE_GXM_CULL_CW); break;
        case CULL_FRONT: sceGxmSetCullMode(g_ctx, SCE_GXM_CULL_CCW); break;
    }
    const SceGxmPolygonMode pm = s.wireframe ? SCE_GXM_POLYGON_MODE_TRIANGLE_LINE
                                             : SCE_GXM_POLYGON_MODE_TRIANGLE_FILL;
    sceGxmSetFrontPolygonMode(g_ctx, pm);
}

int fragment_for(const render_state& s) {
    switch (s.blend) {
        case BLEND_ALPHA:    return FP_ALPHA;
        case BLEND_ADD:      return FP_ADD;
        case BLEND_MODULATE: return FP_MODULATE;
        default:             return FP_NONE;
    }
}

void submit(const mesh_rec& m, int32_t first, int32_t count, const float* mvp,
            int fp, const float* tint, texture_id tex, const render_state& s) {
    sceGxmSetVertexProgram(g_ctx, g_vprog);
    sceGxmSetFragmentProgram(g_ctx, g_fprog[fp]);
    void* vu = 0;
    sceGxmReserveVertexDefaultUniformBuffer(g_ctx, &vu);
    sceGxmSetUniformDataF(vu, g_u_mvp, 0, 16, mvp);
    void* fu = 0;
    sceGxmReserveFragmentDefaultUniformBuffer(g_ctx, &fu);
    const bool textured = tex != INVALID_TEXTURE && g_tex.count(tex);
    const float flags[4] = { textured ? 1.0f : 0.0f, s.vertex_color ? 1.0f : 0.0f,
                             s.alpha_test ? 1.0f : 0.0f, s.alpha_ref };
    const float fog[4] = { s.fog ? 1.0f : 0.0f, g_fog_start, g_fog_end, 0.0f };
    const float fogc[4] = { g_fog_col[0], g_fog_col[1], g_fog_col[2], 1.0f };
    if (g_u_tint) sceGxmSetUniformDataF(fu, g_u_tint, 0, 4, tint);
    if (g_u_flags) sceGxmSetUniformDataF(fu, g_u_flags, 0, 4, flags);
    if (g_u_fog) sceGxmSetUniformDataF(fu, g_u_fog, 0, 4, fog);
    if (g_u_fog_color) sceGxmSetUniformDataF(fu, g_u_fog_color, 0, 4, fogc);
    // A sampler must always have a texture: white when untextured.
    sceGxmSetFragmentTexture(g_ctx, 0, &g_tex[textured ? tex : g_white].t);
    sceGxmSetVertexStream(g_ctx, 0, m.vbuf);
    sceGxmDraw(g_ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16,
               static_cast<const uint16_t*>(m.ibuf) + first, unsigned(count));
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool init(gl_proc_loader) {
    SceGxmInitializeParams ip;
    std::memset(&ip, 0, sizeof(ip));
    ip.displayQueueMaxPendingCount = DISPLAY_BUFFERS - 1;
    ip.displayQueueCallback = display_callback;
    ip.displayQueueCallbackDataSize = sizeof(display_data);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&ip) < 0) { std::printf("gxm: initialize failed\n"); return false; }

    // As much as the system will give, within these limits: what is free
    // depends on the firmware, plugins and the newlib heap.
    {
        SceKernelFreeMemorySizeInfo fm;
        std::memset(&fm, 0, sizeof(fm));
        fm.size = sizeof(fm);
        sceKernelGetFreeMemorySize(&fm);
        std::printf("gxm: free memory: main %d MB, CDRAM %d MB, phycont %d MB\n",
                    fm.size_user >> 20, fm.size_cdram >> 20, fm.size_phycont >> 20);
        const size_t MB = 1024 * 1024;
        // Leave room for what GXM and the shader patcher still allocate as
        // their own memblocks (USSE code, the render target's driver data).
        size_t v = 96 * MB, r = 64 * MB;
        if (fm.size_cdram > 0 && size_t(fm.size_cdram) < v + 8 * MB)
            v = fm.size_cdram > int(24 * MB) ? (size_t(fm.size_cdram) - 8 * MB) & ~(MB - 1) : 16 * MB;
        if (fm.size_user > 0 && size_t(fm.size_user) < r + 16 * MB)
            r = fm.size_user > int(32 * MB) ? (size_t(fm.size_user) - 16 * MB) & ~(MB - 1) : 16 * MB;
        while (v >= 16 * MB && !g_vram.init(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, v)) v -= 8 * MB;
        while (r >= 16 * MB && !g_ram.init(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, r)) r -= 8 * MB;
        if (!g_vram.base || !g_ram.base) {
            std::printf("gxm: could not reserve GPU memory (CDRAM %s, main %s)\n",
                        g_vram.base ? "ok" : "none", g_ram.base ? "ok" : "none");
            return false;
        }
        std::printf("gxm: GPU heaps: CDRAM %u MB, main %u MB\n", unsigned(v / MB), unsigned(r / MB));
    }

    // Context and its rings.
    SceGxmContextParams cp;
    std::memset(&cp, 0, sizeof(cp));
    cp.hostMem = std::malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = gpu_alloc(SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, 4096, false);
    cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    const size_t vring = 4 * SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;      // many draws a frame
    const size_t fring = 4 * SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = gpu_alloc(vring, 4096, false);
    cp.vertexRingBufferMemSize = vring;
    cp.fragmentRingBufferMem = gpu_alloc(fring, 4096, false);
    cp.fragmentRingBufferMemSize = fring;
    unsigned usse_off = 0;
    cp.fragmentUsseRingBufferMem = usse_alloc(true, SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &usse_off);
    cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = usse_off;
    if (sceGxmCreateContext(&cp, &g_ctx) < 0) { std::printf("gxm: context failed\n"); return false; }

    SceGxmRenderTargetParams rp;
    std::memset(&rp, 0, sizeof(rp));
    rp.width = W;
    rp.height = H;
    rp.scenesPerFrame = 1;
    rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    rp.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&rp, &g_rt) < 0) { std::printf("gxm: render target failed\n"); return false; }

    for (int i = 0; i < DISPLAY_BUFFERS; ++i) {
        g_display[i] = gpu_alloc(size_t(STRIDE) * H * 4, 256 * 1024, true);
        if (!g_display[i]) return false;
        std::memset(g_display[i], 0, size_t(STRIDE) * H * 4);
        sceGxmColorSurfaceInit(&g_surface[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
                               SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
                               W, H, STRIDE, g_display[i]);
        sceGxmSyncObjectCreate(&g_sync[i]);
    }
    const unsigned dw = unsigned(align_up(W, SCE_GXM_TILE_SIZEX));
    const unsigned dh = unsigned(align_up(H, SCE_GXM_TILE_SIZEY));
    void* depth = gpu_alloc(size_t(dw) * dh * 4, 4096, true);
    sceGxmDepthStencilSurfaceInit(&g_depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
                                  SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, dw, depth, 0);
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&g_depth, 1.0f);

    // Shader patcher.
    SceGxmShaderPatcherParams pp;
    std::memset(&pp, 0, sizeof(pp));
    pp.hostAllocCallback = host_alloc;
    pp.hostFreeCallback = host_free;
    pp.bufferAllocCallback = buffer_alloc;
    pp.bufferFreeCallback = buffer_free;
    pp.vertexUsseAllocCallback = vusse_alloc;
    pp.vertexUsseFreeCallback = usse_free;
    pp.fragmentUsseAllocCallback = fusse_alloc;
    pp.fragmentUsseFreeCallback = usse_free;
    if (sceGxmShaderPatcherCreate(&pp, &g_patcher) < 0) { std::printf("gxm: patcher failed\n"); return false; }
    if (!make_programs()) { std::printf("gxm: shader programs failed\n"); return false; }

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        g_arena[i].base = static_cast<uint8_t*>(gpu_alloc(ARENA_BYTES, 4096, false));
        g_arena[i].size = g_arena[i].base ? ARENA_BYTES : 0;
        g_arena[i].used = 0;
    }

    const uint8_t white[4] = { 255, 255, 255, 255 };
    g_white = create_texture(1, 1, white);

    // A screen-covering quad at the far plane, for clear() and movies.
    vertex q[4];
    std::memset(q, 0, sizeof(q));
    const float xy[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int k = 0; k < 4; ++k) {
        q[k].x = xy[k][0]; q[k].y = xy[k][1]; q[k].z = 1.0f;
        q[k].u = (xy[k][0] + 1) * 0.5f; q[k].v = 1.0f - (xy[k][1] + 1) * 0.5f;
        q[k].r = q[k].g = q[k].b = q[k].a = 255;
    }
    const uint16_t qi[6] = { 0, 1, 2, 0, 2, 3 };
    g_screen_quad = create_mesh(q, 4, qi, 6);

    mat4_identity(g_view);
    mat4_identity(g_proj);
    mat4_identity(g_model);
    std::printf("gxm: ready, %dx%d\n", W, H);
    return true;
}

void shutdown() {
    if (!g_ctx) return;
    if (g_in_scene) { sceGxmEndScene(g_ctx, 0, 0); g_in_scene = false; }
    sceGxmFinish(g_ctx);
    sceGxmDisplayQueueFinish();
}

void resize(int32_t, int32_t) {}            // the screen is always 960x544

void begin_frame() {
    if (!g_ctx || g_in_scene) return;
    // The arena and graveyard of the frame three back are free again.
    std::vector<void*>& dead = g_graveyard[g_frame % FRAMES_IN_FLIGHT];
    for (size_t i = 0; i < dead.size(); ++i) gpu_free_now(dead[i]);
    dead.clear();
    g_arena[g_frame % FRAMES_IN_FLIGHT].used = 0;

    sceGxmBeginScene(g_ctx, 0, g_rt, 0, 0, g_sync[g_back], &g_surface[g_back], &g_depth);
    g_in_scene = true;
    // GL's NDC: y up, z -1..1.
    sceGxmSetViewport(g_ctx, W * 0.5f, W * 0.5f, H * 0.5f, -H * 0.5f, 0.5f, 0.5f);
    sceGxmSetRegionClip(g_ctx, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, W - 1, H - 1);
    set_tint(1, 1, 1, 1);
    apply_raster_state(g_state);
}

void end_frame() {
    if (!g_in_scene) return;
    sceGxmEndScene(g_ctx, 0, 0);
    g_in_scene = false;
    sceGxmPadHeartbeat(&g_surface[g_back], g_sync[g_back]);
    display_data d;
    d.address = g_display[g_back];
    sceGxmDisplayQueueAddEntry(g_sync[g_front], g_sync[g_back], &d);
    g_front = g_back;
    g_back = (g_back + 1) % DISPLAY_BUFFERS;
    ++g_frame;
}

void set_clear_color(float r, float g, float b) {
    g_clear[0] = r; g_clear[1] = g; g_clear[2] = b;
}

// GXM has no clear: a quad at the far plane, depth always passing and written.
void clear(bool color, bool depth) {
    if (!g_in_scene || (!color && !depth)) return;
    render_state s;
    s.depth = DEPTH_NONE;
    s.vertex_color = false;
    apply_raster_state(s);
    sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(g_ctx, depth ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
    float ident[16];
    mat4_identity(ident);
    const float col[4] = { g_clear[0], g_clear[1], g_clear[2], 1.0f };
    submit(g_meshes[g_screen_quad], 0, 6, ident, color ? FP_NONE : FP_NO_COLOR, col, INVALID_TEXTURE, s);
    apply_raster_state(g_state);
}

// ---------------------------------------------------------------- resources

namespace {
bool pow2(int32_t v) { return v > 0 && (v & (v - 1)) == 0; }

void fill_texture(tex_rec& r, void* mem, const uint8_t* pixels) {
    uint8_t* dst = static_cast<uint8_t*>(mem);
    if (pixels)
        for (int32_t y = 0; y < r.h; ++y)
            std::memcpy(dst + size_t(y) * r.stride * 4, pixels + size_t(y) * r.w * 4, size_t(r.w) * 4);
    else
        std::memset(dst, 0, size_t(r.stride) * r.h * 4);
}

texture_id make_texture(int32_t w, int32_t h, const uint8_t* pixels, int slots) {
    tex_rec r;
    std::memset(&r, 0, sizeof(r));
    r.w = w; r.h = h;
    r.stride = int32_t(align_up(size_t(w), 8));     // linear textures: rows of 8-pixel multiples
    r.slots = slots;
    for (int i = 0; i < slots; ++i) {
        r.slot[i] = gpu_alloc(size_t(r.stride) * h * 4, 256, true);
        if (!r.slot[i]) { for (int k = 0; k < i; ++k) gpu_free_now(r.slot[k]); return INVALID_TEXTURE; }
        fill_texture(r, r.slot[i], pixels);
    }
    sceGxmTextureInitLinear(&r.t, r.slot[0], SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, unsigned(w), unsigned(h), 0);
    sceGxmTextureSetMinFilter(&r.t, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&r.t, SCE_GXM_TEXTURE_FILTER_LINEAR);
    // Repeat needs power-of-two sides; the rest clamp (menu art, movies).
    const SceGxmTextureAddrMode mode = (slots == 1 && pow2(w) && pow2(h)) ? SCE_GXM_TEXTURE_ADDR_REPEAT
                                                                          : SCE_GXM_TEXTURE_ADDR_CLAMP;
    sceGxmTextureSetUAddrMode(&r.t, mode);
    sceGxmTextureSetVAddrMode(&r.t, mode);
    const texture_id id = g_next_tex++;
    g_tex[id] = r;
    return id;
}
} // namespace

texture_id create_texture(int32_t width, int32_t height, const uint8_t* pixels) {
    return make_texture(width, height, pixels, 1);
}

texture_id create_stream_texture(int32_t width, int32_t height) {
    return make_texture(width, height, 0, FRAMES_IN_FLIGHT);
}

// A stream texture moves to its next slot, one the GPU is done with.
void update_texture(texture_id id, const uint8_t* pixels) {
    std::unordered_map<texture_id, tex_rec>::iterator it = g_tex.find(id);
    if (it == g_tex.end() || !pixels) return;
    tex_rec& r = it->second;
    if (r.slots > 1) r.cur = (r.cur + 1) % r.slots;
    fill_texture(r, r.slot[r.cur], pixels);
    sceGxmTextureSetData(&r.t, r.slot[r.cur]);
}

void destroy_texture(texture_id id) {
    std::unordered_map<texture_id, tex_rec>::iterator it = g_tex.find(id);
    if (it == g_tex.end()) return;
    for (int i = 0; i < it->second.slots; ++i) gpu_free(it->second.slot[i]);
    g_tex.erase(it);
}

mesh_id create_mesh(const vertex* verts, int32_t vert_count,
                    const uint16_t* indices, int32_t index_count) {
    mesh_rec m;
    m.vbuf = gpu_alloc(size_t(vert_count) * sizeof(vertex), 16, false);
    m.ibuf = gpu_alloc(size_t(index_count) * sizeof(uint16_t), 16, false);
    if (!m.vbuf || !m.ibuf) { gpu_free_now(m.vbuf); gpu_free_now(m.ibuf); return INVALID_MESH; }
    std::memcpy(m.vbuf, verts, size_t(vert_count) * sizeof(vertex));
    std::memcpy(m.ibuf, indices, size_t(index_count) * sizeof(uint16_t));
    m.index_count = index_count;
    m.max_verts = vert_count;
    m.max_indices = index_count;
    m.dynamic = false;
    const mesh_id id = g_next_mesh++;
    g_meshes[id] = m;
    return id;
}

mesh_id create_dynamic_mesh(int32_t max_verts, int32_t max_indices) {
    mesh_rec m;
    m.vbuf = m.ibuf = 0;
    m.index_count = 0;
    m.max_verts = max_verts;
    m.max_indices = max_indices;
    m.dynamic = true;
    const mesh_id id = g_next_mesh++;
    g_meshes[id] = m;
    return id;
}

// Dynamic data goes to this frame's arena: an update while earlier draws
// of the same frame are still to run must not overwrite what they read.
void update_mesh(mesh_id id, const vertex* verts, int32_t vert_count,
                 const uint16_t* indices, int32_t index_count) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    mesh_rec& m = it->second;
    void* v = arena_alloc(size_t(vert_count) * sizeof(vertex));
    void* i = arena_alloc(size_t(index_count) * sizeof(uint16_t));
    if (!v || !i) { m.index_count = 0; return; }
    std::memcpy(v, verts, size_t(vert_count) * sizeof(vertex));
    std::memcpy(i, indices, size_t(index_count) * sizeof(uint16_t));
    m.vbuf = v;
    m.ibuf = i;
    m.index_count = index_count;
}

void destroy_mesh(mesh_id id) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    if (!it->second.dynamic) { gpu_free(it->second.vbuf); gpu_free(it->second.ibuf); }
    g_meshes.erase(it);
}

// ---------------------------------------------------------------- drawing

void set_view_projection(const float* view16, const float* proj16) {
    std::memcpy(g_view, view16, 16 * sizeof(float));
    std::memcpy(g_proj, proj16, 16 * sizeof(float));
}

void set_model(const float* model16) { std::memcpy(g_model, model16, 16 * sizeof(float)); }

void set_render_state(const render_state& s) {
    g_state = s;
    if (g_in_scene) apply_raster_state(s);
}

void set_texture(texture_id id) { g_bound = id; }

void set_tint(float r, float g, float b, float a) {
    g_tint[0] = r; g_tint[1] = g; g_tint[2] = b; g_tint[3] = a;
}

void set_fog(float start, float end, float r, float g, float b) {
    g_fog_start = start;
    g_fog_end = end;
    g_fog_col[0] = r; g_fog_col[1] = g; g_fog_col[2] = b;
}

void draw(mesh_id id) {
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end()) return;
    draw_range(id, 0, it->second.index_count);
}

void draw_range(mesh_id id, int32_t index_offset, int32_t index_count) {
    if (!g_in_scene || index_count <= 0) return;
    std::unordered_map<mesh_id, mesh_rec>::iterator it = g_meshes.find(id);
    if (it == g_meshes.end() || !it->second.vbuf) return;
    float vp[16], mvp[16];
    mat4_mul(vp, g_proj, g_view);
    mat4_mul(mvp, vp, g_model);
    submit(it->second, index_offset, index_count, mvp, fragment_for(g_state), g_tint, g_bound, g_state);
}

void draw_fullscreen_quad(texture_id id) {
    if (!g_in_scene) return;
    render_state s;
    s.depth = DEPTH_NONE;
    s.vertex_color = false;
    apply_raster_state(s);
    float ident[16];
    mat4_identity(ident);
    const float white[4] = { 1, 1, 1, 1 };
    submit(g_meshes[g_screen_quad], 0, 6, ident, FP_NONE, white, id, s);
    apply_raster_state(g_state);
}

// The last finished frame, top row first. Debug only.
void read_pixels(int32_t width, int32_t height, uint8_t* pixels) {
    if (!g_ctx) return;
    sceGxmFinish(g_ctx);
    const uint8_t* src = static_cast<const uint8_t*>(g_display[g_front]);
    for (int32_t y = 0; y < height && y < H; ++y)
        std::memcpy(pixels + size_t(y) * width * 4, src + size_t(y) * STRIDE * 4,
                    size_t(width < W ? width : W) * 4);
}

} // namespace gfx
} // namespace k2
