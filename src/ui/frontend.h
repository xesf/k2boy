// frontend.h — the game's screens around play: the main menu, the in-game
// menu (Esc), Options, Load and Save, Cinematics and Credits.
//
// The screens are the original's 640x480 art (data/2D/menus) with the
// GameText.txt strings the binary's menu tables name (0x74494C titles,
// 0x7449DC title positions, 0x744994 / 0x7449B8 button positions, all in
// the 640x480 frame; the option labels' positions from 0x43001D). Saves and
// settings are ours: a small text file each under ~/.k2boy, not the
// original's .pop format.
//
// C++11.

#ifndef K2_UI_FRONTEND_H
#define K2_UI_FRONTEND_H

#include <functional>
#include <string>

#include "ui/ui.h"

namespace k2 {
namespace ui {

struct settings {
    float sound_volume, music_volume;   // 0..1
    int   difficulty;                   // 0 easy, 1 medium, 2 hard
    bool  classic;                      // Classic (tank) controls instead of Modern
    settings() : sound_volume(1.0f), music_volume(1.0f), difficulty(1), classic(false) {}
    void load();
    void save() const;
};

// One saved game: enough to put the Prince back where he was.
struct save_game {
    bool        valid;
    std::string name;                   // shown in the list
    std::string level;                  // "prisonfix"
    int         region;
    float       pos[3], yaw;
    int         health, max_health;
    bool        armed;
    std::string when;                   // date and time, for the list
    save_game() : valid(false), region(0), yaw(0), health(36), max_health(36), armed(false) {
        pos[0] = pos[1] = pos[2] = 0.0f;
    }
    bool read(int slot);
    bool write(int slot) const;
};

enum result {
    R_NONE, R_NEW_GAME, R_LOAD, R_QUIT, R_RESUME, R_RESTART, R_MAIN_MENU, R_CLOSED
};

class frontend {
public:
    static const int SLOTS = 10;

    // `play_movie` shows a cutscene by name ("cs01"); false if the window closed.
    bool init(std::function<bool(const std::string&)> play_movie);

    // The title screen. On R_LOAD, `loaded` holds the save.
    result main_menu(save_game& loaded);
    // Esc during play. `current` is what Save writes; on R_LOAD, `loaded`.
    result game_menu(const save_game& current, save_game& loaded);
    // The Loading screen with its slider filled to `progress` (0..1).
    void loading(float progress);

    settings& prefs() { return m_settings; }
    game_text& text() { return m_text; }

private:
    struct input {
        bool up, down, left, right, enter, back, click, closed;
        bool moved;                     // the pointer moved (or touched) this frame
        float mx, my;                   // canvas coordinates
        input() : up(false), down(false), left(false), right(false), enter(false), back(false),
                  click(false), closed(false), moved(false), mx(-1), my(-1) {}
    };
    input poll();
    void frame_begin(const char* art, bool key = false);
    void frame_end();
    bool hover(const input& in, float x, float y, float w, float h) const;

    result options();
    result load_save(bool saving, const save_game* current, save_game& loaded);
    result cinematics();
    result credits();
    bool confirm(const std::string& question);

    canvas    m_canvas;
    game_text m_text;
    settings  m_settings;
    std::function<bool(const std::string&)> m_play;
    float     m_mx, m_my;
};

} // namespace ui
} // namespace k2

#endif // K2_UI_FRONTEND_H
