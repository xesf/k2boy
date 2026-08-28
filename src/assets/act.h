// act.h — parser for Motivate actor prototypes (.act).
//
// Header and property table are confirmed and validated against all 115 APFM
// files in the install; see docs/formats/act.md. The node / articulation block
// that follows is only partially understood — see the caveat on act_node.
//
// No dependency on gfx or platform.
//
// C++11.

#ifndef K2_ASSETS_ACT_H
#define K2_ASSETS_ACT_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace assets {

enum act_value_type {
    ACT_VALUE_NONE = 0,
    ACT_VALUE_STRING,       // "rtSV"
    ACT_VALUE_FLOAT,        // "olFV"
    ACT_VALUE_VEC3          // "3eVV"
};

// One entry of the Motivate property table: MF_bndRadius, MF_locoSkill, ...
// Every APFM file carries exactly the same 11 keys.
struct act_property {
    std::string    name;
    act_value_type type;
    std::string    string_value;
    float          float_value;
    float          vec3[3];

    act_property() : type(ACT_VALUE_NONE), float_value(0) {
        vec3[0] = vec3[1] = vec3[2] = 0.0f;
    }
};

// A node of the actor's articulation.
//
// Layout recovered from MfProtoNode's constructor and writer: a length-prefixed
// name followed by a 28-byte transform — the same transform type a .skl joint
// key uses. Records are byte-packed, so the floats are not 4-byte aligned.
//
// Validated: for Prince, PG1 and A3, all 12 [BodyParts] entries named in the
// character's master.dat appear among these nodes.
//
// The parent/child linkage is NOT yet decoded. MfProtoNode serialises its child
// MfDblList as one 20-byte record per child, and those 20 bytes are unknown, so
// `nodes` is a flat list in file order rather than a tree.
// The skeleton is a tree. `parent` is an index into act_file::nodes, -1 for the
// root; `child_count` is what the file stores. Nodes appear in depth-first
// order, so parent links are recovered by consuming the counts in sequence.
// See docs/formats/act.md.
struct act_node {
    std::string name;
    float       pos[3];
    float       quat[4];        // w, x, y, z — w first, as in .skl
    size_t      offset;         // byte offset of the length prefix
    int32_t     child_count;    // as stored in the file
    int32_t     parent;         // index into act_file::nodes, -1 for the root
};

struct act_file {
    // True when the child counts consumed exactly, giving one root and no
    // orphans. False means the tree was not recovered and parent links are -1.
    bool         tree_ok;
    std::string  magic;         // "APFM" or " MFB"
    std::string  type;          // "ActP"
    int32_t      version;       // 48, 105 or 106
    std::string  name;
    std::string  display_name;

    std::vector<act_property> properties;
    std::vector<act_node>     nodes;

    // The three " MFB" files (arrow, bigaxes, BlankCamera) use an older
    // container and carry no property table.
    bool legacy_container;

    act_file() : version(0), legacy_container(false) {}

    const act_property* find(const char* key) const;
};

bool act_parse(const uint8_t* data, size_t size, act_file& out,
               std::string* error = 0);

} // namespace assets
} // namespace k2

#endif // K2_ASSETS_ACT_H
