// frontend.cpp — see frontend.h. Positions are in the 640x480 art.

#include "ui/frontend.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include "gfx/gfx.h"
#include "platform/platform.h"

namespace k2 {
namespace ui {

namespace {

const color INK   = { 0.20f, 0.11f, 0.04f, 1.0f };
const color HOT   = { 0.65f, 0.08f, 0.03f, 1.0f };
const color FAINT = { 0.45f, 0.38f, 0.30f, 1.0f };
const color SHADE = { 0.95f, 0.85f, 0.55f, 0.55f };

// GameText ids (positions in GameText.txt).
enum {
    T_SAVE_TITLE = 11, T_LOAD_TITLE = 12, T_OPTIONS_TITLE = 13, T_CINEMATICS_TITLE = 14,
    T_SAVE = 16, T_LOAD = 17, T_OKAY = 18, T_MUSIC = 22, T_SOUND_VOL = 23, T_CANCEL = 25,
    T_NEW = 38, T_GAME_NEW = 39, T_LOAD_L = 40, T_GAME_LOAD = 41, T_OPTIONS = 42, T_PLAY = 43,
    T_CINEMATICS = 44, T_CREDITS = 45, T_EXIT = 46,
    T_SAVE_GAME = 47, T_LOAD_GAME = 48, T_OPTIONS2 = 49, T_RESTART = 50, T_RESUME = 51,
    T_MAIN_MENU = 52, T_QUIT = 53,
    T_CONTROLS = 55, T_GAME_TAB = 56, T_GRAPHICS = 57, T_SOUND_TAB = 58,
    T_LEVEL_OF = 69, T_DIFFICULTY = 70, T_EASY = 6,
    T_NONE = -1
};

std::string home_dir() { return plat::user_dir(); }

std::string slot_path(int slot) {
    char b[32];
    std::snprintf(b, sizeof(b), "/save%02d.txt", slot);
    return home_dir() + b;
}

} // namespace

// --- settings and saves ------------------------------------------------------------

void settings::load() {
    FILE* f = std::fopen((home_dir() + "/settings.txt").c_str(), "r");
    if (!f) return;
    char key[64];
    float v;
    while (std::fscanf(f, "%63s %f", key, &v) == 2) {
        const std::string k = key;
        if (k == "sound_volume") sound_volume = v;
        else if (k == "music_volume") music_volume = v;
        else if (k == "difficulty") difficulty = int(v);
        else if (k == "classic") classic = v != 0.0f;
    }
    std::fclose(f);
}

void settings::save() const {
    FILE* f = std::fopen((home_dir() + "/settings.txt").c_str(), "w");
    if (!f) return;
    std::fprintf(f, "sound_volume %.2f\nmusic_volume %.2f\ndifficulty %d\nclassic %d\n",
                 sound_volume, music_volume, difficulty, int(classic));
    std::fclose(f);
}

bool save_game::read(int slot) {
    valid = false;
    FILE* f = std::fopen(slot_path(slot).c_str(), "r");
    if (!f) return false;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char key[32], rest[200];
        rest[0] = 0;
        if (std::sscanf(line, "%31s %199[^\n]", key, rest) < 1) continue;
        const std::string k = key;
        if (k == "name") name = rest;
        else if (k == "level") level = rest;
        else if (k == "when") when = rest;
        else if (k == "region") region = std::atoi(rest);
        else if (k == "pos") std::sscanf(rest, "%f %f %f %f", &pos[0], &pos[1], &pos[2], &yaw);
        else if (k == "health") std::sscanf(rest, "%d %d", &health, &max_health);
        else if (k == "armed") armed = std::atoi(rest) != 0;
    }
    std::fclose(f);
    valid = !level.empty();
    return valid;
}

bool save_game::write(int slot) const {
    FILE* f = std::fopen(slot_path(slot).c_str(), "w");
    if (!f) return false;
    char when_s[64];
    const std::time_t now = std::time(0);
    std::strftime(when_s, sizeof(when_s), "%Y-%m-%d %H:%M", std::localtime(&now));
    std::fprintf(f, "k2boy-save 1\nname %s\nlevel %s\nwhen %s\nregion %d\npos %.3f %.3f %.3f %.4f\n"
                    "health %d %d\narmed %d\n",
                 name.c_str(), level.c_str(), when_s, region, pos[0], pos[1], pos[2], yaw,
                 health, max_health, int(armed));
    std::fclose(f);
    return true;
}

// --- frontend ------------------------------------------------------------------------

bool frontend::init(std::function<bool(const std::string&)> play_movie) {
    m_play = play_movie;
    m_mx = m_my = -1;
    m_settings.load();
    m_text.load();
    return m_canvas.init();
}

frontend::input frontend::poll() {
    input in;
    // K2_MENU_KEYS="down,down,enter": scripted input for headless checks,
    // one key a frame after a short settle.
    static int frames = 0;
    static std::vector<std::string> script;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (const char* k = std::getenv("K2_MENU_KEYS")) {
            std::string all = k, cur;
            for (size_t i = 0; i <= all.size(); ++i)
                if (i == all.size() || all[i] == ',') { if (!cur.empty()) script.push_back(cur); cur.clear(); }
                else cur += all[i];
        }
    }
    if (++frames > 3 && !script.empty()) {
        const std::string k = script.front();
        script.erase(script.begin());
        if (k == "up") in.up = true;
        else if (k == "down") in.down = true;
        else if (k == "left") in.left = true;
        else if (k == "right") in.right = true;
        else if (k == "enter") in.enter = true;
        else if (k == "esc") in.back = true;
    }
    plat::event e;
    while (plat::poll_event(e)) {
        switch (e.type) {
        case plat::EVENT_QUIT: in.closed = true; break;
        case plat::EVENT_KEY_DOWN:
            if (e.k == plat::KEY_UP) in.up = true;
            if (e.k == plat::KEY_DOWN) in.down = true;
            if (e.k == plat::KEY_LEFT) in.left = true;
            if (e.k == plat::KEY_RIGHT) in.right = true;
            if (e.k == plat::KEY_RETURN || e.k == plat::KEY_SPACE) in.enter = true;
            if (e.k == plat::KEY_ESCAPE) in.back = true;
            break;
        case plat::EVENT_MOUSE_MOVE:
            m_canvas.to_canvas(e.mouse_x, e.mouse_y, m_mx, m_my);
            break;
        case plat::EVENT_MOUSE_DOWN:
            m_canvas.to_canvas(e.mouse_x, e.mouse_y, m_mx, m_my);
            if (e.button == plat::MOUSE_LEFT) in.click = true;
            break;
        case plat::EVENT_PAD_BUTTON:
            if (!e.pad_pressed) break;
            if (e.pad_btn == plat::PAD_DPAD_UP) in.up = true;
            if (e.pad_btn == plat::PAD_DPAD_DOWN) in.down = true;
            if (e.pad_btn == plat::PAD_DPAD_LEFT) in.left = true;
            if (e.pad_btn == plat::PAD_DPAD_RIGHT) in.right = true;
            if (e.pad_btn == plat::PAD_A || e.pad_btn == plat::PAD_START) in.enter = true;
            if (e.pad_btn == plat::PAD_B) in.back = true;
            break;
        default: break;
        }
    }
    in.mx = m_mx;
    in.my = m_my;
    return in;
}

