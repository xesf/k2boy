// master.cpp — see master.h.

#include "assets/master.h"

#include <cstdlib>
#include <cstring>

namespace k2 {
namespace assets {

namespace {

// Strips ';' and '//' comments, trims, drops empty lines.
void split_lines(const uint8_t* data, size_t size, std::vector<std::string>& out) {
    out.clear();
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + size;

    while (p < end) {
        const char* s = p;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        std::string line(s, size_t(p - s));
        while (p < end && (*p == '\n' || *p == '\r')) ++p;

        const size_t semi  = line.find(';');
        if (semi != std::string::npos) line.erase(semi);
        const size_t slash = line.find("//");
        if (slash != std::string::npos) line.erase(slash);

        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        size_t b = line.size();
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;
        if (b > a) out.push_back(line.substr(a, b - a));
    }
}

void split_values(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ',' || c == ' ' || c == '\t' || c == '=') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
}

bool is_section(const std::string& s, std::string& name) {
    if (s.size() < 3 || s[0] != '[' || s[s.size() - 1] != ']') return false;
    name = s.substr(1, s.size() - 2);
    return true;
}

// True when every token is a number — used to spot the weapon-count line, which
// is what terminates the variable-length hand list.
bool all_numeric(const std::string& s) {
    bool any = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '=') continue;
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') { any = true; continue; }
        return false;
    }
    return any;
}

float to_f(const std::string& s) { return float(std::strtod(s.c_str(), 0)); }
int32_t to_i(const std::string& s) { return int32_t(std::strtol(s.c_str(), 0, 10)); }

bool ieq(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return i == a.size() && !b[i];
}

int32_t attribute_bit(const std::string& s) {
    if (ieq(s, "CanWalk"))          return ATTR_CAN_WALK;
    if (ieq(s, "CanCrawl"))         return ATTR_CAN_CRAWL;
    if (ieq(s, "PreferToCrawl"))    return ATTR_PREFER_TO_CRAWL;
    if (ieq(s, "HasWeapon"))        return ATTR_HAS_WEAPON;
    if (ieq(s, "CanFight"))         return ATTR_CAN_FIGHT;
    if (ieq(s, "OnlyRunsAmbient"))  return ATTR_ONLY_RUNS_AMBIENT;
    if (ieq(s, "UsesUpSword"))      return ATTR_USES_UP_SWORD;
    if (ieq(s, "ImmuneToArrows"))   return ATTR_IMMUNE_TO_ARROWS;
    return 0;
}

void parse_header(const std::string& line, master_file& out) {
    std::vector<std::string> tok;
    split_values(line, tok);
    for (size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] == "-v" && i + 1 < tok.size())      out.version = to_i(tok[++i]);
        else if (tok[i] == "-p")                       out.proto = true;
        else if (tok[i] == "-n" && i + 1 < tok.size()) out.name = tok[++i];
        else if (tok[i] == "-b" && i + 1 < tok.size()) out.shared_body = tok[++i];
        else if (tok[i].size() > 2 && tok[i][0] == '-' && tok[i][1] == 'v')
            out.version = to_i(tok[i].substr(2));      // "-v5" with no space
    }
}

} // namespace

