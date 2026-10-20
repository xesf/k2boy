// qdm2.cpp — see qdm2.h. The ScummVM sources are in qdm2_port.inc,
// unchanged but for their #include lines; this file supplies what they
// expect from ScummVM.

#include "media/qdm2.h"

#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace k2 {
namespace media {
namespace scummvm {

typedef uint8_t byte;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef unsigned int uint;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2 0.70710678118654752440
#endif
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MKTAG(a0, a1, a2, a3) ((uint32)((a3) | ((a2) << 8) | ((a1) << 16) | ((a0) << 24)))
#define PACKED_STRUCT
#define FORCEINLINE inline
#define SCUMM_LITTLE_ENDIAN 1

inline void debug(int, const char*, ...) {}
inline void warning(const char* fmt, ...) {
    va_list a; va_start(a, fmt); std::printf("qdm2: "); std::vprintf(fmt, a); std::printf("\n"); va_end(a);
}
inline void error(const char* fmt, ...) {
    va_list a; va_start(a, fmt); std::printf("qdm2 error: "); std::vprintf(fmt, a); std::printf("\n"); va_end(a);
}
template <class T> inline void SWAP(T& a, T& b) { T t = a; a = b; b = t; }

namespace DisposeAfterUse { enum Flag { NO, YES }; }

namespace Common {

inline int intLog2(uint32 v) {
    if (!v) return -1;
    int r = 0;
    while (v >>= 1) ++r;
    return r;
}

// A read stream over memory; all the decoder needs of ScummVM's.
class SeekableReadStream {
public:
    SeekableReadStream(const byte* d, uint32 n) : m_d(d), m_n(n), m_p(0), m_eos(false) {}
    virtual ~SeekableReadStream() {}
    uint32 read(void* dst, uint32 n) {
        if (m_p + n > m_n) { n = m_p < m_n ? m_n - m_p : 0; m_eos = true; }
        std::memcpy(dst, m_d + m_p, n);
        m_p += n;
        return n;
    }
    byte readByte() { byte b = 0; read(&b, 1); return b; }
    uint16 readUint16LE() { byte b[2] = {0, 0}; read(b, 2); return uint16(b[0] | b[1] << 8); }
    uint16 readUint16BE() { byte b[2] = {0, 0}; read(b, 2); return uint16(b[0] << 8 | b[1]); }
    uint32 readUint32LE() { byte b[4] = {0, 0, 0, 0}; read(b, 4); return b[0] | uint32(b[1]) << 8 | uint32(b[2]) << 16 | uint32(b[3]) << 24; }
    uint32 readUint32BE() { byte b[4] = {0, 0, 0, 0}; read(b, 4); return uint32(b[0]) << 24 | uint32(b[1]) << 16 | uint32(b[2]) << 8 | b[3]; }
    int64 pos() const { return m_p; }
    int64 size() const { return m_n; }
    bool seek(int64 off, int whence = SEEK_SET) {
        int64 p = whence == SEEK_SET ? off : whence == SEEK_CUR ? m_p + off : m_n + off;
        if (p < 0 || p > m_n) return false;
        m_p = uint32(p); m_eos = false;
        return true;
    }
    bool skip(uint32 n) { return seek(n, SEEK_CUR); }
    bool eos() const { return m_eos; }
private:
    const byte* m_d;
    uint32 m_n, m_p;
    bool m_eos;
};

class BitStreamMemoryStream {
public:
    BitStreamMemoryStream(const byte* d, uint32 n, DisposeAfterUse::Flag = DisposeAfterUse::NO) : d(d), n(n) {}
    const byte* d;
    uint32 n;
};

// 32-bit little-endian words, least significant bit first: bit i of the
// stream is bit (i & 7) of byte i / 8. Past the end (whole words only)
// reads zero.
class BitStreamMemory32LELSB {
public:
    explicit BitStreamMemory32LELSB(BitStreamMemoryStream* s) : m_d(s->d), m_size((s->n & ~3u) * 8), m_pos(0) {}
    explicit BitStreamMemory32LELSB(BitStreamMemoryStream& s) : m_d(s.d), m_size((s.n & ~3u) * 8), m_pos(0) {}
    uint32 peekBits(size_t count) const {
        uint32 v = 0;
        for (size_t k = 0; k < count; ++k) {
            const uint32 i = m_pos + uint32(k);
            const uint32 b = i < m_size ? (m_d[i >> 3] >> (i & 7)) & 1u : 0u;
            v |= b << k;
        }
        return v;
    }
    uint32 getBits(size_t count) { const uint32 v = peekBits(count); m_pos += uint32(count); return v; }
    template <int N> uint32 peekBits() const { return peekBits(size_t(N)); }
    template <int N> uint32 getBits() { return getBits(size_t(N)); }
    uint getBit() { return getBits(1); }
    void skip(uint32 count) { m_pos += count; }
    uint32 pos() const { return m_pos; }
    uint32 size() const { return m_size; }
    bool eos() const { return m_pos >= m_size; }
private:
    const byte* m_d;
    uint32 m_size, m_pos;
};

} // namespace Common

namespace Audio {

class AudioStream {
public:
    virtual ~AudioStream() {}
};

enum { FLAG_UNSIGNED = 1, FLAG_16BITS = 2, FLAG_LITTLE_ENDIAN = 4, FLAG_STEREO = 8 };

// Collects what the decoder queues.
class QueuingAudioStream : public AudioStream {
public:
    std::vector<int16_t> samples;
    void queueBuffer(byte* data, uint32 size, DisposeAfterUse::Flag dispose, byte) {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        samples.insert(samples.end(), s, s + size / 2);
        if (dispose == DisposeAfterUse::YES) std::free(data);
    }
    void finish() {}
};
inline QueuingAudioStream* makeQueuingAudioStream(int, bool) { return new QueuingAudioStream(); }

class Codec {
public:
    virtual ~Codec() {}
    virtual AudioStream* decodeFrame(Common::SeekableReadStream& stream) = 0;
};

} // namespace Audio

namespace Math { struct Complex { float re, im; }; }  // math/utils.h

#include "media/qdm2_port.inc"

} // namespace scummvm

qdm2_decoder::qdm2_decoder(const uint8_t* extra, size_t extra_size) : m_impl(0) {
    scummvm::Common::SeekableReadStream* s = new scummvm::Common::SeekableReadStream(extra, uint32_t(extra_size));
    m_impl = scummvm::Audio::makeQDM2Decoder(s, scummvm::DisposeAfterUse::YES);
}

qdm2_decoder::~qdm2_decoder() {
    delete static_cast<scummvm::Audio::Codec*>(m_impl);
}

void qdm2_decoder::decode(const uint8_t* data, size_t size, std::vector<int16_t>& out) {
    if (!m_impl) return;
    scummvm::Common::SeekableReadStream in(data, uint32_t(size));
    scummvm::Audio::AudioStream* a = static_cast<scummvm::Audio::Codec*>(m_impl)->decodeFrame(in);
    scummvm::Audio::QueuingAudioStream* q = static_cast<scummvm::Audio::QueuingAudioStream*>(a);
    out.insert(out.end(), q->samples.begin(), q->samples.end());
    delete a;
}

} // namespace media
} // namespace k2
