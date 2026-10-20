// k2a.cpp — see k2a.h.

#include "assets/k2a.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace k2 {
namespace assets {

namespace {

// ---------------------------------------------------------------- tokenizer

struct tokenizer {
    const char* p;
    const char* end;
    std::string tok;        // current token, quotes stripped
    bool        quoted;

    tokenizer(const uint8_t* data, size_t size)
        : p(reinterpret_cast<const char*>(data)),
          end(reinterpret_cast<const char*>(data) + size),
          quoted(false) {}

    bool next() {
        quoted = false;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
        if (p >= end) { tok.clear(); return false; }

        if (*p == '"') {
            ++p;
            const char* s = p;
            while (p < end && *p != '"') ++p;
            tok.assign(s, size_t(p - s));
            if (p < end) ++p;
            quoted = true;
            return true;
        }

        if (*p == '{' || *p == '}') {
            tok.assign(p, 1);
            ++p;
            return true;
        }

        const char* s = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
               *p != '{' && *p != '}' && *p != '"')
            ++p;
        tok.assign(s, size_t(p - s));
        return true;
    }

    // Peeks without consuming.
    bool peek(std::string& out) {
        const char* sp = p;
        std::string st = tok;
        bool sq = quoted;
        bool ok = next();
        if (ok) out = tok;
        p = sp; tok = st; quoted = sq;
        return ok;
    }
};

// "12:" and "12" both yield 12. Handles the *MESH_FACE "0: A: 2 B: 0 C: 1" form.
int32_t to_int(const std::string& s) { return int32_t(std::strtol(s.c_str(), 0, 10)); }
float   to_float(const std::string& s) { return float(std::strtod(s.c_str(), 0)); }

bool is_open(const std::string& s)  { return s == "{"; }
bool is_close(const std::string& s) { return s == "}"; }

// Consumes a balanced { ... } block, assuming the '{' has not been read yet.
// Used to skip anything outside the engine's keyword table.
void skip_block(tokenizer& t) {
    std::string peeked;
    if (!t.peek(peeked) || !is_open(peeked)) return;   // no block to skip
    t.next();                                          // consume '{'
    int depth = 1;
    while (depth > 0 && t.next()) {
        if (!t.quoted && is_open(t.tok))  ++depth;
        else if (!t.quoted && is_close(t.tok)) --depth;
    }
}

void read3(tokenizer& t, float* v) {
    for (int i = 0; i < 3; ++i) { t.next(); v[i] = to_float(t.tok); }
}

// ---------------------------------------------------------------- sections

// A material body contains nested *MAP_<CHANNEL> blocks, each with its own
// *BITMAP and *ALPHA_SOURCE. Both fields therefore have to be read *per channel*
// rather than by scanning the whole material:
//
//   * `bitmap` must come from *MAP_DIFFUSE. Taking the last *BITMAP seen picked
//     up the opacity or specular map instead whenever a material had one.
//   * transparency is signalled by the presence of a *MAP_OPACITY block (or a
//     non-zero *MATERIAL_TRANSPARENCY), *not* by *ALPHA_SOURCE. ALPHA_SOURCE
//     only says where a map's alpha comes from; a plain wall's diffuse map
//     routinely says "Opacity" while the material is fully opaque.
//
// Measured on the prisonfix rooms: prison3 has 105 *ALPHA_SOURCE Opacity lines
// and **zero** *MAP_OPACITY blocks. Keying on ALPHA_SOURCE flagged 105 of its
// 131 materials transparent, which pushed most of the room into the
// depth-write-off pass and destroyed its draw order.
void parse_material(tokenizer& t, k2a_material& m) {
    int depth = 1;                       // '{' already consumed
    std::string pending;                 // last *MAP_* token seen
    std::string channel;                 // channel whose block we are inside
    int channel_depth = -1;
    bool have_diffuse_bitmap = false;

    while (depth > 0 && t.next()) {
        if (!t.quoted && is_open(t.tok)) {
            ++depth;
            if (pending.size() > 5 && pending.compare(0, 5, "*MAP_") == 0) {
                channel = pending;
                channel_depth = depth;
                // An opacity channel is the actual signal for alpha blending.
                if (channel == "*MAP_OPACITY") m.alpha = K2A_ALPHA_OPACITY;
            }
            pending.clear();
            continue;
        }
        if (!t.quoted && is_close(t.tok)) {
            if (depth == channel_depth) { channel.clear(); channel_depth = -1; }
            --depth;
            pending.clear();
            continue;
        }
        if (t.quoted) { pending.clear(); continue; }

        if (t.tok == "*MATERIAL_NAME") { t.next(); m.name = t.tok; }
        else if (t.tok == "*MATERIAL_TRANSPARENCY") {
            t.next();
            if (to_float(t.tok) > 0.0f) m.alpha = K2A_ALPHA_OPACITY;
        }
        else if (t.tok == "*ALPHA_SOURCE") {
            t.next();
            if (channel == "*MAP_DIFFUSE") {
                if      (t.tok == "Opacity") m.diffuse_alpha = k2a_material::DIFFUSE_ALPHA_OPACITY;
                else if (t.tok == "Chroma")  m.diffuse_alpha = k2a_material::DIFFUSE_ALPHA_CHROMA;
            }
        }
        else if (t.tok == "*BITMAP") {
            t.next();
            // Prefer the diffuse map; fall back to the first bitmap seen so a
            // material with no explicit *MAP_DIFFUSE still gets a texture.
            if (channel == "*MAP_DIFFUSE") { m.bitmap = t.tok; have_diffuse_bitmap = true; }
            else if (!have_diffuse_bitmap && m.bitmap.empty()) m.bitmap = t.tok;
        }
        else {
            pending = t.tok;
            continue;
        }
        pending.clear();
    }
}

void parse_material_list(tokenizer& t, std::vector<k2a_material>& out) {
    int depth = 1;
    while (depth > 0 && t.next()) {
        if (!t.quoted && is_close(t.tok)) { --depth; continue; }
        if (t.quoted) continue;

        if (t.tok == "*MATERIAL") {
            t.next();                               // index
            int32_t idx = to_int(t.tok);
            t.next();                               // '{'
            if (!is_open(t.tok)) continue;
            k2a_material m;
            parse_material(t, m);
            if (idx >= 0) {
                if (out.size() <= size_t(idx)) out.resize(size_t(idx) + 1);
                out[size_t(idx)] = m;
            }
        } else if (is_open(t.tok)) {
            ++depth;
        }
    }
}

void parse_node_tm(tokenizer& t, k2a_node& n) {
    int depth = 1;
    while (depth > 0 && t.next()) {
        if (!t.quoted && is_open(t.tok))  { ++depth; continue; }
        if (!t.quoted && is_close(t.tok)) { --depth; continue; }
        if (t.quoted) continue;

        // TM_ROW0..3 are deliberately ignored: the engine cannot read them
        // (its keyword table misspells row 0 as TM_ROWO and lacks row 3).
        if      (t.tok == "*TM_POS")      read3(t, n.pos);
        else if (t.tok == "*TM_ROTAXIS")  read3(t, n.rot_axis);
        else if (t.tok == "*TM_SCALE")    read3(t, n.scale);
        else if (t.tok == "*TM_ROTANGLE") { t.next(); n.rot_angle = to_float(t.tok); }
    }
}

void parse_mesh(tokenizer& t, k2a_mesh& m) {
    int depth = 1;
    while (depth > 0 && t.next()) {
        if (!t.quoted && is_open(t.tok))  { ++depth; continue; }
        if (!t.quoted && is_close(t.tok)) { --depth; continue; }
        if (t.quoted) continue;

        if (t.tok == "*MESH_VERTEX") {
            t.next();                                   // index
            float v[3]; read3(t, v);
            m.verts.push_back(v[0]); m.verts.push_back(v[1]); m.verts.push_back(v[2]);
        } else if (t.tok == "*MESH_FACE") {
            // "*MESH_FACE 0: A: 2 B: 0 C: 1"
            t.next();                                   // "0:"
            for (int i = 0; i < 3; ++i) {
                t.next();                               // "A:" / "B:" / "C:"
                t.next();                               // value
                m.faces.push_back(to_int(t.tok));
            }
        } else if (t.tok == "*MESH_TVERT") {
            t.next();
            float v[3]; read3(t, v);
            m.tverts.push_back(v[0]); m.tverts.push_back(v[1]); m.tverts.push_back(v[2]);
        } else if (t.tok == "*MESH_TFACE") {
            t.next();
            for (int i = 0; i < 3; ++i) { t.next(); m.tfaces.push_back(to_int(t.tok)); }
        } else if (t.tok == "*MESH_VERTCOL") {
            t.next();
            float v[3]; read3(t, v);
            m.cverts.push_back(v[0]); m.cverts.push_back(v[1]); m.cverts.push_back(v[2]);
        } else if (t.tok == "*MESH_CFACE") {
            t.next();
            for (int i = 0; i < 3; ++i) { t.next(); m.cfaces.push_back(to_int(t.tok)); }
        }
    }
}

// Trap fields are flat (no braces), so they are handled inline by the object
// parser below rather than in a sub-parser.

void parse_object(tokenizer& t, k2a_node& n) {
    int depth = 1;                       // '{' already consumed
    bool seen_tm = false;
    while (depth > 0 && t.next()) {
        if (!t.quoted && is_close(t.tok)) { --depth; continue; }
        if (t.quoted) continue;

        if (t.tok == "*NODE_NAME")        { t.next(); n.name = t.tok; }
        else if (t.tok == "*NODE_PARENT") { t.next(); n.parent = t.tok; }
        else if (t.tok == "*MATERIAL_REF"){ t.next(); n.material_ref = to_int(t.tok); }
        else if (t.tok == "*NODE_TM")     {
            t.next();
            if (!is_open(t.tok)) continue;
            if (!seen_tm) { parse_node_tm(t, n); seen_tm = true; continue; }
            // The second transform of a target camera is its target's.
            k2a_node tgt;
            std::memset(tgt.pos, 0, sizeof(tgt.pos));
            parse_node_tm(t, tgt);
            for (int k = 0; k < 3; ++k) n.target_pos[k] = tgt.pos[k];
            n.has_target = true;
        }
        else if (t.tok == "*MESH")        { t.next(); if (is_open(t.tok)) { n.has_mesh = true; parse_mesh(t, n.mesh); } }

        // --- trap block: flat fields, no braces
        else if (t.tok == "*K2_TRAP_BLOCK")        { n.has_trap = true; }
        else if (t.tok == "*K2_TRAP_CENTER")       read3(t, n.trap.center);
        else if (t.tok == "*K2_TRAP_PIVOT")        read3(t, n.trap.pivot);
        else if (t.tok == "*K2_TRAP_PIVOT_AXIS")   read3(t, n.trap.pivot_axis);
        else if (t.tok == "*K2_TRAP_PIVOT_ANGLE")  { t.next(); n.trap.pivot_angle = to_float(t.tok); }
        else if (t.tok == "*K2_TRAP_BBOX")         { for (int i = 0; i < 6; ++i) { t.next(); n.trap.bbox[i] = to_float(t.tok); } }
        else if (t.tok == "*K2_TRAP_NAME")         { t.next(); n.trap.name = t.tok; }
        else if (t.tok == "*K2_TRAP_ID")           { t.next(); n.trap.id = to_int(t.tok); }
        else if (t.tok == "*K2_TRAP_COMMENT")      { t.next(); n.trap.comment = t.tok; }
        else if (t.tok == "*K2_TRAP_ARGC")         { t.next(); n.trap.argv.reserve(size_t(to_int(t.tok))); }
        else if (t.tok.compare(0, 15, "*K2_TRAP_ARGV") == 0 ||
                 (t.tok.size() > 13 && t.tok.compare(0, 13, "*K2_TRAP_ARGV") == 0)) {
            // The slot index is appended to the token: *K2_TRAP_ARGV0, ARGV1, ...
            int32_t slot = to_int(t.tok.substr(13));
            t.next();
            if (slot >= 0) {
                if (n.trap.argv.size() <= size_t(slot)) n.trap.argv.resize(size_t(slot) + 1);
                n.trap.argv[size_t(slot)] = t.tok;
            }
        }
        else if (is_open(t.tok)) { ++depth; }
    }
}

} // namespace

