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
#include "engine/mixer.h"
#include "engine/fight.h"
#include "assets/sfx.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
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

    std::printf("VFS indexed %zu files\n", plat::fs_file_count());
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
    float           tint[3];        // filled by assign_trap_colors once the scene is loaded
    bool            hidden;         // a trap made invisible (argv[0] or "visible:F")
    float           offset[3];      // a trap moved by script ("move,...")
    bool            has_model;      // a trap posed by a skill: draw under `model`
    float           model[16];
    int             material;       // index into scene::materials, -1 for none
    bool            cutout;         // colour-keyed: alpha-tested, still opaque
};

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

    actor_inst() : node(0), is_enemy(false), first_item(0), item_count(0),
                   skinned(false), clip(-1), clip_ms(0.0), clip_once(false), scripted(0), scripted_start(0),
                   scripted_yaw(0), remove_after(false), hidden(false), stance(0),
                   hold_last(false), posed_hold(false), health(100), max_health(100), armed(false) {
        scripted_origin[0] = scripted_origin[1] = scripted_origin[2] = 0.0f;
    }
};

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
    // Static room triangles, for the pick primitive (0x512183). Actor bodies
    // and trigger volumes are not in it.
    k2::engine::collision_world coll;
    // The trap message system: every trap node in the loaded rooms, by name.
    k2::engine::script_world               script;
    std::vector<k2::engine::trap_object*>  traps;   // owned
    std::vector<k2::engine::area_trap*>    areas;   // also in `traps`
    std::vector<k2::engine::pressure_plate*> plates; // also in `traps`
    // Every room node's position, by lower-case name, for scripts that name
    // places: "camera,tripod,(@IntroNISCam01)", "SkillAt:...,IntroNISPH".
    // Engine space. Target cameras also carry where they look.
    struct named_point { float pos[3]; bool has_target; float target[3]; float yaw; };
    std::map<std::string, named_point> points;
    // Pickup traps: position (bbox centre) and argv, for the Prince to collect.
    struct pickup_def { std::string name; std::vector<std::string> argv; float pos[3]; };
    std::vector<pickup_def> pickups;
    // Trap actors' skills, loaded on first use: "actor/skill" lower-case.
    std::map<std::string, assets::skl_file*> trap_skills;
    float mn[3];
    float mx[3];
    bool  empty;

    scene() : root(0), empty(true) { mn[0]=mn[1]=mn[2]=0; mx[0]=mx[1]=mx[2]=0; }
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
gfx::texture_id load_image_texture(const std::string& path, bool chroma, bool& has_alpha,
                                   int& decoded, int& failed) {
    size_t size = 0;
    uint8_t* data = plat::fs_read_basename(path.c_str(), size);
    if (!data) { ++failed; return gfx::INVALID_TEXTURE; }

    assets::image img;
    std::string err;
    const bool ok = assets::image_load(data, size, img, &err);
    plat::fs_free(data);
    if (!ok || !img.valid()) { ++failed; return gfx::INVALID_TEXTURE; }

    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        if (chroma && img.rgba[i] == 0 && img.rgba[i + 1] == 0 && img.rgba[i + 2] == 0)
            img.rgba[i + 3] = 0;
        if (img.rgba[i + 3] != 255) has_alpha = true;
    }
    ++decoded;
    return gfx::create_texture(img.width, img.height, &img.rgba[0]);
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
    }
    return len;
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
void fire_sound_keys(const assets::skl_file& s, double from_ms, double to_ms, const float* at) {
    for (size_t i = 0; i < s.sound_keys.size(); ++i) {
        const double t = s.sound_keys[i].time;
        if (t > from_ms && t <= to_ms) play_sound(s.sound_keys[i].sound, at, 14.0f, false);
        else if (from_ms == 0.0 && t == 0.0 && to_ms > 0.0) play_sound(s.sound_keys[i].sound, at, 14.0f, false);
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
void animate_actor(actor_inst& a, float dt) {
    if (!a.skinned || a.posed_hold) return;
    if (a.clip < 0 && !a.stance) return;
    const assets::skl_file& s = a.stance ? *a.stance : a.loco[size_t(a.clip)];
    const uint32_t len = clip_length_ms(s);
    const double before = a.clip_ms;
    a.clip_ms += double(dt) * 1000.0;
    if (!s.sound_keys.empty() && a.node) {
        const float* at = a.node->local.translate;
        if (len && !a.clip_once && a.clip_ms >= double(len)) {
            fire_sound_keys(s, before, double(len), at);       // the end of this cycle
            fire_sound_keys(s, -1.0, a.clip_ms - double(len), at);
        } else {
            fire_sound_keys(s, before, a.clip_ms, at);
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
    pose_actor(a);
}

// The .act's directory inside the VFS, e.g. "actors/prince/". Skills live in its
// "skills" subdirectory. Found by matching the .act's basename against every
// indexed .act path, because the master.dat names it by basename only.
std::string actor_dir_for(const std::string& act_name) {
    std::string base = act_name;
    const size_t cut = base.find_last_of("/\\");
    if (cut != std::string::npos) base = base.substr(cut + 1);
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] >= 'A' && base[i] <= 'Z') base[i] = char(base[i] - 'A' + 'a');

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
    const std::string dir = actor_dir_for(act_name);
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
    inst.skinned = bound > 0;
    if (inst.skinned) {
        load_loco_clips(act_path, inst);
        set_clip(inst, LOCO_BASE);
    }
    return inst.skinned;
}

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
    sc.points.clear();
    sc.pickups.clear();
    sc.coll.clear();
    sc.script.clear();
    for (size_t i = 0; i < sc.traps.size(); ++i) delete sc.traps[i];
    sc.traps.clear();
    sc.areas.clear();
    sc.plates.clear();
    for (std::map<std::string, assets::skl_file*>::iterator it = sc.trap_skills.begin();
         it != sc.trap_skills.end(); ++it) delete it->second;
    sc.trap_skills.clear();
    sc.empty = true;
    sc.mn[0] = sc.mn[1] = sc.mn[2] =  1e30f;
    sc.mx[0] = sc.mx[1] = sc.mx[2] = -1e30f;
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
    }

    const std::string type;     // the K2_TRAP_NAME: "Crate", "Force", ...
    float             box[6];   // K2_TRAP_BBOX at rest, engine space
    std::string       actor;    // whose skills a message can name (Door: argv1)
    float             pivot[3]; // K2_TRAP_PIVOT: a skill's rotation is about this

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
        if (msg.size() > 5 && (msg.compare(0, 5, "move,") == 0 || msg.compare(0, 5, "Move,") == 0)) {
            k2::engine::translate_motion m;
            if (!m.parse(msg.substr(5))) return false;
            m.begin(m_world.now());
            m_motion = m;
            m_moving = true;
            return true;
        }
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
        // A Door plays the skill its message names ("openslow", "pivot90");
        // the original reports "Doors can't handle message %s" otherwise.
        if (!actor.empty()) {
            const assets::skl_file* sk = find_skill(msg);
            if (sk) { m_skill = sk; m_skill_start = m_world.now(); return true; }
        }
        return trap_object::handle(msg, sender);
    }

    void update(double now) {
        if (m_skill) pose_from_skill(now);
        if (!m_moving) return;
        const double f = m_motion.fraction(now);
        float off[3];
        for (int k = 0; k < 3; ++k) off[k] = m_base[k] + float(m_motion.dir[k] * f);
        apply(off);
        if (m_motion.done(now)) {
            for (int k = 0; k < 3; ++k) m_base[k] = off[k];
            m_moving = false;
        }
    }

