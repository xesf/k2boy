// ui.cpp — see ui.h.

#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "assets/image.h"
#include "platform/platform.h"

namespace k2 {
namespace ui {

canvas::canvas() : m_w(640), m_h(480), m_scale(1), m_ox(0), m_oy(0), m_quad(0), m_part(0), m_font_ok(false) {
    std::memset(&m_font, 0, sizeof(m_font));
}

bool canvas::init() {
    size_t size = 0;
    uint8_t* d = plat::fs_read_basename("Tsunami.ttf", size);
    if (d) {
        // The rasterizer keeps the buffer and frees it with free().
        uint8_t* copy = static_cast<uint8_t*>(std::malloc(size));
        std::memcpy(copy, d, size);
        plat::fs_free(d);
        m_font_ok = gfx::xf_font_init(&m_font, copy, size) == 0;
    }
    gfx::vertex v[4];
    std::memset(v, 0, sizeof(v));
    const float xy[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    for (int k = 0; k < 4; ++k) {
        v[k].x = xy[k][0]; v[k].y = xy[k][1];
        v[k].u = xy[k][0]; v[k].v = xy[k][1];
        v[k].nz = 1.0f; v[k].r = v[k].g = v[k].b = v[k].a = 255;
    }
    const uint16_t ix[6] = { 0, 1, 2, 0, 2, 3 };
    m_quad = gfx::create_mesh(v, 4, ix, 6);
    m_part = gfx::create_dynamic_mesh(4, 6);
    return m_font_ok;
}

void canvas::resize(int width, int height) {
    m_w = width > 0 ? width : 1;
    m_h = height > 0 ? height : 1;
    m_scale = std::min(m_w / 640.0f, m_h / 480.0f);
    m_ox = (m_w - 640.0f * m_scale) * 0.5f;
    m_oy = (m_h - 480.0f * m_scale) * 0.5f;
}

void canvas::to_canvas(int px, int py, float& x, float& y) const {
    x = (px - m_ox) / m_scale;
    y = (py - m_oy) / m_scale;
}

const image& canvas::load(const std::string& path, bool key) {
    const std::string k = path + (key ? "#key" : "");
    std::map<std::string, image>::iterator it = m_images.find(k);
    if (it != m_images.end()) return it->second;
    image im;
    size_t size = 0;
    uint8_t* d = plat::fs_read(path.c_str(), size);
    if (d) {
        assets::image img;
        if (assets::image_load(d, size, img) && img.valid()) {
            if (key)
                for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
                    if (img.rgba[i] < 8 && img.rgba[i + 1] < 8 && img.rgba[i + 2] < 8) img.rgba[i + 3] = 0;
            im.tex = gfx::create_texture(img.width, img.height, &img.rgba[0]);
            im.w = img.width;
            im.h = img.height;
        }
        plat::fs_free(d);
    }
    return m_images[k] = im;
}

void canvas::begin() {
    float ident[16];
    for (int i = 0; i < 16; ++i) ident[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    gfx::set_view_projection(ident, ident);
    gfx::render_state rs;
    rs.blend = gfx::BLEND_ALPHA;
    rs.depth = gfx::DEPTH_NONE;
    rs.alpha_test = false;
    rs.vertex_color = false;
    rs.wireframe = false;
    rs.fog = false;
    gfx::set_render_state(rs);
}

// A canvas rectangle as the unit quad's model matrix (clip space, y up).
void canvas::quad(float x, float y, float w, float h) {
    const float sx = 2.0f / m_w, sy = 2.0f / m_h;
    float m[16];
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    m[0] = w * m_scale * sx;
    m[5] = -h * m_scale * sy;
    m[12] = (m_ox + x * m_scale) * sx - 1.0f;
    m[13] = 1.0f - (m_oy + y * m_scale) * sy;
    gfx::set_model(m);
    gfx::draw(m_quad);
}

void canvas::draw(const image& im, float x, float y, float w, float h, float alpha) {
    if (!im.ok()) return;
    gfx::set_texture(im.tex);
    gfx::set_tint(1, 1, 1, alpha);
    quad(x, y, w < 0 ? float(im.w) : w, h < 0 ? float(im.h) : h);
}

void canvas::draw_part(const image& im, float x, float y, float w, float h, float u0, float u1) {
    if (!im.ok() || w <= 0.0f) return;
    gfx::vertex v[4];
    std::memset(v, 0, sizeof(v));
    const float xy[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    for (int k = 0; k < 4; ++k) {
        v[k].x = xy[k][0]; v[k].y = xy[k][1];
        v[k].u = xy[k][0] ? u1 : u0; v[k].v = xy[k][1];
        v[k].nz = 1.0f; v[k].r = v[k].g = v[k].b = v[k].a = 255;
    }
    const uint16_t ix[6] = { 0, 1, 2, 0, 2, 3 };
    gfx::update_mesh(m_part, v, 4, ix, 6);
    gfx::set_texture(im.tex);
    gfx::set_tint(1, 1, 1, 1);
    std::swap(m_quad, m_part);      // quad() draws m_quad
    quad(x, y, w, h);
    std::swap(m_quad, m_part);
}

void canvas::fill(float x, float y, float w, float h, color c) {
    gfx::set_texture(gfx::INVALID_TEXTURE);
    gfx::set_tint(c.r, c.g, c.b, c.a);
    quad(x, y, w, h);
}

// The string rasterized at the window's pixel scale, white on clear.
const canvas::text_tex& canvas::render(const std::string& s, float px) {
    char key[32];
    std::snprintf(key, sizeof(key), "#%.1f#%.2f", px, m_scale);
    const std::string k = s + key;
    std::map<std::string, text_tex>::iterator it = m_texts.find(k);
    if (it != m_texts.end()) return it->second;
    text_tex t;
    t.tex = gfx::INVALID_TEXTURE; t.w = t.h = 0; t.x0 = t.y0 = 0; t.scale = m_scale;
    if (m_font_ok && !s.empty()) {
        const float dpx = px * m_scale;
        const float asc = float(m_font.ascent) * dpx / m_font.units_per_em;
        const float desc = float(-m_font.descent) * dpx / m_font.units_per_em;
        float width = 0;
        for (size_t i = 0; i < s.size(); ++i)
            width += gfx::xf_font_advance(&m_font, gfx::xf_font_glyph_index(&m_font, uint8_t(s[i])), dpx);
        t.w = int(std::ceil(width)) + 4;
        t.h = int(std::ceil(asc + desc)) + 4;
        std::vector<uint8_t> px4(size_t(t.w) * t.h * 4, 0);
        float pen = 2.0f;
        const int base = int(std::ceil(asc)) + 2;
        for (size_t i = 0; i < s.size(); ++i) {
            const int g = gfx::xf_font_glyph_index(&m_font, uint8_t(s[i]));
            gfx::xf_glyph gl;
            if (gfx::xf_font_render(&m_font, g, dpx, &gl) == 0) {
                for (int y = 0; y < gl.height; ++y)
                    for (int x = 0; x < gl.width; ++x) {
                        const int X = int(pen) + gl.x0 + x, Y = base + gl.y0 + y;
                        if (X < 0 || Y < 0 || X >= t.w || Y >= t.h) continue;
                        uint8_t* q = &px4[size_t(Y * t.w + X) * 4];
                        q[0] = q[1] = q[2] = 255;
                        q[3] = std::max(q[3], gl.alpha[y * gl.width + x]);
                    }
                std::free(gl.alpha);
            }
            pen += gfx::xf_font_advance(&m_font, g, dpx);
        }
        t.tex = gfx::create_texture(t.w, t.h, &px4[0]);
        t.x0 = 2.0f;
        t.y0 = float(base);
    }
    return m_texts[k] = t;
}

float canvas::text_width(const std::string& s, float px) {
    const text_tex& t = render(s, px);
    return t.w > 4 ? (t.w - 4) / t.scale : 0.0f;
}

void canvas::text(const std::string& s, float x, float y, float px, color c, int align) {
    const text_tex& t = render(s, px);
    if (t.tex == gfx::INVALID_TEXTURE) return;
    const float w = t.w / t.scale, h = t.h / t.scale;
    float left = x - t.x0 / t.scale;
    if (align == 1) left -= (w - 4 / t.scale) * 0.5f;
    else if (align == 2) left -= (w - 4 / t.scale);
    gfx::set_texture(t.tex);
    gfx::set_tint(c.r, c.g, c.b, c.a);
    quad(left, y - t.y0 / t.scale, w, h);
}

// --- game_text -------------------------------------------------------------------

bool game_text::load() {
    size_t size = 0;
    uint8_t* d = plat::fs_read_basename("GameText.txt", size);
    if (!d) return false;
    const std::string t(reinterpret_cast<const char*>(d), size);
    plat::fs_free(d);
    // Line by line: "//" lines are comments (commented-out ids among them);
    // "#textIDName" names the next quoted string.
    std::string pending;
    bool have_name = false;
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find('\n', p);
        if (e == std::string::npos) e = t.size();
        std::string line = t.substr(p, e - p);
        p = e + 1;
        size_t a = 0;
        while (a < line.size() && std::isspace(static_cast<unsigned char>(line[a]))) ++a;
        if (line.compare(a, 2, "//") == 0) continue;
        if (line.compare(a, 7, "#textID") == 0) {
            size_t b = a + 7;
            while (b < line.size() && (std::isalnum(static_cast<unsigned char>(line[b])) || line[b] == '_')) ++b;
            pending = line.substr(a + 7, b - a - 7);
            have_name = true;
            continue;
        }
        if (have_name && a < line.size() && line[a] == '"') {
            std::string v;
            for (size_t q = a + 1; q < line.size() && line[q] != '"'; ++q) {
                if (line[q] == '\\' && q + 1 < line.size()) { ++q; v += line[q] == 'n' ? '\n' : line[q]; }
                else v += line[q];
            }
            m_by_name[pending] = int(m_text.size());
            m_text.push_back(v);
            have_name = false;
        }
    }
    return !m_text.empty();
}

int game_text::id(const std::string& name) const {
    std::map<std::string, int>::const_iterator it = m_by_name.find(name);
    return it == m_by_name.end() ? -1 : it->second;
}

const std::string& game_text::get(int id) const {
    static const std::string none;
    return id >= 0 && size_t(id) < m_text.size() ? m_text[size_t(id)] : none;
}

const std::string& game_text::get(const std::string& name) const {
    static const std::string none;
    std::map<std::string, int>::const_iterator it = m_by_name.find(name);
    return it == m_by_name.end() ? none : m_text[size_t(it->second)];
}

} // namespace ui
} // namespace k2
