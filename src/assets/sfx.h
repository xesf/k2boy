// sfx.h — the game's sound effects (.sfx): a WAVEFORMATEX, a two-byte tag ("WI", "MA"), the
// data size and loop points, then raw 16-bit PCM. See docs/formats/sfx.md.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_SFX_H
#define K2_ASSETS_SFX_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

struct sfx_file {
    int32_t              channels;
    int32_t              rate;
    std::vector<int16_t> samples;    // interleaved
    // Loop region in frames; both 0 for a one-shot. The file stores bytes.
    uint32_t             loop_start;
    uint32_t             loop_end;

    sfx_file() : channels(1), rate(22050), loop_start(0), loop_end(0) {}
    bool loops() const { return loop_end > loop_start; }
    size_t frames() const { return channels ? samples.size() / size_t(channels) : 0; }
};

bool sfx_parse(const uint8_t* data, size_t size, sfx_file& out, std::string* error = 0);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_SFX_H
