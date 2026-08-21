// image.cpp — see image.h.

#include "assets/image.h"

#include <cstring>

namespace k2 {
namespace assets {

namespace {

inline uint16_t rd16(const uint8_t* p) {
    return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline int32_t rds32(const uint8_t* p) { return int32_t(rd32(p)); }

void fail(std::string* e, const char* msg) { if (e) *e = msg; }

} // namespace

// ---------------------------------------------------------------- BMP

bool image_load_bmp(const uint8_t* d, size_t size, image& out, std::string* error) {
    if (size < 54 || d[0] != 'B' || d[1] != 'M') { fail(error, "not a BMP"); return false; }

    const uint32_t pixel_off = rd32(d + 10);
    const uint32_t hdr_size  = rd32(d + 14);
    if (hdr_size < 40) { fail(error, "unsupported BMP header size"); return false; }

    const int32_t  w       = rds32(d + 18);
    const int32_t  h_signed = rds32(d + 22);
    const uint16_t bpp     = rd16(d + 28);
    const uint32_t comp    = rd32(d + 30);

    if (comp != 0)                      { fail(error, "compressed BMP unsupported"); return false; }
    if (bpp != 24 && bpp != 8)          { fail(error, "BMP bpp not 8 or 24"); return false; }
    if (w <= 0 || h_signed == 0)        { fail(error, "bad BMP dimensions"); return false; }

    // Negative height means the rows are stored top-down.
    const bool    top_down = h_signed < 0;
    const int32_t h        = top_down ? -h_signed : h_signed;

    // 8-bit images carry a palette between the header and the pixel data.
    const uint8_t* palette = d + 14 + hdr_size;
    uint32_t palette_bytes = 0;
    if (bpp == 8) {
        uint32_t used = rd32(d + 46);
        if (used == 0) used = 256;
        palette_bytes = used * 4;
        if (size_t(14 + hdr_size + palette_bytes) > size) {
            fail(error, "BMP palette truncated");
            return false;
        }
    }

    const size_t row_bytes = ((size_t(w) * bpp + 31) / 32) * 4;   // rows pad to 4 bytes
    const size_t need      = size_t(pixel_off) + row_bytes * size_t(h);
    if (need > size) { fail(error, "BMP pixel data truncated"); return false; }

    out.width  = w;
    out.height = h;
    out.rgba.assign(size_t(w) * size_t(h) * 4, 255);

    for (int32_t y = 0; y < h; ++y) {
        const int32_t  src_row = top_down ? y : (h - 1 - y);   // BMP is bottom-up by default
        const uint8_t* src     = d + pixel_off + row_bytes * size_t(src_row);
        uint8_t*       dst     = &out.rgba[size_t(y) * size_t(w) * 4];

        if (bpp == 24) {
            for (int32_t x = 0; x < w; ++x) {
                dst[x * 4 + 0] = src[x * 3 + 2];   // stored BGR
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 0];
                dst[x * 4 + 3] = 255;
            }
        } else {
            for (int32_t x = 0; x < w; ++x) {
                const uint8_t* e = palette + size_t(src[x]) * 4;   // BGRA entries
                dst[x * 4 + 0] = e[2];
                dst[x * 4 + 1] = e[1];
                dst[x * 4 + 2] = e[0];
                dst[x * 4 + 3] = 255;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- TGA

bool image_load_tga(const uint8_t* d, size_t size, image& out, std::string* error) {
    if (size < 18) { fail(error, "TGA too small"); return false; }

    const uint8_t  id_len     = d[0];
    const uint8_t  cmap_type  = d[1];
    const uint8_t  image_type = d[2];
    const int32_t  w          = int32_t(rd16(d + 12));
    const int32_t  h          = int32_t(rd16(d + 14));
    const uint8_t  bpp        = d[16];
    const uint8_t  desc       = d[17];

    if (cmap_type != 0)                   { fail(error, "paletted TGA unsupported"); return false; }
    if (image_type != 2 && image_type != 10) { fail(error, "TGA type not 2 or 10"); return false; }
    if (bpp != 24 && bpp != 32)           { fail(error, "TGA bpp not 24 or 32"); return false; }
    if (w <= 0 || h <= 0)                 { fail(error, "bad TGA dimensions"); return false; }

    const size_t   bytes_pp = size_t(bpp) / 8;
    const uint8_t* src      = d + 18 + id_len;
    const uint8_t* end      = d + size;

    // Bit 5 of the descriptor: set means the first row is the top one.
    const bool top_down = (desc & 0x20) != 0;

    out.width  = w;
    out.height = h;
    out.rgba.assign(size_t(w) * size_t(h) * 4, 255);

    std::vector<uint8_t> pixels(size_t(w) * size_t(h) * bytes_pp);

    if (image_type == 2) {
        if (src + pixels.size() > end) { fail(error, "TGA data truncated"); return false; }
        std::memcpy(&pixels[0], src, pixels.size());
    } else {
        // Type 10: run-length encoded. Packet header bit 7 selects RLE vs literal,
        // low 7 bits are (count - 1).
        size_t written = 0;
        while (written < pixels.size()) {
            if (src >= end) { fail(error, "TGA RLE truncated"); return false; }
            const uint8_t packet = *src++;
            const size_t  count  = size_t(packet & 0x7F) + 1;
            const size_t  bytes  = count * bytes_pp;
            if (written + bytes > pixels.size()) { fail(error, "TGA RLE overflow"); return false; }

            if (packet & 0x80) {
                if (src + bytes_pp > end) { fail(error, "TGA RLE truncated"); return false; }
                for (size_t i = 0; i < count; ++i)
                    std::memcpy(&pixels[written + i * bytes_pp], src, bytes_pp);
                src += bytes_pp;
            } else {
                if (src + bytes > end) { fail(error, "TGA literal truncated"); return false; }
                std::memcpy(&pixels[written], src, bytes);
                src += bytes;
            }
            written += bytes;
        }
    }

    for (int32_t y = 0; y < h; ++y) {
        const int32_t  src_row = top_down ? y : (h - 1 - y);
        const uint8_t* s = &pixels[size_t(src_row) * size_t(w) * bytes_pp];
        uint8_t*       t = &out.rgba[size_t(y) * size_t(w) * 4];
        for (int32_t x = 0; x < w; ++x) {
            t[x * 4 + 0] = s[x * bytes_pp + 2];   // stored BGR(A)
            t[x * 4 + 1] = s[x * bytes_pp + 1];
            t[x * 4 + 2] = s[x * bytes_pp + 0];
            t[x * 4 + 3] = (bytes_pp == 4) ? s[x * bytes_pp + 3] : 255;
        }
    }
    return true;
}

// ---------------------------------------------------------------- sniff

bool image_load(const uint8_t* d, size_t size, image& out, std::string* error) {
    if (size >= 2 && d[0] == 'B' && d[1] == 'M')
        return image_load_bmp(d, size, out, error);
    // TGA has no magic; accept it if the type byte is one we handle.
    if (size >= 18 && (d[2] == 2 || d[2] == 10))
        return image_load_tga(d, size, out, error);
    fail(error, "unrecognised image format");
    return false;
}

} // namespace assets
} // namespace k2
