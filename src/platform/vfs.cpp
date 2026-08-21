// vfs.cpp — case-insensitive virtual filesystem over the retail install.
//
// Mandatory, not a convenience. The shipped data mixes `.K2A` / `.k2a` / `.K2a`
// inside one directory, embeds Windows-style backslash paths
// (`geometry\rooms\prisonfix\prison3.k2a`), uses doubled backslashes in .lvl
// [Bodies] entries, and must resolve on a case-sensitive volume.
//
// Strategy: index the tree once at mount, keyed by lowercased forward-slash
// relative path. Lookups are then a single hash probe.
//
// Portable C++11 + POSIX dirent. No platform headers.

#include "platform/platform.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace k2 {
namespace plat {

namespace {

std::unordered_map<std::string, std::string> g_index;     // normalised path -> real path
std::unordered_map<std::string, std::string> g_by_name;   // lowercase basename -> real path
std::string                                  g_root;

// Backslash to slash, collapse repeated separators, strip "./", lowercase.
std::string normalise(const char* path) {
    std::string out;
    out.reserve(std::strlen(path));
    bool prev_sep = false;
    for (const char* p = path; *p; ++p) {
        char c = *p;
        if (c == '\\') c = '/';
        if (c == '/') {
            if (prev_sep) continue;      // collapse "\\\\" and "//"
            prev_sep = true;
        } else {
            prev_sep = false;
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        }
        out.push_back(c);
    }
    // strip a leading separator so mounts and lookups agree
    if (!out.empty() && out[0] == '/') out.erase(0, 1);
    if (out.compare(0, 2, "./") == 0) out.erase(0, 2);
    return out;
}

void index_dir(const std::string& abs, const std::string& rel) {
    DIR* d = ::opendir(abs.c_str());
    if (!d) return;

    while (struct dirent* e = ::readdir(d)) {
        const char* name = e->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        std::string child_abs = abs + "/" + name;
        std::string child_rel = rel.empty() ? std::string(name) : rel + "/" + name;

        struct stat st;
        if (::stat(child_abs.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            index_dir(child_abs, child_rel);
        } else if (S_ISREG(st.st_mode)) {
            // First writer wins. data/Rooms duplicates data/Geometry/Rooms; either
            // resolves to a byte-identical file, so collisions are harmless.
            g_index.emplace(normalise(child_rel.c_str()), child_abs);

            std::string base(name);
            for (size_t i = 0; i < base.size(); ++i)
                if (base[i] >= 'A' && base[i] <= 'Z') base[i] = char(base[i] - 'A' + 'a');
            g_by_name.emplace(base, child_abs);
        }
    }
    ::closedir(d);
}

} // namespace

bool fs_mount(const char* root_path) {
    if (!root_path || !*root_path) return false;

    struct stat st;
    if (::stat(root_path, &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    g_root = root_path;
    while (!g_root.empty() && g_root[g_root.size() - 1] == '/')
        g_root.erase(g_root.size() - 1);

    g_index.clear();
    g_by_name.clear();
    index_dir(g_root, std::string());
    return !g_index.empty();
}

bool fs_resolve(const char* path, char* out, size_t out_size) {
    if (!path || !out || out_size == 0) return false;

    std::unordered_map<std::string, std::string>::const_iterator it =
        g_index.find(normalise(path));
    if (it == g_index.end()) return false;

    if (it->second.size() + 1 > out_size) return false;
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    return true;
}

bool fs_resolve_basename(const char* name, char* out, size_t out_size) {
    if (!name || !out || out_size == 0) return false;

    // Take everything after the last separator, either kind.
    const char* base = name;
    for (const char* p = name; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;

    std::string key(base);
    for (size_t i = 0; i < key.size(); ++i)
        if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');

    std::unordered_map<std::string, std::string>::const_iterator it = g_by_name.find(key);
    if (it == g_by_name.end()) return false;
    if (it->second.size() + 1 > out_size) return false;
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    return true;
}

namespace {
uint8_t* read_real(const char* real, size_t& size_out) {
    size_out = 0;
    FILE* f = ::fopen(real, "rb");
    if (!f) return NULL;
    ::fseek(f, 0, SEEK_END);
    long len = ::ftell(f);
    ::fseek(f, 0, SEEK_SET);
    if (len < 0) { ::fclose(f); return NULL; }
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(size_t(len) + 1));
    if (!buf) { ::fclose(f); return NULL; }
    size_t got = ::fread(buf, 1, size_t(len), f);
    ::fclose(f);
    buf[got] = 0;
    size_out = got;
    return buf;
}
} // namespace

uint8_t* fs_read_basename(const char* name, size_t& size_out) {
    size_out = 0;
    char real[4096];
    if (!fs_resolve_basename(name, real, sizeof(real))) return NULL;
    return read_real(real, size_out);
}

uint8_t* fs_read(const char* path, size_t& size_out) {
    size_out = 0;

    char real[4096];
    if (!fs_resolve(path, real, sizeof(real))) return NULL;
    return read_real(real, size_out);
}

void fs_free(uint8_t* data) {
    std::free(data);
}

void fs_list_by_extension(const char* ext, std::vector<std::string>& out) {
    out.clear();
    if (!ext || !*ext) return;

    std::string suffix(ext);
    for (size_t i = 0; i < suffix.size(); ++i)
        if (suffix[i] >= 'A' && suffix[i] <= 'Z') suffix[i] = char(suffix[i] - 'A' + 'a');

    for (std::unordered_map<std::string, std::string>::const_iterator it = g_by_name.begin();
         it != g_by_name.end(); ++it) {
        const std::string& name = it->first;      // already lowercased at index time
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(name);
    }
}

void fs_list_paths_by_extension(const char* ext, std::vector<std::string>& out) {
    out.clear();
    if (!ext || !*ext) return;

    std::string suffix(ext);
    for (size_t i = 0; i < suffix.size(); ++i)
        if (suffix[i] >= 'A' && suffix[i] <= 'Z') suffix[i] = char(suffix[i] - 'A' + 'a');

    for (std::unordered_map<std::string, std::string>::const_iterator it = g_index.begin();
         it != g_index.end(); ++it) {
        const std::string& path = it->first;      // lowercased at index time
        if (path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(path);
    }
}

size_t fs_file_count() {
    return g_index.size();
}

} // namespace plat
} // namespace k2
