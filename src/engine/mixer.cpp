// mixer.cpp — see mixer.h.

#include "engine/mixer.h"

#include <cmath>

namespace k2 {
namespace engine {

namespace {
std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = char(s[i] - 'A' + 'a');
    return s;
}
}

void mixer::add_sound(const std::string& name, const assets::sfx_file& s) {
    std::lock_guard<std::mutex> g(m_lock);
    m_sounds[lower(name)] = s;
}

bool mixer::has_sound(const std::string& name) const {
    std::lock_guard<std::mutex> g(m_lock);
    return m_sounds.count(lower(name)) != 0;
}

int32_t mixer::play(const std::string& name, const float* pos, float radius, bool loop, float gain) {
    std::lock_guard<std::mutex> g(m_lock);
    std::map<std::string, assets::sfx_file>::const_iterator it = m_sounds.find(lower(name));
    if (it == m_sounds.end() || it->second.frames() == 0) return 0;
    voice v;
    v.id = m_next_id++;
    v.sound = &it->second;
    v.cursor = 0.0;
    v.loop = loop && it->second.loops();
    v.positional = pos != 0;
    for (int k = 0; k < 3; ++k) v.pos[k] = pos ? pos[k] : 0.0f;
    v.radius = radius > 0.0f ? radius : 12.0f;
    v.gain = gain;
    m_voices.push_back(v);
    return v.id;
}

void mixer::stop(int32_t id) {
    std::lock_guard<std::mutex> g(m_lock);
    for (size_t i = 0; i < m_voices.size(); ++i)
        if (m_voices[i].id == id) { m_voices.erase(m_voices.begin() + long(i)); return; }
}

void mixer::set_listener(const float pos[3], const float right[3]) {
    std::lock_guard<std::mutex> g(m_lock);
    for (int k = 0; k < 3; ++k) { m_ear[k] = pos[k]; m_ear_right[k] = right[k]; }
}

size_t mixer::voices() const {
    std::lock_guard<std::mutex> g(m_lock);
    return m_voices.size();
}

void mixer::mix(float* out, int32_t frames) {
    std::lock_guard<std::mutex> g(m_lock);
    for (size_t vi = 0; vi < m_voices.size(); ) {
        voice& v = m_voices[vi];
        const assets::sfx_file& s = *v.sound;
        const double step = double(s.rate) / double(m_rate);
        float gl = v.gain * m_volume * 0.7071f, gr = v.gain * m_volume * 0.7071f;
        if (v.positional) {
            const float d[3] = { v.pos[0] - m_ear[0], v.pos[1] - m_ear[1], v.pos[2] - m_ear[2] };
            const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float att = 1.0f - dist / v.radius;
            if (att < 0.0f) att = 0.0f;
            att *= att;
            const float pan = dist > 1e-3f ? (d[0] * m_ear_right[0] + d[1] * m_ear_right[1] +
                                              d[2] * m_ear_right[2]) / dist : 0.0f;
            const float a = (pan + 1.0f) * 0.25f * 3.14159265f;      // 0 .. pi/2
            gl = v.gain * m_volume * att * std::cos(a);
            gr = v.gain * m_volume * att * std::sin(a);
        }
        const size_t n = s.frames();
        const int ch = s.channels;
        bool done = false;
        for (int32_t f = 0; f < frames; ++f) {
            size_t i0 = size_t(v.cursor);
            if (v.loop && i0 >= s.loop_end) {
                v.cursor -= double(s.loop_end - s.loop_start);
                i0 = size_t(v.cursor);
            }
            if (i0 + 1 >= n) { done = !v.loop; if (done) break; v.cursor = s.loop_start; continue; }
            const float t = float(v.cursor - double(i0));
            float l, r;
            if (ch == 1) {
                l = r = (s.samples[i0] * (1.0f - t) + s.samples[i0 + 1] * t) / 32768.0f;
            } else {
                l = (s.samples[i0 * 2] * (1.0f - t) + s.samples[i0 * 2 + 2] * t) / 32768.0f;
                r = (s.samples[i0 * 2 + 1] * (1.0f - t) + s.samples[i0 * 2 + 3] * t) / 32768.0f;
            }
            out[f * 2]     += l * gl;
            out[f * 2 + 1] += r * gr;
            v.cursor += step;
        }
        if (done) m_voices.erase(m_voices.begin() + long(vi));
        else ++vi;
    }
}

} // namespace engine
} // namespace k2
