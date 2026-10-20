// nif.cpp — see nif.h.

#include "assets/nif.h"

#include <cstring>

namespace k2 {
namespace assets {

namespace {

struct unknown {};          // a block or key type this reader does not know

class reader {
public:
    reader(const uint8_t* d, size_t n, size_t p) : m_d(d), m_n(n), m_p(p) {}
    size_t pos() const { return m_p; }
    void need(size_t k) const { if (m_p + k > m_n) throw unknown(); }
    void skip(size_t k) { need(k); m_p += k; }
    uint8_t u8() { need(1); return m_d[m_p++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, m_d + m_p, 2); m_p += 2; return v; }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, m_d + m_p, 4); m_p += 4; return v; }
    int32_t i32() { return int32_t(u32()); }
    float f() { need(4); float v; std::memcpy(&v, m_d + m_p, 4); m_p += 4; return v; }
    void fs(float* out, int n) { for (int i = 0; i < n; ++i) out[i] = f(); }
    std::string str() {
        const int32_t n = i32();
        if (n < 0 || n > 65536) throw unknown();
        need(size_t(n));
        std::string s(reinterpret_cast<const char*>(m_d + m_p), size_t(n));
        m_p += size_t(n);
        return s;
    }
    const uint8_t* at() const { return m_d + m_p; }
private:
    const uint8_t* m_d;
    size_t m_n, m_p;
};

// Key readers, by the type number the tables at 0x8B280C (position),
// 0x8B2D68 (rotation) and 0x8B2A1C (float) are indexed with. Only the key's
// own value is kept.
void pos_keys(reader& r, int32_t n, std::vector<nif_vec_key>* out) {
    const uint32_t type = r.u32();
    for (int32_t i = 0; i < n; ++i) {
        nif_vec_key k;
        k.time = r.f();
        r.fs(k.v, 3);
        switch (type) {
        case 1: break;                              // 0x5E44D0
        case 2: r.skip(48); break;                  // + 4 tangent vectors
        case 3: r.skip(12 + 48); break;             // + TCB, 4 vectors
        default: throw unknown();
        }
        if (out) out->push_back(k);
    }
}

void float_keys(reader& r, int32_t n, uint32_t type, std::vector<nif_float_key>* out) {
    for (int32_t i = 0; i < n; ++i) {
        nif_float_key k;
        k.time = r.f();
        k.v = r.f();
        switch (type) {
        case 1: break;                              // 0x5F1050
        case 2: r.skip(8); break;                   // in, out
        case 3: r.skip(20); break;                  // TCB and the rest
        default: throw unknown();
        }
        if (out) out->push_back(k);
    }
}

void rot_keys(reader& r, int32_t n, std::vector<nif_quat_key>* out) {
    const uint32_t type = r.u32();
    for (int32_t i = 0; i < n; ++i) {
        // Common part (0x5FBBC0): time, a flag, an axis, the quaternion, two floats.
        nif_quat_key k;
        k.time = r.f();
        r.skip(4 + 12);
        r.fs(k.q, 4);
        r.skip(8);
        switch (type) {
        case 1: break;
        case 2: r.skip(16 + 4); break;              // 0x5EFD20
        case 3: r.skip(12 + 32 + 8); break;         // 0x5F0740
        case 4: {                                   // 0x5F0DD0: Euler, three float tracks
            r.skip(2);
            for (int a = 0; a < 3; ++a) {
                const int32_t c = r.i32();
                if (c > 0) float_keys(r, c, r.u32(), 0);
            }
            break;
        }
        default: throw unknown();
        }
        if (out) out->push_back(k);
    }
}

void object(reader& r, nif_block& b) {
    // NiObject (0x552BF0): its pointer, a name, then attached extra data,
    // each a type name and the object inline.
    b.id = r.u32();
    b.name = r.str();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        const std::string t = r.str();
        if (t == "NiStringExtraData") {             // 0x580F00
            r.u32();
            b.extra.push_back(r.str());
        } else if (t == "Ni3dsPropAnimExtraData") { // 0x5FC070
            r.u32(); r.u32();
        } else {
            throw unknown();
        }
    }
}

void av_object(reader& r, nif_block& b) {
    object(r, b);
    r.u8();                                         // flags
    r.fs(b.translate, 3);
    r.fs(b.rotate, 9);
    b.scale = r.f();
    r.skip(12);                                     // velocity
    const uint32_t np = r.u32();
    for (uint32_t i = 0; i < np; ++i) b.props.push_back(r.u32());
    r.u32();
    if (r.u32()) throw unknown();                   // a bounding volume: not in the effects
}

void node(reader& r, nif_block& b) {
    av_object(r, b);
    r.u32(); r.u32(); r.u8();
    const uint32_t nc = r.u32();
    for (uint32_t i = 0; i < nc; ++i) b.children.push_back(r.u32());
    const uint32_t ne = r.u32();
    for (uint32_t i = 0; i < ne; ++i) r.u32();      // effects
}

