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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

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

// Every indexed file's relative path, in the order it was found: the order
// decides which of two same-named files a basename lookup returns, so the
// index cache keeps it.
std::vector<std::string> g_order;

void add_file(const std::string& rel, const std::string& abs) {
    g_index.emplace(normalise(rel.c_str()), abs);
    const size_t slash = rel.find_last_of('/');
    std::string base = slash == std::string::npos ? rel : rel.substr(slash + 1);
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] >= 'A' && base[i] <= 'Z') base[i] = char(base[i] - 'A' + 'a');
    g_by_name.emplace(base, abs);
}

// The names at the top of the install, sorted: enough to tell a cached
// index from a different install.
std::string root_signature(const std::string& root) {
    std::vector<std::string> names;
    if (DIR* d = ::opendir(root.c_str())) {
        while (struct dirent* e = ::readdir(d)) names.push_back(e->d_name);
        ::closedir(d);
    }
    std::sort(names.begin(), names.end());
    std::string sig;
    for (size_t i = 0; i < names.size(); ++i) { sig += names[i]; sig += '|'; }
    return sig;
}

// Walking thousands of directory entries takes seconds on a memory card;
// the index is kept in the user directory and reused while the install's
// top level is unchanged.
bool load_index_cache(const std::string& file, const std::string& header) {
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    size_t p = text.find('\n');
    if (p == std::string::npos || text.compare(0, p, header) != 0) return false;
    ++p;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        if (e > p) {
            const std::string rel = text.substr(p, e - p);
            add_file(rel, g_root + "/" + rel);
            g_order.push_back(rel);
        }
        p = e + 1;
    }
    return !g_index.empty();
}

void save_index_cache(const std::string& file, const std::string& header) {
    FILE* f = std::fopen(file.c_str(), "wb");
    if (!f) return;
    std::fputs(header.c_str(), f);
    std::fputc('\n', f);
    for (size_t i = 0; i < g_order.size(); ++i) { std::fputs(g_order[i].c_str(), f); std::fputc('\n', f); }
    std::fclose(f);
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

#ifdef __vita__
        // The Vita's readdir carries each entry's status: a stat() per file
        // over thousands of files takes the better part of a minute.
        const bool is_dir = SCE_S_ISDIR(e->d_stat.st_mode);
        const bool is_reg = SCE_S_ISREG(e->d_stat.st_mode);
#else
        struct stat st;
        if (::stat(child_abs.c_str(), &st) != 0) continue;
        const bool is_dir = S_ISDIR(st.st_mode);
        const bool is_reg = S_ISREG(st.st_mode);
#endif

        if (is_dir) {
            index_dir(child_abs, child_rel);
        } else if (is_reg) {
            // First writer wins. data/Rooms duplicates data/Geometry/Rooms; either
            // resolves to a byte-identical file, so collisions are harmless.
            add_file(child_rel, child_abs);
            g_order.push_back(child_rel);
        }
    }
    ::closedir(d);
}

} // namespace

// --- on-demand lookups -------------------------------------------------------------
//
// The full index costs a walk of every directory in the install - seconds
// on a memory card. Until something needs it, paths are opened directly
// (the Vita's and the Mac's file systems ignore case, so the data's own
// mixed-case names resolve as written), and a name alone is looked for in
// the directories recent lookups touched, listing each of those once. The
// full walk happens only on a miss, or for a listing; its result is cached.

namespace {

bool g_indexed = false;
// The file system ignores case (the Vita's, the Mac's by default): then a
// full path that does not open as written is simply not there, and needs
// no index to say so.
bool g_case_blind = false;

// Lookups come from the loading workers too: one at a time. A platform
// semaphore, as the Vita's workers are not pthreads.
sema g_lock = 0;
struct vfs_guard {
    vfs_guard() { if (!g_lock) g_lock = sema_create(1); sema_wait(g_lock); }
    ~vfs_guard() { sema_signal(g_lock, 1); }
};

struct dir_listing {
    std::string abs;
    bool listed;
    std::unordered_map<std::string, std::string> names;     // lower case -> as on disk
    dir_listing() : listed(false) {}
};
std::unordered_map<std::string, dir_listing> g_dirs;        // by lower-case relative dir
std::vector<std::string> g_recent;                           // most recent first

std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = char(s[i] - 'A' + 'a');
    return s;
}

