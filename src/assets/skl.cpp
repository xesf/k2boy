// skl.cpp — see skl.h.

#include "assets/skl.h"

#include <cstring>

namespace k2 {
namespace assets {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline float rdf(const uint8_t* p) {
    const uint32_t v = rd32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// Chunks are located by their 4CC; the stream carries no chunk length.
//
// JKey is fixed at sizeof(MfJointKeyframe). SKey and vKey are NOT: each writes
// a fixed part, then a length-prefixed string, then a trailing tail, so their
// on-disk size depends on the string. Their sizeof() is only the fixed part.
const size_t JKEY_SIZE       = 212;   // sizeof(MfJointKeyframe)
const size_t SKEY_FIXED      = 0x2C;  // then u32 len + bytes + 8-byte tail
const size_t SKEY_TAIL       = 8;
const size_t VKEY_STR_OFFSET = 0x10;  // u32 len + bytes
const size_t VKEY_TAIL       = 12;

// Length-prefixed, no terminator.
bool read_string_at(const uint8_t* d, size_t size, size_t off, std::string& out) {
    if (off + 4 > size) return false;
    const uint32_t len = rd32(d + off);
    if (len > 1024 || off + 4 + len > size) return false;
    out.assign(reinterpret_cast<const char*>(d + off + 4), len);
    return true;
}

} // namespace

bool skl_parse(const uint8_t* d, size_t size, skl_file& out, std::string* error) {
    out = skl_file();

    if (!d || size < 24) { if (error) *error = "too small"; return false; }
    if (std::memcmp(d, "kSFM", 4) != 0) { if (error) *error = "bad magic"; return false; }

    out.kind_tag.assign(reinterpret_cast<const char*>(d + 4), 4);
    if      (out.kind_tag == "LSkl") out.kind = SKL_LOCO;
    else if (out.kind_tag == "MSkl") out.kind = SKL_MANIP;
    else                             out.kind = SKL_GENERIC;

    out.version = int32_t(rd32(d + 8));
    out.chunk_tag.assign(reinterpret_cast<const char*>(d + 12), 4);

    // header: magic, kind, version, chunk tag, one u32, then the name
    read_string_at(d, size, 20, out.name);

    // Scan for chunk tags. Records have no length prefix, so a tag match plus a
    // fixed size is the only framing available.
    for (size_t i = 24; i + 4 <= size; ) {
        const uint8_t* p = d + i;

        if (!std::memcmp(p, "JKey", 4) && i + JKEY_SIZE <= size) {
            skl_joint_key k;
            k.time     = rd32(p + 0x04);
            k.flag     = rd32(p + 0x08);
            k.field_0c = rd32(p + 0x0C);
            for (int t = 0; t < 6; ++t) {
                const uint8_t* q = p + 0x10 + t * 28;
                for (int c = 0; c < 3; ++c) k.xf[t].pos[c]  = rdf(q + 4 * c);
                for (int c = 0; c < 4; ++c) k.xf[t].quat[c] = rdf(q + 12 + 4 * c);
            }
            for (int c = 0; c < 3; ++c) k.vec3[c] = rdf(p + 0xB8 + 4 * c);
            k.field_c4 = rd32(p + 0xC4);
            k.ease_in  = rdf(p + 0xC8);
            k.ease_out = rdf(p + 0xCC);
            k.field_d0 = rd32(p + 0xD0);
            out.joint_keys.push_back(k);
            i += JKEY_SIZE;
            continue;
        }

        if (!std::memcmp(p, "vKey", 4) && i + VKEY_STR_OFFSET + 4 <= size) {
            skl_event_key k;
            k.value = rd32(p + 0x08);
            read_string_at(d, size, i + VKEY_STR_OFFSET, k.target);
            const uint32_t len = rd32(p + VKEY_STR_OFFSET);
            const size_t   end = i + VKEY_STR_OFFSET + 4 + len;
            // The event time follows the string rather than preceding it: the
            // words before it are 0 even for keys that fire late in a clip.
            k.time = (end + 4 + 4 <= size) ? rd32(d + end + 4) : 0;
            out.event_keys.push_back(k);
            i = end + VKEY_TAIL;
            continue;
        }

        if (!std::memcmp(p, "SKey", 4) && i + SKEY_FIXED + 4 <= size) {
            // 0x2C fixed bytes, then a length-prefixed name, then an 8-byte
            // tail. The name is a ".wav" even though the files on disk are
            // .sfx, so callers must strip the extension when resolving.
            skl_sound_key k;
            k.time = rd32(p + 0x04);
            read_string_at(d, size, i + 0x28, k.sound);
            const uint32_t len = rd32(p + 0x28);
            out.sound_keys.push_back(k);
            i = i + SKEY_FIXED + len + SKEY_TAIL;
            continue;
        }

        ++i;
    }

    return true;
}

} // namespace assets
} // namespace k2
