// lvl.h — parser for the level description (.lvl).
//
// Two parts: a one-line command line, then INI-style bracketed sections.
// `LevelFileParser::Parse` at 0x441756 handles only the command line; the
// sections are consumed by the caller. See docs/formats/lvl.md.
//
// `;` starts a comment. Designers disabled entries by commenting them rather
// than deleting, so commented lines are common and must be skipped.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_LVL_H
#define K2_ASSETS_LVL_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

// [Bodies] — the actor roster, resolving to master.dat character descriptors.
struct lvl_body {
    int32_t     index;
    std::string path;       // e.g. "geometry\\prince\\master.dat"
};

// [Prince] — "<index> x,y,z,heading". Heading is optional in some levels.
struct lvl_prince_start {
    int32_t index;
    float   x, y, z;
    float   heading;
};

// A [Region] and everything that follows it until the next region. Levels are
// split into regions (e.g. "Parts1to3", "Parts4to6") which are streamed
// separately; the same names appear in savegame resource lists.
struct lvl_region {
    std::string                   name;
    std::vector<lvl_prince_start> prince_starts;
    // Room names. Some levels write a bare name ("prison1"), others include the
    // extension ("AltarChamber.k2a"). Use lvl_room_filename() rather than
    // appending blindly.
    std::vector<std::string>      rooms;
};

// [Fog] — one entry per line, two lines in every shipped level.
// Colour components are 0..255.
struct lvl_fog {
    float   distance;
    float   r, g, b;
    int32_t flag;
};

struct lvl_file {
    std::string              name;            // first bare token of the header
    std::string              command_line;    // the whole first line, verbatim
    std::vector<lvl_body>    bodies;
    std::vector<lvl_region>  regions;
    std::vector<std::string> items;           // [Items] — .k2a filenames
    std::vector<lvl_fog>     fog;
    float                    sky_color[3];    // 0..255
    float                    clip_near, clip_far;
    bool                     has_sky_color;
    bool                     has_clip_planes;

    size_t room_count() const {
        size_t n = 0;
        for (size_t i = 0; i < regions.size(); ++i) n += regions[i].rooms.size();
        return n;
    }
};

bool lvl_parse(const uint8_t* data, size_t size, lvl_file& out,
               std::string* error = 0);

// Returns the geometry filename for a [Rooms] entry, appending ".k2a" only when
// the entry does not already carry an extension.
std::string lvl_room_filename(const std::string& room);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_LVL_H