void frontend::frame_begin(const char* art, bool key) {
    int32_t w = 0, h = 0;
    plat::drawable_size(w, h);
    m_canvas.resize(w, h);
    gfx::begin_frame();
    gfx::set_clear_color(0.0f, 0.0f, 0.0f);
    gfx::clear(true, true);
    m_canvas.begin();
    if (art) m_canvas.draw(m_canvas.load(std::string("2D/menus/") + art + ".bmp", key), 0, 0, 640, 480);
}

void frontend::frame_end() {
    gfx::set_tint(1, 1, 1, 1);
    gfx::set_texture(gfx::INVALID_TEXTURE);
    // K2_MENU_SHOT=out.bmp: once the scripted keys are spent, save the frame
    // (24-bit BMP) and quit.
    static int settle = 0;
    if (const char* shot = std::getenv("K2_MENU_SHOT")) {
        const char* keys = std::getenv("K2_MENU_KEYS");
        static int needed = -1;
        if (needed < 0) {
            needed = 8;
            if (keys) for (const char* c = keys; *c; ++c) needed += (*c == ',') ? 1 : 0;
            if (keys && *keys) needed += 1;
        }
        if (++settle > needed + 4) {
            int32_t w = 0, h = 0;
            plat::drawable_size(w, h);
            std::vector<uint8_t> px(size_t(w) * h * 4);
            gfx::read_pixels(w, h, &px[0]);
            FILE* f = std::fopen(shot, "wb");
            if (f) {
                const int row = ((w * 3 + 3) / 4) * 4;
                const uint32_t size = 54 + uint32_t(row) * uint32_t(h);
                uint8_t hd[54] = { 'B', 'M' };
                std::memcpy(hd + 2, &size, 4);
                const uint32_t off = 54, dib = 40, img = uint32_t(row) * uint32_t(h);
                const uint16_t planes = 1, bpp = 24;
                std::memcpy(hd + 10, &off, 4); std::memcpy(hd + 14, &dib, 4);
                std::memcpy(hd + 18, &w, 4); std::memcpy(hd + 22, &h, 4);
                std::memcpy(hd + 26, &planes, 2); std::memcpy(hd + 28, &bpp, 2); std::memcpy(hd + 34, &img, 4);
                std::fwrite(hd, 1, 54, f);
                std::vector<uint8_t> line(size_t(row), 0);
                for (int y = h - 1; y >= 0; --y) {     // read_pixels rows are top-down
                    for (int x = 0; x < w; ++x) {
                        const uint8_t* p = &px[(size_t(y) * w + x) * 4];
                        line[size_t(x) * 3] = p[2]; line[size_t(x) * 3 + 1] = p[1]; line[size_t(x) * 3 + 2] = p[0];
                    }
                    std::fwrite(&line[0], 1, size_t(row), f);
                }
                std::fclose(f);
            }
            std::printf("menu shot: %s\n", shot);
            std::exit(0);
        }
    }
    gfx::end_frame();
    plat::swap_buffers();
}

