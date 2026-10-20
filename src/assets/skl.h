// skl.h — parser for Motivate skills (.skl): animation clips plus event tracks.
//
// The file is read the way the original reads it: as an MfStream object graph,
// field by field, following the readers in pop3dreal.exe. It used to be found
// by scanning for 4CC tags, which could not see the SklS joint data at all and
// mistook the skill's duration for an event-key field. See docs/formats/skl.md.
//
//   MfSkill base          0x60C1ED   header, joint count, SklK key lists, root
//   Skil                  0x623CE6   SklS joint paths, key lists, duration
//   LSkl  (MfLocoSkill)   0x63D3A0   locomotion block after Skil
//   MSkl  (MfManipSkill)  0x63F4DD   manipulation block after Skil (not decoded)
//   MfJointPath ("JPat")  0x640E59   one SklS joint: RLE position + rotation
//   MfKeyList             0x61196A   count, then tagged keyframe objects
//
// Validated: all 2363 .skl files in the install parse to exactly EOF (Skil and
// LSkl), or to the start of the undecoded MSkl block.
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

// "JKey" — 212 bytes, matching sizeof(MfJointKeyframe). Only SklK skills use it.
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

// "SKey" — MfSoundKeyframe, read by 0x64058A. Fires a sound. The name is a
// ".wav" even though the files on disk are .sfx, so lookups must ignore the
// extension.
struct skl_sound_key {
    uint32_t    time;
    std::string sound;
};

// "vKey" — MfEventKeyframe, read by 0x6407E9: time, flag, value, target name.
struct skl_event_key {
    uint32_t    time;
    uint32_t    value;
    std::string target;
};

// "JPat" — MfJointPath, one per joint in an SklS skill. A sampled clip: one key
// per frame, `interval_ms` apart, run-length encoded on disk. The writer's
// compaction pass (0x640CF8) drops trailing keys equal to the last one, so a
// track can be shorter than `frames`; the sampler clamps to its last key, and a
// track of length 1 is a constant.
struct skl_joint_path {
    uint32_t           interval_ms;   // +0x04, 33 by default (the ctor's 0x21)
    uint32_t           frames;        // +0x10, clip length in frames
    std::vector<float> pos;           // +0x14, 3 floats per key
    std::vector<float> rot;           // +0x18, 4 floats per key, w first

    skl_joint_path() : interval_ms(33), frames(0) {}

    size_t pos_keys() const { return pos.size() / 3; }
    size_t rot_keys() const { return rot.size() / 4; }
};

// One joint slot of a skill, indexed like the actor's .act nodes. A slot with
// `present == false` is not driven by the skill; the original then keeps the
// node's own transform (MfNode +0x28), see 0x63BA81.
struct skl_joint {
    bool                       present;
    skl_joint_path             path;    // SklS
    std::vector<skl_joint_key> keys;    // SklK

    skl_joint() : present(false) {}
};

enum skl_kind {
    SKL_GENERIC = 0,    // "Skil"
    SKL_LOCO,           // "LSkl" — locomotion, MfLocoSkill
    SKL_MANIP           // "MSkl" — manipulation, MfManipSkill
};

struct skl_file {
    std::string kind_tag;       // "Skil" | "MSkl" | "LSkl"
    skl_kind    kind;
    int32_t     version;        // 105 or 106; gates fields as in the readers
    std::string chunk_tag;      // "SklK" (keyframe splines) | "SklS" (sampled)
    std::string name;

    std::vector<skl_joint> joints;      // MfSkill +0x18 count, +0x1C array
    skl_transform root;                 // +0x20
    int32_t       interpolate;          // +0x44, passed to the sampler as its flag
    int32_t       field_48;
    float         field_4c[4];          // v >= 0x67
    uint32_t      duration_ms;          // Skil +0x80
    int32_t       field_84;             // Skil +0x84, 1 before v34

    // LSkl only (0x63D3A0). Meanings not yet established.
    float         loco[5];              // +0x98 .. +0xA8

    // MSkl only: bytes left after the Skil part, i.e. the manipulation block.
    size_t        manip_bytes;

    // Flattened for statistics; the per-joint lists are in `joints`.
    std::vector<skl_joint_key> joint_keys;
    std::vector<skl_sound_key> sound_keys;
    std::vector<skl_event_key> event_keys;

    skl_file() : kind(SKL_GENERIC), version(0), interpolate(0), field_48(0),
                 duration_ms(0), field_84(0), manip_bytes(0) {
        for (int i = 0; i < 4; ++i) field_4c[i] = 0.0f;
        for (int i = 0; i < 5; ++i) loco[i] = 0.0f;
    }

    // Clip length in milliseconds, as the skill stores it.
    uint32_t duration() const { return duration_ms; }
};

bool skl_parse(const uint8_t* data, size_t size, skl_file& out,
               std::string* error = 0);

// Port of MfJointPath's sampler (0x640B01). Writes the joint's local transform
// at `time_ms`. With `interpolate` set, blends toward the next key: positions
// linearly, rotations by the original's slerp (0x612B97).
void skl_sample(const skl_joint_path& p, uint32_t time_ms, bool interpolate,
                float pos[3], float quat[4]);

// SklK playback: the pose a joint's keyframe list gives at `time_ms`. Port of
// JKey::Interpolate (0x612278) for the transform joints use, xf[0]:
//   * find the keys around t, u = (t - t0) / (t1 - t0)
//   * ease u with the cubic 3s^2u*a.ease_out + 3su^2*b.ease_in + u^3 (s = 1-u);
//     the shipped 1/3 and 2/3 make that exactly linear
//   * mode 2 (+0xD0) holds key a; otherwise xf[0] is lerped and slerped
// (xf[3], the Bezier channel, does not drive the node.) Clamps outside the
// keys. False when the list is empty.
bool skl_sample_keys(const std::vector<skl_joint_key>& keys, uint32_t time_ms,
                     float pos[3], float quat[4]);

// Port of the quaternion slerp at 0x612B97 with spin 0: shortest arc, falling
// back to a linear blend when the keys are nearly equal, then renormalised.
void skl_slerp(const float a[4], const float b[4], float t, float out[4]);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_SKL_H