private:
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
            m_sc.coll.set_node_transform(m_node, r.rotate, pivot, off);
            if (m_item >= 0) {
                draw_item& di = m_sc.items[size_t(m_item)];
                // world = R (p - pivot) + pivot + off, as a column-major 4x4.
                float* m = di.model;
                for (int c = 0; c < 3; ++c)
                    for (int rr = 0; rr < 3; ++rr) m[c * 4 + rr] = r.rotate[rr * 3 + c];
                m[3] = m[7] = m[11] = 0.0f;
                for (int rr = 0; rr < 3; ++rr) {
                    const float rp = r.rotate[rr * 3] * pivot[0] + r.rotate[rr * 3 + 1] * pivot[1]
                                   + r.rotate[rr * 3 + 2] * pivot[2];
                    m[12 + rr] = pivot[rr] - rp + off[rr];
                }
                m[15] = 1.0f;
                di.has_model = true;
            }
            return;
        }
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
};

// An actor as a script target, under the name of the node that spawned it
// ("prince" for the Prince). Understood:
//   snooze:t                         no player input for t seconds (the Prince)
//   SkillAt:skill[,placeholder[,REMOVE]]
//                                    play one of its skills once; with a
//                                    placeholder, from that node's position
//                                    and facing; REMOVE hides it afterwards
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
        return false;
    }

    double snooze_until;

private:
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
        a.remove_after = f.size() > 2 && (f[2] == "REMOVE" || f[2] == "remove");
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
        if (!s.joints[j].present) { a.joint_local_cur[j] = a.joint_local_bind[j]; continue; }
        float pos[3], q[4];
        assets::skl_sample(s.joints[j].path, t, s.interpolate != 0, pos, q);
        if (j == 0)
            for (int k = 0; k < 3; ++k) pos[k] = a.joint_local_bind[0].translate[k];
        transform_from_pos_quat(pos, q, a.joint_local_cur[j]);
    }
    pose_actor(a);

    float p0[3], pt[3], q[4];
    p0[0] = p0[1] = p0[2] = pt[0] = pt[1] = pt[2] = 0.0f;
    if (!s.joints.empty() && s.joints[0].present) {
        assets::skl_sample(s.joints[0].path, 0, s.interpolate != 0, p0, q);
        assets::skl_sample(s.joints[0].path, t, s.interpolate != 0, pt, q);
    }
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
        if (!assets::skl_parse(d, size, *sk) || sk->chunk_tag != "SklS") { delete sk; sk = 0; }
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
//
// Trailing letters ("X") are flags the original keeps (0x409975, |5); the
// transition they select is not modelled - every change is a cut.
class camera_target : public k2::engine::script_target {
public:
    enum mode_t { DEFAULT, TRIPOD, FOLLOW };

    explicit camera_target(const scene& sc) : script_target("camera"), mode(DEFAULT), m_sc(sc) {
        for (int k = 0; k < 3; ++k) pos[k] = look[k] = 0.0f;
    }

    bool receive(const std::string& msg, k2::engine::script_target*) {
        if (msg.empty()) return false;
        if (msg[0] == 'D' || msg[0] == 'd') { mode = DEFAULT; return true; }
        const size_t comma = msg.find(',');
        if (comma == std::string::npos) return false;
        std::string verb = msg.substr(0, comma);
        for (size_t i = 0; i < verb.size(); ++i)
            if (verb[i] >= 'A' && verb[i] <= 'Z') verb[i] = char(verb[i] - 'A' + 'a');
        const std::string args = msg.substr(comma + 1);
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
        if (verb.compare(0, 3, "fol") == 0) {
            if (!have_b) return false;
            for (int k = 0; k < 3; ++k) { pos[k] = a[k]; look[k] = b[k]; }
            mode = FOLLOW;
            return true;
        }
        return false;
    }