// The original's loading screen (LoadingScreen__Begin / __Tick, 0x526EEA /
// 0x5271AE): Loading.bmp, then Slider Back and Loading Slider at
// g_LoadingSliderPos (230, 309), g_LoadingSliderSize 179 x 24. The original
// fills the slider a tenth at a time on a 500 ms timer, round and round;
// here it shows how far the load has actually got.
void frontend::loading(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    poll();                         // keep the system responsive (and the window alive)
    frame_begin(0);
    m_canvas.draw(m_canvas.load("2D/Loading.bmp"), 0, 0, 640, 480);
    const float X = 230, Y = 309, W = 179, H = 24;
    const image& back = m_canvas.load("2D/Slider Back.bmp");
    const image& fill = m_canvas.load("2D/Loading Slider.bmp");
    if (back.ok()) m_canvas.draw_part(back, X, Y, W, H, 0.0f, W / float(back.w));
    if (fill.ok()) m_canvas.draw_part(fill, X, Y, W * progress, H, 0.0f, W * progress / float(fill.w));
    frame_end();
}

bool frontend::hover(const input& in, float x, float y, float w, float h) const {
    return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h;
}

// A Yes / No question over the current screen (DialogBox YesNo).
bool frontend::confirm(const std::string& question) {
    int sel = 1;
    for (;;) {
        input in = poll();
        if (in.closed) return false;
        if (in.left || in.right) sel ^= 1;
        frame_begin(0);
        const image& box = m_canvas.load("2D/menus/DialogBox YesNo.bmp", true);
        const float bx = 320 - box.w * 0.5f, by = 240 - box.h * 0.5f;
        m_canvas.draw(box, bx, by);
        m_canvas.text(question, 320, by + box.h * 0.38f, 16, INK, 1);
        const float yy = by + box.h * 0.78f;
        const bool hy = hover(in, 230, yy - 22, 80, 30), hn = hover(in, 330, yy - 22, 80, 30);
        if (hy) sel = 0;
        if (hn) sel = 1;
        m_canvas.text(m_text.get("Yes"), 270, yy, 20, sel == 0 ? HOT : INK, 1);
        m_canvas.text(m_text.get("No"), 370, yy, 20, sel == 1 ? HOT : INK, 1);
        frame_end();
        if (in.back) return false;
        if (in.enter || (in.click && (hy || hn))) return sel == 0;
    }
}

