// movie.h — a cutscene: SVQ1 frames shown at their times, and the sound
// track (twos PCM or QDM2) as 16-bit PCM.
//
// Video and sound can be decoded on different threads: next_frame() walks
// the frames in order, and decode_audio_into() fills a buffer from its own
// file handle while the frames play.
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

    // Decodes the next frame in order into rgba(); its start time in
    // seconds in `t`. False at the end.
    bool next_frame(double& t);

    // The whole sound track, interleaved 16-bit, at audio_rate(). Empty
    // until decode_audio() runs.
    void decode_audio();
    const std::vector<int16_t>& pcm() const { return m_pcm; }
    // Interleaved samples the sound track decodes to (an upper bound).
    size_t audio_samples() const;
    // Decodes the sound track into dst, at most `capacity` samples, from a
    // second handle on the file - safe beside next_frame() on another
    // thread. Stops early when *stop turns true. Returns samples written.
    size_t decode_audio_into(int16_t* dst, size_t capacity, const volatile bool* stop) const;
    int audio_rate() const { return m_audio_rate; }
    int audio_channels() const { return m_audio_channels; }

private:
    template <class F>
    static void decode_track(xf_mov* mov, const xf_mov_track* track, std::vector<int16_t>& pcm, F chunk_done);

    std::string    m_path;
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