// Separators fixed, case kept.
std::string as_written(const char* path) {
    std::string out;
    bool prev = false;
    for (const char* p = path; *p; ++p) {
        char c = *p == '\\' ? '/' : *p;
        if (c == '/') { if (prev) continue; prev = true; } else prev = false;
        out.push_back(c);
    }
    while (!out.empty() && out[0] == '/') out.erase(0, 1);
    if (out.compare(0, 2, "./") == 0) out.erase(0, 2);
    return out;
}

void note_dir(const std::string& rel_dir) {
    const std::string key = lower(rel_dir);
    dir_listing& d = g_dirs[key];
    if (d.abs.empty()) d.abs = rel_dir.empty() ? g_root : g_root + "/" + rel_dir;
    std::vector<std::string>::iterator it = std::find(g_recent.begin(), g_recent.end(), key);
    if (it != g_recent.end()) g_recent.erase(it);
    g_recent.insert(g_recent.begin(), key);
    if (g_recent.size() > 24) g_recent.pop_back();
    // The install's top level (the font, GameText.txt) stays searchable.
    if (key != "" && std::find(g_recent.begin(), g_recent.end(), std::string()) == g_recent.end())
        g_recent.push_back(std::string());
}

bool find_in_dir(dir_listing& d, const std::string& lname, std::string& out) {
    if (!d.listed) {
        d.listed = true;
        if (DIR* dd = ::opendir(d.abs.c_str())) {
            while (struct dirent* e = ::readdir(dd)) d.names.emplace(lower(e->d_name), e->d_name);
            ::closedir(dd);
        }
    }
    std::unordered_map<std::string, std::string>::const_iterator it = d.names.find(lname);
    if (it == d.names.end()) return false;
    out = d.abs + "/" + it->second;
    return true;
}

// An actor's images sit in its Textures folder (Actors/rat/Textures/rat.bmp).
bool find_in_textures(const std::string& dir_key, const std::string& lname, std::string& out) {
    dir_listing& d = g_dirs[dir_key];
    std::string sub;
    if (!find_in_dir(d, "textures", sub)) return false;
    const std::string key = dir_key.empty() ? std::string("textures") : dir_key + "/textures";
    dir_listing& t = g_dirs[key];
    if (t.abs.empty()) t.abs = sub;
    return find_in_dir(t, lname, out);
}

void ensure_index(const char* why) {
    if (g_indexed) return;
    g_indexed = true;
    std::printf("vfs: indexing the install (%s)\n", why);
    g_index.clear();
    g_by_name.clear();
    g_order.clear();
    index_dir(g_root, std::string());
    if (!g_index.empty()) save_index_cache(user_dir() + "/vfs-index.txt",
                                           "k2boy vfs 1 " + g_root + " " + root_signature(g_root));
}

