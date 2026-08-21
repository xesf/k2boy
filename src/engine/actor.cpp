// actor.cpp — see actor.h.

#include "engine/actor.h"

#include <cctype>

namespace k2 {
namespace engine {

namespace {

// The install mixes case freely in paths and names, so every comparison in this
// project folds case. See the VFS notes in docs/plan/04-platform-layer.md.
bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// The original allocates the table from the declared count; a level with a
// wildly out-of-range index is corrupt, not merely sparse.
const int32_t MAX_BODIES = 1024;

} // namespace

bool body_table::set(int32_t index, const std::string& path,
                     const assets::master_file& m, bool loaded) {
    if (index < 0 || index >= MAX_BODIES) return false;
    if (size_t(index) >= m_bodies.size()) m_bodies.resize(size_t(index) + 1);

    avatar_body& b = m_bodies[size_t(index)];
    b.index  = index;
    b.path   = path;
    b.master = m;
    b.loaded = loaded;
    // master.dat's -n is the name spawn points match against. Some files omit
    // it; fall back to the descriptor's own name field so the slot is still
    // addressable rather than silently unmatchable.
    b.name   = !m.name.empty() ? m.name : path;
    return true;
}

const avatar_body* body_table::at(int32_t index) const {
    if (index < 0 || size_t(index) >= m_bodies.size()) return 0;
    const avatar_body& b = m_bodies[size_t(index)];
    return b.index < 0 ? 0 : &b;
}

int32_t body_table::find_by_name(const std::string& name) const {
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        if (m_bodies[i].index < 0) continue;
        if (iequal(m_bodies[i].name, name)) return int32_t(i);
    }
    return -1;
}

size_t body_table::loaded_count() const {
    size_t n = 0;
    for (size_t i = 0; i < m_bodies.size(); ++i)
        if (m_bodies[i].index >= 0 && m_bodies[i].loaded) ++n;
    return n;
}

int32_t find_prince(const std::vector<actor_spawn>& spawns) {
    for (size_t i = 0; i < spawns.size(); ++i)
        if (!spawns[i].is_enemy) return int32_t(i);
    return -1;
}

int32_t spawn_actor(const body_table& bodies, std::vector<actor_spawn>& out,
                    bool is_enemy, int32_t body_index,
                    float x, float y, float z, float yaw,
                    const std::string& name, std::string* error) {
    // "making a second prince!" — the original refuses and returns null.
    if (!is_enemy && find_prince(out) >= 0) {
        if (error) *error = "making a second prince";
        return -1;
    }

    const avatar_body* b = bodies.at(body_index);
    if (!b || !b->loaded) {
        // "failed to load our boy's playmate"
        if (error) *error = "no loaded body at that index";
        return -1;
    }

    actor_spawn a;
    a.body     = body_index;
    a.is_enemy = is_enemy;
    a.pos[0]   = x;
    a.pos[1]   = y;
    a.pos[2]   = z;
    a.yaw      = yaw;
    // PrinceStart falls back to the literal "Prince" when the trap carries no
    // name of its own; otherwise the body's name is the sensible default.
    a.name     = !name.empty() ? name : b->name;

    out.push_back(a);
    return int32_t(out.size()) - 1;
}

} // namespace engine
} // namespace k2
