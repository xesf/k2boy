// node.cpp — see node.h.

#include "engine/node.h"

#include <cmath>
#include <cstring>

namespace k2 {
namespace engine {

K2_DEFINE_RTTI(ni_node, ni_object)
K2_DEFINE_RTTI(level_group, ni_node)
K2_DEFINE_RTTI(room_group, ni_node)
K2_DEFINE_RTTI(animation_node, ni_node)

void transform::set_yaw(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    rotate[0] =  c; rotate[1] = 0.0f; rotate[2] = s;
    rotate[3] = 0.0f; rotate[4] = 1.0f; rotate[5] = 0.0f;
    rotate[6] = -s; rotate[7] = 0.0f; rotate[8] = c;
}

void transform::to_matrix(float* m) const {
    // Row-major 3x3 into a column-major 4x4: m[col*4 + row] = rotate[row*3 + col].
    // Scale is uniform, so it can fold into the basis vectors.
    m[0]  = rotate[0] * scale; m[1]  = rotate[3] * scale; m[2]  = rotate[6] * scale; m[3]  = 0.0f;
    m[4]  = rotate[1] * scale; m[5]  = rotate[4] * scale; m[6]  = rotate[7] * scale; m[7]  = 0.0f;
    m[8]  = rotate[2] * scale; m[9]  = rotate[5] * scale; m[10] = rotate[8] * scale; m[11] = 0.0f;
    m[12] = translate[0];      m[13] = translate[1];      m[14] = translate[2];      m[15] = 1.0f;
}

void transform_compose(const transform& a, const transform& b, transform& out) {
    // Rotation: R = Ra * Rb, both row-major.
    float r[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) s += a.rotate[row * 3 + k] * b.rotate[k * 3 + col];
            r[row * 3 + col] = s;
        }

    // Translation: t = Ra * (scale_a * tb) + ta.
    float t[3];
    for (int row = 0; row < 3; ++row) {
        float s = 0.0f;
        for (int k = 0; k < 3; ++k) s += a.rotate[row * 3 + k] * b.translate[k];
        t[row] = s * a.scale + a.translate[row];
    }

    // Written last so `out` may alias `a` or `b`.
    std::memcpy(out.rotate, r, sizeof(r));
    std::memcpy(out.translate, t, sizeof(t));
    out.scale = a.scale * b.scale;
}

ni_node::~ni_node() {
    for (size_t i = 0; i < m_children.size(); ++i) delete m_children[i];
    m_children.clear();
}

void ni_node::add_child(ni_node* child) {
    if (!child || child->m_parent) return;   // one parent, always
    child->m_parent = this;
    m_children.push_back(child);
}

ni_node* ni_node::find(const std::string& n) {
    if (name == n) return this;
    for (size_t i = 0; i < m_children.size(); ++i)
        if (ni_node* r = m_children[i]->find(n)) return r;
    return 0;
}

void ni_node::update_world(const transform* parent_world) {
    if (parent_world) transform_compose(*parent_world, local, m_world);
    else              m_world = local;

    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->update_world(&m_world);
}

size_t ni_node::subtree_size() const {
    size_t n = 1;
    for (size_t i = 0; i < m_children.size(); ++i) n += m_children[i]->subtree_size();
    return n;
}

} // namespace engine
} // namespace k2
