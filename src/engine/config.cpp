// config.cpp — see config.h.

#include "engine/config.h"

#include <cctype>

namespace k2 {
namespace engine {

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void trim(std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    s = s.substr(a, b - a);
}

} // namespace

const config_flag* config::find(char key) const {
    const char k = char(std::tolower((unsigned char)key));
    for (size_t i = 0; i < flags.size(); ++i)
        if (flags[i].key == k) return &flags[i];
    return 0;
}

std::string config::startup_file() const {
    const config_flag* f = find('i');
    return f ? f->tail : std::string();
}

std::string config::console_file() const {
    const config_flag* f = find('c');
    if (!f) return std::string();
    // The 'c' handler consumes its own tail; the shipped file writes
    // "-cmd console.dat", so the filename is the last whitespace-separated
    // token. Anything more specific would be guessing at an undecoded handler.
    const std::string& t = f->tail;
    size_t sp = t.find_last_of(" \t");
    return sp == std::string::npos ? t : t.substr(sp + 1);
}

void config_parse(const char* text, size_t size, config& out) {
    out.flags.clear();
    if (!text) return;

    size_t i = 0;
    while (i < size) {
        // ';' comments run to end of line. distribution.dat uses them both for
        // prose and to disable flags, so this must come before '-' handling.
        if (text[i] == ';') {
            while (i < size && text[i] != '\n') ++i;
            continue;
        }
        if (text[i] != '-') { ++i; continue; }

        ++i;                                  // past '-'
        if (i >= size) break;
        // A bare "--" is not a flag; the dispatcher would take '-' as the key.
        config_flag f;
        f.key = char(std::tolower((unsigned char)text[i]));
        ++i;

        // The tail is the rest of the line, minus any trailing comment. The
        // original lets each handler decide how much of this it wants; keeping
        // the whole thing loses nothing and invents nothing.
        size_t start = i;
        while (i < size && text[i] != '\n' && text[i] != ';') ++i;
        f.tail.assign(text + start, i - start);
        trim(f.tail);

        out.flags.push_back(f);
    }
}

} // namespace engine
} // namespace k2