result frontend::main_menu(save_game& loaded) {
    // Five slots on the left page (their rules at y 130 .. 319) and Exit in
    // the box below; two-word items are set on two lines, as the strings are.
    struct item { int a, b; float y; };
    // Each slot is the space between two rules, the last closed by the
    // box's foot (y 366).
    const item items[6] = { { T_NEW, T_GAME_NEW, 177 }, { T_LOAD_L, T_GAME_LOAD, 224 },
                            { T_OPTIONS, -1, 271 }, { T_PLAY, T_CINEMATICS, 319 },
                            { T_CREDITS, -1, 366 }, { T_EXIT, -1, 398 } };
    int sel = 0;
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.up) sel = (sel + 5) % 6;
        if (in.down) sel = (sel + 1) % 6;
        frame_begin("Main Menu");
        int clicked = -1;
        for (int i = 0; i < 6; ++i) {
            const float top = i < 5 ? items[i].y - 47 : 368, h = i < 5 ? 47 : 42;
            if (hover(in, 158, top, 125, h)) { sel = i; if (in.click) clicked = i; }
            const color c = sel == i ? HOT : INK;
            if (items[i].b >= 0) {
                m_canvas.text(m_text.get(items[i].a), 220, items[i].y - 26, 19, c, 1);
                m_canvas.text(m_text.get(items[i].b), 220, items[i].y - 6, 19, c, 1);
            } else {
                m_canvas.text(m_text.get(items[i].a), 220, items[i].y - (i < 5 ? 14 : 4), 22, c, 1);
            }
        }
        frame_end();
        if (in.back) clicked = 5;
        if (in.enter) clicked = sel;
        switch (clicked) {
        case 0: return R_NEW_GAME;
        case 1: {
            const result r = load_save(false, 0, loaded);
            if (r == R_LOAD || r == R_CLOSED) return r;
            break;
        }
        case 2: if (options() == R_CLOSED) return R_CLOSED; break;
        case 3: if (cinematics() == R_CLOSED) return R_CLOSED; break;
        case 4: if (credits() == R_CLOSED) return R_CLOSED; break;
        case 5: return R_QUIT;
        default: break;
        }
    }
}

