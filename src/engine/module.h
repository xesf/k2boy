// module.h — the engine's top-level state machine.
//
// Mirrors the `module` interface recovered from pop3dreal.exe: a six-virtual
// base with five subclasses (game, menu, title, NIS, finale). See
// docs/reference/engine-structure.md for the vtable diff this comes from.
//
// Slot numbering is deliberately preserved so the code and the disassembly can
// be checked against each other. Where a slot's role is not yet established the
// method keeps its slot name (vf1, vf2, vf4) rather than an invented one — a
// wrong name is worse than no name when the point is to stay verifiable.
//
// C++11. No dependency on gfx or platform beyond the event type.

#ifndef K2_ENGINE_MODULE_H
#define K2_ENGINE_MODULE_H

#include <stdint.h>

#include "platform/platform.h"

namespace k2 {
namespace engine {

enum module_id {
    MODULE_NONE = 0,
    MODULE_TITLE,       // sTitleModuleClass
    MODULE_MENU,        // sMenuModuleClass
    MODULE_GAME,        // gameModule
    MODULE_NIS,         // sNISModuleClass — non-interactive sequence
    MODULE_FINALE,      // sFinaleModuleClass — a gameModule with slot 3 replaced
    MODULE_COUNT
};

// Base class. Slots 0-3 are mandatory in the original (slot 0 points at the
// pure-virtual stub); slots 4 and 5 have real no-op bases and are optional.
class module {
public:
    virtual ~module() {}

    // slot 0 — pure virtual in the original: every subclass overrides it.
    virtual bool vf0() = 0;

    // slot 1 — overridden by all five subclasses, each chaining to the base.
    virtual void vf1() {}

    // slot 2 — three dwords, returns nothing. gameModule's is a nullsub while
    // sMenuModuleClass's is 248 bytes, so the work is not uniform across
    // subclasses.
    virtual void vf2() {}

    // slot 3 — event handler. Six dwords in the original; the base returns 0,
    // meaning "not handled", and handlers set a flag through an out-pointer.
    // Return true when the event is consumed.
    virtual bool on_event(const plat::event& e) { (void)e; return false; }

    // slot 4 — optional. sTitleModuleClass leaves it at the base no-op.
    virtual void vf4() {}

    // slot 5 — optional. Only gameModule and sFinaleModuleClass override it.
    virtual void vf5() {}

    virtual module_id id() const = 0;
};

// Owns the active module and the transition between them, standing in for
// `moduleManager`. Kept minimal until the original's transition logic is read.
class module_manager {
public:
    module_manager();
    ~module_manager();

    void   set(module* m);      // takes ownership; destroys the previous one
    module* current() const { return m_current; }

    // Drives the active module for one frame. The mapping from these calls onto
    // slots 1/2/4 is not yet established, so this deliberately does the minimum:
    // pump events, then run the frame hooks in slot order.
    void dispatch_event(const plat::event& e);
    void frame();

private:
    module_manager(const module_manager&);
    module_manager& operator=(const module_manager&);

    module* m_current;
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_MODULE_H
