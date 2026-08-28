// act.cpp — see act.h.

#include "assets/act.h"

#include <cstring>
#include <vector>

namespace k2 {
namespace assets {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline float rdf(const uint8_t* p) {
    uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// Length-prefixed, no NUL terminator. Used for every string in the format.
bool read_string(const uint8_t* d, size_t size, size_t& off, std::string& out,
                 uint32_t max_len = 4096) {
    if (off + 4 > size) return false;
    const uint32_t len = rd32(d + off);
    if (len > max_len || off + 4 + len > size) return false;
    out.assign(reinterpret_cast<const char*>(d + off + 4), len);
    off += 4 + len;
    return true;
}

bool printable(const uint8_t* p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
        if (p[i] < 32 || p[i] >= 127) return false;
    return true;
}

} // namespace

const act_property* act_file::find(const char* key) const {
    for (size_t i = 0; i < properties.size(); ++i)
        if (properties[i].name == key) return &properties[i];
    return 0;
}

bool act_parse(const uint8_t* d, size_t size, act_file& out, std::string* error) {
    out = act_file();
    out.tree_ok = false;

    if (!d || size < 16) { if (error) *error = "too small"; return false; }

    out.magic.assign(reinterpret_cast<const char*>(d), 4);

    if (out.magic == " MFB") {
        // Older container: " MFB" <u32> "ActP" ... — no property table.
        out.legacy_container = true;
        out.type.assign(reinterpret_cast<const char*>(d + 8), 4);
        return true;
    }
    if (out.magic != "APFM") { if (error) *error = "bad magic"; return false; }

    out.type.assign(reinterpret_cast<const char*>(d + 4), 4);
    out.version = int32_t(rd32(d + 8));

    size_t off = 16;    // magic, type, version, flags
    if (!read_string(d, size, off, out.name, 256) ||
        !read_string(d, size, off, out.display_name, 256)) {
        if (error) *error = "bad name fields";
        return false;
    }

    // --- property table
    //
    // Records are: u32 nameLen, name, u32 0xFFFFFFFF, 4CC tag, u32 payloadLen,
    // payload. Scanned rather than walked, because the preamble between the
    // names and the first record is not fully mapped.
    for (size_t i = off; i + 12 < size; ) {
        const uint32_t len = rd32(d + i);
        if (len < 3 || len > 64 || i + 4 + len + 12 > size) { ++i; continue; }

        const uint8_t* name = d + i + 4;
        if (!(name[0] == 'M' && name[1] == 'F' && name[2] == '_') ||
            !printable(name, len)) { ++i; continue; }

        const size_t after = i + 4 + len;
        if (rd32(d + after) != 0xFFFFFFFFu) { ++i; continue; }

        const uint8_t* tag = d + after + 4;
        const uint32_t payload_len = rd32(d + after + 8);
        const size_t   payload     = after + 12;
        if (payload + payload_len > size) { ++i; continue; }

        act_property p;
        p.name.assign(reinterpret_cast<const char*>(name), len);

        if (!std::memcmp(tag, "rtSV", 4)) {
            p.type = ACT_VALUE_STRING;
            if (payload_len >= 4) {
                const uint32_t sl = rd32(d + payload);
                if (payload + 4 + sl <= size && sl <= payload_len)
                    p.string_value.assign(
                        reinterpret_cast<const char*>(d + payload + 4), sl);
            }
        } else if (!std::memcmp(tag, "olFV", 4)) {
            p.type = ACT_VALUE_FLOAT;
            if (payload_len >= 4) p.float_value = rdf(d + payload);
        } else if (!std::memcmp(tag, "3eVV", 4)) {
            p.type = ACT_VALUE_VEC3;
            if (payload_len >= 12)
                for (int k = 0; k < 3; ++k) p.vec3[k] = rdf(d + payload + 4 * k);
        } else {
            ++i;
            continue;   // an unknown value tag; not a property record
        }

        out.properties.push_back(p);
        i = payload + payload_len;
    }

    // --- node / articulation block
    //
    // Each node is: u32 nameLen, name, then a 28-byte transform (pos[3] then
    // quat[4] with w first). Recovered from MfProtoNode's ctor + writer; see
    // docs/formats/act.md. There is no tag and no length, so nodes are found by
    // matching that shape and rejecting anything whose quaternion is not unit.
    for (size_t i = 0; i + 4 < size; ) {
        const uint32_t len = rd32(d + i);
        if (len < 1 || len > 64 || i + 4 + len + 28 > size) { ++i; continue; }

        const uint8_t* name = d + i + 4;
        if (!printable(name, len))                       { ++i; continue; }
        if (len >= 3 && !std::memcmp(name, "MF_", 3))    { ++i; continue; }

        const size_t t = i + 4 + len;

        float pos[3];
        bool sane = true;
        for (int k = 0; k < 3; ++k) {
            pos[k] = rdf(d + t + 4 * k);
            // Reject NaN and absurd magnitudes: those are property keys and the
            // actor name matching by accident.
            if (!(pos[k] == pos[k]) || pos[k] > 1e4f || pos[k] < -1e4f) sane = false;
        }
        if (!sane) { ++i; continue; }

        float q[4];
        double n2 = 0.0;
        for (int k = 0; k < 4; ++k) {
            q[k] = rdf(d + t + 12 + 4 * k);
            if (!(q[k] == q[k])) { sane = false; break; }
            n2 += double(q[k]) * double(q[k]);
        }
        if (!sane || n2 < 0.998 || n2 > 1.002) { ++i; continue; }

        act_node nd;
        nd.child_count = 0;
        nd.parent      = -1;
        nd.name.assign(reinterpret_cast<const char*>(name), len);
        for (int k = 0; k < 3; ++k) nd.pos[k]  = pos[k];
        for (int k = 0; k < 4; ++k) nd.quat[k] = q[k];
        nd.offset = i;
        out.nodes.push_back(nd);
        i = t + 28;
    }

    // --- skeleton hierarchy
    //
    // Child nodes are written immediately after their parent's child count, and
    // nodes appear depth-first (see MfProtoNode__ReadStream, docs/formats/act.md).
    // So node i's child count is the u32 sitting just before node i+1's length
    // prefix - no need to parse the variable-length payloads in between.
    //
    // The last node has nothing after it to read, but a valid tree only needs
    // sum(child_count) == n-1, so its count is whatever the others leave over,
    // and for a depth-first layout that is always 0.
    if (!out.nodes.empty()) {
        const size_t n = out.nodes.size();
        int64_t total = 0;
        for (size_t i = 0; i + 1 < n; ++i) {
            const size_t at = out.nodes[i + 1].offset;
            if (at < 4) { total = -1; break; }
            const uint32_t c = rd32(d + at - 4);
            if (c > n) { total = -1; break; }        // not a plausible count
            out.nodes[i].child_count = int32_t(c);
            total += c;
        }
        out.nodes[n - 1].child_count = 0;

        out.tree_ok = (total == int64_t(n) - 1);
        if (out.tree_ok) {
            // Walk depth-first, consuming counts, to assign parents.
            std::vector<int32_t> stack;      // (index, remaining children)
            std::vector<int32_t> left;
            size_t i = 0;
            for (; i < n; ++i) {
                if (!stack.empty()) {
                    out.nodes[i].parent = stack.back();
                    --left.back();
                    while (!left.empty() && left.back() == 0) {
                        stack.pop_back();
                        left.pop_back();
                    }
                } else if (i != 0) {
                    out.tree_ok = false;     // a second root: not a tree
                    break;
                }
                if (out.nodes[i].child_count > 0) {
                    stack.push_back(int32_t(i));
                    left.push_back(out.nodes[i].child_count);
                }
            }
            if (!stack.empty()) out.tree_ok = false;   // counts ran out early
        }
        if (!out.tree_ok)
            for (size_t k = 0; k < n; ++k) out.nodes[k].parent = -1;
    }

    return true;
}

} // namespace assets
} // namespace k2
