// collision.h — the pick primitive the game's movement code is built on.
//
// Every ground, wall and ledge test in the Prince's motion code goes through one
// function, 0x512183:
//
//     bool pick(int mask, vec3* hit, const vec3* origin, const vec3* dir, bool front_only)
//
// It selects a set of scene nodes by `mask` (0x41D8C9), runs a NetImmerse
// NiPick over each, and keeps the nearest intersection (0x511E58 tracks the
// minimum distance in flt_89F600, seeded with FLT_MAX). 0x511F96 then derives
// the hit normal from the triangle's own vertices (0x5120ED).
//
// What is ported here is that contract: nearest hit along a ray, optional
// front-face rejection, face normal. What is not yet ported is the mask: the
// original picks against per-room node categories (LevelGroup +0xF4 / +0xFC /
// +0x104, widened over neighbour rooms by 0x4184C2), and the rule that puts a
// node into a category is not traced. Static room geometry stands in for all
// of them. See docs/reference/collision.md.
//
// No gfx or platform dependency.
//
// C++11.

#ifndef K2_ENGINE_COLLISION_H
#define K2_ENGINE_COLLISION_H

#include <stdint.h>
#include <stddef.h>
#include <vector>

namespace k2 {
namespace engine {

struct pick_hit {
    float point[3];
    float normal[3];        // unit face normal, facing the side the ray came from
    float distance;         // along the ray, in units of |dir|
    int32_t triangle;
};

class collision_world {
public:
    collision_world() : m_cell(2.0f), m_built(false) {}

    void clear();

    // World-space triangles: xyz triples and index triples. `node` is a
    // handle from add_node, or -1 for geometry that never changes.
    void add_mesh(const float* xyz, size_t stride_floats, size_t vertex_count,
                  const uint16_t* indices, size_t index_count, int32_t node = -1);

    // A trap's geometry, which scripts can switch off ("collide:F") or move
    // ("move,..."). Its triangles stay out of the grid and are tested with the
    // node's current offset.
    int32_t add_node();
    void set_node_enabled(int32_t node, bool enabled);
    void set_node_offset(int32_t node, const float offset[3]);

    // Buckets triangles into a uniform XZ grid. Call after the last add_mesh.
    void build();

    // Nearest intersection of origin + t*dir, 0 <= t <= max_t. With
    // `front_only`, triangles whose winding faces away from the ray are skipped,
    // as NiPick does when its front-only flag is set.
    bool pick(const float origin[3], const float dir[3], float max_t,
              bool front_only, pick_hit& out) const;

    size_t triangle_count() const { return m_tris.size() / 9; }

private:
    bool ray_triangle(size_t tri, const float o[3], const float d[3], float max_t,
                      bool front_only, float& t) const;
    void cell_range(float mnx, float mnz, float mxx, float mxz,
                    int32_t& x0, int32_t& z0, int32_t& x1, int32_t& z1) const;

    struct node_state { bool enabled; float offset[3]; };

    std::vector<float>                  m_tris;     // 9 floats per triangle
    std::vector<int32_t>                m_tri_node; // per triangle, -1 for static
    std::vector<uint32_t>               m_dynamic;  // triangles with a node
    std::vector<node_state>             m_nodes;
    float                               m_cell;
    float                               m_min[2];
    int32_t                             m_nx, m_nz;
    std::vector<std::vector<uint32_t> > m_grid;
    bool                                m_built;
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_COLLISION_H
