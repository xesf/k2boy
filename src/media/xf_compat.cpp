// xf_compat.cpp — see xf_compat.h.

#include "media/xf_compat.h"

#include "platform/platform.h"

namespace k2 {
namespace media {

xf_file* xf_open(const char* path) {
    char real[4096];
    if (!plat::fs_resolve(path, real, sizeof(real)) && !plat::fs_resolve_basename(path, real, sizeof(real)))
        return 0;
    std::FILE* fp = std::fopen(real, "rb");
    if (!fp) return 0;
    std::fseek(fp, 0, SEEK_END);
    const long len = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (len < 0) { std::fclose(fp); return 0; }
    xf_file* f = new xf_file;
    f->fp = fp;
    f->size = size_t(len);
    return f;
}

void xf_close(xf_file* f) {
    if (!f) return;
    std::fclose(f->fp);
    delete f;
}

int xf_read_at(xf_file* f, u64 pos, void* dst, size_t n) {
    if (!f || pos > f->size || n > f->size - pos) return -1;
    if (n == 0) return 0;
    if (std::fseek(f->fp, long(pos), SEEK_SET) != 0) return -1;
    return std::fread(dst, 1, n, f->fp) == n ? 0 : -1;
}

} // namespace media
} // namespace k2
