// trap.h — the header every trap shares, and what it means for collision.
//
// Trap's base initialiser (0x454A41) reads argv[0] as a comma-separated list,
// which Traps.txt documents as
//
//     InitialState, ResetEnabled, Visible, Collideable
//
// The three flags start true and are only overwritten by a field the boolean
// parser (0x50BBB6) recognises, so ",true,false" leaves Collideable true.
//
// Visible and Collideable decide where a Generic trap's geometry is filed
// (0x474525) and so which picks see it:
//
//     visible  collideable   filed by      category   picked by floor/wall (mask 6)
//     yes      yes           0x41FF5D      0          yes
//     no       yes           0x42015E      2          yes
//     yes      no            0x420266      1          no
//     no       no            (nothing)     -          no
//
// See docs/reference/collision.md.
//
// C++11.

#ifndef K2_ENGINE_TRAP_H
#define K2_ENGINE_TRAP_H

#include <string>

namespace k2 {
namespace engine {

struct trap_header {
    std::string initial_state;  // Trap +0x134
    bool        reset_enabled;  // +0x144
    bool        visible;        // +0x145, applied through 0x455900 into +0x159
    bool        collideable;    // +0x146, applied through 0x455918 into +0x15A

    trap_header() : reset_enabled(true), visible(true), collideable(true) {}
};

// Port of the argv[0] handling in 0x454A41.
trap_header parse_trap_header(const std::string& argv0);

// Port of 0x50BBB6. Case-insensitive: t/true/yes/on/1 and f/false/no/off/0.
// Returns false, leaving `out` untouched, for anything else, the empty string
// included.
bool parse_trap_bool(const std::string& s, bool& out);

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_TRAP_H