void tri_based(reader& r, nif_block& b) {
    av_object(r, b);
    const uint16_t nv = r.u16();
    if (r.u32()) { b.verts.resize(size_t(nv) * 3); r.fs(&b.verts[0], nv * 3); }
    if (r.u32()) r.skip(size_t(nv) * 12);           // normals
    r.skip(16);                                     // bound: centre, radius
    const uint16_t nt = r.u16();
    const uint16_t nuv = r.u16();
    if (r.u32()) {                                  // uvw per set
        for (uint16_t s = 0; s < nuv; ++s)
            for (uint16_t v = 0; v < nv; ++v) {
                const float u = r.f(), w = r.f();
                r.f();
                if (s == 0) { b.uvs.push_back(u); b.uvs.push_back(w); }
            }
    }
    if (r.u32()) { b.colors.resize(size_t(nv) * 4); r.fs(&b.colors[0], nv * 4); }
    if (r.u32()) r.skip(size_t(nt) * 16);           // triangle planes
    b.tris.resize(size_t(nt) * 3);                  // NiTriShape reads these; particles have none
    (void)nt;
}

void property(reader& r, nif_block& b) { object(r, b); r.u8(); }

void anim_header(reader& r, nif_block& b) {
    // 0x5FB420: a flag, four bytes, six floats - the animation's time range
    // among them (the last is its length: 3.33 s for 100 frames).
    r.u32();
    r.skip(4);
    float f[6];
    r.fs(f, 6);
    b.anim_start = f[0];
    b.anim_end = f[5];
}

void raw_image(reader& r, nif_block& b) {
    object(r, b);
    const uint32_t w = r.u32(), h = r.u32(), fmt = r.u32();   // 0x55DAD0
    if (w > 4096 || h > 4096) throw unknown();
    b.width = w; b.height = h;
    b.rgba.resize(size_t(w) * h * 4);
    if (fmt == 0) {                                 // 8-bit, 256-entry RGBA palette
        r.need(1024);
        uint8_t pal[1024];
        std::memcpy(pal, r.at(), 1024);
        r.skip(1024);
        r.need(size_t(w) * h);
        const uint8_t* ix = r.at();
        for (size_t i = 0; i < size_t(w) * h; ++i) std::memcpy(&b.rgba[i * 4], pal + ix[i] * 4, 4);
        r.skip(size_t(w) * h);
    } else if (fmt == 1) {                          // RGB
        r.need(size_t(w) * h * 3);
        const uint8_t* p = r.at();
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            b.rgba[i * 4] = p[i * 3]; b.rgba[i * 4 + 1] = p[i * 3 + 1];
            b.rgba[i * 4 + 2] = p[i * 3 + 2]; b.rgba[i * 4 + 3] = 255;
        }
        r.skip(size_t(w) * h * 3);
    } else if (fmt == 2) {                          // RGBA
        r.need(size_t(w) * h * 4);
        std::memcpy(&b.rgba[0], r.at(), size_t(w) * h * 4);
        r.skip(size_t(w) * h * 4);
    } else {
        throw unknown();
    }
}

void particle_system(reader& r, nif_block& b) {
    // Ni3dsParticleSystem (0x5E8C50).
    node(r, b);
    anim_header(r, b);
    nif_psys& p = b.psys;
    p.max_particles = r.u32();
    r.skip(2);
    r.fs(p.raw13, 13);
    r.fs(p.axis, 3);
    r.skip(24);
    r.skip(12);
    r.fs(p.raw7, 7);
    p.size = p.raw13[0]; p.speed = p.raw13[1]; p.life = p.raw13[2]; p.rate = p.raw13[5];
    p.emit_start = p.raw7[0]; p.emit_stop = p.raw7[1];
    p.spread_a = p.raw7[2]; p.spread_b = p.raw7[3];
    if (r.u32()) {                                  // a list of ids
        const uint32_t m = r.u32();
        r.skip(size_t(m) * 4);
    }
    if (r.u32()) r.skip(size_t(p.max_particles) * 56);   // saved particle state
    r.u32();
    uint32_t n = r.u32(); r.skip(size_t(n) * 4);
    n = r.u32(); r.skip(size_t(n) * 4);
    const int32_t keys = r.i32();
    if (keys) pos_keys(r, keys, 0);
    r.skip(8);
}

