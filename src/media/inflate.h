// inflate.h — ported from the X-Files engine (src/core/xf_inflate.h, same author, GPL-3.0) to C++11.

/* DEFLATE decompression (RFC 1951) with the zlib wrapper (RFC 1950). */
#ifndef XF_INFLATE_H
#define XF_INFLATE_H

#include "media/xf_compat.h"

namespace k2 {
namespace media {


/* Decompresses a zlib stream into `dst`. Returns the number of bytes
 * written, or -1 on error or if `dst` is too small. */
long xf_zlib_uncompress(u8 *dst, size_t dst_size, const u8 *src, size_t src_size);
/* Same for a raw DEFLATE stream without the zlib header. */
long xf_inflate(u8 *dst, size_t dst_size, const u8 *src, size_t src_size);

} // namespace media
} // namespace k2

#endif
