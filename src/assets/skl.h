// skl.h — parser for Motivate skills (.skl): animation clips plus event tracks.
//
// Record layouts were recovered from the writers in pop3dreal.exe rather than
// guessed from data; see docs/formats/skl.md. The JKey layout is validated at
// 99.89% across all 69 396 channels in the install.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_SKL_H
#define K2_ASSETS_SKL_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

// The 28-byte element the joint keyframe is built from.
//
// Component order is not a guess: the element's constructor writes `fld1` into
// the FIRST quaternion slot, so identity is stored (1, 0, 0, 0) — w first.
struct skl_transform {
    float pos[3];
    float quat[4];      // w, x, y, z
};

// "JKey" — 212 bytes, matching sizeof(MfJointKeyframe).
struct skl_joint_key {
    uint32_t      time;
    uint32_t      flag;         // 0 or 1
    uint32_t      field_0c;
    skl_transform xf[6];        // grouped (value, tangent, tangent) x 2
    float         vec3[3];
    uint32_t      field_c4;
    float         ease_in;      // 0.666667 in all but two records
    float         ease_out;     // 0.333333
    uint32_t      field_d0;
};

// "SKey" — 44 bytes. Fires a sound. The payload carries a ".wav" name even
// though the files on disk are .sfx, so lookups must ignore the extension.
struct skl_sound_key {
    uint32_t    time;
    std::string sound;
};

// "vKey" — 20 bytes. Binds a named node to a timed state change.
struct skl_event_key {
    uint32_t    time;
    uint32_t    value;
    std::string target;
};

enum skl_kind {
    SKL_GENERIC = 0,    // "Skil"
    SKL_LOCO,           // "LSkl" — locomotion, MfLocoSkill
    SKL_MANIP           // "MSkl" — manipulation, MfManipSkill
};

struct skl_file {
    std::string kind_tag;       // "Skil" | "MSkl" | "LSkl"
    skl_kind    kind;
    int32_t     version;        // 105 or 106
    std::string chunk_tag;      // "SklK" | "SklS"
    std::string name;

    std::vector<skl_joint_key> joint_keys;
    std::vector<skl_sound_key> sound_keys;
    std::vector<skl_event_key> event_keys;

    // Longest `time` seen. Units unresolved, but consistent across chunk types.
    uint32_t duration() const {
        uint32_t d = 0;
        for (size_t i = 0; i < joint_keys.size(); ++i)
            if (joint_keys[i].time > d) d = joint_keys[i].time;
        for (size_t i = 0; i < sound_keys.size(); ++i)
            if (sound_keys[i].time > d) d = sound_keys[i].time;
        for (size_t i = 0; i < event_keys.size(); ++i)
            if (event_keys[i].time > d) d = event_keys[i].time;
        return d;
    }
};

bool skl_parse(const uint8_t* data, size_t size, skl_file& out,
               std::string* error = 0);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_SKL_H
