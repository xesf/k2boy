// xf_compat.cpp — see xf_compat.h.

#include "media/xf_compat.h"

#include "platform/platform.h"

namespace k2 {
namespace media {

xf_file* xf_open(const char* path) {
    size_t size = 0;
    uint8_t* d = plat::fs_read(path, size);
    if (!d) d = plat::fs_read_basename(path, size);
    if (!d) return 0;
    xf_file* f = new xf_file;
    f->data = d;
    f->size = size;
    return f;
}

void xf_close(xf_file* f) {
    if (!f) return;
    plat::fs_free(f->data);
    delete f;
}

int xf_read_at(xf_file* f, u64 pos, void* dst, size_t n) {
    if (!f || pos > f->size || n > f->size - pos) return -1;
    std::memcpy(dst, f->data + pos, n);
    return 0;
}

} // namespace media
} // namespace k2
