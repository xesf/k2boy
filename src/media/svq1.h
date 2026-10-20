// svq1.h — Sorenson Video 1 decoder, the cutscenes' video codec.
//
// Ported from ScummVM (image/codecs/svq1.cpp, GPL-3.0-or-later, the ScummVM
// team; itself based on FFmpeg's decoder by Arpi and Nick Kurshev), with our
// own bit reader, Huffman decoder and YUV 4:1:0 to RGBA conversion.
//
// C++11.

#ifndef K2_MEDIA_SVQ1_H
#define K2_MEDIA_SVQ1_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace k2 {
namespace media {

class svq1_decoder {
public:
    svq1_decoder(int width, int height);
    ~svq1_decoder();

    // Decodes one frame. On success the picture is in rgba() (width * height
    // * 4 bytes, top row first) and true is returned; on a bad frame the last
    // good picture stays.
    bool decode(const uint8_t* data, size_t size);
    const uint8_t* rgba() const { return m_rgba.empty() ? 0 : &m_rgba[0]; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    struct bits;
    struct huffman;
    struct point { int x, y; };

private:
    bool block_intra(bits& s, uint8_t* pixels, int pitch);
    bool block_non_intra(bits& s, uint8_t* pixels, int pitch);
    bool motion_vector(bits& s, point* mv, point** pmv);
    bool motion_inter(bits& s, uint8_t* current, uint8_t* previous, int pitch, point* motion, int x, int y);
    bool motion_inter4v(bits& s, uint8_t* current, uint8_t* previous, int pitch, point* motion, int x, int y);
    bool delta_block(bits& s, uint8_t* current, uint8_t* previous, int pitch, point* motion, int x, int y);
    void to_rgba(const uint8_t* y, const uint8_t* u, const uint8_t* v, int y_pitch, int uv_pitch);

    int m_width, m_height;
    int m_frame_width, m_frame_height;
    uint8_t* m_last[3];
    std::vector<uint8_t> m_rgba;

    huffman* m_block_type;
    huffman* m_intra_multistage[6];
    huffman* m_inter_multistage[6];
    huffman* m_intra_mean;
    huffman* m_inter_mean;
    huffman* m_motion;
};

} // namespace media
} // namespace k2

#endif // K2_MEDIA_SVQ1_H