void block(reader& r, const std::string& t, nif_block& b) {
    b.type = t;
    if (t == "NiNode" || t == "NiBillboardNode" || t == "k2PostSceneNode" ||
        t == "k2AnimationPropertyHub" || t == "k2AnimatedTextureHub" || t == "NiMarkerNode") {
        b.kind = nif_block::NODE;
        node(r, b);
    } else if (t == "NiTriShape") {
        b.kind = nif_block::SHAPE;
        tri_based(r, b);
        for (size_t i = 0; i < b.tris.size(); ++i) b.tris[i] = r.u16();
    } else if (t == "NiAutoNormalParticles") {
        b.kind = nif_block::PARTICLES;
        tri_based(r, b);
        b.tris.clear();
        r.u32(); r.u16();                           // radius, active count
        const size_t nv = b.verts.size() / 3;
        if (r.u32()) { b.sizes.resize(nv); for (size_t i = 0; i < nv; ++i) b.sizes[i] = r.f(); }
        if (r.u32()) r.skip(nv * 12);
    } else if (t == "NiAlphaProperty") {
        b.kind = nif_block::ALPHA;
        property(r, b);
        b.alpha_enable = r.u8(); b.alpha_src = r.u32(); b.alpha_dst = r.u32();
    } else if (t == "NiMaterialProperty") {
        b.kind = nif_block::MATERIAL;
        property(r, b);
        r.fs(b.ambient, 3); r.fs(b.diffuse, 3); r.fs(b.specular, 3); r.fs(b.emissive, 3);
        b.gloss = r.f(); b.alpha = r.f();
    } else if (t == "NiTextureProperty") {
        b.kind = nif_block::TEXTURE;
        property(r, b);
        r.u32();
        const uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) b.images.push_back(r.u32());
    } else if (t == "NiTextureModeProperty") {
        property(r, b); r.skip(12);
    } else if (t == "NiVertexColorProperty") {
        b.kind = nif_block::VERTEX_COLOR;
        property(r, b); r.skip(4);
    } else if (t == "NiZBufferProperty") {         // 0x584AB0
        property(r, b); r.skip(2);
    } else if (t == "NiSpecularProperty") {        // 0x580A10
        property(r, b); r.u8();
    } else if (t == "NiShadeProperty") {           // 0x580460
        object(r, b); r.u8();
    } else if (t == "NiImage") {
        b.kind = nif_block::IMAGE;
        object(r, b);
        if (r.u8()) b.file = r.str(); else b.raw = r.u32();
        r.u32();
    } else if (t == "NiRawImageData") {
        b.kind = nif_block::RAW_IMAGE;
        raw_image(r, b);
    } else if (t == "Ni3dsAnimationNode") {
        b.kind = nif_block::ANIM_NODE;
        node(r, b);
        anim_header(r, b);
        int32_t n = r.i32();
        if (n > 0) rot_keys(r, n, &b.rot_keys);
        n = r.i32();
        if (n > 0) pos_keys(r, n, &b.pos_keys);
        n = r.i32();
        if (n > 0) float_keys(r, n, r.u32(), &b.scale_keys);
        n = r.i32();
        if (n > 0) r.skip(size_t(n) * 5);           // 0x5FB700
    } else if (t == "Ni3dsParticleSystem") {
        b.kind = nif_block::PSYS;
        particle_system(r, b);
    } else if (t == "Ni3dsAlphaAnimator") {
        object(r, b); anim_header(r, b); r.u32();
        const int32_t n = r.i32();
        const uint32_t type = r.u32();
        if (n > 0) float_keys(r, n, type, 0);
    } else if (t == "Ni3dsPathController") {
        node(r, b); anim_header(r, b);
        const int32_t npos = r.i32(), nflt = r.i32();
        r.skip(1 + 4 + 4 + 8 + 2 + 1);
        if (npos) pos_keys(r, npos, 0);
        if (nflt) float_keys(r, nflt, r.u32(), 0);
    } else if (t == "Ni3dsColorAnimator") {        // 0x5F9860
        object(r, b); anim_header(r, b); r.u32();
        r.u32();
        const int32_t n = r.i32();
        if (n > 0) pos_keys(r, n, 0); else r.u32();
    } else if (t == "NiGravity") {
        object(r, b); r.skip(12 + 24);
    } else {
        throw unknown();
    }
}

} // namespace

int nif_file::find(uint32_t id) const {
    for (size_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].id == id) return int(i);
    return -1;
}

bool nif_parse(const uint8_t* d, size_t size, nif_file& out, std::string* error) {
    out = nif_file();
    static const char magic[] = "NetImmerse File Format, Version 2.3";
    if (!d || size < sizeof(magic) || std::memcmp(d, magic, sizeof(magic) - 1) != 0) {
        if (error) *error = "not a NetImmerse 2.3 file";
        return false;
    }
    // Four header lines.
    size_t p = 0;
    for (int line = 0; line < 4 && p < size; ++line) {
        while (p < size && d[p] != '\n') ++p;
        ++p;
    }
    reader r(d, size, p);
    bool top = false;
    try {
        for (;;) {
            const std::string t = r.str();
            if (t == "Top Level Object") { top = true; continue; }
            if (t == "End Of File") { out.complete = true; break; }
            nif_block b;
            try {
                block(r, t, b);
            } catch (const unknown&) {
                out.stopped_at = t;
                break;
            }
            b.top = top;
            top = false;
            out.blocks.push_back(b);
        }
    } catch (const unknown&) {
        out.stopped_at = "<truncated>";
    }
    if (out.blocks.empty() && error) *error = "no blocks read (" + out.stopped_at + ")";
    return !out.blocks.empty();
}

} // namespace assets
} // namespace k2
