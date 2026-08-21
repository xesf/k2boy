// image.h — texture decoding for the formats this game ships.
//
// Surveyed across the whole install:
//   .bmp  1522 x 24-bit uncompressed, 82 x 8-bit paletted uncompressed
//         all BITMAPINFOHEADER (40-byte), no RLE
//   .tga   302 x 32-bit uncompressed, 2 x 24-bit uncompressed,
//          27 x 24-bit RLE (image type 10)
//
// Everything decodes to RGBA8, top-left origin, ready for gfx::create_texture.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_IMAGE_H
#define K2_ASSETS_IMAGE_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

struct image {
    int32_t              width;
    int32_t              height;
    std::vector<uint8_t> rgba;      // width * height * 4, top-left origin

    image() : width(0), height(0) {}
    bool valid() const { return width > 0 && height > 0 &&
                                rgba.size() == size_t(width) * size_t(height) * 4; }
};

// Sniffs BMP vs TGA from the content, not the extension — the install has
// mismatched extensions elsewhere, so trusting the name is unwise.
bool image_load(const uint8_t* data, size_t size, image& out,
                std::string* error = 0);

bool image_load_bmp(const uint8_t* data, size_t size, image& out,
                    std::string* error = 0);
bool image_load_tga(const uint8_t* data, size_t size, image& out,
                    std::string* error = 0);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_IMAGE_H
