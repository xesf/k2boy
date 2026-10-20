// mixer.h — plays .sfx sounds: one-shots, loops over the file's loop points,
// and positional voices attenuated by distance from a listener.
//
// The original mixes through Aureal A3D or DirectSound 3D (A3DSource,
// DS3DSource); none of that is reproduced. This is a plain software mixer:
// linear resampling, equal-power pan, inverse-distance falloff.
//
// mix() runs on the audio thread; everything else on the game thread. A mutex
// guards the voice list.
//
// C++11.

#ifndef K2_ENGINE_MIXER_H
#define K2_ENGINE_MIXER_H

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "assets/sfx.h"

namespace k2 {
namespace engine {

class mixer {
public:
    explicit mixer(int32_t out_rate) : m_rate(out_rate), m_next_id(1) {
        for (int k = 0; k < 3; ++k) m_ear[k] = 0.0f;
        m_ear_right[0] = 1.0f; m_ear_right[1] = m_ear_right[2] = 0.0f;
    }

    // Registers a decoded sound under a lower-case name.
    void add_sound(const std::string& name, const assets::sfx_file& s);
    bool has_sound(const std::string& name) const;

    // Starts a voice; returns its id, 0 when the sound is unknown. `pos` null
    // for a non-positional sound. `loop` honours the file's loop points.
    int32_t play(const std::string& name, const float* pos, float radius, bool loop, float gain = 1.0f);
    void    stop(int32_t id);

    void set_listener(const float pos[3], const float right[3]);

    // Audio thread: adds `frames` stereo frames into `out`.
    void mix(float* out, int32_t frames);

    size_t voices() const;

private:
    struct voice {
        int32_t id;
        const assets::sfx_file* sound;
        double  cursor;         // in source frames
        bool    loop, positional;
        float   pos[3], radius, gain;
    };

    int32_t m_rate;
    std::map<std::string, assets::sfx_file> m_sounds;
    std::vector<voice> m_voices;
    mutable std::mutex m_lock;
    int32_t m_next_id;
    float   m_ear[3], m_ear_right[3];
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_MIXER_H
