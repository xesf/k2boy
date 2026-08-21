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

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
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

// Y-axis rotation plus translation: the placement a spawn point specifies.
void mat4_translate_yaw(float* m, const float* pos, float yaw) {
    const float c = std::cos(yaw), sn = std::sin(yaw);
    mat4_identity(m);
    m[0]  =  c;  m[2]  = -sn;
    m[8]  =  sn; m[10] =  c;
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2];
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
    float           tint[3];        // filled by assign_trap_colors once the scene is loaded
};

// A spawned actor's drawable form: where it is, and which items are its body.
// The original attaches an AnimationNode to the LevelGroup; this is the same
// relationship expressed as a draw range plus a transform.
struct actor_inst {
    std::string name;
    float       pos[3];
    float       yaw;
    bool        is_enemy;
    size_t      first_item;
    size_t      item_count;
};

struct scene {
    std::vector<draw_item>  items;
    std::vector<gfx::texture_id> textures;   // owned, one per material
    std::vector<std::string> trap_types;     // distinct types present, for the legend
    std::vector<actor_inst>  actors;
    // PrinceStart / EnemyStart trap blocks found in the rooms. argv[0] is the
    // body name (Traps.txt); centre and pivot angle give the placement.
    std::vector<assets::k2a_trap> spawn_points;
    float mn[3];
    float mx[3];
    bool  empty;
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

void scene_reset(scene& sc) {
    sc.items.clear();
    sc.textures.clear();
    sc.trap_types.clear();
    sc.actors.clear();
    sc.spawn_points.clear();
    sc.empty = true;
    sc.mn[0] = sc.mn[1] = sc.mn[2] =  1e30f;
    sc.mx[0] = sc.mx[1] = sc.mx[2] = -1e30f;
}

// Parses one .K2A and appends its meshes to `sc`, growing the bounds.
bool append_scene(const char* path, scene& sc) {
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
        if (!is_volume) {
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

        // k2a_vertex and gfx::vertex have identical layout by construction.
        draw_item it;
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
        it.trap_type = -1;
        it.owner     = -1;
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
    }

    std::printf("  %-28s %3zu meshes %6zu verts  tex %3d ok %2d fail  traps %d\n",
                path, f.nodes.size(), total_v, decoded, failed, f.trap_count());
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
    if (!append_scene(geometry.c_str(), sc)) {
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
        actor_inst inst;
        inst.name       = spawns[size_t(id)].name;
        inst.pos[0]     = t.center[0];
        inst.pos[1]     = t.center[1];
        inst.pos[2]     = t.center[2];
        inst.yaw        = t.pivot_angle;
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

        std::printf("  spawned %-14s body %d (%s) at (%.1f, %.1f, %.1f) yaw %.2f  %zu meshes"
                    "  local x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
                    inst.name.c_str(), body, b->name.c_str(),
                    inst.pos[0], inst.pos[1], inst.pos[2], inst.yaw, inst.item_count,
                    lmn[0], lmx[0], lmn[1], lmx[1], lmn[2], lmx[2]);
    }
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
bool load_level(const char* path, scene& sc, assets::lvl_file& lv) {
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
        const assets::lvl_region& reg = lv.regions[r];
        for (size_t i = 0; i < reg.rooms.size(); ++i) {
            const std::string file = assets::lvl_room_filename(reg.rooms[i]);
            if (append_scene(file.c_str(), sc)) ++loaded;
            else { ++missing; std::printf("  missing room %s\n", file.c_str()); }
        }
    }
    std::printf("  rooms loaded %d, missing %d\n", loaded, missing);
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
        const actor_inst& a = sc.actors[size_t(start)];
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
    }

