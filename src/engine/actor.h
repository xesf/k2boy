// actor.h — the cast of a level, and the spawn rule.
//
// Ports the data half of the New Game path recovered from pop3dreal.exe. See
// docs/reference/actor-spawn.md for the chain and the addresses.
//
//   .lvl [Bodies]      -> body_table            (g_avatar_bodies / count)
//   .lvl [Prince]      -> spawn_actor(...)      (LevelGroup__SpawnActor, 0x445A1D)
//   .K2A EnemyStart    -> spawn_actor(...)      by body *name*
//
// The original resolves a spawn point to a body by **name**, not by path: the
// trap argv carries a name and the code walks g_avatar_bodies comparing against
// each body's own name ("can't find body %s"). find_by_name mirrors that.
//
// Deliberately free of gfx and platform: this decides *what* to spawn and
// *where*, never how to draw it. Building the meshes is the caller's job, as it
// is in the original, where LevelGroup attaches the avatar to the scene graph.
//
// C++11.

#ifndef K2_ENGINE_ACTOR_H
#define K2_ENGINE_ACTOR_H

#include <stdint.h>
#include <string>
#include <vector>

#include "assets/master.h"

namespace k2 {
namespace engine {

// One [Bodies] entry: "<index> <path-to-master.dat>". 244 bytes in the original.
struct avatar_body {
    int32_t             index;
    std::string         path;      // exactly as written in the .lvl
    std::string         name;      // master.dat -n; the key spawn points match on
    assets::master_file master;
    bool                loaded;    // false when the master.dat could not be read

    avatar_body() : index(-1), loaded(false) {}
};

// Mirrors the g_avatar_bodies array. The original allocates it from the count on
// the [Bodies] line and indexes it directly, so entries are addressed by their
// declared index rather than by insertion order.
class body_table {
public:
    void reset() { m_bodies.clear(); }

    // Grows to fit `index`. Returns false if the index is negative or absurd,
    // matching the original's "illegal body index specified in file %s" check.
    bool set(int32_t index, const std::string& path, const assets::master_file& m,
             bool loaded);

    size_t size() const { return m_bodies.size(); }

    // Null when the slot is out of range or was never filled.
    const avatar_body* at(int32_t index) const;

    // Case-insensitive, as the rest of the data pipeline is. -1 when absent.
    int32_t find_by_name(const std::string& name) const;

    size_t loaded_count() const;

private:
    std::vector<avatar_body> m_bodies;
};

// A spawned actor: which body, where, and what it is called.
struct actor_spawn {
    int32_t     body;
    bool        is_enemy;
    float       pos[3];
    float       yaw;        // radians
    std::string name;

    actor_spawn() : body(-1), is_enemy(false), yaw(0.0f) {
        pos[0] = pos[1] = pos[2] = 0.0f;
    }
};

// Port of LevelGroup__SpawnActor (0x445A1D).
//
// Enforces the same two rules the original does:
//   * only one non-enemy actor may exist  ("making a second prince!")
//   * the body index must name a loaded body ("failed to load our boy's playmate")
//
// Appends to `out` and returns its index, or -1 with `error` set.
int32_t spawn_actor(const body_table& bodies, std::vector<actor_spawn>& out,
                    bool is_enemy, int32_t body_index,
                    float x, float y, float z, float yaw,
                    const std::string& name, std::string* error = 0);

// Index of the single non-enemy actor in `spawns`, or -1. Mirrors g_prince.
int32_t find_prince(const std::vector<actor_spawn>& spawns);

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_ACTOR_H