// ---------------------------------------------------------------- entry point

bool k2a_parse(const uint8_t* data, size_t size, k2a_file& out, std::string* error) {
    out.version = 0;
    out.materials.clear();
    out.nodes.clear();

    if (!data || size < 16) {
        if (error) *error = "empty or too small";
        return false;
    }
    if (std::memcmp(data, "*K2_ASCIIEXPORT", 15) != 0) {
        if (error) *error = "missing *K2_ASCIIEXPORT header";
        return false;
    }

    tokenizer t(data, size);
    while (t.next()) {
        if (t.quoted) continue;

        if (t.tok == "*K2_ASCIIEXPORT") {
            t.next();
            out.version = to_int(t.tok);
        } else if (t.tok == "*MATERIAL_LIST") {
            t.next();
            if (is_open(t.tok)) parse_material_list(t, out.materials);
        } else if (t.tok == "*OBJECT") {
            t.next();                       // "GEOM" / "SHAPE" / "HELPER" / "CAMERA"
            std::string kind = t.tok;
            t.next();
            if (!is_open(t.tok)) continue;

            k2a_node n;
            n.parent        = "ROOT";
            n.rot_angle     = 0.0f;
            n.material_ref  = -1;
            n.has_mesh      = false;
            n.has_trap      = false;
            n.has_target    = false;
            std::memset(n.target_pos, 0, sizeof(n.target_pos));
            n.trap.id       = -1;
            n.trap.pivot_angle = 0.0f;
            std::memset(n.pos, 0, sizeof(n.pos));
            std::memset(n.rot_axis, 0, sizeof(n.rot_axis));
            std::memset(n.trap.center, 0, sizeof(n.trap.center));
            std::memset(n.trap.bbox, 0, sizeof(n.trap.bbox));
            std::memset(n.trap.pivot, 0, sizeof(n.trap.pivot));
            std::memset(n.trap.pivot_axis, 0, sizeof(n.trap.pivot_axis));
            n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;

            parse_object(t, n);
            out.nodes.push_back(n);
        } else if (t.tok == "*SCENE" || t.tok == "*GROUP") {
            skip_block(t);
        }
    }

    return true;
}

