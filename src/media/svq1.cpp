// svq1.cpp — see svq1.h. Block decoding and motion compensation follow
// ScummVM's image/codecs/svq1.cpp (GPL-3.0-or-later).

#include "media/svq1.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "media/svq1_tables.h"

namespace k2 {
namespace media {

using namespace svq1;

// Bits read most-significant first (ScummVM's BitStream32BEMSB over bytes).
struct svq1_decoder::bits {
    const uint8_t* d;
    size_t n;
    size_t pos;                     // in bits
    bits(const uint8_t* data, size_t size) : d(data), n(size), pos(0) {}
    uint32_t bit() {
        const size_t byte = pos >> 3;
        const uint32_t b = byte < n ? (d[byte] >> (7 - (pos & 7))) & 1u : 0u;
        ++pos;
        return b;
    }
    uint32_t get(int count) {
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v = (v << 1) | bit();
        return v;
    }
    void skip(int count) { pos += size_t(count); }
};

// Canonical-free Huffman: codes matched by (length, value), read bit by bit.
struct svq1_decoder::huffman {
    std::map<uint64_t, uint32_t> table;     // (length << 32 | code) -> symbol
    int max_len;
    huffman(uint32_t count, const uint32_t* codes, const uint8_t* lengths) : max_len(0) {
        for (uint32_t i = 0; i < count; ++i) {
            if (!lengths[i]) continue;
            table[(uint64_t(lengths[i]) << 32) | codes[i]] = i;
            if (lengths[i] > max_len) max_len = lengths[i];
        }
    }
    int symbol(bits& s) const {
        uint32_t code = 0;
        for (int len = 1; len <= max_len; ++len) {
            code = (code << 1) | s.bit();
            std::map<uint64_t, uint32_t>::const_iterator it = table.find((uint64_t(len) << 32) | code);
            if (it != table.end()) return int(it->second);
        }
        return -1;
    }
};

namespace {

enum { BLOCK_SKIP = 0, BLOCK_INTER, BLOCK_INTER_4V, BLOCK_INTRA };

inline uint32_t rd32(const void* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline void wr32(void* p, uint32_t v) { std::memcpy(p, &v, 4); }

inline uint32_t align(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

inline uint32_t clip_pairs(uint32_t n) {
    // Clip two 16-bit lanes to [0..255] (ScummVM's / FFmpeg's trick).
    if (n & 0xFF00FF00) {
        const uint32_t n3 = (((n >> 15) & 0x00010001) | 0x01000100) - 0x00010001;
        n += 0x7F007F00;
        n |= (((~n >> 15) & 0x00010001) | 0x01000100) - 0x00010001;
        n &= n3 & 0x00FF00FF;
    }
    return n;
}

inline int mid_pred(int a, int b, int c) {
    if (a > b) { if (c > b) b = c > a ? a : c; }
    else       { if (b > c) b = c > a ? c : a; }
    return b;
}

inline uint32_t rnd_avg32(uint32_t a, uint32_t b) { return (a | b) - (((a ^ b) & ~0x01010101u) >> 1); }

void put8(uint8_t* block, const uint8_t* pixels, int line, int h) {
    for (int i = 0; i < h; ++i) {
        wr32(block, rd32(pixels));
        wr32(block + 4, rd32(pixels + 4));
        pixels += line;
        block += line;
    }
}

void put8_l2(uint8_t* dst, const uint8_t* s1, const uint8_t* s2, int stride, int h) {
    for (int i = 0; i < h; ++i) {
        wr32(&dst[stride * i], rnd_avg32(rd32(&s1[stride * i]), rd32(&s2[stride * i])));
        wr32(&dst[stride * i + 4], rnd_avg32(rd32(&s1[stride * i + 4]), rd32(&s2[stride * i + 4])));
    }
}

void put8_x2(uint8_t* b, const uint8_t* p, int line, int h) { put8_l2(b, p, p + 1, line, h); }
void put8_y2(uint8_t* b, const uint8_t* p, int line, int h) { put8_l2(b, p, p + line, line, h); }

void put8_xy2(uint8_t* block, const uint8_t* pixels, int line, int h) {
    for (int j = 0; j < 2; ++j) {
        uint32_t a = rd32(pixels), b = rd32(pixels + 1);
        uint32_t l0 = (a & 0x03030303u) + (b & 0x03030303u) + 0x02020202u;
        uint32_t h0 = ((a & 0xFCFCFCFCu) >> 2) + ((b & 0xFCFCFCFCu) >> 2);
        pixels += line;
        for (int i = 0; i < h; i += 2) {
            a = rd32(pixels); b = rd32(pixels + 1);
            const uint32_t l1 = (a & 0x03030303u) + (b & 0x03030303u);
            const uint32_t h1 = ((a & 0xFCFCFCFCu) >> 2) + ((b & 0xFCFCFCFCu) >> 2);
            wr32(block, h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0Fu));
            pixels += line; block += line;
            a = rd32(pixels); b = rd32(pixels + 1);
            l0 = (a & 0x03030303u) + (b & 0x03030303u) + 0x02020202u;
            h0 = ((a & 0xFCFCFCFCu) >> 2) + ((b & 0xFCFCFCFCu) >> 2);
            wr32(block, h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0Fu));
            pixels += line; block += line;
        }
        pixels += 4 - line * (h + 1);
        block += 4 - line * h;
    }
}

void put16(uint8_t* b, const uint8_t* p, int line, int h)     { put8(b, p, line, h);     put8(b + 8, p + 8, line, h); }
void put16_x2(uint8_t* b, const uint8_t* p, int line, int h)  { put8_x2(b, p, line, h);  put8_x2(b + 8, p + 8, line, h); }
void put16_y2(uint8_t* b, const uint8_t* p, int line, int h)  { put8_y2(b, p, line, h);  put8_y2(b + 8, p + 8, line, h); }
void put16_xy2(uint8_t* b, const uint8_t* p, int line, int h) { put8_xy2(b, p, line, h); put8_xy2(b + 8, p + 8, line, h); }

} // namespace

svq1_decoder::svq1_decoder(int width, int height)
    : m_width(width), m_height(height), m_frame_width(0), m_frame_height(0) {
    m_last[0] = m_last[1] = m_last[2] = 0;
    m_block_type = new huffman(4, s_svq1BlockTypeCodes, s_svq1BlockTypeLengths);
    for (int i = 0; i < 6; ++i) {
        m_intra_multistage[i] = new huffman(8, s_svq1IntraMultistageCodes[i], s_svq1IntraMultistageLengths[i]);
        m_inter_multistage[i] = new huffman(8, s_svq1InterMultistageCodes[i], s_svq1InterMultistageLengths[i]);
    }
    m_intra_mean = new huffman(256, s_svq1IntraMeanCodes, s_svq1IntraMeanLengths);
    m_inter_mean = new huffman(512, s_svq1InterMeanCodes, s_svq1InterMeanLengths);
    m_motion = new huffman(33, s_svq1MotionComponentCodes, s_svq1MotionComponentLengths);
}

svq1_decoder::~svq1_decoder() {
    for (int i = 0; i < 3; ++i) delete[] m_last[i];
    delete m_block_type;
    delete m_intra_mean;
    delete m_inter_mean;
    delete m_motion;
    for (int i = 0; i < 6; ++i) { delete m_intra_multistage[i]; delete m_inter_multistage[i]; }
}

bool svq1_decoder::decode(const uint8_t* data, size_t size) {
    bits s(data, size);
    const uint32_t frame_code = s.get(22);
    if ((frame_code & ~0x70u) || !(frame_code & 0x60u)) return false;
    s.get(8);                                       // temporal reference
    const int frame_type = int(s.get(2));
    if (frame_type == 0) {                          // I frame
        if (frame_code == 0x50 || frame_code == 0x60) s.get(16);   // checksum
        if ((frame_code ^ 0x10) >= 0x50) {          // embedded string
            const int len = int(s.get(8));
            for (int i = 0; i < len - 1; ++i) s.skip(8);
        }
        s.skip(5);
        static const int sizes[7][2] = { {160,120}, {128,96}, {176,144}, {352,288}, {704,576}, {240,180}, {320,240} };
        const int code = int(s.get(3));
        if (code == 7) { m_frame_width = int(s.get(12)); m_frame_height = int(s.get(12)); }
        else { m_frame_width = sizes[code][0]; m_frame_height = sizes[code][1]; }
    } else if (frame_type == 3) {
        return false;
    }
    // Type 2 ("B") frames are predicted from the last reference like P
    // frames but are not references themselves (FFmpeg's reading; the
    // cutscenes alternate P and B).
    const bool reference = frame_type != 2;
    if (!m_frame_width || (frame_type != 0 && !m_last[0])) return false;
    if (s.bit()) {                                  // checksums present
        s.bit(); s.bit();
        s.get(2);
    }
    if (s.bit()) {
        s.skip(8);
        while (s.bit()) s.skip(8);
    }

    const uint32_t y_w = align(uint32_t(m_frame_width), 16), y_h = align(uint32_t(m_frame_height), 16);
    const uint32_t uv_w = align(y_w / 4, 16), uv_h = align(y_h / 4, 16);
    const uint32_t uv_pitch = uv_w + 4;
    uint8_t* current[3];
    bool ok = true;
    for (int i = 0; i < 3 && ok; ++i) {
        const uint32_t w = i ? uv_w : y_w, h = i ? uv_h : y_h, pitch = i ? uv_pitch : y_w;
        current[i] = new uint8_t[pitch * (h + 1)]();
        uint8_t* cp = current[i];
        if (frame_type == 0) {
            for (uint32_t y = 0; y < h && ok; y += 16, cp += 16 * pitch)
                for (uint32_t x = 0; x < w && ok; x += 16)
                    ok = block_intra(s, &cp[x], int(pitch));
        } else {
            std::vector<point> pmv(w / 8 + 3);
            for (uint32_t y = 0; y < h && ok; y += 16, cp += 16 * pitch) {
                for (uint32_t x = 0; x < w && ok; x += 16)
                    ok = delta_block(s, &cp[x], m_last[i], int(pitch), &pmv[0], int(x), int(y));
                pmv[0].x = pmv[0].y = 0;
            }
        }
        if (!ok) { for (int k = 0; k <= i; ++k) delete[] current[k]; return false; }
    }
    // The chroma converter reads one column and one row past the data.
    for (uint32_t i = 0; i < uv_h; ++i) {
        current[1][i * uv_pitch + uv_w] = current[1][i * uv_pitch + uv_w - 1];
        current[2][i * uv_pitch + uv_w] = current[2][i * uv_pitch + uv_w - 1];
    }
    std::memcpy(current[1] + uv_h * uv_pitch, current[1] + (uv_h - 1) * uv_pitch, uv_w + 1);
    std::memcpy(current[2] + uv_h * uv_pitch, current[2] + (uv_h - 1) * uv_pitch, uv_w + 1);
    to_rgba(current[0], current[1], current[2], int(y_w), int(uv_pitch));
    for (int i = 0; i < 3; ++i) {
        if (reference) { delete[] m_last[i]; m_last[i] = current[i]; }
        else delete[] current[i];
    }
    return true;
}

// Full-range BT.601 (ScummVM's kScaleFull), chroma at a quarter resolution
// both ways, interpolated.
void svq1_decoder::to_rgba(const uint8_t* ys, const uint8_t* us, const uint8_t* vs, int y_pitch, int uv_pitch) {
    m_rgba.resize(size_t(m_width) * m_height * 4);
    for (int y = 0; y < m_height; ++y) {
        const int cy = y >> 2, fy = y & 3;
        for (int x = 0; x < m_width; ++x) {
            const int cx = x >> 2, fx = x & 3;
            const int i00 = cy * uv_pitch + cx, i01 = i00 + 1, i10 = i00 + uv_pitch, i11 = i10 + 1;
            const int w00 = (4 - fx) * (4 - fy), w01 = fx * (4 - fy), w10 = (4 - fx) * fy, w11 = fx * fy;
            const int u = (us[i00] * w00 + us[i01] * w01 + us[i10] * w10 + us[i11] * w11 + 8) / 16 - 128;
            const int v = (vs[i00] * w00 + vs[i01] * w01 + vs[i10] * w10 + vs[i11] * w11 + 8) / 16 - 128;
            const int lum = ys[y * y_pitch + x];
            int r = lum + ((91881 * v) >> 16);
            int g = lum - ((22554 * u + 46802 * v) >> 16);
            int b = lum + ((116130 * u) >> 16);
            uint8_t* o = &m_rgba[(size_t(y) * m_width + x) * 4];
            o[0] = uint8_t(r < 0 ? 0 : r > 255 ? 255 : r);
            o[1] = uint8_t(g < 0 ? 0 : g > 255 ? 255 : g);
            o[2] = uint8_t(b < 0 ? 0 : b > 255 ? 255 : b);
            o[3] = 255;
        }
    }
}

bool svq1_decoder::block_intra(bits& s, uint8_t* pixels, int pitch) {
    uint8_t* list[63];
    list[0] = pixels;
    for (int i = 0, m = 1, n = 1, level = 5; i < n; ++i) {
        for (; level > 0; ++i) {
            if (i == m) {
                m = n;
                if (--level == 0) break;
            }
            if (s.bit() == 0) break;
            list[n++] = list[i];
            list[n++] = list[i] + (((level & 1) ? pitch : 1) << ((level / 2) + 1));
        }
        uint8_t* dst = list[i];
        const int width = 1 << ((level + 4) / 2);
        const int height = 1 << ((level + 3) / 2);
        const int stages = m_intra_multistage[level]->symbol(s) - 1;
        if (stages == -1) {
            for (int y = 0; y < height; ++y) std::memset(dst + y * pitch, 0, size_t(width));
            continue;
        }
        if (stages > 0 && level >= 4) return false;
        int mean = m_intra_mean->symbol(s);
        if (stages == 0) {
            for (int y = 0; y < height; ++y) std::memset(dst + y * pitch, mean, size_t(width));
            continue;
        }
        const uint8_t* codebook = reinterpret_cast<const uint8_t*>(s_svq1IntraCodebooks[level]);
        const uint32_t cache = s.get(stages * 4);
        int entries[6];
        for (int j = 0; j < stages; ++j)
            entries[j] = (int(((cache >> ((stages - j - 1) * 4)) & 0xF)) + j * 16) << (level + 1);
        mean -= stages * 128;
        const uint32_t n4 = (uint32_t(mean + (mean >> 31)) << 16) | (uint32_t(mean) & 0xFFFF);
        int cb = 0;                                 // in 32-bit words
        for (int y = 0; y < height; ++y, dst += pitch)
            for (int x = 0; x < width / 4; ++x, ++cb) {
                uint32_t n1 = n4, n2 = n4;
                for (int j = 0; j < stages; ++j) {
                    const uint32_t n3 = rd32(codebook + size_t(entries[j] + cb) * 4) ^ 0x80808080u;
                    n1 += (n3 & 0xFF00FF00) >> 8;
                    n2 += n3 & 0x00FF00FF;
                }
                wr32(dst + x * 4, (clip_pairs(n1) << 8) | clip_pairs(n2));
            }
    }
    return true;
}

bool svq1_decoder::block_non_intra(bits& s, uint8_t* pixels, int pitch) {
    uint8_t* list[63];
    list[0] = pixels;
    for (int i = 0, m = 1, n = 1, level = 5; i < n; ++i) {
        for (; level > 0; ++i) {
            if (i == m) {
                m = n;
                if (--level == 0) break;
            }
            if (s.bit() == 0) break;
            list[n++] = list[i];
            list[n++] = list[i] + (((level & 1) ? pitch : 1) << ((level / 2) + 1));
        }
        uint8_t* dst = list[i];
        const int width = 1 << ((level + 4) / 2);
        const int height = 1 << ((level + 3) / 2);
        const int stages = m_inter_multistage[level]->symbol(s) - 1;
        if (stages == -1) continue;
        if (stages > 0 && level >= 4) return false;
        int mean = m_inter_mean->symbol(s) - 256;
        const uint8_t* codebook = reinterpret_cast<const uint8_t*>(s_svq1InterCodebooks[level]);
        const uint32_t cache = s.get(stages * 4);
        int entries[6];
        for (int j = 0; j < stages; ++j)
            entries[j] = (int(((cache >> ((stages - j - 1) * 4)) & 0xF)) + j * 16) << (level + 1);
        mean -= stages * 128;
        const uint32_t n4 = (uint32_t(mean + (mean >> 31)) << 16) | (uint32_t(mean) & 0xFFFF);
        int cb = 0;
        for (int y = 0; y < height; ++y, dst += pitch)
            for (int x = 0; x < width / 4; ++x, ++cb) {
                uint32_t n3 = rd32(dst + x * 4);
                uint32_t n1 = ((n3 & 0xFF00FF00) >> 8) + n4;
                uint32_t n2 = (n3 & 0x00FF00FF) + n4;
                for (int j = 0; j < stages; ++j) {
                    n3 = rd32(codebook + size_t(entries[j] + cb) * 4) ^ 0x80808080u;
                    n1 += (n3 & 0xFF00FF00) >> 8;
                    n2 += n3 & 0x00FF00FF;
                }
                wr32(dst + x * 4, (clip_pairs(n1) << 8) | clip_pairs(n2));
            }
    }
    return true;
}

bool svq1_decoder::motion_vector(bits& s, point* mv, point** pmv) {
    for (int i = 0; i < 2; ++i) {
        int diff = m_motion->symbol(s);
        if (diff < 0) return false;
        if (diff && s.bit()) diff = -diff;
        if (i == 1) mv->y = ((diff + mid_pred(pmv[0]->y, pmv[1]->y, pmv[2]->y)) << 26) >> 26;
        else        mv->x = ((diff + mid_pred(pmv[0]->x, pmv[1]->x, pmv[2]->x)) << 26) >> 26;
    }
    return true;
}

bool svq1_decoder::motion_inter(bits& s, uint8_t* current, uint8_t* previous, int pitch,
                                point* motion, int x, int y) {
    point* pmv[3];
    pmv[0] = &motion[0];
    if (y == 0) pmv[1] = pmv[2] = pmv[0];
    else { pmv[1] = &motion[x / 8 + 2]; pmv[2] = &motion[x / 8 + 4]; }
    point mv;
    if (!motion_vector(s, &mv, pmv)) return false;
    motion[0].x = motion[x / 8 + 2].x = motion[x / 8 + 3].x = mv.x;
    motion[0].y = motion[x / 8 + 2].y = motion[x / 8 + 3].y = mv.y;
    if (y + (mv.y >> 1) < 0) mv.y = 0;
    if (x + (mv.x >> 1) < 0) mv.x = 0;
    const uint8_t* src = &previous[(x + (mv.x >> 1)) + (y + (mv.y >> 1)) * pitch];
    switch (((mv.y & 1) << 1) + (mv.x & 1)) {
    case 0: put16(current, src, pitch, 16); break;
    case 1: put16_x2(current, src, pitch, 16); break;
    case 2: put16_y2(current, src, pitch, 16); break;
    default: put16_xy2(current, src, pitch, 16); break;
    }
    return true;
}

bool svq1_decoder::motion_inter4v(bits& s, uint8_t* current, uint8_t* previous, int pitch,
                                  point* motion, int x, int y) {
    point* pmv[4];
    pmv[0] = &motion[0];
    if (y == 0) pmv[1] = pmv[2] = pmv[0];
    else { pmv[1] = &motion[x / 8 + 2]; pmv[2] = &motion[x / 8 + 4]; }
    point mv;
    if (!motion_vector(s, &mv, pmv)) return false;
    pmv[0] = &mv;
    if (y == 0) pmv[1] = pmv[2] = pmv[0];
    else pmv[1] = &motion[x / 8 + 3];
    if (!motion_vector(s, &motion[0], pmv)) return false;
    pmv[1] = &motion[0];
    pmv[2] = &motion[x / 8 + 1];
    if (!motion_vector(s, &motion[x / 8 + 2], pmv)) return false;
    pmv[2] = &motion[x / 8 + 2];
    pmv[3] = &motion[x / 8 + 3];
    if (!motion_vector(s, pmv[3], pmv)) return false;
    for (int i = 0; i < 4; ++i) {
        int mvx = pmv[i]->x + (i & 1) * 16;
        int mvy = pmv[i]->y + (i >> 1) * 16;
        if (y + (mvy >> 1) < 0) mvy = 0;
        if (x + (mvx >> 1) < 0) mvx = 0;
        const uint8_t* src = &previous[(x + (mvx >> 1)) + (y + (mvy >> 1)) * pitch];
        switch (((mvy & 1) << 1) + (mvx & 1)) {
        case 0: put8(current, src, pitch, 8); break;
        case 1: put8_x2(current, src, pitch, 8); break;
        case 2: put8_y2(current, src, pitch, 8); break;
        default: put8_xy2(current, src, pitch, 8); break;
        }
        if (i & 1) current += (pitch - 1) * 8;
        else       current += 8;
    }
    return true;
}

bool svq1_decoder::delta_block(bits& s, uint8_t* current, uint8_t* previous, int pitch,
                               point* motion, int x, int y) {
    const int type = m_block_type->symbol(s);
    if (type == BLOCK_SKIP || type == BLOCK_INTRA) {
        motion[0].x = motion[0].y = 0;
        motion[x / 8 + 2].x = motion[x / 8 + 2].y = 0;
        motion[x / 8 + 3].x = motion[x / 8 + 3].y = 0;
    }
    switch (type) {
    case BLOCK_SKIP: {
        const uint8_t* src = &previous[x + y * pitch];
        for (int i = 0; i < 16; ++i) std::memcpy(current + i * pitch, src + i * pitch, 16);
        return true;
    }
    case BLOCK_INTER:
        return motion_inter(s, current, previous, pitch, motion, x, y) && block_non_intra(s, current, pitch);
    case BLOCK_INTER_4V:
        return motion_inter4v(s, current, previous, pitch, motion, x, y) && block_non_intra(s, current, pitch);
    case BLOCK_INTRA:
        return block_intra(s, current, pitch);
    default:
        return false;
    }
}

} // namespace media
} // namespace k2
