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

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <map>
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

    actor_inst() : node(0), is_enemy(false), first_item(0), item_count(0),
                   skinned(false), clip(-1), clip_ms(0.0) {}
};

// The original's locomotion set, in the order of its name table at 0x749C30:
// { "BasePose_L", "Running_L", "Walk_L", "AmbientType_G" }. AmbientType_G is a
// stem that gets a random 1..6 suffix (see 0x4BDDA4), so it is not loaded here.
enum loco_clip { LOCO_BASE = 0, LOCO_RUN, LOCO_WALK, LOCO_COUNT };
const char* const k_loco_names[LOCO_COUNT] = { "BasePose_L", "Running_L", "Walk_L" };


struct scene {
    std::vector<draw_item>  items;
    std::vector<gfx::texture_id> textures;   // owned, one per material
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

gfx::texture_id load_texture(const std::string& bitmap_path, int& decoded, int& failed) {
    if (bitmap_path.empty()) return gfx::INVALID_TEXTURE;

    const std::string path = resolve_ifl(bitmap_path);

    size_t size = 0;
    uint8_t* data = plat::fs_read_basename(path.c_str(), size);
    if (!data) { ++failed; return gfx::INVALID_TEXTURE; }

    assets::image img;
    std::string err;
    const bool ok = assets::image_load(data, size, img, &err);
    plat::fs_free(data);
    if (!ok || !img.valid()) { ++failed; return gfx::INVALID_TEXTURE; }

    ++decoded;
    return gfx::create_texture(img.width, img.height, &img.rgba[0]);
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

void set_clip(actor_inst& a, int clip) {
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
    if (!a.skinned || a.clip < 0) return;
    const assets::skl_file& s = a.loco[size_t(a.clip)];
    const uint32_t len = clip_length_ms(s);
    a.clip_ms += double(dt) * 1000.0;
    if (len) a.clip_ms = std::fmod(a.clip_ms, double(len));
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
    sc.trap_types.clear();
    sc.actors.clear();
    delete sc.root;
    sc.root = new k2::engine::level_group();
    sc.root->name = "LevelRoot";
    sc.spawn_points.clear();
    sc.coll.clear();
    sc.script.clear();
    for (size_t i = 0; i < sc.traps.size(); ++i) delete sc.traps[i];
    sc.traps.clear();
    sc.areas.clear();
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
               const k2::engine::trap_header& h, scene& sc, int item, int32_t coll_node)
        : trap_object(w, name, h), m_sc(sc), m_item(item), m_node(coll_node), m_moving(false) {
        m_base[0] = m_base[1] = m_base[2] = 0.0f;
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
        return trap_object::handle(msg, sender);
    }

    void update(double now) {
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
};

// "prince": the messages the Prince takes from scripts. Only snooze (no input
// for N seconds) is acted on; skills and weapons need the PrinceObject.
class prince_target : public k2::engine::script_target {
public:
    explicit prince_target(k2::engine::script_world& w)
        : script_target("prince"), m_world(w), snooze_until(0.0) {}
    bool receive(const std::string& msg, k2::engine::script_target*) {
        if (msg.compare(0, 7, "snooze:") == 0) {
            snooze_until = m_world.now() + std::atof(msg.c_str() + 7);
            return true;
        }
        return false;
    }
    k2::engine::script_world& m_world;
    double snooze_until;
};

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
    for (size_t i = 0; i < f.materials.size(); ++i) {
        sc.textures.push_back(load_texture(f.materials[i].bitmap, decoded, failed));
        mat_transparent[i] = (f.materials[i].alpha == assets::K2A_ALPHA_OPACITY);
    }

    for (size_t i = 0; i < f.nodes.size(); ++i) {
        const assets::k2a_node& n = f.nodes[i];
        if (!n.has_trap) continue;
        if (n.trap.name == "PrinceStart" || n.trap.name == "EnemyStart")
            sc.spawn_points.push_back(n.trap);
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
            if (n.trap.name == "Area") {
                k2::engine::area_trap* a = new k2::engine::area_trap(
                    sc.script, n.name, hdr, n.trap.argv, n.trap.bbox);
                sc.areas.push_back(a);
                t = a;
            } else {
                t = new scene_trap(sc.script, n.name, hdr, sc,
                                   int(sc.items.size()) - 1, node);
            }
            sc.traps.push_back(t);
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
const float GRAVITY      = 9.8f;
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
    // Scripts run on game time, which starts at zero with the level.
    prince_target prince_msgs(sc.script);
    sc.script.add(&prince_msgs);
    sc.script.trace = trapmsg;
    double game_time = 0.0;
    std::printf("scripts: %zu traps, %zu areas%s\n", sc.traps.size(), sc.areas.size(),
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
                    if (e.k == plat::KEY_F) {
                        follow_mode = !follow_mode;
                        std::printf("camera: %s\n", follow_mode ? "follow" : "free-fly");
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
                    if (looking) {
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
            const bool action = follow_mode && down[plat::KEY_SPACE];
            for (size_t i = 0; i < sc.areas.size(); ++i)
                sc.areas[i]->update(game_time, pp && sc.areas[i]->contains(pp), action);
            for (size_t i = 0; i < sc.traps.size(); ++i) {
                scene_trap* st = dynamic_cast<scene_trap*>(sc.traps[i]);
                if (st) st->update(game_time);
            }
        }
        const bool snoozed = !noscript && game_time < prince_msgs.snooze_until;

        // Two control modes. Follow mode drives the Prince and puts the camera
        // behind him, which is what the game does; free-fly drives the camera
        // and is the inspection tool. F toggles. Follow is the default whenever
        // the level actually spawned a Prince.
        actor_inst* player = 0;
        if (follow_mode) {
            for (size_t i = 0; i < sc.actors.size(); ++i)
                if (!sc.actors[i].is_enemy) { player = &sc.actors[i]; break; }
        }

        float mv[3] = {0.0f, 0.0f, 0.0f};
        if (!(player && snoozed)) {
        if (down[plat::KEY_W] || autowalk) for (int a = 0; a < 3; ++a) mv[a] += fwd[a];
        if (down[plat::KEY_S]) for (int a = 0; a < 3; ++a) mv[a] -= fwd[a];
        if (down[plat::KEY_D]) for (int a = 0; a < 3; ++a) mv[a] += right[a];
        if (down[plat::KEY_A]) for (int a = 0; a < 3; ++a) mv[a] -= right[a];
        }

        if (player) {
            // Movement is horizontal: the Prince walks, he does not fly. There
            // is no collision or ground query yet, so he keeps his spawn height.
            mv[1] = 0.0f;
            const float ml = std::sqrt(mv[0]*mv[0] + mv[2]*mv[2]);
            k2::engine::transform& lt = player->node->local;
            const bool run = autorun || down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT];
            if (ml > 1e-6f) {
                // Walk_L or Running_L, moved at the speed its own root motion
                // covers, so the feet do not slide. Without the clip, the old
                // fixed speeds stand in.
                const int clip = run ? LOCO_RUN : LOCO_WALK;
                set_clip(*player, clip);
                float speed = walk_speed * (run ? 2.5f : 1.0f);
                float fwd_model[3] = {0.0f, 0.0f, 1.0f};
                if (player->clip == clip) {
                    const assets::skl_file& s = player->loco[size_t(clip)];
                    const float cs = clip_speed(s);
                    if (cs > 0.0f) speed = cs;
                    float d[3];
                    if (clip_root_motion(s, d) && (d[0]*d[0] + d[2]*d[2]) > 1e-8f)
                        { fwd_model[0] = d[0]; fwd_model[2] = d[2]; }
                }
                if (noclip) {
                    lt.translate[0] += mv[0] / ml * speed * dt;
                    lt.translate[2] += mv[2] / ml * speed * dt;
                } else {
                    move_horizontal(sc.coll, lt.translate,
                                    mv[0] / ml * speed * dt, mv[2] / ml * speed * dt);
                }
                // Face the way he is going: choose yaw so that set_yaw carries
                // the model's forward axis onto the travel direction. set_yaw
                // adds yaw to atan2(x, z), hence the difference of two angles.
                player_yaw = std::atan2(mv[0], mv[2]) - std::atan2(fwd_model[0], fwd_model[2]);
            } else {
                set_clip(*player, LOCO_BASE);
            }
            // Stand on the floor, step up small ledges, fall off edges.
            if (!noclip) {
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
            lt.set_yaw(player_yaw);
            sc.root->update_world();

            // Chase camera: behind and above, looking slightly down at him.
            const k2::engine::transform& w = player->node->world();
            cam.pos[0] = w.translate[0] - std::sin(cam.yaw) * follow_dist;
            cam.pos[1] = w.translate[1] + follow_height;
            cam.pos[2] = w.translate[2] + std::cos(cam.yaw) * follow_dist;
            // Keep the camera on the Prince's side of any wall: cast the arm
            // from his head and stop short of the first surface it meets. A
            // stand-in until CameraLogic / camera.dat are reversed.
            if (!noclip) {
                const float head[3] = { w.translate[0], w.translate[1] + 1.5f, w.translate[2] };
                const float arm[3]  = { cam.pos[0] - head[0], cam.pos[1] - head[1], cam.pos[2] - head[2] };
                const float len = std::sqrt(arm[0]*arm[0] + arm[1]*arm[1] + arm[2]*arm[2]);
                k2::engine::pick_hit h;
                if (len > 1e-4f && sc.coll.pick(head, arm, 1.0f, false, h)) {
                    // Back off 0.25 units from the hit, but never closer than
                    // half a unit to the head.
                    float t = h.distance - 0.25f / len;
                    if (t < 0.5f / len) t = 0.5f / len;
                    for (int k = 0; k < 3; ++k) cam.pos[k] = head[k] + arm[k] * t;
                }
            }
            camera_basis(cam, fwd, right);
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
            for (size_t i = 0; i < sc.actors.size(); ++i) animate_actor(sc.actors[i], dt);

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
            if (sc.items[i].transparent || sc.items[i].is_trap) continue;
            if (sc.items[i].owner >= 0 || sc.items[i].hidden) continue;
            set_item_model(sc.items[i]);
            gfx::set_texture(sc.items[i].tex);
            gfx::draw(sc.items[i].mesh);
        }
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
                gfx::set_texture(di.tex);
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
                    std::printf("player at (%.2f, %.2f, %.2f) yaw %.2f  camera (%.2f, %.2f, %.2f)\n",
                                tw.translate[0], tw.translate[1], tw.translate[2],
                                std::atan2(tw.rotate[2], tw.rotate[8]),
                                cam.pos[0], cam.pos[1], cam.pos[2]);
                    break;
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
    gfx::shutdown();
    plat::shutdown();
    return 0;
}