// ---------------------------------------------------------------- mesh build

void k2a_build_mesh(const k2a_node& node,
                    std::vector<k2a_vertex>& verts,
                    std::vector<uint16_t>&   indices) {
    verts.clear();
    indices.clear();

    const k2a_mesh& m = node.mesh;
    const int32_t faces = m.face_count();
    if (faces <= 0) return;

    const bool have_uv  = !m.tfaces.empty() && m.tfaces.size() >= size_t(faces * 3);
    const bool have_col = !m.cfaces.empty() && m.cfaces.size() >= size_t(faces * 3);

    // Weld corners that agree on all three streams.
    std::map<uint64_t, uint16_t> weld;

    for (int32_t f = 0; f < faces; ++f) {
        // Face normal, since the format carries none.
        float n[3] = {0.0f, 1.0f, 0.0f};
        {
            const int32_t ia = m.faces[size_t(f * 3 + 0)];
            const int32_t ib = m.faces[size_t(f * 3 + 1)];
            const int32_t ic = m.faces[size_t(f * 3 + 2)];
            if (ia >= 0 && ib >= 0 && ic >= 0 &&
                size_t(ic * 3 + 2) < m.verts.size()) {
                const float* a = &m.verts[size_t(ia * 3)];
                const float* b = &m.verts[size_t(ib * 3)];
                const float* c = &m.verts[size_t(ic * 3)];
                const float e0[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
                const float e1[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
                float cr[3] = {e0[1]*e1[2] - e0[2]*e1[1],
                               e0[2]*e1[0] - e0[0]*e1[2],
                               e0[0]*e1[1] - e0[1]*e1[0]};
                const float len = std::sqrt(cr[0]*cr[0] + cr[1]*cr[1] + cr[2]*cr[2]);
                if (len > 1e-8f) { n[0]=cr[0]/len; n[1]=cr[1]/len; n[2]=cr[2]/len; }
            }
        }

        for (int32_t k = 0; k < 3; ++k) {
            const int32_t vi = m.faces[size_t(f * 3 + k)];
            const int32_t ti = have_uv  ? m.tfaces[size_t(f * 3 + k)] : -1;
            const int32_t ci = have_col ? m.cfaces[size_t(f * 3 + k)] : -1;
            if (vi < 0 || size_t(vi * 3 + 2) >= m.verts.size()) continue;

            const uint64_t key = (uint64_t(uint32_t(vi)) << 42) ^
                                 (uint64_t(uint32_t(ti)) << 21) ^
                                  uint64_t(uint32_t(ci));

            std::map<uint64_t, uint16_t>::const_iterator it = weld.find(key);
            if (it != weld.end()) { indices.push_back(it->second); continue; }

            k2a_vertex v;
            v.x = m.verts[size_t(vi * 3 + 0)];
            v.y = m.verts[size_t(vi * 3 + 1)];
            v.z = m.verts[size_t(vi * 3 + 2)];
            v.nx = n[0]; v.ny = n[1]; v.nz = n[2];

            v.u = 0.0f; v.v = 0.0f;
            if (ti >= 0 && size_t(ti * 3 + 1) < m.tverts.size()) {
                v.u = m.tverts[size_t(ti * 3 + 0)];

                // V needs two corrections, which compose to (1 + stored):
                //
                //  1. The exporter stores V negated. Measured over the shipped
                //     geometry, U already lies in [0,1] (986/986 tverts in
                //     prince.k2a) while V lies in [-1,0] (99.5% in PG1.k2a), so
                //     the true Max coordinate is -stored.
                //
                //  2. Max puts V=0 at the BOTTOM of the image; our decoded
                //     textures are top-left origin (assets/image.cpp flips BMP
                //     rows), and GL samples v=0 at the first row. So the axis
                //     must be inverted again: v_gl = 1 - v_true.
                //
                // v_gl = 1 - (-stored) = 1 + stored.
                //
                // Written as one expression on purpose: applying only the
                // negation leaves the texture vertically mirrored, and for
                // tiled UVs (prison1.K2A spans V -20.6..12.4) the two forms are
                // not congruent modulo 1, so GL_REPEAT does not hide the error.
                v.v = 1.0f + m.tverts[size_t(ti * 3 + 1)];
            }

            v.r = v.g = v.b = 255; v.a = 255;
            if (ci >= 0 && size_t(ci * 3 + 2) < m.cverts.size()) {
                const float* c = &m.cverts[size_t(ci * 3)];
                v.r = uint8_t(c[0] < 0 ? 0 : (c[0] > 1 ? 255 : c[0] * 255.0f + 0.5f));
                v.g = uint8_t(c[1] < 0 ? 0 : (c[1] > 1 ? 255 : c[1] * 255.0f + 0.5f));
                v.b = uint8_t(c[2] < 0 ? 0 : (c[2] > 1 ? 255 : c[2] * 255.0f + 0.5f));
            }

            if (verts.size() >= 0xFFFF) return;   // 16-bit index budget
            const uint16_t idx = uint16_t(verts.size());
            verts.push_back(v);
            indices.push_back(idx);
            weld[key] = idx;
        }
    }
}

} // namespace assets
} // namespace k2