    std::printf("skills: %d parsed, %d failed\n", files, bad);
    std::printf("  JKey %zu, SKey %zu (%zu named), vKey %zu\n",
                jkeys, skeys, named_sounds, vkeys);
    std::printf("  quaternions unit-norm: %zu / %zu  (%.2f%%)\n",
                unit, chan, chan ? 100.0 * double(unit) / double(chan) : 0.0);
    return bad == 0 ? 0 : 1;
}

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
    std::fflush(stdout);

    if (dumpactor_path) {
        assets::master_file m;
        return describe_actor(dumpactor_path, m) ? 0 : 1;
    }
    if (skills_only)    return dump_skills();
    if (spawns_only)    return check_spawns();
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

    scene sc;
    assets::lvl_file lv;
    bool have_level = false;
    k2::engine::body_table bodies;
    std::vector<k2::engine::actor_spawn> spawns;

    if (actor_path) {
        load_actor(actor_path, sc);
        if (!sc.empty) {
            std::printf("  bounds x[%.1f, %.1f] y[%.1f, %.1f] z[%.1f, %.1f]\n",
                        sc.mn[0], sc.mx[0], sc.mn[1], sc.mx[1], sc.mn[2], sc.mx[2]);
        }
    } else if (level_path) {
        scene_reset(sc);
        have_level = load_level(level_path, sc, lv);
        if (have_level) {
            build_body_table(lv, bodies);
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
    bool   looking   = false;
    double last_time = plat::time_seconds();

    std::printf("\ncamera: WASD move, Q/E or Space down/up, drag LMB to look,\n"
                "        scroll to change speed, Shift fast, Ctrl slow, R reframe, Esc quit\n\n");

    float view[16], proj[16];
    mat4_identity(view);
    mat4_perspective(proj, 1.0472f /* 60 deg */, float(w) / float(h ? h : 1), 0.1f, 1000.0f);

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
                                     0.1f, 1000.0f);
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

        float fwd[3], right[3];
        camera_basis(cam, fwd, right);

        float mv[3] = {0.0f, 0.0f, 0.0f};
        if (down[plat::KEY_W]) for (int a = 0; a < 3; ++a) mv[a] += fwd[a];
        if (down[plat::KEY_S]) for (int a = 0; a < 3; ++a) mv[a] -= fwd[a];
        if (down[plat::KEY_D]) for (int a = 0; a < 3; ++a) mv[a] += right[a];
        if (down[plat::KEY_A]) for (int a = 0; a < 3; ++a) mv[a] -= right[a];
        if (down[plat::KEY_E] || down[plat::KEY_SPACE]) mv[1] += 1.0f;
        if (down[plat::KEY_Q]) mv[1] -= 1.0f;

        const float ml = std::sqrt(mv[0]*mv[0] + mv[1]*mv[1] + mv[2]*mv[2]);
        if (ml > 1e-6f) {
            float sp = cam.speed * dt;
            if (down[plat::KEY_LSHIFT] || down[plat::KEY_RSHIFT]) sp *= 4.0f;
            if (down[plat::KEY_LCTRL]  || down[plat::KEY_RCTRL])  sp *= 0.25f;
            for (int a = 0; a < 3; ++a) cam.pos[a] += mv[a] / ml * sp;
        }

        const float at[3] = {cam.pos[0] + fwd[0], cam.pos[1] + fwd[1], cam.pos[2] + fwd[2]};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        mat4_look_at(view, cam.pos, at, up);

        gfx::begin_frame();
        gfx::clear(true, true);
        gfx::set_view_projection(view, proj);

        gfx::render_state rs;
        rs.blend = gfx::BLEND_NONE;
        rs.cull  = gfx::CULL_NONE;      // .K2A winding not yet verified
        rs.depth = gfx::DEPTH_TEST_WRITE;
        rs.alpha_test = false;
        rs.alpha_ref  = 0.5f;
        rs.vertex_color = true;
        rs.fog = have_level && lv.fog.size() >= 2;
        rs.wireframe = false;
        if (rs.fog) {
            const assets::lvl_fog& f0 = lv.fog[0];
            const assets::lvl_fog& f1 = lv.fog[1];
            gfx::set_fog(f0.distance, f1.distance,
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
        for (size_t i = 0; i < sc.items.size(); ++i) {
            if (sc.items[i].transparent || sc.items[i].is_trap) continue;
            if (sc.items[i].owner >= 0) continue;
            gfx::set_texture(sc.items[i].tex);
            gfx::draw(sc.items[i].mesh);
        }

        rs.blend      = gfx::BLEND_ALPHA;
        rs.depth      = gfx::DEPTH_TEST;      // test, but do not write
        rs.alpha_test = true;                 // drop fully transparent texels
        rs.alpha_ref  = 0.02f;
        gfx::set_render_state(rs);
        for (size_t i = 0; i < sc.items.size(); ++i) {
            if (!sc.items[i].transparent || sc.items[i].is_trap) continue;
            if (sc.items[i].owner >= 0) continue;
            gfx::set_texture(sc.items[i].tex);
            gfx::draw(sc.items[i].mesh);
        }

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
                float am[16];
                mat4_translate_yaw(am, ai.pos, ai.yaw);
                gfx::set_model(am);
                for (size_t k = 0; k < ai.item_count; ++k) {
                    const draw_item& di = sc.items[ai.first_item + k];
                    if (di.is_trap) continue;
                    gfx::set_texture(di.tex);
                    gfx::draw(di.mesh);
                }
            }
            gfx::set_model(model);   // restore identity for the passes below
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
            if (++frames >= 3) {
                std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
                gfx::read_pixels(w, h, &px[0]);
                if (write_bmp(shot_path, w, h, &px[0]))
                    std::printf("wrote %s (%dx%d)\n", shot_path, w, h);
                else
                    std::fprintf(stderr, "k2boy: cannot write %s\n", shot_path);
                std::fflush(stdout);
                running = false;
            }
        }
    }

    gfx::shutdown();
    plat::shutdown();
    return 0;
}
