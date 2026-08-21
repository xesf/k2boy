// nirtti.h — NetImmerse's hand-rolled type descriptor.
//
// NetImmerse does not use compiler RTTI for its own dispatch. Every NiObject
// subclass carries a static 8-byte descriptor and returns it from virtual slot
// 0. Recovering it gave the complete inheritance graph of both the engine and
// the K2 game layer — 169 classes with exact parent links. See
// docs/reference/nirtti-hierarchy.md.
//
// The original (pop3dreal.exe, ctor at 0x54CC00):
//
//     struct NiRTTI { char* name; const NiRTTI* parent; };
//
// Two deliberate differences here:
//
//   * The original strdup's the name into the heap. String literals already
//     have static storage duration, so this keeps the pointer.
//   * The original uses file-scope statics filled by MSVC dynamic-initializer
//     thunks, which makes descriptor construction order across translation
//     units unspecified. This uses function-local statics instead, so a parent
//     is always constructed before the child that references it. Observable
//     behaviour is the same: one descriptor per class, one parent link,
//     get_rtti() returns it.
//
// C++11.

#ifndef K2_ENGINE_NIRTTI_H
#define K2_ENGINE_NIRTTI_H

#include <stddef.h>

namespace k2 {
namespace engine {

// One per class. Address identity is what matters: two descriptors are the same
// type iff they are the same object, exactly as in the original.
class class_id {
public:
    class_id(const char* name, const class_id* parent)
        : m_name(name), m_parent(parent) {}

    const char*     name()   const { return m_name; }
    const class_id* parent() const { return m_parent; }

    // Walks the parent chain, as the original's type checks do. A class is a
    // kind of itself.
    bool is_kind_of(const class_id& base) const {
        for (const class_id* c = this; c; c = c->m_parent)
            if (c == &base) return true;
        return false;
    }

    // Depth below the root; 0 for a root. Used by the hierarchy self-test.
    size_t depth() const {
        size_t d = 0;
        for (const class_id* c = m_parent; c; c = c->m_parent) ++d;
        return d;
    }

private:
    class_id(const class_id&);
    class_id& operator=(const class_id&);

    const char*     m_name;
    const class_id* m_parent;
};

// Put in the body of a class deriving from ni_object.
#define K2_DECLARE_RTTI()                                                     \
    public:                                                                   \
        static const class_id& rtti();                                        \
        virtual const class_id* get_rtti() const { return &rtti(); }

// Put in exactly one .cpp. `base` must itself declare rtti().
#define K2_DEFINE_RTTI(cls, base)                                             \
    const ::k2::engine::class_id& cls::rtti() {                               \
        static const ::k2::engine::class_id id(#cls, &base::rtti());          \
        return id;                                                            \
    }

#define K2_DEFINE_RTTI_ROOT(cls)                                              \
    const ::k2::engine::class_id& cls::rtti() {                               \
        static const ::k2::engine::class_id id(#cls, 0);                      \
        return id;                                                            \
    }

// Root of the engine's object hierarchy. `NiObject` in the original; one of the
// two roots, the other being `NiExtraData`.
class ni_object {
public:
    virtual ~ni_object() {}

    K2_DECLARE_RTTI()

    bool is_kind_of(const class_id& base) const {
        return get_rtti()->is_kind_of(base);
    }
};

// --- the hierarchy as recovered, for lookup and validation -----------------
//
// 169 classes is far more than the engine has code for yet, so they are carried
// as a table rather than as 169 empty C++ classes. This lets the trap factory
// and the level loader answer "is this name a Trap?" against the shipped data
// before the corresponding class exists. Generated from the binary; see
// docs/reference/nirtti-hierarchy.md.

struct rtti_record {
    const char* name;
    int         parent;     // index into the table, -1 for a root
};

// Returns the table and its length.
const rtti_record* rtti_table(size_t& count);

// Index of `name`, or -1. Linear; the table is small and this is not hot.
int rtti_find(const char* name);

// True when `name` is `base` or derives from it. Both are looked up by name;
// returns false if either is unknown.
bool rtti_is_kind_of(const char* name, const char* base);

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_NIRTTI_H
