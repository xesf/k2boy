// sfx.cpp — see sfx.h.

#include "assets/sfx.h"

#include <cstring>

namespace k2 {
namespace assets {

namespace {
inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

bool sfx_parse(const uint8_t* d, size_t size, sfx_file& out, std::string* error) {
    out = sfx_file();
    if (!d || size < 0x20) { if (error) *error = "too small"; return false; }
    // The two bytes at 0x12 are "WI" in 1264 of 1271 shipped files, "MA" in
    // five and binary in one; the layout is the same in all of them, so they
    // are not checked. The PCM header and the data size are.
    if (rd16(d) != 1 || rd16(d + 0x0E) != 16) { if (error) *error = "not 16-bit PCM"; return false; }
    out.channels = rd16(d + 2);
    out.rate     = int32_t(rd32(d + 4));
    const uint32_t bytes = rd32(d + 0x14);
    if (out.channels < 1 || out.channels > 2 || 0x20 + size_t(bytes) > size) {
        if (error) *error = "bad header";
        return false;
    }
    const uint32_t align = uint32_t(out.channels) * 2;
    out.loop_start = rd32(d + 0x18) / align;
    out.loop_end   = rd32(d + 0x1C) / align;
    out.samples.resize(bytes / 2);
    std::memcpy(&out.samples[0], d + 0x20, out.samples.size() * 2);   // little-endian host
    return true;
}

} // namespace assets
} // namespace k2
