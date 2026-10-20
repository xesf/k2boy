// movie.h — a cutscene: SVQ1 frames shown at their times, and the whole
// sound track (twos PCM or QDM2) decoded up front as 16-bit PCM.
//
// No dependency on gfx or the mixer: the game uploads rgba() and plays pcm().
//
// C++11.

#ifndef K2_MEDIA_MOVIE_H
#define K2_MEDIA_MOVIE_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "media/mov.h"

namespace k2 {
namespace media {

class svq1_decoder;

class movie {
public:
    movie();
    ~movie();

    bool open(const std::string& path);
    void close();
    bool is_open() const { return m_mov != 0; }

    int width() const;
    int height() const;
    double duration() const;                    // seconds

    // Advances the picture to `t` seconds; true when it changed.
    bool seek_video(double t);
    const uint8_t* rgba() const;

    // The whole sound track, interleaved 16-bit, at audio_rate().
    const std::vector<int16_t>& pcm() const { return m_pcm; }
    int audio_rate() const { return m_audio_rate; }
    int audio_channels() const { return m_audio_channels; }

private:
    void decode_audio();

    xf_mov*        m_mov;
    xf_mov_track*  m_video;
    xf_mov_track*  m_audio;
    svq1_decoder*  m_dec;
    int            m_next_sample;               // next video sample to decode
    std::vector<uint8_t> m_buf;
    std::vector<int16_t> m_pcm;
    int            m_audio_rate, m_audio_channels;
};

} // namespace media
} // namespace k2

#endif // K2_MEDIA_MOVIE_H
