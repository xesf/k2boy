// node.h — the scene graph.
//
// Ports NiAVObject / NiNode, the spine every other object hangs off. See
// docs/reference/nirtti-hierarchy.md for the class tree and
// docs/reference/scene-graph.md for the field offsets this mirrors.
//
// The transform layout is not invented. NiAVObject__LoadBinary (0x551EC0) reads,
// in order, into consecutive fields of `this`:
//
//     +0x2C  1 byte                     flags
//     +0x30  NiStream__ReadVector3      translation   (12 bytes -> next at +0x3C)
//     +0x3C  NiStream__ReadMatrix3      rotation      (36 bytes -> next at +0x60)
//     +0x60  4 bytes                    scale
//
// The gaps between the destinations fix the sizes exactly: 0x3C-0x30 = 12 and
// 0x60-0x3C = 36. So the local transform is vec3 + 3x3 + uniform scale, which is
// NetImmerse's NiTransform.
//
// Rotation is kept as a 3x3 rather than a quaternion deliberately: that is what
// the format stores and what the engine composes, and converting on load would
// mean converting back to compare against the original.
//
// No gfx or platform dependency: a node knows where it is, not how it is drawn.
//
// C++11.

#ifndef K2_ENGINE_NODE_H
#define K2_ENGINE_NODE_H

#include <stdint.h>
#include <string>
#include <vector>

#include "engine/nirtti.h"

namespace k2 {
namespace engine {

// NiTransform: rotation is row-major 3x3, applied as R*v*scale + t.
struct transform {
    float translate[3];
    float rotate[9];
    float scale;

    transform() { set_identity(); }

    void set_identity() {
        translate[0] = translate[1] = translate[2] = 0.0f;
        for (int i = 0; i < 9; ++i) rotate[i] = 0.0f;
        rotate[0] = rotate[4] = rotate[8] = 1.0f;
        scale = 1.0f;
    }

    // Y-axis rotation, the only one a spawn point specifies.
    void set_yaw(float radians);

    // Column-major 4x4, ready for gfx::set_model.
    void to_matrix(float* m16) const;
};

// out = a * b, i.e. apply b in a's space. Mirrors how the original composes a
// child's local transform with its parent's world transform.
void transform_compose(const transform& a, const transform& b, transform& out);

// Base scene-graph node. `NiAVObject` and `NiNode` are collapsed into one type:
// the engine has no use for an AVObject that cannot have children, and the
// recovered hierarchy shows the game layer only ever derives from NiNode.
class ni_node : public ni_object {
    K2_DECLARE_RTTI()

public:
    ni_node() : m_parent(0), m_visible(true) {}
    virtual ~ni_node();

    // Takes ownership. A node has exactly one parent.
    void add_child(ni_node* child);

    ni_node*       parent()       { return m_parent; }
    const ni_node* parent() const { return m_parent; }

    size_t         child_count() const { return m_children.size(); }
    ni_node*       child(size_t i)       { return m_children[i]; }
    const ni_node* child(size_t i) const { return m_children[i]; }

    // Depth-first search by name, this node included. Null when absent.
    ni_node* find(const std::string& name);

    // Recomputes world transforms for this subtree. `parent_world` is the
    // parent's world transform, or null for a root.
    void update_world(const transform* parent_world = 0);

    const transform& world() const { return m_world; }

    std::string name;
    transform   local;

    bool visible() const      { return m_visible; }
    void set_visible(bool v)  { m_visible = v; }

    // Total nodes in this subtree, including this one.
    size_t subtree_size() const;

private:
    ni_node(const ni_node&);
    ni_node& operator=(const ni_node&);

    ni_node*              m_parent;
    std::vector<ni_node*> m_children;
    transform             m_world;
    bool                  m_visible;
};

// The level root. Only overrides GetRTTI and the destructor in the original
// (2 of 45 slots), so it really is a plain grouping node.
class level_group : public ni_node {
    K2_DECLARE_RTTI()
};

// One room under the level. Same story: 2 of 45.
class room_group : public ni_node {
    K2_DECLARE_RTTI()
public:
    std::string geometry;    // the .K2A this room was built from
};

// An actor's root node. `AnimationNode` in the original, constructed at
// 0x401E3C with the literal name "AnimNode"; the body geometry hangs off it.
class animation_node : public ni_node {
    K2_DECLARE_RTTI()
public:
    animation_node() : body(-1), is_enemy(false) { name = "AnimNode"; }

    int32_t body;        // index into body_table
    bool    is_enemy;
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_NODE_H
