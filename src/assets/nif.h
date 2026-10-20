// nif.h — NetImmerse 2.3 scene files (.nif): the game's visual effects
// (potion sprays, residual auras, arrow trails, splashes).
//
// The layout follows the engine's own loaders, which the binary links in
// whole (NiObject__LoadBinary 0x552BF0, NiAVObject 0x551EC0, NiNode 0x54F3B0,
// NiGeometry 0x55A690, NiTriBasedGeom 0x55C820, NiTriShape 0x5788E0,
// NiParticles 0x56FD10, NiRawImageData 0x55DA40, Ni3dsAnimationNode 0x5E14F0,
// Ni3dsParticleSystem 0x5E8C50, ...). See docs/formats/nif.md.
//
// A file is a text header, then blocks: a length-prefixed type name and the
// object, written in no particular order and linked by the pointer value each
// object was saved with. "Top Level Object" precedes a root; "End Of File"
// ends the list. Coordinates are 3ds Max's (Z up), as in the .K2A rooms.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_NIF_H
#define K2_ASSETS_NIF_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

// A keyed track sampled down to its values: whatever the key type (linear,
// Bezier, TCB), the key's own value at its time.
struct nif_vec_key  { float time; float v[3]; };
struct nif_quat_key { float time; float q[4]; };   // w, x, y, z
struct nif_float_key { float time; float v; };

struct nif_psys {
    // Ni3dsParticleSystem (a 3ds Max SuperSpray / Spray). The fields we use;
    // the meaning is ours, read off the shipped effects.
    uint32_t max_particles;
    float    size, speed, life, rate;       // f13[0], [1], [2], [5]
    float    emit_start, emit_stop;         // f7[0], f7[1], seconds
    float    spread_a, spread_b;            // f7[2..3] radians, off-axis / in-plane
    float    raw13[13], raw7[7];
    float    axis[3];                       // the emit direction in node space
    nif_psys() : max_particles(0), size(0), speed(0), life(0), rate(0), emit_start(0),
                 emit_stop(0), spread_a(0), spread_b(0) {
        for (int i = 0; i < 13; ++i) raw13[i] = 0;
        for (int i = 0; i < 7; ++i) raw7[i] = 0;
        axis[0] = axis[1] = 0; axis[2] = 1;
    }
};

struct nif_block {
    enum kind_t {
        OTHER, NODE, ANIM_NODE, PSYS, SHAPE, PARTICLES,
        ALPHA, MATERIAL, TEXTURE, VERTEX_COLOR, IMAGE, RAW_IMAGE
    } kind;
    std::string type;
    std::string name;
    uint32_t    id;                         // the pointer it was saved under
    uint8_t     av_flags;                   // NiAVObject flags; bit 0 hides it (APP_CULLED)
    bool        top;                        // preceded by "Top Level Object"
    std::vector<std::string> extra;         // NiStringExtraData ("loop", ...)

    // NiAVObject
    float    translate[3], rotate[9], scale;
    std::vector<uint32_t> props, children;

    // NiTriShape / NiAutoNormalParticles
    std::vector<float>    verts;            // xyz
    std::vector<float>    uvs;              // first set, uv
    std::vector<float>    colors;           // rgba per vertex, if any
    std::vector<uint16_t> tris;
    std::vector<float>    sizes;            // particles

    // Ni3dsAnimationNode
    std::vector<nif_quat_key>  rot_keys;
    std::vector<nif_vec_key>   pos_keys;
    std::vector<nif_float_key> scale_keys;
    float    anim_start, anim_end;          // from its 32-byte header

    nif_psys psys;

    // properties
    uint8_t  alpha_enable; uint32_t alpha_src, alpha_dst;
    float    ambient[3], diffuse[3], specular[3], emissive[3], gloss, alpha;
    std::vector<uint32_t> images;           // NiTextureProperty
    std::string file;                       // NiImage, external
    uint32_t raw;                           // NiImage, internal: NiRawImageData id
    uint32_t width, height;                 // NiRawImageData, as RGBA8
    std::vector<uint8_t> rgba;

    nif_block() : kind(OTHER), id(0), av_flags(0), top(false), scale(1), anim_start(0), anim_end(0),
                  alpha_enable(0), alpha_src(6), alpha_dst(7), gloss(0), alpha(1), raw(0),
                  width(0), height(0) {
        for (int i = 0; i < 3; ++i) translate[i] = ambient[i] = specular[i] = emissive[i] = 0;
        for (int i = 0; i < 3; ++i) diffuse[i] = 1;
        for (int i = 0; i < 9; ++i) rotate[i] = (i % 4 == 0) ? 1.0f : 0.0f;
    }
};

struct nif_file {
    std::vector<nif_block> blocks;
    bool complete;                          // read through "End Of File"
    std::string stopped_at;                 // the block type that was not understood

    nif_file() : complete(false) {}
    // The block saved under `id`, or -1.
    int find(uint32_t id) const;
};

// Reads as much as it understands. False only when nothing could be read;
// `complete` says whether the whole file was.
bool nif_parse(const uint8_t* data, size_t size, nif_file& out, std::string* error = 0);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_NIF_H
