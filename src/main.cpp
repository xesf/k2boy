// main.cpp — entry point and main loop.

#include "platform/platform.h"
#include "gfx/gfx.h"
#include "assets/k2a.h"
#include "assets/image.h"
#include "assets/lvl.h"
#include "assets/master.h"
#include "assets/act.h"
#include "assets/skl.h"
#include "engine/nirtti.h"
#include "engine/actor.h"
#include "engine/node.h"
#include "engine/config.h"
#include "engine/module_table.h"
#include "engine/module.h"
#include "engine/collision.h"
#include "engine/trap.h"
#include "engine/script.h"
#include "engine/jobs.h"
#include "engine/mixer.h"
#include "engine/fight.h"
#include "assets/sfx.h"
#include "assets/nif.h"
#include "gfx/ttf.h"
#include "media/mov.h"
#include "media/svq1.h"
#include "media/qdm2.h"
#include "media/movie.h"
#include "ui/frontend.h"

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <vector>
#include <map>
#include <array>
#include <string>

using namespace k2;

namespace {

const char* DEFAULT_DATA_ROOT = "data";

void mat4_identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_look_at(float* m, const float* eye, const float* at, const float* up) {
    float f[3] = {at[0]-eye[0], at[1]-eye[1], at[2]-eye[2]};
    float fl = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (fl > 1e-8f) { f[0]/=fl; f[1]/=fl; f[2]/=fl; }
    float s[3] = {f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0]};
    float sl = std::sqrt(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
    if (sl > 1e-8f) { s[0]/=sl; s[1]/=sl; s[2]/=sl; }
    float u[3] = {s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0]};
    m[0]=s[0]; m[4]=s[1]; m[8]=s[2];  m[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[1]=u[0]; m[5]=u[1]; m[9]=u[2];  m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    m[2]=-f[0];m[6]=-f[1];m[10]=-f[2];m[14]=  f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];
    m[3]=0; m[7]=0; m[11]=0; m[15]=1;
}

void mat4_perspective(float* m, float fov_y_rad, float aspect, float znear, float zfar) {
    std::memset(m, 0, 16 * sizeof(float));
    const float f = 1.0f / std::tan(fov_y_rad * 0.5f);
    m[0]  = f / (aspect > 0.0f ? aspect : 1.0f);
    m[5]  = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

// Probe a handful of formats to prove the VFS resolves the awkward cases:
// backslash paths, doubled backslashes, and mismatched extension case.
void verify_data() {
    static const char* probes[] = {
        "pop3dreal.exe",
        "Traps.txt",
        "GameText.txt",
        "geometry\\rooms\\prisonfix\\prison1.k2a",   // backslash + case
        "geometry\\\\prince\\\\master.dat",          // doubled, as in .lvl [Bodies]
        "Actors/Prince/Prince.act",
        "Audio/sfx/A1deathv001.sfx",
        "Geometry/Rooms/Cistern/cistern.lvl",
    };

    std::printf("VFS mounted (%zu files indexed so far)\n", plat::fs_file_count());
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        char real[4096];
        const bool ok = plat::fs_resolve(probes[i], real, sizeof(real));
        std::printf("  %-42s %s\n", probes[i], ok ? "ok" : "MISSING");
    }
}

// Self-test for the recovered NiRTTI hierarchy: checks the table is internally
// consistent, then checks the spine that the reimplementation depends on.
// See docs/reference/nirtti-hierarchy.md.
void verify_rtti() {
    size_t n = 0;
    const k2::engine::rtti_record* t = k2::engine::rtti_table(n);

    // Every parent index must point backwards and stay in range, so a chain
    // walk always terminates.
    size_t roots = 0, bad = 0;
    for (size_t i = 0; i < n; ++i) {
        if (t[i].parent < 0) { ++roots; continue; }
        if (t[i].parent >= int(i)) ++bad;
    }

    // The chain the engine is being built around.
    static const char* spine[] = {
        "PrinceObject", "CharacterStateTable", "CharacterObject",
        "NiCollisionSwitch", "NiNode", "NiAVObject", "NiObject"
    };
    bool spine_ok = true;
    for (size_t i = 0; i + 1 < sizeof(spine) / sizeof(spine[0]); ++i)
        if (!k2::engine::rtti_is_kind_of(spine[i], spine[i + 1])) spine_ok = false;

    // Traps are the largest family and the one the level loader dispatches on.
    size_t traps = 0;
    for (size_t i = 0; i < n; ++i)
        if (k2::engine::rtti_is_kind_of(t[i].name, "Trap") &&
            std::strcmp(t[i].name, "Trap") != 0) ++traps;

    std::printf("NiRTTI %zu classes, %zu roots, %zu bad parent links\n", n, roots, bad);
    std::printf("  Prince -> NiObject chain   %s\n", spine_ok ? "ok" : "BROKEN");
    std::printf("  Trap subclasses            %zu (expected 39)\n", traps);
    // A negative control: these must not be related, or is_kind_of is degenerate.
    std::printf("  NiCamera is not a Trap     %s\n",
                k2::engine::rtti_is_kind_of("NiCamera", "Trap") ? "WRONG" : "ok");
}

// Self-test for the scene graph. Transform composition cannot be eyeballed, and
// a wrong basis would silently misplace every actor, so check it against
// hand-computed answers. See docs/reference/scene-graph.md.
void verify_nodes() {
    using namespace k2::engine;
    int pass = 0, fail = 0;
    #define CHECK(what, cond) do { if (cond) ++pass; else { ++fail; \
        std::printf("  node self-test FAILED: %s\n", what); } } while (0)

    // A 90-degree yaw sends +X to -Z. set_yaw uses the same convention as the
    // camera basis, so this pins the sign down rather than assuming it.
    transform r;
    r.set_yaw(1.5707963f);
    float m[16];
    r.to_matrix(m);
    CHECK("yaw90 maps +X to -Z", std::fabs(m[0]) < 1e-4f && std::fabs(m[2] + 1.0f) < 1e-4f);

    // Composition: parent translated, child offset along +X, parent yawed 90.
    // The child should end up offset along -Z from the parent.
    transform parent, childl, out;
    parent.set_yaw(1.5707963f);
    parent.translate[0] = 10.0f; parent.translate[1] = 0.0f; parent.translate[2] = 5.0f;
    childl.translate[0] = 2.0f;
    transform_compose(parent, childl, out);
    CHECK("compose rotates child offset",
          std::fabs(out.translate[0] - 10.0f) < 1e-3f &&
          std::fabs(out.translate[2] - 3.0f)  < 1e-3f);

    // Identity parent must leave the child untouched.
    transform id, c2, out2;
    c2.translate[0] = 1.0f; c2.translate[1] = 2.0f; c2.translate[2] = 3.0f;
    transform_compose(id, c2, out2);
    CHECK("identity parent is a no-op",
          std::fabs(out2.translate[0]-1.0f) < 1e-6f &&
          std::fabs(out2.translate[1]-2.0f) < 1e-6f &&
          std::fabs(out2.translate[2]-3.0f) < 1e-6f);

    // Scale multiplies down the chain and scales the child's offset.
    transform sp, sc2, so;
    sp.scale = 2.0f; sc2.scale = 3.0f; sc2.translate[0] = 1.0f;
    transform_compose(sp, sc2, so);
    CHECK("scale composes", std::fabs(so.scale - 6.0f) < 1e-6f &&
                            std::fabs(so.translate[0] - 2.0f) < 1e-6f);

    // Inverse must undo compose: T * inv(T) == identity.
    transform ti, tinv, tid;
    ti.set_yaw(0.7f);
    ti.translate[0] = 3.0f; ti.translate[1] = -1.0f; ti.translate[2] = 2.0f;
    ti.scale = 2.0f;
    transform_inverse(ti, tinv);
    transform_compose(ti, tinv, tid);
    CHECK("inverse undoes compose",
          std::fabs(tid.translate[0]) < 1e-4f && std::fabs(tid.translate[1]) < 1e-4f &&
          std::fabs(tid.translate[2]) < 1e-4f && std::fabs(tid.scale - 1.0f) < 1e-4f &&
          std::fabs(tid.rotate[0] - 1.0f) < 1e-4f && std::fabs(tid.rotate[4] - 1.0f) < 1e-4f);

    // Graph: parent -> child, world of child must equal the composition.
    level_group* root = new level_group();
    root->name = "root";
    root->local.translate[1] = 100.0f;
    ni_node* kid = new ni_node();
    kid->name = "kid";
    kid->local.translate[1] = 5.0f;
    root->add_child(kid);
    root->update_world();
    CHECK("child world inherits parent", std::fabs(kid->world().translate[1] - 105.0f) < 1e-4f);
    CHECK("find by name", root->find("kid") == kid);
    CHECK("subtree size", root->subtree_size() == 2);
    CHECK("rtti", kid->is_kind_of(ni_node::rtti()) &&
                  root->is_kind_of(ni_node::rtti()) &&
                  !kid->is_kind_of(level_group::rtti()));
    delete root;   // must also delete the child

    #undef CHECK
    std::printf("scene graph self-test: %d passed, %d failed\n", pass, fail);
}

// Headless .K2A dump: parses a file and reports what came out. The real check
// on the parser, since it can run without a window.
int dump_k2a(const char* path) {
    size_t size = 0;
    uint8_t* data = plat::fs_read(path, size);
    if (!data) {
        std::fprintf(stderr, "k2boy: cannot read '%s'\n", path);
        return 1;
    }

    assets::k2a_file f;
    std::string err;
    const bool ok = assets::k2a_parse(data, size, f, &err);
    plat::fs_free(data);
    if (!ok) {
        std::fprintf(stderr, "k2boy: parse failed: %s\n", err.c_str());
        return 1;
    }

    std::printf("%s\n  version %d, %zu materials, %zu nodes, %d traps\n",
                path, f.version, f.materials.size(), f.nodes.size(), f.trap_count());

    size_t total_v = 0, total_i = 0, meshes = 0;
    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_mesh) continue;
        std::vector<assets::k2a_vertex> vb;
        std::vector<uint16_t> ib;
        assets::k2a_build_mesh(n, vb, ib);
        total_v += vb.size();
        total_i += ib.size();
        ++meshes;
        if (meshes <= 6) {
            std::printf("  mesh %-24s src %5d verts / %5d faces  ->  %5zu verts / %5zu idx  mat %d\n",
                        n.name.c_str(), n.mesh.vert_count(), n.mesh.face_count(),
                        vb.size(), ib.size(), n.material_ref);
        }
    }
    if (meshes > 6) std::printf("  ... %zu meshes total\n", meshes);
    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_mesh || n.material_ref >= 0) continue;
        std::vector<assets::k2a_vertex> vb; std::vector<uint16_t> ib;
        assets::k2a_build_mesh(n, vb, ib);
        float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
        for (size_t v = 0; v < vb.size(); ++v) {
            const float p2[3] = {vb[v].x, vb[v].y, vb[v].z};
            for (int a = 0; a < 3; ++a) { if (p2[a]<mn[a]) mn[a]=p2[a]; if (p2[a]>mx[a]) mx[a]=p2[a]; }
        }
        std::printf("  NO-MATERIAL %-24s %s  extent %.0f x %.0f x %.0f\n",
                    n.name.c_str(), n.has_trap ? n.trap.name.c_str() : "(not a trap)",
                    mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]);
    }
    std::printf("  built %zu vertices, %zu indices\n", total_v, total_i);

    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_trap) continue;
        std::printf("  trap %-16s id=%-4d argc=%zu  center=(%.1f, %.1f, %.1f)\n",
                    n.trap.name.c_str(), n.trap.id, n.trap.argv.size(),
                    n.trap.center[0], n.trap.center[1], n.trap.center[2]);
    }

    for (size_t i = 0; i < f.materials.size() && i < 4; ++i) {
        std::printf("  mat %zu  %-28s %s\n", i, f.materials[i].name.c_str(),
                    f.materials[i].bitmap.c_str());
    }
    return 0;
}


// Headless validation of the spawn path across the whole install: parse every
// .lvl, build its body table, scan its rooms for PrinceStart / EnemyStart, and
// check each one resolves to a loaded body by name. Needs no GL, so it can run
// over all levels in one go. See docs/reference/actor-spawn.md.
int check_spawns() {
    std::vector<std::string> levels;
    plat::fs_list_paths_by_extension(".lvl", levels);
    std::sort(levels.begin(), levels.end());

    size_t tot_princes = 0, tot_enemies = 0, tot_unresolved = 0, tot_multi = 0;
    size_t levels_ok = 0;

    for (size_t li = 0; li < levels.size(); ++li) {
        size_t size = 0;
        uint8_t* data = plat::fs_read(levels[li].c_str(), size);
        if (!data) continue;
        assets::lvl_file lv;
        std::string err;
        const bool ok = assets::lvl_parse(data, size, lv, &err);
        plat::fs_free(data);
        if (!ok) { std::printf("%-40s PARSE FAILED\n", levels[li].c_str()); continue; }

        k2::engine::body_table bodies;
        bodies.reset();
        for (size_t i = 0; i < lv.bodies.size(); ++i) {
            const assets::lvl_body& b = lv.bodies[i];
            size_t bs = 0;
            uint8_t* bd = plat::fs_read(b.path.c_str(), bs);
            if (!bd) bd = plat::fs_read_basename(b.path.c_str(), bs);
            assets::master_file m;
            bool mok = false;
            if (bd) { std::string e2; mok = assets::master_parse(bd, bs, m, &e2); plat::fs_free(bd); }
            bodies.set(b.index, b.path, m, mok);
        }

        std::vector<k2::engine::actor_spawn> spawns;
        size_t princes = 0, enemies = 0, unresolved = 0, refused = 0;

        for (size_t r = 0; r < lv.regions.size(); ++r) {
            for (size_t i = 0; i < lv.regions[r].rooms.size(); ++i) {
                const std::string file = assets::lvl_room_filename(lv.regions[r].rooms[i]);
                size_t rs = 0;
                uint8_t* rd = plat::fs_read_basename(file.c_str(), rs);
                if (!rd) continue;
                assets::k2a_file f;
                std::string e3;
                const bool rok = assets::k2a_parse(rd, rs, f, &e3);
                plat::fs_free(rd);
                if (!rok) continue;

                for (size_t n = 0; n < f.nodes.size(); ++n) {
                    if (!f.nodes[n].has_trap) continue;
                    const assets::k2a_trap& t = f.nodes[n].trap;
                    const bool is_enemy = (t.name == "EnemyStart");
                    if (!is_enemy && t.name != "PrinceStart") continue;

                    const std::string want = t.argv.empty() ? std::string() : t.argv[0];
                    const int32_t body = want.empty() ? -1 : bodies.find_by_name(want);
                    if (body < 0) {
                        ++unresolved;
                        std::printf("    unresolved %-12s body '%s' in %s\n",
                                    t.name.c_str(), want.c_str(), file.c_str());
                        continue;
                    }

                    std::string e4;
                    if (k2::engine::spawn_actor(bodies, spawns, is_enemy, body,
                                                t.center[0], t.center[1], t.center[2],
                                                t.pivot_angle, want, &e4) < 0) {
                        ++refused;
                        std::printf("    refused %-12s body '%s' in %s: %s\n",
                                    t.name.c_str(), want.c_str(), file.c_str(), e4.c_str());
                        continue;
                    }
                    if (is_enemy) ++enemies; else ++princes;
                }
            }
        }

        tot_princes += princes; tot_enemies += enemies;
        tot_unresolved += unresolved; tot_multi += refused;
        if (!unresolved && princes <= 1) ++levels_ok;

        std::printf("%-42s bodies %2zu  prince %zu  enemies %2zu  unresolved %zu  refused %zu\n",
                    levels[li].c_str(), bodies.loaded_count(),
                    princes, enemies, unresolved, refused);
    }

    std::printf("\nlevels %zu, clean %zu\n", levels.size(), levels_ok);
    std::printf("princes %zu, enemies %zu, unresolved names %zu, refused spawns %zu\n",
                tot_princes, tot_enemies, tot_unresolved, tot_multi);
    return 0;
}

// Writes a 24-bit bottom-up BMP. Only used by --shot, so that a rendered frame
// can be inspected without a person at the screen.
bool write_bmp(const char* path, int w, int h, const uint8_t* rgba) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const int row = ((w * 3 + 3) / 4) * 4;
    const int img = row * h;
    const int off = 54;
    uint8_t hdr[54];
    std::memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    const int fsz = off + img;
    std::memcpy(hdr + 2, &fsz, 4);
    std::memcpy(hdr + 10, &off, 4);
    const int hsz = 40; std::memcpy(hdr + 14, &hsz, 4);
    std::memcpy(hdr + 18, &w, 4);
    std::memcpy(hdr + 22, &h, 4);
    const uint16_t planes = 1, bpp = 24;
    std::memcpy(hdr + 26, &planes, 2);
    std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &img, 4);
    std::fwrite(hdr, 1, sizeof(hdr), f);

    std::vector<uint8_t> line(size_t(row), 0);
    for (int y = h - 1; y >= 0; --y) {          // BMP is bottom-up
        const uint8_t* src = rgba + size_t(y) * size_t(w) * 4;
        for (int x = 0; x < w; ++x) {
            line[size_t(x * 3 + 0)] = src[x * 4 + 2];   // B
            line[size_t(x * 3 + 1)] = src[x * 4 + 1];   // G
            line[size_t(x * 3 + 2)] = src[x * 4 + 0];   // R
        }
        std::fwrite(&line[0], 1, size_t(row), f);
    }
    std::fclose(f);
    return true;
}


// Dumps the bytes surrounding each .act node record, to look for the parent
// link. Nodes are `u32 len, name, 28-byte transform`; whatever encodes the tree
// has to live near them. See docs/formats/act.md.
int dump_act_nodes(const char* path) {
    size_t size = 0;
    uint8_t* d = plat::fs_read_basename(path, size);
    if (!d) d = plat::fs_read(path, size);
    if (!d) { std::fprintf(stderr, "k2boy: cannot read %s\n", path); return 1; }

    assets::act_file a;
    std::string err;
    if (!assets::act_parse(d, size, a, &err)) { plat::fs_free(d); return 1; }

    std::printf("%s: %zu nodes, file %zu bytes\n", path, a.nodes.size(), size);
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        const assets::act_node& n = a.nodes[i];
        const size_t rec_end = n.offset + 4 + n.name.size() + 28;
        const size_t gap = (i + 1 < a.nodes.size())
                         ? a.nodes[i + 1].offset - rec_end : 0;
        std::printf("  [%2zu] %-14s off %6zu  rec_end %6zu  gap_to_next %4zu  ",
                    i, n.name.c_str(), n.offset, rec_end, gap);
        // First 16 bytes after the transform, as both hex and u32.
        const size_t show = (rec_end + 16 <= size) ? 16 : 0;
        for (size_t k = 0; k < show; ++k) std::printf("%02x ", d[rec_end + k]);
        if (show >= 8) {
            uint32_t a0, a1;
            std::memcpy(&a0, d + rec_end, 4);
            std::memcpy(&a1, d + rec_end + 4, 4);
            std::printf(" u32[%u, %u]", a0, a1);
        }
        std::printf("\n");
    }
    plat::fs_free(d);
    return 0;
}
// Validates the recovered skeleton against the geometry: runs forward
// kinematics over the .act tree and compares each joint's world bind pose with
// the matching .K2A node's own transform.
//
// The two files use different spaces. .act transforms are local-to-parent in
// engine space (Y-up); a .K2A node transform is the world bind pose in Max
// space (Z-up), mapping engine = (x, z, -y). Nodes are depth-first, so a parent
// is always resolved before its children.
int check_bind_pose(const char* master_path) {
    assets::master_file m;
    {
        size_t size = 0;
        uint8_t* d = plat::fs_read(master_path, size);
        if (!d) d = plat::fs_read_basename(master_path, size);
        if (!d) { std::fprintf(stderr, "k2boy: cannot read %s\n", master_path); return 1; }
        std::string err;
        const bool ok = assets::master_parse(d, size, m, &err);
        plat::fs_free(d);
        if (!ok) return 1;
    }
    assets::k2a_file g;
    {
        size_t size = 0;
        uint8_t* d = plat::fs_read_basename(m.geometry.c_str(), size);
        if (!d) { std::fprintf(stderr, "k2boy: cannot read %s\n", m.geometry.c_str()); return 1; }
        std::string err;
        const bool ok = assets::k2a_parse(d, size, g, &err);
        plat::fs_free(d);
        if (!ok) return 1;
    }
    assets::act_file a;
    {
        std::string act_path = m.proto ? m.actor : (m.actor + ".act");
        size_t size = 0;
        uint8_t* d = plat::fs_read_basename(act_path.c_str(), size);
        if (!d) { std::fprintf(stderr, "k2boy: cannot read %s\n", act_path.c_str()); return 1; }
        std::string err;
        const bool ok = assets::act_parse(d, size, a, &err);
        plat::fs_free(d);
        if (!ok) return 1;
    }

    std::printf("%s: %zu geometry nodes, %zu act nodes, tree %s\n",
                m.name.c_str(), g.nodes.size(), a.nodes.size(),
                a.tree_ok ? "ok" : "NOT RECOVERED");
    if (!a.tree_ok) return 1;

    const size_t n = a.nodes.size();
    std::vector<k2::engine::transform> world(n);
    for (size_t i = 0; i < n; ++i) {
        const assets::act_node& nd = a.nodes[i];
        k2::engine::transform local;
        const float w = nd.quat[0], x = nd.quat[1], y = nd.quat[2], z = nd.quat[3];
        local.rotate[0]=1-2*(y*y+z*z); local.rotate[1]=  2*(x*y-w*z); local.rotate[2]=  2*(x*z+w*y);
        local.rotate[3]=  2*(x*y+w*z); local.rotate[4]=1-2*(x*x+z*z); local.rotate[5]=  2*(y*z-w*x);
        local.rotate[6]=  2*(x*z-w*y); local.rotate[7]=  2*(y*z+w*x); local.rotate[8]=1-2*(x*x+y*y);
        for (int k = 0; k < 3; ++k) local.translate[k] = nd.pos[k];
        local.scale = 1.0f;
        if (nd.parent >= 0) k2::engine::transform_compose(world[size_t(nd.parent)], local, world[i]);
        else                world[i] = local;
    }

    size_t matched = 0, orphan = 0;
    double sum = 0.0; float worst = 0.0f; std::string worst_name;
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const assets::k2a_node& nd = g.nodes[i];
        if (!nd.has_mesh || nd.has_trap) continue;
        size_t j = 0;
        for (; j < n; ++j) if (a.nodes[j].name == nd.name) break;
        if (j == n) { ++orphan; continue; }
        ++matched;
        const float wp[3] = { nd.pos[0], nd.pos[2], -nd.pos[1] };
        const float dx = world[j].translate[0] - wp[0];
        const float dy = world[j].translate[1] - wp[1];
        const float dz = world[j].translate[2] - wp[2];
        const float d  = std::sqrt(dx*dx + dy*dy + dz*dz);
        sum += d;
        if (d > worst) { worst = d; worst_name = nd.name; }
        if (matched <= 6)
            std::printf("  %-14s FK (%7.3f %7.3f %7.3f)  K2A (%7.3f %7.3f %7.3f)  err %.4f\n",
                        nd.name.c_str(), world[j].translate[0], world[j].translate[1],
                        world[j].translate[2], wp[0], wp[1], wp[2], d);
    }

    std::printf("  matched %zu, orphan %zu\n", matched, orphan);
    std::printf("  FK vs .K2A bind pose: mean %.4f, worst %.4f (%s)\n",
                matched ? sum / double(matched) : 0.0, worst, worst_name.c_str());
    std::printf("  verdict: %s\n",
                worst < 0.02f ? "hierarchy reproduces the bind pose"
                              : "MISMATCH - hierarchy or convention still wrong");
    return 0;
}

// Reads the startup flag file the way WinMain does, and reports what it found.
// See docs/reference/boot-sequence.md.
int dump_config(const char* path) {
    size_t size = 0;
    uint8_t* d = plat::fs_read(path, size);
    if (!d) d = plat::fs_read_basename(path, size);
    if (!d) { std::fprintf(stderr, "k2boy: cannot read '%s'\n", path); return 1; }

    k2::engine::config cfg;
    k2::engine::config_parse(reinterpret_cast<const char*>(d), size, cfg);
    plat::fs_free(d);

    std::printf("%s: %zu active flags\n", path, cfg.flags.size());
    for (size_t i = 0; i < cfg.flags.size(); ++i)
        std::printf("  -%c  %s\n", cfg.flags[i].key,
                    cfg.flags[i].tail.empty() ? "(no tail)" : cfg.flags[i].tail.c_str());

    const std::string startup = cfg.startup_file();
    const std::string console = cfg.console_file();
    std::printf("  startup file (-i): %s\n", startup.empty() ? "(none)" : startup.c_str());
    std::printf("  console file (-c): %s\n", console.empty() ? "(none)" : console.c_str());

    // Follow the -i file, which is what the original would load next.
    if (!startup.empty()) {
        size_t s2 = 0;
        uint8_t* d2 = plat::fs_read_basename(startup.c_str(), s2);
        if (d2) {
            std::string body(reinterpret_cast<const char*>(d2), s2);
            while (!body.empty() && (body[body.size()-1] == '\n' || body[body.size()-1] == '\r'))
                body.erase(body.size()-1);
            std::printf("  %s contents: '%s'\n", startup.c_str(), body.c_str());
            plat::fs_free(d2);
        } else {
            std::printf("  %s: NOT FOUND\n", startup.c_str());
        }
    }
    return 0;
}

// Prints the recovered module registry and checks the ordering that defines
// the ids. See docs/plan/05-boot-to-gameplay.md.
int dump_modules() {
    const char* why = 0;
    const bool ok = k2::engine::module_table_verify(&why);

    size_t n = 0;
    const k2::engine::module_entry* t = k2::engine::module_table(n);
    std::printf("%zu modules; id = position in the strcmpi-sorted name list\n", n);
    for (size_t i = 0; i < n; ++i) {
        const char* kind = t[i].kind == k2::engine::MODULE_KIND_GAME  ? "game"
                         : t[i].kind == k2::engine::MODULE_KIND_MENU  ? "menu"
                         : t[i].kind == k2::engine::MODULE_KIND_NIS   ? "NIS"
                                                                      : "title";
        std::printf("  %2zu (0x%02zX) %-5s %-12s %s%s\n", i, i, kind, t[i].name,
                    t[i].level,
                    int32_t(i) == k2::engine::MODULE_ID_STARTUP ? "   <== WinMain posts 0x0C" : "");
    }
    std::printf("table ordering: %s\n", ok ? "ok" : why);
    return ok ? 0 : 1;
}

} // namespace

// Free-fly camera. Yaw/pitch in radians; pitch clamped just short of vertical
// so the view basis never degenerates against world up.
struct camera {
    float pos[3];
    float yaw;
    float pitch;
    float speed;        // units per second, scaled to the scene on reset
};

void camera_basis(const camera& c, float* fwd, float* right) {
    const float cp = std::cos(c.pitch), sp = std::sin(c.pitch);
    const float cy = std::cos(c.yaw),   sy = std::sin(c.yaw);
    fwd[0] = cp * sy;  fwd[1] = sp;  fwd[2] = -cp * cy;
    // Right is forward x worldUp, which stays well-defined given the clamp.
    right[0] = cy;     right[1] = 0.0f; right[2] = sy;
}

// A loaded room: one draw per .K2A node, plus scene bounds for framing.
struct draw_item {
    gfx::mesh_id    mesh;
    gfx::texture_id tex;
    bool            transparent;
    bool            is_trap;        // a trigger volume, drawn as coloured wireframe
    int             trap_type;      // index into scene::trap_types; -1 when not a volume
    int             owner;          // index into scene::actors; -1 for static room geometry
    int             joint;          // index into actor_inst::joint_*; -1 if unbound
    float           center[3];      // world centroid, for back-to-front sorting
    float           radius;         // bounding sphere about `center`, for frustum culling
    float           tint[3];        // filled by assign_trap_colors once the scene is loaded
    bool            hidden;         // a trap made invisible (argv[0] or "visible:F")
    float           offset[3];      // a trap moved by script ("move,...")
    bool            has_model;      // a trap posed by a skill: draw under `model`
    float           model[16];
    int             material;       // index into scene::materials, -1 for none
    bool            cutout;         // colour-keyed: alpha-tested, still opaque
    int32_t         coll_node = -1; // a solid prop's part: collision that follows `owner`'s pose
};

// While set, actor geometry being loaded also goes into the collision world,
// one node per part, switched off until its actor says otherwise.
bool g_part_collision = false;

// K2_SCRIPTAUDIT: every trap's script arguments, run once at the start so the
// exit summary lists what nothing handles. A diagnostic only.
std::vector<std::string> g_audit_scripts;

// A material's texture: one frame, or an IFL sequence with per-frame holds.
struct mat_tex {
    std::vector<gfx::texture_id> frames;
    std::vector<int>             holds;      // frames each image is shown for
    bool                         has_alpha;  // some texel is not fully opaque
    mat_tex() : has_alpha(false) {}
};

// A spawned actor's drawable form: where it is, and which items are its body.
// The original attaches an AnimationNode to the LevelGroup; this is the same
// relationship expressed as a draw range plus a transform.
struct actor_inst {
    std::string             name;
    k2::engine::animation_node* node;   // owned by scene::root
    bool                    is_enemy;
    size_t                  first_item;
    size_t                  item_count;

    // The skeleton, from the .act. `bind_inv[j]` takes geometry that is baked
    // in bind pose back into joint j's local space; `cur[j]` is where that joint
    // is now. A part is drawn at  actor_world * cur[j] * bind_inv[j], which is
    // the identity at bind pose - so binding must not change the image until
    // something actually poses a joint.
    std::vector<std::string>          joint_names;
    std::vector<int32_t>              joint_parent;
    std::vector<k2::engine::transform> joint_local_bind;
    std::vector<k2::engine::transform> joint_local_cur;   // pose; starts at bind
    std::vector<k2::engine::transform> bind_inv;
    std::vector<k2::engine::transform> cur;
    bool                              skinned;

    // Locomotion clips, indexed by loco_clip. `clip` is the one playing, -1 for
    // none; `clip_ms` is its play head.
    std::vector<assets::skl_file>     loco;
    std::vector<bool>                 loco_ok;
    int                               clip;
    double                            clip_ms;
    bool                              clip_once;      // hold the last frame instead of looping

    // Scripts address an actor by the node that spawned it ("prince" for the
    // Prince); "SkillAt:" plays one of its skills once, at a placeholder.
    std::string                       script_name;
    std::string                       skill_dir;      // VFS dir of its .act
    const assets::skl_file*           scripted;       // playing, or 0
    double                            scripted_start;
    float                             scripted_origin[3];
    float                             scripted_yaw;
    bool                              remove_after;   // "...,REMOVE"
    bool                              hidden;
    // Combat: a looping stance clip that replaces the locomotion set while
    // set (CombatAtReady_G*), and whether a one-shot keeps its last frame.
    const assets::skl_file*           stance;
    bool                              hold_last;
    bool                              posed_hold;     // holding that frame now
    int                               health, max_health;
    bool                              armed;          // the Prince has his sword
    bool                              has_bow;        // "weapon:+bow"
    int                               arrows;         // "weapon:+arrow,type,count"
    int                               pending_hurt;   // "hurt:N", applied by combat
    std::string                       pending_kill;   // "kill:<clip>": dies playing it
    std::string                       held_clip;      // a prop's last scripted clip, for saves
    bool                              archer;         // a bowman: shoots, never closes in
    // The other hand (master.dat's second grip), where the Prince's bow is
    // held while `bow_shown`.
    int                               alt_hand_joint;
    float                             alt_hand_off[3];
    bool                              bow_shown;
    // A Fixture that is solid (argv0's Collideable, "Collide:T/F"): carts,
    // crates, levers - not the creatures, which idle. Its parts' collision
    // follows the pose (ours; the original's prop collision is not traced).
    bool                              collides = false;
    // "WalkAt:walk|run,<placeholder>[,REMOVE]": on his way there, under the
    // script's control; REMOVE hides him on arrival.
    bool                              walking;
    bool                              walk_run, walk_remove;
    float                             walk_to[3];
    // A Fixture trap's actor (prisoners, rats, levers, cranes): it idles
    // through `ambient`, a random pick each time, and plays any skill a
    // script names. `home` is where it stands between them.
    bool                              is_fixture;
    bool                              start_flag;     // "Start" received (0x1000 at +0xD8)
    // The weapon in his hand: master.dat [Weapons] names it and the hand
    // node with an offset into it. Drawn while `weapon_shown`.
    std::string                       weapon;
    int                               hand_joint;
    float                             hand_off[3];
    bool                              weapon_shown;
    // World objects in his hand ("Item:name,on,R", 0x46DC51): items drawn
    // at the hand, about their own centre, while carried.
    std::vector<size_t>               carried;
    int                               carry_joint;
    std::vector<std::string>          ambient;
    float                             home[3];
    float                             home_yaw;
    // The whole body tipped forward about the pelvis (radians, + face
    // down): swimming lays him flat; the swim clips are authored upright.
    float                             body_pitch;
    // Each body part's model matrix for this frame (16 floats a part),
    // computed with the poses on the worker cores; empty when not skinned.
    std::vector<float>                part_mats;

    actor_inst() : node(0), is_enemy(false), first_item(0), item_count(0),
                   skinned(false), clip(-1), clip_ms(0.0), clip_once(false), scripted(0), scripted_start(0),
                   scripted_yaw(0), remove_after(false), hidden(false), stance(0),
                   hold_last(false), posed_hold(false), health(100), max_health(100), armed(false),
                   has_bow(false), arrows(0), pending_hurt(0),
                   walking(false), walk_run(false), walk_remove(false), archer(false),
                   alt_hand_joint(-1), bow_shown(false),
                   is_fixture(false), start_flag(false), hand_joint(-1), weapon_shown(false), carry_joint(-1), home_yaw(0), body_pitch(0) {
        hand_off[0] = hand_off[1] = hand_off[2] = 0.0f;
        scripted_origin[0] = scripted_origin[1] = scripted_origin[2] = 0.0f;
        home[0] = home[1] = home[2] = 0.0f;
    }
};

bool equals_nocase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && !b[i];
}

// The player: neither a guard nor a Fixture's actor.
inline bool is_prince(const actor_inst& a) { return !a.is_enemy && !a.is_fixture; }

// The original's locomotion set, in the order of its name table at 0x749C30:
// { "BasePose_L", "Running_L", "Walk_L", "AmbientType_G" }. AmbientType_G is a
// stem that gets a random 1..6 suffix (see 0x4BDDA4), so it is not loaded here.
//
// The crate set follows: the skills StStartCrateMove (0x4D648C) chooses from,
// in its own order of names (off_74AB28 .. off_74AB3C), plus the grab.
//
// The jump set follows, named as StContinueJump (0x4B6A0F) and
// StStartActualJump (0x4B6C40) name them.
enum loco_clip { LOCO_BASE = 0, LOCO_RUN, LOCO_WALK,
                 CRATE_FINISH, CRATE_BASE, CRATE_PUSH, CRATE_PUSH_FAIL,
                 CRATE_PULL, CRATE_PULL_FAIL, CRATE_ANTIC,
                 JUMP_UP_ANTIC, JUMP_UP, JUMP_FWD_ANTIC, JUMP_FWD, JUMP_BACK_ANTIC, JUMP_BACK,
                 JUMP_LEFT_ANTIC, JUMP_LEFT, JUMP_RIGHT_ANTIC, JUMP_RIGHT,
                 LAND_VERT, LAND_FWD, LAND_BACK, LAND_LEFT, LAND_RIGHT, LAND_PAINFUL,
                 RUN_JUMP_ANTIC, RUN_JUMPING, RUN_JUMP_LAND, FALL_VERT,
                 HOP_SMALL, HOP_1M, HOP_1_5M, HOP_2M,
                 LEDGE_GRAB_VERT, LEDGE_GRAB_HORIZ, LEDGE_HANG, LEDGE_PULL_UP, LEDGE_PULL_FAIL,
                 LEDGE_MOVE_LEFT, LEDGE_MOVE_RIGHT, WALK_BACK,
                 LOCO_COUNT };
const char* const k_loco_names[LOCO_COUNT] = { "BasePose_L", "Running_L", "Walk_L",
    "PushOrPullFinish_M", "PushOrPullBase_M", "Push_M", "PushFail_M",
    "Pull_M", "PullFail_M", "PushOrPullAntic_M",
    "JumpUpAntic_G", "JumpingUp_G", "JumpForwardAntic_G", "JumpingForward_G",
    "JumpBackAntic_G", "JumpingBack_G", "JumpLeftAntic_G", "JumpingLeft_G",
    "JumpRightAntic_G", "JumpingRight_G",
    "LandVertSafe_G", "LandJumpForward_G", "LandBackSafe_G", "LandLeftSafe_G",
    "LandRightSafe_G", "LandVertPainful_G",
    "RunJumpAnticLeft_L", "RunJumpingLeft_G", "RunJumpLand_L", "FallVertUncontrolled_G",
    "HopUpOntoSmall_M", "HopUpOnto1m_M", "HopUpOnto1point5m_M", "HopUpOnto2m_M",
    "LedgeGrabVert_M", "LedgeGrabHoriz_M", "LedgeHangBase_M", "LedgePullUpOnto_M1",
    "LedgeHangPullUpFail_M", "LedgeHangMovingLeft_M", "LedgeHangMovingRight_M",
    "WalkBackwards_L1" };


struct scene {
    std::vector<draw_item>  items;
    std::vector<gfx::texture_id> textures;   // owned, one per material
    std::vector<mat_tex>    materials;       // parallel to textures, plus keyed variants
    std::vector<std::string> trap_types;     // distinct types present, for the legend
    std::vector<actor_inst>  actors;
    std::vector<std::string> item_names;    // parallel to items; the .K2A node name
    // The scene graph. Rooms and actors are children of the level root; room
    // geometry is authored in world space so rooms sit at identity, while
    // actors carry the transform their spawn point specifies.
    k2::engine::level_group* root;
    // PrinceStart / EnemyStart trap blocks found in the rooms. argv[0] is the
    // body name (Traps.txt); centre and pivot angle give the placement.
    std::vector<assets::k2a_trap> spawn_points;
    std::vector<assets::k2a_trap> fixture_points;   // Fixture traps, spawned as actors
    std::vector<assets::k2a_trap> special_points;   // Specials traps (SPEC_ spikes, blades)
    std::vector<assets::k2a_trap> rope_points;      // Rope traps: a TRAP_ROPE actor hangs at each
    // A swinging rope (Rope__ctor 0x487FF3): its actor, the Attach joint the
    // Prince hangs on, and where the rope's update is (idle, swing, end).
    struct rope {
        int actor = -1, attach = -1;
        float pivot[3] = { 0.0f, 0.0f, 0.0f };
        bool grabbed = false;
        enum stage_t { IDLE, SWING, END } stage = IDLE;
    };
    std::vector<rope> ropes;
    // A Specials trap: its SPEC_ actor and the actor's own Trigger and Kill
    // volumes (actor space). See specials_update.
    struct special {
        int    actor;
        float  trig[6], kill[6];
        bool   have_trig, have_kill;
        double struck_at, safe_at;      // -1 while not struck / not reset
        std::string death;              // the Prince's clip when it kills him
        special() : actor(-1), have_trig(false), have_kill(false), struck_at(-1), safe_at(-1) {}
    };
    std::vector<special> specials;
    // Static room triangles, for the pick primitive (0x512183). Actor bodies
    // and trigger volumes are not in it.
    k2::engine::collision_world coll;
    // The trap message system: every trap node in the loaded rooms, by name.
    k2::engine::script_world               script;
    std::vector<k2::engine::trap_object*>  traps;   // owned
    std::vector<k2::engine::area_trap*>    areas;   // also in `traps`
    std::vector<k2::engine::action_trap*>  actions; // also in `traps`
    std::vector<k2::engine::pressure_plate*> plates; // also in `traps`
    // Every room node's position, by lower-case name, for scripts that name
    // places: "camera,tripod,(@IntroNISCam01)", "SkillAt:...,IntroNISPH".
    // Engine space. Target cameras also carry where they look.
    struct named_point { float pos[3]; bool has_target; float target[3]; float yaw; };
    std::map<std::string, named_point> points;
    // Pickup traps: position (bbox centre) and argv, for the Prince to collect.
    struct pickup_def {
        std::string name; std::vector<std::string> argv; float pos[3];
        assets::k2a_trap trap;      // for placing its model
        int actor;                  // the model in actors, or -1
        pickup_def() : actor(-1) {}
    };
    std::vector<pickup_def> pickups;
    // Water traps' outlines: the surface mesh's triangles in xz (both
    // windings), the region Water__Update (0x48C601) tests the Prince
    // against - not the trap's box.
    struct water_area { size_t trap; std::vector<float> tris; float mn[2], mx[2]; };
    std::vector<water_area> waters;
    // Abyss traps (no arguments): falling into one is death. Ours; the
    // class is not traced.
    struct abyss_area { k2::engine::trap_object* trap; k2::engine::trap_volume vol; };
    std::vector<abyss_area> abysses;
    // Trap actors' skills, loaded on first use: "actor/skill" lower-case.
    std::map<std::string, assets::skl_file*> trap_skills;
    // Hand-held weapons (Geometry/Utils/WEAPONS.K2A, OtherWeapons.K2A), by
    // node name: draw items kept out of the world passes, drawn in a hand.
    std::map<std::string, std::vector<size_t> > weapons;
    bool weapons_loaded;
    float mn[3];
    float mx[3];
    bool  empty;

    scene() : weapons_loaded(false), root(0), empty(true) { mn[0]=mn[1]=mn[2]=0; mx[0]=mx[1]=mx[2]=0; }
};

// Trigger volumes carry no material, so they used to render as solid black
// blocks. Give each trap type its own colour instead.
//
// Hues are spread evenly over the types *actually present in the loaded scene*
// rather than derived from a global id. A global mapping is stable but useless
// in practice: a room holds only a handful of types, and hashing their indices
// put four of prison1's six on nearly the same purple. Even spacing guarantees
// they are distinguishable, and the legend printed at load maps colour to name.
// Assignment happens once per scene, so a whole level stays self-consistent.
void hue_color(float hue01, float* rgb) {
    const float hue = std::fmod(hue01, 1.0f) * 6.0f;
    const float s = 0.85f, v = 1.0f;
    const int   i = int(hue);
    const float f = hue - float(i);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: rgb[0]=v; rgb[1]=t; rgb[2]=p; break;
        case 1: rgb[0]=q; rgb[1]=v; rgb[2]=p; break;
        case 2: rgb[0]=p; rgb[1]=v; rgb[2]=t; break;
        case 3: rgb[0]=p; rgb[1]=q; rgb[2]=v; break;
        case 4: rgb[0]=t; rgb[1]=p; rgb[2]=v; break;
        default:rgb[0]=v; rgb[1]=p; rgb[2]=q; break;
    }
}

// Materials embed absolute authoring paths ("D:\\K2BoyDemo\\...\\wall .bmp"),
// so resolve by basename. Verified to work for every bitmap reference in the
// shipped rooms. Returns INVALID_TEXTURE when the file is missing or undecodable.
// Materials may point at a .ifl — an Image File List, one frame filename per
// line — rather than an image. 28 ship, driving animated textures such as the
// torch flames. Until animation is wired up, take the first frame.
std::string resolve_ifl(const std::string& path) {
    if (path.size() < 4) return path;
    std::string ext = path.substr(path.size() - 4);
    for (size_t i = 0; i < ext.size(); ++i)
        if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = char(ext[i] - 'A' + 'a');
    if (ext != ".ifl") return path;

    size_t size = 0;
    uint8_t* data = plat::fs_read_basename(path.c_str(), size);
    if (!data) return path;

    std::string first;
    const char* p   = reinterpret_cast<const char*>(data);
    const char* end = p + size;
    while (p < end && first.empty()) {
        const char* s0 = p;
        while (p < end && *p != '\n' && *p != '\r') ++p;
        std::string line(s0, size_t(p - s0));
        while (p < end && (*p == '\n' || *p == '\r')) ++p;
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == ';') continue;
        size_t b = line.find_first_of(" \t", a);
        first = line.substr(a, (b == std::string::npos ? line.size() : b) - a);
    }
    plat::fs_free(data);
    return first.empty() ? path : first;
}

// Loads one image. `chroma` keys pure black to transparent, as an
// ALPHA_SOURCE Chroma map or a trap's ChromaKey/Transparent flag asks.
// Rooms of one level share most of their textures (prison3_hall and
// prison3_junction carry prison3's whole material list), and every copy of
// an actor names the same ones: each image is decoded and uploaded once.
struct texture_cache_entry { gfx::texture_id tex; bool has_alpha; };
std::map<std::string, texture_cache_entry> g_texture_cache;
size_t g_texture_bytes = 0;

// Images decoded ahead on the worker cores (prefetch_rooms), by the same
// lower-case basename; load_image_texture takes them from here.
std::map<std::string, assets::image> g_decoded_images;

std::string image_key(const std::string& path) {
    std::string key = path;
    const size_t slash = key.find_last_of("/\\");
    if (slash != std::string::npos) key = key.substr(slash + 1);
    for (size_t i = 0; i < key.size(); ++i)
        if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');
    return key;
}

gfx::texture_id load_image_texture(const std::string& path, bool chroma, bool& has_alpha,
                                   int& decoded, int& failed) {
    const std::string name_key = image_key(path);
    std::string key = name_key;
    if (chroma) key += "#chroma";
    std::map<std::string, texture_cache_entry>::const_iterator hit = g_texture_cache.find(key);
    if (hit != g_texture_cache.end()) {
        if (hit->second.has_alpha) has_alpha = true;
        ++decoded;
        return hit->second.tex;
    }
    assets::image img;
    std::map<std::string, assets::image>::iterator pre = g_decoded_images.find(name_key);
    if (pre != g_decoded_images.end()) {
        img = pre->second;          // a copy: the chroma key below changes it
    } else {
        size_t size = 0;
        uint8_t* data = plat::fs_read_basename(path.c_str(), size);
        if (!data) { ++failed; return gfx::INVALID_TEXTURE; }
        std::string err;
        const bool ok = assets::image_load(data, size, img, &err);
        plat::fs_free(data);
        if (!ok || !img.valid()) { ++failed; return gfx::INVALID_TEXTURE; }
    }

    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        if (chroma && img.rgba[i] == 0 && img.rgba[i + 1] == 0 && img.rgba[i + 2] == 0)
            img.rgba[i + 3] = 0;
        if (img.rgba[i + 3] != 255) has_alpha = true;
    }
    ++decoded;
    texture_cache_entry e;
    e.tex = gfx::create_texture(img.width, img.height, &img.rgba[0]);
    e.has_alpha = false;
    for (size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] != 255) { e.has_alpha = true; break; }
    g_texture_cache[key] = e;
    g_texture_bytes += img.rgba.size();
    if (std::getenv("K2_TEXLIST")) {
        char real[1024] = "";
        plat::fs_resolve_basename(path.c_str(), real, sizeof(real));
        std::printf("tex %s %dx%d %s\n", key.c_str(), img.width, img.height, real);
    }
    if (pre != g_decoded_images.end()) g_decoded_images.erase(pre);   // uploaded: the CPU copy can go
    return e.tex;
}

// An IFL ("Image File List") is one image name per line, optionally followed
// by how many frames it is held. Anything else is a single image.
mat_tex load_material_texture(const std::string& bitmap_path, bool chroma,
                              int& decoded, int& failed) {
    mat_tex mt;
    if (bitmap_path.empty()) return mt;
    std::string ext = bitmap_path.size() >= 4 ? bitmap_path.substr(bitmap_path.size() - 4) : "";
    for (size_t i = 0; i < ext.size(); ++i)
        if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = char(ext[i] - 'A' + 'a');
    if (ext == ".ifl") {
        size_t size = 0;
        uint8_t* data = plat::fs_read_basename(bitmap_path.c_str(), size);
        if (data) {
            const char* p = reinterpret_cast<const char*>(data);
            const char* end = p + size;
            while (p < end) {
                const char* s0 = p;
                while (p < end && *p != '\n' && *p != '\r') ++p;
                std::string line(s0, size_t(p - s0));
                while (p < end && (*p == '\n' || *p == '\r')) ++p;
                const size_t a = line.find_first_not_of(" \t");
                if (a == std::string::npos || line[a] == ';') continue;
                const size_t b = line.find_first_of(" \t", a);
                const std::string name = line.substr(a, (b == std::string::npos ? line.size() : b) - a);
                int hold = 1;
                if (b != std::string::npos) { hold = std::atoi(line.c_str() + b); if (hold < 1) hold = 1; }
                const gfx::texture_id t = load_image_texture(name, chroma, mt.has_alpha, decoded, failed);
                if (t != gfx::INVALID_TEXTURE) { mt.frames.push_back(t); mt.holds.push_back(hold); }
            }
            plat::fs_free(data);
        }
        if (!mt.frames.empty()) return mt;
    }
    const gfx::texture_id t = load_image_texture(resolve_ifl(bitmap_path), chroma, mt.has_alpha,
                                                 decoded, failed);
    if (t != gfx::INVALID_TEXTURE) { mt.frames.push_back(t); mt.holds.push_back(1); }
    return mt;
}

// The texture a material shows at `time`. IFL frames advance at 30 per
// second, Max's default frame rate, each held for its listed count.
gfx::texture_id material_frame(const mat_tex& mt, double time) {
    if (mt.frames.empty()) return gfx::INVALID_TEXTURE;
    if (mt.frames.size() == 1) return mt.frames[0];
    int total = 0;
    for (size_t i = 0; i < mt.holds.size(); ++i) total += mt.holds[i];
    int f = int(std::fmod(time * 30.0, double(total)));
    for (size_t i = 0; i < mt.frames.size(); ++i) {
        if (f < mt.holds[i]) return mt.frames[i];
        f -= mt.holds[i];
    }
    return mt.frames[0];
}

// The texture a draw item shows now; animated materials step with `time`.
gfx::texture_id item_texture(const scene& sc, const draw_item& di, double time) {
    if (di.material >= 0 && size_t(di.material) < sc.materials.size())
        return material_frame(sc.materials[size_t(di.material)], time);
    return di.tex;
}

// Validates the recovered .act skeleton across every shipped actor: one root,
// every node parented, no cycles, and a plausible depth. See docs/formats/act.md.
int check_skeletons() {
    std::vector<std::string> paths;
    plat::fs_list_paths_by_extension(".act", paths);
    std::sort(paths.begin(), paths.end());

    size_t files = 0, ok = 0, bad = 0, total_nodes = 0;
    std::vector<std::string> failures;

    for (size_t i = 0; i < paths.size(); ++i) {
        size_t size = 0;
        uint8_t* d = plat::fs_read(paths[i].c_str(), size);
        if (!d) continue;
        assets::act_file a;
        std::string err;
        const bool parsed = assets::act_parse(d, size, a, &err);
        plat::fs_free(d);
        if (!parsed || a.nodes.empty()) continue;
        ++files;
        total_nodes += a.nodes.size();

        bool good = a.tree_ok;
        size_t roots = 0, deepest = 0;
        if (good) {
            for (size_t k = 0; k < a.nodes.size(); ++k) {
                if (a.nodes[k].parent < 0) ++roots;
                size_t depth = 0;
                int32_t pth = a.nodes[k].parent;
                while (pth >= 0 && depth <= a.nodes.size()) {
                    ++depth;
                    pth = a.nodes[size_t(pth)].parent;
                }
                if (depth > a.nodes.size()) { good = false; break; }   // cycle
                if (depth > deepest) deepest = depth;
            }
            if (roots != 1) good = false;
        }
        if (good) ++ok;
        else { ++bad; if (failures.size() < 8) failures.push_back(paths[i]); }
    }

    std::printf("skeletons: %zu .act files, %zu nodes\n", files, total_nodes);
    std::printf("  tree recovered  %zu\n", ok);
    std::printf("  failed          %zu\n", bad);
    for (size_t i = 0; i < failures.size(); ++i)
        std::printf("    %s\n", failures[i].c_str());
    return bad == 0 ? 0 : 1;
}

// Sweeps every .K2A in the install and reports how many materials resolve a
// texture, how many are flagged blended, and how many name a bitmap that cannot
// be found. Added after the per-channel material fix, which changed *which*
// bitmap each material uses - a change worth checking install-wide rather than
// on one room. See docs/formats/k2a.md.
int check_materials() {
    std::vector<std::string> paths;
    plat::fs_list_paths_by_extension(".k2a", paths);
    std::sort(paths.begin(), paths.end());

    size_t files = 0, mats = 0, with_bitmap = 0, resolved = 0, blended = 0, missing = 0;
    std::vector<std::string> misses;

    for (size_t i = 0; i < paths.size(); ++i) {
        size_t size = 0;
        uint8_t* d = plat::fs_read(paths[i].c_str(), size);
        if (!d) continue;
        assets::k2a_file f;
        std::string err;
        const bool ok = assets::k2a_parse(d, size, f, &err);
        plat::fs_free(d);
        if (!ok) continue;
        ++files;

        for (size_t m = 0; m < f.materials.size(); ++m) {
            ++mats;
            if (f.materials[m].alpha == assets::K2A_ALPHA_OPACITY) ++blended;
            const std::string& b = f.materials[m].bitmap;
            if (b.empty()) continue;
            ++with_bitmap;
            const std::string img = resolve_ifl(b);
            char real[4096];
            if (plat::fs_resolve_basename(img.c_str(), real, sizeof(real))) ++resolved;
            else {
                ++missing;
                if (misses.size() < 10) misses.push_back(img);
            }
        }
    }

    std::printf("materials: %zu files, %zu materials\n", files, mats);
    std::printf("  name a bitmap   %zu\n", with_bitmap);
    std::printf("  bitmap resolves %zu\n", resolved);
    std::printf("  unresolved      %zu\n", missing);
    std::printf("  blended (MAP_OPACITY or TRANSPARENCY>0) %zu (%.1f%%)\n",
                blended, mats ? 100.0 * double(blended) / double(mats) : 0.0);
    for (size_t i = 0; i < misses.size(); ++i)
        std::printf("    missing: %s\n", misses[i].c_str());
    return missing == 0 ? 0 : 1;
}

// Determines .K2A triangle winding, which gates backface culling.
//
// For a closed mesh the divergence theorem gives a signed volume from the
// triangles alone: V = sum dot(v0, cross(v1-v0, v2-v0)) / 6. With outward-facing
// counter-clockwise winding (OpenGL's default front face) V is positive. Actor
// body parts are watertight, so they are the meshes to measure; room walls are
// often single-sided sheets and are reported separately.
int check_winding() {
    std::vector<std::string> paths;
    plat::fs_list_paths_by_extension(".k2a", paths);
    std::sort(paths.begin(), paths.end());

    size_t pos = 0, neg = 0, flat = 0, meshes = 0;
    double pos_vol = 0.0, neg_vol = 0.0;

    for (size_t i = 0; i < paths.size(); ++i) {
        // Actor geometry lives under Geometry\Enemies or \prince; rooms do not.
        std::string lower = paths[i];
        for (size_t k = 0; k < lower.size(); ++k)
            lower[k] = char(std::tolower((unsigned char)lower[k]));
        const bool is_actor = lower.find("enemies") != std::string::npos ||
                              lower.find("prince")  != std::string::npos;
        if (!is_actor) continue;

        size_t size = 0;
        uint8_t* d = plat::fs_read(paths[i].c_str(), size);
        if (!d) continue;
        assets::k2a_file f;
        std::string err;
        const bool ok = assets::k2a_parse(d, size, f, &err);
        plat::fs_free(d);
        if (!ok) continue;

        for (size_t n = 0; n < f.nodes.size(); ++n) {
            const assets::k2a_node& nd = f.nodes[n];
            if (!nd.has_mesh || nd.has_trap) continue;
            std::vector<assets::k2a_vertex> vb;
            std::vector<uint16_t> ib;
            assets::k2a_build_mesh(nd, vb, ib);
            if (ib.size() < 3) continue;
            ++meshes;

            double vol = 0.0;
            for (size_t t = 0; t + 2 < ib.size(); t += 3) {
                const assets::k2a_vertex& a = vb[ib[t]];
                const assets::k2a_vertex& b = vb[ib[t+1]];
                const assets::k2a_vertex& c = vb[ib[t+2]];
                const double e0[3] = {b.x-a.x, b.y-a.y, b.z-a.z};
                const double e1[3] = {c.x-a.x, c.y-a.y, c.z-a.z};
                const double cr[3] = {e0[1]*e1[2]-e0[2]*e1[1],
                                      e0[2]*e1[0]-e0[0]*e1[2],
                                      e0[0]*e1[1]-e0[1]*e1[0]};
                vol += a.x*cr[0] + a.y*cr[1] + a.z*cr[2];
            }
            vol /= 6.0;
            if      (vol >  1e-6) { ++pos; pos_vol += vol; }
            else if (vol < -1e-6) { ++neg; neg_vol += vol; }
            else                    ++flat;
        }
    }

    std::printf("winding over %zu actor meshes\n", meshes);
    std::printf("  positive volume (CCW outward) %zu   total %.3f\n", pos, pos_vol);
    std::printf("  negative volume (CW outward)  %zu   total %.3f\n", neg, neg_vol);
    std::printf("  degenerate / open             %zu\n", flat);
    const size_t dom = pos > neg ? pos : neg;
    std::printf("  verdict: %s (%.1f%% agree)\n",
                pos == neg ? "inconclusive" : (pos > neg ? "CCW front faces" : "CW front faces"),
                (pos + neg) ? 100.0 * double(dom) / double(pos + neg) : 0.0);
    return 0;
}

// Recomputes every joint's world transform from its current local pose. Joints
// are depth-first, so one forward pass suffices.
void pose_actor(actor_inst& a) {
    for (size_t i = 0; i < a.joint_local_cur.size(); ++i) {
        const int32_t p = a.joint_parent[i];
        if (p >= 0) k2::engine::transform_compose(a.cur[size_t(p)], a.joint_local_cur[i], a.cur[i]);
        else        a.cur[i] = a.joint_local_cur[i];
    }
}

// Room geometry is world-space; a trap moved by script draws at its offset.
void set_item_model(const draw_item& di) {
    if (di.has_model) { gfx::set_model(di.model); return; }
    float m[16];
    mat4_identity(m);
    m[12] = di.offset[0];
    m[13] = di.offset[1];
    m[14] = di.offset[2];
    gfx::set_model(m);
}

void transform_from_pos_quat(const float* pos, const float* q, k2::engine::transform& t) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    t.rotate[0]=1-2*(y*y+z*z); t.rotate[1]=  2*(x*y-w*z); t.rotate[2]=  2*(x*z+w*y);
    t.rotate[3]=  2*(x*y+w*z); t.rotate[4]=1-2*(x*x+z*z); t.rotate[5]=  2*(y*z-w*x);
    t.rotate[6]=  2*(x*z-w*y); t.rotate[7]=  2*(y*z+w*x); t.rotate[8]=1-2*(x*x+y*y);
    for (int k = 0; k < 3; ++k) t.translate[k] = pos[k];
    t.scale = 1.0f;
}

// Length of one cycle in ms. The skill stores it (Skil +0x80); a clip without
// it falls back to its longest joint path.
uint32_t clip_length_ms(const assets::skl_file& s) {
    if (s.duration_ms) return s.duration_ms;
    uint32_t len = 0;
    for (size_t j = 0; j < s.joints.size(); ++j) {
        const assets::skl_joint_path& p = s.joints[j].path;
        if (s.joints[j].present && p.frames * p.interval_ms > len) len = p.frames * p.interval_ms;
        // SklK: keyframed joints end at their last key.
        if (!s.joints[j].keys.empty() && s.joints[j].keys.back().time > len) len = s.joints[j].keys.back().time;
    }
    return len;
}

// One joint of a skill at `t`: SklS sampled paths or SklK keyframes. False
// when the skill does not drive it.
bool skill_joint(const assets::skl_file& s, size_t j, uint32_t t, float* pos, float* q) {
    if (j >= s.joints.size()) return false;
    const assets::skl_joint& jt = s.joints[j];
    if (!jt.keys.empty()) return assets::skl_sample_keys(jt.keys, t, pos, q);
    // A keyframed clip lists joints it has no keys for: those keep their
    // pose, as an absent joint does.
    if (!jt.present || (jt.path.pos.empty() && jt.path.rot.empty())) return false;
    assets::skl_sample(jt.path, t, s.interpolate != 0, pos, q);
    return true;
}

// Root motion: how far joint 0 travels in one cycle, in its parent's space.
// Locomotion clips carry the actor's travel in the root's track, so this is
// both the walk direction in model space and, over the clip length, its speed.
bool clip_root_motion(const assets::skl_file& s, float* delta) {
    if (s.joints.empty() || !s.joints[0].present) return false;
    const assets::skl_joint_path& p = s.joints[0].path;
    if (p.pos_keys() < 2) return false;
    const float* a = &p.pos[0];
    const float* b = &p.pos[(p.pos_keys() - 1) * 3];
    for (int k = 0; k < 3; ++k) delta[k] = b[k] - a[k];
    return true;
}

// Speed of a locomotion clip in units per second, from its root motion.
float clip_speed(const assets::skl_file& s) {
    float d[3];
    const uint32_t len = clip_length_ms(s);
    if (!clip_root_motion(s, d) || !len) return 0.0f;
    // The track's last key is one interval short of the loop point.
    const assets::skl_joint_path& p = s.joints[0].path;
    const float span_ms = float((p.pos_keys() - 1) * p.interval_ms);
    if (span_ms <= 0.0f) return 0.0f;
    return std::sqrt(d[0]*d[0] + d[2]*d[2]) / (span_ms / 1000.0f);
}

int32_t play_sound(const std::string& raw, const float* pos, float radius, bool loop);

// A clip's sound keys (SKey) between two play-head times fire at the actor.
// The six planes of the view frustum from a GL projection * view matrix
// (column-major), each normalised: a sphere is outside when it is wholly
// behind any one of them.
struct frustum {
    float p[6][4];
    void from(const float* m) {
        for (int i = 0; i < 3; ++i)
            for (int sgn = 0; sgn < 2; ++sgn) {
                float* q = p[i * 2 + sgn];
                const float k = sgn ? -1.0f : 1.0f;
                for (int c = 0; c < 4; ++c) q[c] = m[c * 4 + 3] + k * m[c * 4 + i];
                const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                if (l > 0) for (int c = 0; c < 4; ++c) q[c] /= l;
            }
    }
    bool sphere(const float* c, float r) const {
        for (int i = 0; i < 6; ++i)
            if (p[i][0] * c[0] + p[i][1] * c[1] + p[i][2] * c[2] + p[i][3] < -r) return false;
        return true;
    }
};

// A sound key reached while posing on a worker thread, played afterwards on
// the main thread (the mixer is not thread-safe).
struct deferred_sound { const std::string* name; float at[3]; };

void fire_sound_keys(const assets::skl_file& s, double from_ms, double to_ms, const float* at,
                     std::vector<deferred_sound>* defer = 0) {
    for (size_t i = 0; i < s.sound_keys.size(); ++i) {
        const double t = s.sound_keys[i].time;
        if (!((t > from_ms && t <= to_ms) || (from_ms == 0.0 && t == 0.0 && to_ms > 0.0))) continue;
        if (defer) {
            deferred_sound d;
            d.name = &s.sound_keys[i].sound;
            for (int k = 0; k < 3; ++k) d.at[k] = at[k];
            defer->push_back(d);
        } else {
            play_sound(s.sound_keys[i].sound, at, 14.0f, false);
        }
    }
}

void set_clip(actor_inst& a, int clip) {
    a.clip_once = false;
    if (clip == a.clip) return;
    if (clip >= 0 && (clip >= int(a.loco_ok.size()) || !a.loco_ok[size_t(clip)])) clip = -1;
    a.clip = clip;
    a.clip_ms = 0.0;
}

// Plays the current clip: samples every driven joint through the original's
// sampler and re-poses the skeleton. Joints the clip does not drive keep their
// own transform, as 0x63BA81 does. The root's translation is the clip's root
// motion; the caller moves the actor node instead, so it is held at bind here.
void animate_actor(actor_inst& a, float dt, std::vector<deferred_sound>* defer = 0) {
    if (!a.skinned || a.posed_hold) return;
    if (a.clip < 0 && !a.stance) return;
    const assets::skl_file& s = a.stance ? *a.stance : a.loco[size_t(a.clip)];
    const uint32_t len = clip_length_ms(s);
    const double before = a.clip_ms;
    a.clip_ms += double(dt) * 1000.0;
    if (!s.sound_keys.empty() && a.node) {
        const float* at = a.node->local.translate;
        if (len && !a.clip_once && a.clip_ms >= double(len)) {
            fire_sound_keys(s, before, double(len), at, defer);       // the end of this cycle
            fire_sound_keys(s, -1.0, a.clip_ms - double(len), at, defer);
        } else {
            fire_sound_keys(s, before, a.clip_ms, at, defer);
        }
    }
    if (len) a.clip_ms = a.clip_once ? std::min(a.clip_ms, double(len))
                                     : std::fmod(a.clip_ms, double(len));
    const uint32_t t = uint32_t(a.clip_ms);

    const size_t n = std::min(a.joint_local_cur.size(), s.joints.size());
    for (size_t j = 0; j < n; ++j) {
        if (!s.joints[j].present) { a.joint_local_cur[j] = a.joint_local_bind[j]; continue; }
        float pos[3], q[4];
        assets::skl_sample(s.joints[j].path, t, s.interpolate != 0, pos, q);
        if (j == 0)
            for (int k = 0; k < 3; ++k) pos[k] = a.joint_local_bind[0].translate[k];
        transform_from_pos_quat(pos, q, a.joint_local_cur[j]);
    }
    if (a.body_pitch != 0.0f && !a.joint_local_cur.empty()) {
        // Tip the root about the pelvis: v' = P (v - pivot) + pivot.
        const float c = std::cos(a.body_pitch), sn = std::sin(a.body_pitch);
        const float pivot = 1.03f;
        k2::engine::transform P;
        P.rotate[4] = c;  P.rotate[5] = -sn;
        P.rotate[7] = sn; P.rotate[8] = c;
        P.translate[1] = pivot - c * pivot;
        P.translate[2] = -sn * pivot;
        k2::engine::transform_compose(P, a.joint_local_cur[0], a.joint_local_cur[0]);
    }
    pose_actor(a);
}

// Every body part's model matrix for this frame into a.part_mats:
// actor_world * cur[j] * bind_inv[j], or the actor's own for an unbound part.
void actor_part_matrices(const scene& sc, actor_inst& a, bool skin);

// The .act's directory inside the VFS, e.g. "actors/prince/". Skills live in its
// "skills" subdirectory. Found by matching the .act's basename against every
// indexed .act path, because the master.dat names it by basename only.
std::string actor_dir_for(const std::string& act_name) {
    std::string base = act_name;
    const size_t cut = base.find_last_of("/\\");
    if (cut != std::string::npos) base = base.substr(cut + 1);
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] >= 'A' && base[i] <= 'Z') base[i] = char(base[i] - 'A' + 'a');

    // They live at Actors/<name>/<name>.act or Geometry/Enemies/<name>/:
    // tried first, so a level does not need the whole install indexed.
    {
        const std::string stem = base.size() > 4 ? base.substr(0, base.size() - 4) : base;
        static const char* roots[] = { "actors/", "geometry/enemies/", 0 };
        for (int r = 0; roots[r]; ++r) {
            const std::string dir = std::string(roots[r]) + stem + "/";
            char real[1024];
            if (plat::fs_probe((dir + base).c_str(), real, sizeof(real))) return dir;
        }
    }
    std::vector<std::string> acts;
    plat::fs_list_paths_by_extension(".act", acts);
    for (size_t i = 0; i < acts.size(); ++i) {
        const std::string& p = acts[i];
        if (p.size() >= base.size() + 1 &&
            p.compare(p.size() - base.size(), base.size(), base) == 0 &&
            p[p.size() - base.size() - 1] == '/')
            return p.substr(0, p.size() - base.size());
    }
    return std::string();
}

void load_loco_clips(const std::string& act_name, actor_inst& a) {
    a.loco.assign(LOCO_COUNT, assets::skl_file());
    a.loco_ok.assign(LOCO_COUNT, false);
    std::string dir = actor_dir_for(act_name);
    // The archers (PG3Archer, A2ARCHER, D4Archer) have no skills of their
    // own; their base guard's folder (PG3/SKILLS) has the Bow* clips. Ours:
    // how the .act points there is not traced.
    if (!dir.empty()) {
        std::string l = dir;
        for (size_t i = 0; i < l.size(); ++i) l[i] = char(std::tolower(static_cast<unsigned char>(l[i])));
        const size_t at = l.rfind("archer/");
        if (at != std::string::npos) {
            a.archer = true;
            char real[1024];
            if (!plat::fs_probe((dir + "skills/basepose_l.skl").c_str(), real, sizeof(real))) {
                const std::string base = dir.substr(0, at) + "/";
                if (plat::fs_probe((base + "skills/basepose_l.skl").c_str(), real, sizeof(real))) dir = base;
            }
        }
    }
    a.skill_dir = dir;
    if (dir.empty()) return;
    for (int c = 0; c < LOCO_COUNT; ++c) {
        const std::string path = dir + "skills/" + k_loco_names[c] + ".skl";
        size_t size = 0;
        uint8_t* d = plat::fs_read(path.c_str(), size);
        if (!d) continue;
        std::string err;
        a.loco_ok[size_t(c)] = assets::skl_parse(d, size, a.loco[size_t(c)], &err);
        plat::fs_free(d);
        if (!a.loco_ok[size_t(c)])
            std::printf("  skill %s: %s\n", path.c_str(), err.c_str());
        else if (a.loco[size_t(c)].chunk_tag != "SklS")
            // SklK skills are keyframe splines; their evaluator is not ported.
            std::printf("  skill %s: SklK playback not implemented, ignored\n", path.c_str()),
            a.loco_ok[size_t(c)] = false;
        if (!a.loco_ok[size_t(c)]) continue;
        if (c > LOCO_WALK) continue;
        const assets::skl_file& s = a.loco[size_t(c)];
        float rm[3] = {0, 0, 0};
        clip_root_motion(s, rm);
        std::printf("  skill %-11s %u ms, %zu joints, interp %d, root motion (%.3f, %.3f, %.3f) -> %.2f u/s\n",
                    k_loco_names[c], clip_length_ms(s), s.joints.size(), s.interpolate,
                    rm[0], rm[1], rm[2], clip_speed(s));
    }
}

// Fills an actor's skeleton from its .act and binds each body part to a joint by
// name. Every .K2A geometry node has a matching .act node (zero orphans across
// the shipped actors), so the match is by name and nothing else.
bool bind_actor_skeleton(const assets::master_file& m, actor_inst& inst, scene& sc) {
    const std::string act_path = m.proto ? m.actor : (m.actor + ".act");
    size_t size = 0;
    uint8_t* d = plat::fs_read_basename(act_path.c_str(), size);
    if (!d) return false;
    assets::act_file a;
    std::string err;
    const bool ok = assets::act_parse(d, size, a, &err);
    plat::fs_free(d);
    if (!ok || !a.tree_ok || a.nodes.empty()) return false;

    const size_t n = a.nodes.size();
    inst.joint_names.resize(n);
    inst.joint_parent.resize(n);
    inst.joint_local_bind.resize(n);
    inst.joint_local_cur.resize(n);
    inst.bind_inv.resize(n);
    inst.cur.resize(n);

    std::vector<k2::engine::transform> world(n);
    for (size_t i = 0; i < n; ++i) {
        const assets::act_node& nd = a.nodes[i];
        k2::engine::transform local;
        const float w = nd.quat[0], x = nd.quat[1], y = nd.quat[2], z = nd.quat[3];
        local.rotate[0]=1-2*(y*y+z*z); local.rotate[1]=  2*(x*y-w*z); local.rotate[2]=  2*(x*z+w*y);
        local.rotate[3]=  2*(x*y+w*z); local.rotate[4]=1-2*(x*x+z*z); local.rotate[5]=  2*(y*z-w*x);
        local.rotate[6]=  2*(x*z-w*y); local.rotate[7]=  2*(y*z+w*x); local.rotate[8]=1-2*(x*x+y*y);
        for (int k = 0; k < 3; ++k) local.translate[k] = nd.pos[k];
        local.scale = 1.0f;

        inst.joint_names[i]      = nd.name;
        inst.joint_parent[i]     = nd.parent;
        inst.joint_local_bind[i] = local;
        inst.joint_local_cur[i]  = local;

        if (nd.parent >= 0) k2::engine::transform_compose(world[size_t(nd.parent)], local, world[i]);
        else                world[i] = local;

        k2::engine::transform_inverse(world[i], inst.bind_inv[i]);
        inst.cur[i] = world[i];          // start at the bind pose
    }

    // Bind each of this actor's draw items to its joint, by node name.
    size_t bound = 0;
    for (size_t k = 0; k < inst.item_count; ++k) {
        draw_item& di = sc.items[inst.first_item + k];
        const std::string& mesh_name = sc.item_names[inst.first_item + k];
        for (size_t j = 0; j < n; ++j)
            if (inst.joint_names[j] == mesh_name) { di.joint = int(j); ++bound; break; }
    }
    // Some bodies (the prisoners) are modelled about the pelvis rather than
    // the feet: every part sits ~1.03 below its joint, where the Prince's sit
    // within a bone's length. Measured as the median part-to-joint offset;
    // when it is that large the mesh is taken back into the skeleton's space
    // by the pelvis's bind position, or the limbs pivot in the wrong places.
    {
        std::vector<float> dy;
        for (size_t k = 0; k < inst.item_count; ++k) {
            const draw_item& di = sc.items[inst.first_item + k];
            if (di.joint >= 0) dy.push_back(di.center[1] - world[size_t(di.joint)].translate[1]);
        }
        if (!dy.empty()) {
            std::nth_element(dy.begin(), dy.begin() + long(dy.size() / 2), dy.end());
            const float med = dy[dy.size() / 2];
            if (std::fabs(med) > 0.5f) {
                float d[3] = { 0.0f, med, 0.0f };
                for (size_t j = 0; j < n; ++j)
                    if (inst.joint_names[j] == "Pelvis") {
                        for (int k = 0; k < 3; ++k) d[k] = -world[j].translate[k];
                        break;
                    }
                k2::engine::transform shift;     // mesh space -> skeleton space
                for (int k = 0; k < 9; ++k) shift.rotate[k] = (k % 4 == 0) ? 1.0f : 0.0f;
                for (int k = 0; k < 3; ++k) shift.translate[k] = -d[k];
                shift.scale = 1.0f;
                for (size_t j = 0; j < n; ++j) {
                    k2::engine::transform b;
                    k2::engine::transform_compose(inst.bind_inv[j], shift, b);
                    inst.bind_inv[j] = b;
                }
                std::printf("  %s: mesh is %.2f off its skeleton, rebound\n", inst.name.c_str(), med);
            }
        }
    }
    inst.skinned = bound > 0;
    if (inst.skinned) {
        load_loco_clips(act_path, inst);
        set_clip(inst, LOCO_BASE);
    }
    return inst.skinned;
}

bool append_scene(const char* path, scene& sc, bool collide);

// Loads the weapon meshes once and puts master.dat's first weapon (or
// `which`, its index) in the actor's hand.
void bind_actor_weapon(const assets::master_file& m, actor_inst& inst, scene& sc, int which) {
    if (!sc.weapons_loaded) {
        sc.weapons_loaded = true;
        const char* files[2] = { "geometry\\utils\\WEAPONS.K2A", "geometry\\utils\\OtherWeapons.K2A" };
        for (int f = 0; f < 2; ++f) {
            const size_t before = sc.items.size();
            const float mn[3] = { sc.mn[0], sc.mn[1], sc.mn[2] }, mx[3] = { sc.mx[0], sc.mx[1], sc.mx[2] };
            if (!append_scene(files[f], sc, false)) continue;
            for (int k = 0; k < 3; ++k) { sc.mn[k] = mn[k]; sc.mx[k] = mx[k]; }
            for (size_t k = before; k < sc.items.size(); ++k) {
                sc.items[k].hidden = true;          // not part of the world
                std::vector<size_t>& v = sc.weapons[sc.item_names[k]];
                if (v.empty() || f == 0) v.push_back(k);
            }
        }
    }
    if (m.weapons.empty() || m.hands.empty()) return;
    const size_t w = which >= 0 && size_t(which) < m.weapons.size() ? size_t(which) : 0;
    inst.weapon = m.weapons[w].name;
    for (size_t j = 0; j < inst.joint_names.size(); ++j)
        if (inst.joint_names[j] == m.hands[0].node) { inst.hand_joint = int(j); break; }
    for (int k = 0; k < 3; ++k) inst.hand_off[k] = m.hands[0].offset[k];
    if (m.hands.size() > 1) {
        for (size_t j = 0; j < inst.joint_names.size(); ++j)
            if (inst.joint_names[j] == m.hands[1].node) { inst.alt_hand_joint = int(j); break; }
        for (int k = 0; k < 3; ++k) inst.alt_hand_off[k] = m.hands[1].offset[k];
    }
    if (inst.hand_joint < 0 || !sc.weapons.count(inst.weapon))
        std::printf("  %s: weapon %s / hand %s not found\n", inst.name.c_str(), inst.weapon.c_str(),
                    m.hands[0].node.c_str());
}

class effect_trap;
std::vector<effect_trap*> g_effect_traps;     // owned by sc.traps

void scene_reset(scene& sc) {
    sc.items.clear();
    sc.item_names.clear();
    sc.textures.clear();
    sc.materials.clear();
    sc.trap_types.clear();
    sc.actors.clear();
    delete sc.root;
    sc.root = new k2::engine::level_group();
    sc.root->name = "LevelRoot";
    sc.spawn_points.clear();
    sc.fixture_points.clear();
    sc.special_points.clear();
    sc.rope_points.clear();
    sc.ropes.clear();
    sc.specials.clear();
    sc.points.clear();
    sc.pickups.clear();
    sc.coll.clear();
    sc.script.clear();
    for (size_t i = 0; i < sc.traps.size(); ++i) delete sc.traps[i];
    sc.traps.clear();
    sc.waters.clear();
    g_effect_traps.clear();
    sc.areas.clear();
    sc.actions.clear();
    sc.plates.clear();
    for (std::map<std::string, assets::skl_file*>::iterator it = sc.trap_skills.begin();
         it != sc.trap_skills.end(); ++it) delete it->second;
    sc.trap_skills.clear();
    sc.weapons.clear();
    sc.weapons_loaded = false;
    sc.empty = true;
    sc.mn[0] = sc.mn[1] = sc.mn[2] =  1e30f;
    sc.mx[0] = sc.mx[1] = sc.mx[2] = -1e30f;
}

// Darts in flight, from Launcher traps ("fire"). Traps.txt: argv1 Dart (1)
// or Fireball (2). A dart flies straight from the launcher along its facing
// until it hits the Prince (a bottle, Arrow::kArrowNormalDamage 12) or a
// wall; its speed and the facing convention (-Z turned by the trap's pivot)
// are ours - the Launcher class is not traced.
struct dart { float p[3], d[3]; double born; };
std::vector<dart> g_darts;

// Arrows in flight (enemy.dat [Archery]: kArrowSpeed 60, kArrowNormalDamage
// 12): straight on until a body, an ArrowTarget or a wall, where one sticks
// for a while. The flight and the hits are ours; Arrow's class is not traced.
namespace arrow_tuning {
const float SPEED = 60.0f;
const int   DAMAGE = 12;
const double STUCK_FOR = 4.0;
}
struct arrow_shot { float p[3], d[3]; double born, stuck_at; bool from_prince; };
std::vector<arrow_shot> g_arrows;
void fire_arrow(const float* from, const float* to, bool from_prince, double now) {
    arrow_shot a;
    float d[3] = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l < 1e-4f) return;
    for (int k = 0; k < 3; ++k) { a.p[k] = from[k]; a.d[k] = d[k] / l; }
    a.born = now;
    a.stuck_at = -1.0;
    a.from_prince = from_prince;
    g_arrows.push_back(a);
}

// Parses one .K2A and appends its meshes to `sc`, growing the bounds.
// A trap node in a room. The base handles reset / state / enable / visible /
// collide (0x4552B7); this adds what those mean for drawing and collision, and
// the "move" message Force, Mover and Generic traps understand (0x4722E7).
class scene_trap : public k2::engine::trap_object {
public:
    scene_trap(k2::engine::script_world& w, const std::string& name,
               const k2::engine::trap_header& h, scene& sc, int item, int32_t coll_node,
               const std::string& type, const float* bbox)
        : trap_object(w, name, h), type(type), m_sc(sc), m_item(item), m_node(coll_node),
          m_moving(false), m_skill(0), m_skill_start(0) {
        m_base[0] = m_base[1] = m_base[2] = 0.0f;
        for (int i = 0; i < 6; ++i) box[i] = bbox[i];
        pivot[0] = pivot[1] = pivot[2] = 0.0f;
        for (int i = 0; i < 9; ++i) m_rot[i] = i % 4 == 0 ? 1.0f : 0.0f;
    }

    const std::string type;     // the K2_TRAP_NAME: "Crate", "Force", ...
    float             box[6];   // K2_TRAP_BBOX at rest, engine space
    std::string       actor;    // whose skills a message can name (Door: argv1)
    float             pivot[3]; // K2_TRAP_PIVOT: a skill's rotation is about this
    float             pivot_angle = 0.0f;   // K2_TRAP_PIVOT_ANGLE about its axis (Launcher)
    // A Volume ForceTrap's current ("current,green,4": along the trap's
    // green axis turned by its pivot, 4 units a second), pushing whoever
    // swims inside its box. Ours: ForceTrap's Volume type is not traced.
    bool              has_current = false;
    // A CrateTarget's message (Traps.txt argv1), sent once a crate stands
    // in its box (prison4: pushing the crate over the pit drops it on the
    // guard below and frees the sword).
    std::string       message;
    float             current[3] = { 0.0f, 0.0f, 0.0f };

    // Where the trap sits now, relative to rest.
    const float* offset() const { return m_base; }
    int32_t coll_node() const { return m_node; }
    // Puts it somewhere directly, as a crate carried by the Prince is.
    void place(const float* off) {
        for (int k = 0; k < 3; ++k) m_base[k] = off[k];
        apply(off);
    }

    void set_visible(bool b) {
        trap_object::set_visible(b);
        if (m_item >= 0) m_sc.items[size_t(m_item)].hidden = !b;
    }
    void set_collideable(bool b) {
        trap_object::set_collideable(b);
        m_sc.coll.set_node_enabled(m_node, b);
    }
    bool handle(const std::string& msg, k2::engine::script_target* sender) {
        if (is_motion(msg)) return motions(msg);
        // Water: "raise,d,t" / "lower,d,t" move the surface d along y over t
        // seconds - a TranslateMotion built directly (0x48C939 -> 0x47ABDC),
        // with no ease.
        if (type == "Water" && (msg.compare(0, 6, "raise,") == 0 || msg.compare(0, 6, "lower,") == 0)) {
            const bool up = msg[0] == 'r';
            std::string rest = msg.substr(6);
            const size_t c = rest.find(',');
            const float d = float(std::atof(rest.c_str()));
            const float t = c == std::string::npos ? 0.0f : float(std::atof(rest.c_str() + c + 1));
            k2::engine::translate_motion m;
            if (!m.parse(std::string("blue,") + (up ? "" : "-") + std::to_string(d) + "," +
                         std::to_string(t) + ",0,0")) return false;
            m.begin(m_world.now());
            m_motion = m;
            m_moving = true;
            return true;
        }
        if (type == "Launcher" && msg.size() >= 4 && (msg.compare(0, 4, "fire") == 0 || msg.compare(0, 4, "Fire") == 0)) {
            dart dt;
            for (int k = 0; k < 3; ++k) dt.p[k] = (box[k] + box[k + 3]) * 0.5f + m_base[k];
            // -Z turned about +y by the pivot (signed by its axis).
            const float a = pivot_angle;
            dt.d[0] = -std::sin(a); dt.d[1] = 0.0f; dt.d[2] = -std::cos(a);
            dt.born = m_world.now();
            g_darts.push_back(dt);
            play_sound("MULdartshoot", dt.p, 14.0f, false);
            return true;
        }
        // A Door plays the skill its message names ("openslow", "pivot90");
        // the original reports "Doors can't handle message %s" otherwise.
        if (!actor.empty()) {
            const assets::skl_file* sk = find_skill(msg);
            if (sk) { m_skill = sk; m_skill_start = m_world.now(); m_skill_name = msg; return true; }
        }
        return trap_object::handle(msg, sender);
    }

    // A CollapsingFloor (Traps.txt: argv1 the actor whose skills it plays,
    // argv2 wobble, argv3 fall, argv4 crash): stood on, it wobbles through
    // its wobble skill, then drops - the fall skill is a single key, so the
    // drop is gravity's, and it no longer holds anyone up - until it lands
    // (or 30 units on), where the crash skill sounds. The trigger and the
    // drop are ours; the class is not traced.
    std::string wobble_skill, fall_skill, crash_skill;
    void collapse_update(double now, float dt, const float* prince) {
        if (type != "CollapsingFloor" || m_collapse == COLLAPSE_DOWN || !m_enabled) return;
        if (m_collapse == COLLAPSE_NONE) {
            if (!prince) return;
            const float x = prince[0] - m_base[0], y = prince[1] - m_base[1], z = prince[2] - m_base[2];
            if (x < box[0] || x > box[3] || z < box[2] || z > box[5] || y < box[4] - 0.3f || y > box[4] + 0.4f)
                return;
            const assets::skl_file* w = find_skill(wobble_skill);
            if (w) { m_skill = w; m_skill_start = now; }
            m_collapse_until = now + (w ? clip_length_ms(*w) / 1000.0 : 0.3);
            m_collapse = COLLAPSE_WOBBLE;
            if (w) fire_sound_keys(*w, 0.0, 1.0, prince);
            return;
        }
        if (m_collapse == COLLAPSE_WOBBLE) {
            if (now < m_collapse_until) return;
            m_skill = 0;
            set_collideable(false);
            m_fall_v = 0.0f;
            m_fall_from = m_base[1];
            m_collapse = COLLAPSE_FALLING;
            if (const assets::skl_file* f = find_skill(fall_skill)) {
                const float c[3] = { (box[0] + box[3]) * 0.5f + m_base[0], box[4] + m_base[1], (box[2] + box[5]) * 0.5f + m_base[2] };
                fire_sound_keys(*f, 0.0, 1.0, c);
            }
            return;
        }
        // Falling.
        m_fall_v += 17.0f * dt;                     // FallGravity (0x4F8547)
        const float step = m_fall_v * dt;
        const float c[3] = { (box[0] + box[3]) * 0.5f + m_base[0], box[1] + m_base[1], (box[2] + box[5]) * 0.5f + m_base[2] };
        const float down[3] = { 0.0f, -1.0f, 0.0f };
        k2::engine::pick_hit h;
        float off[3] = { m_base[0], m_base[1] - step, m_base[2] };
        bool landed = false;
        if (m_sc.coll.pick(c, down, step, true, h)) { off[1] = m_base[1] - h.distance; landed = true; }
        if (m_fall_from - off[1] > 30.0f) landed = true;
        place(off);
        if (landed) {
            m_collapse = COLLAPSE_DOWN;
            if (const assets::skl_file* k = find_skill(crash_skill)) fire_sound_keys(*k, 0.0, 1.0, c);
        }
    }

    void update(double now) {
        if (m_skill) pose_from_skill(now);
        if (!m_moving) return;
        const double f = m_motion.fraction(now);
        float off[3], rot[9];
        if (m_rotating) {
            rotation_now(float(m_motion.distance * f), rot);
            for (int k = 0; k < 3; ++k) off[k] = m_base[k];
            pose(rot, off);
        } else {
            for (int k = 0; k < 3; ++k) off[k] = m_base[k] + float(m_motion.dir[k] * f);
            if (m_turned) pose(m_rot, off);
            else apply(off);
        }
        if (!m_motion.done(now)) return;
        if (m_rotating) for (int i = 0; i < 9; ++i) m_rot[i] = rot[i];
        else for (int k = 0; k < 3; ++k) m_base[k] = off[k];
        m_moving = false;
        // The next step of a chain; after the last, "loop" starts it over.
        if (m_step + 1 < m_chain.size()) start_step(m_step + 1, m_motion.start + m_motion.total);
        else if (m_loop && !m_chain.empty()) start_step(0, m_motion.start + m_motion.total);
    }

private:
    // "move,<axis>,<d>,<t>,<accel>,<decel>[,Loop|Crush|HalfCrush]" and
    // "rotate,<axis>,<degrees>,<t>,<accel>,<decel>", chained in one message
    // ("move,...,move,...,loop" runs them in turn, over and over). "STOP"
    // first drops whatever it was doing. The chaining and rotation are ours;
    // the TranslateMotion maths is the original's (0x47ABDC).
    struct motion_step { k2::engine::translate_motion m; bool rotate; };
    std::vector<motion_step> m_chain;
    size_t                   m_step = 0;
    bool                     m_loop = false, m_rotating = false, m_turned = false;
    float                    m_rot[9];      // rotation reached by "rotate" so far
    float                    m_axis[3] = { 0.0f, 1.0f, 0.0f };

    static bool is_motion(const std::string& msg) {
        const std::string w = msg.substr(0, msg.find(','));
        return msg.find(',') != std::string::npos &&
               (equals_nocase(w, "move") || equals_nocase(w, "rotate") || equals_nocase(w, "stop"));
    }
    bool motions(const std::string& msg) {
        std::vector<std::string> f;
        size_t st = 0;
        for (;;) {
            const size_t c = msg.find(',', st);
            f.push_back(msg.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        std::vector<motion_step> chain;
        bool loop = false;
        for (size_t i = 0; i < f.size();) {
            if (equals_nocase(f[i], "stop")) {      // freeze where it is
                settle();
                ++i;
                continue;
            }
            if (equals_nocase(f[i], "loop")) { loop = true; ++i; continue; }
            if ((equals_nocase(f[i], "move") || equals_nocase(f[i], "rotate")) && i + 5 < f.size()) {
                motion_step ms;
                ms.rotate = equals_nocase(f[i], "rotate");
                if (!ms.m.parse(f[i + 1] + "," + f[i + 2] + "," + f[i + 3] + "," + f[i + 4] + "," + f[i + 5]))
                    return false;
                chain.push_back(ms);
                i += 6;
                continue;
            }
            ++i;                                    // Crush, HalfCrush: no effect on the path
        }
        if (chain.empty()) return true;
        m_chain = chain;
        m_loop = loop;
        start_step(0, m_world.now());
        return true;
    }
public:
    // Saves: where it has got to - offset, turn, the skill it last played
    // (a door's "openslow"), a collapse that has happened.
    void save_line(std::string& out) {
        if (m_moving && m_loop) return;                 // a looping Mover restarts by itself
        // Where a motion under way has got to, without stopping it.
        float base[3] = { m_base[0], m_base[1], m_base[2] }, rot[9];
        for (int k = 0; k < 9; ++k) rot[k] = m_rot[k];
        if (m_moving) {
            const double f = m_motion.fraction(m_world.now());
            if (m_rotating) rotation_now(float(m_motion.distance * f), rot);
            else for (int k = 0; k < 3; ++k) base[k] += float(m_motion.dir[k] * f);
        }
        if (base[0] == 0.0f && base[1] == 0.0f && base[2] == 0.0f && !m_turned && !m_moving &&
            m_collapse == COLLAPSE_NONE && m_skill_name.empty()) return;     // as it started
        char b[512];
        std::snprintf(b, sizeof(b), "%.4f %.4f %.4f %d %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %d %s",
                      base[0], base[1], base[2], int(m_turned), rot[0], rot[1], rot[2], rot[3],
                      rot[4], rot[5], rot[6], rot[7], rot[8], int(m_collapse),
                      m_skill_name.empty() ? "-" : m_skill_name.c_str());
        out = b;
    }
    void restore_line(const std::string& in) {
        float b[3], r[9];
        int turned = 0, collapse = 0;
        char skill[200] = "-";
        if (std::sscanf(in.c_str(), "%f %f %f %d %f %f %f %f %f %f %f %f %f %d %199s", &b[0], &b[1], &b[2], &turned,
                        &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7], &r[8], &collapse, skill) < 14) return;
        m_moving = false;
        m_chain.clear();
        for (int k = 0; k < 3; ++k) m_base[k] = b[k];
        for (int k = 0; k < 9; ++k) m_rot[k] = r[k];
        m_turned = turned != 0;
        if (m_turned) pose(m_rot, m_base); else apply(m_base);
        if (collapse == int(COLLAPSE_DOWN)) m_collapse = COLLAPSE_DOWN;
        if (std::strcmp(skill, "-") != 0) {
            if (const assets::skl_file* sk = find_skill(skill)) {
                m_skill = sk;
                m_skill_name = skill;
                m_skill_start = m_world.now() - clip_length_ms(*sk) / 1000.0 - 1.0;   // at its end
            }
        }
    }
private:
    std::string              m_skill_name;

    // Whatever motion is under way stops at its current point.
    void settle() {
        if (!m_moving) return;
        const double f = m_motion.fraction(m_world.now());
        if (m_rotating) { float r[9]; rotation_now(float(m_motion.distance * f), r); for (int i = 0; i < 9; ++i) m_rot[i] = r[i]; }
        else for (int k = 0; k < 3; ++k) m_base[k] += float(m_motion.dir[k] * f);
        m_moving = false;
        m_chain.clear();
        m_loop = false;
    }
    void start_step(size_t i, double at) {
        m_step = i;
        m_motion = m_chain[i].m;
        m_motion.begin(at);
        m_rotating = m_chain[i].rotate;
        if (m_rotating) {
            m_turned = true;
            const double d = m_motion.distance;
            for (int k = 0; k < 3; ++k) m_axis[k] = d != 0.0 ? float(m_motion.dir[k] / d) : 0.0f;
        }
        m_moving = true;
    }
    // m_rot turned a further `deg` degrees about m_axis (Rodrigues).
    void rotation_now(float deg, float* out) const {
        const float a = deg * 3.14159265f / 180.0f, c = std::cos(a), sn = std::sin(a), t = 1.0f - c;
        const float x = m_axis[0], y = m_axis[1], z = m_axis[2];
        const float r[9] = { t * x * x + c,     t * x * y - sn * z, t * x * z + sn * y,
                             t * x * y + sn * z, t * y * y + c,     t * y * z - sn * x,
                             t * x * z - sn * y, t * y * z + sn * x, t * z * z + c };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                out[i * 3 + j] = r[i * 3] * m_rot[j] + r[i * 3 + 1] * m_rot[3 + j] + r[i * 3 + 2] * m_rot[6 + j];
    }

    const assets::skl_file* find_skill(const std::string& name) {
        std::string key = actor + "/" + name;
        for (size_t i = 0; i < key.size(); ++i)
            if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');
        std::map<std::string, assets::skl_file*>::iterator it = m_sc.trap_skills.find(key);
        if (it != m_sc.trap_skills.end()) return it->second;
        const std::string slash = key.substr(0, key.find('/'));
        const std::string path = "actors/" + slash + "/skills/" + key.substr(key.find('/') + 1) + ".skl";
        size_t size = 0;
        uint8_t* d = plat::fs_read(path.c_str(), size);
        assets::skl_file* sk = 0;
        if (d) {
            sk = new assets::skl_file();
            if (!assets::skl_parse(d, size, *sk)) { delete sk; sk = 0; }
            plat::fs_free(d);
        }
        m_sc.trap_skills[key] = sk;     // remember misses too
        return sk;
    }

    // The node takes the pose of the first joint the skill keys, relative to
    // its rest position: rotation about the trap pivot, then translation.
    void pose_from_skill(double now) {
        const double ms = (now - m_skill_start) * 1000.0;
        const uint32_t t = ms <= 0.0 ? 0u : uint32_t(ms);
        for (size_t j = 0; j < m_skill->joints.size(); ++j) {
            const assets::skl_joint& jt = m_skill->joints[j];
            float pos[3], q[4];
            bool ok;
            if (!jt.keys.empty())       ok = assets::skl_sample_keys(jt.keys, t, pos, q);
            else if (jt.present && !jt.path.pos.empty()) {
                assets::skl_sample(jt.path, t, m_skill->interpolate != 0, pos, q);
                ok = true;
            } else continue;
            if (!ok) continue;
            k2::engine::transform r;
            transform_from_pos_quat(pos, q, r);
            float off[3];
            for (int k = 0; k < 3; ++k) off[k] = m_base[k] + pos[k];
            pose(r.rotate, off);
            return;
        }
    }

    // Turned by `rot` (row-major) about the pivot, then moved by `off`.
    void pose(const float* rot, const float* off) {
        m_sc.coll.set_node_transform(m_node, rot, pivot, off);
        if (m_item < 0) return;
        draw_item& di = m_sc.items[size_t(m_item)];
        // world = R (p - pivot) + pivot + off, as a column-major 4x4.
        float* m = di.model;
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr) m[c * 4 + rr] = rot[rr * 3 + c];
        m[3] = m[7] = m[11] = 0.0f;
        for (int rr = 0; rr < 3; ++rr) {
            const float rp = rot[rr * 3] * pivot[0] + rot[rr * 3 + 1] * pivot[1] + rot[rr * 3 + 2] * pivot[2];
            m[12 + rr] = pivot[rr] - rp + off[rr];
        }
        m[15] = 1.0f;
        di.has_model = true;
    }

    void apply(const float* off) {
        if (m_item >= 0)
            for (int k = 0; k < 3; ++k) m_sc.items[size_t(m_item)].offset[k] = off[k];
        m_sc.coll.set_node_offset(m_node, off);
    }

    scene&                        m_sc;
    int                           m_item;
    int32_t                       m_node;
    bool                          m_moving;
    float                         m_base[3];
    k2::engine::translate_motion  m_motion;
    const assets::skl_file*       m_skill;
    double                        m_skill_start;
    enum { COLLAPSE_NONE, COLLAPSE_WOBBLE, COLLAPSE_FALLING, COLLAPSE_DOWN } m_collapse = COLLAPSE_NONE;
    double                        m_collapse_until = 0.0;
    float                         m_fall_v = 0.0f, m_fall_from = 0.0f;
};

// An actor as a script target, under the name of the node that spawned it
// ("prince" for the Prince). Understood:
//   snooze:t                         no player input for t seconds (the Prince)
//   SkillAt:skill[,placeholder[,REMOVE]]
//                                    play one of its skills once; with a
//                                    placeholder, from that node's position
//                                    and facing; REMOVE hides it afterwards
bool play_once(scene& sc, actor_inst& a, const std::string& name, double now, float yaw, bool hold);

class actor_target : public k2::engine::script_target {
public:
    actor_target(k2::engine::script_world& w, scene& sc, size_t actor)
        : script_target(sc.actors[actor].script_name), snooze_until(0.0), m_world(w), m_sc(sc),
          m_actor(actor) {}

    bool receive(const std::string& msg, k2::engine::script_target*) {
        if (msg.compare(0, 7, "snooze:") == 0) {
            snooze_until = m_world.now() + std::atof(msg.c_str() + 7);
            return true;
        }
        if (msg.compare(0, 8, "SkillAt:") == 0 || msg.compare(0, 8, "skillat:") == 0)
            return skill_at(msg.substr(8));
        if (msg.size() > 7 && equals_nocase(msg.substr(0, 7), "walkat:")) return walk_at(msg.substr(7));
        actor_inst& a = m_sc.actors[m_actor];
        if (msg.size() > 5 && equals_nocase(msg.substr(0, 5), "item:")) return item(a, msg.substr(5));
        if (is_prince(a)) {
            if (msg.size() > 5 && equals_nocase(msg.substr(0, 5), "hurt:")) {
                a.pending_hurt += std::atoi(msg.c_str() + 5);
                return true;
            }
            if (msg.size() > 7 && equals_nocase(msg.substr(0, 7), "weapon:")) return weapon(a, msg.substr(7));
            if (msg.size() >= 5 && equals_nocase(msg.substr(0, 5), "kill:")) {
                a.pending_kill = msg.substr(5);
                if (a.pending_kill.empty()) a.pending_kill = "DeathGeneric_G";
                return true;
            }
            return false;
        }
        if (a.is_enemy) {
            // EnemyStart's table (0x46CB7C): Start sets flag 0x1000; Remove
            // takes him out of the level.
            if (equals_nocase(msg, "start")) { a.start_flag = true; return true; }
            if (equals_nocase(msg, "remove")) { a.hidden = true; return true; }
            return false;
        }
        if (!a.is_fixture) return false;
        std::string l = msg;
        for (size_t i = 0; i < l.size(); ++i) l[i] = char(std::tolower(static_cast<unsigned char>(l[i])));
        if (l == "enable:t" || l == "visible:t") { a.hidden = false; return true; }
        if (l == "collide:t" || l == "collide:f") { a.collides = l[8] == 't'; return true; }
        if (l == "visible:f") { a.hidden = true; return true; }
        if (l.compare(0, 7, "enable:") == 0 || l.compare(0, 6, "state:") == 0) return true;
        // Anything else names one of its skills. One that interrupts the
        // idles goes back to them; a prop (no idles) keeps its last frame.
        if (play_once(m_sc, a, msg, m_world.now(), a.home_yaw, a.ambient.empty())) {
            if (a.ambient.empty()) a.held_clip = msg;
            return true;
        }
        std::printf("fixture %s: no skill %s\n", name().c_str(), msg.c_str());
        return false;
    }

    double snooze_until;

private:
    bool walk_at(const std::string& args) {
        actor_inst& a = m_sc.actors[m_actor];
        std::vector<std::string> f;
        size_t st = 0;
        for (;;) {
            const size_t c = args.find(',', st);
            f.push_back(args.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        if (f.size() < 2) return false;
        std::string key = f[1];
        for (size_t i = 0; i < key.size(); ++i) key[i] = char(std::tolower(static_cast<unsigned char>(key[i])));
        std::map<std::string, scene::named_point>::const_iterator p = m_sc.points.find(key);
        if (p == m_sc.points.end()) { std::printf("WalkAt: no placeholder %s\n", f[1].c_str()); return false; }
        for (int k = 0; k < 3; ++k) a.walk_to[k] = p->second.pos[k];
        a.walk_run = equals_nocase(f[0], "run");
        a.walk_remove = f.size() > 2 && equals_nocase(f[2], "remove");
        a.walking = true;
        a.scripted = 0;
        a.hidden = false;
        return true;
    }

    // "weapon:+sword", "weapon:+bow", "weapon:+arrow,<type>,<count>",
    // "weapon:-all": what the Prince carries (prison1 takes it all away,
    // the secret guy's door gives the sword back).
    bool weapon(actor_inst& a, const std::string& arg) {
        if (arg.empty()) return false;
        const bool add = arg[0] != '-';
        const std::string w = arg[0] == '+' || arg[0] == '-' ? arg.substr(1) : arg;
        const std::string kind = w.substr(0, w.find(','));
        if (equals_nocase(kind, "all")) {
            if (!add) { a.armed = false; a.has_bow = false; a.arrows = 0; a.stance = 0; }
            return true;
        }
        if (equals_nocase(kind, "sword")) { a.armed = add; if (!add) a.stance = 0; return true; }
        if (equals_nocase(kind, "bow")) { a.has_bow = add; return true; }
        if (equals_nocase(kind, "arrow")) {
            const size_t c2 = w.rfind(',');
            const int n = c2 == std::string::npos || c2 == w.find(',') ? 1 : std::atoi(w.c_str() + c2 + 1);
            a.arrows = add ? a.arrows + n : std::max(0, a.arrows - n);
            return true;
        }
        return false;
    }

    // "Item:<object>,<on|off>[,L|R]": take a world object into a hand, or put
    // it down where the hand is.
    bool item(actor_inst& a, const std::string& args) {
        std::vector<std::string> f;
        size_t st = 0;
        for (;;) {
            const size_t c = args.find(',', st);
            f.push_back(args.substr(st, c == std::string::npos ? std::string::npos : c - st));
            if (c == std::string::npos) break;
            st = c + 1;
        }
        const bool on = f.size() < 2 || equals_nocase(f[1], "on");
        const char* hand = f.size() > 2 && !f[2].empty() && (f[2][0] == 'L' || f[2][0] == 'l') ? "LHand" : "RHand";
        if (on) {
            a.carried.clear();
            for (size_t i = 0; i < m_sc.items.size(); ++i)
                if (m_sc.items[i].owner < 0 && equals_nocase(m_sc.item_names[i], f[0].c_str())) {
                    a.carried.push_back(i);
                    m_sc.items[i].hidden = true;
                }
            a.carry_joint = -1;
            for (size_t j = 0; j < a.joint_names.size(); ++j)
                if (a.joint_names[j] == hand) { a.carry_joint = int(j); break; }
            if (a.carried.empty()) find_item_mesh(f[0], a.carried);
            if (a.carried.empty()) std::printf("Item: can't find item %s\n", f[0].c_str());
            return true;
        }
        // Down where the hand holds it: the mesh moved from its centre to there.
        if (a.carry_joint >= 0) {
            k2::engine::transform hw;
            k2::engine::transform_compose(a.node->world(), a.cur[size_t(a.carry_joint)], hw);
            for (size_t k = 0; k < a.carried.size(); ++k) {
                draw_item& di = m_sc.items[a.carried[k]];
                mat4_identity(di.model);
                for (int c = 0; c < 3; ++c) di.model[12 + c] = hw.translate[c] - di.center[c];
                di.has_model = true;
                di.hidden = false;
            }
        }
        a.carried.clear();
        return true;
    }

    // An item not in the world (the bracelet): its mesh is a node of that
    // name in some .k2a (Rooms/prisonfix/bracelet.k2a). Loaded on first use,
    // kept out of the world passes.
    void find_item_mesh(const std::string& name, std::vector<size_t>& out) {
        std::map<std::string, std::vector<size_t> >::const_iterator hit = m_sc.weapons.find("item:" + name);
        if (hit != m_sc.weapons.end()) { out = hit->second; return; }
        struct remember {
            scene& sc; const std::string key; std::vector<size_t>& v;
            ~remember() { sc.weapons[key] = v; }
        } keep = { m_sc, "item:" + name, out };
        std::vector<std::string> k2as;
        plat::fs_list_paths_by_extension(".k2a", k2as);
        std::string needle = "*NODE_NAME \"" + name + "\"";
        for (size_t i = 0; i < k2as.size() && out.empty(); ++i) {
            if (k2as[i].find("rooms/") != std::string::npos &&
                k2as[i].find("prison") != std::string::npos &&
                k2as[i].find("bracelet") == std::string::npos) continue;   // the big rooms: skip
            size_t size = 0;
            uint8_t* d = plat::fs_read(k2as[i].c_str(), size);
            if (!d) continue;
            const bool has = std::string(reinterpret_cast<const char*>(d), size).find(needle) != std::string::npos;
            plat::fs_free(d);
            if (!has) continue;
            const size_t before = m_sc.items.size();
            const float mn[3] = { m_sc.mn[0], m_sc.mn[1], m_sc.mn[2] }, mx[3] = { m_sc.mx[0], m_sc.mx[1], m_sc.mx[2] };
            append_scene(k2as[i].c_str(), m_sc, false);
            for (int k = 0; k < 3; ++k) { m_sc.mn[k] = mn[k]; m_sc.mx[k] = mx[k]; }
            for (size_t k = before; k < m_sc.items.size(); ++k) {
                m_sc.items[k].hidden = true;
                if (equals_nocase(m_sc.item_names[k], name.c_str())) out.push_back(k);
            }
        }
    }

    bool skill_at(const std::string& args) {
        actor_inst& a = m_sc.actors[m_actor];
        std::vector<std::string> f;
        size_t start = 0;
        for (;;) {
            const size_t c = args.find(',', start);
            f.push_back(args.substr(start, c == std::string::npos ? std::string::npos : c - start));
            if (c == std::string::npos) break;
            start = c + 1;
        }
        if (f.empty() || f[0].empty() || a.skill_dir.empty()) {
            std::printf("SkillAt: %s has no skills (not skinned?)\n", name().c_str());
            return false;
        }

        std::string path = a.skill_dir + "skills/" + f[0] + ".skl";
        for (size_t i = 0; i < path.size(); ++i)
            if (path[i] >= 'A' && path[i] <= 'Z') path[i] = char(path[i] - 'A' + 'a');
        std::map<std::string, assets::skl_file*>::iterator it = m_sc.trap_skills.find(path);
        assets::skl_file* sk = 0;
        if (it != m_sc.trap_skills.end()) sk = it->second;
        else {
            size_t size = 0;
            uint8_t* d = plat::fs_read(path.c_str(), size);
            if (d) {
                sk = new assets::skl_file();
                if (!assets::skl_parse(d, size, *sk) || sk->chunk_tag != "SklS") { delete sk; sk = 0; }
                plat::fs_free(d);
            }
            m_sc.trap_skills[path] = sk;
        }
        if (!sk) return false;

        k2::engine::transform& lt = a.node->local;
        for (int k = 0; k < 3; ++k) a.scripted_origin[k] = lt.translate[k];
        a.scripted_yaw = std::atan2(lt.rotate[2], lt.rotate[8]);
        if (f.size() > 1 && !f[1].empty()) {
            std::string key = f[1];
            for (size_t i = 0; i < key.size(); ++i)
                if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');
            std::map<std::string, scene::named_point>::const_iterator p = m_sc.points.find(key);
            if (p != m_sc.points.end()) {
                for (int k = 0; k < 3; ++k) a.scripted_origin[k] = p->second.pos[k];
                a.scripted_yaw = p->second.yaw;
            }
        }
        a.remove_after = f.size() > 2 && equals_nocase(f[2], "remove");
        // DEAD: a scripted death (the prison's squished guard); he stays down.
        const bool dead = f.size() > 2 && equals_nocase(f[2], "dead");
        a.hold_last = dead;
        a.posed_hold = false;
        if (dead) a.health = 0;
        a.scripted = sk;
        a.scripted_start = m_world.now();
        a.clip_ms = 0.0;               // the play head, for its sound keys
        a.hidden = false;
        return true;
    }

    k2::engine::script_world& m_world;
    scene&                    m_sc;
    size_t                    m_actor;
};

// One frame of a scripted skill: pose every joint from the clip, held at its
// last frame, and carry the actor along the clip's root motion (all three
// axes: being thrown is not a walk). Returns true while it plays.
bool update_scripted(actor_inst& a, double now) {
    if (!a.scripted) return false;
    const assets::skl_file& s = *a.scripted;
    const uint32_t len = clip_length_ms(s);
    const double el = (now - a.scripted_start) * 1000.0;
    const bool done = el >= double(len);
    const uint32_t t = done ? len : uint32_t(el < 0 ? 0 : el);
    fire_sound_keys(s, a.clip_ms, double(t), a.node->local.translate);
    a.clip_ms = double(t);

    const size_t n = std::min(a.joint_local_cur.size(), s.joints.size());
    for (size_t j = 0; j < n; ++j) {
        float pos[3], q[4];
        if (!skill_joint(s, j, t, pos, q)) { a.joint_local_cur[j] = a.joint_local_bind[j]; continue; }
        if (j == 0)
            for (int k = 0; k < 3; ++k) pos[k] = a.joint_local_bind[0].translate[k];
        transform_from_pos_quat(pos, q, a.joint_local_cur[j]);
    }
    pose_actor(a);

    float p0[3], pt[3], q[4];
    p0[0] = p0[1] = p0[2] = pt[0] = pt[1] = pt[2] = 0.0f;
    if (!skill_joint(s, 0, 0, p0, q) || !skill_joint(s, 0, t, pt, q))
        p0[0] = p0[1] = p0[2] = pt[0] = pt[1] = pt[2] = 0.0f;
    const float dx = pt[0] - p0[0], dy = pt[1] - p0[1], dz = pt[2] - p0[2];
    const float c = std::cos(a.scripted_yaw), sn = std::sin(a.scripted_yaw);
    k2::engine::transform& lt = a.node->local;
    lt.set_yaw(a.scripted_yaw);
    lt.translate[0] = a.scripted_origin[0] + c * dx + sn * dz;
    lt.translate[1] = a.scripted_origin[1] + dy;
    lt.translate[2] = a.scripted_origin[2] - sn * dx + c * dz;

    if (done) {
        a.scripted = 0;
        if (a.remove_after) a.hidden = true;
        if (a.hold_last) { a.posed_hold = true; return true; }
        set_clip(a, LOCO_BASE);
        a.clip_ms = 0.0;
    }
    return true;
}

// One of an actor's skills by name ("CombatAttackLeft_G1"), from its own
// Skills folder, cached in the scene. Null when it has no such skill.
const assets::skl_file* find_actor_skill(scene& sc, const actor_inst& a, const std::string& name) {
    if (a.skill_dir.empty() || name.empty()) return 0;
    std::string path = a.skill_dir + "skills/" + name + ".skl";
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] >= 'A' && path[i] <= 'Z') path[i] = char(path[i] - 'A' + 'a');
    std::map<std::string, assets::skl_file*>::iterator it = sc.trap_skills.find(path);
    if (it != sc.trap_skills.end()) return it->second;
    size_t size = 0;
    uint8_t* d = plat::fs_read(path.c_str(), size);
    assets::skl_file* sk = 0;
    if (d) {
        sk = new assets::skl_file();
        if (!assets::skl_parse(d, size, *sk) ||
            (sk->chunk_tag != "SklS" && sk->chunk_tag != "SklK")) { delete sk; sk = 0; }
        plat::fs_free(d);
    }
    sc.trap_skills[path] = sk;
    return sk;
}

// Plays a skill once where the actor stands, facing `yaw`, root motion
// included. `hold` keeps the last frame afterwards (death).
bool play_once(scene& sc, actor_inst& a, const std::string& name, double now, float yaw, bool hold) {
    const assets::skl_file* sk = find_actor_skill(sc, a, name);
    if (!sk) return false;
    for (int k = 0; k < 3; ++k) a.scripted_origin[k] = a.node->local.translate[k];
    a.scripted_yaw = yaw;
    a.scripted = sk;
    a.scripted_start = now;
    a.clip_ms = 0.0;
    a.hold_last = hold;
    a.posed_hold = false;
    a.remove_after = false;
    return true;
}

// "camera": CameraLogic's script messages (parser at 0x40855D).
//
//   D...                    back to the default (chase) camera: DD, DDX, Default
//   tripod,(@Node)          fixed at a camera node, looking at its .Target
//   tripod,(x,y,z)(x,y,z)   fixed at a point, looking at a point (engine space)
//   follow,(dx,dy,dz)(lx,ly,lz)  ride along at an offset from the Prince, looking
//                           along a direction (our reading; not traced further)
//   follow,Actor,(..)(..)   the same about a named actor ("follow,pg2Yell,...")
//   security,[Actor,](pos)(off)  fixed at pos, turning to keep the Prince (or
//                           the actor) in view, aimed at him plus `off`
//   look,(off)(at)          rides at an offset from the Prince, looking at a
//                           fixed point
//   #d                      as D
// security and look are our reading of the data; the class is not traced.
//
// Trailing letters ("X") are flags the original keeps (0x409975, |5); the
// transition they select is not modelled - every change is a cut.
class camera_target : public k2::engine::script_target {
public:
    enum mode_t { DEFAULT, TRIPOD, FOLLOW, SECURITY, LOOK };

    explicit camera_target(const scene& sc) : script_target("camera"), mode(DEFAULT), m_sc(sc) {
        for (int k = 0; k < 3; ++k) pos[k] = look[k] = 0.0f;
    }

    bool receive(const std::string& msg, k2::engine::script_target*) {
        if (msg.empty()) return false;
        if (msg[0] == 'D' || msg[0] == 'd' || equals_nocase(msg, "#d")) { mode = DEFAULT; return true; }
        const size_t comma = msg.find(',');
        if (comma == std::string::npos) return false;
        std::string verb = msg.substr(0, comma);
        for (size_t i = 0; i < verb.size(); ++i)
            if (verb[i] >= 'A' && verb[i] <= 'Z') verb[i] = char(verb[i] - 'A' + 'a');
        std::string args = msg.substr(comma + 1);
        subject.clear();
        if ((verb.compare(0, 3, "fol") == 0 || verb.compare(0, 3, "sec") == 0) && !args.empty() && args[0] != '(') {
            const size_t c2 = args.find(',');
            if (c2 == std::string::npos) return false;
            subject = args.substr(0, c2);
            args = args.substr(c2 + 1);
        }
        float a[3], b[3];
        bool have_b = false;
        size_t at = 0;
        if (!paren(args, at, a, &have_b, b)) return false;
        float b2[3];
        bool dummy;
        if (paren(args, at, b2, &dummy, 0)) { for (int k = 0; k < 3; ++k) b[k] = b2[k]; have_b = true; }
        if (verb.compare(0, 3, "tri") == 0) {
            if (!have_b) return false;
            for (int k = 0; k < 3; ++k) { pos[k] = a[k]; look[k] = b[k]; }
            mode = TRIPOD;
            return true;
        }
        if (verb.compare(0, 3, "fol") == 0 || verb.compare(0, 3, "sec") == 0 || verb.compare(0, 3, "loo") == 0) {
            if (!have_b) return false;
            for (int k = 0; k < 3; ++k) { pos[k] = a[k]; look[k] = b[k]; }
            mode = verb[0] == 'f' ? FOLLOW : verb[0] == 's' ? SECURITY : LOOK;
            return true;
        }
        return false;
    }

    mode_t mode;
    float  pos[3];      // TRIPOD: where; FOLLOW: offset from the Prince
    float  look[3];     // TRIPOD: at what; FOLLOW: what it looks at, also from the Prince
    std::string subject;    // FOLLOW: whom, when not the Prince

private:
    // One "( ... )" group from `at`: "@Node" or "x,y,z". A node that is a
    // target camera also yields its target in `tgt`.
    bool paren(const std::string& s, size_t& at, float* out, bool* has_tgt, float* tgt) {
        if (at >= s.size() || s[at] != '(') return false;
        const size_t close = s.find(')', at);
        if (close == std::string::npos) return false;
        const std::string body = s.substr(at + 1, close - at - 1);
        at = close + 1;
        if (!body.empty() && body[0] == '@') {
            std::string key = body.substr(1);
            for (size_t i = 0; i < key.size(); ++i)
                if (key[i] >= 'A' && key[i] <= 'Z') key[i] = char(key[i] - 'A' + 'a');
            std::map<std::string, scene::named_point>::const_iterator it = m_sc.points.find(key);
            if (it == m_sc.points.end()) return false;
            for (int k = 0; k < 3; ++k) out[k] = it->second.pos[k];
            if (it->second.has_target && tgt) {
                for (int k = 0; k < 3; ++k) tgt[k] = it->second.target[k];
                *has_tgt = true;
            }
            return true;
        }
        return std::sscanf(body.c_str(), "%f,%f,%f", &out[0], &out[1], &out[2]) == 3;
    }

    const scene& m_sc;
};

// --- sound ----------------------------------------------------------------------
//
// One mixer for the game. Sounds are named as the scripts and skills name them
// ("L01showerrun001", or with a ".wav" the files on disk do not have) and are
// loaded from Audio/sfx on first use.
k2::engine::mixer* g_mixer = 0;

void audio_callback(float* out, int32_t frames, void*) {
    std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
    if (g_mixer) g_mixer->mix(out, frames);
    static FILE* tap = std::getenv("K2_AUDIO_TAP") ? std::fopen(std::getenv("K2_AUDIO_TAP"), "wb") : 0;
    if (tap) { std::fwrite(out, sizeof(float), size_t(frames) * 2, tap); std::fflush(tap); }
}

// A bare name ("L01waterdropN") stands for its numbered variants on disk
// (L01waterdropn001.sfx): 1000 of the 1255 effects carry a three-digit
// variant, up to eight per name. One that exists is picked at random each
// time; how the original chooses is not traced.
std::string resolve_sound_file(const std::string& name) {
    char real[4096];
    if (plat::fs_resolve_basename((name + ".sfx").c_str(), real, sizeof(real))) return name;
    std::vector<std::string> found;
    for (int v = 1; v <= 20; ++v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%03d", v);
        if (plat::fs_resolve_basename((name + buf + ".sfx").c_str(), real, sizeof(real)))
            found.push_back(name + buf);
    }
    if (found.empty()) return std::string();
    return found[size_t(std::rand()) % found.size()];
}

bool ensure_sound(const std::string& raw) {
    if (!g_mixer || raw.empty()) return false;
    std::string name = raw;
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    if (g_mixer->has_sound(name)) return true;
    const std::string file = resolve_sound_file(name);
    if (file.empty()) return false;
    size_t size = 0;
    uint8_t* d = plat::fs_read_basename((file + ".sfx").c_str(), size);
    if (!d) return false;
    assets::sfx_file sf;
    const bool ok = assets::sfx_parse(d, size, sf);
    plat::fs_free(d);
    if (ok) g_mixer->add_sound(name, sf);
    return ok;
}

int32_t play_sound(const std::string& raw, const float* pos, float radius, bool loop) {
    if (!ensure_sound(raw)) return 0;
    std::string name = raw;
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return g_mixer->play(name, pos, radius, loop);
}

// "audio": a non-positional sound by name ("audio,L01waterdropN").
class audio_target : public k2::engine::script_target {
public:
    audio_target() : script_target("audio") {}
    bool receive(const std::string& msg, k2::engine::script_target*) {
        return play_sound(msg, 0, 0.0f, false) != 0 || !g_mixer;
    }
};

// A SpecialEffect trap: argv1 a .nif played at the trap while it is visible
// and on ("on" / "off", "Visible:T/F"; the Moon Temple beams, the prison
// showers). argv2 is not yet understood. The effect itself is run by
// fx_sync_traps.
class effect_trap : public k2::engine::trap_object {
public:
    effect_trap(k2::engine::script_world& w, const std::string& name, const k2::engine::trap_header& h,
                const std::vector<std::string>& argv, const float* bbox, float yaw)
        : trap_object(w, name, h), on(true), fx_id(-1), yaw(yaw) {
        file = argv.size() > 1 ? argv[1] : std::string();
        const size_t cut = file.find_last_of("/\\");
        if (cut != std::string::npos) file = file.substr(cut + 1);
        for (int k = 0; k < 3; ++k) pos[k] = (bbox[k] + bbox[k + 3]) * 0.5f;
        pos[1] = bbox[1];
    }
    bool handle(const std::string& msg, k2::engine::script_target* sender) {
        if (msg == "on" || msg == "On") { on = true; return true; }
        if (msg == "off" || msg == "Off") { on = false; return true; }
        return trap_object::handle(msg, sender);
    }
    std::string file;
    bool  on;
    int   fx_id;
    float pos[3], yaw;
};

// An AudioMessage trap (registry key "Audio"). Traps.txt: argv1 use the trap's
// radius, argv2 positional, argv3 sound. "play" plays argv3, "silence" stops
// it, any other message names a sound to play here. Looping sounds named in
// argv3 are ambiences and start with the level (ours: not traced).
class audio_trap : public k2::engine::trap_object {
public:
    audio_trap(k2::engine::script_world& w, const std::string& name,
               const k2::engine::trap_header& h, const std::vector<std::string>& argv,
               const float* bbox)
        : trap_object(w, name, h), m_voice(0) {
        bool use_radius = false;
        m_positional = false;
        if (argv.size() > 1) k2::engine::parse_trap_bool(argv[1], use_radius);
        if (argv.size() > 2) k2::engine::parse_trap_bool(argv[2], m_positional);
        // Ambiences that use their trap's radius (RiverAudio, TortureAmbients,
        // SolitarySinger, Showeraudio) are heard around the trap, not
        // everywhere: placed at it.
        if (use_radius) m_positional = true;
        m_sound = argv.size() > 3 ? argv[3] : std::string();
        for (int k = 0; k < 3; ++k) m_pos[k] = (bbox[k] + bbox[k + 3]) * 0.5f;
        const float dx = bbox[3] - bbox[0], dz = bbox[5] - bbox[2];
        m_radius = use_radius ? std::max(4.0f, 0.5f * std::sqrt(dx * dx + dz * dz)) * 2.0f : 16.0f;
    }

    void start_ambient() {
        // Only positional loops are ambiences (TortureAmbients). A non-
        // positional one (piss_sound) waits for its script's "play"; started
        // with the level it played everywhere, all the time.
        if (!m_positional || m_sound.empty() || !ensure_sound(m_sound)) return;
        m_voice = play_sound(m_sound, m_positional ? m_pos : 0, m_radius, true);
    }

    bool handle(const std::string& msg, k2::engine::script_target* sender) {
        if (msg == "play" || msg == "Play") {
            if (g_mixer && m_voice) g_mixer->stop(m_voice);
            m_voice = play_sound(m_sound, m_positional ? m_pos : 0, m_radius, true);
            return true;
        }
        if (msg == "silence" || msg == "SILENCE") {
            if (g_mixer && m_voice) g_mixer->stop(m_voice);
            m_voice = 0;
            return true;
        }
        if (trap_object::handle(msg, sender)) return true;
        if (msg.find(':') != std::string::npos || msg.find(',') != std::string::npos) return false;
        if (g_mixer && m_voice) g_mixer->stop(m_voice);
        // A sound named by a script plays once, here (the floor plate's
        // "ShowerAud01,L01showerrun001"); looping it kept the shower
        // running everywhere for the rest of the level.
        m_voice = play_sound(msg, m_pos, m_radius, false);
        return m_voice != 0 || !g_mixer;
    }

private:
    std::string m_sound;
    bool        m_positional;
    float       m_pos[3], m_radius;
    int32_t     m_voice;
};

// Length of an actor's skill in seconds, e.g. TRAP_PressurePlate's "On";
// 0 when it cannot be read.
double actor_skill_seconds(const std::string& actor, const char* skill) {
    std::string path = "actors/" + actor + "/skills/" + skill + ".skl";
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] >= 'A' && path[i] <= 'Z') path[i] = char(path[i] - 'A' + 'a');
    size_t size = 0;
    uint8_t* d = plat::fs_read(path.c_str(), size);
    if (!d) return 0.0;
    assets::skl_file sk;
    const bool ok = assets::skl_parse(d, size, sk);
    plat::fs_free(d);
    return ok ? double(sk.duration_ms) / 1000.0 : 0.0;
}

// The loading screen, when there is a front end: set by main, called through
// loading_progress as a level loads. At most twenty updates a second; the
// last one (1.0) always shows.
void (*g_loading_fn)(float) = 0;

void loading_progress(float p) {
    if (!g_loading_fn) return;
    static double last = -1.0;
    const double now = plat::time_seconds();
    static const bool every = std::getenv("K2_MENU_SHOT") != 0;     // headless checks see each step
    if (!every && p < 1.0f && last >= 0.0 && now - last < 0.05) return;
    last = now;
    g_loading_fn(p);
}

// .K2A files parsed ahead on the worker cores (prefetch_rooms), by path.
std::map<std::string, assets::k2a_file> g_parsed_k2a;

bool read_k2a(const char* path, assets::k2a_file& f) {
    size_t size = 0;
    uint8_t* data = std::strchr(path, '/') || std::strchr(path, '\\') ? plat::fs_read(path, size) : 0;
    if (!data) data = plat::fs_read_basename(path, size);
    if (!data) return false;
    std::string err;
    const bool ok = assets::k2a_parse(data, size, f, &err);
    plat::fs_free(data);
    return ok;
}

// Loading a level is mostly parsing text and decoding images, each file and
// each image on its own: they are spread over the cores first, and the
// scene is then built from the results on the main thread (which owns the
// GPU and the scene).
void prefetch_rooms(const std::vector<std::string>& paths) {
    struct job {
        const std::vector<std::string>* paths;
        std::vector<assets::k2a_file> files;
        std::vector<char> ok;
        std::vector<std::string> images;
        std::vector<assets::image> decoded;
        static void parse(size_t i, void* u) {
            job& j = *static_cast<job*>(u);
            j.ok[i] = read_k2a((*j.paths)[i].c_str(), j.files[i]) ? 1 : 0;
        }
        static void decode(size_t i, void* u) {
            job& j = *static_cast<job*>(u);
            size_t size = 0;
            uint8_t* data = plat::fs_read_basename(j.images[i].c_str(), size);
            if (!data) return;
            std::string err;
            if (!assets::image_load(data, size, j.decoded[i], &err)) j.decoded[i] = assets::image();
            plat::fs_free(data);
        }
    };
    const double t0 = plat::time_seconds();
    job j;
    j.paths = &paths;
    j.files.resize(paths.size());
    j.ok.assign(paths.size(), 0);
    k2::jobs::parallel_for(paths.size(), &job::parse, &j);
    const double t_parsed = plat::time_seconds();
    std::map<std::string, size_t> seen;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!j.ok[i]) continue;
        for (size_t m = 0; m < j.files[i].materials.size(); ++m) {
            const std::string& b = j.files[i].materials[m].bitmap;
            const std::string k = image_key(b);
            if (b.empty() || k.size() < 4 || k.compare(k.size() - 4, 4, ".ifl") == 0) continue;
            if (g_decoded_images.count(k) || g_texture_cache.count(k) || seen.count(k)) continue;
            seen[k] = j.images.size();
            j.images.push_back(b);
        }
        std::swap(g_parsed_k2a[paths[i]], j.files[i]);
    }
    j.decoded.resize(j.images.size());
    k2::jobs::parallel_for(j.images.size(), &job::decode, &j);
    for (size_t i = 0; i < j.images.size(); ++i)
        if (j.decoded[i].valid()) std::swap(g_decoded_images[image_key(j.images[i])], j.decoded[i]);
    std::printf("prefetch: %zu files parsed in %.2f s, %zu images decoded in %.2f s, on %d workers\n",
                paths.size(), t_parsed - t0, j.images.size(), plat::time_seconds() - t_parsed,
                k2::jobs::worker_count());
}

bool append_scene(const char* path, scene& sc, bool collide = true) {
    assets::k2a_file f;
    std::map<std::string, assets::k2a_file>::iterator pre = g_parsed_k2a.find(path);
    if (pre != g_parsed_k2a.end()) {
        std::swap(f, pre->second);
        g_parsed_k2a.erase(pre);
    } else if (!read_k2a(path, f)) {
        return false;
    }

    // Texture ids are per-file, so remember where this file's block starts.
    const size_t tex_base = sc.textures.size();
    const size_t first_item_of_file = sc.items.size();
    int decoded = 0, failed = 0;
    std::vector<bool> mat_transparent(f.materials.size(), false);
    std::vector<bool> mat_cutout(f.materials.size(), false);
    const size_t mat_base = sc.materials.size();
    for (size_t i = 0; i < f.materials.size(); ++i) {
        const assets::k2a_material& m = f.materials[i];
        const bool chroma = m.diffuse_alpha == assets::k2a_material::DIFFUSE_ALPHA_CHROMA;
        mat_tex mt = load_material_texture(m.bitmap, chroma, decoded, failed);
        sc.textures.push_back(mt.frames.empty() ? gfx::INVALID_TEXTURE : mt.frames[0]);
        // Blended: an opacity map, or a diffuse map whose own alpha is used
        // and actually has some (the flames). Keyed: alpha-tested in the
        // opaque pass, so it still writes depth.
        mat_transparent[i] = m.alpha == assets::K2A_ALPHA_OPACITY ||
            (m.diffuse_alpha == assets::k2a_material::DIFFUSE_ALPHA_OPACITY && mt.has_alpha);
        mat_cutout[i] = chroma && mt.has_alpha;
        sc.materials.push_back(mt);
    }

    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_trap) continue;
        if (n.trap.name == "PrinceStart" || n.trap.name == "EnemyStart") {
            sc.spawn_points.push_back(n.trap);
            sc.spawn_points.back().node = n.name;
        }
        if (n.trap.name == "Fixture" && n.trap.argv.size() > 1 && !n.trap.argv[1].empty()) {
            sc.fixture_points.push_back(n.trap);
            sc.fixture_points.back().node = n.name;
        }
        if (n.trap.name == "Rope" && collide) {
            sc.rope_points.push_back(n.trap);
            sc.rope_points.back().node = n.name;
        }
        if (n.trap.name == "Specials" && collide && n.trap.argv.size() > 1 && !n.trap.argv[1].empty()) {
            sc.special_points.push_back(n.trap);
            sc.special_points.back().node = n.name;
        }
        if (n.trap.name == "Pickup" && collide) {
            scene::pickup_def pd;
            pd.name = n.name;
            pd.argv = n.trap.argv;
            pd.trap = n.trap;
            pd.trap.node = n.name;
            for (int k = 0; k < 3; ++k) pd.pos[k] = (n.trap.bbox[k] + n.trap.bbox[k + 3]) * 0.5f;
            if (std::getenv("K2_PICKUPS"))
                std::printf("pickup %s at (%.2f %.2f %.2f) %s\n", n.name.c_str(), pd.pos[0], pd.pos[1], pd.pos[2],
                            pd.argv.size() > 1 ? pd.argv[1].c_str() : "");
            sc.pickups.push_back(pd);
        }
    }

    // Node transforms are in Max's frame (z up); vertices are not. Measured on
    // IntroAnim: TM_POS (20.34, -1.53, 0) for a volume centred at engine
    // (20.34, y, 1.53), so engine = (x, z, -y).
    if (collide) {
        for (size_t i = 0; i < f.nodes.size(); ++i) {
            const assets::k2a_node& n = f.nodes[i];
            std::string key = n.name;
            for (size_t c = 0; c < key.size(); ++c)
                if (key[c] >= 'A' && key[c] <= 'Z') key[c] = char(key[c] - 'A' + 'a');
            scene::named_point np;
            np.pos[0] = n.pos[0]; np.pos[1] = n.pos[2]; np.pos[2] = -n.pos[1];
            np.has_target = n.has_target;
            np.target[0] = n.target_pos[0]; np.target[1] = n.target_pos[2];
            np.target[2] = -n.target_pos[1];
            // Facing, for placeholders: a rotation about Max's z is one about
            // engine y. ASE writes TM_ROTANGLE in the opposite sense to the
            // axis (the usual ASE quirk), so yaw = -angle * axis.z. Checked on
            // IntroNISPH: the throw then carries the Prince into the cell.
            const float az = n.rot_axis[2];
            np.yaw = (std::fabs(az) > 0.9f) ? (az > 0 ? -n.rot_angle : n.rot_angle) : 0.0f;
            sc.points[key] = np;
        }
    }

    size_t total_v = 0;
    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_mesh) continue;

        std::vector<assets::k2a_vertex> vb;
        std::vector<uint16_t> ib;
        assets::k2a_build_mesh(n, vb, ib);
        if (vb.empty() || ib.empty()) continue;

        // Trigger volumes are not room geometry: keep them out of the bounds so
        // a large Area trap cannot throw off camera framing. See the note below
        // on why a trap block alone is not enough to classify a node.
        const bool is_volume = n.has_trap && n.material_ref < 0;
        // Every trap's argv[0] carries Visible and Collideable (0x454A41).
        // An invisible trap is not drawn; it joins the trigger-volume view.
        k2::engine::trap_header hdr;
        if (n.has_trap && !n.trap.argv.empty())
            hdr = k2::engine::parse_trap_header(n.trap.argv[0]);
        const bool hidden = n.has_trap && !hdr.visible;
        if (!is_volume && !hidden) {
            for (size_t v = 0; v < vb.size(); ++v) {
                const float p[3] = {vb[v].x, vb[v].y, vb[v].z};
                for (int a = 0; a < 3; ++a) {
                    if (p[a] < sc.mn[a]) sc.mn[a] = p[a];
                    if (p[a] > sc.mx[a]) sc.mx[a] = p[a];
                }
            }
            sc.empty = false;
        }
        total_v += vb.size();

        // Centroid of the built mesh, used to sort the transparent pass.
        float cmn[3] = {1e30f,1e30f,1e30f}, cmx[3] = {-1e30f,-1e30f,-1e30f};
        for (size_t v = 0; v < vb.size(); ++v) {
            const float pv[3] = {vb[v].x, vb[v].y, vb[v].z};
            for (int a = 0; a < 3; ++a) {
                if (pv[a] < cmn[a]) cmn[a] = pv[a];
                if (pv[a] > cmx[a]) cmx[a] = pv[a];
            }
        }

        // k2a_vertex and gfx::vertex have identical layout by construction.
        draw_item it;
        for (int a = 0; a < 3; ++a) it.center[a] = (cmn[a] + cmx[a]) * 0.5f;
        it.radius = 0.5f * std::sqrt((cmx[0] - cmn[0]) * (cmx[0] - cmn[0]) +
                                     (cmx[1] - cmn[1]) * (cmx[1] - cmn[1]) +
                                     (cmx[2] - cmn[2]) * (cmx[2] - cmn[2]));
        it.mesh = gfx::create_mesh(
            reinterpret_cast<const gfx::vertex*>(&vb[0]), int32_t(vb.size()),
            &ib[0], int32_t(ib.size()));
        const size_t tex_index = tex_base + size_t(n.material_ref);
        it.tex = (n.material_ref >= 0 && tex_index < sc.textures.size())
                 ? sc.textures[tex_index]
                 : gfx::INVALID_TEXTURE;
        it.transparent = (n.material_ref >= 0 &&
                          size_t(n.material_ref) < mat_transparent.size() &&
                          mat_transparent[size_t(n.material_ref)]);
        // A trap block does not by itself mean "invisible trigger". Nodes like
        // TRAP_Showerdoor carry a trap *and* real textured geometry — that door
        // must still be drawn normally. The pure trigger volumes are the ones
        // with no material at all; those are what rendered as black boxes,
        // because they fall through to their (black) vertex colours.
        it.is_trap   = is_volume;
        it.hidden    = hidden;
        it.offset[0] = it.offset[1] = it.offset[2] = 0.0f;
        it.has_model = false;
        it.material  = (n.material_ref >= 0 && size_t(n.material_ref) < f.materials.size())
                       ? int(mat_base + size_t(n.material_ref)) : -1;
        it.cutout    = n.material_ref >= 0 && size_t(n.material_ref) < mat_cutout.size() &&
                       mat_cutout[size_t(n.material_ref)];
        // Doors, Forces and Movers have their own colour-key switch (Traps.txt:
        // Door argv2 "Transparent", ForceTrap argv3 / Mover argv1 "ChromaKey").
        // Prison gates use it: their texture is 64% black bars-background.
        if (n.has_trap && it.material >= 0 && !it.cutout) {
            size_t slot = 0;
            if      (n.trap.name == "Door")  slot = 2;
            else if (n.trap.name == "Force") slot = 3;
            else if (n.trap.name == "Mover") slot = 1;
            bool key = false;
            if (slot && n.trap.argv.size() > slot) k2::engine::parse_trap_bool(n.trap.argv[slot], key);
            if (key) {
                mat_tex mt = load_material_texture(f.materials[size_t(n.material_ref)].bitmap, true,
                                                   decoded, failed);
                if (mt.has_alpha) {
                    sc.materials.push_back(mt);
                    it.material = int(sc.materials.size() - 1);
                    it.tex = mt.frames[0];
                    it.cutout = true;
                }
            }
        }
        it.trap_type = -1;
        it.owner     = -1;
        it.joint     = -1;
        it.tint[0] = it.tint[1] = it.tint[2] = 1.0f;
        if (it.is_trap) {
            it.tex = gfx::INVALID_TEXTURE;
            size_t k = 0;
            for (; k < sc.trap_types.size(); ++k)
                if (sc.trap_types[k] == n.trap.name) break;
            if (k == sc.trap_types.size()) sc.trap_types.push_back(n.trap.name);
            it.trap_type = int(k);
        }
        sc.items.push_back(it);
        sc.item_names.push_back(n.name);
        if (!collide && g_part_collision && !it.is_trap && !it.transparent && !ib.empty()) {
            const int32_t pn = sc.coll.add_node();
            sc.coll.set_node_enabled(pn, false);
            sc.coll.add_mesh(&vb[0].x, sizeof(assets::k2a_vertex) / sizeof(float), vb.size(), &ib[0], ib.size(), pn);
            sc.items.back().coll_node = pn;
        }

        // What the floor and wall picks (mask 6) see, per 0x449546 and
        // 0x474525: room geometry minus its alpha leaves, plus any Generic
        // trap that is Collideable, visible or not. For other trap types with
        // real geometry (doors, crates) the same flag is applied; how the
        // original picks those is not traced. Trigger volumes never collide.
        bool solid;
        if (!n.has_trap)                 solid = !it.transparent;
        else if (n.trap.name == "Generic") solid = hdr.collideable;
        else if (n.trap.name == "Water")   solid = false;   // swum in, not stood on
        else if (n.trap.name == "Abyss")   solid = false;   // fallen through
        else                             solid = hdr.collideable && !is_volume;
        // Trap geometry gets its own collision node, so scripts can switch it
        // on and off and move it; it is added even when it starts non-solid.
        int32_t node = -1;
        if (collide && n.has_trap && !is_volume) {
            node = sc.coll.add_node();
            sc.coll.set_node_enabled(node, solid);
        }
        if (collide && (solid || node >= 0))
            sc.coll.add_mesh(&vb[0].x, sizeof(assets::k2a_vertex) / sizeof(float),
                             vb.size(), &ib[0], ib.size(), node);

        if (collide && n.has_trap && std::getenv("K2_SCRIPTAUDIT"))
            for (size_t a = 1; a < n.trap.argv.size(); ++a)
                if (n.trap.argv[a].find(',') != std::string::npos) g_audit_scripts.push_back(n.trap.argv[a]);
        // Every trap node becomes a script target under its node name.
        if (collide && n.has_trap) {
            k2::engine::trap_object* t;
            k2::engine::trap_volume vol;
            vol.build(&vb[0].x, sizeof(assets::k2a_vertex) / sizeof(float), vb.size(),
                      &ib[0], ib.size());
            if (n.trap.name == "Area") {
                k2::engine::area_trap* a = new k2::engine::area_trap(
                    sc.script, n.name, hdr, n.trap.argv, vol);
                sc.areas.push_back(a);
                t = a;
            } else if (n.trap.name == "Action") {
                k2::engine::action_trap* a = new k2::engine::action_trap(
                    sc.script, n.name, hdr, n.trap.argv, vol);
                a->set_center(n.trap.center);
                sc.actions.push_back(a);
                t = a;
            } else if (n.trap.name == "Messenger") {
                t = new k2::engine::messenger_trap(sc.script, n.name, hdr, n.trap.argv);
            } else if (n.trap.name == "Audio") {
                t = new audio_trap(sc.script, n.name, hdr, n.trap.argv, n.trap.bbox);
            } else if (n.trap.name == "SpecialEffect") {
                effect_trap* e = new effect_trap(sc.script, n.name, hdr, n.trap.argv, n.trap.bbox,
                                                 n.trap.pivot_angle);
                g_effect_traps.push_back(e);
                t = e;
            } else if (n.trap.name == "PressurePlate") {
                const std::string actor = (n.trap.argv.size() > 1 && !n.trap.argv[1].empty())
                                          ? n.trap.argv[1] : std::string("TRAP_PressurePlate");
                k2::engine::pressure_plate* pp = new k2::engine::pressure_plate(
                    sc.script, n.name, hdr, n.trap.argv, vol,
                    actor_skill_seconds(actor, "On"), actor_skill_seconds(actor, "Off"));
                sc.plates.push_back(pp);
                t = pp;
            } else {
                scene_trap* st = new scene_trap(sc.script, n.name, hdr, sc,
                                   int(sc.items.size()) - 1, node, n.trap.name, n.trap.bbox);
                for (int k = 0; k < 3; ++k) st->pivot[k] = n.trap.pivot[k];
                st->pivot_angle = n.trap.pivot_angle * (n.trap.pivot_axis[1] < 0.0f ? -1.0f : 1.0f);
                if (n.trap.name == "CrateTarget" && n.trap.argv.size() > 1) st->message = n.trap.argv[1];
                if (n.trap.name == "ArrowTarget" && n.trap.argv.size() > 2) st->message = n.trap.argv[2];
                // Movers (Traps.txt: argv2 "Starting Motions"): lifts and
                // turning platforms already under way when the level opens.
                if (n.trap.name == "Mover" && n.trap.argv.size() > 2 && !n.trap.argv[2].empty())
                    st->handle(n.trap.argv[2], 0);
                if (n.trap.name == "Force" && n.trap.argv.size() > 2 && equals_nocase(n.trap.argv[1], "Volume") &&
                    n.trap.argv[2].compare(0, 8, "current,") == 0) {
                    k2::engine::translate_motion m;     // the axis words as "move" reads them
                    const std::string rest = n.trap.argv[2].substr(8);
                    if (m.parse(rest + ",1,0,0")) {
                        const float c = std::cos(st->pivot_angle), sn = std::sin(st->pivot_angle);
                        // about +y: x' = c x + s z, z' = -s x + c z
                        st->current[0] = c * m.dir[0] + sn * m.dir[2];
                        st->current[1] = m.dir[1];
                        st->current[2] = -sn * m.dir[0] + c * m.dir[2];
                        st->has_current = true;
                    }
                }
                if (n.trap.name == "Door")
                    st->actor = (n.trap.argv.size() > 1 && !n.trap.argv[1].empty())
                                ? n.trap.argv[1] : std::string("TRAP_Door");
                if (n.trap.name == "CollapsingFloor") {
                    const std::vector<std::string>& a = n.trap.argv;
                    struct pick { static std::string arg(const std::vector<std::string>& v, size_t i, const char* def) {
                        return i < v.size() && !v[i].empty() && v[i] != "*" ? v[i] : std::string(def); } };
                    st->actor        = pick::arg(a, 1, "TRAP_CollapsingFloor");
                    st->wobble_skill = pick::arg(a, 2, "Wobble");
                    st->fall_skill   = pick::arg(a, 3, "Fall");
                    st->crash_skill  = pick::arg(a, 4, "Crash");
                }
                t = st;
            }
            if (n.trap.name == "Water") {
                scene::water_area wa;
                wa.trap = sc.traps.size();
                wa.mn[0] = wa.mn[1] = 1e30f; wa.mx[0] = wa.mx[1] = -1e30f;
                for (size_t q = 0; q + 2 < ib.size(); q += 3)
                    for (int c = 0; c < 3; ++c) {
                        const assets::k2a_vertex& v = vb[ib[q + size_t(c)]];
                        wa.tris.push_back(v.x); wa.tris.push_back(v.z);
                        wa.mn[0] = std::min(wa.mn[0], v.x); wa.mx[0] = std::max(wa.mx[0], v.x);
                        wa.mn[1] = std::min(wa.mn[1], v.z); wa.mx[1] = std::max(wa.mx[1], v.z);
                    }
                sc.waters.push_back(wa);
            }
            if (n.trap.name == "Abyss") {
                scene::abyss_area ab;
                ab.trap = t;
                ab.vol = vol;
                sc.abysses.push_back(ab);
            }
            sc.traps.push_back(t);
            // A spawn trap's messages are its actor's (EnemyStart__ctor knows
            // "SkillAt"); the actor answers under this name instead.
            if (n.trap.name != "EnemyStart" && n.trap.name != "PrinceStart" &&
                !(n.trap.name == "Fixture" && n.trap.argv.size() > 1 && !n.trap.argv[1].empty()))
                sc.script.add(t);
        }
    }
    if (collide) sc.coll.build();

    {
        size_t op = 0, tr = 0, tv = 0;
        for (size_t i = first_item_of_file; i < sc.items.size(); ++i) {
            if (sc.items[i].is_trap) { ++tv; continue; }
            if (sc.items[i].transparent) ++tr; else ++op;
        }
        std::printf("  %-28s %3zu meshes %6zu verts  tex %3d ok %2d fail  traps %d"
                    "  [opaque %zu transparent %zu vol %zu, mats %zu]\n",
                    path, f.nodes.size(), total_v, decoded, failed, f.trap_count(),
                    op, tr, tv, f.materials.size());
    }
    return true;
}

// Spread hues over the types present and stamp each volume with its colour.
// Called after every append, so a multi-room level is coloured as one set.
void assign_trap_colors(scene& sc) {
    const size_t n = sc.trap_types.size();
    if (!n) return;
    for (size_t i = 0; i < sc.items.size(); ++i) {
        draw_item& it = sc.items[i];
        if (!it.is_trap || it.trap_type < 0) continue;
        hue_color(float(it.trap_type) / float(n), it.tint);
    }
}

void print_trap_legend(const scene& sc) {
    if (sc.trap_types.empty()) return;
    std::printf("trigger volumes: %zu types (T toggles)\n", sc.trap_types.size());
    for (size_t i = 0; i < sc.trap_types.size(); ++i) {
        float c[3];
        hue_color(float(i) / float(sc.trap_types.size()), c);
        std::printf("  %-18s rgb(%3d,%3d,%3d)\n", sc.trap_types[i].c_str(),
                    int(c[0] * 255.0f), int(c[1] * 255.0f), int(c[2] * 255.0f));
    }
    std::fflush(stdout);   // the window is about to open; do not sit in the buffer
}

// --- actors -----------------------------------------------------------------
//
// Ports the New Game path from docs/reference/actor-spawn.md:
//   [Bodies] -> body_table, then PrinceStart / EnemyStart -> spawn_actor.

// Reads every [Bodies] entry into the table. Mirrors LevelFileParser's
// BeginBodies + ParseBodyLine pair: the index is authoritative, not the order.
void build_body_table(const assets::lvl_file& lv, k2::engine::body_table& bodies) {
    bodies.reset();
    for (size_t i = 0; i < lv.bodies.size(); ++i) {
        const assets::lvl_body& b = lv.bodies[i];

        size_t size = 0;
        uint8_t* data = plat::fs_read(b.path.c_str(), size);
        if (!data) data = plat::fs_read_basename(b.path.c_str(), size);

        assets::master_file m;
        bool ok = false;
        if (data) {
            std::string err;
            ok = assets::master_parse(data, size, m, &err);
            plat::fs_free(data);
        }
        if (!bodies.set(b.index, b.path, m, ok))
            std::printf("  illegal body index %d (%s)\n", b.index, b.path.c_str());
        else if (!ok)
            std::printf("  body %d unreadable: %s\n", b.index, b.path.c_str());
    }
    std::printf("  bodies: %zu declared, %zu loaded\n",
                lv.bodies.size(), bodies.loaded_count());
}

// A standalone room has no [Bodies] list — that lives in the .lvl. To make
// `--room` show its actors anyway, index every master.dat in the install by its
// -n name. Spawn points resolve by name regardless of which level they came
// from, so this is the same lookup the original does, over a wider set.
void build_body_table_from_install(k2::engine::body_table& bodies) {
    bodies.reset();
    std::vector<std::string> paths;
    plat::fs_list_paths_by_extension(".dat", paths);
    std::sort(paths.begin(), paths.end());

    int32_t next = 0;
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string& p = paths[i];
        // Only master.dat files describe bodies; enemy.dat and camera.dat do not.
        const size_t slash = p.find_last_of("/\\");
        const std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
        if (base != "master.dat") continue;

        size_t size = 0;
        uint8_t* data = plat::fs_read(p.c_str(), size);
        if (!data) continue;
        assets::master_file m;
        std::string err;
        const bool ok = assets::master_parse(data, size, m, &err);
        plat::fs_free(data);
        if (!ok || m.name.empty()) continue;

        // Keep the first of any duplicate name, as a linear search would.
        if (bodies.find_by_name(m.name) >= 0) continue;
        bodies.set(next++, p, m, true);
    }
    std::printf("  body index: %zu master.dat entries\n", bodies.loaded_count());
}

// Loads one body's geometry and records the item range it occupies, so the
// actor can be transformed independently of the room. Actor geometry is kept
// out of the scene bounds for the same reason trigger volumes are: it is not
// the room.
bool append_actor_geometry(const std::string& geometry, scene& sc,
                           size_t& first, size_t& count, float* local_mn, float* local_mx) {
    const size_t before = sc.items.size();
    const float  mn[3] = {sc.mn[0], sc.mn[1], sc.mn[2]};
    const float  mx[3] = {sc.mx[0], sc.mx[1], sc.mx[2]};

    for (int a = 0; a < 3; ++a) { sc.mn[a] =  1e30f; sc.mx[a] = -1e30f; }
    if (!append_scene(geometry.c_str(), sc, false)) {
        for (int a = 0; a < 3; ++a) { sc.mn[a] = mn[a]; sc.mx[a] = mx[a]; }
        return false;
    }
    // The body's own extent, before it is placed. Reported because .K2A node
    // transforms are not applied when building meshes: if a body's parts were
    // positioned by node transform rather than baked into the vertices, they
    // would all pile up at the origin and this would show it.
    for (int a = 0; a < 3; ++a) { local_mn[a] = sc.mn[a]; local_mx[a] = sc.mx[a]; }
    for (int a = 0; a < 3; ++a) { sc.mn[a] = mn[a]; sc.mx[a] = mx[a]; }
    first = before;
    count = sc.items.size() - before;
    return count > 0;
}

// PrinceStart and EnemyStart both take the body *name* in argv[0] (Traps.txt),
// and the original resolves it by walking the body table comparing names. The
// placement comes from the trap's centre and pivot angle.
// The Prince has two possible spawn sources and levels use different ones:
//
//   * `.lvl` [Prince] inside a [Region]: "<body> x,y,z,heading". Active in
//     prisonfix, the game's opening level, where it is commented `;cell`.
//   * a PrinceStart trap in room geometry, resolved by body name. Used by
//     Cistern, whose [Prince] line is commented out.
//
// An earlier reading of this project concluded [Prince] was a dead authoring
// leftover, on the strength of Cistern alone. That was wrong: it is live in the
// first level. Try it first, fall back to the traps.
//
// `heading` is in **degrees** here - prisonfix writes 83.25, which is nonsense
// as radians. The trap route's pivot_angle is radians.
size_t spawn_prince_from_level(const assets::lvl_file& lv, scene& sc,
                               k2::engine::body_table& bodies,
                               std::vector<k2::engine::actor_spawn>& spawns,
                               int region);

void spawn_fixtures(scene& sc);

void spawn_level_actors(scene& sc, k2::engine::body_table& bodies,
                        std::vector<k2::engine::actor_spawn>& spawns) {
    for (size_t i = 0; i < sc.spawn_points.size(); ++i) {
        loading_progress(0.7f + 0.2f * float(i) / float(sc.spawn_points.size()));
        const assets::k2a_trap& t = sc.spawn_points[i];
        const bool is_enemy = (t.name == "EnemyStart");

        const std::string want = t.argv.empty() ? std::string() : t.argv[0];
        int32_t body = want.empty() ? -1 : bodies.find_by_name(want);
        if (body < 0) {
            std::printf("  can't find body %s (%s)\n",
                        want.empty() ? "<none>" : want.c_str(), t.name.c_str());
            continue;
        }

        std::string err;
        // The trap's pivot - its node's position - is where he stands. The
        // box can be much larger than the body: an EnemyStart's volume is
        // the ground he covers (P4G1's spans 18 units, its centre 7 from his
        // bed), and its centre is not him.
        const float* at = t.pivot;
        const int32_t id = k2::engine::spawn_actor(
            bodies, spawns, is_enemy, body,
            at[0], at[1], at[2], t.pivot_angle,
            is_enemy ? want : std::string("Prince"), &err);
        if (id < 0) { std::printf("  spawn refused (%s): %s\n", want.c_str(), err.c_str()); continue; }

        // Attach the geometry. This stands in for AvatarBody__CreateAvatar,
        // which builds an AnimationNode and hangs the body parts off it; here
        // the whole body is one .K2A, which is what the shipped data provides.
        const k2::engine::avatar_body* b = bodies.at(body);
        k2::engine::animation_node* an = new k2::engine::animation_node();
        an->name = spawns[size_t(id)].name;
        an->body = body;
        an->is_enemy = is_enemy;
        an->local.set_yaw(t.pivot_angle);
        an->local.translate[0] = at[0];
        an->local.translate[1] = at[1];
        an->local.translate[2] = at[2];
        sc.root->add_child(an);

        actor_inst inst;
        inst.name       = spawns[size_t(id)].name;
        inst.node       = an;
        inst.is_enemy   = is_enemy;
        inst.first_item = 0;
        inst.item_count = 0;
        float lmn[3] = {0,0,0}, lmx[3] = {0,0,0};
        if (!append_actor_geometry(b->master.geometry, sc,
                                   inst.first_item, inst.item_count, lmn, lmx)) {
            std::printf("  actor %s: no geometry (%s)\n",
                        inst.name.c_str(), b->master.geometry.c_str());
        }
        const int owner = int(sc.actors.size());
        for (size_t k = 0; k < inst.item_count; ++k)
            sc.items[inst.first_item + k].owner = owner;
        inst.script_name = is_enemy ? t.node : std::string("prince");
        bind_actor_skeleton(b->master, inst, sc);
        bind_actor_weapon(b->master, inst, sc, -1);
        inst.weapon_shown = is_enemy;           // guards hold theirs; the Prince draws his
        {   // the bowmen's bodies: PG3Archer, A2ARCHER, D4Archer
            std::string bn = b->name;
            for (size_t i = 0; i < bn.size(); ++i) bn[i] = char(std::tolower(static_cast<unsigned char>(bn[i])));
            if (is_enemy && bn.find("archer") != std::string::npos) inst.archer = true;
        }
        sc.actors.push_back(inst);

        sc.root->update_world();
        std::printf("  spawned %-14s body %d (%s) at (%.1f, %.1f, %.1f) yaw %.2f  %zu meshes"
                    "  local x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
                    inst.name.c_str(), body, b->name.c_str(),
                    an->local.translate[0], an->local.translate[1],
                    an->local.translate[2], t.pivot_angle, inst.item_count,
                    lmn[0], lmx[0], lmn[1], lmx[1], lmn[2], lmx[2]);
    }
    spawn_fixtures(sc);
    loading_progress(1.0f);
    std::printf("textures: %zu unique, %.1f MB\n", g_texture_cache.size(), g_texture_bytes / 1048576.0);
}

// Fixture traps: "<skill>,<visible>,<collide>,<?>" "<actor>" "<ambient,...>".
// The actor is a directory under Actors with its own .act and .K2A and no
// master.dat (Prisoner3, rat, Lever01, FIX_Crane), placed at the trap and
// answering scripts under the trap's node name. Its messages are mostly its
// own skill names: "Prisoner3,LookUp", "P4FIX_Crane01,lower4m".
bool ground_under(const k2::engine::collision_world& w, const float* p, float& y);

// An actor directory's body placed at a trap: Actors/<who>/<who>.act and the
// .K2A beside it, stood on the floor found under the trap. Returns its index
// in sc.actors, or -1. Fixtures and pickups both come through here.
int spawn_prop(scene& sc, const assets::k2a_trap& t, const std::string& who,
               const std::string& script_name) {
    static std::vector<std::string> k2as;
    const std::string dir = actor_dir_for(who + ".act");
    if (dir.empty()) { std::printf("  prop %s: no actor %s\n", t.node.c_str(), who.c_str()); return -1; }
    std::string geo;
    {
        // <dir>/<who>.k2a, the usual case, without listing the install.
        std::string lw = who;
        for (size_t i = 0; i < lw.size(); ++i) lw[i] = char(std::tolower(static_cast<unsigned char>(lw[i])));
        char real[1024];
        if (plat::fs_probe((dir + lw + ".k2a").c_str(), real, sizeof(real))) geo = dir + lw + ".k2a";
    }
    if (geo.empty() && k2as.empty()) plat::fs_list_paths_by_extension(".k2a", k2as);
    for (size_t k = 0; geo.empty() && k < k2as.size(); ++k)
        if (k2as[k].size() > dir.size() && k2as[k].compare(0, dir.size(), dir) == 0 &&
            k2as[k].find('/', dir.size()) == std::string::npos) { geo = k2as[k]; break; }
    if (geo.empty()) { std::printf("  prop %s: %s has no .k2a\n", t.node.c_str(), who.c_str()); return -1; }
    {

        assets::master_file m;
        m.name = who;
        m.geometry = geo;
        m.actor = who;

        k2::engine::animation_node* an = new k2::engine::animation_node();
        an->name = t.node;
        an->is_enemy = false;
        an->local.set_yaw(t.pivot_angle);
        for (int k = 0; k < 3; ++k) an->local.translate[k] = t.center[k];
        sc.root->add_child(an);

        actor_inst inst;
        inst.name = t.node;
        inst.node = an;
        inst.is_fixture = true;
        float lmn[3], lmx[3];
        if (!append_actor_geometry(geo, sc, inst.first_item, inst.item_count, lmn, lmx)) {
            std::printf("  prop %s: %s did not load\n", t.node.c_str(), geo.c_str());
            return -1;
        }
        float gy;
        // Probed from the box's floor: some (the Mopers) sit in pits under
        // a grate that a probe from the centre would land on.
        const float base[3] = { t.center[0], t.bbox[1] + 0.2f, t.center[2] };
        if (ground_under(sc.coll, base, gy) || ground_under(sc.coll, t.center, gy))
            an->local.translate[1] = gy;
        const int owner = int(sc.actors.size());
        for (size_t k = 0; k < inst.item_count; ++k) sc.items[inst.first_item + k].owner = owner;
        inst.script_name = script_name;
        bind_actor_skeleton(m, inst, sc);
        if (inst.skill_dir.empty()) inst.skill_dir = dir;
        for (int k = 0; k < 3; ++k) inst.home[k] = an->local.translate[k];
        inst.home_yaw = t.pivot_angle;
        sc.actors.push_back(inst);
        sc.root->update_world();
        return int(sc.actors.size()) - 1;
    }
}

void spawn_fixtures(scene& sc) {
    for (size_t i = 0; i < sc.fixture_points.size(); ++i) {
        loading_progress(0.9f + 0.1f * float(i) / float(sc.fixture_points.size()));
        const assets::k2a_trap& t = sc.fixture_points[i];
        const std::string& who = t.argv[1];
        g_part_collision = true;
        const int idx = spawn_prop(sc, t, who, t.node);
        g_part_collision = false;
        if (idx < 0) continue;
        actor_inst& inst = sc.actors[size_t(idx)];

        std::vector<std::string> f0;
        {
            const std::string& a0 = t.argv[0];
            size_t st = 0;
            for (;;) {
                const size_t c = a0.find(',', st);
                f0.push_back(a0.substr(st, c == std::string::npos ? std::string::npos : c - st));
                if (c == std::string::npos) break;
                st = c + 1;
            }
        }
        if (f0.size() > 1 && (f0[1] == "false" || f0[1] == "F" || f0[1] == "f")) inst.hidden = true;
        if (t.argv.size() > 2) {
            const std::string& a2 = t.argv[2];
            size_t st = 0;
            for (;;) {
                const size_t c = a2.find(',', st);
                const std::string w = a2.substr(st, c == std::string::npos ? std::string::npos : c - st);
                if (!w.empty()) inst.ambient.push_back(w);
                if (c == std::string::npos) break;
                st = c + 1;
            }
        }
        actor_inst& placed = inst;
        placed.collides = !(f0.size() > 3 && (f0[3] == "false" || f0[3] == "F" || f0[3] == "f")) &&
                          placed.ambient.empty() &&
                          !(placed.loco_ok.size() > size_t(LOCO_BASE) && placed.loco_ok[LOCO_BASE]);   // people
        if (placed.collides) std::printf("  fixture %s is solid\n", t.node.c_str());
        // A named starting state ("Empty", "closed") is posed and held.
        if (!f0.empty() && !f0[0].empty() && find_actor_skill(sc, placed, f0[0]))
            play_once(sc, placed, f0[0], 0.0, placed.home_yaw, true);
        std::printf("  fixture %-20s %-16s at (%.1f, %.1f, %.1f) %zu meshes, %zu ambient%s\n",
                    t.node.c_str(), who.c_str(), placed.home[0], placed.home[1], placed.home[2],
                    placed.item_count, placed.ambient.size(), placed.hidden ? ", hidden" : "");
    }
    sc.coll.build();        // the props' parts joined the collision world
    // Ropes: TRAP_ROPE hung at the trap's node, which is the rope's bottom
    // end (argv "Type actor" is the default in every shipped level).
    for (size_t i = 0; i < sc.rope_points.size(); ++i) {
        const assets::k2a_trap& t = sc.rope_points[i];
        const int idx = spawn_prop(sc, t, "TRAP_ROPE", t.node + "_rope");
        if (idx < 0) continue;
        actor_inst& a = sc.actors[size_t(idx)];
        for (int k = 0; k < 3; ++k) a.node->local.translate[k] = t.pivot[k];
        a.home_yaw = t.pivot_angle;
        scene::rope r;
        r.actor = idx;
        for (int k = 0; k < 3; ++k) r.pivot[k] = t.pivot[k];
        for (size_t j = 0; j < a.joint_names.size(); ++j)
            if (a.joint_names[j] == "Attach") r.attach = int(j);
        play_once(sc, a, "RopeBase", 0.0, a.home_yaw, true);
        sc.ropes.push_back(r);
        std::printf("  rope %s at (%.1f, %.1f, %.1f)%s\n", t.node.c_str(), t.pivot[0], t.pivot[1], t.pivot[2],
                    r.attach < 0 ? ", no Attach joint" : "");
    }
    // Specials: the SPEC_ actor named by argv1 (Specials ctor, 0x4896B1:
    // "actors\\<name>\\<name>.k2a"), and the volumes in it the binary looks
    // up by name, "trigger" and "kill".
    for (size_t i = 0; i < sc.special_points.size(); ++i) {
        const assets::k2a_trap& t = sc.special_points[i];
        scene::special sp;
        sp.actor = spawn_prop(sc, t, t.argv[1], t.node);
        if (sp.actor < 0) continue;
        const std::string path = "actors\\" + t.argv[1] + "\\" + t.argv[1] + ".k2a";
        size_t size = 0;
        uint8_t* d = plat::fs_read(path.c_str(), size);
        if (d) {
            assets::k2a_file f;
            std::string err;
            if (assets::k2a_parse(d, size, f, &err))
                for (size_t k = 0; k < f.nodes.size(); ++k) {
                    if (!f.nodes[k].has_trap) continue;
                    std::string nm = f.nodes[k].trap.name;
                    for (size_t c = 0; c < nm.size(); ++c) nm[c] = char(std::tolower(static_cast<unsigned char>(nm[c])));
                    float* box = nm == "trigger" ? sp.trig : nm == "kill" ? sp.kill : 0;
                    if (!box) continue;
                    for (int c = 0; c < 6; ++c) box[c] = f.nodes[k].trap.bbox[c];
                    (nm == "trigger" ? sp.have_trig : sp.have_kill) = true;
                }
            plat::fs_free(d);
        }
        // How each kind kills him, by the Prince's Death* clips' names. The
        // pairing is ours; the binary's table is not traced.
        {
            std::string an = t.argv[1];
            for (size_t c = 0; c < an.size(); ++c) an[c] = char(std::tolower(static_cast<unsigned char>(an[c])));
            struct kind { const char* key; const char* clip; };
            static const kind kinds[] = {
                { "feetgrinder", "DeathFeetGrinders_G" }, { "lowblade", "DeathLowBlade_G" },
                { "cspike", "DeathCSpike_G" },            { "crawldrill", "DeathCrawl_G" },
                { "scythe", "DeathDecap_G" },             { "guillotine", "DeathDecap_G" },
                { "chopper", "DeathDecap_G" },            { "wallblade", "DeathDecap_G" },
                { "ceilingblade", "DeathDecap_G" },       { "slicer", "DeathDecap_G" },
                { "hammer", "DeathCrushing_M" },          { "grinder", "DeathCrushing_M" },
                { "djinni", "DeathCrushing_M" },          { 0, 0 } };
            sp.death = "DeathSpikes_G";
            for (int k = 0; kinds[k].key; ++k)
                if (an.find(kinds[k].key) != std::string::npos) { sp.death = kinds[k].clip; break; }
        }
        actor_inst& a = sc.actors[size_t(sp.actor)];
        if (find_actor_skill(sc, a, "StartPosition")) play_once(sc, a, "StartPosition", 0.0, a.home_yaw, true);
        sc.specials.push_back(sp);
    }
    if (!sc.specials.empty()) std::printf("  specials: %zu\n", sc.specials.size());
    // Pickups: a numbered potion is the TRAP_Potion actor; anything else
    // names its own (PrinceSword01, PICK_Jewel2, Key).
    for (size_t i = 0; i < sc.pickups.size(); ++i) {
        scene::pickup_def& pk = sc.pickups[i];
        if (pk.argv.size() < 2 || pk.argv[1].empty()) continue;
        const bool numbered = pk.argv[1].find_first_not_of("0123456789") == std::string::npos;
        pk.actor = spawn_prop(sc, pk.trap, numbered ? std::string("TRAP_Potion") : pk.argv[1], "");
    }
}

size_t spawn_prince_from_level(const assets::lvl_file& lv, scene& sc,
                               k2::engine::body_table& bodies,
                               std::vector<k2::engine::actor_spawn>& spawns,
                               int region) {
    size_t made = 0;
    for (size_t r = 0; r < lv.regions.size() && !made; ++r) {
        if (region >= 0 && int(r) != region) continue;
        const assets::lvl_region& reg = lv.regions[r];
        for (size_t i = 0; i < reg.prince_starts.size(); ++i) {
            const assets::lvl_prince_start& ps = reg.prince_starts[i];
            const k2::engine::avatar_body* b = bodies.at(ps.index);
            if (!b || !b->loaded) {
                std::printf("  [Prince] body %d not loaded\n", ps.index);
                continue;
            }
            const float yaw = ps.heading * 3.14159265f / 180.0f;
            std::string err;
            const int32_t id = k2::engine::spawn_actor(bodies, spawns, false, ps.index,
                                                       ps.x, ps.y, ps.z, yaw, "Prince", &err);
            if (id < 0) { std::printf("  [Prince] refused: %s\n", err.c_str()); continue; }

            k2::engine::animation_node* an = new k2::engine::animation_node();
            an->name = "Prince";
            an->body = ps.index;
            an->is_enemy = false;
            an->local.set_yaw(yaw);
            an->local.translate[0] = ps.x;
            an->local.translate[1] = ps.y;
            an->local.translate[2] = ps.z;
            sc.root->add_child(an);

            actor_inst inst;
            inst.name = "Prince";
            inst.script_name = "prince";
            inst.node = an;
            inst.is_enemy = false;
            inst.first_item = 0;
            inst.item_count = 0;
            float lmn[3] = {0,0,0}, lmx[3] = {0,0,0};
            append_actor_geometry(b->master.geometry, sc,
                                  inst.first_item, inst.item_count, lmn, lmx);
            const int owner = int(sc.actors.size());
            for (size_t k = 0; k < inst.item_count; ++k)
                sc.items[inst.first_item + k].owner = owner;
            bind_actor_skeleton(b->master, inst, sc);
            bind_actor_weapon(b->master, inst, sc, -1);
            sc.actors.push_back(inst);
            sc.root->update_world();

            std::printf("  [Prince] body %d (%s) at (%.1f, %.1f, %.1f) heading %.1f deg  %zu meshes\n",
                        ps.index, b->name.c_str(), ps.x, ps.y, ps.z, ps.heading, inst.item_count);
            ++made;
            break;   // one Prince
        }
    }
    return made;
}

bool load_scene(const char* path, scene& sc) {
    scene_reset(sc);
    const bool ok = append_scene(path, sc);
    assign_trap_colors(sc);
    if (ok && !sc.empty) {
        std::printf("  bounds x[%.1f, %.1f] y[%.1f, %.1f] z[%.1f, %.1f]\n",
                    sc.mn[0], sc.mx[0], sc.mn[1], sc.mx[1], sc.mn[2], sc.mx[2]);
    }
    return ok && !sc.empty;
}

// Headless sweep: decode every image the VFS knows about. The real check on
// the BMP/TGA decoders, since it needs no GL context.
int dump_images() {
    static const char* exts[] = { ".bmp", ".tga" };
    int ok = 0, bad = 0;
    std::vector<std::string> failures;

    for (size_t e = 0; e < 2; ++e) {
        std::vector<std::string> names;
        plat::fs_list_by_extension(exts[e], names);
        int ext_ok = 0, ext_bad = 0;
        for (size_t i = 0; i < names.size(); ++i) {
            size_t size = 0;
            uint8_t* data = plat::fs_read_basename(names[i].c_str(), size);
            if (!data) { ++bad; ++ext_bad; failures.push_back(names[i] + " (unreadable)"); continue; }

            assets::image img;
            std::string err;
            const bool good = assets::image_load(data, size, img, &err) && img.valid();
            plat::fs_free(data);

            if (good) { ++ok; ++ext_ok; }
            else {
                ++bad; ++ext_bad;
                if (failures.size() < 12) failures.push_back(names[i] + " (" + err + ")");
            }
        }
        std::printf("  %s: %d decoded, %d failed  (of %zu unique names)\n",
                    exts[e], ext_ok, ext_bad, names.size());
    }
    std::printf("images: %d decoded, %d failed\n", ok, bad);
    for (size_t i = 0; i < failures.size(); ++i)
        std::printf("   FAIL %s\n", failures[i].c_str());
    return bad == 0 ? 0 : 1;
}

// Loads every room of a .lvl into one scene. Room names carry no extension and
// live beside the .lvl, so try "<name>.k2a" by basename.
// `region` selects one [Region] to load, or -1 for all of them. The original
// streams a region at a time: prisonfix has two, with separate room lists *and*
// separate [Prince] entries, so loading both at once puts two starts and two
// sets of rooms in one scene.
bool load_level(const char* path, scene& sc, assets::lvl_file& lv, int region) {
    size_t size = 0;
    uint8_t* data = plat::fs_read(path, size);
    if (!data) { std::fprintf(stderr, "k2boy: cannot read '%s'\n", path); return false; }

    std::string err;
    const bool ok = assets::lvl_parse(data, size, lv, &err);
    plat::fs_free(data);
    if (!ok) { std::fprintf(stderr, "k2boy: lvl parse failed: %s\n", err.c_str()); return false; }

    std::printf("level '%s' (%s): %zu bodies, %zu regions, %zu rooms, %zu items\n",
                lv.name.c_str(), path, lv.bodies.size(), lv.regions.size(),
                lv.room_count(), lv.items.size());

    // Every region's rooms. The original streams one region at a time as
    // the Prince moves between them (LevelGroup's PrinceBrowser, through the
    // .nbr neighbour data; not traced), so the doors out of Parts1to3 led to
    // nothing. All of them are loaded instead; `region` still picks which
    // [Prince] start is used.
    (void)region;
    std::vector<std::string> files;
    for (size_t r = 0; r < lv.regions.size(); ++r)
        for (size_t i = 0; i < lv.regions[r].rooms.size(); ++i) {
            const std::string f = assets::lvl_room_filename(lv.regions[r].rooms[i]);
            if (std::find(files.begin(), files.end(), f) == files.end()) files.push_back(f);
        }
    int loaded = 0, missing = 0;
    loading_progress(0.05f);
    prefetch_rooms(files);
    loading_progress(0.3f);
    const int room_total = int(files.size());
    int room_index = 0;
    {
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string& file = files[i];
            if (append_scene(file.c_str(), sc)) ++loaded;
            else { ++missing; std::printf("  missing room %s\n", file.c_str()); }
            loading_progress(0.3f + 0.4f * float(++room_index) / float(room_total ? room_total : 1));
        }
    }
    std::printf("  rooms loaded %d, missing %d (all regions)\n", loaded, missing);
    return loaded > 0;
}

// Headless: parse a .lvl, report its sections, and check every room and body
// path resolves. No GL, so it works in CI.
int dump_level(const char* path) {
    size_t size = 0;
    uint8_t* data = plat::fs_read(path, size);
    if (!data) { std::fprintf(stderr, "k2boy: cannot read '%s'\n", path); return 1; }

    assets::lvl_file lv;
    std::string err;
    const bool ok = assets::lvl_parse(data, size, lv, &err);
    plat::fs_free(data);
    if (!ok) { std::fprintf(stderr, "k2boy: parse failed: %s\n", err.c_str()); return 1; }

    std::printf("%s\n  name '%s'\n  cmd  %s\n",
                path, lv.name.c_str(), lv.command_line.c_str());
    std::printf("  bodies %zu, regions %zu, rooms %zu, items %zu, fog %zu\n",
                lv.bodies.size(), lv.regions.size(), lv.room_count(),
                lv.items.size(), lv.fog.size());
    if (lv.has_sky_color)
        std::printf("  sky   %.0f,%.0f,%.0f\n",
                    lv.sky_color[0], lv.sky_color[1], lv.sky_color[2]);
    if (lv.has_clip_planes)
        std::printf("  clip  near %.2f far %.2f\n", lv.clip_near, lv.clip_far);

    char real[4096];
    int miss_body = 0, miss_room = 0, miss_item = 0;

    for (size_t i = 0; i < lv.bodies.size(); ++i)
        if (!plat::fs_resolve(lv.bodies[i].path.c_str(), real, sizeof(real)) &&
            !plat::fs_resolve_basename(lv.bodies[i].path.c_str(), real, sizeof(real))) {
            ++miss_body;
            std::printf("  MISSING body %s\n", lv.bodies[i].path.c_str());
        }

    for (size_t r = 0; r < lv.regions.size(); ++r) {
        const assets::lvl_region& reg = lv.regions[r];
        std::printf("  region '%s': %zu rooms, %zu prince starts\n",
                    reg.name.c_str(), reg.rooms.size(), reg.prince_starts.size());
        for (size_t i = 0; i < reg.rooms.size(); ++i) {
            const std::string f = assets::lvl_room_filename(reg.rooms[i]);
            if (!plat::fs_resolve_basename(f.c_str(), real, sizeof(real))) {
                ++miss_room;
                std::printf("    MISSING room %s\n", f.c_str());
            }
        }
        for (size_t i = 0; i < reg.prince_starts.size(); ++i) {
            const assets::lvl_prince_start& p = reg.prince_starts[i];
            std::printf("    prince %d at (%.2f, %.2f, %.2f) heading %.2f\n",
                        p.index, p.x, p.y, p.z, p.heading);
        }
    }

    for (size_t i = 0; i < lv.items.size(); ++i)
        if (!plat::fs_resolve_basename(lv.items[i].c_str(), real, sizeof(real))) {
            ++miss_item;
            std::printf("  MISSING item %s\n", lv.items[i].c_str());
        }

    std::printf("  unresolved: %d bodies, %d rooms, %d items\n",
                miss_body, miss_room, miss_item);
    return (miss_body + miss_room + miss_item) == 0 ? 0 : 1;
}

// Headless: parse every master.dat the VFS knows and report. Character
// descriptors are the doorway from a level's [Bodies] list to actors.
int dump_masters() {
    // Full paths, not basenames: 41 character descriptors ship and most are
    // called "master.dat", so a basename listing collapses them to one.
    std::vector<std::string> names;
    plat::fs_list_paths_by_extension(".dat", names);
    std::sort(names.begin(), names.end());

    int parsed = 0, skipped = 0, bad = 0, geom_missing = 0, actor_missing = 0;
    char real[4096];

    for (size_t i = 0; i < names.size(); ++i) {
        size_t size = 0;
        uint8_t* data = plat::fs_read(names[i].c_str(), size);
        if (!data) continue;

        // Only files that open with the master header are character descriptors;
        // the install has many other .dat files (camera, enemy, config).
        const bool looks_master = size > 4 &&
            (std::strstr(reinterpret_cast<const char*>(data), "MASTER") != 0 ||
             std::strstr(reinterpret_cast<const char*>(data), "[Attributes]") != 0);
        if (!looks_master) { plat::fs_free(data); ++skipped; continue; }

        assets::master_file m;
        std::string err;
        const bool ok = assets::master_parse(data, size, m, &err);
        plat::fs_free(data);
        if (!ok) { ++bad; std::printf("  FAIL %s (%s)\n", names[i].c_str(), err.c_str()); continue; }

        ++parsed;
        const bool geom_ok = plat::fs_resolve_basename(m.geometry.c_str(), real, sizeof(real));
        if (!geom_ok) ++geom_missing;

        bool actor_ok = true;
        if (m.proto) {
            actor_ok = plat::fs_resolve_basename(m.actor.c_str(), real, sizeof(real));
            if (!actor_ok) ++actor_missing;
        }

        std::printf("  %-44s v%d%s %-14s geom %-4s weapons %zu parts %2zu anims %zu  %s\n",
                    names[i].c_str(), m.version, m.proto ? " -p" : "   ",
                    m.name.c_str(), geom_ok ? "ok" : "MISS",
                    m.weapons.size(), m.body_parts.size(), m.animations.size(),
                    assets::master_attribute_names(m.attributes).c_str());
        if (!m.shared_body.empty())
            std::printf("      shares body '%s'\n", m.shared_body.c_str());
        if (m.has_archery)
            std::printf("      archery range %.0f  aim %.0f/%.0f/%.0f\n",
                        m.max_range, m.aim_speed, m.aim_acceleration, m.aim_speed_limit);
    }

    std::printf("masters: %d parsed, %d failed, %d non-master .dat skipped\n",
                parsed, bad, skipped);
    std::printf("  unresolved: %d geometry, %d actor\n", geom_missing, actor_missing);
    return bad == 0 ? 0 : 1;
}

// Headless: parse every .act and report. Validates the property table against
// the whole install, the way the other loaders are checked.
int dump_actors() {
    std::vector<std::string> names;
    plat::fs_list_paths_by_extension(".act", names);
    std::sort(names.begin(), names.end());

    int parsed = 0, legacy = 0, bad = 0, wrong_props = 0;
    size_t total_nodes = 0;

    for (size_t i = 0; i < names.size(); ++i) {
        size_t size = 0;
        uint8_t* data = plat::fs_read(names[i].c_str(), size);
        if (!data) continue;

        assets::act_file a;
        std::string err;
        const bool ok = assets::act_parse(data, size, a, &err);
        plat::fs_free(data);
        if (!ok) { ++bad; std::printf("  FAIL %s (%s)\n", names[i].c_str(), err.c_str()); continue; }

        if (a.legacy_container) { ++legacy; continue; }
        ++parsed;
        total_nodes += a.nodes.size();
        if (a.properties.size() != 11) {
            ++wrong_props;
            std::printf("  %-46s v%-3d props %2zu (expected 11)\n",
                        names[i].c_str(), a.version, a.properties.size());
        }
    }

    std::printf("actors: %d parsed, %d legacy \" MFB\", %d failed\n", parsed, legacy, bad);
    std::printf("  files with other than 11 properties: %d\n", wrong_props);
    std::printf("  skeleton nodes found: %zu\n", total_nodes);
    return bad == 0 ? 0 : 1;
}

// Places the camera to take in the whole scene, and scales travel speed to it.
void frame_scene(camera& c, const scene& sc) {
    c.yaw   = 0.0f;
    c.pitch = -0.25f;

    if (sc.empty) {
        c.pos[0] = 0.0f; c.pos[1] = 0.0f; c.pos[2] = 5.0f;
        c.speed  = 5.0f;
        return;
    }

    float centre[3];
    for (int a = 0; a < 3; ++a) centre[a] = (sc.mn[a] + sc.mx[a]) * 0.5f;
    const float dx = sc.mx[0] - sc.mn[0];
    const float dy = sc.mx[1] - sc.mn[1];
    const float dz = sc.mx[2] - sc.mn[2];
    float radius = std::sqrt(dx*dx + dy*dy + dz*dz) * 0.6f;
    if (radius < 0.1f) radius = 0.1f;

    c.speed = radius * 0.8f;      // cross the scene in a bit over a second

    // Prefer to start where the game would: at the player's spawn, looking the
    // way it faces. Framing the whole bounding box drops the camera in the
    // middle of a level, which for a 400-unit level like Cistern is nowhere
    // near anything interesting. Falls back to the box when there is no actor.
    int start = -1;
    for (size_t i = 0; i < sc.actors.size(); ++i)
        if (is_prince(sc.actors[i])) { start = int(i); break; }
    if (start < 0 && !sc.actors.empty()) start = 0;

    if (start >= 0) {
        const actor_inst& ai = sc.actors[size_t(start)];
        const k2::engine::transform& aw = ai.node->world();
        struct { float pos[3]; float yaw; } a;
        a.pos[0] = aw.translate[0]; a.pos[1] = aw.translate[1]; a.pos[2] = aw.translate[2];
        // Recover yaw from the world basis: rotate[2] = sin, rotate[8] = cos.
        a.yaw = std::atan2(aw.rotate[2], aw.rotate[8]);
        // Stand *at* the spawn, at eye height, looking the way the actor faces.
        //
        // An earlier version stood 6 units back along the facing. That is
        // geometry-blind and it showed: in prison1 it put the camera through a
        // cell wall, and in Cistern through the ceiling, so nothing was visible
        // in either. A spawn point is by definition somewhere a character can
        // stand, so the only placement guaranteed to be in open space is the
        // spawn itself. Eye height is the one offset small enough to be safe.
        // Offset is deliberately small. Six units back put the camera through a
        // cell wall in prison1 and through the ceiling in Cistern; a spawn point
        // only guarantees open space immediately around itself. 2.5 back and
        // 1.6 up keeps the actor framed and, measured on both, stays indoors.
        const float back = 2.5f;
        c.yaw    = a.yaw;
        c.pitch  = -0.10f;
        c.pos[0] = a.pos[0] - std::sin(a.yaw) * back;
        c.pos[1] = a.pos[1] + 1.6f;
        c.pos[2] = a.pos[2] + std::cos(a.yaw) * back;
        // Character-scale scene, not level-scale.
        c.speed  = 8.0f;
        return;
    }

    c.pos[0] = centre[0];
    c.pos[1] = centre[1] + radius * 0.35f;
    c.pos[2] = centre[2] + radius;
}

// Loads a character from its master.dat: the descriptor names the geometry and
// the actor prototype. Renders the geometry; the .act supplies properties and,
// eventually, the skeleton.
// Reads a character descriptor and its actor prototype, reporting both.
// Needs no GL, so it doubles as the headless --dumpactor path.
bool describe_actor(const char* master_path, assets::master_file& m) {
    size_t size = 0;
    uint8_t* data = plat::fs_read(master_path, size);
    if (!data) data = plat::fs_read_basename(master_path, size);
    if (!data) { std::fprintf(stderr, "k2boy: cannot read '%s'\n", master_path); return false; }

    std::string err;
    const bool ok = assets::master_parse(data, size, m, &err);
    plat::fs_free(data);
    if (!ok) { std::fprintf(stderr, "k2boy: master parse failed: %s\n", err.c_str()); return false; }

    std::printf("actor '%s' v%d%s\n  geometry %s\n  actor    %s\n",
                m.name.c_str(), m.version, m.proto ? " (proto)" : "",
                m.geometry.c_str(), m.actor.c_str());
    std::printf("  attributes %s\n", assets::master_attribute_names(m.attributes).c_str());
    std::printf("  %zu weapons, %zu body parts, %zu hands\n",
                m.weapons.size(), m.body_parts.size(), m.hands.size());

    // The actor prototype: a .act path when -p, otherwise a directory whose
    // .act shares its name.
    std::string act_path = m.actor;
    if (!m.proto) act_path = m.actor + ".act";

    size_t asize = 0;
    uint8_t* adata = plat::fs_read_basename(act_path.c_str(), asize);
    if (adata) {
        assets::act_file a;
        if (assets::act_parse(adata, asize, a, &err)) {
            std::printf("  .act '%s' v%d: %zu properties, %zu nodes\n",
                        a.name.c_str(), a.version, a.properties.size(), a.nodes.size());
            for (size_t n = 0; n < a.nodes.size() && n < 8; ++n)
                std::printf("      node %-14s pos=(%7.3f,%7.3f,%7.3f)\n",
                            a.nodes[n].name.c_str(), a.nodes[n].pos[0],
                            a.nodes[n].pos[1], a.nodes[n].pos[2]);
            const assets::act_property* r = a.find("MF_bndRadius");
            if (r) std::printf("    MF_bndRadius %.4f\n", r->float_value);
            const assets::act_property* l = a.find("MF_locoSkill");
            if (l && !l->string_value.empty())
                std::printf("    MF_locoSkill '%s'\n", l->string_value.c_str());
        }
        plat::fs_free(adata);
    } else {
        std::printf("  .act not found: %s\n", act_path.c_str());
    }

    for (size_t i = 0; i < m.weapons.size(); ++i) {
        const assets::master_weapon& w = m.weapons[i];
        std::printf("    weapon %-22s damage %.1f style %d%s\n",
                    w.name.c_str(), w.damage, w.style,
                    w.blur.present ? "  (blur)" : "");
    }
    return true;
}

bool load_actor(const char* master_path, scene& sc) {
    assets::master_file m;
    if (!describe_actor(master_path, m)) return false;

    scene_reset(sc);
    if (!append_scene(m.geometry.c_str(), sc)) {
        std::fprintf(stderr, "k2boy: could not load geometry '%s'\n", m.geometry.c_str());
        return false;
    }
    return !sc.empty;
}

// Headless: parse every .skl and validate the JKey quaternions. The strongest
// available check, since a wrong field boundary destroys unit-norm immediately.
// Headless: parse every .sfx. Counts, formats, and how many loop.
int dump_sounds() {
    std::vector<std::string> names;
    plat::fs_list_paths_by_extension(".sfx", names);
    int ok = 0, bad = 0, loops = 0, stereo = 0;
    double seconds = 0.0;
    for (size_t i = 0; i < names.size(); ++i) {
        size_t size = 0;
        uint8_t* d = plat::fs_read(names[i].c_str(), size);
        if (!d) { ++bad; continue; }
        assets::sfx_file sf;
        std::string err;
        if (assets::sfx_parse(d, size, sf, &err)) {
            ++ok;
            if (sf.loops()) ++loops;
            if (sf.channels == 2) ++stereo;
            seconds += double(sf.frames()) / double(sf.rate);
        } else {
            ++bad;
            if (bad <= 5) std::printf("  FAIL %s (%s)\n", names[i].c_str(), err.c_str());
        }
        plat::fs_free(d);
    }
    std::printf("sounds: %d parsed, %d failed, %d loop, %d stereo, %.1f minutes\n",
                ok, bad, loops, stereo, seconds / 60.0);

    // Mixer self-test: half a second of a sound the prison scripts play, and
    // a positional copy beside and far from the listener.
    k2::engine::mixer mx(44100);
    k2::engine::mixer* keep = g_mixer;
    g_mixer = &mx;
    const char* probe = "L01waterdropN";
    std::vector<float> buf(44100 / 2 * 2, 0.0f);
    double rms[3] = { 0, 0, 0 };
    const float ear[3] = { 0, 0, 0 }, right[3] = { 1, 0, 0 };
    mx.set_listener(ear, right);
    const float near_right[3] = { 3, 0, 0 }, far_away[3] = { 100, 0, 0 };
    const float* where[3] = { 0, near_right, far_away };
    for (int k = 0; k < 3; ++k) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        const int32_t id = play_sound(probe, where[k], 12.0f, false);
        mx.mix(&buf[0], int32_t(buf.size() / 2));
        double l = 0, r = 0;
        for (size_t i = 0; i + 1 < buf.size(); i += 2) { l += buf[i] * buf[i]; r += buf[i + 1] * buf[i + 1]; }
        rms[k] = std::sqrt((l + r) / double(buf.size()));
        std::printf("  mixer %-11s voice %d  rms %.4f  (left %.4f right %.4f)\n",
                    k == 0 ? "centre" : k == 1 ? "3 right" : "100 away", id, rms[k],
                    std::sqrt(l / double(buf.size() / 2)), std::sqrt(r / double(buf.size() / 2)));
        mx.stop(id);
    }
    g_mixer = keep;
    return bad == 0 ? 0 : 1;
}

int dump_skills() {
    std::vector<std::string> names;
    plat::fs_list_paths_by_extension(".skl", names);
    std::sort(names.begin(), names.end());

    int files = 0, bad = 0;
    size_t jkeys = 0, skeys = 0, vkeys = 0;
    size_t chan = 0, unit = 0;
    size_t named_sounds = 0;
    int    sampled = 0, manip = 0;
    size_t paths = 0, overlong = 0, path_rot = 0, path_unit = 0;

    for (size_t i = 0; i < names.size(); ++i) {
        size_t size = 0;
        uint8_t* data = plat::fs_read(names[i].c_str(), size);
        if (!data) continue;

        assets::skl_file sk;
        std::string err;
        const bool ok = assets::skl_parse(data, size, sk, &err);
        plat::fs_free(data);
        if (!ok) { ++bad; std::printf("  FAIL %s (%s)\n", names[i].c_str(), err.c_str()); continue; }

        ++files;
        jkeys += sk.joint_keys.size();
        skeys += sk.sound_keys.size();
        vkeys += sk.event_keys.size();

        for (size_t k = 0; k < sk.sound_keys.size(); ++k)
            if (!sk.sound_keys[k].sound.empty()) ++named_sounds;

        for (size_t k = 0; k < sk.joint_keys.size(); ++k) {
            for (int t = 0; t < 6; ++t) {
                const float* q = sk.joint_keys[k].xf[t].quat;
                const float n2 = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
                ++chan;
                if (n2 > 0.998f && n2 < 1.002f) ++unit;
            }
        }

        // SklS: every rotation key of every joint path should be a unit
        // quaternion, and no track may be longer than the clip it belongs to.
        if (sk.chunk_tag == "SklS") ++sampled;
        for (size_t j = 0; j < sk.joints.size(); ++j) {
            const assets::skl_joint& jt = sk.joints[j];
            if (!jt.present || sk.chunk_tag != "SklS") continue;
            const assets::skl_joint_path& p = jt.path;
            ++paths;
            if (p.pos_keys() > p.frames || p.rot_keys() > p.frames) ++overlong;
            for (size_t r = 0; r < p.rot_keys(); ++r) {
                const float* q = &p.rot[r * 4];
                const float n2 = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
                ++path_rot;
                if (n2 > 0.998f && n2 < 1.002f) ++path_unit;
            }
        }
        if (sk.kind == assets::SKL_MANIP) ++manip;
    }

    std::printf("skills: %d parsed, %d failed\n", files, bad);
    std::printf("  JKey %zu, SKey %zu (%zu named), vKey %zu\n",
                jkeys, skeys, named_sounds, vkeys);
    std::printf("  quaternions unit-norm: %zu / %zu  (%.2f%%)\n",
                unit, chan, chan ? 100.0 * double(unit) / double(chan) : 0.0);
    std::printf("  SklS skills %d: %zu joint paths, %zu longer than their clip\n",
                sampled, paths, overlong);
    std::printf("  joint path rotations unit-norm: %zu / %zu  (%.2f%%)\n",
                path_unit, path_rot, path_rot ? 100.0 * double(path_unit) / double(path_rot) : 0.0);
    std::printf("  MSkl %d (manipulation block not decoded)\n", manip);
    return bad == 0 ? 0 : 1;
}

// --- player movement over the pick primitive ---------------------------------
//
// The original's PrinceMotion is animation-driven and far larger; this is a
// stand-in built on the same primitive it uses (0x512183): a down probe for the
// floor, horizontal probes for walls, and the hit normal to tell them apart.
// The constants are ours, not the game's.
namespace move_tuning {
const float STEP_UP      = 0.45f;   // highest ledge walked up without climbing
const float PROBE_DOWN   = 0.6f;    // below the feet still counts as standing
const float RADIUS       = 0.35f;   // body radius for wall probes
const float GRAVITY      = 17.0f;   // FallGravity (0x4F8547), as a magnitude
const float FLOOR_MIN_NY = 0.7f;    // up-facing enough to stand on (~45 deg)
const float PROBE_HEIGHTS[3] = { 0.5f, 1.0f, 1.6f };
}

// Height of the floor under `p`, searched from STEP_UP above it down to
// PROBE_DOWN below. False when there is none in range.
bool ground_under(const k2::engine::collision_world& w, const float* p, float& y) {
    const float o[3] = { p[0], p[1] + move_tuning::STEP_UP, p[2] };
    const float d[3] = { 0.0f, -1.0f, 0.0f };
    k2::engine::pick_hit h;
    if (!w.pick(o, d, move_tuning::STEP_UP + move_tuning::PROBE_DOWN, true, h)) return false;
    if (h.normal[1] < move_tuning::FLOOR_MIN_NY) return false;
    y = h.point[1];
    return true;
}

void move_horizontal(const k2::engine::collision_world& w, float* p, float dx, float dz, bool low);
inline void move_horizontal(const k2::engine::collision_world& w, float* p, float dx, float dz) {
    move_horizontal(w, p, dx, dz, false);
}

// Moves the body by (dx, dz), sliding along walls. Each probe height casts
// along the move from the body's centre; a hit inside RADIUS of the new
// position removes the component of the move that points into the wall.
// `low`: crouched or crawling, so only what is below his lowered body stops
// him - a gap under a wall is a way through.
void move_horizontal(const k2::engine::collision_world& w, float* p, float dx, float dz, bool low) {
    static const float LOW_HEIGHTS[3] = { 0.25f, 0.5f, 0.75f };
    const float* heights = low ? LOW_HEIGHTS : move_tuning::PROBE_HEIGHTS;
    for (int pass = 0; pass < 2; ++pass) {
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-6f) return;
        const float d[3] = { dx / len, 0.0f, dz / len };
        bool blocked = false;
        for (int k = 0; k < 3 && !blocked; ++k) {
            const float o[3] = { p[0], p[1] + heights[k], p[2] };
            k2::engine::pick_hit h;
            if (!w.pick(o, d, len + move_tuning::RADIUS, false, h)) continue;
            if (h.normal[1] >= move_tuning::FLOOR_MIN_NY) continue;   // a slope, not a wall
            // Horizontal part of the normal, then cancel motion into it.
            float nx = h.normal[0], nz = h.normal[2];
            const float nl = std::sqrt(nx * nx + nz * nz);
            if (nl < 1e-6f) continue;
            nx /= nl; nz /= nl;
            const float into = dx * nx + dz * nz;
            if (into < 0.0f) { dx -= into * nx; dz -= into * nz; }
            blocked = true;
        }
        if (!blocked) break;
        if (pass == 1) return;           // still blocked after one slide: stop
    }
    p[0] += dx;
    p[2] += dz;
}

// Where the hands (LGrip/RGrip, averaged) are in a clip's pose at time t,
// in the actor's own frame. False without grip joints.
bool clip_grip(actor_inst& a, const assets::skl_file& s, uint32_t t, float* out) {
    std::vector<k2::engine::transform> keep = a.joint_local_cur;
    const size_t n = std::min(a.joint_local_cur.size(), s.joints.size());
    for (size_t j = 0; j < n; ++j) {
        if (!s.joints[j].present) continue;
        float pos[3], q[4];
        assets::skl_sample(s.joints[j].path, t, true, pos, q);
        if (j == 0) for (int k = 0; k < 3; ++k) pos[k] = a.joint_local_bind[0].translate[k];
        transform_from_pos_quat(pos, q, a.joint_local_cur[j]);
    }
    pose_actor(a);
    float sum[3] = { 0, 0, 0 };
    int cnt = 0;
    for (size_t j = 0; j < a.joint_names.size(); ++j)
        if (a.joint_names[j] == "LGrip" || a.joint_names[j] == "RGrip") {
            for (int k = 0; k < 3; ++k) sum[k] += a.cur[j].translate[k];
            ++cnt;
        }
    a.joint_local_cur = keep;
    pose_actor(a);
    if (!cnt) return false;
    for (int k = 0; k < 3; ++k) out[k] = sum[k] / float(cnt);
    return true;
}

// Where joint `j` is in a clip's pose at time t, in the actor's own frame,
// with the root held at its bind position the way the clip is played.
bool clip_joint(actor_inst& a, const assets::skl_file& s, uint32_t t, size_t j, float* out) {
    if (j >= a.joint_local_cur.size()) return false;
    std::vector<k2::engine::transform> keep = a.joint_local_cur;
    for (size_t i = 0; i < a.joint_local_cur.size(); ++i) {
        if (i >= s.joints.size() || !s.joints[i].present) { a.joint_local_cur[i] = a.joint_local_bind[i]; continue; }
        float pos[3], q[4];
        assets::skl_sample(s.joints[i].path, t, true, pos, q);
        if (i == 0) for (int k = 0; k < 3; ++k) pos[k] = a.joint_local_bind[0].translate[k];
        transform_from_pos_quat(pos, q, a.joint_local_cur[i]);
    }
    pose_actor(a);
    for (int k = 0; k < 3; ++k) out[k] = a.cur[j].translate[k];
    a.joint_local_cur = keep;
    pose_actor(a);
    return true;
}

// --- crates -------------------------------------------------------------------
//
// The Prince's crate states, StStartCrateGrab -> StIdleCrate -> StStartCrateMove
// (0x4D648C). The rules come from the binary: a crate moves one step of
// flt_7C1F5C = 1.5 units, and only when sub_4D1719 finds floor at the far end
// at the Prince's own height. The clips carry the motion: Push_M's root moves
// exactly +1.5, Pull_M's -1.5, the antic and finish 0.7 in and out. How a crate
// is found and grabbed is ours; that part of the state machine is not traced.
namespace crate_tuning {
const float  STEP      = 1.5f;      // flt_7C1F5C
const double FLOOR_TOL = 0.01;      // dbl_743AD0
const float  REACH     = 1.2f;      // ours: furthest grabbable gap
}

// Port of sub_4D1719. `mv` is +1 to push, -1 to pull; `fwd` the facing.
bool crate_move_ok(const k2::engine::collision_world& w, const float* pos,
                   const float* fwd, int mv) {
    const float S = crate_tuning::STEP;
    const float dist = mv == 1 ? S * 0.5f + S : S * 0.5f - (S + S);
    const float probe[3] = { pos[0] + fwd[0] * dist, pos[1], pos[2] + fwd[2] * dist };
    const float o[3] = { probe[0], probe[1] + S, probe[2] };    // + up * flt_7C1F5C
    const float d[3] = { 0.0f, -1.0f, 0.0f };
    k2::engine::pick_hit h;
    if (!w.pick(o, d, 1.0e6f, true, h)) return false;           // mask 6, front only
    return std::fabs(double(h.point[1]) - double(probe[1])) <= crate_tuning::FLOOR_TOL;
}

// The nearest Crate trap ahead of the Prince, and the axis to face it along.
scene_trap* crate_in_front(scene& sc, const float* pos, const float* fwd, float* axis) {
    scene_trap* best = 0;
    float best_gap = crate_tuning::REACH;
    for (size_t i = 0; i < sc.traps.size(); ++i) {
        scene_trap* t = dynamic_cast<scene_trap*>(sc.traps[i]);
        if (!t || t->type != "Crate" || !t->collideable()) continue;
        const float* o = t->offset();
        const float mn[3] = { t->box[0] + o[0], t->box[1] + o[1], t->box[2] + o[2] };
        const float mx[3] = { t->box[3] + o[0], t->box[4] + o[1], t->box[5] + o[2] };
        if (std::fabs(mn[1] - pos[1]) > 0.3f) continue;          // not on our floor
        const float cx = (mn[0] + mx[0]) * 0.5f, cz = (mn[2] + mx[2]) * 0.5f;
        const float dx = cx - pos[0], dz = cz - pos[2];
        const bool along_x = std::fabs(dx) > std::fabs(dz);
        const float ax[3] = { along_x ? (dx > 0 ? 1.0f : -1.0f) : 0.0f, 0.0f,
                              along_x ? 0.0f : (dz > 0 ? 1.0f : -1.0f) };
        if (fwd[0] * ax[0] + fwd[2] * ax[2] < 0.7f) continue;     // not facing it
        const float half = along_x ? (mx[0] - mn[0]) * 0.5f : (mx[2] - mn[2]) * 0.5f;
        const float side = along_x ? std::fabs(dz) - (mx[2] - mn[2]) * 0.5f
                                   : std::fabs(dx) - (mx[0] - mn[0]) * 0.5f;
        if (side > 0.0f) continue;                                // beside it, not before
        const float gap = std::fabs(along_x ? dx : dz) - half;
        if (gap < 0.0f || gap > best_gap) continue;
        best = t;
        best_gap = gap;
        for (int k = 0; k < 3; ++k) axis[k] = ax[k];
    }
    return best;
}

// Root motion of `clip` from its start to `t_ms`, rotated into the world by
// `yaw` the way transform::set_yaw rotates the body.
void root_delta(const assets::skl_file& s, uint32_t t_ms, float yaw, float* out) {
    out[0] = out[1] = out[2] = 0.0f;
    if (s.joints.empty() || !s.joints[0].present) return;
    float p0[3], pt[3], q[4];
    assets::skl_sample(s.joints[0].path, 0, s.interpolate != 0, p0, q);
    assets::skl_sample(s.joints[0].path, t_ms, s.interpolate != 0, pt, q);
    const float dx = pt[0] - p0[0], dz = pt[2] - p0[2];
    const float c = std::cos(yaw), sn = std::sin(yaw);
    out[0] = c * dx + sn * dz;
    out[2] = -sn * dx + c * dz;
}

struct crate_ctl {
    enum state_t { NONE, ANTIC, IDLE, MOVE, FINISH } state;
    scene_trap* crate;
    int         clip;
    bool        moves_crate;
    double      t0;
    float       origin[3];
    float       crate_origin[3];

    crate_ctl() : state(NONE), crate(0), clip(-1), moves_crate(false), t0(0) {
        for (int k = 0; k < 3; ++k) origin[k] = crate_origin[k] = 0.0f;
    }

    void begin(actor_inst& a, int c, double now, const float* pos, bool carry) {
        clip = c;
        set_clip(a, c);
        t0 = now;
        moves_crate = carry;
        for (int k = 0; k < 3; ++k) origin[k] = pos[k];
        if (crate) for (int k = 0; k < 3; ++k) crate_origin[k] = crate->offset()[k];
    }
};

// One frame of the crate states. Returns false when the Prince is not
// handling a crate, so normal movement applies.
bool update_crate(crate_ctl& cc, scene& sc, actor_inst& a, float* pos, float& yaw,
                  double now, bool action, bool push, bool pull) {
    if (a.clip >= 0 && size_t(a.clip) >= a.loco.size()) return false;
    const float fwd[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };

    if (cc.state == crate_ctl::NONE) {
        if (!action || !a.loco_ok[CRATE_ANTIC]) return false;
        float axis[3];
        scene_trap* t = crate_in_front(sc, pos, fwd, axis);
        if (!t) return false;
        cc.crate = t;
        yaw = std::atan2(axis[0], axis[2]);               // square up to it
        // Stand where the antic's 0.7 step in leaves his hands on the face
        // (PushOrPullBase_M's grip reach), centred on the crate; grabbing
        // from wherever he stopped put his arms through it.
        float grip[3];
        if (a.loco_ok[CRATE_BASE] && clip_grip(a, a.loco[CRATE_BASE], 0, grip)) {
            float step[3] = { 0.0f, 0.0f, 0.7f };
            clip_root_motion(a.loco[CRATE_ANTIC], step);
            const float in = std::sqrt(step[0] * step[0] + step[2] * step[2]);
            const float* o = t->offset();
            const float mn[3] = { t->box[0] + o[0], t->box[1] + o[1], t->box[2] + o[2] };
            const float mx[3] = { t->box[3] + o[0], t->box[4] + o[1], t->box[5] + o[2] };
            const float back = in + grip[2];
            if (axis[0] != 0.0f) {
                pos[0] = (axis[0] > 0 ? mn[0] : mx[0]) - axis[0] * back;
                pos[2] = (mn[2] + mx[2]) * 0.5f;
            } else {
                pos[2] = (axis[2] > 0 ? mn[2] : mx[2]) - axis[2] * back;
                pos[0] = (mn[0] + mx[0]) * 0.5f;
            }
        }
        cc.state = crate_ctl::ANTIC;
        cc.begin(a, CRATE_ANTIC, now, pos, false);
        std::printf("crate: grab %s at (%.2f, %.2f)\n", t->name().c_str(), pos[0], pos[2]);
        return true;
    }

    const assets::skl_file& s = a.loco[size_t(cc.clip)];
    const uint32_t len = clip_length_ms(s);
    const double el_ms = (now - cc.t0) * 1000.0;
    const bool finished = el_ms >= double(len);
    const uint32_t t_ms = finished ? len : uint32_t(el_ms);

    float d[3];
    root_delta(s, t_ms, yaw, d);
    pos[0] = cc.origin[0] + d[0];
    pos[2] = cc.origin[2] + d[2];
    if (cc.moves_crate && cc.crate) {
        const float off[3] = { cc.crate_origin[0] + d[0], cc.crate_origin[1],
                               cc.crate_origin[2] + d[2] };
        cc.crate->place(off);
    }
    if (!finished) return true;

    switch (cc.state) {
    case crate_ctl::ANTIC:
    case crate_ctl::MOVE:
        cc.state = crate_ctl::IDLE;
        cc.begin(a, CRATE_BASE, now, pos, false);
        break;
    case crate_ctl::IDLE:
        cc.begin(a, CRATE_BASE, now, pos, false);          // hold the pose
        break;
    case crate_ctl::FINISH:
        cc.state = crate_ctl::NONE;
        cc.crate = 0;
        set_clip(a, LOCO_BASE);
        return true;
    default:
        break;
    }
    return true;
}

// Inputs while holding a crate: release, push or pull (StStartCrateMove).
void crate_input(crate_ctl& cc, scene& sc, actor_inst& a, const float* pos, float yaw,
                 double now, bool action, bool push, bool pull) {
    if (cc.state != crate_ctl::IDLE) return;
    const float fwd[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
    if (!action) {
        cc.state = crate_ctl::FINISH;
        cc.begin(a, CRATE_FINISH, now, pos, false);
        return;
    }
    if (push || pull) {
        const int mv = push ? 1 : -1;
        // From where he took hold, before the antic's 0.7 step in (the
        // crate's step and the clips' travel all assume it). Measured from
        // where the step leaves him, a push from the ledge towards prison4's
        // CrateTarget probed onto RampThingie's slope and always failed.
        const float held[3] = { pos[0] - fwd[0] * 0.7f, pos[1], pos[2] - fwd[2] * 0.7f };
        const bool ok = crate_move_ok(sc.coll, held, fwd, mv);
        const int clip = push ? (ok ? CRATE_PUSH : CRATE_PUSH_FAIL)
                              : (ok ? CRATE_PULL : CRATE_PULL_FAIL);
        cc.state = crate_ctl::MOVE;
        cc.begin(a, clip, now, pos, ok);
        std::printf("crate: %s %s -> %s\n", push ? "push" : "pull",
                    cc.crate ? cc.crate->name().c_str() : "?", k_loco_names[clip]);
    }
}

// --- jumps ----------------------------------------------------------------------
//
// The Prince's stand and run jumps. Recovered from the binary:
//   * StContinueJump (0x4B6A0F) picks the direction from the input relative to
//     him - none: up; left / right; forward / back - and plays the matching
//     *Antic_G clip
//   * StStartActualJump (0x4B6C40) launches him with a velocity built from the
//     tuning block (dword_801C2C, defaults in its ctor 0x4F8547, keys parsed by
//     0x4F8667; the CONFIG.ENT that would override them is not shipped):
//       up       fwd*fStandJumpUpZ      + up*fStandJumpUpY        0.07, 8.7
//       forward  fwd*fStandJumpForwardZ + up*fStandJumpForwardY   5.7, 2.75
//       back     fwd*fStandJumpBackZ    + up*fStandJumpBackY     -4.2, 3.75
//       side     side*fStandJumpSideZ   + up*fStandJumpSideY      3.28, 3.28
//       run      fwd*fRunJumpZ          + up*fRunJumpY            7.2, 5.1
//     and gravity is FallGravity, -17.
// Landing picks the matching Land* clip, or LandVertPainful_G past
// MinFallHeight (6.25). How the original detects the landing (the predictive
// planner) is not traced; here the floor probe ends the flight.
// Potions on the Prince, timed effects from the pickup handler (0x47FC8C):
// "Strength:dmg,t", "Shield:t", "Camo:t", "Guard:t", "Jump:f,t", "Fall:t".
// Each holds until game time `*_until`; a duration of -1 is for good.
struct potion_state {
    double strength_until, shield_until, camo_until, guard_until, jump_until, fall_until;
    float  strength, jump;
    potion_state() : strength_until(-1), shield_until(-1), camo_until(-1), guard_until(-1),
                     jump_until(-1), fall_until(-1), strength(0), jump(1) {}
    static bool on(double until, double now) { return until >= 1.0e29 || now < until; }
};
potion_state g_potions;

namespace jump_tuning {
const float GRAVITY = -17.0f;           // FallGravity
const float MIN_FALL_HEIGHT = 6.25f;
// The rest of the fall block (0x4F8547; config.dat sets the damage three).
const float FALL_DEATH_HEIGHT = 12.0f;
const float FALL_CONST_DAMAGE = 6.0f, FALL_LINEAR_DAMAGE = 0.5f, FALL_SQUARED_DAMAGE = 1.5f;
// Damage for a drop of `h`. How the three combine is not traced: here a
// polynomial in the height past MinFallHeight, and death past
// FallDeathHeight.
inline int fall_damage(float h, int health) {
    if (h <= MIN_FALL_HEIGHT) return 0;
    if (h >= FALL_DEATH_HEIGHT) return health;
    const float x = h - MIN_FALL_HEIGHT;
    return int(FALL_CONST_DAMAGE + FALL_LINEAR_DAMAGE * x + FALL_SQUARED_DAMAGE * x * x + 0.5f);
}
}

struct jump_ctl {
    enum state_t { NONE, ANTIC, AIR, LAND } state;
    int    dir;            // 0 up, 1 forward, 2 back, 3 left, 4 right, 5 run
    double t0;
    float  origin[3];
    float  vel[3];
    float  peak_y;
    float  fell;           // height of the last landing's drop, until combat takes it

    jump_ctl() : state(NONE), dir(0), t0(0), peak_y(0), fell(0) {
        for (int k = 0; k < 3; ++k) origin[k] = vel[k] = 0.0f;
    }
};

// Plays clip `c` once from now, from `pos`.
void jump_begin(jump_ctl& jc, actor_inst& a, int c, double now, const float* pos) {
    set_clip(a, c);
    a.clip_once = true;
    a.clip_ms = 0.0;
    jc.t0 = now;
    for (int k = 0; k < 3; ++k) jc.origin[k] = pos[k];
}

bool clip_finished(const actor_inst& a, int c, const jump_ctl& jc, double now) {
    return c < 0 || !a.loco_ok[size_t(c)] ||
           (now - jc.t0) * 1000.0 >= double(clip_length_ms(a.loco[size_t(c)]));
}

// Starts a jump. `local_dir` is the input direction in the Prince's frame
// (x right, z forward); zero for straight up. Returns false without the clips.
bool jump_start(jump_ctl& jc, actor_inst& a, bool running, const float* local_dir,
                double now, const float* pos) {
    if (jc.state != jump_ctl::NONE || !a.loco_ok[JUMP_UP_ANTIC]) return false;
    int antic;
    if (running) { jc.dir = 5; antic = RUN_JUMP_ANTIC; }
    else if (local_dir[0] == 0.0f && local_dir[2] == 0.0f) { jc.dir = 0; antic = JUMP_UP_ANTIC; }
    else if (std::fabs(local_dir[2]) >= std::fabs(local_dir[0])) {
        jc.dir = local_dir[2] > 0 ? 1 : 2;
        antic = jc.dir == 1 ? JUMP_FWD_ANTIC : JUMP_BACK_ANTIC;
    } else {
        // The Prince's +x is his left (set_yaw maps +z forward, +x to the left
        // of a viewer behind him).
        jc.dir = local_dir[0] > 0 ? 3 : 4;
        antic = jc.dir == 3 ? JUMP_LEFT_ANTIC : JUMP_RIGHT_ANTIC;
    }
    jc.state = jump_ctl::ANTIC;
    jump_begin(jc, a, antic, now, pos);
    return true;
}

// One frame of a jump. Returns true while the jump owns the body.
bool jump_update(jump_ctl& jc, actor_inst& a, const k2::engine::collision_world& w,
                 float* pos, float yaw, double now, float dt) {
    if (jc.state == jump_ctl::NONE) return false;
    const float fwd[3]  = { std::sin(yaw), 0.0f, std::cos(yaw) };
    const float side[3] = { std::cos(yaw), 0.0f, -std::sin(yaw) };    // his left
    static const int anti[6] = { JUMP_UP_ANTIC, JUMP_FWD_ANTIC, JUMP_BACK_ANTIC,
                                 JUMP_LEFT_ANTIC, JUMP_RIGHT_ANTIC, RUN_JUMP_ANTIC };
    static const int air[6]  = { JUMP_UP, JUMP_FWD, JUMP_BACK, JUMP_LEFT, JUMP_RIGHT, RUN_JUMPING };
    static const int land[6] = { LAND_VERT, LAND_FWD, LAND_BACK, LAND_LEFT, LAND_RIGHT, RUN_JUMP_LAND };

    if (jc.state == jump_ctl::ANTIC || jc.state == jump_ctl::LAND) {
        const int c = jc.state == jump_ctl::ANTIC ? anti[jc.dir] : a.clip;
        if (c >= 0 && a.loco_ok[size_t(c)]) {
            const assets::skl_file& s = a.loco[size_t(c)];
            const uint32_t len = clip_length_ms(s);
            const double el = (now - jc.t0) * 1000.0;
            float d[3];
            root_delta(s, el >= double(len) ? len : uint32_t(el), yaw, d);
            float target[3] = { jc.origin[0] + d[0], pos[1], jc.origin[2] + d[2] };
            move_horizontal(w, pos, target[0] - pos[0], target[2] - pos[2]);
        }
        if (!clip_finished(a, c, jc, now)) return true;
        if (jc.state == jump_ctl::LAND) {
            jc.state = jump_ctl::NONE;
            set_clip(a, LOCO_BASE);
            return false;
        }
        // StStartActualJump: launch.
        float Y, Z;
        switch (jc.dir) {
            case 0:  Y = 8.7f;  Z = 0.07f; break;
            case 1:  Y = 2.75f; Z = 5.7f;  break;
            case 2:  Y = 3.75f; Z = -4.2f; break;
            case 5:  Y = 5.1f;  Z = 7.2f;  break;
            default: Y = 3.28f; Z = 3.28f; break;
        }
        for (int k = 0; k < 3; ++k) jc.vel[k] = 0.0f;
        if (jc.dir == 3 || jc.dir == 4) {
            const float sgn = jc.dir == 3 ? 1.0f : -1.0f;
            for (int k = 0; k < 3; ++k) jc.vel[k] = side[k] * Z * sgn;
        } else {
            for (int k = 0; k < 3; ++k) jc.vel[k] = fwd[k] * Z;
        }
        // Jump potion: f times the height, so sqrt(f) on the launch.
        if (potion_state::on(g_potions.jump_until, now)) Y *= std::sqrt(std::max(1.0f, g_potions.jump));
        jc.vel[1] = Y;
        jc.peak_y = pos[1];
        jc.state = jump_ctl::AIR;
        jump_begin(jc, a, air[jc.dir], now, pos);
        return true;
    }

    // Airborne: ballistic under FallGravity; walls stop the horizontal part.
    jc.vel[1] += jump_tuning::GRAVITY * dt;
    const float before[2] = { pos[0], pos[2] };
    move_horizontal(w, pos, jc.vel[0] * dt, jc.vel[2] * dt);
    {
        // Over something higher than where he took off (a crate's top), he
        // may only come down on it if he fits there - 1.8 of headroom.
        // Otherwise the move over it is undone and he drops back beside it.
        const float o[3] = { pos[0], pos[1] + 0.05f, pos[2] };
        const float down[3] = { 0.0f, -1.0f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
        k2::engine::pick_hit h, ch;
        if (w.pick(o, down, 8.0f, true, h) && h.point[1] > jc.origin[1] + 0.4f) {
            const float co[3] = { h.point[0], h.point[1] + 0.05f, h.point[2] };
            if (w.pick(co, up, 1.8f, false, ch)) {
                pos[0] = before[0];
                pos[2] = before[1];
                jc.vel[0] = jc.vel[2] = 0.0f;
            }
        }
    }
    if (std::fabs(pos[0] - before[0]) < std::fabs(jc.vel[0] * dt) * 0.5f) jc.vel[0] = 0.0f;
    if (std::fabs(pos[2] - before[1]) < std::fabs(jc.vel[2] * dt) * 0.5f) jc.vel[2] = 0.0f;
    const float dy = jc.vel[1] * dt;
    if (dy > 0.0f) {
        // Rising: a ceiling stops the climb.
        const float o[3] = { pos[0], pos[1] + 1.8f, pos[2] };
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        k2::engine::pick_hit h;
        if (w.pick(o, up, dy, false, h)) jc.vel[1] = 0.0f;
        else pos[1] += dy;
        if (pos[1] > jc.peak_y) jc.peak_y = pos[1];
        return true;
    }
    const float o[3] = { pos[0], pos[1] + 0.05f, pos[2] };
    const float down[3] = { 0.0f, -1.0f, 0.0f };
    k2::engine::pick_hit h;
    if (w.pick(o, down, -dy + 0.05f, true, h) && h.normal[1] >= move_tuning::FLOOR_MIN_NY) {
        pos[1] = h.point[1];
        const float fell = jc.peak_y - pos[1];
        jc.state = jump_ctl::LAND;
        const bool feather = potion_state::on(g_potions.fall_until, now);    // Fall potion
        const int c = fell > jump_tuning::MIN_FALL_HEIGHT && !feather ? LAND_PAINFUL : land[jc.dir];
        jc.fell = feather ? 0.0f : fell;
        jump_begin(jc, a, c, now, pos);
        return true;
    }
    pos[1] += dy;
    return true;
}

// --- ledges -------------------------------------------------------------------
//
// Climbing onto, hanging from, shimmying along and pulling up over ledges.
//
// From the binary: StStart1MClimb (0x4B46C2) chooses the climb clip by the
// ledge's height above the Prince - HopUpOnto2m_M above 1.75, 1point5m above
// 1.3, 1m above 0.9, else HopUpOntoSmall_M (flt_7461A4 / flt_74AB68 /
// flt_743F9C). The clips carry no vertical root motion; the engine raises the
// body. "Reachable Ledge YAHOO! Start Hanging!" (0x4B4CC8) goes through two
// probe functions (0x4CF65C, 0x4D0980) that cast with mask 6 from the hand
// nodes; those are not traced, so finding the ledge is ours: a downward pick
// a little ahead for an up-facing top, a horizontal one for its face.
// The hang is placed from the skeleton: the LGrip/RGrip joints of
// LedgeHangBase_M are put on the edge.
struct ledge_hit {
    float top;          // y of the ledge's upper surface
    float edge[3];      // a point on the edge, at `top`
    float facing;       // yaw that faces the wall
    bool  can_stand;    // headroom on top to stand up
};

// A ledge ahead of `pos` along `yaw`, whose top is between min_h and max_h
// above pos[1].
bool find_ledge(const k2::engine::collision_world& w, const float* pos, float yaw,
                float min_h, float max_h, ledge_hit& out) {
    const float f[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
    const float ds[3] = { 0.35f, 0.6f, 0.9f };
    for (int i = 0; i < 3; ++i) {
        const float o[3] = { pos[0] + f[0] * ds[i], pos[1] + max_h + 0.25f, pos[2] + f[2] * ds[i] };
        const float down[3] = { 0.0f, -1.0f, 0.0f };
        k2::engine::pick_hit h;
        if (!w.pick(o, down, max_h + 0.25f - min_h, true, h)) continue;
        if (h.normal[1] < move_tuning::FLOOR_MIN_NY) continue;
        const float top = h.point[1];
        // The face: a horizontal ray just under the top.
        const float fo[3] = { pos[0], top - 0.1f, pos[2] };
        k2::engine::pick_hit fh;
        float edge_d = ds[i] - 0.15f, yaw_face = yaw;
        if (w.pick(fo, f, ds[i] + 0.3f, false, fh) && std::fabs(fh.normal[1]) < 0.5f) {
            edge_d = fh.distance;
            yaw_face = std::atan2(-fh.normal[0], -fh.normal[2]);
        }
        // Nothing solid between him and the face at his own height.
        const float wo[3] = { pos[0], pos[1] + 0.3f, pos[2] };
        k2::engine::pick_hit wh;
        if (top - pos[1] > 0.6f && w.pick(wo, f, edge_d - 0.05f, false, wh)) continue;
        out.top = top;
        out.facing = yaw_face;
        const float ff[3] = { std::sin(yaw_face), 0.0f, std::cos(yaw_face) };
        out.edge[0] = pos[0] + f[0] * edge_d;
        out.edge[1] = top;
        out.edge[2] = pos[2] + f[2] * edge_d;
        // Headroom to stand on top, a little past the edge.
        const float so[3] = { out.edge[0] + ff[0] * 0.35f, top + 0.05f, out.edge[2] + ff[2] * 0.35f };
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        k2::engine::pick_hit sh;
        out.can_stand = !w.pick(so, up, 1.9f, false, sh);
        // And room for his body there: no wall just past the edge at knee
        // or chest height (a crate pushed against a wall), and a floor
        // under the spot he ends on.
        for (int hi = 0; hi < 2 && out.can_stand; ++hi) {
            const float bo[3] = { out.edge[0], top + (hi ? 1.2f : 0.4f), out.edge[2] };
            k2::engine::pick_hit bh;
            if (w.pick(bo, ff, 0.35f + 0.3f, false, bh)) out.can_stand = false;
        }
        if (out.can_stand) {
            const float go[3] = { so[0], top + 0.3f, so[2] };
            k2::engine::pick_hit gh;
            if (!w.pick(go, down, 0.5f, true, gh) || gh.normal[1] < move_tuning::FLOOR_MIN_NY)
                out.can_stand = false;
        }
        return true;
    }
    return false;
}

struct hang_ctl {
    enum state_t { NONE, CLIMB, GRAB, HANG, SHIMMY, PULL_UP, PULL_FAIL } state;
    ledge_hit ledge;
    int       clip;
    double    t0;
    float     from[3], to[3];
    float     corr[3];      // CLIMB / PULL_UP: drift spread over the clip so it ends at `to`
    float     grip[3];      // grip point in the Prince's frame (x left, y up, z fwd)
    bool      have_grip;

    hang_ctl() : state(NONE), clip(-1), t0(0), have_grip(false) {
        for (int k = 0; k < 3; ++k) from[k] = to[k] = corr[k] = grip[k] = 0.0f;
    }
};


// Where the grip joints sit in the hang pose, relative to the root: pose
// LedgeHangBase_M at t = 0 and read LGrip/RGrip. Cached in `hc`.
void hang_measure_grip(hang_ctl& hc, actor_inst& a) {
    if (hc.have_grip || !a.loco_ok[LEDGE_HANG]) return;
    hc.have_grip = clip_grip(a, a.loco[LEDGE_HANG], 0, hc.grip);
}

// Root position that puts the grip on the ledge edge, facing the wall.
void hang_root(const hang_ctl& hc, float* out) {
    const float yaw = hc.ledge.facing;
    const float c = std::cos(yaw), s = std::sin(yaw);
    // set_yaw: model x -> (c, 0, -s), model z -> (s, 0, c)
    const float gx = c * hc.grip[0] + s * hc.grip[2];
    const float gz = -s * hc.grip[0] + c * hc.grip[2];
    out[0] = hc.ledge.edge[0] - gx;
    out[1] = hc.ledge.top - hc.grip[1];
    out[2] = hc.ledge.edge[2] - gz;
}

void hang_play(hang_ctl& hc, actor_inst& a, int c, double now, bool once) {
    hc.clip = c;
    set_clip(a, c);
    a.clip_once = once;
    a.clip_ms = 0.0;
    hc.t0 = now;
}

double hang_len(const actor_inst& a, int c) {
    return (c >= 0 && a.loco_ok[size_t(c)]) ? clip_length_ms(a.loco[size_t(c)]) / 1000.0 : 0.5;
}

// A hop or pull-up raises the body in the pose (the Prime joint), by the
// clip's own amount, not the ledge's. Where the clip's last frame would put
// the body is compared with standing at `to` in the base pose; the
// difference is spread over the clip, so the body meets the top as the clip
// ends and the switch to the base pose does not jump. Call once hc.from,
// hc.to and hc.clip are set.
void hang_begin_rise(hang_ctl& hc, actor_inst& a) {
    for (int k = 0; k < 3; ++k) hc.corr[k] = hc.to[k] - hc.from[k];
    if (hc.clip < 0 || !a.loco_ok[size_t(hc.clip)] || !a.loco_ok[LOCO_BASE]) return;
    const assets::skl_file& s = a.loco[size_t(hc.clip)];
    const uint32_t len = clip_length_ms(s);
    float pe[3], pb[3], d[3];
    if (!clip_joint(a, s, len, 1, pe) || !clip_joint(a, a.loco[LOCO_BASE], 0, 1, pb)) return;
    root_delta(s, len, hc.ledge.facing, d);
    const float c = std::cos(hc.ledge.facing), sn = std::sin(hc.ledge.facing);
    const float ox = pb[0] - pe[0], oz = pb[2] - pe[2];
    // set_yaw: model x -> (c, 0, -s), model z -> (s, 0, c)
    const float end[3] = { hc.to[0] + c * ox + sn * oz, hc.to[1] + pb[1] - pe[1], hc.to[2] - sn * ox + c * oz };
    hc.corr[0] = end[0] - (hc.from[0] + d[0]);
    hc.corr[1] = end[1] - hc.from[1];
    hc.corr[2] = end[2] - (hc.from[2] + d[2]);
}

// From standing: climb a low ledge (HopUpOnto*). True when it started.
bool hang_try_climb(hang_ctl& hc, actor_inst& a, const k2::engine::collision_world& w,
                    const float* pos, float& yaw, double now) {
    if (!a.loco_ok[HOP_SMALL]) return false;
    ledge_hit l;
    // The original hops onto anything up to 2 m (0x4B46C2); higher ledges
    // are jumped at and caught.
    if (!find_ledge(w, pos, yaw, 0.3f, 2.0f, l) || !l.can_stand) return false;
    const float dh = l.top - pos[1];
    const int c = dh > 1.75f ? HOP_2M : dh > 1.3f ? HOP_1_5M : dh > 0.9f ? HOP_1M : HOP_SMALL;
    hc.ledge = l;
    yaw = l.facing;
    const float ff[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
    // Start where the clip expects him: its root travel ends him 0.35 past
    // the edge, so he begins that travel short of it.
    float travel = 0.5f;
    if (a.loco_ok[size_t(c)]) {
        float d[3];
        if (clip_root_motion(a.loco[size_t(c)], d)) travel = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    }
    hc.from[0] = l.edge[0] - ff[0] * (travel - 0.35f);
    hc.from[1] = pos[1];
    hc.from[2] = l.edge[2] - ff[2] * (travel - 0.35f);
    hc.to[0] = l.edge[0] + ff[0] * 0.35f;
    hc.to[1] = l.top;
    hc.to[2] = l.edge[2] + ff[2] * 0.35f;
    hc.state = hang_ctl::CLIMB;
    hang_play(hc, a, c, now, true);
    hang_begin_rise(hc, a);
    std::printf("ledge: climb %.2f (%s)\n", dh, k_loco_names[c]);
    return true;
}

double skill_seconds(scene& sc, const actor_inst& a, const std::string& name);

// --- crouching and crawling ------------------------------------------------------
//
// The original's states (strings at 0x7CA8A8 on): StStartStandToCrouch
// (CrouchAntic_G), StCrouching (CrouchBase_G), StStartCrouchToCrawl /
// StStartCrawling (CrawlForward_L, CrawlBase_G at rest), StStartCrawlToCrouch
// (CrawlToCrouch_G), StStartCrouchToStand (CrouchToStand_G). C crouches
// (the manual's "Crouch - C"); held, he stays down, and moving forward
// crawls. He cannot stand where there is no room - that is what lets him
// through low gaps. The pivot clips and the original's CrawlCollider are
// not used: he turns as he crawls, and his wall probes drop to knee height.
struct crouch_ctl {
    enum state_t { NONE, DOWN, CROUCH, TO_CRAWL, CRAWL, TO_CROUCH, UP } state;
    double until;           // when the current transition clip ends
    crouch_ctl() : state(NONE), until(0) {}
};

void crouch_clip(scene& sc, actor_inst& a, const char* name, bool once) {
    const assets::skl_file* sk = find_actor_skill(sc, a, name);
    if (a.stance == sk) return;
    a.stance = sk;
    a.clip_ms = 0.0;
    a.clip_once = once;
}

// Room to stand: nothing solid within his height above the floor.
bool crouch_headroom(const k2::engine::collision_world& w, const float* pos) {
    const float o[3] = { pos[0], pos[1] + 0.1f, pos[2] };
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    k2::engine::pick_hit h;
    return !w.pick(o, up, 1.75f, false, h);
}

// One frame. `held` is the crouch key; `mv`/`ml` the world move input.
// True while crouched in any way: it owns the body.
bool crouch_update(crouch_ctl& cc, scene& sc, actor_inst& a, const k2::engine::collision_world& w,
                   float* pos, float& yaw, bool held, const float* mv, float ml, bool classic,
                   float in_x, float in_y, double now, float dt) {
    const bool moving = classic ? in_y > 0.2f : ml > 0.2f;
    switch (cc.state) {
    case crouch_ctl::NONE:
        if (!held) return false;
        crouch_clip(sc, a, "CrouchAntic_G", true);
        cc.until = now + skill_seconds(sc, a, "CrouchAntic_G");
        cc.state = crouch_ctl::DOWN;
        return true;
    case crouch_ctl::DOWN:
        if (now >= cc.until) { cc.state = crouch_ctl::CROUCH; crouch_clip(sc, a, "CrouchBase_G", false); }
        return true;
    case crouch_ctl::CROUCH:
        if (!held && crouch_headroom(w, pos)) {
            crouch_clip(sc, a, "CrouchToStand_G", true);
            cc.until = now + skill_seconds(sc, a, "CrouchToStand_G");
            cc.state = crouch_ctl::UP;
        } else if (moving) {
            crouch_clip(sc, a, "CrawlAntic_G", true);
            cc.until = now + skill_seconds(sc, a, "CrawlAntic_G");
            cc.state = crouch_ctl::TO_CRAWL;
        }
        return true;
    case crouch_ctl::TO_CRAWL:
        if (now >= cc.until) { cc.state = crouch_ctl::CRAWL; crouch_clip(sc, a, "CrawlBase_G", false); }
        return true;
    case crouch_ctl::CRAWL: {
        // Let go of crouch where there is room: back to a crouch, then up.
        if (!held && crouch_headroom(w, pos)) {
            crouch_clip(sc, a, "CrawlToCrouch_G", true);
            cc.until = now + skill_seconds(sc, a, "CrawlToCrouch_G");
            cc.state = crouch_ctl::TO_CROUCH;
            return true;
        }
        if (!moving) {
            crouch_clip(sc, a, "CrawlBase_G", false);
            if (classic && std::fabs(in_x) > 0.2f) yaw -= in_x * 1.6f * dt;
            return true;
        }
        crouch_clip(sc, a, "CrawlForward_L", false);
        // Modern: turn toward the stick, a crawl's worth at a time.
        if (!classic && ml > 1e-6f) {
            const float want = std::atan2(mv[0], mv[2]);
            float d = std::fmod(want - yaw + 3.14159265f, 6.2831853f);
            if (d < 0) d += 6.2831853f;
            d -= 3.14159265f;
            const float turn = 2.5f * dt;
            yaw += d > turn ? turn : d < -turn ? -turn : d;
        } else if (classic && std::fabs(in_x) > 0.2f) {
            yaw -= in_x * 1.6f * dt;
        }
        float speed = 0.8f;
        if (a.stance) { const float cs = clip_speed(*a.stance); if (cs > 0.1f) speed = cs; }
        const float step = speed * dt;
        move_horizontal(w, pos, std::sin(yaw) * step, std::cos(yaw) * step, true);
        float gy;
        if (ground_under(w, pos, gy)) pos[1] = gy;
        return true;
    }
    case crouch_ctl::TO_CROUCH:
        if (now >= cc.until) { cc.state = crouch_ctl::CROUCH; crouch_clip(sc, a, "CrouchBase_G", false); }
        return true;
    case crouch_ctl::UP:
        if (now >= cc.until) { cc.state = crouch_ctl::NONE; a.stance = 0; a.clip_once = false; return false; }
        return true;
    }
    return false;
}

// --- swimming -----------------------------------------------------------------
//
// Water is a Water trap: a flat sheet at the surface over the outline of its
// mesh; below it is water. The Prince swims once his feet are more than
// SWIM_ENTER under the surface (StStartSwimming, 0x4B8A7B):
// on the surface (SwimOnSurface_G / SwimAmbientOnSurface_G) or, after a dive
// (StStartSubmerging, 0x4B8FB0, SwimAmbientDive_G), under it
// (SwimUnderWater_G / SwimAmbientUnderWater_G) on one bottle of air.
// Depths, speeds and the air time are ours; the K2SwimActivity that holds
// the original's is not traced.
namespace swim_tuning {
// Prince__DeepEnoughToSwim (0x4D0D98): a joint (the pelvis, our reading)
// at least 1.8 - 1.0 + 0.05 = 0.85 under the surface; with the pelvis 1.03
// above the feet, water about 1.9 deep. Shallower water - the basins under
// the spouts and showers - is walked through.
const float  SWIM_ENTER = 0.85f + 1.03f;
const float  FLOAT      = 1.3f;     // feet under the surface while on it
const float  SPEED      = 1.6f;     // m/s when the clip carries no travel
const float  VSPEED     = 1.6f;     // m/s up or down, under water
const double AIR        = 20.0;     // seconds of air; the bottle shows 4 steps
const float  PITCH_SURFACE = 1.25f; // body tipped forward while stroking (radians)
const float  PITCH_UNDER   = 1.45f;
}

struct swim_ctl {
    enum state_t { NONE, SURFACE, UNDER } state;
    double air;                     // seconds left
    swim_ctl() : state(NONE), air(swim_tuning::AIR) {}
};

// The surface over `p`, if it is in water: the highest Water sheet above the
// feet whose area holds p.
bool water_surface(scene& sc, const float* p, float& surf) {
    bool found = false;
    for (size_t w = 0; w < sc.waters.size(); ++w) {
        const scene::water_area& wa = sc.waters[w];
        const scene_trap* t = dynamic_cast<const scene_trap*>(sc.traps[wa.trap]);
        if (!t) continue;
        const float* o = t->offset();
        const float x = p[0] - o[0], z = p[2] - o[2];
        if (x < wa.mn[0] || x > wa.mx[0] || z < wa.mn[1] || z > wa.mx[1]) continue;
        bool inside = false;
        for (size_t i = 0; i + 5 < wa.tris.size() && !inside; i += 6) {
            const float* tr = &wa.tris[i];
            float sgn[3];
            for (int e = 0; e < 3; ++e) {
                const float ax = tr[e * 2], az = tr[e * 2 + 1];
                const float bx = tr[((e + 1) % 3) * 2], bz = tr[((e + 1) % 3) * 2 + 1];
                sgn[e] = (bx - ax) * (z - az) - (bz - az) * (x - ax);
            }
            inside = (sgn[0] >= 0 && sgn[1] >= 0 && sgn[2] >= 0) || (sgn[0] <= 0 && sgn[1] <= 0 && sgn[2] <= 0);
        }
        if (!inside) continue;
        const float y = t->box[4] + o[1];
        if (y < p[1] - 0.05f) continue;
        if (!found || y > surf) surf = y;
        found = true;
    }
    return found;
}

// One frame in the water. `mv` is the world move direction (length `ml`),
// `up`/`down` held (jump, action), `jump` pressed. Returns false when he has
// left the water. `drowning` is set while out of air.
bool swim_update(swim_ctl& sw, scene& sc, actor_inst& a, float* pos, float& yaw, float surf,
                 const float* mv, float ml, bool up, bool down, bool dive, bool classic, float dt,
                 bool& drowning) {
    drowning = false;
    const bool under = sw.state == swim_ctl::UNDER;
    const char* clip = under ? (ml > 1e-3f ? "SwimUnderWater_G" : "SwimAmbientUnderWater_G")
                             : (ml > 1e-3f ? "SwimOnSurface_G" : "SwimAmbientOnSurface_G");
    a.stance = find_actor_skill(sc, a, clip);
    // The stroke clips are authored standing; swimming, he lies along the
    // way he goes (flatter under water), and treads water upright.
    {
        const float want = ml > 1e-3f ? (under ? swim_tuning::PITCH_UNDER : swim_tuning::PITCH_SURFACE) : 0.0f;
        const float k = std::min(1.0f, dt * 4.0f);
        a.body_pitch += (want - a.body_pitch) * k;
    }
    if (ml > 1e-3f) {
        float speed = swim_tuning::SPEED;
        if (a.stance) { const float cs = clip_speed(*a.stance); if (cs > 0.2f) speed = cs; }
        const float step = speed * dt * std::min(1.0f, ml);
        move_horizontal(sc.coll, pos, mv[0] / ml * step, mv[2] / ml * step);
        if (!classic) yaw = std::atan2(mv[0], mv[2]);
    }
    float gy = -1.0e9f;
    const float probe[3] = { pos[0], surf, pos[2] };
    k2::engine::pick_hit h;
    const float dn[3] = { 0.0f, -1.0f, 0.0f };
    if (sc.coll.pick(probe, dn, 100.0f, true, h)) gy = h.point[1];
    // Shallow enough to stand: walk out.
    if (gy > surf - swim_tuning::SWIM_ENTER) {
        sw.state = swim_ctl::NONE;
        a.stance = 0;
        a.body_pitch = 0.0f;
        pos[1] = gy;
        return false;
    }
    if (!under) {
        pos[1] = surf - swim_tuning::FLOAT;
        sw.air = swim_tuning::AIR;
        if (dive && gy < surf - swim_tuning::FLOAT - 1.0f) {
            sw.state = swim_ctl::UNDER;
            std::printf("swim: dive\n");
        }
        return true;
    }
    float vy = (up ? swim_tuning::VSPEED : 0.0f) - (down ? swim_tuning::VSPEED : 0.0f);
    if (!up && !down) vy = 0.3f;                    // drifts up
    pos[1] += vy * dt;
    if (pos[1] < gy + 0.2f) pos[1] = gy + 0.2f;
    if (pos[1] >= surf - swim_tuning::FLOAT) {
        pos[1] = surf - swim_tuning::FLOAT;
        sw.state = swim_ctl::SURFACE;
        std::printf("swim: surfaced\n");
        return true;
    }
    sw.air -= dt;
    if (sw.air <= 0.0) { sw.air = 0.0; drowning = true; }
    return true;
}

// In the air: catch a ledge the hands can reach. True when it grabbed.
bool hang_try_grab(hang_ctl& hc, actor_inst& a, const k2::engine::collision_world& w,
                   const float* pos, float yaw, float vy, float horiz_speed, double now) {
    hang_measure_grip(hc, a);
    if (!hc.have_grip || vy > 1.5f) return false;
    ledge_hit l;
    // Hands reach about grip[1] above the feet; take ledges near that height.
    if (!find_ledge(w, pos, yaw, hc.grip[1] - 0.6f, hc.grip[1] + 0.35f, l)) return false;
    // Too low to hang from: his feet would go through the floor under him.
    {
        const float o[3] = { pos[0], pos[1] + 0.05f, pos[2] };
        const float down[3] = { 0.0f, -1.0f, 0.0f };
        k2::engine::pick_hit h;
        if (w.pick(o, down, 3.0f, true, h) && l.top - hc.grip[1] < h.point[1] + 0.3f) return false;
    }
    hc.ledge = l;
    for (int k = 0; k < 3; ++k) hc.from[k] = pos[k];
    hc.state = hang_ctl::GRAB;
    hang_play(hc, a, horiz_speed > 1.0f ? LEDGE_GRAB_HORIZ : LEDGE_GRAB_VERT, now, true);
    std::printf("ledge: grab at %.2f\n", l.top);
    return true;
}

// One frame while on a ledge. `local` is the input in the Prince's frame
// (x left, z toward the wall). Returns true while it owns the body.
// `drop` is set when he lets go.
bool hang_update(hang_ctl& hc, actor_inst& a, const k2::engine::collision_world& w,
                 float* pos, float& yaw, double now, const float* local, bool jump, bool& drop) {
    drop = false;
    if (hc.state == hang_ctl::NONE) return false;
    const double el = now - hc.t0;
    const double len = hang_len(a, hc.clip);
    const double u = len > 0 ? std::min(1.0, el / len) : 1.0;

    switch (hc.state) {
    case hang_ctl::CLIMB:
    case hang_ctl::PULL_UP: {
        // The clip does the climbing: its Prime joint rises the 0.75 .. 1.93
        // of the hop (or pull-up), so the body follows the root's travel
        // forward plus hc.corr, the gap between the clip's rise and the
        // ledge's (hang_begin_rise). When the clip ends he stands at `to`
        // as the pose returns to base.
        float d[3];
        const assets::skl_file& sk = a.loco[size_t(hc.clip)];
        root_delta(sk, uint32_t(std::min(len, el) * 1000.0), hc.ledge.facing, d);
        const float f = float(u);
        pos[0] = hc.from[0] + d[0] + hc.corr[0] * f;
        pos[1] = hc.from[1] + hc.corr[1] * f;
        pos[2] = hc.from[2] + d[2] + hc.corr[2] * f;
        yaw = hc.ledge.facing;
        if (u >= 1.0) {
            for (int k = 0; k < 3; ++k) pos[k] = hc.to[k];
            hc.state = hang_ctl::NONE;
            set_clip(a, LOCO_BASE);
            return false;
        }
        return true;
    }
    case hang_ctl::GRAB:
    case hang_ctl::PULL_FAIL: {
        // Catching: blend from where the hands met the edge into the hang
        // over the first fifth of the clip.
        float r[3];
        hang_root(hc, r);
        const double b = std::min(1.0, u / 0.2);
        for (int k = 0; k < 3; ++k) pos[k] = float(hc.from[k] + (r[k] - hc.from[k]) * b);
        yaw = hc.ledge.facing;
        if (u >= 1.0) { hc.state = hang_ctl::HANG; hang_play(hc, a, LEDGE_HANG, now, false); }
        return true;
    }
    case hang_ctl::SHIMMY: {
        float r[3];
        hang_root(hc, r);
        pos[0] = float(hc.from[0] + (r[0] - hc.from[0]) * u);
        pos[1] = r[1];
        pos[2] = float(hc.from[2] + (r[2] - hc.from[2]) * u);
        yaw = hc.ledge.facing;
        if (u >= 1.0) { hc.state = hang_ctl::HANG; hang_play(hc, a, LEDGE_HANG, now, false); }
        return true;
    }
    case hang_ctl::HANG: {
        hang_root(hc, pos);
        yaw = hc.ledge.facing;
        if (local[2] > 0.5f || jump) {
            if (hc.ledge.can_stand) {
                const float ff[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
                for (int k = 0; k < 3; ++k) hc.from[k] = pos[k];
                hc.to[0] = hc.ledge.edge[0] + ff[0] * 0.35f;
                hc.to[1] = hc.ledge.top;
                hc.to[2] = hc.ledge.edge[2] + ff[2] * 0.35f;
                hc.state = hang_ctl::PULL_UP;
                hang_play(hc, a, LEDGE_PULL_UP, now, true);
                hang_begin_rise(hc, a);
            } else {
                hc.state = hang_ctl::PULL_FAIL;
                hang_play(hc, a, LEDGE_PULL_FAIL, now, true);
            }
        } else if (local[2] < -0.5f) {
            hc.state = hang_ctl::NONE;
            drop = true;
            return false;
        } else if (std::fabs(local[0]) > 0.5f) {
            // Shimmy 0.6 (the clips' root motion) if the ledge goes on there.
            const float sgn = local[0] > 0 ? 1.0f : -1.0f;     // + is his left
            const float side[3] = { std::cos(yaw), 0.0f, -std::sin(yaw) };
            float probe[3];
            for (int k = 0; k < 3; ++k) probe[k] = pos[k] + side[k] * sgn * 0.6f;
            const float back[3] = { -std::sin(yaw) * 0.5f, 0.0f, -std::cos(yaw) * 0.5f };
            for (int k = 0; k < 3; ++k) probe[k] += back[k];
            ledge_hit l;
            const float rel = hc.ledge.top - probe[1];
            if (find_ledge(w, probe, yaw, rel - 0.15f, rel + 0.15f, l)) {
                for (int k = 0; k < 3; ++k) hc.from[k] = pos[k];
                hc.ledge = l;
                hc.ledge.facing = yaw;
                hc.state = hang_ctl::SHIMMY;
                hang_play(hc, a, sgn > 0 ? LEDGE_MOVE_LEFT : LEDGE_MOVE_RIGHT, now, true);
            }
        }
        return true;
    }
    default:
        return false;
    }
}

// --- combat -------------------------------------------------------------------
//
// From the data: fight.ini's attack-begin times (when a swing can still be
// blocked, then lands), recover times and the unblocked-attack window;
// enemy.dat's distances (watch 10, charge 5, give up 8 from home, vanish 20)
// and peripheral vision (30 degrees); the clip sets (Prince _G2 with the
// sword, guards _G1). Not in the shipped files and so ours: health (100),
// damage per hit (12, and enemy.dat's 15 for a running swing is unused),
// attack cadence, guard block chance. The original's CharacterStateTable /
// EnemyAIObject logic is not traced.
namespace combat_tuning {
const float REACH         = 2.2f;   // a swing connects within this
const float FIGHT_DIST    = 1.6f;   // guards stop and fight here
const int   HIT_DAMAGE    = 12;       // a bottle; Arrow::kArrowNormalDamage is 12 too
// Health is counted in thirds of a bottle, 4 each (sub_422A5F scales
// thirds by 4: 30 thirds, ten bottles, make 120), so a bottle is 12.
const int   BOTTLE        = 12;
const int   PRINCE_BOTTLES = 3;     // what the Prince starts the game with
// Guards' bottles by skill (argv1 "skill,weapon,bottles"), word_7C5820.
const int   SKILL_BOTTLES[11] = { 2, 3, 3, 4, 5, 5, 6, 7, 8, 10, 10 };
const double RESPAWN_TIME = 3.5;
}

enum attack_dir { DIR_LEFT = 0, DIR_CENTER, DIR_RIGHT };
const char* const k_dir_names[3] = { "Left", "Center", "Right" };

// fight.ini "Skill Level <n> <what>", for a guard's skill.
double skill_param(const k2::engine::fight_params& fp, int skill, const char* what, double fallback) {
    char key[64];
    std::snprintf(key, sizeof(key), "Skill Level %d %s", std::max(1, std::min(10, skill)), what);
    return fp.fight(key, fallback);
}

double frand() { return double(std::rand() % 10000) / 10000.0; }

// Sword damage by direction ("Sword Left Attack Damage" = 14, ...).
int sword_damage(const k2::engine::fight_params& fp, int dir) {
    return int(fp.fight(std::string("Sword ") + k_dir_names[dir] + " Attack Damage", 12.0));
}

struct pending_hit {
    bool   active;
    double at;              // when it lands
    int    dir;
    int    attacker;        // actor index; -1 the Prince
    // What kind of swing (Fight__AttackParams 0x4AB7F0's types): a plain
    // attack, a counter (9/10), a follow (6..8), or a feint, which lands
    // nothing. `dmg` < 0 takes the attack's own damage.
    enum kind_t { ATTACK, COUNTER, FOLLOW, FEINT } kind = ATTACK;
    int    dmg = -1;
};

// Fight helpers from docs/reference/combat-moves.md.
// L -> C -> R -> L (Fight__FollowNextDir 0x4AE8DE), and the follow clip.
inline int follow_next(int dir) { return (dir + 1) % 3; }
inline const char* follow_clip(int from) {
    return from == 0 ? "CombatFollowLeftToCenter" : from == 1 ? "CombatFollowCenterToRight" : "CombatFollowRightToLeft";
}

struct enemy_ai {
    enum state_t { PATROL, CHARGE, FIGHT, RECOVER, DEAD, DYING } state;
    int    actor;
    float  home[3];
    std::vector<std::array<float, 3> > way;
    size_t wp;
    double next_attack;
    double busy_until;
    pending_hit hit;
    std::string set;        // clip suffix: "_G1"
    std::string hurt_set;   // "_L1"
    // EnemyStart argv4 (+0x11C): run when he falls ("P4_gatenew,move,...").
    std::string on_death;
    // argv5, the "start" skill (0x4A8B4E): "*Name" loops as his idle;
    // otherwise he holds its first frame, waiting, until "Start" (actor flag
    // 0x1000, 0x46D9C4) or he sees the Prince, then plays it and takes over.
    std::string start_skill;
    bool        start_loops;
    bool        waiting;
    // fight.ini "Skill Level N ..." (1..10), and his answer to the Prince's
    // swing in flight: rolled from Blocks / Wrong Blocks / Did Nothings /
    // Late Attacks when the swing starts, acted on after his block delay.
    int         skill;
    enum react_t { R_NONE, R_BLOCK, R_WRONG, R_LATE } react;
    double      react_at;
    // An archer's round: draw, aim, loose, and the wait before the next.
    int         shot_stage = 0;
    double      shot_at = 0.0;
    // Dying (fight state 12): until the clip ends, a finisher can take him.
    double      dying_until = 0.0;
    bool        last_feint = false;     // never two feints in a row (fight+0x130)
    bool        taunted = false;        // CombatTaunt on first engaging
};

struct combat_state {
    std::vector<enemy_ai> enemies;
    const k2::engine::fight_params* params;
    bool                  prince_drawn;
    bool                  prince_blocking;
    pending_hit           prince_hit;
    double                prince_busy_until;
    double                dead_since;     // < 0 while alive
    float                 spawn[3];
    float                 spawn_yaw;
    // The Prince's block points one way, chosen as he raises it
    // (Fight__StartBlock 0x4AC758): the attacker's direction, else random.
    int                   prince_block_dir = 1;
    bool                  prince_was_blocking = false;
    // After he blocks a left or right swing: a counter on that side is open
    // for Counter Attack Window (0x4ACFD3).
    double                counter_until = -1.0;
    int                   counter_dir = 0;
    // After his swing lands unblocked: the next direction within +-Follow
    // Window/2 of it is a follow (an early press is kept).
    int                   follow_from = -1;
    double                follow_lo = 0.0, follow_hi = -1.0;
    double                press_at = -1.0;
    int                   press_dir = 0;
    // Dying (a blow that would leave him 1 or less): helpless until then.
    double                prince_dying_until = -1.0;
    double                taunt_at = -1.0;   // CombatTaunt after the draw
    // A press that could not start yet waits for him to be ready
    // (fight+0x12C, Fight__RunQueuedRequest 0x4AC7D6). The 1 s it keeps
    // is ours.
    double                queued_at = -1.0;
    int                   queued_dir = 0;
    bool                  queued_feint = false;
};

float yaw_toward(const float* from, const float* to) {
    return std::atan2(to[0] - from[0], to[2] - from[2]);
}

float actor_yaw(const actor_inst& a) {
    return std::atan2(a.node->local.rotate[2], a.node->local.rotate[8]);
}

float angle_diff(float a, float b) {
    float d = std::fmod(a - b + 3.14159265f, 6.2831853f);
    if (d < 0) d += 6.2831853f;
    return std::fabs(d - 3.14159265f);
}

double skill_seconds(scene& sc, const actor_inst& a, const std::string& name) {
    const assets::skl_file* sk = find_actor_skill(sc, a, name);
    return sk ? clip_length_ms(*sk) / 1000.0 : 0.5;
}

void combat_init(combat_state& cs, scene& sc, const k2::engine::fight_params& fp) {
    cs.params = &fp;
    cs.prince_drawn = cs.prince_blocking = false;
    cs.prince_hit.active = false;
    cs.prince_busy_until = 0.0;
    cs.dead_since = -1.0;
    cs.enemies.clear();
    for (size_t i = 0; i < sc.actors.size(); ++i) {
        actor_inst& a = sc.actors[i];
        if (a.is_fixture) continue;
        if (!a.is_enemy) {
            a.max_health = a.health = combat_tuning::PRINCE_BOTTLES * combat_tuning::BOTTLE;
            for (int k = 0; k < 3; ++k) cs.spawn[k] = a.node->local.translate[k];
            cs.spawn_yaw = actor_yaw(a);
            continue;
        }
        if (!a.skinned) continue;
        enemy_ai e;
        e.state = enemy_ai::PATROL;
        e.actor = int(i);
        for (int k = 0; k < 3; ++k) e.home[k] = a.node->local.translate[k];
        e.wp = 0;
        e.next_attack = e.busy_until = 0.0;
        e.hit.active = false;
        e.set = "_G1";
        e.hurt_set = "_L1";
        e.start_loops = false;
        e.waiting = false;
        e.skill = 1;
        e.react = enemy_ai::R_NONE;
        e.react_at = 0.0;
        // EnemyStart argv: body, "difficulty,weapon,health", waypoints, ...
        for (size_t t = 0; t < sc.spawn_points.size(); ++t) {
            const assets::k2a_trap& sp = sc.spawn_points[t];
            if (sp.node != a.script_name) continue;
            if (sp.argv.size() > 1) {
                // EnemyStart argv1, read at 0x4A92D7: skill (0..10, else 4),
                // weapon, bottles (else by skill). Normal difficulty.
                const std::string& a1 = sp.argv[1];
                int skill = std::atoi(a1.c_str());
                if (skill < 0 || skill > 10) skill = 4;
                e.skill = std::min(10, skill + 1);   // index 0 is "Skill Level 1"
                int bottles = combat_tuning::SKILL_BOTTLES[skill];
                size_t c1 = a1.find(',');
                size_t c2 = c1 == std::string::npos ? c1 : a1.find(',', c1 + 1);
                if (c2 != std::string::npos && std::atoi(a1.c_str() + c2 + 1) > 0)
                    bottles = std::atoi(a1.c_str() + c2 + 1);
                a.max_health = a.health = bottles * combat_tuning::BOTTLE;
            }
            if (sp.argv.size() > 4) e.on_death = sp.argv[4];
            if (sp.argv.size() > 5 && !sp.argv[5].empty()) {
                e.start_loops = sp.argv[5][0] == '*';
                e.start_skill = e.start_loops ? sp.argv[5].substr(1) : sp.argv[5];
                if (!e.start_loops && play_once(sc, a, e.start_skill, 0.0, actor_yaw(a), true)) {
                    a.scripted_start = 1.0e9;       // held at its first frame until woken
                    e.waiting = true;
                }
            }
            if (sp.argv.size() > 2) {
                std::string list = sp.argv[2];
                size_t st = 0;
                while (st < list.size()) {
                    size_t c = list.find(',', st);
                    std::string nm = list.substr(st, c == std::string::npos ? std::string::npos : c - st);
                    for (size_t q = 0; q < nm.size(); ++q)
                        if (nm[q] >= 'A' && nm[q] <= 'Z') nm[q] = char(nm[q] - 'A' + 'a');
                    std::map<std::string, scene::named_point>::const_iterator it = sc.points.find(nm);
                    if (it != sc.points.end()) {
                        std::array<float, 3> w = {{ it->second.pos[0], it->second.pos[1], it->second.pos[2] }};
                        e.way.push_back(w);
                    }
                    if (c == std::string::npos) break;
                    st = c + 1;
                }
            }
        }
        cs.enemies.push_back(e);
    }
}

// `death` names the clip he dies with when this blow kills him (drowning:
// DeathUnderwater_G); otherwise the generic one.
void prince_damage(combat_state& cs, scene& sc, actor_inst& p, int dmg, int dir, double now,
                   const char* death = 0) {
    if (cs.dead_since >= 0.0) return;
    if (dmg > 0 && potion_state::on(g_potions.shield_until, now)) {       // Shield potion
        play_sound("gblswordclank", p.node->local.translate, 14.0f, false);
        return;
    }
    p.health -= dmg;
    play_sound("GBLpfiteinjur", p.node->local.translate, 14.0f, false);
    if (p.health <= 0) {
        p.health = 0;
        cs.dead_since = now;
        p.stance = 0;
        if (death && play_once(sc, p, death, now, actor_yaw(p), true)) {}
        else if (!play_once(sc, p, p.armed ? "CombatDeathGeneric_G2" : "DeathGeneric_G", now, actor_yaw(p), true))
            play_once(sc, p, "CombatDeathGeneric_G1", now, actor_yaw(p), true);
        std::printf("combat: the Prince falls\n");
        return;
    }
    play_once(sc, p, std::string("CombatBeingInjured") + k_dir_names[dir] + "_L2", now, actor_yaw(p), false);
    cs.prince_busy_until = now + std::max(skill_seconds(sc, p, std::string("CombatBeingInjured") + k_dir_names[dir] + "_L2"),
                                          cs.params->fight("Sword Recover From Being Injured", 0.267));
}

// Specials traps (spikes in a pit floor, blades): the Prince inside the
// actor's Trigger volume sets off Strike; from its '#kill#' event (66 ms)
// until Reset's '#safe#' the Kill volume is deadly, and he dies there with
// DeathSpikes_G. The original drives this from the clips' events through
// the trap's own state (Specials, 0x4896B1); the 2 s before it resets once
// he is clear of the trigger is ours.
void specials_update(scene& sc, combat_state& cs, actor_inst& prince, double now) {
    const float* p = prince.node->local.translate;
    for (size_t i = 0; i < sc.specials.size(); ++i) {
        scene::special& sp = sc.specials[i];
        if (sp.actor < 0 || !sp.have_trig) continue;
        actor_inst& a = sc.actors[size_t(sp.actor)];
        const float dx = p[0] - a.home[0], dy = p[1] - a.home[1], dz = p[2] - a.home[2];
        const float c = std::cos(a.home_yaw), sn = std::sin(a.home_yaw);
        // Inverse of set_yaw: world -> actor space.
        const float l[3] = { c * dx - sn * dz, dy, sn * dx + c * dz };
        const bool in_trig = l[0] >= sp.trig[0] && l[0] <= sp.trig[3] && l[1] >= sp.trig[1] - 0.3f &&
                             l[1] <= sp.trig[4] && l[2] >= sp.trig[2] && l[2] <= sp.trig[5];
        // The kill slab is 0.21 high at the floor: his feet in it, his
        // body's 0.3 radius counted (the pit's slabs leave gaps narrower
        // than he is).
        const float R = 0.3f;
        const bool in_kill = sp.have_kill && l[0] >= sp.kill[0] - R && l[0] <= sp.kill[3] + R &&
                             l[1] >= sp.kill[1] - 0.3f && l[1] <= sp.kill[4] + 0.3f &&
                             l[2] >= sp.kill[2] - R && l[2] <= sp.kill[5] + R;
        if (sp.struck_at < 0.0) {
            if (sp.safe_at >= 0.0 && now < sp.safe_at) continue;
            if (!in_trig) continue;
            sp.struck_at = now;
            sp.safe_at = -1.0;
            play_once(sc, a, "Strike", now, a.home_yaw, true);
            std::printf("specials: %s strikes\n", a.name.c_str());
            continue;
        }
        const double t = now - sp.struck_at;
        if (t >= 0.066 && in_kill && cs.dead_since < 0.0) {
            prince_damage(cs, sc, prince, prince.health, 1, now, sp.death.c_str());
            std::printf("specials: %s kills the Prince\n", a.name.c_str());
        }
        if (t > 2.0 && !in_trig && cs.dead_since < 0.0) {
            play_once(sc, a, "Reset", now, a.home_yaw, true);
            sp.struck_at = -1.0;
            sp.safe_at = now + 0.066;
        }
    }
}

// A fight blow to the Prince: as for guards, one that would leave him 1 or
// less leaves 1 and he goes Dying; a blow while dying kills.
void prince_fight_damage(combat_state& cs, scene& sc, actor_inst& p, int dmg, int dir, double now) {
    if (cs.dead_since >= 0.0) return;
    if (cs.prince_dying_until >= now) { prince_damage(cs, sc, p, p.health, dir, now); cs.prince_dying_until = -1.0; return; }
    if (!potion_state::on(g_potions.shield_until, now) && p.health - dmg <= 1) {
        p.health = 1;
        p.stance = 0;
        play_once(sc, p, "CombatDying_G2", now, actor_yaw(p), true);
        cs.prince_dying_until = now + std::max(1.0, skill_seconds(sc, p, "CombatDying_G2"));
        cs.prince_busy_until = cs.prince_dying_until;
        cs.prince_hit.active = false;
        play_sound("GBLpfiteinjur", p.node->local.translate, 14.0f, false);
        std::printf("combat: the Prince is dying\n");
        return;
    }
    prince_damage(cs, sc, p, dmg, dir, now);
}

// Finishing move (Fight__TryFinishingMove 0x4AE6C5): the two are put
// g_fight_FinishingDistance apart (1.85 with swords), facing; the finisher
// plays CombatFinishingMove<N>, the dying CombatFinishingReceive<N> and is
// dead. N is the finisher's weapon + 1 (guard sword 1, Prince sword 2); the
// per-enemy numbers at actor+0x1C0 are not traced.
void finishing_move(scene& sc, actor_inst& who, const std::string& move,
                    actor_inst& victim, const std::string& receive, double now) {
    float* v = victim.node->local.translate;
    const float* w = who.node->local.translate;
    float dx = v[0] - w[0], dz = v[2] - w[2];
    const float l = std::sqrt(dx * dx + dz * dz);
    if (l > 1e-4f) { dx /= l; dz /= l; } else { dx = 0.0f; dz = 1.0f; }
    v[0] = w[0] + dx * 1.85f;
    v[2] = w[2] + dz * 1.85f;
    const float yw = std::atan2(dx, dz);
    who.node->local.set_yaw(yw);
    victim.node->local.set_yaw(yw + 3.14159265f);
    who.stance = victim.stance = 0;
    play_once(sc, who, move, now, yw, false);
    if (!play_once(sc, victim, receive, now, yw + 3.14159265f, true)) victim.posed_hold = true;
}

void enemy_killed(scene& sc, enemy_ai& e) {
    actor_inst& a = sc.actors[size_t(e.actor)];
    e.state = enemy_ai::DEAD;
    a.health = 0;
    a.stance = 0;
    std::printf("combat: %s is defeated\n", a.script_name.c_str());
    if (!e.on_death.empty()) sc.script.execute(e.on_death, 0);
}

// `sword`: a fight blow (Fight__TakeDamage 0x4ADBA0), which never takes the
// last point: one that would leave 1 or less leaves exactly 1 and he goes
// Dying (0x4ADC0F, 0x4AD63B). A blow to the dying kills.
void enemy_damage(combat_state& cs, scene& sc, enemy_ai& e, int dmg, int dir, double now, bool sword = true) {
    actor_inst& a = sc.actors[size_t(e.actor)];
    play_sound("gblswordswish", a.node->local.translate, 14.0f, false);
    if (sword && e.state == enemy_ai::DYING) {
        enemy_killed(sc, e);
        if (!play_once(sc, a, "CombatDeathGeneric" + e.set, now, actor_yaw(a), true)) a.posed_hold = true;
        return;
    }
    if (sword && a.health - dmg <= 1) {
        a.health = 1;
        e.state = enemy_ai::DYING;
        e.hit.active = false;
        a.stance = 0;
        play_once(sc, a, "CombatDying" + e.set, now, actor_yaw(a), true);
        e.dying_until = now + std::max(1.0, skill_seconds(sc, a, "CombatDying" + e.set));
        std::printf("combat: %s is dying\n", a.script_name.c_str());
        return;
    }
    a.health -= dmg;
    if (a.health <= 0) {
        e.state = enemy_ai::DEAD;
        a.stance = 0;
        play_once(sc, a, "CombatDying" + e.set, now, actor_yaw(a), true);
        std::printf("combat: %s is defeated\n", a.script_name.c_str());
        if (!e.on_death.empty()) sc.script.execute(e.on_death, 0);
        return;
    }
    const std::string hurt = std::string("CombatBeingInjured") + k_dir_names[dir] + e.hurt_set;
    play_once(sc, a, hurt, now, actor_yaw(a), false);
    e.busy_until = now + skill_seconds(sc, a, hurt);
    e.hit.active = false;
}

// Moves an actor toward `to` at its walk or run clip's speed, on the floor.
void steer(scene& sc, actor_inst& a, const float* to, bool run, float dt) {
    const int clip = run ? LOCO_RUN : LOCO_WALK;
    a.stance = 0;
    set_clip(a, clip);
    float* p = a.node->local.translate;
    const float dx = to[0] - p[0], dz = to[2] - p[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-4f) return;
    float speed = run ? 4.5f : 1.6f;
    if (a.loco_ok.size() > size_t(clip) && a.loco_ok[size_t(clip)]) {
        const float cs = clip_speed(a.loco[size_t(clip)]);
        if (cs > 0.0f) speed = cs;
    }
    const float step = std::min(d, speed * dt);
    move_horizontal(sc.coll, p, dx / d * step, dz / d * step);
    a.node->local.set_yaw(std::atan2(dx, dz));
}

// An archer (PG3Archer, A2ARCHER, D4Archer) keeps his distance: with the
// Prince in sight and in range he turns to him, draws (BowDrawArrow_G),
// aims (BowAim_G) and looses (BowFireArrow_G), then waits. The ranges and
// the pace are ours; enemy.dat's shooter table (LittleBilly..RobinHood) is
// not traced, and skill only shortens the wait here.
namespace archer_tuning {
const float  RANGE = 40.0f;
const double AIM = 0.6, WAIT = 3.0;
}
void archer_update(scene& sc, enemy_ai& e, actor_inst& a, const actor_inst& prince,
                   bool prince_available, double now) {
    float* p = a.node->local.translate;
    const float* q = prince.node->local.translate;
    const float eye[3] = { p[0], p[1] + 1.5f, p[2] };
    const float chest[3] = { q[0], q[1] + 1.2f, q[2] };
    float d[3] = { chest[0] - eye[0], chest[1] - eye[1], chest[2] - eye[2] };
    const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    bool sees = prince_available && dist < archer_tuning::RANGE && dist > 0.5f;
    if (sees) {
        for (int k = 0; k < 3; ++k) d[k] /= dist;
        k2::engine::pick_hit h;
        if (sc.coll.pick(eye, d, dist - 0.5f, false, h)) sees = false;
    }
    if (a.scripted) return;                         // a clip of the round is playing
    if (!sees) {
        e.shot_stage = 0;
        a.stance = 0;
        set_clip(a, LOCO_BASE);
        return;
    }
    const float yaw = yaw_toward(p, q);
    a.node->local.set_yaw(yaw);
    switch (e.shot_stage) {
    case 0:
        a.stance = find_actor_skill(sc, a, "BowDrawBase_G");
        if (now < e.shot_at) return;
        if (play_once(sc, a, "BowDrawArrow_G", now, yaw, false)) {
            play_sound("MULarrowdraw", eye, 14.0f, false);
            e.shot_stage = 1;
        }
        return;
    case 1:
        a.stance = find_actor_skill(sc, a, "BowAim_G");
        e.shot_at = now + archer_tuning::AIM;
        e.shot_stage = 2;
        return;
    default: {
        if (now < e.shot_at) return;
        play_once(sc, a, "BowFireArrow_G", now, yaw, false);
        // A little off, less so the better he is.
        const float miss = 0.6f / float(std::max(1, e.skill));
        const float to[3] = { chest[0] + miss * float(std::rand() % 200 - 100) / 100.0f, chest[1],
                              chest[2] + miss * float(std::rand() % 200 - 100) / 100.0f };
        const float from[3] = { eye[0] + d[0] * 0.6f, eye[1], eye[2] + d[2] * 0.6f };
        fire_arrow(from, to, false, now);
        e.shot_at = now + archer_tuning::WAIT - 0.15 * double(std::min(10, e.skill));
        e.shot_stage = 0;
        return;
    }
    }
}

void enemy_update(combat_state& cs, scene& sc, enemy_ai& e, actor_inst& prince,
                  bool prince_available, double now, float dt) {
    actor_inst& a = sc.actors[size_t(e.actor)];
    if (e.state == enemy_ai::DEAD || a.hidden) return;
    if (a.health <= 0) {               // killed by a script ("SkillAt:...,DEAD")
        e.state = enemy_ai::DEAD;
        std::printf("combat: %s is defeated\n", a.script_name.c_str());
        if (!e.on_death.empty()) sc.script.execute(e.on_death, 0);
        return;
    }
    if (e.state == enemy_ai::DYING) {
        if (now < e.dying_until) return;
        // No finisher came (0x4AD7CB): skill levels 7-10 get up with 1;
        // the rest die.
        if (e.skill >= 7) {
            e.state = enemy_ai::FIGHT;
            a.posed_hold = a.hold_last = false;
            a.scripted = 0;
            set_clip(a, LOCO_BASE);
            std::printf("combat: %s gets up\n", a.script_name.c_str());
        } else {
            enemy_killed(sc, e);
        }
        return;
    }
    const k2::engine::fight_params& fp = *cs.params;
    if (e.waiting) {
        // Asleep, or braced behind a door: wakes on "Start", on being hit,
        // or when the Prince comes close.
        const float* q = a.node->local.translate;
        const float* r = prince.node->local.translate;
        const float wx = r[0] - q[0], wz = r[2] - q[2];
        bool near = prince_available && std::fabs(r[1] - q[1]) < 1.5f &&
                    wx * wx + wz * wz < 9.0f;
        // An archer wakes to the Prince anywhere he can shoot him (ours).
        if (a.archer && prince_available && !near) {
            const float eye[3] = { q[0], q[1] + 1.5f, q[2] };
            float d[3] = { r[0] - eye[0], r[1] + 1.2f - eye[1], r[2] - eye[2] };
            const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l < archer_tuning::RANGE && l > 0.5f) {
                for (int k = 0; k < 3; ++k) d[k] /= l;
                k2::engine::pick_hit h;
                near = !sc.coll.pick(eye, d, l - 0.5f, false, h);
            }
        }
        if (!a.start_flag && !near && a.health == a.max_health) return;
        e.waiting = false;
        a.scripted_start = now;
        a.hold_last = false;
        a.posed_hold = false;
        e.busy_until = now + skill_seconds(sc, a, e.start_skill);
        std::printf("combat: %s starts (%s)\n", a.script_name.c_str(), e.start_skill.c_str());
        return;
    }
    if (a.archer) { archer_update(sc, e, a, prince, prince_available, now); return; }
    float* p = a.node->local.translate;
    const float* pp = prince.node->local.translate;
    const float dx = pp[0] - p[0], dz = pp[2] - p[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    const bool same_floor = std::fabs(pp[1] - p[1]) < 1.5f;

    // A swing in flight lands at its time, blocked or not. A feint lands
    // nothing.
    if (e.hit.active && now >= e.hit.at) {
        e.hit.active = false;
        const bool in_front = angle_diff(yaw_toward(p, pp), actor_yaw(a)) < 1.2f;
        if (e.hit.kind != pending_hit::FEINT && prince_available && same_floor &&
            d <= combat_tuning::REACH && in_front) {
            const int dir = e.hit.dir;
            // Blocked only by a block the same way (0x4ACE54), or the Guard potion.
            if ((cs.prince_blocking && cs.prince_block_dir == dir) || potion_state::on(g_potions.guard_until, now)) {
                play_sound("gblswordclank", p, 14.0f, false);
                const std::string rec = std::string("CombatBlocked") + k_dir_names[dir] + e.set;
                play_once(sc, a, rec, now, actor_yaw(a), false);
                e.busy_until = now + std::max(skill_seconds(sc, a, rec),
                    fp.fight(std::string("Sword Blocked ") + k_dir_names[dir] + " Attack Recover", 0.37));
                // A left or right swing blocked opens the Prince's counter;
                // the guard stays open until the window closes.
                if (dir != DIR_CENTER && e.hit.kind == pending_hit::ATTACK) {
                    cs.counter_until = now + fp.fight("Counter Attack Window", 0.333);
                    cs.counter_dir = dir;
                    e.busy_until = std::max(e.busy_until, cs.counter_until);
                }
            } else {
                const int dmg = e.hit.dmg >= 0 ? e.hit.dmg : sword_damage(fp, dir);
                prince_fight_damage(cs, sc, prince, dmg, dir, now);
                // His swing landed: a follow to the next side, by skill
                // (Follow Attacks out of Follow Attack Opportunities, 0x4AD3B8).
                if (e.hit.kind == pending_hit::ATTACK && cs.dead_since < 0.0 && cs.prince_dying_until < now &&
                    frand() * skill_param(fp, e.skill, "Follow Attack Opportunities", 100) <
                        skill_param(fp, e.skill, "Follow Attacks", 5)) {
                    const int nd = follow_next(dir);
                    const std::string fc = std::string(follow_clip(dir)) + e.set;
                    if (play_once(sc, a, fc, now, actor_yaw(a), false)) {
                        e.hit.active = true;
                        e.hit.kind = pending_hit::FOLLOW;
                        e.hit.dir = nd;
                        e.hit.at = now + fp.fight(std::string(k_dir_names[nd]) + " Follow Attack Begin", 0.4);
                        e.hit.dmg = int(fp.fight(std::string(k_dir_names[nd]) + " Follow Damage", 7));
                        e.busy_until = now + skill_seconds(sc, a, fc);
                    }
                }
            }
        }
    }
    if (a.scripted || now < e.busy_until) return;     // a clip owns the body

    // Stay on the floor.
    float gy;
    if (ground_under(sc.coll, p, gy)) p[1] = gy;

    const float watch  = float(fp.enemy("distFarFromEnemy", 10.0));
    const float charge = float(fp.enemy("distApproachEnemy", 5.0));
    const float periph = float(fp.enemy("peripheral", 30.0)) * 3.14159265f / 180.0f;
    // Camouflaged, he is seen only from distCamoflauge (1.5).
    const bool  camo = potion_state::on(g_potions.camo_until, now) && e.state == enemy_ai::PATROL &&
                       d > float(fp.enemy("distCamoflauge", 1.5));
    const bool  sees = !camo && prince_available && same_floor && d < watch &&
                       (d < charge || angle_diff(yaw_toward(p, pp), actor_yaw(a)) < periph);

    switch (e.state) {
    case enemy_ai::PATROL:
        if (sees) {
            e.state = enemy_ai::CHARGE;
            play_sound("PG1dlg", p, 20.0f, false);
            std::printf("combat: %s sees the Prince (%.1f away)\n", a.script_name.c_str(), d);
            break;
        }
        if (!e.way.empty()) {
            const std::array<float, 3>& w = e.way[e.wp % e.way.size()];
            const float wx = w[0] - p[0], wz = w[2] - p[2];
            if (wx * wx + wz * wz < 0.25f) e.wp = (e.wp + 1) % e.way.size();
            else steer(sc, a, &w[0], false, dt);
        } else {
            const float hx = e.home[0] - p[0], hz = e.home[2] - p[2];
            if (hx * hx + hz * hz > 0.25f) steer(sc, a, e.home, false, dt);
            else if (e.start_loops) {
                a.stance = find_actor_skill(sc, a, e.start_skill);   // "*AmbientType_G4"
                if (!a.stance) set_clip(a, LOCO_BASE);
            } else { a.stance = 0; set_clip(a, LOCO_BASE); }
        }
        break;
    case enemy_ai::CHARGE:
        if (!prince_available || d > watch * 1.5f) { e.state = enemy_ai::PATROL; break; }
        if (d <= combat_tuning::FIGHT_DIST) {
            e.state = enemy_ai::FIGHT;
            e.next_attack = now + 0.6;
            // Fighter__StSetupForFightMode (0x4C604D): a taunt going in.
            if (!e.taunted) {
                e.taunted = true;
                a.node->local.set_yaw(std::atan2(dx, dz));
                const std::string tc = "CombatTaunt" + e.set;
                if (play_once(sc, a, tc, now, std::atan2(dx, dz), false)) {
                    e.busy_until = now + skill_seconds(sc, a, tc);
                    e.next_attack = e.busy_until + 0.3;
                }
            }
            break;
        }
        steer(sc, a, pp, true, dt);
        break;
    case enemy_ai::FIGHT:
    case enemy_ai::RECOVER: {
        if (!prince_available) { e.state = enemy_ai::PATROL; a.stance = 0; break; }
        if (d > combat_tuning::FIGHT_DIST * 1.6f) { e.state = enemy_ai::CHARGE; break; }
        a.node->local.set_yaw(std::atan2(dx, dz));
        // At ready; woozy at a bottle or less (Fight__IdleAmbientOrWoozy 0x4AE60F).
        a.stance = a.health <= combat_tuning::BOTTLE ? find_actor_skill(sc, a, "CombatWoozy" + e.set) : 0;
        if (!a.stance) a.stance = find_actor_skill(sc, a, "CombatAtReady" + e.set);
        // The Prince dying in reach: finish him (0x4ACBB4).
        if (cs.prince_dying_until >= now && cs.dead_since < 0.0 && d <= combat_tuning::REACH) {
            finishing_move(sc, a, "CombatFinishingMove1" + e.set, prince, "CombatFinishingReceive1_G2", now);
            e.busy_until = now + skill_seconds(sc, a, "CombatFinishingMove1" + e.set);
            prince.health = 0;
            cs.dead_since = now;
            cs.prince_dying_until = -1.0;
            std::printf("combat: %s finishes the Prince\n", a.script_name.c_str());
            break;
        }
        if (e.react == enemy_ai::R_LATE && now >= e.react_at) {
            e.react = enemy_ai::R_NONE;
            // A late answer to his swing meets it: a clash (Fight__Clash
            // 0x4AC5FF) - both thrown 1.5 apart, nobody hurt. Our timing
            // stands in for the 1/30 s test (0x4AC9E8).
            if (cs.prince_hit.active && cs.prince_hit.kind != pending_hit::FEINT && d <= combat_tuning::REACH) {
                cs.prince_hit.active = false;
                const float nx = d > 1e-4f ? dx / d : 0.0f, nz = d > 1e-4f ? dz / d : 1.0f;
                p[0] = pp[0] - nx * 1.5f;
                p[2] = pp[2] - nz * 1.5f;
                play_once(sc, a, "CombatClash" + e.set, now, actor_yaw(a), false);
                play_once(sc, prince, "CombatClash_G2", now, actor_yaw(prince), false);
                play_sound("gblswordclank", p, 14.0f, false);
                e.busy_until = now + skill_seconds(sc, a, "CombatClash" + e.set);
                cs.prince_busy_until = std::max(cs.prince_busy_until, now + skill_seconds(sc, prince, "CombatClash_G2"));
                std::printf("combat: clash with %s\n", a.script_name.c_str());
                break;
            }
            e.next_attack = now;
        }
        // Blocking or flinching wrong: show it when his delay is up.
        if ((e.react == enemy_ai::R_BLOCK || e.react == enemy_ai::R_WRONG) && now >= e.react_at && !a.scripted)
            a.stance = find_actor_skill(sc, a, std::string("CombatBlocking") +
                k_dir_names[e.react == enemy_ai::R_BLOCK ? cs.prince_hit.dir : (cs.prince_hit.dir + 1) % 3] + e.set);
        if (now >= e.next_attack && e.react != enemy_ai::R_BLOCK) {
            const int dir = std::rand() % 3;
            // A feint now and then: 100 out of the weight total 300, never
            // twice running (0x4ACC36).
            const bool feint = !e.last_feint && frand() * 300.0 < 100.0;
            e.last_feint = feint;
            const std::string atk = std::string(feint ? "CombatFeint" : "CombatAttack") + k_dir_names[dir] + e.set;
            if (play_once(sc, a, atk, now, actor_yaw(a), false)) {
                e.hit.active = true;
                e.hit.dir = dir;
                e.hit.kind = feint ? pending_hit::FEINT : pending_hit::ATTACK;
                e.hit.dmg = -1;
                e.hit.at = feint ? now + fp.fight(std::string("Sword ") + k_dir_names[dir] + " Feint", 0.367)
                         : now + fp.fight(std::string("Sword ") + k_dir_names[dir] + " Attack Begin", 0.3)
                               + fp.fight("Unblocked Attack", 0.0667);
                e.busy_until = now + skill_seconds(sc, a, atk);
            }
            // Aggression is his pause between attacks, by skill; now and
            // then he goes again at once (Double Attack Percentage).
            const double aggr = skill_param(fp, e.skill, "Aggression", 2.0);
            e.next_attack = now + aggr * (0.75 + 0.5 * frand());
            if (frand() * 100.0 < skill_param(fp, e.skill, "Double Attack Percentage", 10.0))
                e.next_attack = e.busy_until;
        }
        break;
    }
    default:
        break;
    }
}

// The Prince's swing lands: the nearest guard in reach and in front takes it,
// or blocks.
void prince_hit_resolve(combat_state& cs, scene& sc, actor_inst& p, double now) {
    if (!cs.prince_hit.active || now < cs.prince_hit.at) return;
    cs.prince_hit.active = false;
    const float* pp = p.node->local.translate;
    enemy_ai* best = 0;
    float best_d = combat_tuning::REACH;
    for (size_t i = 0; i < cs.enemies.size(); ++i) {
        enemy_ai& e = cs.enemies[i];
        if (e.state == enemy_ai::DEAD || sc.actors[size_t(e.actor)].hidden) continue;
        const float* q = sc.actors[size_t(e.actor)].node->local.translate;
        const float dx = q[0] - pp[0], dz = q[2] - pp[2];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > best_d || std::fabs(q[1] - pp[1]) > 1.5f) continue;
        if (angle_diff(std::atan2(dx, dz), actor_yaw(p)) > 1.2f) continue;
        best = &e;
        best_d = d;
    }
    const k2::engine::fight_params& fp = *cs.params;
    // Everyone's answer to this swing is spent, whoever it lands on.
    enemy_ai::react_t react = best ? best->react : enemy_ai::R_NONE;
    const bool react_ready = best && now >= best->react_at;
    for (size_t i = 0; i < cs.enemies.size(); ++i)
        if (cs.enemies[i].react != enemy_ai::R_LATE) cs.enemies[i].react = enemy_ai::R_NONE;
    if (!best) return;
    actor_inst& a = sc.actors[size_t(best->actor)];
    const int dir = cs.prince_hit.dir;
    if (cs.prince_hit.kind == pending_hit::FEINT) {
        // Half way through a feint, a guard who rose to block it lets the
        // block fall, and is open for Computer Recover From Block (0x4AD858).
        if (react == enemy_ai::R_BLOCK && react_ready && !a.scripted && best->state == enemy_ai::FIGHT) {
            play_once(sc, a, std::string("CombatAIEndBlock") + k_dir_names[dir] + best->set, now, actor_yaw(a), false);
            best->busy_until = now + fp.fight("Computer Recover From Block", 0.367);
            best->next_attack = std::max(best->next_attack, best->busy_until);
        }
        return;
    }
    if (react == enemy_ai::R_BLOCK && react_ready && !a.scripted && best->state != enemy_ai::DYING) {
        // Blocked: the Prince recoils for his Blocked .. Recover.
        play_sound("gblswordclank", a.node->local.translate, 14.0f, false);
        play_once(sc, a, std::string("CombatBlock") + k_dir_names[dir] + best->set, now, actor_yaw(a), false);
        play_once(sc, p, std::string("CombatBlocked") + k_dir_names[dir] + "_G2", now, actor_yaw(p), false);
        best->busy_until = now + fp.fight("Recover From Block", 0.0667);
        cs.prince_busy_until = std::max(cs.prince_busy_until,
            now + fp.fight(std::string("Sword Blocked ") + k_dir_names[dir] + " Attack Recover", 0.367));
        // His left or right blocked: the guard may counter on that side
        // (Fight__AITryCounter 0x4AD975: Counterattacks out of Opportunities).
        // Both sides use the left counter's numbers (0x4AB83E).
        if (dir != DIR_CENTER && cs.prince_hit.kind == pending_hit::ATTACK &&
            frand() * skill_param(fp, best->skill, "Counterattack Opportunities", 100) <
                skill_param(fp, best->skill, "Counterattacks", 5)) {
            const std::string cc = std::string("CombatCounter") + k_dir_names[dir] + best->set;
            if (play_once(sc, a, cc, now, actor_yaw(a), false)) {
                best->hit.active = true;
                best->hit.kind = pending_hit::COUNTER;
                best->hit.dir = dir;
                best->hit.at = now + fp.fight("Sword Left Counter Attack Begin", 0.467);
                best->hit.dmg = int(fp.fight("Sword Left Counter Damage", 8));
                best->busy_until = now + skill_seconds(sc, a, cc);
                std::printf("combat: %s counters\n", a.script_name.c_str());
            }
        }
        return;
    }
    const int base = cs.prince_hit.dmg >= 0 ? cs.prince_hit.dmg : sword_damage(fp, dir);
    const int dmg = base + (potion_state::on(g_potions.strength_until, now) ? int(g_potions.strength) : 0);
    enemy_damage(cs, sc, *best, dmg, dir, now);
    cs.prince_busy_until = std::max(cs.prince_busy_until, now + fp.fight("Sword Recover From Injuring", 0.2));
    // A plain swing landed: the follow window opens about this moment.
    if (cs.prince_hit.kind == pending_hit::ATTACK && best->state != enemy_ai::DEAD) {
        const double fw = fp.fight("Follow Window", 0.233);
        cs.follow_from = dir;
        cs.follow_lo = now - fw * 0.5;
        cs.follow_hi = now + fw * 0.5;
    }
}

// The Prince has started a swing at `dir`: the guard it is aimed at decides
// now how he will answer, from his skill's percentages, and how soon.
void enemy_react(combat_state& cs, scene& sc, const float* pp, float yaw, double now) {
    const k2::engine::fight_params& fp = *cs.params;
    for (size_t i = 0; i < cs.enemies.size(); ++i) {
        enemy_ai& e = cs.enemies[i];
        const actor_inst& a = sc.actors[size_t(e.actor)];
        if (e.state != enemy_ai::FIGHT || a.hidden) continue;
        const float* q = a.node->local.translate;
        const float dx = q[0] - pp[0], dz = q[2] - pp[2];
        if (dx * dx + dz * dz > combat_tuning::REACH * combat_tuning::REACH * 1.5f) continue;
        if (angle_diff(std::atan2(dx, dz), yaw) > 1.2f) continue;
        const double w[4] = { skill_param(fp, e.skill, "Blocks", 12), skill_param(fp, e.skill, "Wrong Blocks", 12),
                              skill_param(fp, e.skill, "Did Nothings", 65), skill_param(fp, e.skill, "Late Attacks", 11) };
        double r = frand() * (w[0] + w[1] + w[2] + w[3]);
        int pick = 0;
        while (pick < 3 && r >= w[pick]) { r -= w[pick]; ++pick; }
        const double lo = fp.fight("AI Min Block Delay", 0.15), hi = fp.fight("AI Max Block Delay", 0.433);
        e.react_at = now + lo + (hi - lo) * frand();
        // Too slow to raise the sword before it lands: it lands.
        const bool in_time = e.react_at <= cs.prince_hit.at && now >= e.busy_until && !a.scripted;
        e.react = pick == 0 ? (in_time ? enemy_ai::R_BLOCK : enemy_ai::R_NONE)
                : pick == 1 ? enemy_ai::R_WRONG
                : pick == 3 ? enemy_ai::R_LATE : enemy_ai::R_NONE;
    }
}

// The guard nearest the Prince within `range`, or -1.
int nearest_enemy(const combat_state& cs, const scene& sc, const float* pp, float range) {
    int best = -1;
    float bd = range;
    for (size_t i = 0; i < cs.enemies.size(); ++i) {
        if (cs.enemies[i].state == enemy_ai::DEAD) continue;
        const float* q = sc.actors[size_t(cs.enemies[i].actor)].node->local.translate;
        const float d = std::sqrt((q[0]-pp[0])*(q[0]-pp[0]) + (q[2]-pp[2])*(q[2]-pp[2]));
        if (d < bd && std::fabs(q[1] - pp[1]) < 1.5f) { bd = d; best = int(i); }
    }
    return best;
}

// --- relaunch ---------------------------------------------------------------------
//
// Restart Level, Main Menu and Load from the in-game menu start the binary
// afresh: the original's arguments without the ones that place or set up a
// run, then `extra`. The data root (the last argument) is kept.
void relaunch(int argc, char** argv, const std::vector<std::string>& extra) {
    static const char* drop1[] = { "--menu", "--menu-game", "--armed", "--boot", 0 };
    static const char* drop2[] = { "--place", "--region", "--health", "--send", "--movie", "--level", "--world", 0 };
    std::vector<std::string> args;
    std::string root;
    for (int i = 1; i < argc; ++i) {
        bool skip = false;
        for (int k = 0; drop1[k]; ++k) if (std::strcmp(argv[i], drop1[k]) == 0) skip = true;
        for (int k = 0; drop2[k]; ++k) if (std::strcmp(argv[i], drop2[k]) == 0) { skip = true; ++i; }
        if (skip) continue;
        if (i == argc - 1 && argv[i][0] != '-') { root = argv[i]; continue; }
        args.push_back(argv[i]);
    }
    args.insert(args.end(), extra.begin(), extra.end());
    if (!root.empty()) args.push_back(root);
    std::fflush(stdout);
    plat::audio_close();
    gfx::shutdown();
    plat::shutdown();
    plat::relaunch(argv[0], args);
    std::perror("relaunch");
    std::exit(1);
}

// --- cutscenes -------------------------------------------------------------------
//
// Plays a movie (data/Movies/<name>.mov) full screen, its sound through the
// mixer. Esc, Space, Enter or a click skips it. Returns false if the window
// was closed.
// A cutscene, spread over the cores. Core 1 decodes the SVQ1 frames (the
// bit stream is serial), core 2 turns each into RGBA, and the sound decodes
// into the mixer's buffer beside the main thread on core 0, which only
// uploads the frame that is due. A ring of RING slots carries frames from
// the decoder to the converter to the screen; `space` counts the slots the
// decoder may still fill (the one on screen is never one of them).
namespace {
struct movie_play {
    static const int RING = 4;
    media::movie* mv;
    int width, height, bytes;
    media::svq1_decoder::yuv_frame yuv[RING];
    std::vector<uint8_t> frame[RING];
    double               time[RING];
    std::atomic<int>     decoded;           // frames handed to the converter
    std::atomic<int>     head;              // frames ready to show
    plat::sema           space, to_convert, done;
    volatile bool        stop;
    volatile bool        decode_finished;
    int16_t*             pcm;
    size_t               pcm_cap;
    volatile size_t      pcm_ready;         // samples the sound thread has written
    movie_play() : mv(0), width(0), height(0), bytes(0), decoded(0), head(0), space(0), to_convert(0),
                   done(0), stop(false), decode_finished(false), pcm(0), pcm_cap(0), pcm_ready(0) {}

    static void video(void* u) {
        movie_play& p = *static_cast<movie_play*>(u);
        for (;;) {
            plat::sema_wait(p.space);
            double t = 0.0;
            if (p.stop || !p.mv->next_frame(t) || !p.mv->yuv()) break;
            const int slot = p.decoded.load() % RING;
            p.yuv[slot] = *p.mv->yuv();
            p.time[slot] = t;
            p.decoded.fetch_add(1);
            plat::sema_signal(p.to_convert, 1);
        }
        p.decode_finished = true;
        plat::sema_signal(p.to_convert, 1);             // wake the converter to see it
        plat::sema_signal(p.done, 1);
    }

    static void convert(void* u) {
        movie_play& p = *static_cast<movie_play*>(u);
        for (;;) {
            plat::sema_wait(p.to_convert);
            if (p.stop) break;
            const int n = p.head.load();
            if (n >= p.decoded.load()) { if (p.decode_finished) break; continue; }
            const int slot = n % RING;
            media::svq1_decoder::yuv_to_rgba(p.yuv[slot], p.width, p.height, &p.frame[slot][0]);
            p.head.fetch_add(1);
        }
        plat::sema_signal(p.done, 1);
    }

    static void audio(void* u) {
        movie_play& p = *static_cast<movie_play*>(u);
        p.mv->decode_audio_into(p.pcm, p.pcm_cap, &p.stop, &p.pcm_ready);
        p.pcm_ready = p.pcm_cap;
        plat::sema_signal(p.done, 1);
    }
};
} // namespace

bool play_movie(const std::string& name) {
    const double t_open = plat::time_seconds();
    media::movie mv;
    std::string path = "movies/" + name;
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".mov") != 0) path += ".mov";
    for (size_t i = 0; i < path.size(); ++i) path[i] = char(std::tolower(static_cast<unsigned char>(path[i])));
    if (!mv.open(path)) { std::printf("movie: %s not found\n", path.c_str()); return true; }
    std::printf("movie: %s %dx%d, %.1f s, %zu audio samples\n", path.c_str(), mv.width(), mv.height(),
                mv.duration(), mv.audio_samples());

    static movie_play p;
    p.pcm = 0;
    p.pcm_cap = 0;
    p.pcm_ready = 0;
    p.mv = &mv;
    p.stop = false;
    p.decode_finished = false;
    p.head.store(0);
    p.decoded.store(0);
    p.width = mv.width();
    p.height = mv.height();
    p.bytes = mv.width() * mv.height() * 4;
    mv.set_convert(false);
    for (int i = 0; i < movie_play::RING; ++i) p.frame[i].assign(size_t(p.bytes), 0);
    // Fresh semaphores per movie: the last one's leave counts behind.
    p.space = plat::sema_create(movie_play::RING - 1);
    p.to_convert = plat::sema_create(0);
    p.done = plat::sema_create(0);
    int threads = 0;

    // The sound: a silent buffer of the track's length, registered, and
    // filled ahead of the play head by its own thread.
    int32_t voice = 0;
    if (!g_mixer) std::printf("movie: no mixer, silent\n");
    if (g_mixer && mv.audio_samples()) {
        assets::sfx_file sf;
        sf.channels = mv.audio_channels();
        sf.rate = mv.audio_rate();
        sf.samples.assign(mv.audio_samples(), 0);
        p.pcm_cap = sf.samples.size();
        g_mixer->add_sound("__movie", std::move(sf));
        p.pcm = g_mixer->sound_samples("__movie");
        if (p.pcm && plat::thread_start(&movie_play::audio, &p, 0, "k2 movie audio")) ++threads;
    }
    if (plat::thread_start(&movie_play::video, &p, 1, "k2 movie video")) ++threads;
    if (plat::thread_start(&movie_play::convert, &p, 2, "k2 movie colour")) ++threads;

    // Picture and sound start together: the clock waits for the first frame
    // and a second of decoded sound (or all of it). Started at once, the
    // first frames came late and the sound played silence until its decoder
    // passed the play head - a frozen black screen, then sound seconds late.
    bool alive = true, skip = false;
    {
        const double t0 = plat::time_seconds();
        const size_t want = std::min(p.pcm_cap, size_t(mv.audio_rate()) * size_t(mv.audio_channels()));
        while (alive && !skip && plat::time_seconds() - t0 < 5.0) {
            const bool video_ready = p.head.load() >= 1 || p.decode_finished;
            const bool audio_ready = !p.pcm || p.pcm_ready >= want;
            if (video_ready && audio_ready) break;
            plat::event e;
            while (plat::poll_event(e)) {
                if (e.type == plat::EVENT_QUIT) alive = false;
                if (e.type == plat::EVENT_PAD_BUTTON && e.pad_pressed) skip = true;
                if (e.type == plat::EVENT_KEY_DOWN && e.k == plat::KEY_ESCAPE) skip = true;
            }
            gfx::begin_frame();
            gfx::set_clear_color(0.0f, 0.0f, 0.0f);
            gfx::clear(true, true);
            gfx::end_frame();
            plat::swap_buffers();
        }
        std::printf("movie: opened in %.2f s, buffered in %.2f s (%d frames, %.1f s of sound)\n",
                    t0 - t_open, plat::time_seconds() - t0, p.head.load(),
                    p.pcm_cap ? double(p.pcm_ready) / (mv.audio_rate() * mv.audio_channels()) : 0.0);
    }
    if (g_mixer && p.pcm) voice = g_mixer->play("__movie", 0, 0.0f, false, 1.0f);
    std::fflush(stdout);

    gfx::texture_id tex = gfx::INVALID_TEXTURE;
    const double start = plat::time_seconds();
    int shown = -1;
    while (alive && !skip) {
        plat::event e;
        while (plat::poll_event(e)) {
            if (e.type == plat::EVENT_QUIT) alive = false;
            if (e.type == plat::EVENT_KEY_DOWN &&
                (e.k == plat::KEY_ESCAPE || e.k == plat::KEY_SPACE || e.k == plat::KEY_RETURN)) skip = true;
            if (e.type == plat::EVENT_MOUSE_DOWN) skip = true;
            if (e.type == plat::EVENT_PAD_BUTTON && e.pad_pressed) skip = true;
        }
        const double t = plat::time_seconds() - start;
        if (t >= mv.duration()) break;
        // The newest decoded frame that is due; the one it replaces goes back
        // to the decode thread.
        bool changed = false;
        while (shown + 1 < p.head.load() && p.time[(shown + 1) % movie_play::RING] <= t) {
            if (shown >= 0) plat::sema_signal(p.space, 1);
            ++shown;
            changed = true;
        }
        if (changed) {
            if (tex == gfx::INVALID_TEXTURE) tex = gfx::create_stream_texture(mv.width(), mv.height());
            gfx::update_texture(tex, &p.frame[shown % movie_play::RING][0]);
        }
        gfx::begin_frame();
        gfx::set_clear_color(0.0f, 0.0f, 0.0f);
        gfx::clear(true, true);
        if (tex != gfx::INVALID_TEXTURE) gfx::draw_fullscreen_quad(tex);
        gfx::end_frame();
        plat::swap_buffers();
    }
    // Stop both threads and wait for them before the movie goes away.
    p.stop = true;
    plat::sema_signal(p.space, movie_play::RING);
    plat::sema_signal(p.to_convert, movie_play::RING);
    for (int i = 0; i < threads; ++i) plat::sema_wait(p.done);
    std::printf("movie: %d of %d frames decoded shown, %.1f s\n", shown + 1, p.head.load(),
                plat::time_seconds() - start);
    plat::sema_destroy(p.space);
    plat::sema_destroy(p.to_convert);
    plat::sema_destroy(p.done);
    p.space = p.to_convert = p.done = 0;
    if (g_mixer && voice) g_mixer->stop(voice);
    if (tex != gfx::INVALID_TEXTURE) gfx::destroy_texture(tex);
    return alive;
}

// --- visual effects (.nif) -----------------------------------------------------
//
// A NetImmerse file played at a point or on an actor: its node tree posed
// at time t (Ni3dsAnimationNode keys), its shapes drawn, and each
// Ni3dsParticleSystem (a 3ds Max SuperSpray) emitting camera-facing sprites.
// The files are in Max space (Z up) and are turned into the engine's the
// way the rooms are, (x, y, z) -> (x, z, -y). The particle parameters'
// meanings are read off the shipped effects, not traced.
struct fx_def {
    assets::nif_file nif;
    std::vector<gfx::texture_id> tex;       // per block: RAW_IMAGE -> texture
    std::vector<gfx::mesh_id>    mesh;      // per block: SHAPE -> mesh
    std::vector<int>             parent;    // per block, -1 for none
    float                        length;    // seconds
    bool                         loop;
};

struct fx_particle { float p[3], v[3]; double born; float life, size; };

struct fx_instance {
    int     id;
    fx_def* def;
    float   pos[3];
    float   yaw;
    double  start, until;                   // until < 0: the file's own length
    int     actor;                          // follows this actor, or -1
    float   lift;                           // above the actor's feet
    std::vector<std::vector<fx_particle> > parts;   // per block (PSYS)
    std::vector<double> emit_acc;
};

std::map<std::string, fx_def*> g_fx_defs;
std::vector<fx_instance> g_fx;
int g_fx_next_id = 1;

fx_def* fx_load(const std::string& name) {
    std::string key = name;
    for (size_t i = 0; i < key.size(); ++i) key[i] = char(std::tolower(static_cast<unsigned char>(key[i])));
    std::map<std::string, fx_def*>::iterator it = g_fx_defs.find(key);
    if (it != g_fx_defs.end()) return it->second;
    fx_def* d = 0;
    size_t size = 0;
    uint8_t* data = plat::fs_read_basename(name.c_str(), size);
    if (data) {
        d = new fx_def();
        std::string err;
        if (!assets::nif_parse(data, size, d->nif, &err)) { delete d; d = 0; }
        plat::fs_free(data);
    }
    if (d) {
        const std::vector<assets::nif_block>& b = d->nif.blocks;
        d->tex.assign(b.size(), gfx::INVALID_TEXTURE);
        d->mesh.assign(b.size(), 0);
        d->parent.assign(b.size(), -1);
        d->length = 0.0f;
        d->loop = false;
        // K2_FXDUMP: each block's kind, name, transform and emitter.
        if (std::getenv("K2_FXDUMP"))
            for (size_t i = 0; i < b.size(); ++i) {
                std::printf("  [%zu] f%02x %-22s %-18s T(%.2f %.2f %.2f) R(%.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f) s %.2f\n",
                            i, unsigned(b[i].av_flags), b[i].type.c_str(), b[i].name.c_str(), b[i].translate[0], b[i].translate[1],
                            b[i].translate[2], b[i].rotate[0], b[i].rotate[1], b[i].rotate[2], b[i].rotate[3],
                            b[i].rotate[4], b[i].rotate[5], b[i].rotate[6], b[i].rotate[7], b[i].rotate[8], b[i].scale);
                {
                    std::printf("        children");
                    for (size_t c = 0; c < b[i].children.size(); ++c) std::printf(" %d", d->nif.find(b[i].children[c]));
                    std::printf("\n");
                }
                if (b[i].kind == assets::nif_block::RAW_IMAGE && !b[i].rgba.empty()) {
                    int amin = 255, amax = 0, mag = 0;
                    for (size_t q = 0; q + 3 < b[i].rgba.size(); q += 4) {
                        amin = std::min<int>(amin, b[i].rgba[q + 3]); amax = std::max<int>(amax, b[i].rgba[q + 3]);
                        if (b[i].rgba[q] > 240 && b[i].rgba[q + 1] < 16 && b[i].rgba[q + 2] > 240) ++mag;
                    }
                    std::printf("        image %ux%u alpha %d..%d, magenta %d px\n", b[i].width, b[i].height, amin, amax, mag);
                }
                if (b[i].kind == assets::nif_block::PSYS)
                    std::printf("        psys axis(%.2f %.2f %.2f) speed %.2f size %.2f life %.2f rate %.1f spread %.2f/%.2f\n",
                                b[i].psys.axis[0], b[i].psys.axis[1], b[i].psys.axis[2], b[i].psys.speed,
                                b[i].psys.size, b[i].psys.life, b[i].psys.rate, b[i].psys.spread_a, b[i].psys.spread_b);
            }
        for (size_t i = 0; i < b.size(); ++i) {
            for (size_t c = 0; c < b[i].children.size(); ++c) {
                const int k = d->nif.find(b[i].children[c]);
                if (k >= 0) d->parent[size_t(k)] = int(i);
            }
            if (b[i].anim_end > d->length) d->length = b[i].anim_end;
            for (size_t e = 0; e < b[i].extra.size(); ++e) if (b[i].extra[e] == "loop") d->loop = true;
            if (b[i].kind == assets::nif_block::RAW_IMAGE && !b[i].rgba.empty())
                d->tex[i] = gfx::create_texture(int32_t(b[i].width), int32_t(b[i].height), &b[i].rgba[0]);
            if (b[i].kind == assets::nif_block::SHAPE && !b[i].verts.empty() && !b[i].tris.empty()) {
                const size_t nv = b[i].verts.size() / 3;
                std::vector<gfx::vertex> v(nv);
                std::memset(&v[0], 0, nv * sizeof(gfx::vertex));
                for (size_t k = 0; k < nv; ++k) {
                    v[k].x = b[i].verts[k * 3]; v[k].y = b[i].verts[k * 3 + 1]; v[k].z = b[i].verts[k * 3 + 2];
                    v[k].nz = 1.0f;
                    if (b[i].uvs.size() >= (k + 1) * 2) { v[k].u = b[i].uvs[k * 2]; v[k].v = b[i].uvs[k * 2 + 1]; }
                    const float* c = b[i].colors.size() >= (k + 1) * 4 ? &b[i].colors[k * 4] : 0;
                    v[k].r = uint8_t(255 * (c ? c[0] : 1)); v[k].g = uint8_t(255 * (c ? c[1] : 1));
                    v[k].b = uint8_t(255 * (c ? c[2] : 1)); v[k].a = uint8_t(255 * (c ? c[3] : 1));
                }
                d->mesh[i] = gfx::create_mesh(&v[0], int32_t(nv), &b[i].tris[0], int32_t(b[i].tris.size()));
            }
        }
        if (d->length <= 0.0f) d->length = 2.0f;
        std::printf("fx: %s, %zu blocks%s, %.2f s%s\n", name.c_str(), b.size(),
                    d->nif.complete ? "" : " (partial)", d->length, d->loop ? ", loops" : "");
    } else {
        std::printf("fx: %s not found\n", name.c_str());
    }
    g_fx_defs[key] = d;
    return d;
}

// Starts an effect. `seconds` < 0 plays the file once (or for good, if it
// loops and follows an actor until stopped with fx_stop).
int fx_spawn(const std::string& name, const float* pos, float yaw, double now, double seconds,
             int actor = -1, float lift = 0.0f) {
    fx_def* d = fx_load(name);
    if (!d) return -1;
    fx_instance fi;
    fi.id = g_fx_next_id++;
    fi.def = d;
    for (int k = 0; k < 3; ++k) fi.pos[k] = pos ? pos[k] : 0.0f;
    fi.yaw = yaw;
    fi.start = now;
    fi.until = seconds >= 0.0 ? now + seconds : (d->loop && actor >= 0 ? 1.0e30 : now + d->length);
    fi.actor = actor;
    fi.lift = lift;
    fi.parts.resize(d->nif.blocks.size());
    fi.emit_acc.assign(d->nif.blocks.size(), 0.0);
    g_fx.push_back(fi);
    return fi.id;
}

bool fx_alive(int id) {
    for (size_t i = 0; i < g_fx.size(); ++i) if (g_fx[i].id == id) return true;
    return false;
}

void fx_stop(int id) {
    for (size_t i = 0; i < g_fx.size(); ++i) if (g_fx[i].id == id) g_fx[i].until = 0.0;
}

// SpecialEffect traps: running while visible and on.
void fx_sync_traps(double now) {
    for (size_t i = 0; i < g_effect_traps.size(); ++i) {
        effect_trap& e = *g_effect_traps[i];
        const bool want = e.visible() && e.on && !e.file.empty();
        const bool have = e.fx_id >= 0 && fx_alive(e.fx_id);
        if (want && !have) e.fx_id = fx_spawn(e.file, e.pos, e.yaw, now, 1.0e30);
        else if (!want && have) { fx_stop(e.fx_id); e.fx_id = -1; }
    }
}

void fx_stop_on(int actor, const std::string& name) {
    fx_def* d = fx_load(name);
    for (size_t i = 0; i < g_fx.size(); ++i)
        if (g_fx[i].actor == actor && g_fx[i].def == d) g_fx[i].until = 0.0;
}

// Column-major 4x4 helpers.
void m4_mul(const float* a, const float* b, float* out) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    std::memcpy(out, r, sizeof(r));
}

// A block's local transform at `t` seconds into the effect.
void fx_local(const assets::nif_block& b, float t, float* m) {
    float R[9], T[3], S = b.scale;
    std::memcpy(R, b.rotate, sizeof(R));
    std::memcpy(T, b.translate, sizeof(T));
    if (b.kind == assets::nif_block::ANIM_NODE) {
        const float span = b.anim_end > b.anim_start ? b.anim_end - b.anim_start : 0.0f;
        const float tt = span > 0.0f ? b.anim_start + std::fmod(t, span) : t;
        if (!b.pos_keys.empty()) {
            size_t k = 0;
            while (k + 1 < b.pos_keys.size() && b.pos_keys[k + 1].time <= tt) ++k;
            const assets::nif_vec_key& a = b.pos_keys[k];
            const assets::nif_vec_key& c = b.pos_keys[std::min(k + 1, b.pos_keys.size() - 1)];
            const float u = c.time > a.time ? std::max(0.0f, std::min(1.0f, (tt - a.time) / (c.time - a.time))) : 0.0f;
            for (int i = 0; i < 3; ++i) T[i] = a.v[i] + (c.v[i] - a.v[i]) * u;
        }
        if (!b.rot_keys.empty()) {
            size_t k = 0;
            while (k + 1 < b.rot_keys.size() && b.rot_keys[k + 1].time <= tt) ++k;
            const assets::nif_quat_key& a = b.rot_keys[k];
            const assets::nif_quat_key& c = b.rot_keys[std::min(k + 1, b.rot_keys.size() - 1)];
            const float u = c.time > a.time ? std::max(0.0f, std::min(1.0f, (tt - a.time) / (c.time - a.time))) : 0.0f;
            float q[4];
            assets::skl_slerp(a.q, c.q, u, q);
            const float w = q[0], x = q[1], y = q[2], z = q[3];
            R[0]=1-2*(y*y+z*z); R[1]=  2*(x*y-w*z); R[2]=  2*(x*z+w*y);
            R[3]=  2*(x*y+w*z); R[4]=1-2*(x*x+z*z); R[5]=  2*(y*z-w*x);
            R[6]=  2*(x*z-w*y); R[7]=  2*(y*z+w*x); R[8]=1-2*(x*x+y*y);
        }
        if (!b.scale_keys.empty()) {
            size_t k = 0;
            while (k + 1 < b.scale_keys.size() && b.scale_keys[k + 1].time <= tt) ++k;
            S = b.scale_keys[k].v;
        }
    }
    // NiMatrix3 is row-major; v' = R v S + T.
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) m[c * 4 + r] = R[r * 3 + c] * S;
    m[3] = m[7] = m[11] = 0.0f;
    m[12] = T[0]; m[13] = T[1]; m[14] = T[2]; m[15] = 1.0f;
}

// World matrix of block i: instance placement * Max->engine * the chain.
void fx_world(const fx_instance& fi, int i, float t, float* out) {
    const fx_def& d = *fi.def;
    if (i < 0) {
        float place[16], conv[16];
        mat4_identity(place);
        const float c = std::cos(fi.yaw), s = std::sin(fi.yaw);
        place[0] = c;  place[2] = -s;               // set_yaw: model +X -> (c, 0, -s)
        place[8] = s;  place[10] = c;               //          model +Z -> (s, 0, c)
        place[12] = fi.pos[0]; place[13] = fi.pos[1]; place[14] = fi.pos[2];
        // The effects are exported y-up, as the engine is: the shower's floor
        // splash node sits 3.1 below its jet, the nozzle's height above the
        // shower room floor. No Max -> engine swap here.
        mat4_identity(conv);
        m4_mul(place, conv, out);
        return;
    }
    float pm[16], lm[16];
    fx_world(fi, d.parent[size_t(i)], t, pm);
    fx_local(d.nif.blocks[size_t(i)], t, lm);
    m4_mul(pm, lm, out);
}

// Hidden in the file (NiAVObject flag bit 0, APP_CULLED), itself or an
// ancestor: the shower's "Box" is the spray's helper volume, magenta in Max.
bool fx_hidden(const fx_def& d, int i) {
    for (int b = i; b >= 0; b = d.parent[size_t(b)])
        if (d.nif.blocks[size_t(b)].av_flags & 1) return true;
    return false;
}

// The property of `kind` on block i or the nearest ancestor that has one.
int fx_prop(const fx_def& d, int i, assets::nif_block::kind_t kind) {
    for (int b = i; b >= 0; b = d.parent[size_t(b)])
        for (size_t p = 0; p < d.nif.blocks[size_t(b)].props.size(); ++p) {
            const int k = d.nif.find(d.nif.blocks[size_t(b)].props[p]);
            if (k >= 0 && d.nif.blocks[size_t(k)].kind == kind) return k;
        }
    return -1;
}

gfx::texture_id fx_texture(const fx_def& d, int i) {
    const int tp = fx_prop(d, i, assets::nif_block::TEXTURE);
    if (tp < 0 || d.nif.blocks[size_t(tp)].images.empty()) return gfx::INVALID_TEXTURE;
    const int im = d.nif.find(d.nif.blocks[size_t(tp)].images[0]);
    if (im < 0) return gfx::INVALID_TEXTURE;
    const int raw = d.nif.find(d.nif.blocks[size_t(im)].raw);
    return raw >= 0 ? d.tex[size_t(raw)] : gfx::INVALID_TEXTURE;
}

double fx_rand() { return double(std::rand() % 10000) / 10000.0; }

void fx_update(scene& sc, double now, float dt) {
    for (size_t n = 0; n < g_fx.size(); ) {
        fx_instance& fi = g_fx[n];
        if (now >= fi.until) { g_fx.erase(g_fx.begin() + long(n)); continue; }
        if (fi.actor >= 0 && size_t(fi.actor) < sc.actors.size()) {
            const actor_inst& a = sc.actors[size_t(fi.actor)];
            for (int k = 0; k < 3; ++k) fi.pos[k] = a.node->local.translate[k];
            fi.pos[1] += fi.lift;
            fi.yaw = std::atan2(a.node->local.rotate[2], a.node->local.rotate[8]);
        }
        const float t = float(now - fi.start);
        const fx_def& d = *fi.def;
        for (size_t i = 0; i < d.nif.blocks.size(); ++i) {
            const assets::nif_block& b = d.nif.blocks[i];
            if (b.kind != assets::nif_block::PSYS) continue;
            std::vector<fx_particle>& ps = fi.parts[i];
            for (size_t k = 0; k < ps.size(); ) {
                fx_particle& p = ps[k];
                if (now - p.born > p.life) { ps.erase(ps.begin() + long(k)); continue; }
                for (int c = 0; c < 3; ++c) p.p[c] += p.v[c] * dt;
                ++k;
            }
            // Emit while the effect runs (to its length, or for good if it loops).
            const bool emitting = d.loop || t < d.length;
            if (!emitting) continue;
            const float rate = std::max(5.0f, std::min(b.psys.rate, 200.0f));
            fi.emit_acc[i] += rate * dt;
            float wm[16];
            fx_world(fi, int(i), t, wm);
            // A SuperSpray leaves along its node's +Z (the shower's jet points
            // down, its floor splash up).
            const float up[3] = { wm[8], wm[9], wm[10] };
            const float ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
            if (std::getenv("K2_FXDUMP") && fi.emit_acc[i] >= 1.0 && fi.parts[i].empty())
                std::printf("fx %d block %zu emits at (%.2f %.2f %.2f) along (%.2f %.2f %.2f)\n", fi.id, i,
                            wm[12], wm[13], wm[14], up[0], up[1], up[2]);
            const size_t cap = std::max<size_t>(8, std::min<size_t>(b.psys.max_particles, 200));
            while (fi.emit_acc[i] >= 1.0 && ps.size() < cap) {
                fi.emit_acc[i] -= 1.0;
                fx_particle p;
                for (int c = 0; c < 3; ++c) p.p[c] = wm[12 + c];
                const float spread = std::max(0.25f, std::fabs(b.psys.spread_a));
                const float a = float(fx_rand() * 6.2831853), r = float(fx_rand()) * spread;
                float dir[3] = { up[0] / ul, up[1] / ul, up[2] / ul };
                dir[0] += std::cos(a) * r; dir[2] += std::sin(a) * r;
                const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                const float speed = std::max(0.3f, b.psys.speed * 3.0f) * float(0.8 + 0.4 * fx_rand());
                for (int c = 0; c < 3; ++c) p.v[c] = dir[c] / dl * speed;
                p.born = now;
                p.life = b.psys.raw13[3] > 0.05f ? b.psys.raw13[3] : 1.0f;
                p.size = std::max(0.08f, std::min(b.psys.size, 1.2f));
                ps.push_back(p);
            }
            if (fi.emit_acc[i] > 1.0) fi.emit_acc[i] = 1.0;
        }
        ++n;
    }
}

void fx_draw(scene& sc, const float* view, double now) {
    (void)sc;
    if (g_fx.empty()) return;
    static gfx::mesh_id quads = 0;
    const int maxq = 4000;
    if (!quads) quads = gfx::create_dynamic_mesh(maxq * 4, maxq * 6);
    // Camera right and up, from the view matrix's rows.
    const float rt[3] = { view[0], view[4], view[8] }, upv[3] = { view[1], view[5], view[9] };
    gfx::render_state rs;
    rs.depth = gfx::DEPTH_TEST;
    rs.alpha_test = false;
    rs.vertex_color = true;
    rs.wireframe = false;
    rs.fog = false;
    float ident[16];
    mat4_identity(ident);
    for (size_t n = 0; n < g_fx.size(); ++n) {
        const fx_instance& fi = g_fx[n];
        const fx_def& d = *fi.def;
        const float t = float(now - fi.start);
        for (size_t i = 0; i < d.nif.blocks.size(); ++i) {
            const assets::nif_block& b = d.nif.blocks[i];
            const int ap = fx_prop(d, int(i), assets::nif_block::ALPHA);
            const bool additive = ap < 0 || d.nif.blocks[size_t(ap)].alpha_dst == 0;
            rs.blend = additive ? gfx::BLEND_ADD : gfx::BLEND_ALPHA;
            const int mp = fx_prop(d, int(i), assets::nif_block::MATERIAL);
            float col[4] = { 1, 1, 1, 1 };
            if (mp >= 0) {
                const assets::nif_block& m = d.nif.blocks[size_t(mp)];
                for (int c = 0; c < 3; ++c) col[c] = std::min(1.0f, m.emissive[c] + m.diffuse[c]);
                col[3] = m.alpha;
            }
            if (b.kind == assets::nif_block::SHAPE && d.mesh[i] && !fx_hidden(d, int(i))) {
                float wm[16];
                fx_world(fi, int(i), t, wm);
                gfx::set_render_state(rs);
                gfx::set_model(wm);
                gfx::set_texture(fx_texture(d, int(i)));
                gfx::set_tint(col[0], col[1], col[2], col[3]);
                gfx::draw(d.mesh[i]);
            } else if (b.kind == assets::nif_block::PSYS && !fi.parts[i].empty()) {
                const std::vector<fx_particle>& ps = fi.parts[i];
                std::vector<gfx::vertex> v;
                std::vector<uint16_t> ix;
                v.reserve(ps.size() * 4);
                for (size_t k = 0; k < ps.size() && int(k) < maxq; ++k) {
                    const fx_particle& p = ps[k];
                    const float age = float(now - p.born) / p.life;
                    const float fade = std::max(0.0f, 1.0f - age);
                    const float h = p.size * 0.5f;
                    const float cu[4][2] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
                    const uint16_t base = uint16_t(v.size());
                    for (int c = 0; c < 4; ++c) {
                        gfx::vertex q;
                        std::memset(&q, 0, sizeof(q));
                        q.x = p.p[0] + (rt[0] * cu[c][0] + upv[0] * cu[c][1]) * h;
                        q.y = p.p[1] + (rt[1] * cu[c][0] + upv[1] * cu[c][1]) * h;
                        q.z = p.p[2] + (rt[2] * cu[c][0] + upv[2] * cu[c][1]) * h;
                        q.nz = 1.0f;
                        q.u = (cu[c][0] + 1) * 0.5f; q.v = (1 - cu[c][1]) * 0.5f;
                        q.r = uint8_t(255 * col[0]); q.g = uint8_t(255 * col[1]); q.b = uint8_t(255 * col[2]);
                        q.a = uint8_t(255 * col[3] * fade);
                        v.push_back(q);
                    }
                    const uint16_t qi[6] = { 0, 1, 2, 0, 2, 3 };
                    for (int c = 0; c < 6; ++c) ix.push_back(uint16_t(base + qi[c]));
                }
                if (v.empty()) continue;
                gfx::update_mesh(quads, &v[0], int32_t(v.size()), &ix[0], int32_t(ix.size()));
                gfx::set_render_state(rs);
                gfx::set_model(ident);
                gfx::set_texture(fx_texture(d, int(i)));
                gfx::set_tint(1, 1, 1, 1);
                gfx::draw(quads);
            }
        }
    }
    gfx::set_tint(1, 1, 1, 1);
    gfx::set_texture(gfx::INVALID_TEXTURE);
}

// --- pickups ------------------------------------------------------------------
//
// Traps.txt: Pickups argv1 ActorName, argv2 ItemName, argv3 code (a script),
// then optional reach / grasp / use animations. In the data a health potion is
// argv1 "1" and argv2 an amount ("24"); the Prince's sword is argv1
// "PrinceSword01", argv2 "weapon", argv3 the script that follows. E picks up.
struct pickup {
    std::string name, actor, item, code;
    float       pos[3];
    bool        taken;
};

// Whether a pickup's trap is shown and enabled (Visible:T / Enable:T).
bool pickup_live(scene& sc, size_t i) {
    const std::string& nm = sc.pickups[i].name;
    for (size_t t = 0; t < sc.traps.size(); ++t)
        if (sc.traps[t]->name() == nm) return sc.traps[t]->visible() && sc.traps[t]->enabled();
    return true;
}

// The pickup handler (0x47FC8C). argv1 is the item: a number is a potion,
// mapped by the table built at 0x4EB9D6 to the effect it applies with argv2
// as its argument; a name is an object (PrinceSword01 and argv2 "weapon"
// arms the Prince). Durations become absolute game times; -1 is for good.
// Taking a pickup: argv4..7 name the Prince's reach, grasp, use and finish
// clips (Traps.txt); left empty, the kind of item picks them - a potion is
// bent for, drunk and finished by its effect (DrinkFinish<Effect>_G), a
// weapon inspected, anything else bent down for. Which clips the original
// picks by default is not traced.
struct pickup_seq {
    int pickup;
    std::vector<std::string> clips;
    size_t step;
    bool taken, applied;
    pickup_seq() : pickup(-1), step(0), taken(false), applied(false) {}
    bool active() const { return pickup >= 0; }
    void begin(scene& sc, const actor_inst& p, const scene::pickup_def& pk, int i) {
        static const char* finish[11] = { 0, "Heal", "Life", "Strength", "Impervious", "Jump", "Fall",
                                          "Camouflage", "Guard", "FullHeal", "Poison" };
        pickup = i;
        step = 0;
        taken = applied = false;
        clips.clear();
        for (size_t a = 4; a <= 7 && a < pk.argv.size(); ++a)
            if (!pk.argv[a].empty() && find_actor_skill(sc, p, pk.argv[a])) clips.push_back(pk.argv[a]);
        if (!clips.empty()) return;
        const std::string a1 = pk.argv.size() > 1 ? pk.argv[1] : "";
        const std::string a2 = pk.argv.size() > 2 ? pk.argv[2] : "";
        if (!a1.empty() && a1.find_first_not_of("0123456789") == std::string::npos) {
            const int type = std::atoi(a1.c_str());
            clips.push_back("BendDownToGrab_G");
            clips.push_back("DrinkPotion_G");
            clips.push_back(type >= 1 && type <= 10 ? std::string("DrinkFinish") + finish[type] + "_G"
                                                    : std::string("DrinkPotionFinish_G"));
        } else if (equals_nocase(a2, "weapon")) {
            clips.push_back("InspectWeaponFromGound_G");
            clips.push_back("InspectWeaponFinish_G");
        } else {
            clips.push_back("BendDownToGrabObject_G");
        }
        std::vector<std::string> have;
        for (size_t c = 0; c < clips.size(); ++c)
            if (find_actor_skill(sc, p, clips[c])) have.push_back(clips[c]);
        clips.swap(have);
    }
};

void pickup_apply(combat_state& cs, scene& sc, actor_inst& p, const scene::pickup_def& pk, double now) {
    const std::string a1 = pk.argv.size() > 1 ? pk.argv[1] : "";
    const std::string a2 = pk.argv.size() > 2 ? pk.argv[2] : "";
    std::string lo = a2;
    for (size_t i = 0; i < lo.size(); ++i) lo[i] = char(std::tolower(static_cast<unsigned char>(lo[i])));
    if (lo == "weapon") {
        // The sword arms him here; the bow and arrows come from their
        // scripts ("prince,weapon:+bow").
        std::string l1 = a1;
        for (size_t i = 0; i < l1.size(); ++i) l1[i] = char(std::tolower(static_cast<unsigned char>(l1[i])));
        if (l1.find("sword") != std::string::npos) {
            p.armed = true;
            std::printf("pickup: %s - the Prince has a sword (R draws it)\n", pk.name.c_str());
        } else {
            std::printf("pickup: %s (%s)\n", pk.name.c_str(), a1.c_str());
        }
        return;
    }
    if (lo == "reload") {
        // "arrows","reload","<type>,<count>,<?>": a quiver's worth.
        const std::string a3 = pk.argv.size() > 3 ? pk.argv[3] : "";
        const size_t c = a3.find(',');
        const int n = c == std::string::npos ? 1 : std::atoi(a3.c_str() + c + 1);
        p.arrows += n;
        std::printf("pickup: %s - %d arrows, %d now\n", pk.name.c_str(), n, p.arrows);
        return;
    }
    if (a1.empty() || a1.find_first_not_of("0123456789") != std::string::npos) {
        std::printf("pickup: %s (%s)\n", pk.name.c_str(), a1.c_str());
        return;
    }
    const int type = std::atoi(a1.c_str());
    const double v0 = std::atof(a2.c_str());
    const size_t comma = a2.find(',');
    const double v1 = comma == std::string::npos ? -1.0 : std::atof(a2.c_str() + comma + 1);
    struct until { static double at(double d, double now) { return d > 0.0 ? now + d : 1.0e30; } };
    const char* what = "?";
    switch (type) {
    case 1:  what = "Heal";       p.health = std::min(p.max_health, p.health + int(v0)); break;
    case 9:  what = "FullHeal";   p.health = p.max_health; break;
    case 2:  what = "ExtraLife";  p.max_health = std::min(10 * combat_tuning::BOTTLE, p.max_health + combat_tuning::BOTTLE);
                                  p.health = p.max_health; break;
    case 10: what = "Poison";     prince_damage(cs, sc, p, int(v0), 0, now); break;
    case 3:  what = "Strength";   g_potions.strength = float(v0);
                                  g_potions.strength_until = until::at(comma == std::string::npos ? -1 : v1, now); break;
    case 4:  what = "Shield";     g_potions.shield_until = until::at(v0, now); break;
    case 5:  what = "Jump";       g_potions.jump = float(v0);
                                  g_potions.jump_until = until::at(comma == std::string::npos ? -1 : v1, now); break;
    case 6:  what = "Fall";       g_potions.fall_until = until::at(v0, now); break;
    case 7:  what = "Camo";       g_potions.camo_until = until::at(v0, now); break;
    case 8:  what = "Guard";      g_potions.guard_until = until::at(v0, now); break;
    default: break;
    }
    std::printf("pickup: %s - %s %s, health %d/%d\n", pk.name.c_str(), what, a2.c_str(), p.health, p.max_health);
    // The drink's burst and, for the lasting ones, the aura while it holds
    // (geometry/utils/Potion<Name>.NIF and Potion<Name>Residual.NIF, the
    // names at 0x4EB3D4).
    static const char* fx_names[11] = { 0, "Heal", "Life", "Strength", "Impervious", "Jump", "Fall",
                                        "Camouflage", "Guard", "FullHeal", "Poison" };
    if (type >= 1 && type <= 10) {
        int who = -1;
        for (size_t i = 0; i < sc.actors.size(); ++i) if (&sc.actors[i] == &p) who = int(i);
        const std::string base = std::string("Potion") + fx_names[type];
        fx_spawn(base + ".NIF", p.node->local.translate, actor_yaw(p), now, -1.0, who);
        double lasts = 3.0;
        if (type == 3 || type == 5) lasts = comma == std::string::npos ? -1.0 : v1;
        else if (type == 4 || type == 6 || type == 7 || type == 8) lasts = v0;
        fx_spawn(base + "Residual.NIF", p.node->local.translate, actor_yaw(p), now,
                 lasts > 0.0 ? lasts : 1.0e9, who);
    }
}
// The game module, as the original structures it: `gameModule` owns a level and
// is entered through the module manager rather than being loaded inline.
//
// Slot 0 is the entry point (pure virtual in the original, overridden by every
// subclass), so the level load happens there. The remaining slots stay stubs
// until their roles are established - naming them enter/update/render would be
// guessing, and that has already gone wrong once in this project.
class game_module : public k2::engine::module {
public:
    game_module(int32_t module_id, int region,
                scene* sc, assets::lvl_file* lv,
                k2::engine::body_table* bodies,
                std::vector<k2::engine::actor_spawn>* spawns)
        : m_id(module_id), m_region(region), m_sc(sc), m_lv(lv),
          m_bodies(bodies), m_spawns(spawns), m_loaded(false) {}

    bool vf0() {
        const k2::engine::module_entry* e = k2::engine::module_at(m_id);
        if (!e || !e->level[0]) {
            std::fprintf(stderr, "game_module: id %d has no level\n", m_id);
            return false;
        }
        scene_reset(*m_sc);
        m_loaded = load_level(e->level, *m_sc, *m_lv, m_region);
        if (!m_loaded) return false;

        build_body_table(*m_lv, *m_bodies);
        spawn_prince_from_level(*m_lv, *m_sc, *m_bodies, *m_spawns, m_region);
        spawn_level_actors(*m_sc, *m_bodies, *m_spawns);
        std::printf("  actors: %zu spawned (prince index %d)\n",
                    m_spawns->size(), k2::engine::find_prince(*m_spawns));
        return true;
    }

    bool loaded() const { return m_loaded; }

    k2::engine::module_id id() const { return k2::engine::MODULE_GAME; }

private:
    int32_t m_id;
    int     m_region;
    scene*  m_sc;
    assets::lvl_file* m_lv;
    k2::engine::body_table* m_bodies;
    std::vector<k2::engine::actor_spawn>* m_spawns;
    bool    m_loaded;
};

void actor_part_matrices(const scene& sc, actor_inst& a, bool skin) {
    a.part_mats.resize(a.item_count * 16);
    if (!a.node || a.item_count == 0) return;
    const k2::engine::transform& aw = a.node->world();
    float am[16];
    aw.to_matrix(am);
    for (size_t k = 0; k < a.item_count; ++k) {
        const draw_item& di = sc.items[a.first_item + k];
        float* m = &a.part_mats[k * 16];
        if (a.skinned && skin && di.joint >= 0 && size_t(di.joint) < a.cur.size()) {
            k2::engine::transform t0, t1;
            k2::engine::transform_compose(a.cur[size_t(di.joint)], a.bind_inv[size_t(di.joint)], t0);
            k2::engine::transform_compose(aw, t0, t1);
            t1.to_matrix(m);
        } else {
            std::memcpy(m, am, sizeof(am));
        }
    }
}

// Pole numbers from the binary (docs/reference/poles-ropes-bars.md).
namespace bar_tuning {
const float STEP         = 0.5717f;  // a shimmy step (0x74AB04)
const float LAUNCH_FROM  = 0.43f, LAUNCH_TO = 0.75f;   // swing phase window (0x74AF70, 0x743D64)
const float LAUNCH_AHEAD = 1.61f;    // placed this far ahead of the bar (0x74AFA4)
const float LAUNCH_Y     = 4.5f;     // fHorizLaunchY
const float LAUNCH_Z     = 5.2f;     // fHorizLaunchZ
// Ours: how near the hands must come to catch one.
const float REACH        = 0.5f;
const float STAND_REACH  = 1.0f;     // above his hands, standing, for the action input
}

namespace pole_tuning {
const float HOLD        = 2.25f;    // hands above the feet (0x74AF68)
const float FEET_CLEAR  = 0.25f;    // feet stay this far above the bottom
const float CLIMB_STEP  = 0.393f;   // a climb clip's rise (0x74AAD0)
const float SLIDE_STEP  = 0.4723f;  // start / end slide (0x74AAD4)
const float SLIDE_SPEED = 3.0f;     // the slide loop, units a second (0x74AB64)
const float TURN_RATE   = 1.0471976f;   // pi/3 a second round the pole
// Ours: the original notices a pole by touching it (Prince__CollidePoleFilter).
const float REACH       = 1.0f;
const float AIR_REACH   = 0.7f;
}

// --- saves: the level's state ------------------------------------------------------
//
// Ours: the original's .pop is not read or written. Each line is one thing:
//   t <trap> <state> <enabled> <visible> <collideable>
//   m <trap> <where a moved trap, door or collapse has got to>
//   p <pickup index taken>
//   a <actor> <hidden> <health> <dead> <a prop's held clip>
//   b <has bow> <arrows>
std::vector<std::string> world_capture(scene& sc, const combat_state& cs, const std::vector<bool>& taken) {
    std::vector<std::string> out;
    char b[1024];
    for (size_t i = 0; i < sc.traps.size(); ++i) {
        k2::engine::trap_object* t = sc.traps[i];
        if (!t || t->name().empty() || t->name().find(' ') != std::string::npos) continue;
        std::snprintf(b, sizeof(b), "t %s %s %d %d %d", t->name().c_str(), t->state().empty() ? "-" : t->state().c_str(),
                      int(t->enabled()), int(t->visible()), int(t->collideable()));
        out.push_back(b);
        if (scene_trap* st = dynamic_cast<scene_trap*>(t)) {
            std::string m;
            st->save_line(m);
            if (!m.empty()) out.push_back("m " + t->name() + " " + m);
        }
    }
    for (size_t i = 0; i < taken.size(); ++i)
        if (taken[i]) { std::snprintf(b, sizeof(b), "p %zu", i); out.push_back(b); }
    for (size_t i = 0; i < sc.actors.size(); ++i) {
        const actor_inst& a = sc.actors[i];
        if (is_prince(a)) {
            std::snprintf(b, sizeof(b), "b %d %d", int(a.has_bow), a.arrows);
            out.push_back(b);
            continue;
        }
        if (a.script_name.empty() || a.script_name.find(' ') != std::string::npos) continue;
        bool dead = false;
        for (size_t e = 0; e < cs.enemies.size(); ++e)
            if (cs.enemies[e].actor == int(i)) dead = cs.enemies[e].state == enemy_ai::DEAD;
        std::snprintf(b, sizeof(b), "a %s %d %d %d %s", a.script_name.c_str(), int(a.hidden), a.health, int(dead),
                      a.held_clip.empty() ? "-" : a.held_clip.c_str());
        out.push_back(b);
    }
    return out;
}

void world_apply(scene& sc, combat_state& cs, std::vector<bool>& taken, const std::vector<std::string>& lines) {
    for (size_t l = 0; l < lines.size(); ++l) {
        const std::string& ln = lines[l];
        char kind = ln.empty() ? 0 : ln[0];
        char name[256], st[256];
        if (kind == 't') {
            int en = 1, vis = 1, col = 1;
            if (std::sscanf(ln.c_str() + 2, "%255s %255s %d %d %d", name, st, &en, &vis, &col) < 5) continue;
            for (size_t i = 0; i < sc.traps.size(); ++i) {
                k2::engine::trap_object* t = sc.traps[i];
                if (!t || t->name() != name) continue;
                t->set_state(std::strcmp(st, "-") == 0 ? std::string() : std::string(st));
                if (t->enabled() != (en != 0)) t->set_enabled(en != 0);
                if (t->visible() != (vis != 0)) t->set_visible(vis != 0);
                if (t->collideable() != (col != 0)) t->set_collideable(col != 0);
            }
        } else if (kind == 'm') {
            const size_t sp = ln.find(' ', 2);
            if (sp == std::string::npos) continue;
            const std::string nm = ln.substr(2, sp - 2);
            for (size_t i = 0; i < sc.traps.size(); ++i)
                if (scene_trap* t = dynamic_cast<scene_trap*>(sc.traps[i]))
                    if (t->name() == nm) t->restore_line(ln.substr(sp + 1));
        } else if (kind == 'p') {
            const size_t i = size_t(std::atoi(ln.c_str() + 2));
            if (i < taken.size()) {
                taken[i] = true;
                if (sc.pickups[i].actor >= 0) sc.actors[size_t(sc.pickups[i].actor)].hidden = true;
            }
        } else if (kind == 'b') {
            int bow = 0, arrows = 0;
            std::sscanf(ln.c_str() + 2, "%d %d", &bow, &arrows);
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) { sc.actors[i].has_bow = bow != 0; sc.actors[i].arrows = arrows; }
        } else if (kind == 'a') {
            int hidden = 0, health = 0, dead = 0;
            if (std::sscanf(ln.c_str() + 2, "%255s %d %d %d %255s", name, &hidden, &health, &dead, st) < 5) continue;
            for (size_t i = 0; i < sc.actors.size(); ++i) {
                actor_inst& a = sc.actors[i];
                if (a.script_name != name || is_prince(a)) continue;
                a.hidden = hidden != 0;
                if (health > 0) a.health = health;
                if (std::strcmp(st, "-") != 0 && play_once(sc, a, st, -1.0e6, a.home_yaw, true))
                    a.held_clip = st;                   // played out long ago: at its end now
                for (size_t e = 0; e < cs.enemies.size(); ++e) {
                    enemy_ai& en = cs.enemies[e];
                    if (en.actor != int(i) || !dead) continue;
                    en.state = enemy_ai::DEAD;
                    a.health = 0;
                    a.stance = 0;
                    if (!play_once(sc, a, "CombatDeathGeneric" + en.set, -1.0e6, actor_yaw(a), true))
                        play_once(sc, a, "CombatDying" + en.set, -1.0e6, actor_yaw(a), true);
                }
            }
        }
    }
    std::printf("save: %zu world lines applied\n", lines.size());
}

int main(int argc, char** argv) {
    if (std::getenv("K2_LINEBUF")) std::setvbuf(stdout, 0, _IOLBF, 0);     // for timing the log
    // Started without arguments on a console: the platform's defaults.
    std::vector<std::string> default_args;
    std::vector<char*> default_argv;
    if (argc <= 1) {
        plat::default_args(default_args);
        if (!default_args.empty()) {
            default_argv.push_back(argv[0]);
            for (size_t i = 0; i < default_args.size(); ++i)
                default_argv.push_back(const_cast<char*>(default_args[i].c_str()));
            default_argv.push_back(0);
            argc = int(default_argv.size()) - 1;
            argv = &default_argv[0];
        }
    }
    const char* data_root = DEFAULT_DATA_ROOT;
    const char* dump_path = 0;
    const char* room_path = 0;
    const char* level_path = 0;
    const char* dumplevel_path = 0;
    bool verify_only = false;
    bool images_only = false;
    bool masters_only = false;
    bool actors_only = false;
    bool skills_only = false;
    bool spawns_only = false;
    const char* shot_path = 0;
    const char* cam_arg = 0;      // "x,y,z,yaw,pitch" — diagnostic override
    bool trig_off = false;
    bool rev_draw = false;
    bool materials_only = false;
    bool winding_only = false;
    bool autowalk = false;
    bool autorun  = false;
    bool noanim   = false;
    bool noclip   = false;
    bool nif_check = false;
    bool mov_info = false;
    std::vector<std::string> movies_to_play;   // --movie NAME (repeatable): play before the game
    const char* mov_audio = 0;        // --movaudio MOVIE: decode its whole sound track, report
    const char* mov_frame_file = 0;   // --movframe MOVIE N OUT.bmp: decode frames 0..N, save N
    int mov_frame_index = 0;
    const char* mov_frame_out = 0;       // list every movie's tracks and codecs
    const char* text_test = 0;   // render a string in Tsunami.ttf to a .bmp      // parse every .nif in the install and report
    bool noscript = false;
    bool autopush = false;
    bool autojump = false;
    bool classic  = false;
    bool mute     = false;
    bool start_armed = false;
    bool use_menu = false;          // --menu: the title screens, Esc menu in play
    bool menu_in_game_only = false; // --menu-game: straight into play, Esc menu only
    int  start_health = -1, start_max_health = -1;   // --health H,MAX (from a save)
    bool sounds_only = false;
    bool find_ledges = false;
    struct key_hold { std::string keys; double from, to; };
    std::vector<key_hold> holds;                           // --hold KEYS@T0[-T1]
    std::vector<std::string> send_args;
    std::vector<std::string> world_lines;   // a save's level state, applied once the level is up
    const char* place_arg = 0;
    bool trapmsg  = false;
    float fixed_dt = 0.0f;
    bool noskin = false;
    const char* pose_arg = 0;   // "Joint:degrees" - proves a joint drives its part        // draw parts at the actor transform, unbound
    bool skeletons_only = false;      // headless: hold forward, to test movement
    int  shot_frame = 3;
    // .K2A winding measured as CCW-outward over 1201 actor meshes (--winding),
    // so GL's default front face is correct and back faces can be dropped.
    // Verified identical to CULL_NONE from inside a room; from outside it
    // correctly reveals the interior instead of the wall's back face.
    gfx::cull_mode cull_mode_arg = gfx::CULL_BACK;
    const char* bind_path = 0;
    bool config_only = false;
    int  region_arg = 0;      // the original streams one region; -1 loads all
    bool boot = false;
    bool modules_only = false;
    const char* actnodes_path = 0;
    const char* dumpskl_path = 0;
    const char* actor_path = 0;
    const char* dumpactor_path = 0;

    for (int i = 1; i < argc; ++i) {
        if      (std::strcmp(argv[i], "--verify") == 0) verify_only = true;
        else if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dump_path = argv[++i];
        else if (std::strcmp(argv[i], "--room") == 0 && i + 1 < argc) room_path = argv[++i];
        else if (std::strcmp(argv[i], "--images") == 0) images_only = true;
        else if (std::strcmp(argv[i], "--masters") == 0) masters_only = true;
        else if (std::strcmp(argv[i], "--actors") == 0) actors_only = true;
        else if (std::strcmp(argv[i], "--skills") == 0) skills_only = true;
        else if (std::strcmp(argv[i], "--spawns") == 0) spawns_only = true;
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot_path = argv[++i];
        else if (std::strcmp(argv[i], "--cam") == 0 && i + 1 < argc) cam_arg = argv[++i];
        else if (std::strcmp(argv[i], "--notrig") == 0) trig_off = true;
        else if (std::strcmp(argv[i], "--revdraw") == 0) rev_draw = true;
        else if (std::strcmp(argv[i], "--materials") == 0) materials_only = true;
        else if (std::strcmp(argv[i], "--winding") == 0) winding_only = true;
        else if (std::strcmp(argv[i], "--autowalk") == 0) autowalk = true;
        else if (std::strcmp(argv[i], "--autorun") == 0) autowalk = autorun = true;
        else if (std::strcmp(argv[i], "--noanim") == 0) noanim = true;
        else if (std::strcmp(argv[i], "--noclip") == 0) noclip = true;
        else if (std::strcmp(argv[i], "--noscript") == 0) noscript = true;
        else if (std::strcmp(argv[i], "--nifcheck") == 0) nif_check = true;
        else if (std::strcmp(argv[i], "--movinfo") == 0) mov_info = true;
        else if (std::strcmp(argv[i], "--movie") == 0 && i + 1 < argc) movies_to_play.push_back(argv[++i]);
        else if (std::strcmp(argv[i], "--movaudio") == 0 && i + 1 < argc) mov_audio = argv[++i];
        else if (std::strcmp(argv[i], "--movframe") == 0 && i + 3 < argc) {
            mov_frame_file = argv[++i]; mov_frame_index = std::atoi(argv[++i]); mov_frame_out = argv[++i];
        }
        else if (std::strcmp(argv[i], "--texttest") == 0 && i + 1 < argc) text_test = argv[++i];
        else if (std::strcmp(argv[i], "--autopush") == 0) autopush = true;
        else if (std::strcmp(argv[i], "--autojump") == 0) autojump = true;
        else if (std::strcmp(argv[i], "--findledges") == 0) find_ledges = true;
        else if (std::strcmp(argv[i], "--mute") == 0) mute = true;
        else if (std::strcmp(argv[i], "--armed") == 0) start_armed = true;
        else if (std::strcmp(argv[i], "--world") == 0 && i + 1 < argc) {
            // The level state of a save being loaded (written by the game menu).
            if (FILE* wf = std::fopen(argv[++i], "r")) {
                char line[1024];
                while (std::fgets(line, sizeof(line), wf)) {
                    std::string l = line;
                    while (!l.empty() && (l[l.size() - 1] == '\n' || l[l.size() - 1] == '\r')) l.erase(l.size() - 1);
                    if (!l.empty()) world_lines.push_back(l);
                }
                std::fclose(wf);
            }
        }
        else if (std::strcmp(argv[i], "--menu") == 0) use_menu = true;
        else if (std::strcmp(argv[i], "--menu-game") == 0) { use_menu = true; menu_in_game_only = true; }
        else if (std::strcmp(argv[i], "--health") == 0 && i + 1 < argc)
            std::sscanf(argv[++i], "%d,%d", &start_health, &start_max_health);
        else if (std::strcmp(argv[i], "--sounds") == 0) sounds_only = true;
        else if (std::strcmp(argv[i], "--hold") == 0 && i + 1 < argc) {
            const std::string h = argv[++i];
            const size_t at = h.find('@');
            key_hold kh;
            kh.keys = h.substr(0, at);
            kh.from = at == std::string::npos ? 0.0 : std::atof(h.c_str() + at + 1);
            const size_t dash = at == std::string::npos ? std::string::npos : h.find('-', at + 1);
            kh.to = dash == std::string::npos ? 1e30 : std::atof(h.c_str() + dash + 1);
            holds.push_back(kh);
        }
        else if (std::strcmp(argv[i], "--controls") == 0 && i + 1 < argc)
            classic = std::strcmp(argv[++i], "classic") == 0;
        else if (std::strcmp(argv[i], "--send") == 0 && i + 1 < argc) send_args.push_back(argv[++i]);
        else if (std::strcmp(argv[i], "--place") == 0 && i + 1 < argc) place_arg = argv[++i];
        else if (std::strcmp(argv[i], "--trapmsg") == 0) trapmsg = true;
        else if (std::strcmp(argv[i], "--fixeddt") == 0 && i + 1 < argc) fixed_dt = float(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--noskin") == 0) noskin = true;
        else if (std::strcmp(argv[i], "--pose") == 0 && i + 1 < argc) pose_arg = argv[++i];
        else if (std::strcmp(argv[i], "--skeletons") == 0) skeletons_only = true;
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) shot_frame = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--cull") == 0 && i + 1 < argc) {
            const char* m = argv[++i];
            cull_mode_arg = !std::strcmp(m, "back")  ? gfx::CULL_BACK
                          : !std::strcmp(m, "front") ? gfx::CULL_FRONT
                                                     : gfx::CULL_NONE;
        }
        else if (std::strcmp(argv[i], "--bind") == 0 && i + 1 < argc) bind_path = argv[++i];
        else if (std::strcmp(argv[i], "--config") == 0) config_only = true;
        else if (std::strcmp(argv[i], "--region") == 0 && i + 1 < argc) region_arg = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--boot") == 0) boot = true;
        else if (std::strcmp(argv[i], "--modules") == 0) modules_only = true;
        else if (std::strcmp(argv[i], "--actnodes") == 0 && i + 1 < argc) actnodes_path = argv[++i];
        else if (std::strcmp(argv[i], "--skl") == 0 && i + 1 < argc) dumpskl_path = argv[++i];
        else if (std::strcmp(argv[i], "--actor") == 0 && i + 1 < argc) actor_path = argv[++i];
        else if (std::strcmp(argv[i], "--dumpactor") == 0 && i + 1 < argc) dumpactor_path = argv[++i];
        else if (std::strcmp(argv[i], "--level") == 0 && i + 1 < argc) level_path = argv[++i];
        else if (std::strcmp(argv[i], "--dumplevel") == 0 && i + 1 < argc) dumplevel_path = argv[++i];
        else                                            data_root = argv[i];
    }

    std::printf("mounting %s\n", data_root);
    std::fflush(stdout);
    if (!plat::fs_mount(data_root)) {
        std::fprintf(stderr,
                     "k2boy: could not mount data root '%s'\n"
                     "usage: k2boy [--verify] [path-to-retail-install]\n",
                     data_root);
        return 1;
    }
    if (text_test) {
        size_t size = 0;
        uint8_t* d = plat::fs_read_basename("Tsunami.ttf", size);
        gfx::xf_font font;
        std::memset(&font, 0, sizeof(font));
        if (!d || gfx::xf_font_init(&font, d, size) != 0) { std::printf("text: no font\n"); return 1; }
        const char* msg = "Prince of Persia 3D";
        const float px = 48.0f;
        const int w = 900, h = 80;
        std::vector<uint8_t> img(size_t(w) * h * 4, 0);
        float pen = 10.0f;
        for (const char* c = msg; *c; ++c) {
            const int g = gfx::xf_font_glyph_index(&font, uint32_t(uint8_t(*c)));
            gfx::xf_glyph gl;
            if (gfx::xf_font_render(&font, g, px, &gl) == 0) {
                for (int y = 0; y < gl.height; ++y)
                    for (int x = 0; x < gl.width; ++x) {
                        const int X = int(pen) + gl.x0 + x, Y = 60 + gl.y0 + y;
                        if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                        uint8_t* q = &img[size_t(Y * w + X) * 4];
                        q[0] = q[1] = q[2] = std::max(q[0], gl.alpha[y * gl.width + x]); q[3] = 255;
                    }
                std::free(gl.alpha);
            }
            pen += gfx::xf_font_advance(&font, g, px);
        }
        write_bmp(text_test, w, h, &img[0]);
        std::printf("text: wrote %s\n", text_test);
        return 0;
    }
    if (mov_frame_file) {
        media::xf_mov mv;
        std::memset(&mv, 0, sizeof(mv));
        if (media::xf_mov_open(&mv, mov_frame_file) != 0) { std::printf("movframe: cannot open\n"); return 1; }
        media::xf_mov_track* vt = media::xf_mov_find(&mv, media::XF_TRACK_VIDEO);
        if (!vt) { std::printf("movframe: no video\n"); return 1; }
        media::svq1_decoder dec(vt->width, vt->height);
        std::vector<uint8_t> buf;
        int good = 0;
        for (int f = 0; f <= mov_frame_index && uint32_t(f) < vt->sample_count; ++f) {
            buf.resize(vt->sample_size[f]);
            media::xf_mov_read_sample(&mv, vt, uint32_t(f), buf.empty() ? 0 : &buf[0]);
            const bool okf = dec.decode(buf.empty() ? 0 : &buf[0], buf.size());
            good += okf;
            if (!okf && std::getenv("K2_MOVDBG")) std::printf("  frame %d failed (%zu bytes, first %02x %02x %02x %02x)\n", f, buf.size(),
                buf.size() > 3 ? buf[0] : 0, buf.size() > 3 ? buf[1] : 0, buf.size() > 3 ? buf[2] : 0, buf.size() > 3 ? buf[3] : 0);
        }
        std::printf("movframe: %d of %d frames decoded\n", good, mov_frame_index + 1);
        if (dec.rgba()) write_bmp(mov_frame_out, dec.width(), dec.height(), dec.rgba());
        media::xf_mov_close(&mv);
        return 0;
    }
    if (mov_audio) {
        media::xf_mov mv;
        std::memset(&mv, 0, sizeof(mv));
        if (media::xf_mov_open(&mv, mov_audio) != 0) return 1;
        media::xf_mov_track* at = media::xf_mov_find(&mv, media::XF_TRACK_AUDIO);
        if (!at) return 1;
        char cc[5];
        std::printf("audio %s %u Hz %uch, %u chunks, spp %u bpp %u bpf %u\n", media::xf_fourcc_str(at->codec, cc),
                    at->sample_rate, at->channels, at->chunk_count, at->samples_per_packet, at->bytes_per_packet,
                    at->bytes_per_frame);
        // Extra data from the 'frma' atom on (the QDM2 header).
        size_t ex = 0;
        for (size_t k = 4; k + 4 <= at->desc_size; ++k)
            if (std::memcmp(at->desc + k, "frma", 4) == 0) { ex = k - 4; break; }
        media::qdm2_decoder q(at->desc + ex, at->desc_size - ex);
        std::vector<int16_t> pcm;
        std::vector<uint8_t> chunk;
        for (uint32_t c = 0; c < at->chunk_count; ++c) {
            const uint32_t n = at->chunk_samples[c];
            const uint32_t bytes = at->samples_per_packet ? n / at->samples_per_packet * at->bytes_per_frame
                                                          : n * at->channels * 2;
            chunk.resize(bytes);
            if (bytes) media::xf_read_at(mv.file, at->chunk_offset[c], &chunk[0], bytes);
            if (at->codec == 0x51444D32 /* QDM2 */) q.decode(bytes ? &chunk[0] : 0, bytes, pcm);
        }
        if (const char* raw = std::getenv("K2_PCM_OUT")) {         // raw s16le dump, for comparison
            FILE* f = std::fopen(raw, "wb");
            if (f) { std::fwrite(pcm.empty() ? 0 : &pcm[0], 2, pcm.size(), f); std::fclose(f); }
        }
        double sum = 0;
        for (size_t k = 0; k < pcm.size(); ++k) sum += double(pcm[k]) * pcm[k];
        std::printf("audio: %zu samples (%.1f s), rms %.0f\n", pcm.size(),
                    at->channels ? double(pcm.size()) / at->channels / at->sample_rate : 0.0,
                    pcm.empty() ? 0.0 : std::sqrt(sum / double(pcm.size())));
        media::xf_mov_close(&mv);
        return 0;
    }
    if (mov_info) {
        std::vector<std::string> movs;
        plat::fs_list_paths_by_extension(".mov", movs);
        for (size_t i = 0; i < movs.size(); ++i) {
            media::xf_mov mv;
            std::memset(&mv, 0, sizeof(mv));
            if (media::xf_mov_open(&mv, movs[i].c_str()) != 0) { std::printf("mov %s: unreadable\n", movs[i].c_str()); continue; }
            std::printf("mov %-24s %u tracks, %.1f s\n", movs[i].c_str(), mv.track_count,
                        mv.timescale ? double(mv.duration) / mv.timescale : 0.0);
            for (uint32_t t = 0; t < mv.track_count; ++t) {
                const media::xf_mov_track& tr = mv.tracks[t];
                char cc[5];
                std::printf("    %s %s %ux%u %u Hz %uch, %u samples\n",
                            tr.kind == media::XF_TRACK_VIDEO ? "video" : tr.kind == media::XF_TRACK_AUDIO ? "audio" : "other",
                            media::xf_fourcc_str(tr.codec, cc), tr.width, tr.height, tr.sample_rate, tr.channels,
                            tr.sample_count);
            }
            media::xf_mov_close(&mv);
        }
        return 0;
    }
    if (nif_check) {
        std::vector<std::string> nifs;
        plat::fs_list_paths_by_extension(".nif", nifs);
        int whole = 0;
        for (size_t i = 0; i < nifs.size(); ++i) {
            size_t size = 0;
            uint8_t* d = plat::fs_read(nifs[i].c_str(), size);
            if (!d) continue;
            assets::nif_file nf;
            std::string err;
            assets::nif_parse(d, size, nf, &err);
            plat::fs_free(d);
            size_t psys = 0, shapes = 0, images = 0;
            for (size_t k = 0; k < nf.blocks.size(); ++k) {
                psys += nf.blocks[k].kind == assets::nif_block::PSYS;
                shapes += nf.blocks[k].kind == assets::nif_block::SHAPE;
                images += nf.blocks[k].kind == assets::nif_block::RAW_IMAGE;
            }
            whole += nf.complete;
            std::printf("nif %-48s %4zu blocks %2zu psys %2zu shapes %2zu images %s%s\n", nifs[i].c_str(),
                        nf.blocks.size(), psys, shapes, images, nf.complete ? "ok" : "stopped at ",
                        nf.complete ? "" : nf.stopped_at.c_str());
        }
        std::printf("nif: %d of %zu read whole\n", whole, nifs.size());
        return 0;
    }
    verify_data();
    verify_rtti();
    verify_nodes();
    std::fflush(stdout);

    if (dumpactor_path) {
        assets::master_file m;
        return describe_actor(dumpactor_path, m) ? 0 : 1;
    }
    if (skills_only)    return dump_skills();
    if (sounds_only)    return dump_sounds();
    if (spawns_only)    return check_spawns();
    if (bind_path)      return check_bind_pose(bind_path);
    if (config_only)    return dump_config("distribution.dat");
    if (modules_only)   return dump_modules();
    if (materials_only) return check_materials();
    if (winding_only)   return check_winding();
    if (skeletons_only) return check_skeletons();
    if (actnodes_path)  return dump_act_nodes(actnodes_path);
    if (dumpskl_path) {
        size_t sz = 0;
        uint8_t* dd = plat::fs_read(dumpskl_path, sz);
        if (!dd) { std::fprintf(stderr, "k2boy: cannot read '%s'\n", dumpskl_path); return 1; }
        assets::skl_file sk; std::string e;
        const bool good = assets::skl_parse(dd, sz, sk, &e);
        plat::fs_free(dd);
        if (!good) { std::fprintf(stderr, "k2boy: %s\n", e.c_str()); return 1; }
        std::printf("%s\n  '%s' kind %s v%d chunk %s\n  JKey %zu  SKey %zu  vKey %zu  duration %u\n",
                    dumpskl_path, sk.name.c_str(), sk.kind_tag.c_str(), sk.version,
                    sk.chunk_tag.c_str(), sk.joint_keys.size(), sk.sound_keys.size(),
                    sk.event_keys.size(), sk.duration());
        for (size_t k = 0; k < sk.joint_keys.size() && (k < 3 || std::getenv("K2_ALLKEYS")); ++k) {
            const assets::skl_joint_key& j = sk.joint_keys[k];
            std::printf("    key t=%-6u ease %.4f/%.4f\n", j.time, j.ease_in, j.ease_out);
            for (int t = 0; t < 2; ++t)
                std::printf("      xf%d pos=(%7.3f,%7.3f,%7.3f) quat=(%7.4f,%7.4f,%7.4f,%7.4f)\n",
                            t, j.xf[t].pos[0], j.xf[t].pos[1], j.xf[t].pos[2],
                            j.xf[t].quat[0], j.xf[t].quat[1], j.xf[t].quat[2], j.xf[t].quat[3]);
        }
        if (std::getenv("K2_ALLKEYS"))
            for (size_t j = 0; j < sk.joints.size(); ++j)
                if (!sk.joints[j].keys.empty())
                    std::printf("    joint %zu: %zu keys, first (%.3f %.3f %.3f) last (%.3f %.3f %.3f) q0 (%.3f %.3f %.3f %.3f)\n", j, sk.joints[j].keys.size(),
                        sk.joints[j].keys[0].xf[0].pos[0], sk.joints[j].keys[0].xf[0].pos[1], sk.joints[j].keys[0].xf[0].pos[2],
                        sk.joints[j].keys.back().xf[0].pos[0], sk.joints[j].keys.back().xf[0].pos[1], sk.joints[j].keys.back().xf[0].pos[2],
                        sk.joints[j].keys[0].xf[0].quat[0], sk.joints[j].keys[0].xf[0].quat[1], sk.joints[j].keys[0].xf[0].quat[2], sk.joints[j].keys[0].xf[0].quat[3]);
        if (std::getenv("K2_ALLKEYS") && !sk.joints.empty() && sk.joints[0].present)
            for (int i = 0; i <= 8; ++i) {
                float p[3], q[4];
                const uint32_t t = uint32_t(double(sk.duration()) * i / 8.0);
                assets::skl_sample(sk.joints[0].path, t, sk.interpolate != 0, p, q);
                std::printf("    root t=%u (%.3f %.3f %.3f)\n", t, p[0], p[1], p[2]);
            }
        // Sampled (SklS) clips: the first joints' tracks at the start.
        for (size_t j = 0; j < sk.joints.size() && j < 4; ++j) {
            if (!sk.joints[j].present) { std::printf("    joint %zu absent\n", j); continue; }
            float p[3], q[4];
            assets::skl_sample(sk.joints[j].path, 0, sk.interpolate != 0, p, q);
            std::printf("    joint %zu pos=(%7.3f,%7.3f,%7.3f) quat=(%7.4f,%7.4f,%7.4f,%7.4f)\n",
                        j, p[0], p[1], p[2], q[0], q[1], q[2], q[3]);
        }
        for (size_t k = 0; k < sk.sound_keys.size(); ++k)
            std::printf("    sound t=%-6u '%s'\n", sk.sound_keys[k].time, sk.sound_keys[k].sound.c_str());
        for (size_t k = 0; k < sk.event_keys.size(); ++k)
            std::printf("    event t=%-6u v=%u '%s'\n", sk.event_keys[k].time,
                        sk.event_keys[k].value, sk.event_keys[k].target.c_str());
        return 0;
    }
    if (actors_only)    return dump_actors();
    if (masters_only)   return dump_masters();
    if (dumplevel_path) return dump_level(dumplevel_path);
    if (images_only) return dump_images();
    if (dump_path)   return dump_k2a(dump_path);

    // Headless: mount and probe the VFS, then exit. No window, no GL.
    if (verify_only) return 0;

    plat::config cfg;
    cfg.title      = "k2boy";
    cfg.width      = 1280;
    cfg.height     = 720;
    cfg.vsync      = true;
    cfg.fullscreen = false;
    cfg.gl_major   = 3;
    cfg.gl_minor   = 3;

    if (!plat::init(cfg)) {
        std::fprintf(stderr, "k2boy: platform init failed\n");
        return 1;
    }
    k2::jobs::init(2);

    if (!gfx::init(plat::gl_get_proc_address)) {
        std::fprintf(stderr, "k2boy: gfx init failed\n");
        plat::shutdown();
        return 1;
    }

    int32_t w = 0, h = 0;
    plat::drawable_size(w, h);
    gfx::resize(w, h);

    // --menu: the Red Orb logo, then the title screen. New Game shows the
    // Introduction cinematic and boots the first level; Load boots the
    // saved level with the Prince where the save put him.
    static std::string fe_place, fe_region;
    ui::frontend frontend;
    const bool fe_ok = use_menu && frontend.init([](const std::string& n) { return play_movie(n); });
    static ui::frontend* loading_fe = 0;
    if (fe_ok) {
        loading_fe = &frontend;
        g_loading_fn = [](float p) { loading_fe->loading(p); };
        // Up as soon as there is something to draw it with: a relaunch (Load,
        // Restart, Main Menu) comes back here, and so does a cold start.
        loading_progress(0.0f);
    }
    if (use_menu && !menu_in_game_only) {
        k2::engine::mixer fe_mixer(44100);
        fe_mixer.set_volume(frontend.prefs().sound_volume);
        bool fe_audio = false;
        if (!mute) {
            g_mixer = &fe_mixer;
            fe_audio = plat::audio_open(44100, audio_callback, 0);
            if (!fe_audio) g_mixer = 0;
        }
        bool alive = play_movie("Broder");
        ui::save_game sv;
        const ui::result r = alive ? frontend.main_menu(sv) : ui::R_CLOSED;
        if (r == ui::R_NEW_GAME) {
            alive = play_movie("cs01");
            boot = true;
        } else if (r == ui::R_LOAD) {
            static std::string load_level_path;
            if (sv.level.empty() || sv.level.find("prisonfix") != std::string::npos) boot = true;
            else { load_level_path = sv.level; level_path = load_level_path.c_str(); }
            region_arg = sv.region;
            char b[96];
            std::snprintf(b, sizeof(b), "%.3f,%.3f,%.3f,%.2f", sv.pos[0], sv.pos[1], sv.pos[2],
                          sv.yaw * 180.0f / 3.14159265f);
            fe_place = b;
            place_arg = fe_place.c_str();
            start_health = sv.health;
            start_max_health = sv.max_health;
            start_armed = sv.armed;
            world_lines = sv.world;
            send_args.push_back("IntroAnim,enable:F");   // the opening scene has been seen
        }
        if (fe_audio) plat::audio_close();
        g_mixer = 0;
        if (!alive || (r != ui::R_NEW_GAME && r != ui::R_LOAD)) {
            gfx::shutdown();
            plat::shutdown();
            return 0;
        }
        classic = frontend.prefs().classic;
    }

    // --boot: do what WinMain does. Read distribution.dat, then resolve the
    // startup module id to its level. No hardcoded path.
    std::string boot_level;
    if (boot) {
        const char* why = 0;
        if (!k2::engine::module_table_verify(&why))
            std::fprintf(stderr, "k2boy: module table invalid: %s\n", why);

        size_t csize = 0;
        uint8_t* cd = plat::fs_read("distribution.dat", csize);
        k2::engine::config cfg;
        if (cd) {
            k2::engine::config_parse(reinterpret_cast<const char*>(cd), csize, cfg);
            plat::fs_free(cd);
        }
        const k2::engine::module_entry* m =
            k2::engine::module_at(k2::engine::MODULE_ID_STARTUP);
        std::printf("boot: %zu config flags, startup module 0x%02X = '%s'\n",
                    cfg.flags.size(), k2::engine::MODULE_ID_STARTUP,
                    m ? m->name : "?");
        if (m && m->level[0]) {
            boot_level = m->level;
            std::printf("boot: level %s (region %d)\n", boot_level.c_str(), region_arg);
        }
    }

    scene sc;
    assets::lvl_file lv;
    bool have_level = false;
    k2::engine::body_table bodies;
    std::vector<k2::engine::actor_spawn> spawns;

    k2::engine::module_manager mgr;

    if (boot) {
        // The original's path: post a module id, the manager transitions, and
        // the module loads its own level. No hardcoded level here.
        game_module* gm = new game_module(k2::engine::MODULE_ID_STARTUP, region_arg,
                                          &sc, &lv, &bodies, &spawns);
        mgr.goto_module(k2::engine::MODULE_ID_STARTUP, gm);
        have_level = gm->loaded();
        std::printf("module: current '%s' previous '%s'\n",
                    k2::engine::module_manager::id_name(mgr.current_id()),
                    k2::engine::module_manager::id_name(mgr.previous_id()));
    } else if (actor_path) {
        load_actor(actor_path, sc);
        if (!sc.empty) {
            std::printf("  bounds x[%.1f, %.1f] y[%.1f, %.1f] z[%.1f, %.1f]\n",
                        sc.mn[0], sc.mx[0], sc.mn[1], sc.mx[1], sc.mn[2], sc.mx[2]);
        }
    } else if (level_path) {
        scene_reset(sc);
        have_level = load_level(level_path, sc, lv, region_arg);
        if (have_level) {
            build_body_table(lv, bodies);
            spawn_prince_from_level(lv, sc, bodies, spawns, region_arg);
            spawn_level_actors(sc, bodies, spawns);
            std::printf("  actors: %zu spawned (prince index %d)\n",
                        spawns.size(), k2::engine::find_prince(spawns));
        }
        if (have_level && !sc.empty) {
            std::printf("  level bounds x[%.1f, %.1f] y[%.1f, %.1f] z[%.1f, %.1f]\n",
                        sc.mn[0], sc.mx[0], sc.mn[1], sc.mx[1], sc.mn[2], sc.mx[2]);
        }
    } else {
        // (room path) — see below for where actors are spawned
        const char* scene_path = room_path ? room_path
                                           : "geometry\\rooms\\prisonfix\\prison1.k2a";
        load_scene(scene_path, sc);
        // A lone room carries spawn points but no [Bodies] list, so resolve
        // against every master.dat in the install.
        if (!sc.spawn_points.empty()) {
            build_body_table_from_install(bodies);
            spawn_level_actors(sc, bodies, spawns);
            std::printf("  actors: %zu spawned (prince index %d)\n",
                        spawns.size(), k2::engine::find_prince(spawns));
        }
    }

    // [SkyColor] and [Fog] are 0..255 in the file.
    if (have_level && lv.has_sky_color) {
        gfx::set_clear_color(lv.sky_color[0] / 255.0f,
                             lv.sky_color[1] / 255.0f,
                             lv.sky_color[2] / 255.0f);
    }

    // Covers every load path (room, level, actor); load_level appends many rooms
    // before the full type set is known, so colours are assigned once, here.
    assign_trap_colors(sc);
    // --pose Joint:degrees rotates one joint about X and re-poses the actor, to
    // show that a joint actually drives the geometry bound to it.
    if (pose_arg) {
        std::string spec(pose_arg);
        const size_t colon = spec.find(':');
        const std::string jname = spec.substr(0, colon);
        const float deg = colon == std::string::npos ? 30.0f
                        : float(std::atof(spec.c_str() + colon + 1));
        const float r = deg * 3.14159265f / 180.0f;
        for (size_t ai = 0; ai < sc.actors.size(); ++ai) {
            actor_inst& A = sc.actors[ai];
            if (!A.skinned) continue;
            for (size_t j = 0; j < A.joint_names.size(); ++j) {
                if (A.joint_names[j] != jname) continue;
                k2::engine::transform rot;
                const float c = std::cos(r), sn = std::sin(r);
                rot.rotate[0]=1; rot.rotate[1]=0;  rot.rotate[2]=0;
                rot.rotate[3]=0; rot.rotate[4]=c;  rot.rotate[5]=-sn;
                rot.rotate[6]=0; rot.rotate[7]=sn; rot.rotate[8]=c;
                k2::engine::transform out;
                k2::engine::transform_compose(A.joint_local_bind[j], rot, out);
                A.joint_local_cur[j] = out;
                std::printf("pose: %s '%s' by %.1f deg\n", A.name.c_str(), jname.c_str(), deg);
                break;
            }
            pose_actor(A);
        }
    }

    print_trap_legend(sc);

    camera cam;
    frame_scene(cam, sc);
    if (cam_arg) {
        float v[5] = {0,0,0,0,0};
        if (std::sscanf(cam_arg, "%f,%f,%f,%f,%f", &v[0],&v[1],&v[2],&v[3],&v[4]) >= 3) {
            cam.pos[0]=v[0]; cam.pos[1]=v[1]; cam.pos[2]=v[2];
            cam.yaw=v[3]; cam.pitch=v[4];
        }
    }
    std::printf("camera start (%.2f, %.2f, %.2f) yaw %.2f pitch %.2f speed %.1f\n",
                cam.pos[0], cam.pos[1], cam.pos[2], cam.yaw, cam.pitch, cam.speed);
    std::fflush(stdout);

    bool  down[plat::KEY_COUNT];
    std::memset(down, 0, sizeof(down));
    bool  show_traps = !trig_off;
    // Follow the Prince when there is one; the viewer's free-fly is the
    // fallback and stays available on F.
    bool  follow_mode  = (k2::engine::find_prince(spawns) >= 0);
    float player_yaw   = 0.0f;
    float player_vy    = 0.0f;          // vertical speed while falling
    const float walk_speed   = 3.2f;    // metres/second, eyeballed
    const float follow_dist  = 4.5f;
    const float follow_height = 2.2f;
    if (follow_mode) {
        const int32_t pi = k2::engine::find_prince(spawns);
        for (size_t i = 0; i < sc.actors.size(); ++i)
            if (is_prince(sc.actors[i])) {
                player_yaw = std::atan2(sc.actors[i].node->local.rotate[2],
                                        sc.actors[i].node->local.rotate[8]);
                break;
            }
        (void)pi;
        cam.pitch = -0.22f;
        cam.yaw = 3.14159265f - player_yaw;     // behind him, looking the way he faces
    }
    // --place x,y,z,yaw puts the Prince somewhere for a test; yaw in degrees,
    // 0 facing +z.
    if (place_arg) {
        float v[4] = {0,0,0,0};
        if (std::sscanf(place_arg, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) >= 3)
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) {
                    for (int k = 0; k < 3; ++k) sc.actors[i].node->local.translate[k] = v[k];
                    player_yaw = v[3] * 3.14159265f / 180.0f;
                    cam.yaw = 3.14159265f - player_yaw;   // look the way he faces
                    sc.root->update_world();
                    break;
                }
    }
    {
        std::printf("collision: %zu triangles\n", sc.coll.triangle_count());
        for (size_t i = 0; i < sc.actors.size(); ++i) {
            if (!is_prince(sc.actors[i])) continue;
            float gy;
            const float* p = sc.actors[i].node->local.translate;
            if (ground_under(sc.coll, p, gy))
                std::printf("  ground under the Prince at y %.3f (spawn y %.3f)\n", gy, p[1]);
            else
                std::printf("  no ground under the Prince within reach (spawn y %.3f)\n", p[1]);
        }
    }
    // --findledges: list standing spots with a ledge ahead, for testing.
    // K2_FLOORPROBE=x,y,z0,z1: the floor under a line along z (front faces,
    // from y + 1.5 down), every 0.1 - for checking crate and ledge rules.
    if (const char* fp = std::getenv("K2_FLOORPROBE")) {
        float x, y, z0, z1;
        if (std::sscanf(fp, "%f,%f,%f,%f", &x, &y, &z0, &z1) == 4)
            for (float z = z0; z <= z1 + 1e-4f; z += 0.1f) {
                const float o[3] = { x, y + 1.5f, z }, d[3] = { 0.0f, -1.0f, 0.0f };
                k2::engine::pick_hit h;
                if (sc.coll.pick(o, d, 1.0e6f, true, h)) std::printf("floor z %.2f: %.3f\n", z, h.point[1]);
                else std::printf("floor z %.2f: none\n", z);
            }
    }
    // K2_WATERPROBE: each water area's triangles with the floor depth under
    // their centres, to find where the outline covers dry ground.
    if (std::getenv("K2_WATERPROBE")) {
        for (size_t w = 0; w < sc.waters.size(); ++w) {
            const scene::water_area& wa = sc.waters[w];
            const scene_trap* t = dynamic_cast<const scene_trap*>(sc.traps[wa.trap]);
            if (!t) continue;
            const float* o = t->offset();
            const float surf = t->box[4] + o[1];
            std::printf("water %s surface %.2f\n", t->name().c_str(), surf);
            for (size_t i = 0; i + 5 < wa.tris.size(); i += 6) {
                const float cx = (wa.tris[i] + wa.tris[i + 2] + wa.tris[i + 4]) / 3.0f + o[0];
                const float cz = (wa.tris[i + 1] + wa.tris[i + 3] + wa.tris[i + 5]) / 3.0f + o[2];
                const float p[3] = { cx, surf + 0.05f, cz };
                const float dn[3] = { 0.0f, -1.0f, 0.0f };
                k2::engine::pick_hit h;
                const bool hit = sc.coll.pick(p, dn, 20.0f, true, h);
                std::printf("  tri %3zu at (%.1f, %.1f) depth %s%.2f\n", i / 6, cx, cz, hit ? "" : ">",
                            hit ? surf - h.point[1] : 20.0f);
            }
        }
    }
    if (find_ledges) {
        int found = 0;
        for (float x = sc.mn[0]; x < sc.mx[0] && found < 40; x += 1.0f)
            for (float z = sc.mn[2]; z < sc.mx[2] && found < 40; z += 1.0f) {
                const float o[3] = { x, sc.mx[1], z };
                const float dn[3] = { 0.0f, -1.0f, 0.0f };
                k2::engine::pick_hit h;
                if (!sc.coll.pick(o, dn, sc.mx[1] - sc.mn[1] + 1.0f, true, h)) continue;
                for (int d = 0; d < 4; ++d) {
                    const float yaw = d * 1.5707963f;
                    ledge_hit l;
                    if (find_ledge(sc.coll, h.point, yaw, 0.6f, 4.0f, l) && l.can_stand) {
                        std::printf("ledge: stand (%.2f, %.2f, %.2f) yaw %d deg -> top %.2f (+%.2f)\n",
                                    h.point[0], h.point[1], h.point[2], d * 90, l.top, l.top - h.point[1]);
                        ++found;
                        break;
                    }
                }
            }
        return 0;
    }

    // Scripts run on game time, which starts at zero with the level.
    // Sound: one mixer, the camera as listener. Headless shots stay silent.
    k2::engine::mixer game_mixer(44100);
    if (fe_ok) game_mixer.set_volume(frontend.prefs().sound_volume);
    audio_target audio_msgs;
    sc.script.add(&audio_msgs);
    if (!shot_path && !mute) {
        g_mixer = &game_mixer;
        if (!plat::audio_open(44100, audio_callback, 0)) {
            std::printf("audio: no output device\n");
            g_mixer = 0;
        }
    }
    for (size_t i = 0; i < movies_to_play.size(); ++i)
        if (!play_movie(movies_to_play[i])) return 0;
    for (size_t i = 0; i < sc.traps.size(); ++i) {
        audio_trap* at = dynamic_cast<audio_trap*>(sc.traps[i]);
        if (at) at->start_ambient();
    }

    // Every actor answers to its spawn node's name; the Prince to "prince".
    std::vector<actor_target*> actor_msgs;
    actor_target* prince_ptr = 0;
    for (size_t i = 0; i < sc.actors.size(); ++i) {
        if (sc.actors[i].script_name.empty()) continue;
        actor_target* t = new actor_target(sc.script, sc, i);
        actor_msgs.push_back(t);
        sc.script.add(t);
        if (is_prince(sc.actors[i])) prince_ptr = t;
    }
    actor_target* const prince_msgs = prince_ptr;  // 0 when no Prince was spawned

    // Combat tuning from the install, then the guards.
    k2::engine::fight_params fight_tuning;
    {
        size_t sz = 0;
        uint8_t* d = plat::fs_read_basename("fight.ini", sz);
        if (d) { fight_tuning.parse_fight_ini(reinterpret_cast<const char*>(d), sz); plat::fs_free(d); }
        d = plat::fs_read_basename("enemy.dat", sz);
        if (d) { fight_tuning.parse_enemy_dat(reinterpret_cast<const char*>(d), sz); plat::fs_free(d); }
    }
    combat_state combat;
    combat_init(combat, sc, fight_tuning);
    if (start_armed)
        for (size_t i = 0; i < sc.actors.size(); ++i)
            if (is_prince(sc.actors[i])) sc.actors[i].armed = true;
    if (start_health > 0)
        for (size_t i = 0; i < sc.actors.size(); ++i)
            if (is_prince(sc.actors[i])) {
                sc.actors[i].max_health = start_max_health > 0 ? start_max_health : start_health;
                sc.actors[i].health = start_health;
            }
    std::vector<bool> pickup_taken(sc.pickups.size(), false);
    pickup_seq taking;
    std::printf("combat: %zu fight.ini values, %zu enemy.dat values, %zu guards, %zu pickups\n",
                fight_tuning.fight_count(), fight_tuning.enemy_count(), combat.enemies.size(),
                sc.pickups.size());
    camera_target camera_msgs(sc);
    sc.script.add(&camera_msgs);
    // "game": "exit" ends the level (prison6's exit door: "delay,2,game,exit").
    struct game_target : public k2::engine::script_target {
        bool exit_requested;
        game_target() : script_target("game"), exit_requested(false) {}
        bool receive(const std::string& msg, k2::engine::script_target*) {
            if (equals_nocase(msg, "exit")) { exit_requested = true; return true; }
            return false;
        }
    };
    static game_target game_msgs;
    // "enemy,<name>:<msg>": a message to a guard by name (Ivory Tower's
    // cutscenes: "enemy,TowerGuardB:SkillAt:...,DEAD").
    struct enemy_target : public k2::engine::script_target {
        k2::engine::script_world& w;
        explicit enemy_target(k2::engine::script_world& sw) : script_target("enemy"), w(sw) {}
        bool receive(const std::string& msg, k2::engine::script_target* from) {
            const size_t c = msg.find(':');
            if (c == std::string::npos) return false;
            w.send(msg.substr(0, c), msg.substr(c + 1), from);
            return true;
        }
    };
    enemy_target enemy_msgs(sc.script);
    sc.script.add(&enemy_msgs);
    game_msgs.exit_requested = false;
    sc.script.add(&game_msgs);
    // While a script holds the Prince (snooze) the player has no control at
    // all, camera included: that is a cutscene.
    bool input_locked = false;
    sc.script.trace = trapmsg;
    // --send "target,msg" delivers a message at start, as if a trap sent it.
    for (size_t i = 0; i < send_args.size(); ++i) sc.script.send_item(send_args[i], 0);
    if (!world_lines.empty()) world_apply(sc, combat, pickup_taken, world_lines);
    for (size_t i = 0; i < g_audit_scripts.size(); ++i) {
        // "{state:items}" blocks run regardless of state here.
        std::string sct = g_audit_scripts[i];
        for (size_t k = 0; k < sct.size(); ++k) {
            if (sct[k] == '}') sct[k] = ';';
            else if (sct[k] == '{') {
                const size_t c = sct.find(':', k);
                if (c != std::string::npos) sct.erase(k, c - k + 1); else sct[k] = ' ';
                --k;
            }
        }
        sc.script.execute(sct, 0);
    }
    double game_time = 0.0;
    crate_ctl crates;
    jump_ctl  jumps;
    hang_ctl  ledges;
    swim_ctl  swim;
    crouch_ctl crouch;
    // The Action trap the Prince is working, and its next stage.
    struct { k2::engine::action_trap* trap; int stage; } working = { 0, 0 };
    int       prince_last_dir = -1;
    double    prince_last_attack = -10.0;
    double    drown_tick = 0.0;
    bool      draw_was_down = false, atk_was_down = false, bow_was_down = false;
    // The bow (Ivory Tower on): B / pad L3 readies it or puts it away; held
    // attack draws an arrow, aims, and letting go looses it. He aims where
    // the camera looks, at the nearest guard or ArrowTarget in a narrow cone
    // if there is one. The controls and the aim help are ours.
    struct bow_ctl { bool ready = false; int stage = 0; bool released = false; } bow;
    // Locomotion transitions (docs/reference/locomotion.md): the run's first
    // step (StartRun*Right_L), its last (StepEndRun*_L), and turning round
    // standing (QuickTurnaround_G) or running (RunningQuickTurn_L). The
    // original chains a step clip per footfall; here the loops carry the
    // middle and only these ends are its clips. A turn clip ends facing
    // back, so his facing flips when it is done.
    struct loco_ctl { double run_since = -1.0; bool flip = false; } loco;
    // Riding a Mover (lifts, swinging platforms, the gears' teeth): where he
    // stands, held in the platform's own frame, so its motion carries him.
    // Ours; the original's rider handling is not traced.
    // Poles: vertical ropes and chains he climbs (docs/reference/poles-ropes-bars.md).
    // The hands hold the pole's axis at `hand_y`; the body hangs behind,
    // turned by `yaw`; the clips play about that hold, as the original's
    // K2AttachActivity plays them about its attach transform.
    struct pole_ctl {
        enum state_t { NONE, MOUNT, HANG, CLIMB, SLIDE_START, SLIDE, SLIDE_END, PUSHOFF } state = NONE;
        scene_trap* pole = 0;
        float yaw = 0.0f, hand_y = 0.0f;
        int   hand = 1;                 // which hand is up: +1 right (fight+0xF0's sign)
        float grip[3] = { 0.0f, 2.25f, 0.0f };
        bool  have_grip = false;
        bool  let_go = false;           // a release pressed during a clip, kept for the hang
    } pole;
    // Bars: horizontal bars and ropes he hangs from, shimmies along and
    // swings on (StStartHorizHang .. StStartHorizLaunch). `hold` is where the
    // hands are on the bar's axis.
    struct bar_ctl {
        enum state_t { NONE, CATCH, HANG, SH_START, SH_MOVE, SH_END, SW_START, SWING, SW_STOP } state = NONE;
        scene_trap* bar = 0;
        float hold[3] = { 0.0f, 0.0f, 0.0f };
        float yaw = 0.0f;
        int   dir = 0;                  // shimmy: +1 his left, -1 his right
        float grip[3] = { 0.0f, 2.25f, 0.0f };
        bool  have_grip = false;
        double swing_t0 = 0.0, swing_len = 1.749;
    } bar;
    // On a swinging rope (StStartSwingRope .. StStartSwingRopeRelease): he
    // hangs on the rope actor's Attach joint; letting go throws him with
    // that joint's velocity.
    struct rope_ctl {
        int rope = -1;                  // index into sc.ropes
        bool looping = false;           // past SwingStart_M, on SwingBase_M
        float prev[3] = { 0.0f, 0.0f, 0.0f };
        double prev_t = -1.0;
        float vel[3] = { 0.0f, 0.0f, 0.0f };
    } rope;
    struct ride_ctl { int32_t node = -1; float local[3] = { 0.0f, 0.0f, 0.0f }; float at[3] = { 0.0f, 0.0f, 0.0f }; } ride;
    bool      jump_was_down = false;
    bool      act_was_down = false;
    double    regrab_after = 0.0;      // let go: no catching the same ledge at once
    bool      pad_act_down = false;   // pad X last frame; areas run before the pad is read
    std::printf("scripts: %zu traps, %zu areas, %zu plates%s\n", sc.traps.size(),
                sc.areas.size(), sc.plates.size(),
                noscript ? " (disabled by --noscript)" : "");

    bool   looking   = false;
    double last_time = plat::time_seconds();

    std::printf("\ncamera: WASD move, Q/E or Space down/up, drag LMB to look,\n"
                "        scroll to change speed, Shift fast, Ctrl slow, R reframe, Esc quit\n\n");

    float view[16], proj[16];
    mat4_identity(view);
    // Depth precision is governed by the near/far *ratio*, and the near plane
    // dominates it. Hardcoding 0.1 .. 1000 gave a ratio of 10000, which left so
    // little precision at distance that far polygons won z-fighting against
    // near ones - rooms drew through each other.
    //
    // Levels ship their own [ClipPlanes] and the original uses them: prisonfix
    // is .1, 50 (ratio 500), cistern .5, 85 (ratio 170). Honour them. The far
    // plane is deliberately not widened to fit the scene: the original relies on
    // region streaming plus fog, and fog saturates at 1.5x far anyway, so
    // anything beyond it is solid fog colour regardless.
    float znear = 0.1f, zfar = 1000.0f;
    if (have_level && lv.has_clip_planes && lv.clip_far > lv.clip_near && lv.clip_near > 0.0f) {
        znear = lv.clip_near;
        zfar  = lv.clip_far;

        // Deviation from the original, on purpose. Its far plane is tuned for
        // portal-culled region streaming; this loads a whole region at once, so
        // taking prisonfix's far of 50 literally sliced rooms off mid-wall.
        // Extend far to cover the loaded bounds. Precision is governed by the
        // near/far ratio and dominated by *near*, so keeping the level's near
        // retains most of the gain: prisonfix goes 10000 -> ~2000 rather than
        // -> 500, still several times better than the old hardcoded pair.
        if (!sc.empty) {
            const float dx = sc.mx[0] - sc.mn[0];
            const float dy = sc.mx[1] - sc.mn[1];
            const float dz = sc.mx[2] - sc.mn[2];
            const float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (diag * 1.1f > zfar) zfar = diag * 1.1f;
        }
        std::printf("clip planes: near %.2f far %.2f (level far %.2f, ratio %.0f)\n",
                    znear, zfar, lv.clip_far, zfar / znear);
    }
    mat4_perspective(proj, 1.0472f /* 60 deg */, float(w) / float(h ? h : 1), znear, zfar);

    gfx::set_clear_color(0.05f, 0.06f, 0.09f);

    bool running = true;
    bool open_menu = false;
    bool motion_camera = false;
    while (running) {
        plat::event e;
        while (plat::poll_event(e)) {
            switch (e.type) {
                case plat::EVENT_QUIT:
                    running = false;
                    break;
                case plat::EVENT_KEY_DOWN:
                    if (e.k == plat::KEY_ESCAPE) {
                        if (!use_menu || !fe_ok || shot_path) running = false;
                        else open_menu = true;
                    }
                    if (e.k == plat::KEY_R) frame_scene(cam, sc);
                    if (e.k == plat::KEY_V && !input_locked) {
                        follow_mode = !follow_mode;
                        std::printf("camera: %s\n", follow_mode ? "follow" : "free-fly");
                        std::fflush(stdout);
                    }
                    if (e.k == plat::KEY_TAB) {
                        classic = !classic;
                        std::printf("controls: %s\n", classic ? "classic" : "modern");
                        std::fflush(stdout);
                    }
                    if (e.k == plat::KEY_T) {
                        show_traps = !show_traps;
                        std::printf("triggers %s\n", show_traps ? "on" : "off");
                        std::fflush(stdout);
                    }
                    if (e.k < plat::KEY_COUNT) down[e.k] = true;
                    break;
                case plat::EVENT_KEY_UP:
                    if (e.k < plat::KEY_COUNT) down[e.k] = false;
                    break;
                case plat::EVENT_MOUSE_DOWN:
                    // Left drag on a desktop; right is the Vita's rear touchpad.
                    if (e.button == plat::MOUSE_LEFT || e.button == plat::MOUSE_RIGHT) looking = true;
                    break;
                case plat::EVENT_MOUSE_UP:
                    if (e.button == plat::MOUSE_LEFT || e.button == plat::MOUSE_RIGHT) looking = false;
                    break;
                case plat::EVENT_PAD_BUTTON:
                    if (!e.pad_pressed) break;
                    // Start is Esc: the game menu.
                    if (e.pad_btn == plat::PAD_START) {
                        if (!use_menu || !fe_ok || shot_path) running = false;
                        else open_menu = true;
                    }
                    // Select turns aiming the camera by tilting the pad on or off.
                    if (e.pad_btn == plat::PAD_SELECT) {
                        motion_camera = !motion_camera;
                        std::printf("motion camera %s\n", motion_camera ? "on" : "off");
                        std::fflush(stdout);
                    }
                    break;
                case plat::EVENT_MOUSE_MOVE:
                    if (looking && !input_locked) {
                        const float sens = 0.005f;
                        cam.yaw   += float(e.mouse_dx) * sens;
                        cam.pitch -= float(e.mouse_dy) * sens;   // y grows downward
                        const float lim = 1.55f;   // just under 90 degrees
                        if (cam.pitch >  lim) cam.pitch =  lim;
                        if (cam.pitch < -lim) cam.pitch = -lim;
                    }
                    break;
                case plat::EVENT_MOUSE_WHEEL:
                    // Scroll changes travel speed rather than FOV: with scenes
                    // ranging from a 0.1-unit gem to a 200-unit level, a fixed
                    // speed is unusable and zooming the lens does not help.
                    cam.speed *= std::pow(1.15f, e.wheel);
                    if (cam.speed < 0.01f)   cam.speed = 0.01f;
                    if (cam.speed > 10000.f) cam.speed = 10000.f;
                    break;
                case plat::EVENT_RESIZE:
                    gfx::resize(e.width, e.height);
                    mat4_perspective(proj, 1.0472f,
                                     float(e.width) / float(e.height ? e.height : 1),
                                     znear, zfar);
                    break;
                default:
                    break;
            }
        }

        // The level is over: on to the next, in the game's order (the
        // order GameText lists the levels' names in - the module that
        // follows a level is not traced). A relaunch, as Load is.
        if (game_msgs.exit_requested) {
            game_msgs.exit_requested = false;
            static const char* order[] = {
                "geometry\\rooms\\prisonfix\\prisonfix.lvl", "geometry\\rooms\\ivorytwr\\ivorytwr.lvl",
                "geometry\\rooms\\Cistern\\cistern.lvl", "geometry\\rooms\\palace2\\palace2.lvl",
                "geometry\\rooms\\palace3\\palace3.lvl", "geometry\\rooms\\palace4\\palace4.lvl",
                "geometry\\rooms\\roof1\\roof1.lvl", "geometry\\rooms\\cityanddocks\\cityanddocks.lvl",
                "geometry\\rooms\\dirig1a\\dirig1a.lvl", "geometry\\rooms\\dirig1b\\dirig1b.lvl",
                "geometry\\rooms\\dirig2\\dirig2.lvl", "geometry\\rooms\\DirigFinale\\DirigFinale.lvl",
                "geometry\\rooms\\ruins\\ruins.lvl", "geometry\\rooms\\cliffs\\cliffs.lvl",
                "geometry\\rooms\\solar1\\solar1.lvl", "geometry\\rooms\\moontemple\\moontemple.lvl",
                "geometry\\rooms\\finale\\finale.lvl" };
            const size_t n = sizeof(order) / sizeof(order[0]);
            std::string cur = boot_level.empty() ? (level_path ? level_path : "") : boot_level;
            for (size_t k = 0; k < cur.size(); ++k) cur[k] = char(std::tolower(static_cast<unsigned char>(cur[k])));
            size_t next = n;
            for (size_t k = 0; k + 1 < n && next == n; ++k) {
                std::string o = order[k];
                for (size_t q = 0; q < o.size(); ++q) o[q] = char(std::tolower(static_cast<unsigned char>(o[q])));
                if (cur == o) next = k + 1;
            }
            if (next < n) {
                std::printf("level: exit to %s\n", order[next]);
                std::vector<std::string> extra;
                extra.push_back("--level");
                extra.push_back(order[next]);
                if (use_menu) extra.push_back("--menu-game");
                relaunch(argc, argv, extra);
            } else {
                std::printf("level: exit with no level after %s\n", cur.c_str());
            }
        }

        // K2_GAME_MENU=1 opens the in-game menu on the tenth frame (headless checks).
        {
            static int menu_frames = 0;
            if (++menu_frames == 10 && std::getenv("K2_GAME_MENU") && use_menu && fe_ok) open_menu = true;
        }
        if (open_menu) {
            open_menu = false;
            do {
                    // The in-game menu (Game Menu art). Resume carries on;
                    // the rest start a fresh run of the binary.
                    ui::save_game cur;
                    cur.level = !boot_level.empty() ? boot_level : level_path ? std::string(level_path) : std::string("prisonfix");
                    cur.region = region_arg;
                    for (size_t i = 0; i < sc.actors.size(); ++i)
                        if (is_prince(sc.actors[i])) {
                            for (int k = 0; k < 3; ++k) cur.pos[k] = sc.actors[i].node->local.translate[k];
                            cur.health = sc.actors[i].health;
                            cur.max_health = sc.actors[i].max_health;
                            cur.armed = sc.actors[i].armed;
                        }
                    cur.world = world_capture(sc, combat, pickup_taken);
                    cur.yaw = player_yaw;
                    {
                        // The level's name in GameText, for the save list.
                        static const char* names[][2] = {
                            { "prisonfix", "Prison" }, { "ivorytwr", "Ivory_Tower" }, { "cistern", "Cistern" },
                            { "palace2", "Palace_2" }, { "palace3", "Palace_3" }, { "palace4", "Palace_4" },
                            { "roof1", "Rooftops" }, { "cityanddocks", "StreetsDocks" },
                            { "dirig1a", "Lower_Dirigible" }, { "dirig1b", "Lower_Dirigible_2" },
                            { "dirig2", "Upper_Dirigible" }, { "dirigfinale", "Dirigible_Finale" },
                            { "ruins", "Floating_Ruins" }, { "cliffs", "Cliffs" }, { "solar1", "Sun_Temple" },
                            { "moontemple", "Moon_Temple" }, { "finale", "Finale" } };
                        std::string lv = cur.level;
                        for (size_t k = 0; k < lv.size(); ++k) lv[k] = char(std::tolower(static_cast<unsigned char>(lv[k])));
                        for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); ++k)
                            if (lv.find(std::string(names[k][0]) + ".lvl") != std::string::npos)
                                cur.name = frontend.text().get(names[k][1]);
                        if (cur.name.empty()) cur.name = lv;
                    }
                    ui::save_game ld;
                    const ui::result r = frontend.game_menu(cur, ld);
                    for (int k = 0; k < plat::KEY_COUNT; ++k) down[k] = false;
                    classic = frontend.prefs().classic;
                    game_mixer.set_volume(frontend.prefs().sound_volume);
                    if (r == ui::R_RESUME || r == ui::R_NONE) break;
                    if (r == ui::R_QUIT || r == ui::R_CLOSED) { running = false; break; }
                    std::vector<std::string> extra;
                    if (r == ui::R_MAIN_MENU) extra.push_back("--menu");
                    if (r == ui::R_LOAD) {
                        char b[96];
                        std::snprintf(b, sizeof(b), "%.3f,%.3f,%.3f,%.2f", ld.pos[0], ld.pos[1], ld.pos[2],
                                      ld.yaw * 180.0f / 3.14159265f);
                        // The prison boots as the original does; later levels load by path.
                        if (ld.level.empty() || ld.level.find("prisonfix") != std::string::npos) extra.push_back("--boot");
                        else { extra.push_back("--level"); extra.push_back(ld.level); }
                        extra.push_back("--menu-game");
                        extra.push_back("--region"); extra.push_back(std::to_string(ld.region));
                        extra.push_back("--place"); extra.push_back(b);
                        std::snprintf(b, sizeof(b), "%d,%d", ld.health, ld.max_health);
                        extra.push_back("--health"); extra.push_back(b);
                        if (ld.armed) extra.push_back("--armed");
                        extra.push_back("--send"); extra.push_back("IntroAnim,enable:F");
                        if (!ld.world.empty()) {
                            const std::string wp = plat::user_dir() + "/world-load.txt";
                            if (FILE* wf = std::fopen(wp.c_str(), "w")) {
                                for (size_t k = 0; k < ld.world.size(); ++k) std::fprintf(wf, "%s\n", ld.world[k].c_str());
                                std::fclose(wf);
                                extra.push_back("--world"); extra.push_back(wp);
                            }
                        }
                    }
                    if (r == ui::R_RESTART) {
                        if (level_path && boot_level.empty()) { extra.push_back("--level"); extra.push_back(level_path); }
                        else extra.push_back("--boot");
                        extra.push_back("--menu-game");
                    }
                    relaunch(argc, argv, extra);
                    running = false;
            } while (false);
            if (!running) break;
        }

        // --- free-fly camera integration
        const double now = plat::time_seconds();
        float dt = float(now - last_time);
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;          // don't lurch after a stall
        if (fixed_dt > 0.0f) dt = fixed_dt; // headless runs: game time per frame

        float fwd[3], right[3];
        camera_basis(cam, fwd, right);

        // Trap scripts: delays come due, Areas test the Prince, movers move.
        // Only the Prince is tested against Areas; the original tests every
        // character, but enemies do not move yet.
        game_time += dt;
        if (!noscript) {
            sc.script.update(game_time);
            fx_sync_traps(game_time);
            fx_update(sc, game_time, dt);
            const float* pp = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) { pp = sc.actors[i].node->local.translate; break; }
            const bool action = follow_mode && (down[plat::KEY_E] || pad_act_down);
            for (size_t i = 0; i < sc.areas.size(); ++i)
                sc.areas[i]->update(game_time, pp && sc.areas[i]->contains(pp), action);
            for (size_t i = 0; i < sc.plates.size(); ++i)
                sc.plates[i]->update(game_time, pp && sc.plates[i]->contains(pp));
            // Scripted skills ("SkillAt:") drive their actors outright.
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (update_scripted(sc.actors[i], game_time) && is_prince(sc.actors[i]))
                    player_yaw = sc.actors[i].scripted_yaw;
            // Fixtures between scripted skills: the next idle, picked at
            // random from the trap's list, from where they stand.
            for (size_t i = 0; i < sc.actors.size(); ++i) {
                actor_inst& fa = sc.actors[i];
                if (!fa.is_fixture || fa.scripted || fa.posed_hold || fa.hidden) continue;
                while (!fa.ambient.empty()) {
                    const size_t pick = size_t(std::rand()) % fa.ambient.size();
                    for (int k = 0; k < 3; ++k) fa.node->local.translate[k] = fa.home[k];
                    if (play_once(sc, fa, fa.ambient[pick], game_time, fa.home_yaw, false)) break;
                    // Not a sampled skill (the rats' are SklK keyframes): drop it.
                    fa.ambient.erase(fa.ambient.begin() + long(pick));
                    if (fa.ambient.empty()) std::printf("fixture %s: no playable idles\n", fa.name.c_str());
                }
            }

            // Combat: guards, swings landing, and a fallen Prince coming back.
            actor_inst* prince_a = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) { prince_a = &sc.actors[i]; break; }
            if (prince_a) {
                const bool snoozing = game_time < (prince_msgs ? prince_msgs->snooze_until : 0.0);
                const bool available = !snoozing && combat.dead_since < 0.0;
                // Scripted walks ("WalkAt:") come before the guards' own minds.
                for (size_t i = 0; i < sc.actors.size(); ++i) {
                    actor_inst& wa = sc.actors[i];
                    if (!wa.walking || wa.scripted) continue;
                    const float* q = wa.node->local.translate;
                    const float wx = wa.walk_to[0] - q[0], wz = wa.walk_to[2] - q[2];
                    if (wx * wx + wz * wz > 0.04f) { steer(sc, wa, wa.walk_to, wa.walk_run, dt); continue; }
                    wa.walking = false;
                    set_clip(wa, LOCO_BASE);
                    if (wa.walk_remove) wa.hidden = true;
                }
                for (size_t i = 0; i < combat.enemies.size(); ++i)
                    if (!sc.actors[size_t(combat.enemies[i].actor)].walking)
                        enemy_update(combat, sc, combat.enemies[i], *prince_a, available, game_time, dt);
                prince_hit_resolve(combat, sc, *prince_a, game_time);
                // Bodies do not overlap: a swing's root motion stops at
                // arm's length from a guard (ours; the original's character
                // collision is not traced).
                for (size_t i = 0; i < combat.enemies.size(); ++i) {
                    const enemy_ai& en = combat.enemies[i];
                    const actor_inst& ea = sc.actors[size_t(en.actor)];
                    if (en.state == enemy_ai::DEAD || ea.hidden) continue;
                    float* pp2 = prince_a->node->local.translate;
                    const float* q = ea.node->local.translate;
                    const float ox = pp2[0] - q[0], oz = pp2[2] - q[2];
                    const float od = std::sqrt(ox * ox + oz * oz);
                    if (od >= 1.1f || std::fabs(pp2[1] - q[1]) > 1.5f) continue;
                    const float nx = od > 1e-4f ? ox / od : 0.0f, nz = od > 1e-4f ? oz / od : -1.0f;
                    move_horizontal(sc.coll, pp2, nx * (1.1f - od), nz * (1.1f - od));
                    if (prince_a->scripted) for (int k = 0; k < 3; k += 2) prince_a->scripted_origin[k] += (k ? nz : nx) * (1.1f - od);
                }
                // Falling out of the world is a death too.
                if (combat.dead_since < 0.0 && prince_a->node->local.translate[1] < sc.mn[1] - 15.0f) {
                    prince_a->health = 0;
                    combat.dead_since = game_time;
                    std::printf("combat: the Prince fell out of the world\n");
                }
                if (combat.dead_since >= 0.0 &&
                    game_time - combat.dead_since > combat_tuning::RESPAWN_TIME) {
                    // Back to the level's start, whole; guards return home.
                    k2::engine::transform& plt = prince_a->node->local;
                    for (int k = 0; k < 3; ++k) plt.translate[k] = combat.spawn[k];
                    player_yaw = combat.spawn_yaw;
                    prince_a->health = prince_a->max_health;
                    prince_a->scripted = 0;
                    prince_a->posed_hold = false;
                    prince_a->stance = 0;
                    set_clip(*prince_a, LOCO_BASE);
                    combat.dead_since = -1.0;
                    combat.prince_drawn = false;
                    combat.prince_busy_until = 0.0;
                    jumps.state = jump_ctl::NONE;
                    ledges.state = hang_ctl::NONE;
                    crates.state = crate_ctl::NONE;
                    for (size_t i = 0; i < combat.enemies.size(); ++i) {
                        enemy_ai& e = combat.enemies[i];
                        if (e.state == enemy_ai::DEAD) continue;
                        e.state = enemy_ai::PATROL;
                        e.hit.active = false;
                        actor_inst& ea = sc.actors[size_t(e.actor)];
                        for (int k = 0; k < 3; ++k) ea.node->local.translate[k] = e.home[k];
                        ea.stance = 0;
                    }
                    std::printf("combat: the Prince returns\n");
                }
            }
            for (size_t i = 0; i < sc.traps.size(); ++i) {
                scene_trap* st = dynamic_cast<scene_trap*>(sc.traps[i]);
                if (st) { st->collapse_update(game_time, dt, pp); st->update(game_time); }
            }
            // CrateTargets: a crate's centre inside the box sends the message.
            for (size_t i = 0; i < sc.traps.size(); ++i) {
                scene_trap* tg = dynamic_cast<scene_trap*>(sc.traps[i]);
                if (!tg || tg->type != "CrateTarget" || !tg->enabled() || tg->message.empty()) continue;
                for (size_t j = 0; j < sc.traps.size(); ++j) {
                    scene_trap* cr = dynamic_cast<scene_trap*>(sc.traps[j]);
                    if (!cr || cr->type != "Crate" || !cr->enabled()) continue;
                    const float* o = cr->offset();
                    const float c[3] = { (cr->box[0] + cr->box[3]) * 0.5f + o[0], cr->box[1] + o[1],
                                         (cr->box[2] + cr->box[5]) * 0.5f + o[2] };
                    const float* b = tg->box;
                    if (c[0] < b[0] || c[0] > b[3] || c[2] < b[2] || c[2] > b[5] || std::fabs(c[1] - b[1]) > 1.0f) continue;
                    std::printf("crate target: %s reached by %s\n", tg->name().c_str(), cr->name().c_str());
                    tg->set_enabled(false);
                    tg->execute(tg->message);
                    break;
                }
            }
            // Darts in flight: straight on until the Prince or a wall.
            for (size_t i = 0; i < g_darts.size(); ) {
                dart& d = g_darts[i];
                const float step = 18.0f * dt;
                bool gone = game_time - d.born > 3.0;
                k2::engine::pick_hit h;
                float travel = step;
                if (!gone && sc.coll.pick(d.p, d.d, step, false, h)) { travel = h.distance; gone = true; }
                // The Prince: his body a column from his feet up 1.8.
                if (pp && combat.dead_since < 0.0) {
                    const float ex = d.p[0] + d.d[0] * travel, ez = d.p[2] + d.d[2] * travel;
                    const float vx = ex - d.p[0], vz = ez - d.p[2];
                    const float wx = pp[0] - d.p[0], wz = pp[2] - d.p[2];
                    const float l2 = vx * vx + vz * vz;
                    const float u = l2 > 0.0f ? std::max(0.0f, std::min(1.0f, (wx * vx + wz * vz) / l2)) : 0.0f;
                    const float cx = d.p[0] + vx * u - pp[0], cz = d.p[2] + vz * u - pp[2];
                    if (cx * cx + cz * cz < 0.4f * 0.4f && d.p[1] > pp[1] && d.p[1] < pp[1] + 1.8f) {
                        for (size_t a = 0; a < sc.actors.size(); ++a)
                            if (is_prince(sc.actors[a])) {
                                prince_damage(combat, sc, sc.actors[a], combat_tuning::BOTTLE, DIR_CENTER, game_time);
                                break;
                            }
                        play_sound("MULdarthitz", d.p, 12.0f, false);
                        gone = true;
                    }
                }
                for (int k = 0; k < 3; ++k) d.p[k] += d.d[k] * travel;
                if (gone) {
                    fx_spawn("DartImpact.NIF", d.p, 0.0f, game_time, 0.5);
                    g_darts.erase(g_darts.begin() + long(i));
                    continue;
                }
                if (int(game_time * 30.0) % 2 == 0)
                    fx_spawn("DartTrail.NIF", d.p, std::atan2(d.d[0], d.d[2]), game_time, 0.2);
                ++i;
            }
            // Arrows: the Prince's hit guards and ArrowTargets, the archers'
            // hit him; walls stop both.
            for (size_t i = 0; i < g_arrows.size(); ) {
                arrow_shot& ar = g_arrows[i];
                if (ar.stuck_at >= 0.0) {
                    if (game_time - ar.stuck_at > arrow_tuning::STUCK_FOR) { g_arrows.erase(g_arrows.begin() + long(i)); continue; }
                    ++i;
                    continue;
                }
                const float step = arrow_tuning::SPEED * dt;
                bool gone = game_time - ar.born > 3.0, stuck = false;
                k2::engine::pick_hit h;
                float travel = step;
                if (!gone && sc.coll.pick(ar.p, ar.d, step, false, h)) { travel = h.distance; stuck = true; }
                // A body: a column 0.4 round from the feet up 1.8, met along
                // this frame's path.
                struct body_hit { static bool test(const float* p, const float* d, float len, const float* foot) {
                    const float wx = foot[0] - p[0], wz = foot[2] - p[2];
                    const float hl = std::sqrt(d[0] * d[0] + d[2] * d[2]);
                    if (hl < 1e-5f) return false;
                    const float u = std::max(0.0f, std::min(len * hl, (wx * d[0] + wz * d[2]) / hl));
                    const float t = u / hl;
                    const float cx = p[0] + d[0] * t - foot[0], cz = p[2] + d[2] * t - foot[2];
                    const float y = p[1] + d[1] * t;
                    return cx * cx + cz * cz < 0.16f && y > foot[1] && y < foot[1] + 1.8f;
                } };
                if (ar.from_prince) {
                    for (size_t e = 0; e < combat.enemies.size() && !gone; ++e) {
                        enemy_ai& en = combat.enemies[e];
                        actor_inst& ea = sc.actors[size_t(en.actor)];
                        if (en.state == enemy_ai::DEAD || ea.hidden) continue;
                        if (!body_hit::test(ar.p, ar.d, travel, ea.node->local.translate)) continue;
                        en.waiting = false;
                        enemy_damage(combat, sc, en, arrow_tuning::DAMAGE, DIR_CENTER, game_time, false);
                        play_sound("mularrowhitop", ar.p, 14.0f, false);
                        gone = true;
                    }
                    for (size_t t = 0; t < sc.traps.size() && !gone; ++t) {
                        scene_trap* tg = dynamic_cast<scene_trap*>(sc.traps[t]);
                        if (!tg || tg->type != "ArrowTarget" || !tg->enabled()) continue;
                        const float* b = tg->box;
                        bool in = false;
                        for (int sN = 0; sN <= 4 && !in; ++sN) {
                            const float f = travel * float(sN) / 4.0f;
                            const float q[3] = { ar.p[0] + ar.d[0] * f, ar.p[1] + ar.d[1] * f, ar.p[2] + ar.d[2] * f };
                            in = q[0] >= b[0] - 0.2f && q[0] <= b[3] + 0.2f && q[1] >= b[1] - 0.2f && q[1] <= b[4] + 0.2f &&
                                 q[2] >= b[2] - 0.2f && q[2] <= b[5] + 0.2f;
                        }
                        if (!in) continue;
                        std::printf("arrow target: %s hit\n", tg->name().c_str());
                        tg->set_enabled(false);
                        tg->execute(tg->message);
                        play_sound("L02brarrowhit", ar.p, 14.0f, false);
                        stuck = true;
                    }
                } else if (pp && combat.dead_since < 0.0 && body_hit::test(ar.p, ar.d, travel, pp)) {
                    for (size_t a = 0; a < sc.actors.size(); ++a)
                        if (is_prince(sc.actors[a])) {
                            prince_damage(combat, sc, sc.actors[a], arrow_tuning::DAMAGE, DIR_CENTER, game_time);
                            break;
                        }
                    play_sound("mularrowhitop", ar.p, 14.0f, false);
                    gone = true;
                }
                if (!gone) for (int k = 0; k < 3; ++k) ar.p[k] += ar.d[k] * travel;
                if (gone) { g_arrows.erase(g_arrows.begin() + long(i)); continue; }
                if (stuck) { ar.stuck_at = game_time; play_sound("mularrowstrik", ar.p, 14.0f, false); }
                ++i;
            }
        }
        const bool snoozed = !noscript && game_time < (prince_msgs ? prince_msgs->snooze_until : 0.0);
        input_locked = snoozed;

        // Two control modes. Follow mode drives the Prince and puts the camera
        // behind him, which is what the game does; free-fly drives the camera
        // and is the inspection tool. F toggles. Follow is the default whenever
        // the level actually spawned a Prince.
        actor_inst* player = 0;
        if (follow_mode) {
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) { player = &sc.actors[i]; break; }
        }

        // --hold KEYS@T: headless input. Letters, or ' ' for Space, held from
        // game time T on.
        if (!holds.empty()) {
            // A key is down while any of its ranges covers now.
            for (size_t i = 0; i < holds.size(); ++i)
                for (size_t c = 0; c < holds[i].keys.size(); ++c) {
                    const char ch = holds[i].keys[c];
                    if (ch >= 'A' && ch <= 'Z') down[plat::KEY_A + (ch - 'A')] = false;
                    if (ch == '_') down[plat::KEY_SPACE] = false;
                    if (ch == '^') down[plat::KEY_LSHIFT] = false;
                }
            for (size_t i = 0; i < holds.size(); ++i) {
                if (game_time < holds[i].from || game_time >= holds[i].to) continue;
                for (size_t c = 0; c < holds[i].keys.size(); ++c) {
                    const char ch = holds[i].keys[c];
                    if (ch >= 'A' && ch <= 'Z') down[plat::KEY_A + (ch - 'A')] = true;
                    if (ch == '_') down[plat::KEY_SPACE] = true;
                    if (ch == '^') down[plat::KEY_LSHIFT] = true;
                }
            }
        }

        // --- input -------------------------------------------------------
        // Keyboard and the first gamepad, folded into one stick, buttons and a
        // camera look. Two schemes map the stick onto the Prince:
        //   Modern (default): the stick moves him relative to the camera, and
        //     the camera is the player's own - mouse drag or right stick.
        //   Classic: the original's tank controls - up walks the way he faces,
        //     down walks backwards, left/right turn him - and the camera stays
        //     behind him.
        plat::pad_state pad;
        plat::pad_poll(pad);
        pad_act_down = pad.connected && pad.button[plat::PAD_X];
        float in_x = 0.0f, in_y = 0.0f;                 // stick: x right, y forward
        bool  pad_run = false;
        if (!(player && snoozed)) {
            if (down[plat::KEY_W] || down[plat::KEY_UP] || autowalk) in_y += 1.0f;
            if (down[plat::KEY_S] || down[plat::KEY_DOWN])           in_y -= 1.0f;
            if (down[plat::KEY_D] || down[plat::KEY_RIGHT])          in_x += 1.0f;
            if (down[plat::KEY_A] || down[plat::KEY_LEFT])           in_x -= 1.0f;
            if (pad.connected) {
                float lx = pad.axis[plat::PAD_AXIS_LX], ly = pad.axis[plat::PAD_AXIS_LY];
                const float lm = std::sqrt(lx * lx + ly * ly);
                if (lm > 0.2f) {
                    in_x += lx; in_y += ly;
                    pad_run = lm > 0.85f;           // a full push runs, part walks
                }
                if (pad.button[plat::PAD_DPAD_UP])    in_y += 1.0f;
                if (pad.button[plat::PAD_DPAD_DOWN])  in_y -= 1.0f;
                if (pad.button[plat::PAD_DPAD_RIGHT]) in_x += 1.0f;
                if (pad.button[plat::PAD_DPAD_LEFT])  in_x -= 1.0f;
            }
        }
        {
            const float m = std::sqrt(in_x * in_x + in_y * in_y);
            if (m > 1.0f) { in_x /= m; in_y /= m; }
        }
        // Right stick: the camera, unless a cutscene holds it.
        if (pad.connected && !input_locked) {
            const float rx = pad.axis[plat::PAD_AXIS_RX], ry = pad.axis[plat::PAD_AXIS_RY];
            if (std::fabs(rx) > 0.15f) cam.yaw   += rx * 2.6f * dt;
            if (std::fabs(ry) > 0.15f) cam.pitch += ry * 1.6f * dt;
            // Motion camera (Select): turning and tilting the pad turns the
            // camera, as its gyroscope reads it. Small rates are noise.
            if (motion_camera && pad.has_motion && !input_locked) {
                const float gy = pad.motion[1], gx = pad.motion[0];
                if (std::fabs(gy) > 0.05f) cam.yaw   -= gy * dt;
                if (std::fabs(gx) > 0.05f) cam.pitch += gx * dt;
            }
            if (cam.pitch >  1.2f) cam.pitch =  1.2f;
            if (cam.pitch < -1.2f) cam.pitch = -1.2f;
        }

        float mv[3] = {0.0f, 0.0f, 0.0f};
        bool  walk_back = false;                        // Classic: S walks backwards
        if (player && classic) {
            // Tank controls: left/right turn, up/down along his facing.
            if (std::fabs(in_x) > 0.2f && player->scripted == 0 && crates.state == crate_ctl::NONE &&
                ledges.state == hang_ctl::NONE && jumps.state == jump_ctl::NONE)
                player_yaw -= in_x * 2.6f * dt;
            const float pf[3] = { std::sin(player_yaw), 0.0f, std::cos(player_yaw) };
            if (in_y > 0.2f)       for (int a = 0; a < 3; ++a) mv[a] = pf[a] * in_y;
            else if (in_y < -0.2f) { for (int a = 0; a < 3; ++a) mv[a] = pf[a] * in_y; walk_back = true; }
        } else {
            // Camera-relative, on the ground plane.
            float fl[3] = { fwd[0], 0.0f, fwd[2] };
            const float fm = std::sqrt(fl[0] * fl[0] + fl[2] * fl[2]);
            if (fm > 1e-5f) { fl[0] /= fm; fl[2] /= fm; }
            for (int a = 0; a < 3; ++a) mv[a] = fl[a] * in_y + right[a] * in_x;
            if (!player) {
                // Free-fly keeps the camera's own pitch.
                for (int a = 0; a < 3; ++a) mv[a] = fwd[a] * in_y + right[a] * in_x;
            }
        }

        if (player) {
            // Movement is horizontal: the Prince walks, he does not fly; the
            // floor below decides his height.
            mv[1] = 0.0f;
            const float ml = std::sqrt(mv[0]*mv[0] + mv[2]*mv[2]);
            k2::engine::transform& lt = player->node->local;
            // The platform under him moved since last frame: so does he.
            if (ride.node >= 0 && !noclip) {
                float w[3];
                sc.coll.node_to_world(ride.node, ride.local, w);
                for (int k = 0; k < 3; ++k) lt.translate[k] += w[k] - ride.at[k];
            }
            const bool run = autorun || down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT] || pad_run ||
                             (pad.connected && (pad.button[plat::PAD_RSHOULDER] ||
                                                pad.axis[plat::PAD_AXIS_RTRIGGER] > 0.5f));
            // The input in the Prince's own frame: x his left, z ahead.
            const float pf[3] = { std::sin(player_yaw), 0.0f, std::cos(player_yaw) };
            const float ps[3] = { std::cos(player_yaw), 0.0f, -std::sin(player_yaw) };
            float local[3] = { 0.0f, 0.0f, 0.0f };
            if (classic) { local[0] = -in_x; local[2] = in_y; }
            else if (ledges.state != hang_ctl::NONE) {
                // Hanging, up/down are pull up/let go whatever the camera's
                // angle; left/right shimmy the way they point on screen.
                local[2] = in_y;
                local[0] = in_x * (right[0] * ps[0] + right[2] * ps[2]);
            } else if (ml > 1e-6f) {
                local[0] = (mv[0] * ps[0] + mv[2] * ps[2]) / std::max(ml, 1.0f);
                local[2] = (mv[0] * pf[0] + mv[2] * pf[2]) / std::max(ml, 1.0f);
            }
            // E / pad X acts (grab a crate and hold, talk); Space / pad A jumps.
            const bool act  = !snoozed && (down[plat::KEY_E] || autopush ||
                                           (pad.connected && pad.button[plat::PAD_X]));
            const bool push = !snoozed && (local[2] > 0.5f || autopush);
            const bool pull = !snoozed && local[2] < -0.5f;
            const bool scripted_now = player->scripted != 0 || combat.dead_since >= 0.0 ||
                                      game_time < combat.prince_busy_until;
            const bool jump_key = !snoozed && (down[plat::KEY_SPACE] || autojump ||
                                               (pad.connected && pad.button[plat::PAD_A]));
            const bool jump_pressed_raw = jump_key && !jump_was_down;
            jump_was_down = jump_key;
            const bool act_pressed = act && !act_was_down;
            act_was_down = act;

            // --- combat input: R / pad Y draw or sheathe, F / pad B attack,
            // G / pad LB block (held). Only with the sword.
            const bool dead = combat.dead_since >= 0.0;
            const bool draw_key = !snoozed && (down[plat::KEY_R] || (pad.connected && pad.button[plat::PAD_Y]));
            const bool atk_key  = !snoozed && (down[plat::KEY_F] || (pad.connected && pad.button[plat::PAD_B]));
            const bool blk_key  = !snoozed && (down[plat::KEY_G] || (pad.connected && pad.button[plat::PAD_LSHOULDER]));
            const bool draw_pressed = draw_key && !draw_was_down;
            const bool atk_pressed  = atk_key && !atk_was_down;
            draw_was_down = draw_key;
            atk_was_down = atk_key;
            const bool free_body = !dead && !player->scripted && game_time >= combat.prince_busy_until &&
                                   jumps.state == jump_ctl::NONE && ledges.state == hang_ctl::NONE &&
                                   crates.state == crate_ctl::NONE;
            const bool bow_key = !snoozed && (down[plat::KEY_B] || (pad.connected && pad.button[plat::PAD_LSTICK]));
            const bool bow_pressed = bow_key && !bow_was_down;
            bow_was_down = bow_key;
            if (!player->has_bow || dead) bow.ready = false;
            if (bow_pressed && player->has_bow && free_body && !combat.prince_drawn) {
                bow.ready = !bow.ready;
                bow.stage = 0;
                const char* clip = bow.ready ? "BowDraw_G" : "BowPutAway_G";
                play_once(sc, *player, clip, game_time, player_yaw, false);
                combat.prince_busy_until = game_time + skill_seconds(sc, *player, clip);
                player->stance = 0;
            }
            player->bow_shown = bow.ready;
            if (bow.ready && !dead) {
                if (!input_locked) player_yaw = 3.14159265f - cam.yaw;
                if (free_body) {
                    if (bow.stage == 0) {
                        player->stance = find_actor_skill(sc, *player, "BowDrawBase_G");
                        if (atk_key && player->arrows > 0) {
                            play_once(sc, *player, "BowDrawArrow_G", game_time, player_yaw, false);
                            play_sound("MULarrowdraw", lt.translate, 12.0f, false);
                            bow.stage = 1;
                            bow.released = false;
                        } else if (atk_pressed) {
                            std::printf("bow: no arrows\n");     // the original's empty click is not traced
                        }
                    } else if (bow.stage == 1) {
                        player->stance = find_actor_skill(sc, *player, "BowAim_G");
                        bow.stage = 2;
                    }
                    if (bow.stage == 2 && (!atk_key || bow.released)) {
                        play_once(sc, *player, "BowFireArrow_G", game_time, player_yaw, false);
                        const float fx = std::sin(player_yaw), fz = std::cos(player_yaw);
                        const float from[3] = { lt.translate[0] + fx * 0.6f, lt.translate[1] + 1.45f,
                                                lt.translate[2] + fz * 0.6f };
                        float to[3] = { from[0] + fx * 30.0f, from[1], from[2] + fz * 30.0f };
                        float best = 0.97f;         // cos of the help cone, ~14 degrees
                        for (size_t e = 0; e < combat.enemies.size(); ++e) {
                            const actor_inst& ea = sc.actors[size_t(combat.enemies[e].actor)];
                            if (combat.enemies[e].state == enemy_ai::DEAD || ea.hidden) continue;
                            const float* q = ea.node->local.translate;
                            const float c[3] = { q[0], q[1] + 1.2f, q[2] };
                            const float vx = c[0] - from[0], vz = c[2] - from[2], l = std::sqrt(vx * vx + vz * vz);
                            if (l < 0.5f || l > 40.0f) continue;
                            const float cs = (vx * fx + vz * fz) / l;
                            if (cs > best) { best = cs; for (int k = 0; k < 3; ++k) to[k] = c[k]; }
                        }
                        for (size_t t = 0; t < sc.traps.size(); ++t) {
                            scene_trap* tg = dynamic_cast<scene_trap*>(sc.traps[t]);
                            if (!tg || tg->type != "ArrowTarget" || !tg->enabled()) continue;
                            const float c[3] = { (tg->box[0] + tg->box[3]) * 0.5f, (tg->box[1] + tg->box[4]) * 0.5f,
                                                 (tg->box[2] + tg->box[5]) * 0.5f };
                            const float vx = c[0] - from[0], vz = c[2] - from[2], l = std::sqrt(vx * vx + vz * vz);
                            if (l < 0.5f || l > 60.0f) continue;
                            const float cs = (vx * fx + vz * fz) / l;
                            if (cs > best) { best = cs; for (int k = 0; k < 3; ++k) to[k] = c[k]; }
                        }
                        fire_arrow(from, to, true, game_time);
                        --player->arrows;
                        std::printf("bow: loosed, %d arrows left\n", player->arrows);
                        bow.stage = 0;
                    }
                } else if (bow.stage == 1 && !atk_key) {
                    bow.released = true;    // let go while still drawing: looses when drawn
                }
            }
            if (draw_pressed && player->armed && free_body && !bow.ready) {
                combat.prince_drawn = !combat.prince_drawn;
                const char* clip = combat.prince_drawn ? "CombatDrawWeaponBack_G2" : "CombatStoreWeaponBack_G2";
                play_once(sc, *player, clip, game_time, player_yaw, false);
                combat.prince_busy_until = game_time + skill_seconds(sc, *player, clip);
                if (!combat.prince_drawn) player->stance = 0;
                // Prince__StDrawSwordDone (0x4AF7FF): a taunt once it is out.
                combat.taunt_at = combat.prince_drawn ? combat.prince_busy_until : -1.0;
            }
            if (combat.taunt_at >= 0.0 && free_body && game_time >= combat.taunt_at) {
                combat.taunt_at = -1.0;
                if (combat.prince_drawn && play_once(sc, *player, "CombatTaunt_G2", game_time, player_yaw, false))
                    combat.prince_busy_until = game_time + skill_seconds(sc, *player, "CombatTaunt_G2");
            }
            // Dying ran out with nobody to finish him: up again with 1.
            if (combat.prince_dying_until >= 0.0 && game_time >= combat.prince_dying_until && !dead) {
                combat.prince_dying_until = -1.0;
                player->posed_hold = player->hold_last = false;
                player->scripted = 0;
                set_clip(*player, LOCO_BASE);
                std::printf("combat: the Prince gets up\n");
            }
            combat.prince_blocking = combat.prince_drawn && blk_key && free_body;
            if (combat.prince_blocking && !combat.prince_was_blocking) {
                // Raised toward a swing coming at him, else any way.
                int bd = std::rand() % 3;
                for (size_t i = 0; i < combat.enemies.size(); ++i) {
                    const enemy_ai& en = combat.enemies[i];
                    if (!en.hit.active || en.state == enemy_ai::DEAD) continue;
                    const float* q = sc.actors[size_t(en.actor)].node->local.translate;
                    const float qx = q[0] - lt.translate[0], qz = q[2] - lt.translate[2];
                    if (qx * qx + qz * qz <= combat_tuning::REACH * combat_tuning::REACH * 1.5f) { bd = en.hit.dir; break; }
                }
                combat.prince_block_dir = bd;
            }
            combat.prince_was_blocking = combat.prince_blocking;
            player->weapon_shown = combat.prince_drawn;
            // The stick's side for a swing; the run key held makes it a feint
            // (the original's Walk + attack, 0x502..0x504).
            const int atk_dir = local[0] > 0.4f ? DIR_LEFT : local[0] < -0.4f ? DIR_RIGHT : DIR_CENTER;
            const bool feint_key = down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT] ||
                                   (pad.connected && pad.button[plat::PAD_RSHOULDER]);
            if (atk_pressed && combat.prince_drawn && !dead) { combat.press_at = game_time; combat.press_dir = atk_dir; }
            if (atk_pressed && combat.prince_drawn && !dead && !free_body) {
                combat.queued_at = game_time;
                combat.queued_dir = atk_dir;
                combat.queued_feint = feint_key;
            }
            // A follow: the next side pressed about when his swing landed,
            // even while that swing's clip still plays.
            if (combat.follow_from >= 0 && combat.prince_drawn && !dead) {
                if (game_time > combat.follow_hi) combat.follow_from = -1;
                else if (combat.press_at >= combat.follow_lo && combat.press_at <= combat.follow_hi &&
                         combat.press_dir == follow_next(combat.follow_from)) {
                    const int nd = follow_next(combat.follow_from);
                    const std::string fc = std::string(follow_clip(combat.follow_from)) + "_G2";
                    combat.follow_from = -1;
                    if (play_once(sc, *player, fc, game_time, player_yaw, false)) {
                        combat.prince_hit.active = true;
                        combat.prince_hit.kind = pending_hit::FOLLOW;
                        combat.prince_hit.dir = nd;
                        combat.prince_hit.at = game_time + fight_tuning.fight(std::string(k_dir_names[nd]) + " Follow Attack Begin", 0.4);
                        combat.prince_hit.dmg = int(fight_tuning.fight(std::string(k_dir_names[nd]) + " Follow Damage", 7));
                        combat.prince_busy_until = game_time + skill_seconds(sc, *player, fc);
                        enemy_react(combat, sc, lt.translate, player_yaw, game_time);
                    }
                }
            }
            if (combat.prince_drawn && free_body) {
                // A queued press goes now, as if just made.
                bool atk_go = atk_pressed;
                int  go_dir = atk_dir;
                bool go_feint = feint_key;
                if (!atk_go && combat.queued_at >= 0.0 && game_time - combat.queued_at < 1.0 &&
                    !(combat.prince_blocking && game_time - combat.queued_at > 0.1)) {
                    atk_go = true;
                    go_dir = combat.queued_dir;
                    go_feint = combat.queued_feint;
                }
                combat.queued_at = -1.0;
                // Face the nearest guard; stand ready, or block.
                const int ne = nearest_enemy(combat, sc, lt.translate, 6.0f);
                if (ne >= 0) player_yaw = yaw_toward(lt.translate,
                                                     sc.actors[size_t(combat.enemies[size_t(ne)].actor)].node->local.translate);
                // A dying guard in reach and in front, for a finisher.
                int dying = -1;
                for (size_t i = 0; i < combat.enemies.size(); ++i) {
                    const enemy_ai& en = combat.enemies[i];
                    if (en.state != enemy_ai::DYING) continue;
                    const float* q = sc.actors[size_t(en.actor)].node->local.translate;
                    const float qx = q[0] - lt.translate[0], qz = q[2] - lt.translate[2];
                    if (qx * qx + qz * qz > combat_tuning::REACH * combat_tuning::REACH || std::fabs(q[1] - lt.translate[1]) > 1.5f) continue;
                    if (angle_diff(std::atan2(qx, qz), player_yaw) > 1.2f) continue;
                    dying = int(i);
                    break;
                }
                if (atk_go && dying >= 0) {
                    enemy_ai& en = combat.enemies[size_t(dying)];
                    actor_inst& ea = sc.actors[size_t(en.actor)];
                    finishing_move(sc, *player, "CombatFinishingMove2_G2", ea, "CombatFinishingReceive2" + en.set, game_time);
                    player_yaw = actor_yaw(*player);
                    combat.prince_busy_until = game_time + skill_seconds(sc, *player, "CombatFinishingMove2_G2");
                    enemy_killed(sc, en);
                    std::printf("combat: the Prince finishes %s\n", ea.script_name.c_str());
                } else if (atk_go && go_feint) {
                    // Feint (Fight__StartFeint 0x4AC229): threatens, lands nothing.
                    const std::string fc = std::string("CombatFeint") + k_dir_names[go_dir] + "_G2";
                    if (play_once(sc, *player, fc, game_time, player_yaw, false)) {
                        const double len = fight_tuning.fight(std::string("Sword ") + k_dir_names[go_dir] + " Feint", 0.367);
                        combat.prince_hit.active = true;
                        combat.prince_hit.kind = pending_hit::FEINT;
                        combat.prince_hit.dir = go_dir;
                        combat.prince_hit.dmg = -1;
                        combat.prince_hit.at = game_time + len * 0.5;
                        combat.prince_busy_until = game_time + std::max(len, skill_seconds(sc, *player, fc));
                        enemy_react(combat, sc, lt.translate, player_yaw, game_time);
                    }
                } else if (atk_go && combat.prince_blocking && game_time < combat.counter_until &&
                           go_dir == combat.counter_dir) {
                    // Counter (0x4AC002): the side he just blocked, from the
                    // block, in the window. Left counter numbers both sides.
                    const std::string cc = std::string("CombatCounter") + k_dir_names[go_dir] + "_G2";
                    combat.counter_until = -1.0;
                    if (play_once(sc, *player, cc, game_time, player_yaw, false)) {
                        combat.prince_hit.active = true;
                        combat.prince_hit.kind = pending_hit::COUNTER;
                        combat.prince_hit.dir = go_dir;
                        combat.prince_hit.at = game_time + fight_tuning.fight("Sword Left Counter Attack Begin", 0.467);
                        combat.prince_hit.dmg = int(fight_tuning.fight("Sword Left Counter Damage", 8));
                        combat.prince_busy_until = game_time + skill_seconds(sc, *player, cc);
                        enemy_react(combat, sc, lt.translate, player_yaw, game_time);
                    }
                } else if (atk_go) {
                    const int dir = go_dir;
                    const std::string atk = std::string("CombatAttack") + k_dir_names[dir] + "_G2";
                    if (play_once(sc, *player, atk, game_time, player_yaw, false)) {
                        combat.prince_hit.active = true;
                        combat.prince_hit.kind = pending_hit::ATTACK;
                        combat.prince_hit.dmg = -1;
                        combat.prince_hit.dir = dir;
                        combat.prince_hit.at = game_time + fight_tuning.fight(
                            std::string("Sword ") + k_dir_names[dir] + " Attack Begin", 0.3);
                        combat.prince_busy_until = game_time + skill_seconds(sc, *player, atk);
                        // The same direction again, quickly, is slower to come
                        // round (Same Direction Attack Penalty).
                        if (dir == prince_last_dir &&
                            game_time - prince_last_attack < fight_tuning.fight("Max Fast Attack Time", 1.5))
                            combat.prince_hit.at += fight_tuning.fight("Same Direction Attack Penalty", 0.4);
                        prince_last_dir = dir;
                        prince_last_attack = game_time;
                        enemy_react(combat, sc, lt.translate, player_yaw, game_time);
                    }
                }
                player->stance = combat.prince_blocking
                    ? find_actor_skill(sc, *player, std::string("CombatBlocking") + k_dir_names[combat.prince_block_dir] + "_G2")
                    : player->health <= combat_tuning::BOTTLE ? find_actor_skill(sc, *player, "CombatWoozy_G2") : 0;
                if (!player->stance) player->stance = find_actor_skill(sc, *player, "CombatAtReady_G2");
            }
            // E near a pickup collects it.
            if (act && !dead && !taking.active()) {
                for (size_t i = 0; i < sc.pickups.size(); ++i) {
                    if (pickup_taken[i] || !pickup_live(sc, i)) continue;
                    const scene::pickup_def& pk = sc.pickups[i];
                    const float dx = pk.pos[0] - lt.translate[0], dz = pk.pos[2] - lt.translate[2];
                    if (dx * dx + dz * dz > 1.6f * 1.6f || std::fabs(pk.pos[1] - lt.translate[1]) > 2.0f) continue;
                    pickup_taken[i] = true;
                    taking.begin(sc, *player, pk, int(i));
                    player_yaw = yaw_toward(lt.translate, pk.pos);
                    break;
                }
            }
            // A pickup's clips, one after another: the item leaves the floor
            // as the first ends, and works as the last (the finish) starts.
            if (taking.active() && !player->scripted && !dead) {
                const scene::pickup_def& pk = sc.pickups[size_t(taking.pickup)];
                if ((taking.step >= 1 || taking.clips.empty()) && !taking.taken) {
                    taking.taken = true;
                    if (pk.actor >= 0) sc.actors[size_t(pk.actor)].hidden = true;
                    play_sound("PRNpickup", lt.translate, 10.0f, false);
                }
                if (taking.step + 1 >= taking.clips.size() && !taking.applied) {
                    taking.applied = true;
                    pickup_apply(combat, sc, *player, pk, game_time);
                    const bool code = pk.argv.size() > 2 && equals_nocase(pk.argv[2], "reload");
                    if (!code && pk.argv.size() > 3 && !pk.argv[3].empty()) sc.script.execute(pk.argv[3], 0);
                }
                if (taking.step < taking.clips.size())
                    play_once(sc, *player, taking.clips[taking.step++], game_time, player_yaw, false);
                else
                    taking.pickup = -1;
            }
            if (!dead) specials_update(sc, combat, *player, game_time);
            if (!player->pending_kill.empty()) {  // "Prince,kill:Lvl1HandGetsEaten_G"
                if (!dead) prince_damage(combat, sc, *player, player->health, 0, game_time, player->pending_kill.c_str());
                player->pending_kill.clear();
            }
            if (player->pending_hurt > 0) {       // "prince,hurt:N" from a trap
                if (!dead) prince_damage(combat, sc, *player, player->pending_hurt, 0, game_time);
                player->pending_hurt = 0;
            }
            // Pickups shown or hidden by script (P4BoxPotion02 appears when
            // its box breaks) follow their trap.
            for (size_t i = 0; i < sc.pickups.size(); ++i)
                if (sc.pickups[i].actor >= 0)
                    sc.actors[size_t(sc.pickups[i].actor)].hidden = pickup_taken[i] || !pickup_live(sc, i);
            // While the sword is drawn, jump is not a jump.
            const bool jump_pressed = jump_pressed_raw && !combat.prince_drawn && !dead;

            // Ledges first: hanging and climbing own the body.
            bool dropped = false;
            const bool on_ledge = !scripted_now &&
                hang_update(ledges, *player, sc.coll, lt.translate, player_yaw, game_time,
                            local, jump_pressed, dropped);
            if (dropped) {
                // Let go: fall from the hang, landing as from a jump up.
                jumps.state = jump_ctl::AIR;
                jumps.dir = 0;
                jumps.vel[0] = jumps.vel[1] = jumps.vel[2] = 0.0f;
                jumps.peak_y = lt.translate[1];
                jump_begin(jumps, *player, FALL_VERT, game_time, lt.translate);
                regrab_after = game_time + 0.6;
            }
            if (!on_ledge && jump_pressed && !scripted_now && crates.state == crate_ctl::NONE && pole.state == pole_ctl::NONE && bar.state == bar_ctl::NONE && rope.rope < 0 &&
                crouch.state == crouch_ctl::NONE &&
                jumps.state == jump_ctl::NONE && player_vy == 0.0f && swim.state == swim_ctl::NONE) {
                // Jump never climbs (the original climbs from StStartStepBack,
                // on the action input, 0x4B4CC8): under a high ledge it jumps
                // straight up to catch it, else an ordinary jump.
                {
                    const bool running = run && ml > 1e-6f && player->clip == LOCO_RUN;
                    float jl[3] = { local[0], 0.0f, local[2] };
                    hang_measure_grip(ledges, *player);
                    ledge_hit high;
                    if (!running && ledges.have_grip &&
                        find_ledge(sc.coll, lt.translate, player_yaw, 2.0f, ledges.grip[1] + 2.4f, high)) {
                        // Line up under where he will hang, square to the
                        // wall, and jump straight up: the hands then meet the
                        // edge where the hang puts them.
                        player_yaw = high.facing;
                        hang_ctl probe = ledges;
                        probe.ledge = high;
                        float hr[3];
                        hang_root(probe, hr);
                        float dx = hr[0] - lt.translate[0], dz = hr[2] - lt.translate[2];
                        if (dx * dx + dz * dz < 1.0f) move_horizontal(sc.coll, lt.translate, dx, dz);
                        jl[0] = jl[2] = 0.0f;
                    }
                    jump_start(jumps, *player, running, jl, game_time, lt.translate);
                }
            }
            // Water: deep enough and he swims; that owns the body until he
            // climbs or walks out.
            float surf = 0.0f;
            const bool water = !noclip && !scripted_now && !on_ledge && ledges.state == hang_ctl::NONE &&
                               !dead && water_surface(sc, lt.translate, surf);
            // Under the outline but not under the water - a room below the
            // pool or the pit, a ceiling between him and the surface - is dry.
            // Checked on entry only: under water he may swim beneath an arch.
            bool open_above = true;
            if (swim.state == swim_ctl::NONE && water) {
                const float from[3] = { lt.translate[0], lt.translate[1] + 0.3f, lt.translate[2] };
                const float up[3] = { 0.0f, 1.0f, 0.0f };
                k2::engine::pick_hit h;
                if (surf - from[1] > 0.0f && sc.coll.pick(from, up, surf - from[1], false, h)) open_above = false;
            }
            if (swim.state == swim_ctl::NONE && water && open_above && lt.translate[1] < surf - swim_tuning::SWIM_ENTER) {
                swim.state = swim_ctl::SURFACE;
                jumps.state = jump_ctl::NONE;
                player_vy = 0.0f;
                set_clip(*player, LOCO_BASE);
                play_sound("prnUnderwater", lt.translate, 14.0f, false);
                std::printf("swim: in the water (surface %.2f)\n", surf);
            }
            if (swim.state != swim_ctl::NONE && !water) { swim.state = swim_ctl::NONE; player->stance = 0; }
            if (swim.state == swim_ctl::NONE) player->body_pitch = 0.0f;
            bool in_swim = false;
            if (swim.state != swim_ctl::NONE) {
                // Out over a low edge: the jump climbs, as on land.
                if (swim.state == swim_ctl::SURFACE && (jump_pressed || act_pressed) &&
                    hang_try_climb(ledges, *player, sc.coll, lt.translate, player_yaw, game_time)) {
                    swim.state = swim_ctl::NONE;
                    player->stance = 0;
                } else {
                    bool drowning = false;
                    in_swim = swim_update(swim, sc, *player, lt.translate, player_yaw, surf, mv, ml,
                                          jump_key, act, act_pressed && swim.state == swim_ctl::SURFACE,
                                          classic, dt, drowning);
                    // Currents carry a swimmer along.
                    if (in_swim)
                        for (size_t i = 0; i < sc.traps.size(); ++i) {
                            scene_trap* ft = dynamic_cast<scene_trap*>(sc.traps[i]);
                            if (!ft || !ft->has_current || !ft->enabled()) continue;
                            const float* b = ft->box;
                            const float* q = lt.translate;
                            if (q[0] < b[0] || q[0] > b[3] || q[1] < b[1] || q[1] > b[4] || q[2] < b[2] || q[2] > b[5]) continue;
                            move_horizontal(sc.coll, lt.translate, ft->current[0] * dt, ft->current[2] * dt);
                        }
                    if (drowning && game_time >= drown_tick) {
                        drown_tick = game_time + 1.0;
                        prince_damage(combat, sc, *player, combat_tuning::BOTTLE / 3, 0, game_time, "DeathUnderwater_G");
                    }
                }
            }
            bool in_jump = !in_swim && !scripted_now && !on_ledge && ledges.state == hang_ctl::NONE &&
                jump_update(jumps, *player, sc.coll, lt.translate, player_yaw, game_time, dt);
            if (jumps.fell > 0.0f) {            // a hard landing hurts; a long drop kills
                const int dmg = jump_tuning::fall_damage(jumps.fell, player->health);
                jumps.fell = 0.0f;
                if (dmg > 0 && !dead) {
                    prince_damage(combat, sc, *player, dmg, 0, game_time, "LandVertDeadly_G");
                    if (combat.dead_since < 0.0)
                        player->scripted = 0;   // the painful landing, not a sword injury
                }
            }
            if (!dead && combat.dead_since < 0.0)
                for (size_t i = 0; i < sc.abysses.size(); ++i) {
                    if (!sc.abysses[i].trap->enabled() || !sc.abysses[i].vol.contains(lt.translate)) continue;
                    // Into the abyss: dead, still falling.
                    player->health = 0;
                    combat.dead_since = game_time;
                    std::printf("combat: the Prince falls into the abyss\n");
                    break;
                }
            // --- bars ----------------------------------------------------------
            {
                namespace bt = bar_tuning;
                // A bar's axis: its box's long horizontal side, through its middle.
                struct bar_axis { static bool along_x(const scene_trap* b) { return b->box[3] - b->box[0] >= b->box[5] - b->box[2]; } };
                struct bar_place { static void at(const bar_ctl& bc, float* out) {
                    const float c = std::cos(bc.yaw), sn = std::sin(bc.yaw);
                    out[0] = bc.hold[0] - (c * bc.grip[0] + sn * bc.grip[2]);
                    out[1] = bc.hold[1] - bc.grip[1];
                    out[2] = bc.hold[2] - (-sn * bc.grip[0] + c * bc.grip[2]);
                } };
                struct bar_drop { static void go(bar_ctl& bc, jump_ctl& jc, actor_inst& p, const float* pos, double now,
                                                  const float* vel, int clip) {
                    bc.state = bar_ctl::NONE;
                    p.stance = 0;
                    jc.state = jump_ctl::AIR;
                    jc.dir = vel ? 5 : 0;
                    for (int k = 0; k < 3; ++k) jc.vel[k] = vel ? vel[k] : 0.0f;
                    jc.peak_y = pos[1];
                    jump_begin(jc, p, clip, now, pos);
                } };
                if (!bar.have_grip) {
                    const assets::skl_file* hb = find_actor_skill(sc, *player, "HorizHangBase_M");
                    bar.have_grip = hb && clip_grip(*player, *hb, 0, bar.grip);
                }
                // Catching one: from a jump or fall, the hands near the axis
                // (ours; the original reaches bars through its ledge finder,
                // 0x4CF65C, untraced); or the action input beneath one.
                const bool air = in_jump && jumps.state == jump_ctl::AIR && game_time >= regrab_after;
                if (bar.state == bar_ctl::NONE && pole.state == pole_ctl::NONE && !dead && !in_swim &&
                    ledges.state == hang_ctl::NONE && (air || (act_pressed && !scripted_now))) {
                    const float hands[3] = { lt.translate[0], lt.translate[1] + bar.grip[1], lt.translate[2] };
                    for (size_t t = 0; t < sc.traps.size(); ++t) {
                        scene_trap* b = dynamic_cast<scene_trap*>(sc.traps[t]);
                        if (!b || b->type != "Bar" || !b->enabled()) continue;
                        const bool ax = bar_axis::along_x(b);
                        const float by = (b->box[1] + b->box[4]) * 0.5f;
                        const float lo = ax ? b->box[0] : b->box[2], hi = ax ? b->box[3] : b->box[5];
                        const float along = ax ? hands[0] : hands[2];
                        const float across = ax ? hands[2] - (b->box[2] + b->box[5]) * 0.5f
                                                : hands[0] - (b->box[0] + b->box[3]) * 0.5f;
                        const float up = by - hands[1];
                        if (along < lo + 0.2f || along > hi - 0.2f) continue;
                        if (std::fabs(across) > bt::REACH) continue;
                        if (air ? (up < -0.4f || up > 0.5f) : (up < 0.0f || up > bt::STAND_REACH)) continue;
                        bar.bar = b;
                        bar.hold[0] = ax ? along : (b->box[0] + b->box[3]) * 0.5f;
                        bar.hold[1] = by;
                        bar.hold[2] = ax ? (b->box[2] + b->box[5]) * 0.5f : along;
                        // Face across the bar, the way he was facing.
                        const float fx = std::sin(player_yaw), fz = std::cos(player_yaw);
                        bar.yaw = ax ? (fz >= 0.0f ? 0.0f : 3.14159265f) : (fx >= 0.0f ? 1.5707963f : -1.5707963f);
                        player_yaw = bar.yaw;
                        const bool running = air && jumps.dir == 5;
                        jumps.state = jump_ctl::NONE;
                        in_jump = false;
                        bar_place::at(bar, lt.translate);
                        play_once(sc, *player, running ? "LedgeGrabHoriz_M" : "LedgeGrabNoMomentum_M", game_time, bar.yaw, false);
                        bar.state = bar_ctl::CATCH;
                        std::printf("bar: %s caught\n", b->name().c_str());
                        break;
                    }
                }
                if (bar.state != bar_ctl::NONE) {
                    float at[3];
                    bar_place::at(bar, at);
                    const float ps[3] = { std::cos(bar.yaw), 0.0f, -std::sin(bar.yaw) };    // his left
                    const float lx = in_x * (right[0] * ps[0] + right[2] * ps[2]);
                    const bool ax = bar_axis::along_x(bar.bar);
                    const float lo = ax ? bar.bar->box[0] : bar.bar->box[2], hi = ax ? bar.bar->box[3] : bar.bar->box[5];
                    struct bar_can { static bool step(const bar_ctl& bc, bool along_x, float lo, float hi, int dir, const float* side) {
                        const float d = float(dir) * bar_tuning::STEP;
                        const float v = (along_x ? bc.hold[0] + side[0] * d : bc.hold[2] + side[2] * d);
                        return v > lo + 0.2f && v < hi - 0.2f;     // Prince__CanHangOnBarAt, by the bar's extent
                    } };
                    if (bar.state == bar_ctl::SWING && jump_pressed) {
                        // Prince__CanHorizLaunch (0x4D095D): only in the swing's
                        // middle (0.43 .. 0.75 of the clip) does he fly.
                        const double ph = std::fmod(game_time - bar.swing_t0, bar.swing_len) / bar.swing_len;
                        const float f[3] = { std::sin(bar.yaw), 0.0f, std::cos(bar.yaw) };
                        player->scripted = 0;
                        if (ph > bt::LAUNCH_FROM && ph < bt::LAUNCH_TO) {
                            float pos[3] = { bar.hold[0] + f[0] * bt::LAUNCH_AHEAD, lt.translate[1], bar.hold[2] + f[2] * bt::LAUNCH_AHEAD };
                            for (int k = 0; k < 3; ++k) lt.translate[k] = pos[k];
                            const float v[3] = { f[0] * bt::LAUNCH_Z, bt::LAUNCH_Y, f[2] * bt::LAUNCH_Z };
                            bar_drop::go(bar, jumps, *player, lt.translate, game_time, v, RUN_JUMPING);
                            std::printf("bar: launch\n");
                        } else {
                            bar_drop::go(bar, jumps, *player, lt.translate, game_time, 0, FALL_VERT);
                        }
                        regrab_after = game_time + 0.6;
                    } else if (!player->scripted) {
                        switch (bar.state) {
                        case bar_ctl::CATCH: case bar_ctl::SH_END: case bar_ctl::SW_STOP:
                            bar.state = bar_ctl::HANG;
                            break;
                        case bar_ctl::SH_START: case bar_ctl::SH_MOVE:
                            if (bar.state == bar_ctl::SH_MOVE) {
                                for (int k = 0; k < 3; k += 2) bar.hold[k] += ps[k] * float(bar.dir) * bt::STEP;
                                bar_place::at(bar, at);
                            }
                            if ((lx > 0.5f ? 1 : lx < -0.5f ? -1 : 0) == bar.dir && bar_can::step(bar, ax, lo, hi, bar.dir, ps)) {
                                bar.state = bar_ctl::SH_MOVE;
                                for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                                play_once(sc, *player, bar.dir > 0 ? "HorizMovingLeft_M" : "HorizMovingRight_M", game_time, bar.yaw, false);
                            } else {
                                bar.state = bar_ctl::SH_END;
                                for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                                play_once(sc, *player, bar.dir > 0 ? "HorizEndMovingLeft_M" : "HorizEndMovingRight_M", game_time, bar.yaw, false);
                            }
                            break;
                        case bar_ctl::SW_START: case bar_ctl::SWING:
                            if (in_y > 0.5f) {
                                // StIdleHorizSwing: the loop runs while forward is held.
                                bar.state = bar_ctl::SWING;
                                for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                                if (play_once(sc, *player, "HorizSwingBase_M", game_time, bar.yaw, false)) {
                                    bar.swing_t0 = game_time;
                                    bar.swing_len = std::max(0.1, skill_seconds(sc, *player, "HorizSwingBase_M"));
                                }
                            } else {
                                bar.state = bar_ctl::SW_STOP;
                                for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                                play_once(sc, *player, "HorizSwingStop_M", game_time, bar.yaw, false);
                            }
                            break;
                        default:
                            break;
                        }
                        if (bar.state == bar_ctl::HANG) {
                            // StIdleHorizHang.
                            player->stance = find_actor_skill(sc, *player, "HorizHangBase_M");
                            player_yaw = bar.yaw;
                            for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                            if (in_y < -0.5f || jump_pressed) {
                                bar_drop::go(bar, jumps, *player, lt.translate, game_time, 0, FALL_VERT);
                                regrab_after = game_time + 0.6;
                            } else if (in_y > 0.5f) {
                                bar.state = bar_ctl::SW_START;
                                player->stance = 0;
                                play_once(sc, *player, "HorizSwingStart_M", game_time, bar.yaw, false);
                            } else if (std::fabs(lx) > 0.5f) {
                                const int d = lx > 0.0f ? 1 : -1;
                                if (bar_can::step(bar, ax, lo, hi, d, ps)) {
                                    bar.dir = d;
                                    bar.state = bar_ctl::SH_START;
                                    player->stance = 0;
                                    play_once(sc, *player, d > 0 ? "HorizStartMovingLeft_M" : "HorizStartMovingRight_M", game_time, bar.yaw, false);
                                }
                            }
                        }
                    }
                }
            }
            // --- swinging ropes ------------------------------------------------
            {
                // The ropes run their own clips: a held rope swings on, a
                // let-go one ends its swing and rests (Rope__UpdateSwing,
                // Rope__UpdateEndSwing).
                for (size_t r = 0; r < sc.ropes.size(); ++r) {
                    scene::rope& rp = sc.ropes[r];
                    actor_inst& ra = sc.actors[size_t(rp.actor)];
                    if (ra.scripted) continue;
                    if (rp.grabbed) { play_once(sc, ra, "SwingFullRange", game_time, actor_yaw(ra), true); rp.stage = scene::rope::SWING; }
                    else if (rp.stage == scene::rope::SWING) { play_once(sc, ra, "EndSwing", game_time, actor_yaw(ra), true); rp.stage = scene::rope::END; }
                    else if (rp.stage == scene::rope::END) rp.stage = scene::rope::IDLE;
                }
                struct rope_attach { static bool at(scene& sc, const scene::rope& rp, float* out) {
                    if (rp.attach < 0) return false;
                    const actor_inst& ra = sc.actors[size_t(rp.actor)];
                    k2::engine::transform w;
                    k2::engine::transform_compose(ra.node->world(), ra.cur[size_t(rp.attach)], w);
                    for (int k = 0; k < 3; ++k) out[k] = w.translate[k];
                    return true;
                } };
                // Catching one (Prince__CanGrabRope 0x4D1541): only in the air,
                // his root within 2 below to 0.5 above the rope's resting
                // bottom and 0.61 of it across, the rope not behind him.
                if (rope.rope < 0 && in_jump && jumps.state == jump_ctl::AIR && game_time >= regrab_after && !dead &&
                    bar.state == bar_ctl::NONE && pole.state == pole_ctl::NONE) {
                    for (size_t r = 0; r < sc.ropes.size(); ++r) {
                        scene::rope& rp = sc.ropes[r];
                        const float* q = lt.translate;
                        if (!(rp.pivot[1] - 2.0f < q[1] && q[1] < rp.pivot[1] + 0.5f)) continue;
                        const float dx = rp.pivot[0] - q[0], dz = rp.pivot[2] - q[2];
                        if (dx * dx + dz * dz >= 0.61f * 0.61f) continue;
                        float at[3];
                        if (!rope_attach::at(sc, rp, at)) continue;
                        if (std::sin(player_yaw) * (q[0] - at[0]) + std::cos(player_yaw) * (q[2] - at[2]) > 0.0f) continue;
                        // The rope swings in the plane he faces.
                        actor_inst& ra = sc.actors[size_t(rp.actor)];
                        ra.home_yaw = player_yaw;
                        ra.node->local.set_yaw(player_yaw);
                        play_once(sc, ra, "StartSwing", game_time, player_yaw, true);
                        rp.grabbed = true;
                        rp.stage = scene::rope::SWING;
                        rope.rope = int(r);
                        rope.looping = false;
                        rope.prev_t = -1.0;
                        jumps.state = jump_ctl::NONE;
                        in_jump = false;
                        sc.root->update_world();
                        rope_attach::at(sc, rp, at);
                        for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                        play_once(sc, *player, "SwingStart_M", game_time, player_yaw, false);
                        std::printf("rope: caught\n");
                        break;
                    }
                }
                if (rope.rope >= 0) {
                    scene::rope& rp = sc.ropes[size_t(rope.rope)];
                    float at[3];
                    if (rope_attach::at(sc, rp, at)) {
                        // The Attach joint's velocity, sampled when it moves.
                        if (rope.prev_t >= 0.0 && game_time > rope.prev_t &&
                            (at[0] != rope.prev[0] || at[1] != rope.prev[1] || at[2] != rope.prev[2]))
                            for (int k = 0; k < 3; ++k) rope.vel[k] = (at[k] - rope.prev[k]) / float(game_time - rope.prev_t);
                        for (int k = 0; k < 3; ++k) rope.prev[k] = at[k];
                        rope.prev_t = game_time;
                        // The clip plays about the joint: keep his origin on it.
                        if (player->scripted) for (int k = 0; k < 3; ++k) player->scripted_origin[k] = at[k];
                        for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                    }
                    if (jump_pressed || act_pressed) {
                        // StStartSwingRopeRelease: thrown with the joint's velocity.
                        rp.grabbed = false;
                        rope.rope = -1;
                        player->scripted = 0;
                        player->stance = 0;
                        jumps.state = jump_ctl::AIR;
                        jumps.dir = 5;
                        for (int k = 0; k < 3; ++k) jumps.vel[k] = rope.vel[k];
                        jumps.peak_y = lt.translate[1];
                        jump_begin(jumps, *player, RUN_JUMPING, game_time, lt.translate);
                        regrab_after = game_time + 0.6;
                        std::printf("rope: let go at (%.1f %.1f %.1f)\n", rope.vel[0], rope.vel[1], rope.vel[2]);
                    } else if (!player->scripted) {
                        rope.looping = true;            // "Done with start anim, now do looping!"
                        play_once(sc, *player, "SwingBase_M", game_time, player_yaw, false);
                    }
                }
            }
            // In the air, the hands catch any ledge in reach.
            if (in_jump && jumps.state == jump_ctl::AIR && game_time >= regrab_after && bar.state == bar_ctl::NONE &&
                rope.rope < 0) {
                const float hs = std::sqrt(jumps.vel[0] * jumps.vel[0] + jumps.vel[2] * jumps.vel[2]);
                if (hang_try_grab(ledges, *player, sc.coll, lt.translate, player_yaw,
                                  jumps.vel[1], hs, game_time)) {
                    jumps.state = jump_ctl::NONE;
                    in_jump = false;
                }
            }
            // --- poles ---------------------------------------------------------
            {
                namespace pt = pole_tuning;
                // The root that puts the hands on the axis at hand_y, facing yaw.
                struct pole_place { static void at(const pole_ctl& pc, float* out) {
                    const float c = std::cos(pc.yaw), sn = std::sin(pc.yaw);
                    const float gx = c * pc.grip[0] + sn * pc.grip[2], gz = -sn * pc.grip[0] + c * pc.grip[2];
                    const float* b = pc.pole->box;
                    out[0] = (b[0] + b[3]) * 0.5f - gx;
                    out[1] = pc.hand_y - pc.grip[1];
                    out[2] = (b[2] + b[5]) * 0.5f - gz;
                } };
                // Prince__CanMoveOnPole (0x4D141C), without the body probe.
                struct pole_can { static bool move(const pole_ctl& pc, float dy) {
                    const float y = pc.hand_y + dy;
                    return dy >= 0.0f ? y <= pc.pole->box[4] : y - pt::HOLD + pt::FEET_CLEAR >= pc.pole->box[1];
                } };
                const char* side = pole.hand >= 0 ? "RH_M" : "LH_M";
                const char* other = pole.hand >= 0 ? "LH_M" : "RH_M";     // the slides pick the other way
                if (pole.state == pole_ctl::NONE && !dead && !in_swim && ledges.state == hang_ctl::NONE &&
                    crates.state == crate_ctl::NONE && (act_pressed || (in_jump && jumps.state == jump_ctl::AIR)) &&
                    (!scripted_now || in_jump)) {
                    // Prince__CanGrabPole (0x4D11E4): a pole ahead, in a cos 0.7 cone
                    // from half a unit behind him; the hands 2.25 up must be on it.
                    const float fx = std::sin(player_yaw), fz = std::cos(player_yaw);
                    for (size_t t = 0; t < sc.traps.size(); ++t) {
                        scene_trap* pl = dynamic_cast<scene_trap*>(sc.traps[t]);
                        if (!pl || pl->type != "Pole" || !pl->enabled()) continue;
                        const float ax = (pl->box[0] + pl->box[3]) * 0.5f, az = (pl->box[2] + pl->box[5]) * 0.5f;
                        const float dx = ax - lt.translate[0], dz = az - lt.translate[2];
                        const float d = std::sqrt(dx * dx + dz * dz);
                        if (d > (in_jump ? pt::AIR_REACH : pt::REACH)) continue;
                        const float hy = lt.translate[1] + pt::HOLD;
                        if (hy > pl->box[4] + 0.3f || hy - pt::HOLD + pt::FEET_CLEAR < pl->box[1]) continue;
                        const float bx = ax - (lt.translate[0] - fx * 0.5f), bz = az - (lt.translate[2] - fz * 0.5f);
                        const float bl = std::sqrt(bx * bx + bz * bz);
                        if (bl < 1e-4f || (bx * fx + bz * fz) / bl <= 0.7f) continue;
                        pole.pole = pl;
                        pole.yaw = std::atan2(bx, bz);
                        pole.hand_y = std::min(hy, pl->box[4]);
                        pole.hand = 1;
                        if (!pole.have_grip) {
                            const assets::skl_file* base = find_actor_skill(sc, *player, "VerticalRopeBaseRH_M");
                            pole.have_grip = base && clip_grip(*player, *base, 0, pole.grip);
                        }
                        pl->set_collideable(false);           // StStartPoleMount
                        const bool from_jump = in_jump;
                        jumps.state = jump_ctl::NONE;
                        in_jump = false;
                        player_yaw = pole.yaw;
                        pole_place::at(pole, lt.translate);
                        play_once(sc, *player, from_jump ? "VerticalRopeFromJump_M" : "VerticalRopeAntic_M",
                                  game_time, player_yaw, false);
                        pole.state = pole_ctl::MOUNT;
                        pole.let_go = false;
                        std::printf("pole: %s grabbed\n", pl->name().c_str());
                        break;
                    }
                }
                if (pole.state != pole_ctl::NONE && pole.state != pole_ctl::PUSHOFF && (jump_pressed || act_pressed))
                    pole.let_go = true;
                if (pole.state != pole_ctl::NONE && !player->scripted) {
                    float at[3];
                    switch (pole.state) {
                    case pole_ctl::CLIMB:
                        pole.hand_y += pt::CLIMB_STEP;
                        pole.hand = -pole.hand;           // climbing swaps hands
                        pole.state = pole_ctl::HANG;
                        break;
                    case pole_ctl::SLIDE_START:
                        if (in_y < -0.5f && pole_can::move(pole, -pt::SLIDE_STEP)) {
                            pole.hand_y -= pt::SLIDE_STEP;
                            pole.state = pole_ctl::SLIDE;
                        } else {
                            pole.state = pole_ctl::SLIDE_END;
                            pole_place::at(pole, lt.translate);
                            play_once(sc, *player, std::string("VerticalRopeEndSlideDown") + other, game_time, pole.yaw, false);
                        }
                        break;
                    case pole_ctl::SLIDE_END:
                    case pole_ctl::MOUNT:
                        pole.state = pole_ctl::HANG;
                        break;
                    case pole_ctl::PUSHOFF:
                        // StIdlePolePushOff: then a fall from rest.
                        pole.pole->set_collideable(true);
                        pole.state = pole_ctl::NONE;
                        player->stance = 0;
                        jumps.state = jump_ctl::AIR;
                        jumps.dir = 0;
                        jumps.vel[0] = jumps.vel[1] = jumps.vel[2] = 0.0f;
                        jumps.peak_y = lt.translate[1];
                        jump_begin(jumps, *player, FALL_VERT, game_time, lt.translate);
                        regrab_after = game_time + 0.6;
                        break;
                    default:
                        break;
                    }
                    if (pole.state == pole_ctl::SLIDE) {
                        // StIdlePoleMove: down 3 units a second while back is held.
                        player->stance = find_actor_skill(sc, *player, std::string("VerticalRopeSlideDown") + other);
                        pole_place::at(pole, lt.translate);
                        if (in_y < -0.5f && pole_can::move(pole, -pt::SLIDE_SPEED * dt)) {
                            pole.hand_y -= pt::SLIDE_SPEED * dt;
                        } else {
                            pole.state = pole_ctl::SLIDE_END;
                            player->stance = 0;
                            play_once(sc, *player, std::string("VerticalRopeEndSlideDown") + other, game_time, pole.yaw, false);
                        }
                    } else if (pole.state == pole_ctl::HANG) {
                        // StIdlePoleHang.
                        player->stance = find_actor_skill(sc, *player, std::string("VerticalRopeBase") + (pole.hand >= 0 ? "RH_M" : "LH_M"));
                        if (std::fabs(in_x) > 0.3f) pole.yaw += (in_x > 0.0f ? -1.0f : 1.0f) * pt::TURN_RATE * dt;
                        player_yaw = pole.yaw;
                        pole_place::at(pole, at);
                        for (int k = 0; k < 3; ++k) lt.translate[k] = at[k];
                        const char* hs = pole.hand >= 0 ? "RH_M" : "LH_M";
                        if (pole.let_go) {
                            pole.let_go = false;
                            pole.state = pole_ctl::PUSHOFF;
                            player->stance = 0;
                            play_once(sc, *player, std::string("VerticalRopePushOff") + hs, game_time, pole.yaw, false);
                        } else if (in_y > 0.5f && pole_can::move(pole, pt::CLIMB_STEP)) {
                            pole.state = pole_ctl::CLIMB;
                            player->stance = 0;
                            play_once(sc, *player, std::string("VerticalRopeClimbUp") + hs, game_time, pole.yaw, false);
                        } else if (in_y < -0.5f && pole_can::move(pole, -pt::SLIDE_STEP)) {
                            pole.state = pole_ctl::SLIDE_START;
                            player->stance = 0;
                            play_once(sc, *player, std::string("VerticalRopeStartSlideDown") + other, game_time, pole.yaw, false);
                        }
                    }
                }
                (void)side;
            }
            // Action traps (levers, cranes, the cart, the crusher, Assan's
            // mouth): the action input inside one plays its stages - reach,
            // use, finish - each a Prince skill and a script, in turn.
            bool action_started = false;
            if (act_pressed && !scripted_now && !on_ledge && !in_jump && !in_swim && !dead &&
                ledges.state == hang_ctl::NONE && crates.state == crate_ctl::NONE &&
                crouch.state == crouch_ctl::NONE && !working.trap) {
                for (size_t i = 0; i < sc.actions.size(); ++i) {
                    k2::engine::action_trap* a = sc.actions[i];
                    if (!a->contains(lt.translate) || !a->can_act()) continue;
                    working.trap = a;
                    working.stage = 0;
                    const float* c = a->center();
                    player_yaw = std::atan2(c[0] - lt.translate[0], c[2] - lt.translate[2]);
                    action_started = true;
                    std::printf("action: %s (state %s)\n", a->name().c_str(), a->state().c_str());
                    break;
                }
            }
            if (working.trap && player->scripted == 0) {
                typedef k2::engine::action_trap at;
                static const at::field skills[3] = { at::PRINCE_REACH, at::PRINCE_USE, at::PRINCE_FINISH };
                static const at::field scripts[3] = { at::ACTION_REACH, at::ACTION_USE, at::ACTION_FINISH };
                // The next stage with something in it; each stage's skill
                // plays to its end before the next begins.
                while (working.stage < 3) {
                    const int st = working.stage++;
                    if (st == 2) working.trap->run(at::DO_IT);
                    const std::string skill = working.trap->pick(skills[st]);
                    working.trap->run(scripts[st]);
                    if (!skill.empty() && play_once(sc, *player, skill, game_time, player_yaw, false)) break;
                }
                if (working.stage >= 3 && player->scripted == 0) working.trap = 0;
            }
            // Action at a ledge he can get onto climbs it (StStart1MClimb).
            // At a crate, action alone grabs it to push or pull; action
            // while pushing toward it climbs on top, when there is room.
            if (act_pressed && !action_started && !working.trap && !scripted_now && !on_ledge && !in_jump && !in_swim &&
                ledges.state == hang_ctl::NONE && crates.state == crate_ctl::NONE && !dead) {
                float ax[3];
                const float pfw[3] = { std::sin(player_yaw), 0.0f, std::cos(player_yaw) };
                if (push || !crate_in_front(sc, lt.translate, pfw, ax))
                    hang_try_climb(ledges, *player, sc.coll, lt.translate, player_yaw, game_time);
            }
            // C / pad B (sword away) crouches; held and moving, he crawls.
            const bool crouch_key = !snoozed && !dead && !combat.prince_drawn &&
                (down[plat::KEY_C] || (pad.connected && pad.button[plat::PAD_B]));
            const bool crouched = !scripted_now && !in_jump && !on_ledge && !in_swim &&
                ledges.state == hang_ctl::NONE && crates.state == crate_ctl::NONE &&
                crouch_update(crouch, sc, *player, sc.coll, lt.translate, player_yaw, crouch_key,
                              mv, ml, classic, in_x, in_y, game_time, dt);
            const bool busy = on_ledge || ledges.state != hang_ctl::NONE || in_swim || crouched ||
                              working.trap != 0 || bow.ready || pole.state != pole_ctl::NONE ||
                              bar.state != bar_ctl::NONE || rope.rope >= 0;
            const bool in_crate = !scripted_now && !in_jump && !busy &&
                                  update_crate(crates, sc, *player, lt.translate, player_yaw,
                                               game_time, act, push, pull);
            if (loco.flip && !player->scripted) {
                player_yaw += 3.14159265f;
                loco.flip = false;
            }
            // Height of the floor `ahead` units along his facing, from his
            // feet (the step selectors' probe, K2ActivityRun 0x49023F).
            struct floor_ahead { static float dh(const k2::engine::collision_world& w, const float* p, float yaw, float ahead) {
                const float q[3] = { p[0] + std::sin(yaw) * ahead, p[1] + 1.0f, p[2] + std::cos(yaw) * ahead };
                float gy;
                return ground_under(w, q, gy) ? gy - p[1] : -10.0f;
            } };
            bool loco_started = false;
            if (!scripted_now && !in_jump && !busy && !in_crate && !classic && !noclip && !dead) {
                const bool running_now = player->clip == LOCO_RUN && loco.run_since >= 0.0;
                if (ml > 1e-6f) {
                    float diff = std::atan2(mv[0], mv[2]) - player_yaw;
                    diff = std::fmod(diff + 3.14159265f, 6.2831853f);
                    if (diff < 0.0f) diff += 6.2831853f;
                    diff -= 3.14159265f;
                    const bool reverse = std::fabs(diff) > 2.6f;       // ~150 degrees
                    if (reverse && player->clip == LOCO_BASE && !combat.prince_drawn) {
                        // StStartQuickTurn: turn round where he stands.
                        loco_started = play_once(sc, *player, "QuickTurnaround_G", game_time, player_yaw, false);
                        loco.flip = loco_started;
                    } else if (reverse && running_now && game_time - loco.run_since > 0.3) {
                        // StStartMovingQuickTurn, only on a flat run ahead
                        // (Prince__CanRunningQuickTurn 0x4D18D9: 1 m and 2 m).
                        const float d1 = floor_ahead::dh(sc.coll, lt.translate, player_yaw, 1.0f);
                        const float d2 = floor_ahead::dh(sc.coll, lt.translate, player_yaw, 2.0f);
                        if (std::fabs(d1) <= 0.05f && std::fabs(d2) <= 0.05f) {
                            loco_started = play_once(sc, *player, "RunningQuickTurn_L", game_time, player_yaw, false);
                            loco.flip = loco_started;
                            loco.run_since = -1.0;
                        }
                    } else if (run && player->clip == LOCO_BASE && loco.run_since < 0.0 && !combat.prince_drawn) {
                        // The run's first step, up or down by the floor
                        // ahead (> 0.3 / < -0.3, fMaxRunNormalHeight).
                        player_yaw = std::atan2(mv[0], mv[2]);
                        const float dh = floor_ahead::dh(sc.coll, lt.translate, player_yaw, 1.0f);
                        const char* c = dh > 0.3f && dh < 0.65f ? "StartRunUpRight_L"
                                      : dh < -0.3f && dh > -0.65f ? "StartRunDownRight_L" : "StartRunRight_L";
                        loco_started = play_once(sc, *player, c, game_time, player_yaw, false);
                        if (loco_started) { set_clip(*player, LOCO_RUN); loco.run_since = game_time; }
                    }
                } else if (running_now && game_time - loco.run_since > 0.3) {
                    // StStartRunStop: one last step, on the foot that is down.
                    const float dh = floor_ahead::dh(sc.coll, lt.translate, player_yaw, 1.0f);
                    const double len = double(std::max(1u, clip_length_ms(player->loco[size_t(LOCO_RUN)])));
                    const bool left = std::fmod(player->clip_ms, len) < len * 0.5;
                    std::string c = "StepEndRun";
                    c += dh > 0.3f && dh < 0.65f ? "Up" : dh < -0.3f && dh > -0.65f ? "Down" : "";
                    c += left ? "Left_L" : "Right_L";
                    loco_started = play_once(sc, *player, c, game_time, player_yaw, false);
                    loco.run_since = -1.0;
                }
            }
            if (loco_started && std::getenv("K2_LOCODBG"))
                std::printf("loco: %.2f %s\n", game_time, player->scripted ? "transition" : "?");
            if (scripted_now || in_jump || busy || loco_started) {
                // A scripted skill, a jump or a ledge owns the body.
            } else if (in_crate) {
                crate_input(crates, sc, *player, lt.translate, player_yaw, game_time, act, push, pull);
            } else if (ml > 1e-6f) {
                player->stance = 0;
                // Walk_L or Running_L, moved at the speed its own root motion
                // covers, so the feet do not slide. Classic's back step plays
                // WalkBackwards_L1 and keeps his facing.
                const int clip = walk_back ? WALK_BACK : (run ? LOCO_RUN : LOCO_WALK);
                if (clip == LOCO_RUN && loco.run_since < 0.0) loco.run_since = game_time;
                if (clip != LOCO_RUN) loco.run_since = -1.0;
                set_clip(*player, clip);
                float speed = walk_speed * (run ? 2.5f : 1.0f);
                if (walk_back) speed = walk_speed * 0.6f;
                float fwd_model[3] = {0.0f, 0.0f, 1.0f};
                if (player->clip == clip) {
                    const assets::skl_file& s = player->loco[size_t(clip)];
                    const float cs = clip_speed(s);
                    if (cs > 0.0f) speed = cs;
                    float d[3];
                    if (!walk_back && clip_root_motion(s, d) && (d[0]*d[0] + d[2]*d[2]) > 1e-8f)
                        { fwd_model[0] = d[0]; fwd_model[2] = d[2]; }
                }
                // A partly pushed stick walks slower.
                const float amount = std::min(1.0f, ml);
                const float step = speed * dt * (run ? 1.0f : std::max(0.35f, amount));
                if (noclip) {
                    lt.translate[0] += mv[0] / ml * step;
                    lt.translate[2] += mv[2] / ml * step;
                } else {
                    move_horizontal(sc.coll, lt.translate, mv[0] / ml * step, mv[2] / ml * step);
                }
                // Modern: face the way he is going. set_yaw adds yaw to
                // atan2(x, z), hence the difference of two angles.
                if (!classic)
                    player_yaw = std::atan2(mv[0], mv[2]) - std::atan2(fwd_model[0], fwd_model[2]);
            } else if (!combat.prince_drawn) {
                player->stance = 0;
                set_clip(*player, LOCO_BASE);
                loco.run_since = -1.0;
            }
            // Stand on the floor, step up small ledges, fall off edges.
            if (!noclip && !scripted_now && !in_jump && !busy) {
                static const float k_down[3] = { 0.0f, -1.0f, 0.0f };
                k2::engine::pick_hit fall_probe;
                float gy;
                if (player_vy <= 0.0f && ground_under(sc.coll, lt.translate, gy)) {
                    lt.translate[1] = gy;
                    player_vy = 0.0f;
                } else if (player_vy == 0.0f && !in_swim && crates.state == crate_ctl::NONE &&
                           crouch.state == crouch_ctl::NONE && !sc.coll.pick(lt.translate, k_down, 1.0f, true, fall_probe)) {
                    // Off an edge with nothing within a unit below: a fall,
                    // as letting go of a ledge is - the fall clip, a landing,
                    // and fall damage from the height.
                    jumps.state = jump_ctl::AIR;
                    jumps.dir = 0;
                    jumps.vel[0] = jumps.vel[1] = jumps.vel[2] = 0.0f;
                    jumps.peak_y = lt.translate[1];
                    jump_begin(jumps, *player, FALL_VERT, game_time, lt.translate);
                } else {
                    player_vy -= move_tuning::GRAVITY * dt;
                    const float o[3] = { lt.translate[0], lt.translate[1], lt.translate[2] };
                    const float d[3] = { 0.0f, -1.0f, 0.0f };
                    const float drop = -player_vy * dt;
                    k2::engine::pick_hit h;
                    if (sc.coll.pick(o, d, drop, true, h) && h.normal[1] >= move_tuning::FLOOR_MIN_NY) {
                        lt.translate[1] = h.point[1];
                        player_vy = 0.0f;
                    } else {
                        lt.translate[1] -= drop;
                    }
                }
            }
            // Classic: the camera keeps behind him.
            if (classic && !input_locked) {
                const float want = 3.14159265f - player_yaw;
                float dyw = std::fmod(want - cam.yaw + 3.14159265f, 6.2831853f);
                if (dyw < 0) dyw += 6.2831853f;
                dyw -= 3.14159265f;
                cam.yaw += dyw * std::min(1.0f, dt * 3.0f);
            }
            lt.set_yaw(player_yaw);
            sc.root->update_world();
            {   // What he stands on now, for next frame's ride.
                ride.node = -1;
                const float o[3] = { lt.translate[0], lt.translate[1] + 0.5f, lt.translate[2] };
                const float dn[3] = { 0.0f, -1.0f, 0.0f };
                k2::engine::pick_hit h;
                if (jumps.state != jump_ctl::AIR && ledges.state == hang_ctl::NONE &&
                    sc.coll.pick(o, dn, 0.7f, true, h)) {
                    ride.node = sc.coll.triangle_node(h.triangle);
                    if (ride.node >= 0) {
                        for (int k = 0; k < 3; ++k) ride.at[k] = lt.translate[k];
                        sc.coll.node_to_local(ride.node, lt.translate, ride.local);
                    }
                }
            }

            // Holding a crate, the camera swings round behind the Prince so
            // the crate is in view ahead of him, not between him and the lens.
            if (crates.state != crate_ctl::NONE) {
                const float want = 3.14159265f - player_yaw;   // camera faces his way
                float dy = std::fmod(want - cam.yaw + 3.14159265f, 6.2831853f);
                if (dy < 0) dy += 6.2831853f;
                dy -= 3.14159265f;
                cam.yaw += dy * std::min(1.0f, dt * 4.0f);
            }

            // Chase camera: behind and above, looking slightly down at him.
            // Climbing and hanging move the body in the pose, not the root
            // (a hop rises in the clip and the root follows when it ends), so
            // the camera follows the body: the root plus how far the Prime
            // joint is above where it is standing in the base pose. Without
            // it the camera sank to the root under the ledge and its arm,
            // cast from inside the ledge, pulled it up to his legs.
            const k2::engine::transform& w = player->node->world();
            static float prime_base = -1e30f;
            if (prime_base < -1e29f) {
                float pb[3];
                prime_base = (player->loco_ok[LOCO_BASE] && clip_joint(*player, player->loco[LOCO_BASE], 0, 1, pb))
                             ? pb[1] : 0.0f;
            }
            const float lift = player->cur.size() > 1 ? player->cur[1].translate[1] - prime_base : 0.0f;
            const float body[3] = { w.translate[0], w.translate[1] + std::max(-2.0f, std::min(2.5f, lift)), w.translate[2] };
            cam.pos[0] = body[0] - std::sin(cam.yaw) * follow_dist;
            cam.pos[1] = body[1] + follow_height;
            cam.pos[2] = body[2] + std::cos(cam.yaw) * follow_dist;
            // Keep the camera on the Prince's side of any wall: cast the arm
            // from his head and stop short of the first surface it meets. A
            // stand-in until CameraLogic / camera.dat are reversed. Under a low
            // ceiling a lower arm often clears where the default one does not,
            // so a few heights are tried and the longest clear arm wins. The
            // crate being held is not an obstacle for the camera.
            if (!noclip) {
                const int32_t held = (crates.state != crate_ctl::NONE && crates.crate)
                                     ? crates.crate->coll_node() : -1;
                if (held >= 0) sc.coll.set_node_enabled(held, false);
                const float head[3] = { body[0], body[1] + 1.5f, body[2] };
                const float heights[3] = { follow_height, 1.5f, 0.9f };
                float best_pos[3] = { cam.pos[0], cam.pos[1], cam.pos[2] };
                float best_len = -1.0f;
                for (int hi = 0; hi < 3; ++hi) {
                    const float target[3] = { cam.pos[0], body[1] + heights[hi], cam.pos[2] };
                    const float arm[3] = { target[0] - head[0], target[1] - head[1], target[2] - head[2] };
                    const float len = std::sqrt(arm[0]*arm[0] + arm[1]*arm[1] + arm[2]*arm[2]);
                    if (len < 1e-4f) continue;
                    float t = 1.0f;
                    k2::engine::pick_hit h;
                    if (sc.coll.pick(head, arm, 1.0f, false, h)) {
                        // Back off 0.25 units from the hit, but never closer
                        // than half a unit to the head.
                        t = h.distance - 0.25f / len;
                        if (t < 0.5f / len) t = 0.5f / len;
                    }
                    // Prefer the higher arm unless a lower one is clearly longer.
                    if (t * len > best_len * 1.25f) {
                        best_len = t * len;
                        for (int k = 0; k < 3; ++k) best_pos[k] = head[k] + arm[k] * t;
                    }
                    if (t >= 1.0f) break;
                }
                for (int k = 0; k < 3; ++k) cam.pos[k] = best_pos[k];
                if (held >= 0 && crates.crate->collideable()) sc.coll.set_node_enabled(held, true);
            }
            // A scripted camera replaces the chase camera. The chase camera's
            // own yaw and pitch are left alone, so "camera,DD" returns to it.
            if (!noscript && camera_msgs.mode != camera_target::DEFAULT) {
                float at[3];
                if (camera_msgs.mode == camera_target::TRIPOD) {
                    for (int k = 0; k < 3; ++k) { cam.pos[k] = camera_msgs.pos[k]; at[k] = camera_msgs.look[k]; }
                } else if (camera_msgs.mode == camera_target::LOOK) {
                    for (int k = 0; k < 3; ++k) { cam.pos[k] = w.translate[k] + camera_msgs.pos[k]; at[k] = camera_msgs.look[k]; }
                } else {
                    const float* o = w.translate;
                    if (!camera_msgs.subject.empty())
                        for (size_t i = 0; i < sc.actors.size(); ++i)
                            if (sc.actors[i].node && equals_nocase(sc.actors[i].script_name, camera_msgs.subject.c_str())) {
                                o = sc.actors[i].node->world().translate;
                                break;
                            }
                    const bool fixed = camera_msgs.mode == camera_target::SECURITY;
                    for (int k = 0; k < 3; ++k) {
                        cam.pos[k] = fixed ? camera_msgs.pos[k] : o[k] + camera_msgs.pos[k];
                        at[k] = o[k] + camera_msgs.look[k];
                    }
                }
                const float d[3] = { at[0] - cam.pos[0], at[1] - cam.pos[1], at[2] - cam.pos[2] };
                const float dl = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
                if (dl > 1e-5f) {
                    // camera_basis: forward = (cos p sin y, sin p, -cos p cos y)
                    fwd[0] = d[0] / dl; fwd[1] = d[1] / dl; fwd[2] = d[2] / dl;
                    right[0] = -fwd[2]; right[1] = 0.0f; right[2] = fwd[0];
                    const float rl = std::sqrt(right[0]*right[0] + right[2]*right[2]);
                    if (rl > 1e-5f) { right[0] /= rl; right[2] /= rl; }
                }
            } else {
                camera_basis(cam, fwd, right);
            }
        } else {
            if (down[plat::KEY_E] || down[plat::KEY_SPACE]) mv[1] += 1.0f;
            if (down[plat::KEY_Q]) mv[1] -= 1.0f;
            const float ml = std::sqrt(mv[0]*mv[0] + mv[1]*mv[1] + mv[2]*mv[2]);
            if (ml > 1e-6f) {
                float sp = cam.speed * dt;
                if (down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT]) sp *= 4.0f;
                if (down[plat::KEY_LCTRL]  || down[plat::KEY_RCTRL])  sp *= 0.25f;
                for (int a = 0; a < 3; ++a) cam.pos[a] += mv[a] / ml * sp;
            }
        }

        // Every actor plays its clip; enemies and an undriven Prince stand in
        // BasePose_L. --pose and --noanim hold the pose instead, so the bind
        // pose and a single posed joint can still be checked.
        // Each actor touches only its own skeleton, so they are posed in
        // parallel across the cores; sound keys they cross play afterwards.
        // Each part's model matrix is worked out there too, so the draw loop
        // only submits.
        {
            struct pose_job {
                scene* sc;
                float dt;
                bool animate, skin;
                std::vector<std::vector<deferred_sound> > sounds;
                static void run(size_t i, void* u) {
                    pose_job& j = *static_cast<pose_job*>(u);
                    actor_inst& a = j.sc->actors[i];
                    if (j.animate && !a.scripted) animate_actor(a, j.dt, &j.sounds[i]);
                    actor_part_matrices(*j.sc, a, j.skin);
                }
            };
            static pose_job job;
            job.sc = &sc;
            job.dt = dt;
            job.animate = !pose_arg && !noanim;
            job.skin = !noskin;
            job.sounds.resize(sc.actors.size());
            for (size_t i = 0; i < job.sounds.size(); ++i) job.sounds[i].clear();
            k2::jobs::parallel_for(sc.actors.size(), &pose_job::run, &job);
            for (size_t i = 0; i < job.sounds.size(); ++i)
                for (size_t k = 0; k < job.sounds[i].size(); ++k)
                    play_sound(*job.sounds[i][k].name, job.sounds[i][k].at, 14.0f, false);
            // Solid props: each part's collision where it is drawn.
            for (size_t i = 0; i < sc.actors.size(); ++i) {
                const actor_inst& a = sc.actors[i];
                if (!a.is_fixture || a.part_mats.size() < a.item_count * 16) continue;
                for (size_t k = 0; k < a.item_count; ++k) {
                    const int32_t cn = sc.items[a.first_item + k].coll_node;
                    if (cn < 0) continue;
                    sc.coll.set_node_enabled(cn, a.collides && !a.hidden);
                    if (!a.collides || a.hidden) continue;
                    const float* m = &a.part_mats[k * 16];
                    const float rot[9] = { m[0], m[4], m[8], m[1], m[5], m[9], m[2], m[6], m[10] };
                    const float zero[3] = { 0.0f, 0.0f, 0.0f };
                    sc.coll.set_node_transform(cn, rot, zero, m + 12);
                }
            }
        }

        if (g_mixer) g_mixer->set_listener(cam.pos, right);

        const float at[3] = {cam.pos[0] + fwd[0], cam.pos[1] + fwd[1], cam.pos[2] + fwd[2]};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        mat4_look_at(view, cam.pos, at, up);

        gfx::begin_frame();
        gfx::clear(true, true);
        gfx::set_view_projection(view, proj);

        gfx::render_state rs;           // constructor gives every field a value
        rs.cull  = cull_mode_arg;       // --cull; see check_winding()
        rs.fog   = have_level && lv.fog.size() >= 2;
        if (rs.fog) {
            // The first column of a [Fog] row is a **fraction of the far clip
            // plane**, not a distance. Every shipped level writes 1.5 in the
            // second row, i.e. fog saturates half again beyond the far plane:
            //
            //   prisonfix  far 50   0.5 / 1.5  ->  25 .. 75
            //   cistern    far 85   0.8 / 1.5  ->  68 .. 128
            //   ruins      far 100  0.8 / 1.5  ->  80 .. 150
            //
            // Treating them as metres put fog end at 1.5 units and drowned the
            // whole scene in the fog colour, which is what "the level renders
            // almost nothing" turned out to be.
            const float far_clip = lv.has_clip_planes && lv.clip_far > 1.0f
                                 ? lv.clip_far : 100.0f;
            const assets::lvl_fog& f0 = lv.fog[0];
            const assets::lvl_fog& f1 = lv.fog[1];
            gfx::set_fog(f0.distance * far_clip, f1.distance * far_clip,
                         f1.r / 255.0f, f1.g / 255.0f, f1.b / 255.0f);
        }
        gfx::set_render_state(rs);

        // What the camera can see: room parts and actors outside the view
        // are not submitted at all.
        frustum view_frustum;
        {
            float vp[16];
            m4_mul(proj, view, vp);
            view_frustum.from(vp);
        }
        struct visible {
            static bool item(const frustum& f, const draw_item& di) {
                if (di.has_model || di.radius <= 0.0f) return true;
                const float c[3] = { di.center[0] + di.offset[0], di.center[1] + di.offset[1],
                                     di.center[2] + di.offset[2] };
                return f.sphere(c, di.radius);
            }
        };

        float model[16];
        mat4_identity(model);
        gfx::set_model(model);
        // Opaque first, writing depth; then transparent with blending on and
        // depth writes off, so alpha-mapped geometry (torch flames, water) does
        // not occlude what is behind it. Materials are flagged from
        // *ALPHA_SOURCE Opacity in the .K2A.
        for (size_t n = 0; n < sc.items.size(); ++n) {
            // --revdraw reverses submission order. With a working depth buffer
            // the frame must come out near-identical; if it does not, depth is
            // not deciding visibility and something else is.
            const size_t i = rev_draw ? sc.items.size() - 1 - n : n;
            if (sc.items[i].transparent || sc.items[i].is_trap || sc.items[i].cutout) continue;
            if (sc.items[i].owner >= 0 || sc.items[i].hidden) continue;
            if (!visible::item(view_frustum, sc.items[i])) continue;
            set_item_model(sc.items[i]);
            gfx::set_texture(item_texture(sc, sc.items[i], game_time));
            gfx::draw(sc.items[i].mesh);
        }
        // Colour-keyed geometry (prison bars): still opaque and depth-writing,
        // but texels the key made transparent are discarded.
        rs.alpha_test = true;
        rs.alpha_ref  = 0.5f;
        gfx::set_render_state(rs);
        for (size_t i = 0; i < sc.items.size(); ++i) {
            const draw_item& di = sc.items[i];
            if (!di.cutout || di.transparent || di.is_trap || di.owner >= 0 || di.hidden) continue;
            if (!visible::item(view_frustum, di)) continue;
            set_item_model(di);
            gfx::set_texture(item_texture(sc, di, game_time));
            gfx::draw(di.mesh);
        }
        rs.alpha_test = false;
        gfx::set_render_state(rs);
        gfx::set_model(model);

        // Actors: same material rules as room geometry, but each is drawn under
        // its own transform, which is what the room meshes never need. Stands in
        // for the AnimationNode the original attaches to the LevelGroup.
        if (!sc.actors.empty()) {
            rs.blend        = gfx::BLEND_NONE;
            rs.depth        = gfx::DEPTH_TEST_WRITE;
            rs.alpha_test   = false;
            rs.vertex_color = true;
            rs.wireframe    = false;
            rs.fog          = have_level && lv.fog.size() >= 2;
            gfx::set_render_state(rs);
            for (size_t a = 0; a < sc.actors.size(); ++a) {
                const actor_inst& ai = sc.actors[a];
                if (ai.hidden) continue;
                const k2::engine::transform& aw = ai.node->world();
                // A body fits in a few units about its node.
                const float ac[3] = { aw.translate[0], aw.translate[1] + 1.0f, aw.translate[2] };
                if (!view_frustum.sphere(ac, 3.0f)) continue;
                // actor_world * cur[j] * bind_inv[j], from the pose job.
                for (size_t k = 0; k < ai.item_count; ++k) {
                    const draw_item& di = sc.items[ai.first_item + k];
                    if (di.is_trap) continue;
                    if (ai.part_mats.size() < (k + 1) * 16) continue;
                    gfx::set_model(&ai.part_mats[k * 16]);
                    gfx::set_texture(di.tex);
                    gfx::draw(di.mesh);
                }
                // Carried objects: about their own centre, at the hand.
                if (!ai.carried.empty() && ai.carry_joint >= 0) {
                    k2::engine::transform hw;
                    k2::engine::transform_compose(aw, ai.cur[size_t(ai.carry_joint)], hw);
                    for (size_t k = 0; k < ai.carried.size(); ++k) {
                        const draw_item& cd = sc.items[ai.carried[k]];
                        k2::engine::transform t = hw, back, tt;
                        for (int q = 0; q < 9; ++q) back.rotate[q] = (q % 4 == 0) ? 1.0f : 0.0f;
                        for (int q = 0; q < 3; ++q) back.translate[q] = -cd.center[q];
                        back.scale = 1.0f;
                        k2::engine::transform_compose(t, back, tt);
                        float cm[16];
                        tt.to_matrix(cm);
                        gfx::set_model(cm);
                        gfx::set_texture(cd.tex);
                        gfx::draw(cd.mesh);
                    }
                }
                // The weapon, in the hand: actor * hand joint * grip offset.
                // The Prince's bow goes in the other hand.
                const bool bow_in_hand = ai.bow_shown && ai.alt_hand_joint >= 0;
                if ((ai.weapon_shown || bow_in_hand) && ai.hand_joint >= 0 && ai.skinned) {
                    std::map<std::string, std::vector<size_t> >::const_iterator wi =
                        sc.weapons.find(bow_in_hand ? std::string("LongBow") : ai.weapon);
                    if (wi != sc.weapons.end()) {
                        k2::engine::transform grip, t0, t1;
                        grip = ai.cur[size_t(bow_in_hand ? ai.alt_hand_joint : ai.hand_joint)];
                        k2::engine::transform off;
                        for (int k = 0; k < 9; ++k) off.rotate[k] = (k % 4 == 0) ? 1.0f : 0.0f;
                        for (int k = 0; k < 3; ++k) off.translate[k] = bow_in_hand ? ai.alt_hand_off[k] : ai.hand_off[k];
                        off.scale = 1.0f;
                        k2::engine::transform_compose(grip, off, t0);
                        k2::engine::transform_compose(aw, t0, t1);
                        float wm[16];
                        t1.to_matrix(wm);
                        gfx::set_model(wm);
                        for (size_t k = 0; k < wi->second.size(); ++k) {
                            const draw_item& wd = sc.items[wi->second[k]];
                            gfx::set_texture(wd.tex);
                            gfx::draw(wd.mesh);
                        }
                    }
                }
            }
            // Arrows in flight or stuck: OtherWeapons.K2A's "arrow", whose
            // shaft runs along its +y, turned onto the flight.
            std::map<std::string, std::vector<size_t> >::const_iterator arw = sc.weapons.find("arrow");
            if (arw != sc.weapons.end())
                for (size_t i = 0; i < g_arrows.size(); ++i) {
                    const arrow_shot& ar = g_arrows[i];
                    const float* f = ar.d;
                    float r[3] = { f[2], 0.0f, -f[0] };           // f x up
                    float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
                    if (rl < 1e-4f) { r[0] = 1.0f; r[2] = 0.0f; rl = 1.0f; }
                    r[0] /= rl; r[2] /= rl;
                    const float z[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
                    const float am[16] = { r[0], r[1], r[2], 0.0f,  f[0], f[1], f[2], 0.0f,
                                           z[0], z[1], z[2], 0.0f,  ar.p[0], ar.p[1], ar.p[2], 1.0f };
                    gfx::set_model(am);
                    for (size_t k = 0; k < arw->second.size(); ++k) {
                        const draw_item& wd = sc.items[arw->second[k]];
                        gfx::set_texture(wd.tex);
                        gfx::draw(wd.mesh);
                    }
                }
            gfx::set_model(model);   // room geometry is world-space; restore identity
        }

        // Transparent last of the lit passes: actors are opaque and must be in
        // the depth buffer before alpha surfaces are blended over them.
        rs.blend      = gfx::BLEND_ALPHA;
        rs.depth      = gfx::DEPTH_TEST;      // test, but do not write
        rs.alpha_test = true;                 // drop fully transparent texels
        rs.alpha_ref  = 0.02f;
        gfx::set_render_state(rs);
        // Back to front. These draws do not write depth, so without sorting a
        // distant alpha surface submitted later paints straight over nearer
        // geometry - "far polygons on top of near ones". Order is by distance
        // from the camera, farthest first.
        {
            static std::vector<std::pair<float, size_t> > order;
            order.clear();
            for (size_t i = 0; i < sc.items.size(); ++i) {
                const draw_item& di = sc.items[i];
                if (!di.transparent || di.is_trap || di.owner >= 0 || di.hidden) continue;
                if (!visible::item(view_frustum, di)) continue;
                const float dx = di.center[0] - cam.pos[0];
                const float dy = di.center[1] - cam.pos[1];
                const float dz = di.center[2] - cam.pos[2];
                order.push_back(std::make_pair(dx*dx + dy*dy + dz*dz, i));
            }
            std::sort(order.begin(), order.end());
            for (size_t k = order.size(); k-- > 0; ) {
                const draw_item& di = sc.items[order[k].second];
                set_item_model(di);
                gfx::set_texture(item_texture(sc, di, game_time));
                gfx::draw(di.mesh);
            }
            gfx::set_model(model);
        }

        // Trigger volumes last: coloured wireframe, one colour per trap type.
        // Depth-tested so they sit correctly in the room, but no depth write and
        // no alpha test, and untextured so only the tint shows. Toggle with T.
        if (show_traps) {
            rs.blend        = gfx::BLEND_ALPHA;
            rs.depth        = gfx::DEPTH_TEST;
            rs.alpha_test   = false;
            rs.vertex_color = false;    // volumes have junk vertex colours
            rs.wireframe    = true;
            rs.fog          = false;    // fog would wash the debug colours out
            gfx::set_render_state(rs);
            gfx::set_texture(gfx::INVALID_TEXTURE);
            for (size_t i = 0; i < sc.items.size(); ++i) {
                if (!sc.items[i].is_trap) continue;
                const float* c = sc.items[i].tint;
                gfx::set_tint(c[0], c[1], c[2], 0.9f);
                gfx::draw(sc.items[i].mesh);
            }
            gfx::set_tint(1.0f, 1.0f, 1.0f, 1.0f);
        }

        // Effects: after the world, before the HUD.
        fx_draw(sc, view, game_time);
        gfx::set_model(model);

        // HUD: the Prince's health as potion bottles along the bottom left,
        // as the game draws it (data/2D/health bottles, black keyed out).
        // One bottle holds 12 health (three thirds of 4); each shows full,
        // two thirds (the "half" art), a third or empty. The sword icon sits beside
        // them while it is drawn.
        {
            actor_inst* pa = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (is_prince(sc.actors[i])) { pa = &sc.actors[i]; break; }
            if (pa && follow_mode && !input_locked) {
                static gfx::mesh_id quad = 0;
                if (!quad) {
                    gfx::vertex v[4];
                    std::memset(v, 0, sizeof(v));
                    const float xy[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
                    for (int k = 0; k < 4; ++k) {
                        v[k].x = xy[k][0]; v[k].y = xy[k][1]; v[k].z = 0.0f;
                        v[k].u = xy[k][0]; v[k].v = 1.0f - xy[k][1];
                        v[k].nz = 1.0f; v[k].r = v[k].g = v[k].b = v[k].a = 255;
                    }
                    const uint16_t ix[6] = { 0, 1, 2, 0, 2, 3 };
                    quad = gfx::create_mesh(v, 4, ix, 6);
                }
                enum { B_FULL, B_HALF, B_THIRD, B_EMPTY, B_SWORD, B_ACT, B_AIR0, B_AIR1, B_AIR2, B_AIR3, B_AIR4, B_COUNT };
                static gfx::texture_id hud_tex[B_COUNT];
                static int hud_w[B_COUNT], hud_h[B_COUNT];
                static bool hud_loaded = false;
                if (!hud_loaded) {
                    hud_loaded = true;
                    const char* files[B_COUNT] = {
                        "2D/health bottles/redFull.BMP", "2D/health bottles/REDHALF.BMP",
                        "2D/health bottles/REDTHIRD.BMP", "2D/health bottles/empty.bmp",
                        "2D/interface icons/sword icon.bmp", 0,
                        "2D/air bottles/air0.bmp", "2D/air bottles/air1.bmp", "2D/air bottles/air2.bmp",
                        "2D/air bottles/air3.bmp", "2D/air bottles/air4.bmp" };
                    for (int k = 0; k < B_COUNT; ++k) {
                        hud_tex[k] = gfx::INVALID_TEXTURE;
                        hud_w[k] = hud_h[k] = 0;
                        if (!files[k]) continue;
                        size_t size = 0;
                        uint8_t* data = plat::fs_read(files[k], size);
                        if (!data) { std::printf("hud: %s missing\n", files[k]); continue; }
                        assets::image img;
                        if (assets::image_load(data, size, img) && img.valid()) {
                            for (size_t q = 0; q + 3 < img.rgba.size(); q += 4)
                                if (img.rgba[q] < 8 && img.rgba[q + 1] < 8 && img.rgba[q + 2] < 8)
                                    img.rgba[q + 3] = 0;
                            hud_tex[k] = gfx::create_texture(img.width, img.height, &img.rgba[0]);
                            hud_w[k] = img.width;
                            hud_h[k] = img.height;
                        }
                        plat::fs_free(data);
                    }
                    // The action prompt: the game ships no icon for it, so a
                    // key cap with an "E", drawn here. 32x32, 5x7 glyph x3.
                    static const char* glyph[7] = { "11111", "10000", "10000", "11110",
                                                    "10000", "10000", "11111" };
                    std::vector<uint8_t> px(32 * 32 * 4, 0);
                    for (int yy = 0; yy < 32; ++yy)
                        for (int xx = 0; xx < 32; ++xx) {
                            const float dx = xx - 15.5f, dy = yy - 15.5f;
                            const float r = std::sqrt(dx * dx + dy * dy);
                            uint8_t* q = &px[size_t(yy * 32 + xx) * 4];
                            if (r > 15.5f) continue;
                            const bool rim = r > 13.5f;
                            const int gx = (xx - 8) / 3, gy = (yy - 5) / 3;
                            const bool ink = xx >= 8 && gx < 5 && yy >= 5 && gy < 7 && glyph[gy][gx] == '1';
                            q[0] = rim || ink ? 235 : 40; q[1] = rim || ink ? 205 : 28; q[2] = rim || ink ? 130 : 18;
                            q[3] = rim || ink ? 255 : 190;
                        }
                    hud_tex[B_ACT] = gfx::create_texture(32, 32, &px[0]);
                    hud_w[B_ACT] = hud_h[B_ACT] = 32;
                }
                float ident[16];
                mat4_identity(ident);
                gfx::set_view_projection(ident, ident);
                gfx::render_state hs;
                hs.blend = gfx::BLEND_ALPHA;
                hs.depth = gfx::DEPTH_NONE;
                hs.alpha_test = false;
                hs.vertex_color = false;
                hs.wireframe = false;
                hs.fog = false;
                gfx::set_render_state(hs);
                gfx::set_tint(1.0f, 1.0f, 1.0f, 1.0f);
                // Art pixels to clip space, scaled with the window height
                // against the game's 480-line screen.
                const float px = float(h ? h : 1) / 480.0f;
                const float sx = 2.0f * px / float(w ? w : 1), sy = 2.0f * px / float(h ? h : 1);
                const float per = float(combat_tuning::BOTTLE);
                const int bottles = std::max(1, int(std::ceil(float(pa->max_health) / per)));
                float x = -1.0f + 12.0f * sx;
                const float y = -1.0f + 10.0f * sy;
                float m[16];
                mat4_identity(m);
                for (int b = 0; b < bottles; ++b) {
                    const float in = (float(pa->health) - float(b) * per) / per;
                    const int k = in >= 0.84f ? B_FULL : in >= 0.5f ? B_HALF : in > 0.0f ? B_THIRD : B_EMPTY;
                    if (hud_tex[k] == gfx::INVALID_TEXTURE) continue;
                    m[0] = float(hud_w[k]) * sx; m[5] = float(hud_h[k]) * sy; m[12] = x; m[13] = y;
                    gfx::set_model(m);
                    gfx::set_texture(hud_tex[k]);
                    gfx::draw(quad);
                    x += float(hud_w[k] + 2) * sx;
                }
                if (pa->armed && hud_tex[B_SWORD] != gfx::INVALID_TEXTURE) {
                    const float side = 40.0f;
                    m[0] = side * sx; m[5] = side * sy; m[12] = x + 8.0f * sx; m[13] = y;
                    gfx::set_model(m);
                    gfx::set_texture(hud_tex[B_SWORD]);
                    gfx::draw(quad);
                }
                // Under water: the air bottle, full to empty in four steps.
                if (swim.state == swim_ctl::UNDER) {
                    const int step = std::max(0, std::min(4, int(std::ceil(swim.air / swim_tuning::AIR * 4.0))));
                    const int k = B_AIR0 + step;
                    if (hud_tex[k] != gfx::INVALID_TEXTURE) {
                        m[0] = 40.0f * sx; m[5] = 40.0f * sy;
                        m[12] = x + (pa->armed ? 56.0f : 8.0f) * sx; m[13] = y;
                        gfx::set_model(m);
                        gfx::set_texture(hud_tex[k]);
                        gfx::draw(quad);
                    }
                }
                // Something to do here (talk, pull, use): the highlight icon
                // above the bottles, pulsing, until E is pressed.
                bool can_act = false;
                for (size_t i = 0; i < sc.areas.size() && !can_act; ++i) can_act = sc.areas[i]->can_act();
                if (player && !working.trap)
                    for (size_t i = 0; i < sc.actions.size() && !can_act; ++i)
                        can_act = sc.actions[i]->contains(player->node->local.translate) && sc.actions[i]->can_act();
                if (can_act && hud_tex[B_ACT] != gfx::INVALID_TEXTURE) {
                    const float side = 30.0f;
                    const float pulse = 0.7f + 0.3f * float(std::sin(game_time * 5.0));
                    m[0] = side * sx; m[5] = side * sy;
                    m[12] = -1.0f + 12.0f * sx; m[13] = y + 42.0f * sy;
                    gfx::set_model(m);
                    gfx::set_tint(1.0f, 1.0f, 1.0f, pulse);
                    gfx::set_texture(hud_tex[B_ACT]);
                    gfx::draw(quad);
                }
                gfx::set_texture(gfx::INVALID_TEXTURE);
                gfx::set_tint(1.0f, 1.0f, 1.0f, 1.0f);
                gfx::set_model(model);
            }
        }

        gfx::end_frame();

        plat::swap_buffers();

        // --shot: let a couple of frames settle, grab the buffer, quit.
        if (shot_path) {
            static int frames = 0;
            if (++frames >= shot_frame) {
                std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
                gfx::read_pixels(w, h, &px[0]);
                for (size_t a = 0; a < sc.actors.size(); ++a) {
                    if (!is_prince(sc.actors[a])) continue;
                    const k2::engine::transform& tw = sc.actors[a].node->world();
                    std::printf("player at (%.2f, %.2f, %.2f) yaw %.2f  camera (%.2f, %.2f, %.2f)  health %d\n",
                                tw.translate[0], tw.translate[1], tw.translate[2],
                                std::atan2(tw.rotate[2], tw.rotate[8]),
                                cam.pos[0], cam.pos[1], cam.pos[2], sc.actors[a].health);
                    break;
                }
                for (size_t i = 0; i < combat.enemies.size(); ++i) {
                    const enemy_ai& e = combat.enemies[i];
                    const actor_inst& ea = sc.actors[size_t(e.actor)];
                    std::printf("guard %-20s state %d at (%.2f, %.2f, %.2f) health %d%s\n",
                                ea.script_name.c_str(), int(e.state), ea.node->local.translate[0],
                                ea.node->local.translate[1], ea.node->local.translate[2], ea.health,
                                ea.scripted ? " (in a clip)" : "");
                }
                if (write_bmp(shot_path, w, h, &px[0]))
                    std::printf("wrote %s (%dx%d)\n", shot_path, w, h);
                else
                    std::fprintf(stderr, "k2boy: cannot write %s\n", shot_path);
                std::fflush(stdout);
                running = false;
            }
        }
    }

    // K2_WORLDDUMP=file: the level state a save would hold, for testing --world.
    if (const char* wd = std::getenv("K2_WORLDDUMP"))
        if (FILE* wf = std::fopen(wd, "w")) {
            const std::vector<std::string> w = world_capture(sc, combat, pickup_taken);
            for (size_t i = 0; i < w.size(); ++i) std::fprintf(wf, "%s\n", w[i].c_str());
            std::fclose(wf);
        }
    if (!sc.script.unhandled().empty()) {
        std::printf("scripts: messages not acted on (target,verb -> count)\n");
        for (std::map<std::string, int>::const_iterator it = sc.script.unhandled().begin();
             it != sc.script.unhandled().end(); ++it)
            std::printf("  %-48s %d\n", it->first.c_str(), it->second);
    }
    if (g_mixer) { plat::audio_close(); g_mixer = 0; }
    gfx::shutdown();
    plat::shutdown();
    return 0;
}