result frontend::game_menu(const save_game& current, save_game& loaded) {
    const int ids[7] = { T_SAVE_GAME, T_LOAD_GAME, T_OPTIONS2, T_RESTART, T_RESUME, T_MAIN_MENU, T_QUIT };
    int sel = 4;
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.back) return R_RESUME;
        if (in.up) sel = (sel + 6) % 7;
        if (in.down) sel = (sel + 1) % 7;
        frame_begin("Game Menu");
        int clicked = -1;
        for (int i = 0; i < 7; ++i) {
            const float y = 140 + i * 40.0f;
            if (hover(in, 132, y - 28, 186, 38)) { sel = i; if (in.click) clicked = i; }
            m_canvas.text(m_text.get(ids[i]), 225, y, 19, sel == i ? HOT : INK, 1);
        }
        frame_end();
        if (in.enter) clicked = sel;
        switch (clicked) {
        case 0: { const result r = load_save(true, &current, loaded); if (r == R_CLOSED) return r; break; }
        case 1: { const result r = load_save(false, 0, loaded); if (r == R_LOAD || r == R_CLOSED) return r; break; }
        case 2: if (options() == R_CLOSED) return R_CLOSED; break;
        case 3: return R_RESTART;
        case 4: return R_RESUME;
        case 5: return R_MAIN_MENU;
        case 6: if (confirm(m_text.get("Do_you_really_want_to_qu"))) return R_QUIT; break;
        default: break;
        }
    }
}

result frontend::options() {
    // Tabs down the left page; each has its own art ("Options Sound" ...).
    static const char* arts[4] = { "Options Graphics", "Options Sound", "Options Game", "Options Controls" };
    const int tab_ids[4] = { T_GRAPHICS, T_SOUND_TAB, T_GAME_TAB, T_CONTROLS };
    settings edit = m_settings;
    int tab = 1, row = 0;
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.back) return R_NONE;
        const int rows = tab == 1 ? 2 : tab == 2 ? 1 : tab == 3 ? 1 : 0;
        if (in.up && rows) row = (row + rows - 1) % rows;
        if (in.down && rows) row = (row + 1) % rows;
        frame_begin(arts[tab]);
        m_canvas.text(m_text.get(T_OPTIONS_TITLE), 53, 77, 22, INK);
        for (int i = 0; i < 4; ++i) {
            const float y = 125 + i * 36.0f;
            const bool h = hover(in, 45, y - 26, 105, 34);
            if (h && in.click) { tab = i; row = 0; }
            m_canvas.text(m_text.get(tab_ids[i]), 97, y, 18, tab == i ? HOT : (h ? HOT : INK), 1);
        }
        const float step = (in.right ? 0.1f : 0.0f) - (in.left ? 0.1f : 0.0f);
        if (tab == 1) {                         // Sound: two sliders (0x43001D: labels at 257,156 / 257,216)
            float* vals[2] = { &edit.music_volume, &edit.sound_volume };
            const int labels[2] = { T_MUSIC, T_SOUND_VOL };
            const float ys[2] = { 172, 235 };
            const image& knob = m_canvas.load("2D/menus/Options Sound Slider.bmp", true);
            for (int i = 0; i < 2; ++i) {
                m_canvas.text(m_text.get(labels[i]), 257, ys[i] - 16, 17, row == i ? HOT : INK);
                if (row == i) *vals[i] = std::max(0.0f, std::min(1.0f, *vals[i] + step));
                if (in.click && hover(in, 370, ys[i] - 14, 195, 28)) {
                    row = i;
                    *vals[i] = std::max(0.0f, std::min(1.0f, (in.mx - 378) / 180.0f));
                }
                const float kx = 378 + *vals[i] * 180.0f;
                if (knob.ok()) m_canvas.draw(knob, kx - knob.w * 0.5f, ys[i] - knob.h * 0.5f);
                else m_canvas.fill(kx - 3, ys[i] - 8, 6, 16, HOT);
            }
        } else if (tab == 2) {                  // Game: Level of Difficulty (240,123 / 258,154)
            m_canvas.text(m_text.get(T_LEVEL_OF), 255, 135, 17, INK);
            m_canvas.text(m_text.get(T_DIFFICULTY), 255, 158, 17, INK);
            if (in.left) edit.difficulty = (edit.difficulty + 2) % 3;
            if (in.right) edit.difficulty = (edit.difficulty + 1) % 3;
            for (int i = 0; i < 3; ++i) {
                const float x = 400 + i * 62.0f;
                if (in.click && hover(in, x - 30, 130, 60, 30)) edit.difficulty = i;
                m_canvas.text(m_text.get(T_EASY + i), x, 150, 17, edit.difficulty == i ? HOT : INK, 1);
            }
        } else if (tab == 3) {                  // Controls: Modern / Classic, and the keys
            if (in.left || in.right) edit.classic = !edit.classic;
            if (in.click && hover(in, 380, 105, 200, 30)) edit.classic = !edit.classic;
            m_canvas.text("Scheme", 255, 125, 17, INK);
            m_canvas.text(edit.classic ? "Classic" : "Modern", 470, 125, 17, HOT, 1);
            static const char* lines[6] = {
                "Move: WASD / arrows / left stick", "Run: Shift    Jump: Space",
                "Act, climb, talk: E", "Sword: R draw, F attack, G block",
                "Camera: mouse drag / right stick", "Tab switches scheme in play" };
            for (int i = 0; i < 6; ++i) m_canvas.text(lines[i], 255, 160 + i * 18.0f, 13, INK);
        } else {                                // Graphics: nothing to set in a window
            m_canvas.text("The game runs in a resizable window.", 255, 150, 15, FAINT);
        }
        // Okay (84,313) and Cancel (78,370), from the button tables.
        const bool ho = hover(in, 60, 288, 92, 38), hc = hover(in, 55, 343, 97, 40);
        m_canvas.text(m_text.get(T_OKAY), 105, 313, 19, ho ? HOT : INK, 1);
        m_canvas.text(m_text.get(T_CANCEL), 105, 370, 19, hc ? HOT : INK, 1);
        frame_end();
        if ((in.click && ho) || (in.enter && rows == 0)) { m_settings = edit; m_settings.save(); return R_NONE; }
        if (in.enter && rows) { m_settings = edit; m_settings.save(); return R_NONE; }
        if (in.click && hc) return R_NONE;
    }
}

