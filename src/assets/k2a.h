// k2a.h — parser for the K2 ASCII geometry export (.K2A).
//
// A 3DS Max ASE dialect with K2-specific extensions. The authoritative grammar
// is the 77-entry keyword table at 0x7BCAF0 in pop3dreal.exe; see
// docs/formats/k2a.md.
//
// Deliberately has no dependency on gfx or platform: the asset layer must stay
// usable as a standalone converter and test harness.
//
// C++11.

#ifndef K2_ASSETS_K2A_H
#define K2_ASSETS_K2A_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

// *ALPHA_SOURCE in the material block. "Opacity" marks the material as
// transparent; the engine has no *MATERIAL_TRANSPARENCY use in shipped data.
enum k2a_alpha_source { K2A_ALPHA_NONE = 0, K2A_ALPHA_OPACITY };

struct k2a_material {
    std::string       name;
    std::string       bitmap;   // as written; often an absolute authoring path.
                                // May be a .ifl frame list rather than an image.
    k2a_alpha_source  alpha;

    k2a_material() : alpha(K2A_ALPHA_NONE) {}
};

// A trap instance embedded in room geometry. `name` is the *factory registry
// key* (docs/reference/trap-factory.md), not the C++ class name — three types
// differ: Audio/AudioMessage, Force/ForceTrap, Room/RoomAttributes.
// `argv` slots are documented in the shipped Traps.txt.
struct k2a_trap {
    std::string              name;
    int32_t                  id;
    float                    center[3];
    float                    bbox[6];        // min xyz, max xyz
    float                    pivot[3];
    float                    pivot_axis[3];
    float                    pivot_angle;
    std::vector<std::string> argv;
    std::string              comment;
    std::string              node;           // owning node's name; filled by the loader's caller
};

struct k2a_mesh {
    std::vector<float>   verts;      // xyz triples
    std::vector<int32_t> faces;      // abc triples, indices into verts
    std::vector<float>   tverts;     // uvw triples
    std::vector<int32_t> tfaces;     // abc triples, indices into tverts
    std::vector<float>   cverts;     // rgb triples, 0..1
    std::vector<int32_t> cfaces;     // abc triples, indices into cverts

    int32_t vert_count()  const { return int32_t(verts.size() / 3); }
    int32_t face_count()  const { return int32_t(faces.size() / 3); }
};

struct k2a_node {
    std::string name;
    std::string parent;         // "ROOT" for top level

    // The engine cannot read the row-matrix form: its keyword table misspells
    // the first row as "TM_ROWO" (letter O) while every file writes "TM_ROW0",
    // and TM_ROW3 is absent from the table entirely. Use this decomposed form.
    float pos[3];
    float rot_axis[3];
    float rot_angle;            // radians
    float scale[3];

    // A target camera (*OBJECT CAMERA, CAMERA_TYPE Target) carries a second
    // NODE_TM for "<name>.Target": where it looks. Max space, like `pos`.
    bool      has_target;
    float     target_pos[3];

    int32_t   material_ref;     // -1 when absent
    bool      has_mesh;
    k2a_mesh  mesh;
    bool      has_trap;
    k2a_trap  trap;
};

struct k2a_file {
    int32_t                   version;      // *K2_ASCIIEXPORT, e.g. 105
    std::vector<k2a_material> materials;
    std::vector<k2a_node>     nodes;

    int32_t trap_count() const {
        int32_t n = 0;
        for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].has_trap) ++n;
        return n;
    }
};

// Parses in place. `data` need not be NUL-terminated; `size` governs.
// Unknown blocks are skipped by brace counting, so files using tokens outside
// the engine's keyword table still load.
bool k2a_parse(const uint8_t* data, size_t size, k2a_file& out,
               std::string* error = 0);

// Vertex ready for upload. Mirrors gfx::vertex without depending on it.
struct k2a_vertex {
    float   x, y, z;
    float   nx, ny, nz;
    float   u, v;
    uint8_t r, g, b, a;
};

// Expands a node's indexed mesh into a draw-ready triangle list.
//
// .K2A uses separate index streams for position, UV and colour, so a GL vertex
// must be one unique (pos, uv, colour) triple. Corners are welded where those
// three agree. Normals are generated per face — the format carries none.
//
// Coordinates need no conversion: the K2 exporter already emits engine space
// (Y-up), verified against savegame actor positions. See docs/formats/k2a.md.
void k2a_build_mesh(const k2a_node& node,
                    std::vector<k2a_vertex>& verts,
                    std::vector<uint16_t>&   indices);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_K2A_H
