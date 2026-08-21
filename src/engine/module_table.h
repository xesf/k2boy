// module_table.h — the game's module registry.
//
// In the original, `module::module(name, x)` (0x43475C) inserts itself into
// dword_7F2A10 **sorted alphabetically by name** (strcmpi) and stores its own
// index at [this+8]. Module ids are therefore not constants: they are positions
// in the case-insensitive sorted list of module names, fixed at static-init.
//
// That is why WinMain's `Game__PostModuleMessage(0x0C, ...)` is a bare number.
// Reconstructing the list makes it readable: 0x0C is `prison`.
//
// The table below is the recovered list, kept in sorted order so an index into
// it *is* the original's module id. `module_table_verify()` re-checks that
// ordering at runtime rather than trusting the literal.
//
// See docs/plan/05-boot-to-gameplay.md and docs/reference/boot-sequence.md.
//
// C++11. No gfx or platform dependency.

#ifndef K2_ENGINE_MODULE_TABLE_H
#define K2_ENGINE_MODULE_TABLE_H

#include <stddef.h>
#include <stdint.h>

namespace k2 {
namespace engine {

enum module_kind {
    MODULE_KIND_GAME = 0,   // gameModule, has a level
    MODULE_KIND_MENU,       // sMenuModuleClass
    MODULE_KIND_NIS,        // sNISModuleClass  - non-interactive sequence
    MODULE_KIND_TITLE       // sTitleModuleClass
};

struct module_entry {
    const char* name;       // the ctor's name argument; determines the id
    const char* level;      // .lvl for game modules, empty otherwise
    module_kind kind;
};

// The id WinMain posts before entering the event loop. `prison`.
const int32_t MODULE_ID_STARTUP = 0x0C;

const module_entry* module_table(size_t& count);

// -1 when absent. Case-insensitive, as the original's strcmpi insert is.
int32_t module_find(const char* name);

// Null when out of range.
const module_entry* module_at(int32_t id);

// Checks the table is genuinely in strcmpi order, so an index equals the
// original's id, and that MODULE_ID_STARTUP still lands on `prison`.
// Returns true on success; writes a reason on failure.
bool module_table_verify(const char** why);

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_MODULE_TABLE_H
