// fight.cpp — see fight.h.

#include "engine/fight.h"

#include <cctype>
#include <cstdlib>

namespace k2 {
namespace engine {

namespace {

std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = char(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

template <class F>
void each_line(const char* text, size_t size, F f) {
    size_t i = 0;
    while (i < size) {
        size_t j = i;
        while (j < size && text[j] != '\n' && text[j] != '\r') ++j;
        f(std::string(text + i, j - i));
        while (j < size && (text[j] == '\n' || text[j] == '\r')) ++j;
        i = j;
    }
}

} // namespace

void fight_params::parse_fight_ini(const char* text, size_t size) {
    each_line(text, size, [this](const std::string& raw) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '[') return;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) return;
        m_fight[lower(trim(line.substr(0, eq)))] = std::atof(line.c_str() + eq + 1);
    });
}

void fight_params::parse_enemy_dat(const char* text, size_t size) {
    bool done = false;
    each_line(text, size, [this, &done](const std::string& raw) {
        if (done) return;
        const std::string line = trim(raw);
        if (line.empty() || line[0] == ';') return;
        if (line[0] == '[') { done = true; return; }     // [Archery] and beyond
        char* end = 0;
        const double v = std::strtod(line.c_str(), &end);
        if (end == line.c_str()) return;
        // The identifier: the first word made of letters after the value,
        // skipping a stray '='.
        std::string rest = trim(end);
        if (!rest.empty() && rest[0] == '=') rest = trim(rest.substr(1));
        size_t k = 0;
        while (k < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[k])) || rest[k] == '_')) ++k;
        if (k) m_enemy[lower(rest.substr(0, k))] = v;
    });
}

double fight_params::fight(const std::string& key, double fallback) const {
    std::map<std::string, double>::const_iterator it = m_fight.find(lower(key));
    return it == m_fight.end() ? fallback : it->second;
}

double fight_params::enemy(const std::string& key, double fallback) const {
    std::map<std::string, double>::const_iterator it = m_enemy.find(lower(key));
    return it == m_enemy.end() ? fallback : it->second;
}

} // namespace engine
} // namespace k2