result frontend::load_save(bool saving, const save_game* current, save_game& loaded) {
    save_game slots[SLOTS];
    for (int i = 0; i < SLOTS; ++i) slots[i].read(i);
    int sel = 0;
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.back) return R_NONE;
        if (in.up) sel = (sel + SLOTS - 1) % SLOTS;
        if (in.down) sel = (sel + 1) % SLOTS;
        frame_begin(saving ? "Save Game" : "Load Game");
        m_canvas.text(m_text.get(saving ? T_SAVE_TITLE : T_LOAD_TITLE), saving ? 121 : 120, saving ? 82 : 83, 28, INK);
        bool dbl = false;
        for (int i = 0; i < SLOTS; ++i) {
            const float y = 160 + i * 17.5f;
            if (hover(in, 95, y - 14, 155, 17)) { if (in.click) { dbl = sel == i; sel = i; } }
            char label[64];
            std::snprintf(label, sizeof(label), "%d. %s", i + 1,
                          slots[i].valid ? slots[i].name.c_str() : m_text.get("Empty_Slot").c_str());
            m_canvas.text(label, 102, y, 13, sel == i ? HOT : (slots[i].valid ? INK : FAINT));
        }
        // Details of the selected slot, in the frame on the right.
        if (slots[sel].valid) {
            m_canvas.text(slots[sel].name, 490, 170, 18, INK, 1);
            m_canvas.text(slots[sel].when, 490, 200, 14, INK, 1);
            char hp[48];
            std::snprintf(hp, sizeof(hp), "Health %d / %d", slots[sel].health / 12, slots[sel].max_health / 12);
            m_canvas.text(hp, 490, 230, 14, INK, 1);
        } else {
            m_canvas.text(m_text.get("Empty_Slot"), 490, 210, 16, FAINT, 1);
        }
        if (saving && current) m_canvas.text(current->name, 85, 383, 15, INK);
        else if (slots[sel].valid) m_canvas.text(slots[sel].name, 85, 383, 15, INK);
        // Load/Save (318,376) and Cancel (434,376).
        const bool ha = hover(in, 290, 345, 100, 48), hc = hover(in, 415, 345, 105, 48);
        m_canvas.text(m_text.get(saving ? T_SAVE : T_LOAD), 340, 376, 20, ha ? HOT : INK, 1);
        m_canvas.text(m_text.get(T_CANCEL), 467, 376, 20, hc ? HOT : INK, 1);
        frame_end();
        if (in.click && hc) return R_NONE;
        if (in.enter || (in.click && ha) || dbl) {
            if (saving && current) {
                if (slots[sel].valid && !confirm(m_text.get("Are_you_sure_you_want_t1"))) continue;
                save_game s = *current;
                s.write(sel);
                return R_NONE;
            }
            if (!saving && slots[sel].valid) { loaded = slots[sel]; return R_LOAD; }
        }
    }
}

