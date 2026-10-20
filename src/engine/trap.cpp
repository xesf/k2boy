// trap.cpp — see trap.h.

#include "engine/trap.h"

#include <cctype>

namespace k2 {
namespace engine {

namespace {

bool equals_ci(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && !b[i];
}

// 0x50BD02: the index-th comma-separated field, untrimmed; empty when absent.
std::string field(const std::string& s, int index) {
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        const size_t c = s.find(',', start);
        if (c == std::string::npos) return std::string();
        start = c + 1;
    }
    const size_t end = s.find(',', start);
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

bool parse_trap_bool(const std::string& s, bool& out) {
    if (s.empty()) return false;
    if (equals_ci(s, "t") || equals_ci(s, "true") || equals_ci(s, "yes") ||
        equals_ci(s, "on") || equals_ci(s, "1")) {
        out = true;
        return true;
    }
    if (equals_ci(s, "f") || equals_ci(s, "false") || equals_ci(s, "no") ||
        equals_ci(s, "off") || equals_ci(s, "0")) {
        out = false;
        return true;
    }
    return false;
}

trap_header parse_trap_header(const std::string& argv0) {
    trap_header h;
    h.initial_state = field(argv0, 0);
    parse_trap_bool(field(argv0, 1), h.reset_enabled);
    parse_trap_bool(field(argv0, 2), h.visible);
    parse_trap_bool(field(argv0, 3), h.collideable);
    return h;
}

} // namespace engine
} // namespace k2