bool regular_file(const std::string& abs) {
    struct stat st;
    return ::stat(abs.c_str(), &st) == 0 && S_ISREG(st.st_mode);
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
    g_order.clear();
    g_dirs.clear();
    g_recent.clear();
    note_dir(std::string());
    const std::string cache = user_dir() + "/vfs-index.txt";
    const std::string header = "k2boy vfs 1 " + g_root + " " + root_signature(g_root);
    g_indexed = load_index_cache(cache, header);
    // A name at the top with its case flipped: if it still opens, case does not matter here.
    g_case_blind = false;
    if (DIR* d = ::opendir(g_root.c_str())) {
        while (struct dirent* e = ::readdir(d)) {
            std::string n = e->d_name, f = n;
            bool letters = false;
            for (size_t i = 0; i < f.size(); ++i) {
                if (f[i] >= 'a' && f[i] <= 'z') { f[i] = char(f[i] - 'a' + 'A'); letters = true; }
                else if (f[i] >= 'A' && f[i] <= 'Z') { f[i] = char(f[i] - 'A' + 'a'); letters = true; }
            }
            if (!letters || n == "." || n == "..") continue;
            struct stat a, b;
            g_case_blind = ::stat((g_root + "/" + f).c_str(), &b) == 0 && ::stat((g_root + "/" + n).c_str(), &a) == 0;
            break;
        }
        ::closedir(d);
    }
    if (!g_indexed) { g_index.clear(); g_by_name.clear(); g_order.clear(); }
    return true;
}

namespace {
bool resolve_direct(const char* path, char* out, size_t out_size) {
    const std::string rel = as_written(path);
    const std::string abs = g_root + "/" + rel;
    if (!regular_file(abs) || abs.size() + 1 > out_size) return false;
    const size_t slash = rel.find_last_of('/');
    note_dir(slash == std::string::npos ? std::string() : rel.substr(0, slash));
    std::memcpy(out, abs.c_str(), abs.size() + 1);
    return true;
}
} // namespace

bool fs_probe(const char* path, char* out, size_t out_size) {
    if (!path || !out || out_size == 0) return false;
    {
        vfs_guard guard;
        if (!g_indexed) return resolve_direct(path, out, out_size);
    }
    return fs_resolve(path, out, out_size);
}

bool fs_resolve(const char* path, char* out, size_t out_size) {
    if (!path || !out || out_size == 0) return false;
    vfs_guard guard;
    if (!g_indexed) {
        if (resolve_direct(path, out, out_size)) return true;
        if (g_case_blind) return false;
        // Fallback disabled for now: a miss stays a miss rather than walking
        // the whole install. Listings (fs_list_*) still index.
        // ensure_index(path);
        std::printf("vfs: not found: %s\n", path);
        return false;
    }

    std::unordered_map<std::string, std::string>::const_iterator it =
        g_index.find(normalise(path));
    if (it == g_index.end()) return false;
    {
        const std::string rel = it->second.size() > g_root.size() + 1 ? it->second.substr(g_root.size() + 1) : std::string();
        const size_t slash = rel.find_last_of('/');
        note_dir(slash == std::string::npos ? std::string() : rel.substr(0, slash));
    }

    if (it->second.size() + 1 > out_size) return false;
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    return true;
}

bool fs_resolve_basename(const char* name, char* out, size_t out_size) {
    if (!name || !out || out_size == 0) return false;
    vfs_guard guard;

    // Take everything after the last separator, either kind.
    const char* base = name;
    for (const char* p = name; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;

    std::string key(base);
    for (size_t i = 0; i < key.size(); ++i)
        if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');

    // The directories just used first, indexed or not: a room's textures
    // are beside it, and many names exist in several levels' folders
    // (prison's grate.bmp is not Palace4's), which a whole-install lookup
    // would pick between at random.
    {
        std::string found;
        for (size_t i = 0; i < g_recent.size(); ++i)
            if (find_in_dir(g_dirs[g_recent[i]], key, found) ||
                find_in_textures(g_recent[i], key, found)) {
                if (found.size() + 1 > out_size) return false;
                std::memcpy(out, found.c_str(), found.size() + 1);
                return true;
            }
        // Fallback disabled for now (see fs_resolve).
        // if (!g_indexed) ensure_index(name);
        if (!g_indexed) { std::printf("vfs: not found by name: %s\n", name); return false; }
    }

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
    ::setvbuf(f, NULL, _IONBF, 0);      // one read straight into the buffer, not BUFSIZ at a time
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
    vfs_guard guard;
    ensure_index(ext);

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
    vfs_guard guard;
    ensure_index(ext);

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
