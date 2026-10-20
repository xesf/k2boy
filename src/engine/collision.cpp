// collision.cpp — see collision.h.

#include "engine/collision.h"

#include <cmath>
#include <cfloat>

namespace k2 {
namespace engine {

void collision_world::clear() {
    m_tris.clear();
    m_tri_node.clear();
    m_dynamic.clear();
    m_nodes.clear();
    m_grid.clear();
    m_built = false;
}

int32_t collision_world::add_node() {
    node_state n;
    n.enabled = true;
    n.offset[0] = n.offset[1] = n.offset[2] = 0.0f;
    m_nodes.push_back(n);
    return int32_t(m_nodes.size() - 1);
}

void collision_world::set_node_enabled(int32_t node, bool enabled) {
    if (node >= 0 && size_t(node) < m_nodes.size()) m_nodes[size_t(node)].enabled = enabled;
}

void collision_world::set_node_offset(int32_t node, const float offset[3]) {
    if (node < 0 || size_t(node) >= m_nodes.size()) return;
    for (int k = 0; k < 3; ++k) m_nodes[size_t(node)].offset[k] = offset[k];
}

void collision_world::add_mesh(const float* xyz, size_t stride, size_t vcount,
                               const uint16_t* idx, size_t icount, int32_t node) {
    for (size_t i = 0; i + 2 < icount; i += 3) {
        const uint16_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        if (a >= vcount || b >= vcount || c >= vcount) continue;
        const float* v[3] = { xyz + a * stride, xyz + b * stride, xyz + c * stride };
        for (int k = 0; k < 3; ++k)
            for (int j = 0; j < 3; ++j) m_tris.push_back(v[k][j]);
        m_tri_node.push_back(node);
    }
    m_built = false;
}

void collision_world::cell_range(float mnx, float mnz, float mxx, float mxz,
                                 int32_t& x0, int32_t& z0, int32_t& x1, int32_t& z1) const {
    x0 = int32_t(std::floor((mnx - m_min[0]) / m_cell));
    z0 = int32_t(std::floor((mnz - m_min[1]) / m_cell));
    x1 = int32_t(std::floor((mxx - m_min[0]) / m_cell));
    z1 = int32_t(std::floor((mxz - m_min[1]) / m_cell));
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= m_nx) x1 = m_nx - 1;
    if (z1 >= m_nz) z1 = m_nz - 1;
}

void collision_world::build() {
    m_grid.clear();
    const size_t n = triangle_count();
    if (!n) { m_nx = m_nz = 0; m_built = true; return; }

    float mn[2] = { FLT_MAX, FLT_MAX }, mx[2] = { -FLT_MAX, -FLT_MAX };
    for (size_t i = 0; i < n * 3; ++i) {
        const float x = m_tris[i * 3], z = m_tris[i * 3 + 2];
        if (x < mn[0]) mn[0] = x;
        if (x > mx[0]) mx[0] = x;
        if (z < mn[1]) mn[1] = z;
        if (z > mx[1]) mx[1] = z;
    }
    m_min[0] = mn[0];
    m_min[1] = mn[1];
    m_nx = int32_t((mx[0] - mn[0]) / m_cell) + 1;
    m_nz = int32_t((mx[1] - mn[1]) / m_cell) + 1;
    m_grid.resize(size_t(m_nx) * size_t(m_nz));
    m_dynamic.clear();

    for (size_t t = 0; t < n; ++t) {
        if (m_tri_node[t] >= 0) { m_dynamic.push_back(uint32_t(t)); continue; }
        const float* p = &m_tris[t * 9];
        float tmn[2] = { p[0], p[2] }, tmx[2] = { p[0], p[2] };
        for (int k = 1; k < 3; ++k) {
            const float x = p[k * 3], z = p[k * 3 + 2];
            if (x < tmn[0]) tmn[0] = x;
            if (x > tmx[0]) tmx[0] = x;
            if (z < tmn[1]) tmn[1] = z;
            if (z > tmx[1]) tmx[1] = z;
        }
        int32_t x0, z0, x1, z1;
        cell_range(tmn[0], tmn[1], tmx[0], tmx[1], x0, z0, x1, z1);
        for (int32_t z = z0; z <= z1; ++z)
            for (int32_t x = x0; x <= x1; ++x)
                m_grid[size_t(z) * size_t(m_nx) + size_t(x)].push_back(uint32_t(t));
    }
    m_built = true;
}

// Moller-Trumbore. The winding's normal is (b - a) x (c - a); a triangle is
// front-facing to the ray when that normal points back against `d`.
bool collision_world::ray_triangle(size_t tri, const float o[3], const float d[3],
                                   float max_t, bool front_only, float& t) const {
    const float* p = &m_tris[tri * 9];
    const float e1[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] };
    const float e2[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
    const float h[3]  = { d[1] * e2[2] - d[2] * e2[1],
                          d[2] * e2[0] - d[0] * e2[2],
                          d[0] * e2[1] - d[1] * e2[0] };
    const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    // det = -dot(d, e1 x e2): positive when the face normal opposes the ray.
    if (front_only ? det <= 1e-12f : std::fabs(det) <= 1e-12f) return false;
    const float inv = 1.0f / det;
    const float s[3] = { o[0] - p[0], o[1] - p[1], o[2] - p[2] };
    const float u = inv * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = { s[1] * e1[2] - s[2] * e1[1],
                         s[2] * e1[0] - s[0] * e1[2],
                         s[0] * e1[1] - s[1] * e1[0] };
    const float v = inv * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = inv * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
    return t >= 0.0f && t <= max_t;
}

bool collision_world::pick(const float o[3], const float d[3], float max_t,
                           bool front_only, pick_hit& out) const {
    if (!m_built || m_grid.empty()) return false;

    // The original seeds its running minimum with FLT_MAX and keeps the nearest.
    float best = FLT_MAX;
    int32_t best_tri = -1;

    const float ex = o[0] + d[0] * max_t, ez = o[2] + d[2] * max_t;
    int32_t x0, z0, x1, z1;
    cell_range(std::fmin(o[0], ex), std::fmin(o[2], ez),
               std::fmax(o[0], ex), std::fmax(o[2], ez), x0, z0, x1, z1);

    // A triangle spanning several cells is tested once per cell; that only
    // repeats work, the nearest result is the same.
    for (int32_t z = z0; z <= z1; ++z)
        for (int32_t x = x0; x <= x1; ++x) {
            const std::vector<uint32_t>& c = m_grid[size_t(z) * size_t(m_nx) + size_t(x)];
            for (size_t i = 0; i < c.size(); ++i) {
                float t;
                if (ray_triangle(c[i], o, d, max_t, front_only, t) && t < best) {
                    best = t;
                    best_tri = int32_t(c[i]);
                }
            }
        }
    // Trap geometry: brute force, each triangle shifted by its node's offset,
    // which is the same as casting the ray from origin - offset.
    for (size_t i = 0; i < m_dynamic.size(); ++i) {
        const uint32_t t = m_dynamic[i];
        const node_state& ns = m_nodes[size_t(m_tri_node[t])];
        if (!ns.enabled) continue;
        const float lo[3] = { o[0] - ns.offset[0], o[1] - ns.offset[1], o[2] - ns.offset[2] };
        float tt;
        if (ray_triangle(t, lo, d, max_t, front_only, tt) && tt < best) {
            best = tt;
            best_tri = int32_t(t);
        }
    }
    if (best_tri < 0) return false;

    // Face normal from the triangle's vertices, oriented toward the ray origin.
    const float* p = &m_tris[size_t(best_tri) * 9];
    const float e1[3] = { p[3] - p[0], p[4] - p[1], p[5] - p[2] };
    const float e2[3] = { p[6] - p[0], p[7] - p[1], p[8] - p[2] };
    float n[3] = { e1[1] * e2[2] - e1[2] * e2[1],
                   e1[2] * e2[0] - e1[0] * e2[2],
                   e1[0] * e2[1] - e1[1] * e2[0] };
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 0.0f) { n[0] /= len; n[1] /= len; n[2] /= len; }
    if (n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 0.0f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }

    for (int k = 0; k < 3; ++k) {
        out.point[k]  = o[k] + d[k] * best;
        out.normal[k] = n[k];
    }
    out.distance = best;
    out.triangle = best_tri;
    return true;
}

} // namespace engine
} // namespace k2
