// qdm2.h — QDesign Music 2 audio decoder, the sound of four cutscenes
// (Broder, cs03, cs05, cs08).
//
// ScummVM's decoder (audio/decoders/qdm2.cpp and its tables, math/rdft and
// math/fft; GPL-3.0-or-later, the ScummVM team; after FFmpeg) compiled
// against a small stand-in for the ScummVM classes it uses (qdm2.cpp).
//
// C++11.

#ifndef K2_MEDIA_QDM2_H
#define K2_MEDIA_QDM2_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace k2 {
namespace media {

class qdm2_decoder {
public:
    // `extra` is the sample description's extra data (from the 'wave' atom:
    // the 'frma' and 'QDCA' atoms the decoder reads its parameters from).
    qdm2_decoder(const uint8_t* extra, size_t extra_size);
    ~qdm2_decoder();
    bool ok() const { return m_impl != 0; }

    // Decodes a packet; appends interleaved 16-bit samples to `out`.
    void decode(const uint8_t* data, size_t size, std::vector<int16_t>& out);

private:
    void* m_impl;
};

} // namespace media
} // namespace k2

#endif
