// lvl.cpp — see lvl.h.

#include "assets/lvl.h"

#include <cstdlib>
#include <cstring>

namespace k2 {
namespace assets {

namespace {

// Splits into lines, strips ';' comments and surrounding whitespace, and drops
// anything left empty.
void split_lines(const uint8_t* data, size_t size, std::vector<std::string>& out) {
    out.clear();
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + size;

    while (p < end) {
        const char* line_start = p;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        std::string line(line_start, size_t(p - line_start));
        while (p < end && (*p == '\n' || *p == '\r')) ++p;

        const size_t comment = line.find(';');
        if (comment != std::string::npos) line.erase(comment);

        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        size_t b = line.size();
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;

        if (b > a) out.push_back(line.substr(a, b - a));
    }
}

// A section header is "[Name]" possibly followed by trailing text on the same
// line: Solar1 writes "[Fog] rooms" and cliffsBU writes "[Fog] outside" and
// "[SkyColor] outside". Requiring the line to *end* with ']' missed those, so
// the parser stayed in [Rooms] and pushed the fog header and its numbers in as
// room names - Solar1 reported 8 rooms instead of 5 and zero fog entries.
bool is_section(const std::string& s, std::string& name) {
    if (s.size() < 3 || s[0] != '[') return false;
    const size_t close = s.find(']');
    if (close == std::string::npos || close < 2) return false;
    name = s.substr(1, close - 1);
    return true;
}

// Splits on commas and whitespace alike: shipped levels use both, sometimes in
// the same line ("0 22,0.01,1.92,83.25").
void split_values(const std::string& s, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ',' || c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
}

float to_f(const std::string& s) { return float(std::strtod(s.c_str(), 0)); }
int32_t to_i(const std::string& s) { return int32_t(std::strtol(s.c_str(), 0, 10)); }

} // namespace

bool lvl_parse(const uint8_t* data, size_t size, lvl_file& out, std::string* error) {
    out.name.clear();
    out.command_line.clear();
    out.bodies.clear();
    out.regions.clear();
    out.items.clear();
    out.fog.clear();
    out.sky_color[0] = out.sky_color[1] = out.sky_color[2] = 0.0f;
    out.clip_near = 0.1f;
    out.clip_far  = 1000.0f;
    out.has_sky_color   = false;
    out.has_clip_planes = false;

    if (!data || size == 0) { if (error) *error = "empty"; return false; }

    std::vector<std::string> lines;
    split_lines(data, size, lines);
    if (lines.empty()) { if (error) *error = "no content"; return false; }

    // First non-comment line is the command line; its first bare token is the
    // level name. Flags are decoded in docs/formats/lvl.md but the section data
    // below is what a renderer actually needs.
    out.command_line = lines[0];
    {
        std::vector<std::string> tok;
        split_values(lines[0], tok);
        if (!tok.empty() && tok[0][0] != '-') out.name = tok[0];
    }

    std::string section;
    std::vector<std::string> vals;

    for (size_t i = 1; i < lines.size(); ++i) {
        std::string sec;
        if (is_section(lines[i], sec)) {
            section = sec;

            // A [Region] opens a new group; [Prince] and [Rooms] that follow
            // belong to it. Levels that omit [Region] still need somewhere to
            // put their rooms, so one is synthesised on demand below.
            if (section == "Region" && i + 1 < lines.size()) {
                lvl_region r;
                r.name = lines[++i];
                out.regions.push_back(r);
            }
            continue;
        }

        if (section == "Bodies") {
            split_values(lines[i], vals);
            if (vals.size() >= 2) {
                lvl_body b;
                b.index = to_i(vals[0]);
                b.path  = vals[1];
                out.bodies.push_back(b);
            }
            // a lone number is the count; ignore it and trust the line count
        } else if (section == "Prince") {
            split_values(lines[i], vals);
            // The body may be named as well as indexed:
            //   0 22,0.01,1.92,83.25            (prisonfix)
            //   0 "prince" -153.10,-85,240,110  (Solar1, every cliffs region)
            // Taking vals[1] as x turned the quoted name into 0.0 and shifted
            // every coordinate one place, so all ten cliffs regions spawned in
            // the wrong spot. prisonfix uses the short form, which is why the
            // boot level never showed it.
            size_t v = 1;
            if (v < vals.size() && !vals[v].empty() && vals[v][0] == '"') ++v;
            if (vals.size() >= v + 3) {
                if (out.regions.empty()) out.regions.push_back(lvl_region());
                lvl_prince_start p;
                p.index   = to_i(vals[0]);
                p.x       = to_f(vals[v + 0]);
                p.y       = to_f(vals[v + 1]);
                p.z       = to_f(vals[v + 2]);
                p.heading = vals.size() >= v + 4 ? to_f(vals[v + 3]) : 0.0f;
                out.regions.back().prince_starts.push_back(p);
            }
        } else if (section == "Rooms") {
            // Skip the leading count; room names never start with a digit.
            if (lines[i].find_first_not_of("0123456789") == std::string::npos) continue;
            if (out.regions.empty()) out.regions.push_back(lvl_region());
            out.regions.back().rooms.push_back(lines[i]);
        } else if (section == "Items") {
            out.items.push_back(lines[i]);
        } else if (section == "Fog") {
            split_values(lines[i], vals);
            if (vals.size() >= 4) {
                lvl_fog f;
                f.distance = to_f(vals[0]);
                f.r = to_f(vals[1]);
                f.g = to_f(vals[2]);
                f.b = to_f(vals[3]);
                f.flag = vals.size() >= 5 ? to_i(vals[4]) : 0;
                out.fog.push_back(f);
            }
        } else if (section == "SkyColor") {
            split_values(lines[i], vals);
            if (vals.size() >= 3) {
                out.sky_color[0] = to_f(vals[0]);
                out.sky_color[1] = to_f(vals[1]);
                out.sky_color[2] = to_f(vals[2]);
                out.has_sky_color = true;
            }
        } else if (section == "ClipPlanes") {
            split_values(lines[i], vals);
            if (vals.size() >= 2) {
                out.clip_near = to_f(vals[0]);
                out.clip_far  = to_f(vals[1]);
                out.has_clip_planes = true;
            }
        }
        // Unhandled sections (SkyDome, TextureAnimation, PrinceMaterial,
        // RotateSkyDome, Enemy, ...) are skipped by falling through.
    }

    return true;
}

std::string lvl_room_filename(const std::string& room) {
    const size_t dot   = room.rfind('.');
    const size_t slash = room.find_last_of("/\\");
    const bool has_ext = dot != std::string::npos &&
                         (slash == std::string::npos || dot > slash);
    return has_ext ? room : room + ".k2a";
}

} // namespace assets
} // namespace k2
