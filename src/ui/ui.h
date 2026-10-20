// ui.h — 2D drawing for the menus: a 640x480 canvas (the original screens'
// size) scaled into the window and centred, with the game's menu art and its
// UI font (Tsunami.ttf) drawn on it.
//
// C++11.

#ifndef K2_UI_UI_H
#define K2_UI_UI_H

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "gfx/gfx.h"
#include "gfx/ttf.h"

namespace k2 {
namespace ui {

struct image {
    gfx::texture_id tex;
    int w, h;
    image() : tex(gfx::INVALID_TEXTURE), w(0), h(0) {}
    bool ok() const { return tex != gfx::INVALID_TEXTURE; }
};

struct color { float r, g, b, a; };

class canvas {
public:
    canvas();
    bool init();                            // loads the font; call after gfx::init

    // Window size in pixels; recomputes the 640x480 mapping.
    void resize(int width, int height);
    // Window pixels -> canvas coordinates.
    void to_canvas(int px, int py, float& x, float& y) const;

    // An image from the install ("2D/menus/Main Menu.bmp"), cached. `key`
    // keys pure black to transparent.
    const image& load(const std::string& path, bool key = false);

    void begin();                           // 2D state; call once a frame before drawing
    void draw(const image& im, float x, float y, float w = -1, float h = -1, float alpha = 1.0f);
    // The part of an image from texel column u0 to u1 (0..1 of its width),
    // full height, into the rectangle.
    void draw_part(const image& im, float x, float y, float w, float h, float u0, float u1);
    void fill(float x, float y, float w, float h, color c);
    // Text with its baseline at y, starting at x (align: 0 left, 1 centre, 2 right).
    void text(const std::string& s, float x, float y, float px, color c, int align = 0);
    float text_width(const std::string& s, float px);

private:
    struct text_tex { gfx::texture_id tex; int w, h; float x0, y0, scale; };
    const text_tex& render(const std::string& s, float px);
    void quad(float x, float y, float w, float h);

    int m_w, m_h;
    float m_scale, m_ox, m_oy;
    gfx::mesh_id m_quad;
    gfx::mesh_id m_part;        // draw_part's quad, its UVs rewritten per draw
    gfx::xf_font m_font;
    bool m_font_ok;
    std::map<std::string, image> m_images;
    std::map<std::string, text_tex> m_texts;
};

// GameText.txt: "#textIDName" followed by a quoted string, in order. Ids are
// the entries' positions (the menu tables index it that way).
class game_text {
public:
    bool load();
    const std::string& get(int id) const;
    const std::string& get(const std::string& name) const;
    int id(const std::string& name) const;     // -1 when there is none
    size_t size() const { return m_text.size(); }
private:
    std::vector<std::string> m_text;
    std::map<std::string, int> m_by_name;
};

} // namespace ui
} // namespace k2

#endif // K2_UI_UI_H
