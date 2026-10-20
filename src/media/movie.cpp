// movie.cpp — see movie.h.

#include "media/movie.h"

#include <cstring>

#include "media/mov.h"
#include "media/qdm2.h"
#include "media/svq1.h"

namespace k2 {
namespace media {

movie::movie() : m_mov(0), m_video(0), m_audio(0), m_dec(0), m_next_sample(0),
                 m_audio_rate(22050), m_audio_channels(2) {}

movie::~movie() { close(); }

bool movie::open(const std::string& path) {
    close();
    m_mov = new xf_mov;
    std::memset(m_mov, 0, sizeof(*m_mov));
    if (xf_mov_open(m_mov, path.c_str()) != 0) { delete m_mov; m_mov = 0; return false; }
    m_video = xf_mov_find(m_mov, XF_TRACK_VIDEO);
    m_audio = xf_mov_find(m_mov, XF_TRACK_AUDIO);
    if (m_video) m_dec = new svq1_decoder(m_video->width, m_video->height);
    m_next_sample = 0;
    decode_audio();
    return true;
}

void movie::close() {
    delete m_dec;
    m_dec = 0;
    if (m_mov) { xf_mov_close(m_mov); delete m_mov; m_mov = 0; }
    m_video = m_audio = 0;
    m_pcm.clear();
}

int movie::width() const { return m_video ? m_video->width : 0; }
int movie::height() const { return m_video ? m_video->height : 0; }

double movie::duration() const {
    return m_mov && m_mov->timescale ? double(m_mov->duration) / m_mov->timescale : 0.0;
}

const uint8_t* movie::rgba() const { return m_dec ? m_dec->rgba() : 0; }

bool movie::seek_video(double t) {
    if (!m_video || !m_dec || !m_video->timescale) return false;
    const uint32_t media_t = uint32_t(t * m_video->timescale);
    const int want = int(xf_mov_sample_at(m_video, media_t));
    bool changed = false;
    // Frames depend on the last reference: decode forward, never skip.
    while (m_next_sample <= want && uint32_t(m_next_sample) < m_video->sample_count) {
        const uint32_t n = m_video->sample_size[m_next_sample];
        m_buf.resize(n);
        if (n && xf_mov_read_sample(m_mov, m_video, uint32_t(m_next_sample), &m_buf[0]) == 0)
            changed |= m_dec->decode(&m_buf[0], n);
        ++m_next_sample;
    }
    return changed;
}

void movie::decode_audio() {
    m_pcm.clear();
    if (!m_audio) return;
    m_audio_rate = int(m_audio->sample_rate);
    m_audio_channels = m_audio->channels ? m_audio->channels : 1;
    const bool qdm2 = m_audio->codec == 0x51444D32u;               // 'QDM2'
    const bool twos = m_audio->codec == 0x74776F73u;               // 'twos': big-endian PCM
    if (!qdm2 && !twos) return;
    qdm2_decoder* q = 0;
    if (qdm2) {
        // The decoder's parameters are in the description's 'frma'/'QDCA' atoms.
        size_t ex = 0;
        for (size_t k = 4; k + 4 <= m_audio->desc_size; ++k)
            if (std::memcmp(m_audio->desc + k, "frma", 4) == 0) { ex = k - 4; break; }
        q = new qdm2_decoder(m_audio->desc + ex, m_audio->desc_size - ex);
    }
    std::vector<uint8_t> chunk;
    for (uint32_t c = 0; c < m_audio->chunk_count; ++c) {
        const uint32_t n = m_audio->chunk_samples[c];
        const uint32_t bytes = (qdm2 && m_audio->samples_per_packet)
            ? n / m_audio->samples_per_packet * m_audio->bytes_per_frame
            : n * uint32_t(m_audio_channels) * 2;
        if (!bytes) continue;
        chunk.resize(bytes);
        if (xf_read_at(m_mov->file, m_audio->chunk_offset[c], &chunk[0], bytes) != 0) continue;
        if (qdm2) {
            q->decode(&chunk[0], bytes, m_pcm);
        } else {
            for (uint32_t i = 0; i + 1 < bytes; i += 2)
                m_pcm.push_back(int16_t(uint16_t(chunk[i] << 8 | chunk[i + 1])));
        }
    }
    delete q;
}

} // namespace media
} // namespace k2
