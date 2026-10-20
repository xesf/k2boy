// skl.cpp — see skl.h.

#include "assets/skl.h"

#include <cmath>
#include <cstring>

namespace k2 {
namespace assets {

namespace {

// The subset of MfStream the skill readers use. Every read is bounds-checked;
// the first failure latches, as MfStream__HasError does.
struct stream {
    const uint8_t* d;
    size_t         size;
    size_t         p;
    int32_t        version;     // MfStream +0x04, gates fields in every reader
    std::string    error;

    stream(const uint8_t* data, size_t n) : d(data), size(n), p(0), version(0) {}

    bool ok() const { return error.empty(); }
    bool fail(const char* what) {
        if (error.empty()) error = what;
        return false;
    }
    bool need(size_t n) {
        if (!ok()) return false;
        if (p + n > size) return fail("truncated");
        return true;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint8_t* q = d + p;
        p += 4;
        return uint32_t(q[0]) | (uint32_t(q[1]) << 8) |
               (uint32_t(q[2]) << 16) | (uint32_t(q[3]) << 24);
    }
    int32_t  i32() { return int32_t(u32()); }
    // sub_6051F2: a signed 16-bit count.
    int16_t  i16() {
        if (!need(2)) return 0;
        const int16_t v = int16_t(uint16_t(d[p]) | (uint16_t(d[p + 1]) << 8));
        p += 2;
        return v;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    void floats(float* out, int n) { for (int i = 0; i < n; ++i) out[i] = f32(); }
    void skip(size_t n) { if (need(n)) p += n; }
    // MfStream__ReadString (0x605251): u32 length, then the bytes. 0 is the
    // empty string; anything over 0x200 is a stream error.
    std::string str() {
        const uint32_t n = u32();
        if (!ok()) return std::string();
        if (n > 0x200) { fail("string too long"); return std::string(); }
        if (!need(n)) return std::string();
        std::string s(reinterpret_cast<const char*>(d + p), n);
        p += n;
        return s;
    }
    uint32_t tag() { return u32(); }
};

inline uint32_t fourcc(const char* s) {
    return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) |
           (uint32_t(uint8_t(s[2])) << 16) | (uint32_t(uint8_t(s[3])) << 24);
}

const uint32_t TAG_JKEY = fourcc("JKey");
const uint32_t TAG_SKEY = fourcc("SKey");
const uint32_t TAG_VKEY = fourcc("vKey");
const uint32_t TAG_SKLK = fourcc("SklK");
const uint32_t TAG_SKLS = fourcc("SklS");

// MfKeyframe base (0x6124E7): object +0x08 then +0x04, i.e. time then flag.
void read_keyframe_base(stream& s, uint32_t& time, uint32_t& flag) {
    time = s.u32();
    flag = s.u32();
}

// MfJointKeyframe (0x6123CE): the base, then 200 bytes straight into +0x0C.
void read_joint_key(stream& s, skl_joint_key& k) {
    read_keyframe_base(s, k.time, k.flag);
    k.field_0c = s.u32();
    for (int t = 0; t < 6; ++t) {
        s.floats(k.xf[t].pos, 3);
        s.floats(k.xf[t].quat, 4);
    }
    s.floats(k.vec3, 3);
    k.field_c4 = s.u32();
    k.ease_in  = s.f32();
    k.ease_out = s.f32();
    k.field_d0 = s.u32();
}

// MfSoundKeyframe (0x64058A).
void read_sound_key(stream& s, skl_sound_key& k) {
    uint32_t flag;
    read_keyframe_base(s, k.time, flag);
    s.f32();                            // +0x0C
    s.u32(); s.u32(); s.u32(); s.u32(); // +0x20, +0x24, two locals
    if (s.version > 0x1A) s.u32();      // +0x28 = (value == 0)
    if (s.version > 0x1B) s.u32();
    k.sound = s.str();                  // +0x10
}

// MfEventKeyframe (0x6407E9).
void read_event_key(stream& s, skl_event_key& k) {
    uint32_t flag;
    read_keyframe_base(s, k.time, flag);
    k.value  = s.u32();                 // +0x10
    k.target = s.str();                 // +0x0C
}

// MfKeyList (0x61196A): a count, then one tagged object per entry, each read
// through MfStream__get_object_maybe (0x60541F). That function treats -2 and
// any tag without a registered creator as a null entry, and -1 as an escape
// followed by the real tag and a size word.
bool read_key_list(stream& s, skl_file& out, std::vector<skl_joint_key>* joint) {
    const int32_t n = s.i32();
    for (int32_t i = 0; i < n && s.ok(); ++i) {
        uint32_t t = s.tag();
        if (t == 0xFFFFFFFEu) continue;
        if (t == 0xFFFFFFFFu) { t = s.tag(); s.u32(); }
        if (t == TAG_JKEY) {
            skl_joint_key k;
            read_joint_key(s, k);
            if (joint) joint->push_back(k);
            out.joint_keys.push_back(k);
        } else if (t == TAG_SKEY) {
            skl_sound_key k;
            read_sound_key(s, k);
            out.sound_keys.push_back(k);
        } else if (t == TAG_VKEY) {
            skl_event_key k;
            read_event_key(s, k);
            out.event_keys.push_back(k);
        } else if (t == 0) {
            // No creator for tag 0: the original returns null and moves on.
        } else {
            return s.fail("unknown keyframe tag");
        }
    }
    return s.ok();
}

// MfJointPath (0x640E59). Both arrays are run-length encoded: an i16 repeat
// count, then the value, until the declared number of keys is filled.
bool read_joint_path(stream& s, skl_joint_path& jp) {
    jp.interval_ms = s.u32();
    uint32_t npos, nrot;
    if (s.version < 0x20) {
        jp.frames = s.u32();
        npos = nrot = jp.frames;
    } else {
        npos      = s.u32();
        nrot      = s.u32();
        jp.frames = s.u32();
    }
    if (!s.ok()) return false;
    if (npos > 100000 || nrot > 100000) return s.fail("joint path too long");

    jp.pos.resize(size_t(npos) * 3);
    for (uint32_t i = 0; i < npos && s.ok(); ) {
        const int16_t run = s.i16();
        float v[3];
        s.floats(v, 3);
        if (run <= 0 || i + uint32_t(run) > npos) return s.fail("bad position run");
        for (int16_t r = 0; r < run; ++r, ++i)
            std::memcpy(&jp.pos[size_t(i) * 3], v, sizeof v);
    }
    jp.rot.resize(size_t(nrot) * 4);
    for (uint32_t i = 0; i < nrot && s.ok(); ) {
        const int16_t run = s.i16();
        float v[4];
        s.floats(v, 4);
        if (run <= 0 || i + uint32_t(run) > nrot) return s.fail("bad rotation run");
        for (int16_t r = 0; r < run; ++r, ++i)
            std::memcpy(&jp.rot[size_t(i) * 4], v, sizeof v);
    }
    return s.ok();
}

// MfSkill base (0x60C1ED), entered as a top-level object: the stream version
// is still unset, so it is read here, followed by the chunk tag.
bool read_skill_base(stream& s, skl_file& out) {
    s.version = s.i32();
    const uint32_t chunk = s.tag();
    if (!s.ok()) return false;
    out.version = s.version;
    out.chunk_tag.assign(reinterpret_cast<const char*>(s.d + s.p - 4), 4);

    s.u32();                                // +0x14
    out.name = s.str();                     // +0x10
    if (s.version >= 0x66) s.u32();         // +0x0C
    const int32_t nj = s.i32();             // +0x18
    if (!s.ok()) return false;
    if (nj < 0 || nj > 4096) return s.fail("bad joint count");
    out.joints.resize(size_t(nj));

    if (chunk == TAG_SKLK) {
        for (int32_t j = 0; j < nj && s.ok(); ++j) {
            if (!s.u32()) continue;
            // MfStream__ReadObject: an MfJointKeyList read in place, untagged.
            out.joints[size_t(j)].present = true;
            read_key_list(s, out, &out.joints[size_t(j)].keys);
        }
    }

    if (s.version > 0x24) {
        s.floats(out.root.pos, 3);
        s.floats(out.root.quat, 4);
    } else {
        out.root.pos[0] = out.root.pos[1] = out.root.pos[2] = 0.0f;
        out.root.quat[0] = 1.0f; out.root.quat[1] = out.root.quat[2] = out.root.quat[3] = 0.0f;
    }
    if (s.version > 0x25) {
        out.interpolate = s.i32();          // +0x44
        out.field_48    = s.i32();
    }
    if (s.version >= 0x66 && s.u32()) {
        // A list of object references. No shipped skill has one, and resolving
        // them needs the stream's object table, which this reader does not keep.
        return s.fail("skill object references not supported");
    }
    if (s.version >= 0x67) s.floats(out.field_4c, 4);
    return s.ok();
}

// Skil (0x623CE6).
bool read_skill(stream& s, skl_file& out) {
    if (!read_skill_base(s, out)) return false;

    if (out.chunk_tag == "SklS") {
        for (size_t j = 0; j < out.joints.size() && s.ok(); ++j) {
            if (!s.u32()) continue;
            out.joints[j].present = true;
            read_joint_path(s, out.joints[j].path);
        }
    }
    if (s.u32()) read_key_list(s, out, 0);                      // +0x60
    if (s.version > 40 && s.u32()) read_key_list(s, out, 0);    // +0x68

    // +0x6C: MfTextureKeyList objects (0x63BDDA), read untagged.
    const int32_t ntex = s.i32();
    for (int32_t i = 0; i < ntex && s.ok(); ++i) {
        if (s.version >= 0x10) { s.str(); s.u32(); s.u32(); }
        read_key_list(s, out, 0);
    }

    if (s.version > 25)  out.duration_ms = s.u32();             // +0x80
    out.field_84 = s.version >= 34 ? s.i32() : 1;               // +0x84
    return s.ok();
}

// LSkl (0x63D3A0): the Skil part, then the locomotion block.
bool read_loco_skill(stream& s, skl_file& out) {
    if (!read_skill(s, out)) return false;
    if (s.version >= 0x17) {
        s.floats(out.loco, 5);                                  // +0x98 .. +0xA8
        const uint32_t n = s.u32();                             // +0xAC
        if (n > 100000) return s.fail("bad loco table");
        s.skip(size_t(n) * 8);                                  // +0xB0
    }
    const int32_t n2 = s.i32();
    if (s.version >= 0x11) { s.u32(); s.u32(); }                // +0xB4, +0xB8
    const size_t rec = s.version < 0x11 ? 0x0C : (s.version <= 0x17 ? 0xFC : 0x14C);
    if (n2 < 0 || n2 > 100000) return s.fail("bad loco record count");
    s.skip(size_t(n2) * rec);                                   // into +0x88
    return s.ok();
}

} // namespace

bool skl_parse(const uint8_t* d, size_t size, skl_file& out, std::string* error) {
    out = skl_file();

    if (!d || size < 24) { if (error) *error = "too small"; return false; }
    if (std::memcmp(d, "kSFM", 4) != 0) { if (error) *error = "bad magic"; return false; }

    out.kind_tag.assign(reinterpret_cast<const char*>(d + 4), 4);
    if      (out.kind_tag == "LSkl") out.kind = SKL_LOCO;
    else if (out.kind_tag == "MSkl") out.kind = SKL_MANIP;
    else                             out.kind = SKL_GENERIC;

    stream s(d, size);
    s.p = 8;                            // past the file magic and the object tag

    bool ok;
    if (out.kind == SKL_LOCO) {
        ok = read_loco_skill(s, out);
    } else {
        ok = read_skill(s, out);
        // MfManipSkill continues with manipulation targets (three MfProtoNode
        // records and more). Playback does not need them; keep the size.
        if (ok && out.kind == SKL_MANIP) {
            out.manip_bytes = size - s.p;
            s.p = size;
        }
    }
    if (ok && s.p != size) s.fail("trailing bytes");
    if (!s.ok()) {
        if (error) *error = s.error;
        return false;
    }
    return true;
}

// 0x612B97 with spin = 0. x87 evaluates the trig in 80-bit; double is the
// closest portable match and the result is renormalised either way.
void skl_slerp(const float a[4], const float b_in[4], float t, float out[4]) {
    float b[4] = {b_in[0], b_in[1], b_in[2], b_in[3]};
    double c = double(b[0]) * a[0] + double(b[1]) * a[1] +
               double(b[3]) * a[3] + double(b[2]) * a[2];
    if (c < 0.0) {                          // dbl_743508 = 0.0: take the short arc
        for (int i = 0; i < 4; ++i) b[i] = -b[i];
        c = -c;
    }
    double wa, wb;
    if (1.0 - c > 9.99999993922529e-12) {   // dbl_7512D0, i.e. (double)1e-11f
        const double omega = std::acos(c);
        const double inv   = 1.0 / std::sin(omega);
        wa = std::sin(omega - t * omega) * inv;
        wb = std::sin(t * omega) * inv;
    } else {
        wa = 1.0 - t;
        wb = t;
    }
    for (int i = 0; i < 4; ++i) out[i] = float(a[i] * wa + b[i] * wb);

    // sub_40F68D
    const double n = std::sqrt(double(out[0]) * out[0] + double(out[1]) * out[1] +
                               double(out[2]) * out[2] + double(out[3]) * out[3]);
    if (n > 0.0) {
        const float k = float(1.0 / n);
        for (int i = 0; i < 4; ++i) out[i] *= k;
    }
}

void skl_sample(const skl_joint_path& p, uint32_t time_ms, bool interpolate,
                float pos[3], float quat[4]) {
    const int32_t npos = int32_t(p.pos_keys());
    const int32_t nrot = int32_t(p.rot_keys());

    // Defaults for an empty track: the original builds them in its locals
    // (zero vector, identity quaternion) before choosing a key.
    pos[0] = pos[1] = pos[2] = 0.0f;
    quat[0] = 1.0f; quat[1] = quat[2] = quat[3] = 0.0f;

    if (time_ms == 0 || p.interval_ms == 0) {
        // Key 0 of both tracks.
        if (npos > 0) std::memcpy(pos,  &p.pos[0], 3 * sizeof(float));
        if (nrot > 0) std::memcpy(quat, &p.rot[0], 4 * sizeof(float));
        return;
    }

    // Unsigned divide; then clamp each track to its own last key.
    const uint32_t frame = time_ms / p.interval_ms;
    int32_t ri = int32_t(frame), pi = int32_t(frame);
    if (ri >= nrot) ri = nrot - 1;
    if (pi >= npos) pi = npos - 1;

    if (!interpolate) {
        if (pi >= 0) std::memcpy(pos,  &p.pos[size_t(pi) * 3], 3 * sizeof(float));
        if (ri >= 0) std::memcpy(quat, &p.rot[size_t(ri) * 4], 4 * sizeof(float));
        return;
    }

    // fild/fidiv, stored to a float.
    const float frac = float(double(time_ms % p.interval_ms) / double(p.interval_ms));

    if (frac != 0.0f && ri != nrot - 1)
        skl_slerp(&p.rot[size_t(ri) * 4], &p.rot[size_t(ri + 1) * 4], frac, quat);
    else if (ri >= 0)
        std::memcpy(quat, &p.rot[size_t(ri) * 4], 4 * sizeof(float));

    if (frac != 0.0f && pi != npos - 1) {
        // sub_61376B: a + (b - a) * t
        const float* a = &p.pos[size_t(pi) * 3];
        const float* b = a + 3;
        for (int k = 0; k < 3; ++k) pos[k] = (b[k] - a[k]) * frac + a[k];
    } else if (pi >= 0) {
        std::memcpy(pos, &p.pos[size_t(pi) * 3], 3 * sizeof(float));
    }
}

bool skl_sample_keys(const std::vector<skl_joint_key>& keys, uint32_t t,
                     float pos[3], float quat[4]) {
    if (keys.empty()) return false;
    size_t k = 0;
    while (k + 1 < keys.size() && keys[k + 1].time <= t) ++k;
    const skl_joint_key& a = keys[k];
    if (k + 1 >= keys.size() || t <= a.time || a.field_d0 == 2) {
        std::memcpy(pos,  a.xf[0].pos,  3 * sizeof(float));
        std::memcpy(quat, a.xf[0].quat, 4 * sizeof(float));
        return true;
    }
    const skl_joint_key& b = keys[k + 1];
    const float u0 = float(t - a.time) / float(b.time - a.time);
    const float s  = 1.0f - u0;
    const float u  = s * a.ease_out * s * u0 * 3.0f + s * b.ease_in * u0 * u0 * 3.0f
                   + u0 * u0 * u0;
    for (int i = 0; i < 3; ++i) pos[i] = (b.xf[0].pos[i] - a.xf[0].pos[i]) * u + a.xf[0].pos[i];
    skl_slerp(a.xf[0].quat, b.xf[0].quat, u, quat);
    return true;
}

} // namespace assets
} // namespace k2