result frontend::cinematics() {
    // The 13 cutscenes (cs01 .. cs13), titled by GameText 101 .., with their
    // thumbnails (2D/menus/Movie Thumbnails/Thumb #N).
    int sel = 0;
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.back) return R_NONE;
        if (in.up) sel = (sel + 12) % 13;
        if (in.down) sel = (sel + 1) % 13;
        frame_begin("Show Movie");
        m_canvas.text(m_text.get(T_CINEMATICS_TITLE), 113, 428, 26, INK);
        bool dbl = false;
        for (int i = 0; i < 13; ++i) {
            const float y = 222 + i * 12.5f;
            if (hover(in, 95, y - 11, 175, 12.5f) && in.click) { dbl = sel == i; sel = i; }
            m_canvas.text(m_text.get(m_text.id("Introduction") + i), 102, y, 11, sel == i ? HOT : INK);
        }
        char thumb[64];
        std::snprintf(thumb, sizeof(thumb), "2D/menus/Movie Thumbnails/Thumb #%d.bmp", sel + 1);
        const image& im = m_canvas.load(thumb);
        if (im.ok()) m_canvas.draw(im, 296, 196, 208, 148);
        const bool hp = hover(in, 290, 360, 100, 45), hc = hover(in, 405, 360, 110, 45);
        m_canvas.text(m_text.get(T_PLAY), 340, 394, 20, hp ? HOT : INK, 1);
        m_canvas.text(m_text.get(T_CANCEL), 459, 394, 20, hc ? HOT : INK, 1);
        frame_end();
        if (in.click && hc) return R_NONE;
        if (in.enter || (in.click && hp) || dbl) {
            char name[16];
            std::snprintf(name, sizeof(name), "cs%02d", sel + 1);
            if (m_play && !m_play(name)) return R_CLOSED;
        }
    }
}

result frontend::credits() {
    // The credits strings (GameText 180 on), rising through the centre.
    std::vector<std::string> lines;
    for (size_t i = size_t(std::max(0, m_text.id("Lead_Designer"))); i < m_text.size(); ++i) lines.push_back(m_text.get(int(i)));
    const double start = plat::time_seconds();
    for (;;) {
        input in = poll();
        if (in.closed) return R_CLOSED;
        if (in.back || in.click || in.enter) return R_NONE;
        const float scroll = float(plat::time_seconds() - start) * 30.0f;
        frame_begin("Credits");
        bool any = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            const float y = 480 + i * 22.0f - scroll;
            if (y < -20 || y > 500) { if (y >= 500) any = true; continue; }
            any = true;
            // Odd lines are roles, even ones names, as the file pairs them.
            m_canvas.text(lines[i], 320, y, (i % 2) ? 20 : 15, (i % 2) ? INK : FAINT, 1);
        }
        frame_end();
        if (!any) return R_NONE;
    }
}

} // namespace ui
} // namespace k2
