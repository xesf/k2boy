// xf_compat.h — the small part of the X-Files engine's core (types, byte
// order, file reads, logging) that the code ported from it into src/media
// expects, mapped onto k2boy's VFS. Ported code: same author, GPL-3.0.
//
// C++11.

#ifndef K2_MEDIA_XF_COMPAT_H
#define K2_MEDIA_XF_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace k2 {
namespace media {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#define XF_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define XF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define XF_MAX(a, b) ((a) > (b) ? (a) : (b))
#define XF_FOURCC(a, b, c, d) ((u32)(a) << 24 | (u32)(b) << 16 | (u32)(c) << 8 | (u32)(d))
#define XF_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

inline u16 xf_be16(const u8* p) { return u16(p[0] << 8 | p[1]); }
inline u32 xf_be32(const u8* p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; }
inline u64 xf_be64(const u8* p) { return u64(xf_be32(p)) << 32 | xf_be32(p + 4); }
inline u16 xf_le16(const u8* p) { return u16(p[0] | p[1] << 8); }
inline u32 xf_le32(const u8* p) { return p[0] | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

// A whole file read through the VFS.
struct xf_file { u8* data; size_t size; };
xf_file* xf_open(const char* path);
void xf_close(xf_file* f);
inline u64 xf_size(xf_file* f) { return f->size; }
int xf_read_at(xf_file* f, u64 pos, void* dst, size_t n);

#define xf_warn(...) (std::printf("media: " __VA_ARGS__), std::printf("\n"))
#define xf_error(...) (std::printf("media error: " __VA_ARGS__), std::printf("\n"))

} // namespace media
} // namespace k2

#endif