    mode_t mode;
    float  pos[3];      // TRIPOD: where; FOLLOW: offset from the Prince
    float  look[3];     // TRIPOD: at what; FOLLOW: direction

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
        m_sound = argv.size() > 3 ? argv[3] : std::string();
        for (int k = 0; k < 3; ++k) m_pos[k] = (bbox[k] + bbox[k + 3]) * 0.5f;
        const float dx = bbox[3] - bbox[0], dz = bbox[5] - bbox[2];
        m_radius = use_radius ? std::max(4.0f, 0.5f * std::sqrt(dx * dx + dz * dz)) * 2.0f : 16.0f;
    }

    void start_ambient() {
        if (m_sound.empty() || !ensure_sound(m_sound)) return;
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
        m_voice = play_sound(msg, m_positional ? m_pos : 0, m_radius, true);
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

bool append_scene(const char* path, scene& sc, bool collide = true) {
    size_t size = 0;
    uint8_t* data = plat::fs_read_basename(path, size);
    if (!data) data = plat::fs_read(path, size);
    if (!data) return false;

    assets::k2a_file f;
    std::string err;
    const bool ok = assets::k2a_parse(data, size, f, &err);
    plat::fs_free(data);
    if (!ok) return false;

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
        if (n.trap.name == "Pickup" && collide) {
            scene::pickup_def pd;
            pd.name = n.name;
            pd.argv = n.trap.argv;
            for (int k = 0; k < 3; ++k) pd.pos[k] = (n.trap.bbox[k] + n.trap.bbox[k + 3]) * 0.5f;
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

        // What the floor and wall picks (mask 6) see, per 0x449546 and
        // 0x474525: room geometry minus its alpha leaves, plus any Generic
        // trap that is Collideable, visible or not. For other trap types with
        // real geometry (doors, crates) the same flag is applied; how the
        // original picks those is not traced. Trigger volumes never collide.
        bool solid;
        if (!n.has_trap)                 solid = !it.transparent;
        else if (n.trap.name == "Generic") solid = hdr.collideable;
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
            } else if (n.trap.name == "Audio") {
                t = new audio_trap(sc.script, n.name, hdr, n.trap.argv, n.trap.bbox);
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
                if (n.trap.name == "Door")
                    st->actor = (n.trap.argv.size() > 1 && !n.trap.argv[1].empty())
                                ? n.trap.argv[1] : std::string("TRAP_Door");
                t = st;
            }
            sc.traps.push_back(t);
            // A spawn trap's messages are its actor's (EnemyStart__ctor knows
            // "SkillAt"); the actor answers under this name instead.
            if (n.trap.name != "EnemyStart" && n.trap.name != "PrinceStart")
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

void spawn_level_actors(scene& sc, k2::engine::body_table& bodies,
                        std::vector<k2::engine::actor_spawn>& spawns) {
    for (size_t i = 0; i < sc.spawn_points.size(); ++i) {
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
        const int32_t id = k2::engine::spawn_actor(
            bodies, spawns, is_enemy, body,
            t.center[0], t.center[1], t.center[2], t.pivot_angle,
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
        an->local.translate[0] = t.center[0];
        an->local.translate[1] = t.center[1];
        an->local.translate[2] = t.center[2];
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
        sc.actors.push_back(inst);

        sc.root->update_world();
        std::printf("  spawned %-14s body %d (%s) at (%.1f, %.1f, %.1f) yaw %.2f  %zu meshes"
                    "  local x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
                    inst.name.c_str(), body, b->name.c_str(),
                    an->local.translate[0], an->local.translate[1],
                    an->local.translate[2], t.pivot_angle, inst.item_count,
                    lmn[0], lmx[0], lmn[1], lmx[1], lmn[2], lmx[2]);
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

    int loaded = 0, missing = 0;
    for (size_t r = 0; r < lv.regions.size(); ++r) {
        if (region >= 0 && int(r) != region) continue;
        const assets::lvl_region& reg = lv.regions[r];
        for (size_t i = 0; i < reg.rooms.size(); ++i) {
            const std::string file = assets::lvl_room_filename(reg.rooms[i]);
            if (append_scene(file.c_str(), sc)) ++loaded;
            else { ++missing; std::printf("  missing room %s\n", file.c_str()); }
        }
    }
    std::printf("  region %s: rooms loaded %d, missing %d\n",
                region < 0 ? "all" : "selected", loaded, missing);
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
        if (!sc.actors[i].is_enemy) { start = int(i); break; }
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

// Moves the body by (dx, dz), sliding along walls. Each probe height casts
// along the move from the body's centre; a hit inside RADIUS of the new
// position removes the component of the move that points into the wall.
void move_horizontal(const k2::engine::collision_world& w, float* p, float dx, float dz) {
    for (int pass = 0; pass < 2; ++pass) {
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-6f) return;
        const float d[3] = { dx / len, 0.0f, dz / len };
        bool blocked = false;
        for (int k = 0; k < 3 && !blocked; ++k) {
            const float o[3] = { p[0], p[1] + move_tuning::PROBE_HEIGHTS[k], p[2] };
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
        cc.state = crate_ctl::ANTIC;
        cc.begin(a, CRATE_ANTIC, now, pos, false);
        std::printf("crate: grab %s\n", t->name().c_str());
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
        const bool ok = crate_move_ok(sc.coll, pos, fwd, mv);
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
namespace jump_tuning {
const float GRAVITY = -17.0f;           // FallGravity
const float MIN_FALL_HEIGHT = 6.25f;
}

struct jump_ctl {
    enum state_t { NONE, ANTIC, AIR, LAND } state;
    int    dir;            // 0 up, 1 forward, 2 back, 3 left, 4 right, 5 run
    double t0;
    float  origin[3];
    float  vel[3];
    float  peak_y;

    jump_ctl() : state(NONE), dir(0), t0(0), peak_y(0) {
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
        const int c = fell > jump_tuning::MIN_FALL_HEIGHT ? LAND_PAINFUL : land[jc.dir];
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
    float     grip[3];      // grip point in the Prince's frame (x left, y up, z fwd)
    bool      have_grip;

    hang_ctl() : state(NONE), clip(-1), t0(0), have_grip(false) {
        for (int k = 0; k < 3; ++k) from[k] = to[k] = grip[k] = 0.0f;
    }
};

// Where the grip joints sit in the hang pose, relative to the root: pose
// LedgeHangBase_M at t = 0 and read LGrip/RGrip. Cached in `hc`.
void hang_measure_grip(hang_ctl& hc, actor_inst& a) {
    if (hc.have_grip || !a.loco_ok[LEDGE_HANG]) return;
    const assets::skl_file& s = a.loco[LEDGE_HANG];
    std::vector<k2::engine::transform> keep = a.joint_local_cur;
    const size_t n = std::min(a.joint_local_cur.size(), s.joints.size());
    for (size_t j = 0; j < n; ++j) {
        if (!s.joints[j].present) continue;
        float pos[3], q[4];
        assets::skl_sample(s.joints[j].path, 0, true, pos, q);
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
    if (!cnt) return;
    for (int k = 0; k < 3; ++k) hc.grip[k] = sum[k] / float(cnt);
    hc.have_grip = true;
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

// From standing: climb a low ledge (HopUpOnto*). True when it started.
bool hang_try_climb(hang_ctl& hc, actor_inst& a, const k2::engine::collision_world& w,
                    const float* pos, float& yaw, double now) {
    if (!a.loco_ok[HOP_SMALL]) return false;
    ledge_hit l;
    if (!find_ledge(w, pos, yaw, 0.3f, 2.25f, l) || !l.can_stand) return false;
    const float dh = l.top - pos[1];
    const int c = dh > 1.75f ? HOP_2M : dh > 1.3f ? HOP_1_5M : dh > 0.9f ? HOP_1M : HOP_SMALL;
    hc.ledge = l;
    yaw = l.facing;
    const float ff[3] = { std::sin(yaw), 0.0f, std::cos(yaw) };
    for (int k = 0; k < 3; ++k) hc.from[k] = pos[k];
    hc.to[0] = l.edge[0] + ff[0] * 0.35f;
    hc.to[1] = l.top;
    hc.to[2] = l.edge[2] + ff[2] * 0.35f;
    hc.state = hang_ctl::CLIMB;
    hang_play(hc, a, c, now, true);
    std::printf("ledge: climb %.2f (%s)\n", dh, k_loco_names[c]);
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
    hc.ledge = l;
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
        // Up first, then over: the body rises through the first two thirds
        // of the clip and steps onto the top in the last third.
        const double up = std::min(1.0, u / 0.66);
        const double ov = u < 0.5 ? 0.0 : (u - 0.5) / 0.5;
        const double su = up * up * (3.0 - 2.0 * up);
        pos[1] = float(hc.from[1] + (hc.to[1] - hc.from[1]) * su);
        pos[0] = float(hc.from[0] + (hc.to[0] - hc.from[0]) * ov);
        pos[2] = float(hc.from[2] + (hc.to[2] - hc.from[2]) * ov);
        yaw = hc.ledge.facing;
        if (u >= 1.0) { hc.state = hang_ctl::NONE; set_clip(a, LOCO_BASE); return false; }
        return true;
    }
    case hang_ctl::GRAB:
    case hang_ctl::PULL_FAIL:
        hang_root(hc, pos);
        yaw = hc.ledge.facing;
        if (u >= 1.0) { hc.state = hang_ctl::HANG; hang_play(hc, a, LEDGE_HANG, now, false); }
        return true;
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
const int   HIT_DAMAGE    = 12;
const float GUARD_BLOCK   = 0.35f;  // chance a guard blocks the Prince
const double RESPAWN_TIME = 3.5;
}

enum attack_dir { DIR_LEFT = 0, DIR_CENTER, DIR_RIGHT };
const char* const k_dir_names[3] = { "Left", "Center", "Right" };

struct pending_hit {
    bool   active;
    double at;              // when it lands
    int    dir;
    int    attacker;        // actor index; -1 the Prince
};

struct enemy_ai {
    enum state_t { PATROL, CHARGE, FIGHT, RECOVER, DEAD } state;
    int    actor;
    float  home[3];
    std::vector<std::array<float, 3> > way;
    size_t wp;
    double next_attack;
    double busy_until;
    pending_hit hit;
    std::string set;        // clip suffix: "_G1"
    std::string hurt_set;   // "_L1"
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
        if (!a.is_enemy) {
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
        // EnemyStart argv: body, "difficulty,weapon,health", waypoints, ...
        for (size_t t = 0; t < sc.spawn_points.size(); ++t) {
            const assets::k2a_trap& sp = sc.spawn_points[t];
            if (sp.node != a.script_name) continue;
            if (sp.argv.size() > 1) a.max_health = a.health = 50 + 25 * std::atoi(sp.argv[1].c_str());
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

void prince_damage(combat_state& cs, scene& sc, actor_inst& p, int dmg, int dir, double now) {
    if (cs.dead_since >= 0.0) return;
    p.health -= dmg;
    play_sound("GBLpfiteinjur", p.node->local.translate, 14.0f, false);
    if (p.health <= 0) {
        p.health = 0;
        cs.dead_since = now;
        p.stance = 0;
        if (!play_once(sc, p, p.armed ? "CombatDeathGeneric_G2" : "DeathGeneric_G", now, actor_yaw(p), true))
            play_once(sc, p, "CombatDeathGeneric_G1", now, actor_yaw(p), true);
        std::printf("combat: the Prince falls\n");
        return;
    }
    play_once(sc, p, std::string("CombatBeingInjured") + k_dir_names[dir] + "_L2", now, actor_yaw(p), false);
    cs.prince_busy_until = now + skill_seconds(sc, p, std::string("CombatBeingInjured") + k_dir_names[dir] + "_L2");
}

void enemy_damage(combat_state& cs, scene& sc, enemy_ai& e, int dmg, int dir, double now) {
    actor_inst& a = sc.actors[size_t(e.actor)];
    a.health -= dmg;
    play_sound("gblswordswish", a.node->local.translate, 14.0f, false);
    if (a.health <= 0) {
        e.state = enemy_ai::DEAD;
        a.stance = 0;
        play_once(sc, a, "CombatDying" + e.set, now, actor_yaw(a), true);
        std::printf("combat: %s is defeated\n", a.script_name.c_str());
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

void enemy_update(combat_state& cs, scene& sc, enemy_ai& e, actor_inst& prince,
                  bool prince_available, double now, float dt) {
    actor_inst& a = sc.actors[size_t(e.actor)];
    if (e.state == enemy_ai::DEAD || a.hidden) return;
    const k2::engine::fight_params& fp = *cs.params;
    float* p = a.node->local.translate;
    const float* pp = prince.node->local.translate;
    const float dx = pp[0] - p[0], dz = pp[2] - p[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    const bool same_floor = std::fabs(pp[1] - p[1]) < 1.5f;

    // A swing in flight lands at its time, blocked or not.
    if (e.hit.active && now >= e.hit.at) {
        e.hit.active = false;
        const bool in_front = angle_diff(yaw_toward(p, pp), actor_yaw(a)) < 1.2f;
        if (prince_available && same_floor && d <= combat_tuning::REACH && in_front) {
            if (cs.prince_blocking) {
                play_sound("gblswordclank", p, 14.0f, false);
                const std::string rec = std::string("CombatBlocked") + k_dir_names[e.hit.dir] + e.set;
                play_once(sc, a, rec, now, actor_yaw(a), false);
                e.busy_until = now + std::max(skill_seconds(sc, a, rec),
                    fp.fight(std::string("Sword Blocked ") + k_dir_names[e.hit.dir] + " Attack Recover", 0.37));
            } else {
                prince_damage(cs, sc, prince, combat_tuning::HIT_DAMAGE, e.hit.dir, now);
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
    const bool  sees = prince_available && same_floor && d < watch &&
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
            else { a.stance = 0; set_clip(a, LOCO_BASE); }
        }
        break;
    case enemy_ai::CHARGE:
        if (!prince_available || d > watch * 1.5f) { e.state = enemy_ai::PATROL; break; }
        if (d <= combat_tuning::FIGHT_DIST) {
            e.state = enemy_ai::FIGHT;
            e.next_attack = now + 0.6;
            break;
        }
        steer(sc, a, pp, true, dt);
        break;
    case enemy_ai::FIGHT:
    case enemy_ai::RECOVER: {
        if (!prince_available) { e.state = enemy_ai::PATROL; a.stance = 0; break; }
        if (d > combat_tuning::FIGHT_DIST * 1.6f) { e.state = enemy_ai::CHARGE; break; }
        a.node->local.set_yaw(std::atan2(dx, dz));
        a.stance = find_actor_skill(sc, a, "CombatAtReady" + e.set);
        if (now >= e.next_attack) {
            const int dir = std::rand() % 3;
            const std::string atk = std::string("CombatAttack") + k_dir_names[dir] + e.set;
            if (play_once(sc, a, atk, now, actor_yaw(a), false)) {
                e.hit.active = true;
                e.hit.dir = dir;
                e.hit.at = now + fp.fight(std::string("Sword ") + k_dir_names[dir] + " Attack Begin", 0.3)
                               + fp.fight("Unblocked Attack", 0.0667);
                e.busy_until = now + skill_seconds(sc, a, atk);
            }
            e.next_attack = now + 1.4 + double(std::rand() % 1000) / 1000.0 * 1.2;
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
        if (e.state == enemy_ai::DEAD) continue;
        const float* q = sc.actors[size_t(e.actor)].node->local.translate;
        const float dx = q[0] - pp[0], dz = q[2] - pp[2];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > best_d || std::fabs(q[1] - pp[1]) > 1.5f) continue;
        if (angle_diff(std::atan2(dx, dz), actor_yaw(p)) > 1.2f) continue;
        best = &e;
        best_d = d;
    }
    if (!best) return;
    actor_inst& a = sc.actors[size_t(best->actor)];
    const bool can_block = (best->state == enemy_ai::FIGHT) && !a.scripted && now >= best->busy_until;
    if (can_block && float(std::rand() % 1000) / 1000.0f < combat_tuning::GUARD_BLOCK) {
        play_sound("gblswordclank", a.node->local.translate, 14.0f, false);
        play_once(sc, a, std::string("CombatBlock") + k_dir_names[cs.prince_hit.dir] + best->set,
                  now, actor_yaw(a), false);
        best->busy_until = now + 0.5;
        return;
    }
    enemy_damage(cs, sc, *best, 25, cs.prince_hit.dir, now);
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

int main(int argc, char** argv) {
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
    bool noscript = false;
    bool autopush = false;
    bool autojump = false;
    bool classic  = false;
    bool mute     = false;
    bool start_armed = false;
    bool sounds_only = false;
    bool find_ledges = false;
    struct key_hold { std::string keys; double from, to; };
    std::vector<key_hold> holds;                           // --hold KEYS@T0[-T1]
    std::vector<std::string> send_args;
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
        else if (std::strcmp(argv[i], "--autopush") == 0) autopush = true;
        else if (std::strcmp(argv[i], "--autojump") == 0) autojump = true;
        else if (std::strcmp(argv[i], "--findledges") == 0) find_ledges = true;
        else if (std::strcmp(argv[i], "--mute") == 0) mute = true;
        else if (std::strcmp(argv[i], "--armed") == 0) start_armed = true;
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

    if (!plat::fs_mount(data_root)) {
        std::fprintf(stderr,
                     "k2boy: could not mount data root '%s'\n"
                     "usage: k2boy [--verify] [path-to-retail-install]\n",
                     data_root);
        return 1;
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
        for (size_t k = 0; k < sk.joint_keys.size() && k < 3; ++k) {
            const assets::skl_joint_key& j = sk.joint_keys[k];
            std::printf("    key t=%-6u ease %.4f/%.4f\n", j.time, j.ease_in, j.ease_out);
            for (int t = 0; t < 2; ++t)
                std::printf("      xf%d pos=(%7.3f,%7.3f,%7.3f) quat=(%7.4f,%7.4f,%7.4f,%7.4f)\n",
                            t, j.xf[t].pos[0], j.xf[t].pos[1], j.xf[t].pos[2],
                            j.xf[t].quat[0], j.xf[t].quat[1], j.xf[t].quat[2], j.xf[t].quat[3]);
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

    if (!gfx::init(plat::gl_get_proc_address)) {
        std::fprintf(stderr, "k2boy: gfx init failed\n");
        plat::shutdown();
        return 1;
    }

    int32_t w = 0, h = 0;
    plat::drawable_size(w, h);
    gfx::resize(w, h);

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
            if (!sc.actors[i].is_enemy) {
                player_yaw = std::atan2(sc.actors[i].node->local.rotate[2],
                                        sc.actors[i].node->local.rotate[8]);
                break;
            }
        (void)pi;
        cam.pitch = -0.22f;
    }
    // --place x,y,z,yaw puts the Prince somewhere for a test; yaw in degrees,
    // 0 facing +z.
    if (place_arg) {
        float v[4] = {0,0,0,0};
        if (std::sscanf(place_arg, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) >= 3)
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].is_enemy) {
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
            if (sc.actors[i].is_enemy) continue;
            float gy;
            const float* p = sc.actors[i].node->local.translate;
            if (ground_under(sc.coll, p, gy))
                std::printf("  ground under the Prince at y %.3f (spawn y %.3f)\n", gy, p[1]);
            else
                std::printf("  no ground under the Prince within reach (spawn y %.3f)\n", p[1]);
        }
    }
    // --findledges: list standing spots with a ledge ahead, for testing.
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
    audio_target audio_msgs;
    sc.script.add(&audio_msgs);
    if (!shot_path && !mute) {
        g_mixer = &game_mixer;
        if (!plat::audio_open(44100, audio_callback, 0)) {
            std::printf("audio: no output device\n");
            g_mixer = 0;
        }
    }
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
        if (!sc.actors[i].is_enemy) prince_ptr = t;
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
            if (!sc.actors[i].is_enemy) sc.actors[i].armed = true;
    std::vector<bool> pickup_taken(sc.pickups.size(), false);
    std::printf("combat: %zu fight.ini values, %zu enemy.dat values, %zu guards, %zu pickups\n",
                fight_tuning.fight_count(), fight_tuning.enemy_count(), combat.enemies.size(),
                sc.pickups.size());
    camera_target camera_msgs(sc);
    sc.script.add(&camera_msgs);
    // While a script holds the Prince (snooze) the player has no control at
    // all, camera included: that is a cutscene.
    bool input_locked = false;
    sc.script.trace = trapmsg;
    // --send "target,msg" delivers a message at start, as if a trap sent it.
    for (size_t i = 0; i < send_args.size(); ++i) sc.script.send_item(send_args[i], 0);
    double game_time = 0.0;
    crate_ctl crates;
    jump_ctl  jumps;
    hang_ctl  ledges;
    bool      draw_was_down = false, atk_was_down = false;
    bool      jump_was_down = false;
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
    while (running) {
        plat::event e;
        while (plat::poll_event(e)) {
            switch (e.type) {
                case plat::EVENT_QUIT:
                    running = false;
                    break;
                case plat::EVENT_KEY_DOWN:
                    if (e.k == plat::KEY_ESCAPE) running = false;
                    if (e.k == plat::KEY_R) frame_scene(cam, sc);
                    if (e.k == plat::KEY_V && !input_locked) {
                        follow_mode = !follow_mode;
                        std::printf("camera: %s\n", follow_mode ? "follow" : "free-fly");
                        std::fflush(stdout);
                    }
                    if (e.k == plat::KEY_C) {
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
                    if (e.button == plat::MOUSE_LEFT) looking = true;
                    break;
                case plat::EVENT_MOUSE_UP:
                    if (e.button == plat::MOUSE_LEFT) looking = false;
                    break;
                case plat::EVENT_MOUSE_MOVE:
                    if (looking && !input_locked) {
                        const float sens = 0.005f;
                        cam.yaw   += float(e.mouse_dx) * sens;
                        cam.pitch += float(e.mouse_dy) * sens;
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
            const float* pp = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].is_enemy) { pp = sc.actors[i].node->local.translate; break; }
            const bool action = follow_mode && down[plat::KEY_E];
            for (size_t i = 0; i < sc.areas.size(); ++i)
                sc.areas[i]->update(game_time, pp && sc.areas[i]->contains(pp), action);
            for (size_t i = 0; i < sc.plates.size(); ++i)
                sc.plates[i]->update(game_time, pp && sc.plates[i]->contains(pp));
            // Scripted skills ("SkillAt:") drive their actors outright.
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (update_scripted(sc.actors[i], game_time) && !sc.actors[i].is_enemy)
                    player_yaw = sc.actors[i].scripted_yaw;

            // Combat: guards, swings landing, and a fallen Prince coming back.
            actor_inst* prince_a = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].is_enemy) { prince_a = &sc.actors[i]; break; }
            if (prince_a) {
                const bool snoozing = game_time < (prince_msgs ? prince_msgs->snooze_until : 0.0);
                const bool available = !snoozing && combat.dead_since < 0.0;
                for (size_t i = 0; i < combat.enemies.size(); ++i)
                    enemy_update(combat, sc, combat.enemies[i], *prince_a, available, game_time, dt);
                prince_hit_resolve(combat, sc, *prince_a, game_time);
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
                if (st) st->update(game_time);
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
                if (!sc.actors[i].is_enemy) { player = &sc.actors[i]; break; }
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
                }
            for (size_t i = 0; i < holds.size(); ++i) {
                if (game_time < holds[i].from || game_time >= holds[i].to) continue;
                for (size_t c = 0; c < holds[i].keys.size(); ++c) {
                    const char ch = holds[i].keys[c];
                    if (ch >= 'A' && ch <= 'Z') down[plat::KEY_A + (ch - 'A')] = true;
                    if (ch == '_') down[plat::KEY_SPACE] = true;
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
            const bool run = autorun || down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT] || pad_run ||
                             (pad.connected && (pad.button[plat::PAD_RSHOULDER] ||
                                                pad.axis[plat::PAD_AXIS_RTRIGGER] > 0.5f));
            // The input in the Prince's own frame: x his left, z ahead.
            const float pf[3] = { std::sin(player_yaw), 0.0f, std::cos(player_yaw) };
            const float ps[3] = { std::cos(player_yaw), 0.0f, -std::sin(player_yaw) };
            float local[3] = { 0.0f, 0.0f, 0.0f };
            if (classic) { local[0] = -in_x; local[2] = in_y; }
            else if (ml > 1e-6f) {
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
            if (draw_pressed && player->armed && free_body) {
                combat.prince_drawn = !combat.prince_drawn;
                const char* clip = combat.prince_drawn ? "CombatDrawWeaponBack_G2" : "CombatStoreWeaponBack_G2";
                play_once(sc, *player, clip, game_time, player_yaw, false);
                combat.prince_busy_until = game_time + skill_seconds(sc, *player, clip);
                if (!combat.prince_drawn) player->stance = 0;
            }
            combat.prince_blocking = combat.prince_drawn && blk_key && free_body;
            if (combat.prince_drawn && free_body) {
                // Face the nearest guard; stand ready, or block.
                const int ne = nearest_enemy(combat, sc, lt.translate, 6.0f);
                if (ne >= 0) player_yaw = yaw_toward(lt.translate,
                                                     sc.actors[size_t(combat.enemies[size_t(ne)].actor)].node->local.translate);
                if (atk_pressed) {
                    const int dir = local[0] > 0.4f ? DIR_LEFT : local[0] < -0.4f ? DIR_RIGHT : DIR_CENTER;
                    const std::string atk = std::string("CombatAttack") + k_dir_names[dir] + "_G2";
                    if (play_once(sc, *player, atk, game_time, player_yaw, false)) {
                        combat.prince_hit.active = true;
                        combat.prince_hit.dir = dir;
                        combat.prince_hit.at = game_time + fight_tuning.fight(
                            std::string("Sword ") + k_dir_names[dir] + " Attack Begin", 0.3);
                        combat.prince_busy_until = game_time + skill_seconds(sc, *player, atk);
                    }
                }
                player->stance = find_actor_skill(sc, *player,
                    combat.prince_blocking ? "CombatBlockingCenter_G2" : "CombatAtReady_G2");
            }
            // E near a pickup collects it.
            if (act && !dead) {
                for (size_t i = 0; i < sc.pickups.size(); ++i) {
                    if (pickup_taken[i]) continue;
                    const scene::pickup_def& pk = sc.pickups[i];
                    const float dx = pk.pos[0] - lt.translate[0], dz = pk.pos[2] - lt.translate[2];
                    if (dx * dx + dz * dz > 1.6f * 1.6f || std::fabs(pk.pos[1] - lt.translate[1]) > 2.0f) continue;
                    pickup_taken[i] = true;
                    const std::string a1 = pk.argv.size() > 1 ? pk.argv[1] : "";
                    const std::string a2 = pk.argv.size() > 2 ? pk.argv[2] : "";
                    if (a2 == "weapon") {
                        player->armed = true;
                        std::printf("pickup: %s - the Prince has a sword (R draws it)\n", pk.name.c_str());
                    } else if (a1 == "1") {
                        player->health = std::min(player->max_health, player->health + std::atoi(a2.c_str()));
                        std::printf("pickup: %s - health %d\n", pk.name.c_str(), player->health);
                    } else {
                        std::printf("pickup: %s (%s) - not used yet\n", pk.name.c_str(), a1.c_str());
                    }
                    if (pk.argv.size() > 3 && !pk.argv[3].empty()) sc.script.execute(pk.argv[3], 0);
                    play_sound("PRNpickup", lt.translate, 10.0f, false);
                    break;
                }
            }
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
            }
            if (!on_ledge && jump_pressed && !scripted_now && crates.state == crate_ctl::NONE &&
                jumps.state == jump_ctl::NONE && player_vy == 0.0f) {
                // A ledge in front decides what the jump button does: climb a
                // low one, jump up to catch a high one, else an ordinary jump.
                if (!hang_try_climb(ledges, *player, sc.coll, lt.translate, player_yaw, game_time)) {
                    const bool running = run && ml > 1e-6f && player->clip == LOCO_RUN;
                    float jl[3] = { local[0], 0.0f, local[2] };
                    hang_measure_grip(ledges, *player);
                    ledge_hit high;
                    if (!running && ledges.have_grip &&
                        find_ledge(sc.coll, lt.translate, player_yaw, 2.0f, ledges.grip[1] + 2.4f, high)) {
                        player_yaw = high.facing;
                        jl[0] = jl[2] = 0.0f;                       // straight up, toward it
                    }
                    jump_start(jumps, *player, running, jl, game_time, lt.translate);
                }
            }
            bool in_jump = !scripted_now && !on_ledge && ledges.state == hang_ctl::NONE &&
                jump_update(jumps, *player, sc.coll, lt.translate, player_yaw, game_time, dt);
            // In the air, the hands catch any ledge in reach.
            if (in_jump && jumps.state == jump_ctl::AIR) {
                const float hs = std::sqrt(jumps.vel[0] * jumps.vel[0] + jumps.vel[2] * jumps.vel[2]);
                if (hang_try_grab(ledges, *player, sc.coll, lt.translate, player_yaw,
                                  jumps.vel[1], hs, game_time)) {
                    jumps.state = jump_ctl::NONE;
                    in_jump = false;
                }
            }
            const bool busy = on_ledge || ledges.state != hang_ctl::NONE;
            const bool in_crate = !scripted_now && !in_jump && !busy &&
                                  update_crate(crates, sc, *player, lt.translate, player_yaw,
                                               game_time, act, push, pull);
            if (scripted_now || in_jump || busy) {
                // A scripted skill, a jump or a ledge owns the body.
            } else if (in_crate) {
                crate_input(crates, sc, *player, lt.translate, player_yaw, game_time, act, push, pull);
            } else if (ml > 1e-6f) {
                player->stance = 0;
                // Walk_L or Running_L, moved at the speed its own root motion
                // covers, so the feet do not slide. Classic's back step plays
                // WalkBackwards_L1 and keeps his facing.
                const int clip = walk_back ? WALK_BACK : (run ? LOCO_RUN : LOCO_WALK);
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
            }
            // Stand on the floor, step up small ledges, fall off edges.
            if (!noclip && !scripted_now && !in_jump && !busy) {
                float gy;
                if (player_vy <= 0.0f && ground_under(sc.coll, lt.translate, gy)) {
                    lt.translate[1] = gy;
                    player_vy = 0.0f;
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
            const k2::engine::transform& w = player->node->world();
            cam.pos[0] = w.translate[0] - std::sin(cam.yaw) * follow_dist;
            cam.pos[1] = w.translate[1] + follow_height;
            cam.pos[2] = w.translate[2] + std::cos(cam.yaw) * follow_dist;
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
                const float head[3] = { w.translate[0], w.translate[1] + 1.5f, w.translate[2] };
                const float heights[3] = { follow_height, 1.5f, 0.9f };
                float best_pos[3] = { cam.pos[0], cam.pos[1], cam.pos[2] };
                float best_len = -1.0f;
                for (int hi = 0; hi < 3; ++hi) {
                    const float target[3] = { cam.pos[0], w.translate[1] + heights[hi], cam.pos[2] };
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
                } else {
                    for (int k = 0; k < 3; ++k) {
                        cam.pos[k] = w.translate[k] + camera_msgs.pos[k];
                        at[k] = cam.pos[k] + camera_msgs.look[k];
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
        if (!pose_arg && !noanim)
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].scripted) animate_actor(sc.actors[i], dt);

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
                float am[16];
                aw.to_matrix(am);
                for (size_t k = 0; k < ai.item_count; ++k) {
                    const draw_item& di = sc.items[ai.first_item + k];
                    if (di.is_trap) continue;
                    if (ai.skinned && di.joint >= 0 && !noskin) {
                        // actor_world * cur[j] * bind_inv[j]. Identity at the
                        // bind pose, so this cannot change the image until a
                        // joint is actually moved.
                        k2::engine::transform t0, t1;
                        k2::engine::transform_compose(ai.cur[size_t(di.joint)],
                                                      ai.bind_inv[size_t(di.joint)], t0);
                        k2::engine::transform_compose(aw, t0, t1);
                        float jm[16];
                        t1.to_matrix(jm);
                        gfx::set_model(jm);
                    } else {
                        gfx::set_model(am);
                    }
                    gfx::set_texture(di.tex);
                    gfx::draw(di.mesh);
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

        // HUD: the Prince's health, top left. A unit quad in clip space.
        {
            actor_inst* pa = 0;
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].is_enemy) { pa = &sc.actors[i]; break; }
            if (pa && follow_mode && !input_locked) {
                static gfx::mesh_id quad = 0;
                if (!quad) {
                    gfx::vertex v[4];
                    std::memset(v, 0, sizeof(v));
                    const float xy[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
                    for (int k = 0; k < 4; ++k) {
                        v[k].x = xy[k][0]; v[k].y = xy[k][1]; v[k].z = 0.0f;
                        v[k].nz = 1.0f; v[k].r = v[k].g = v[k].b = v[k].a = 255;
                    }
                    const uint16_t ix[6] = { 0, 1, 2, 0, 2, 3 };
                    quad = gfx::create_mesh(v, 4, ix, 6);
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
                gfx::set_texture(gfx::INVALID_TEXTURE);
                const float frac = pa->max_health > 0 ? float(pa->health) / float(pa->max_health) : 0.0f;
                const float x0 = -0.95f, y0 = 0.88f, w0 = 0.5f, h0 = 0.04f;
                float m[16];
                mat4_identity(m);
                m[0] = w0 + 0.01f; m[5] = h0 + 0.01f; m[12] = x0 - 0.005f; m[13] = y0 - 0.005f;
                gfx::set_model(m);
                gfx::set_tint(0.05f, 0.03f, 0.02f, 0.75f);
                gfx::draw(quad);
                m[0] = w0 * frac; m[5] = h0; m[12] = x0; m[13] = y0;
                gfx::set_model(m);
                gfx::set_tint(0.75f, 0.08f, 0.05f, 0.95f);
                if (frac > 0.0f) gfx::draw(quad);
                if (pa->armed) {                        // a small mark for the sword
                    m[0] = 0.02f; m[5] = h0; m[12] = x0 + w0 + 0.02f; m[13] = y0;
                    gfx::set_model(m);
                    gfx::set_tint(0.85f, 0.8f, 0.6f, 0.95f);
                    gfx::draw(quad);
                }
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
                    if (sc.actors[a].is_enemy) continue;
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