bool master_parse(const uint8_t* data, size_t size, master_file& out, std::string* error) {
    out = master_file();

    if (!data || size == 0) { if (error) *error = "empty"; return false; }

    std::vector<std::string> lines;
    split_lines(data, size, lines);
    if (lines.size() < 3) { if (error) *error = "too few lines"; return false; }

    parse_header(lines[0], out);

    // The first two lines after the header are positional: geometry, then the
    // actor (a directory name, or a .act path when -p is set).
    size_t i = 1;
    out.geometry = lines[i++];
    out.actor    = lines[i++];

    std::string section;
    std::vector<std::string> vals;
    int32_t archery_line = 0;

    for (; i < lines.size(); ++i) {
        std::string sec;
        if (is_section(lines[i], sec)) {
            section = sec;
            if (ieq(section, "ArcheryLimits")) { out.has_archery = true; archery_line = 0; }
            continue;
        }

        if (ieq(section, "Attributes")) {
            // Either spelled-out names or a raw additive number.
            if (all_numeric(lines[i])) out.attributes |= to_i(lines[i]);
            else                       out.attributes |= attribute_bit(lines[i]);

        } else if (ieq(section, "Weapons")) {
            if (lines[i][0] == '-' && (lines[i][1] == 'B' || lines[i][1] == 'b')) {
                // Blur line, attaches to the weapon just read.
                if (!out.weapons.empty()) {
                    split_values(lines[i], vals);
                    master_blur& b = out.weapons.back().blur;
                    if (vals.size() >= 2) { b.texture = vals[1]; b.present = true; }
                    if (vals.size() >= 3) b.weapon_length = to_f(vals[2]);
                    if (vals.size() >= 4) b.tip_opacity   = to_f(vals[3]);
                    if (vals.size() >= 5) b.base_opacity  = to_f(vals[4]);
                    if (vals.size() >= 6) b.falloff       = to_f(vals[5]);
                    if (vals.size() >= 7) b.uv_repeat     = to_f(vals[6]);
                }
                continue;
            }

            if (all_numeric(lines[i])) continue;   // the weapon-count line

            split_values(lines[i], vals);
            if (vals.empty()) continue;

            // Hand lines come first and are "<node> x, y, z[, extra]". They are
            // told apart from weapons by arity: weapons carry 8 ranges plus
            // damage and style. The count varies (Prince lists two hands, PG1
            // one), so the list is terminated by shape rather than by a fixed
            // number of lines.
            if (vals.size() >= 10) {
                master_weapon w;
                w.name = vals[0];
                for (int k = 0; k < 8; ++k) w.ranges[k] = to_f(vals[size_t(1 + k)]);
                w.damage = to_f(vals[9]);
                w.style  = vals.size() >= 11 ? to_i(vals[10]) : 0;
                out.weapons.push_back(w);
            } else if (vals.size() >= 4) {
                // Guard against the weapon-count line, which some files write
                // as "4 = number of weapons" — four tokens, but not an offset.
                bool numeric_offset = true;
                for (int k = 1; k <= 3 && numeric_offset; ++k)
                    if (!all_numeric(vals[size_t(k)])) numeric_offset = false;
                if (!numeric_offset) continue;

                master_hand h;
                h.node = vals[0];
                for (int k = 0; k < 3; ++k) h.offset[k] = to_f(vals[size_t(1 + k)]);
                out.hands.push_back(h);
            }

        } else if (ieq(section, "BodyParts")) {
            out.body_parts.push_back(lines[i]);

        } else if (ieq(section, "Animations")) {
            // "<name> - used to draw the weapon"; keep only the name.
            split_values(lines[i], vals);
            if (!vals.empty()) out.animations.push_back(vals[0]);

        } else if (ieq(section, "GrabPoints")) {
            if (!all_numeric(lines[i])) continue;
            if (!out.has_grab_points) { out.grab_y_offset = to_f(lines[i]); out.has_grab_points = true; }
            else                      { out.grab_xz_offset = to_f(lines[i]); }

        } else if (ieq(section, "ArcheryLimits")) {
            split_values(lines[i], vals);
            switch (archery_line) {
                case 0: if (vals.size() >= 1) out.max_range = to_f(vals[0]); break;
                case 1: if (vals.size() >= 3) { out.aim_speed = to_f(vals[0]);
                                                out.aim_acceleration = to_f(vals[1]);
                                                out.aim_speed_limit = to_f(vals[2]); } break;
                case 2: if (vals.size() >= 3) { out.min_vertical = to_f(vals[0]);
                                                out.initial_vertical = to_f(vals[1]);
                                                out.max_vertical = to_f(vals[2]); } break;
                case 3: if (vals.size() >= 3) { out.min_horizontal = to_f(vals[0]);
                                                out.initial_horizontal = to_f(vals[1]);
                                                out.max_horizontal = to_f(vals[2]); } break;
                default: break;
            }
            ++archery_line;
        }
    }

    return true;
}

std::string master_attribute_names(int32_t a) {
    std::string s;
    struct { int32_t bit; const char* name; } table[] = {
        { ATTR_CAN_WALK,          "CanWalk" },
        { ATTR_CAN_CRAWL,         "CanCrawl" },
        { ATTR_PREFER_TO_CRAWL,   "PreferToCrawl" },
        { ATTR_HAS_WEAPON,        "HasWeapon" },
        { ATTR_CAN_FIGHT,         "CanFight" },
        { ATTR_ONLY_RUNS_AMBIENT, "OnlyRunsAmbient" },
        { ATTR_USES_UP_SWORD,     "UsesUpSword" },
        { ATTR_IMMUNE_TO_ARROWS,  "ImmuneToArrows" }
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (a & table[i].bit) {
            if (!s.empty()) s += "|";
            s += table[i].name;
        }
    }
    return s.empty() ? std::string("none") : s;
}

} // namespace assets
} // namespace k2
