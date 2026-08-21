// master.h — parser for character descriptors (master.dat).
//
// Referenced from a .lvl [Bodies] section. The shipped files document their own
// format in comments; see docs/formats/text-and-tuning.md for the transcription.
//
// 41 files ship, in two versions (-v 4 and -v 5) with optional -p and -b flags.
// The layout is positional for the first two lines, then bracketed sections.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_MASTER_H
#define K2_ASSETS_MASTER_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

// [Attributes] — additive bitmask, spelled out one name per line in the file.
enum master_attribute {
    ATTR_CAN_WALK          = 1,
    ATTR_CAN_CRAWL         = 2,
    ATTR_PREFER_TO_CRAWL   = 4,
    ATTR_HAS_WEAPON        = 8,
    ATTR_CAN_FIGHT         = 16,
    ATTR_ONLY_RUNS_AMBIENT = 32,
    ATTR_USES_UP_SWORD     = 64,
    ATTR_IMMUNE_TO_ARROWS  = 128
};

// Weapon style codes.
//
// NOTE: these follow the shipped *data*, which contradicts the comment block in
// master.dat itself. That comment reads "1 for double-weapons; 2 for staff",
// but every staff in the install (StaffL, StaffR, EnemyStaffL) is style 1 and
// every double blade (PrinceDoubleBlade, A4DoubleBlade) is style 2. The comment
// is wrong; trust the data.
enum weapon_style {
    WEAPON_ONE_HANDED = 0,      // swords, axes
    WEAPON_STAFF      = 1,      // staffs and the "L" long weapons
    WEAPON_DOUBLE     = 2,      // double blades
    WEAPON_BOW        = 3
};

// "-B <texture> <length> <tipOpacity> <baseOpacity> <fallOff> <uvRepeat>"
struct master_blur {
    std::string texture;
    float       weapon_length;
    float       tip_opacity;
    float       base_opacity;
    float       falloff;
    float       uv_repeat;
    bool        present;

    master_blur()
        : weapon_length(0), tip_opacity(0), base_opacity(0),
          falloff(0), uv_repeat(0), present(false) {}
};

// "<name> L,C,R,counterL,counterR,specialL,specialC,specialR  damage, style"
struct master_weapon {
    std::string name;
    float       ranges[8];
    float       damage;
    int32_t     style;
    master_blur blur;
};

// A hand attachment: node name plus an offset into the character's hand.
struct master_hand {
    std::string node;
    float       offset[3];
};

struct master_file {
    // header command line
    int32_t     version;        // -v
    bool        proto;          // -p : actor is a .act path, not a directory
    std::string name;           // -n
    std::string shared_body;    // -b, empty when absent (D4Archer shares D4)

    std::string geometry;       // line 2: .k2a or .GUY path
    std::string actor;          // line 3: actor directory, or .act path when proto

    int32_t     attributes;     // OR of master_attribute

    std::vector<master_hand>   hands;
    std::vector<master_weapon> weapons;
    std::vector<std::string>   body_parts;   // collision nodes, order matters
    std::vector<std::string>   animations;   // draw / sheath weapon

    bool  has_grab_points;
    float grab_y_offset;
    float grab_xz_offset;

    // [ArcheryLimits], optional and last. Degrees except max_range (metres).
    bool  has_archery;
    float max_range;
    float aim_speed, aim_acceleration, aim_speed_limit;
    float min_vertical, initial_vertical, max_vertical;
    float min_horizontal, initial_horizontal, max_horizontal;

    master_file()
        : version(0), proto(false), attributes(0),
          has_grab_points(false), grab_y_offset(0), grab_xz_offset(0),
          has_archery(false), max_range(0),
          aim_speed(0), aim_acceleration(0), aim_speed_limit(0),
          min_vertical(0), initial_vertical(0), max_vertical(0),
          min_horizontal(0), initial_horizontal(0), max_horizontal(0) {}
};

bool master_parse(const uint8_t* data, size_t size, master_file& out,
                  std::string* error = 0);

// Human-readable attribute list, for diagnostics.
std::string master_attribute_names(int32_t attributes);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_MASTER_H
