// platform.h — windowing, input, timing, audio sink, file access.
//
// Implemented per-platform. The macOS backend is Objective-C++ (platform_macos.mm).
// This header must NOT include or reference any graphics API, and gfx.h must not
// include this file. Context creation lives here because it is inherently
// platform-specific; the renderer only ever touches the GL API itself.
//
// C++11. POD types only, no allocation, no exceptions.
// Naming: snake_case types and functions, UPPER_CASE enum constants.

#ifndef K2_PLATFORM_H
#define K2_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace plat {

// ---------------------------------------------------------------- events

enum event_type {
    EVENT_NONE = 0,
    EVENT_QUIT,
    EVENT_RESIZE,       // drawable size changed (HiDPI aware)
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_WHEEL,
    EVENT_PAD_BUTTON,
    EVENT_PAD_AXIS
};

// Physical key positions, not characters. The original used DirectInput
// scancodes, so a position-based enum maps most directly.
enum key {
    KEY_UNKNOWN = 0,
    KEY_ESCAPE, KEY_SPACE, KEY_RETURN, KEY_TAB, KEY_BACKSPACE,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_LALT, KEY_RALT,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
    KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
    KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_COUNT
};

enum mouse_button { MOUSE_LEFT = 0, MOUSE_RIGHT, MOUSE_MIDDLE, MOUSE_BUTTON_COUNT };

enum pad_button {
    PAD_A = 0, PAD_B, PAD_X, PAD_Y,
    PAD_LSHOULDER, PAD_RSHOULDER,
    PAD_DPAD_UP, PAD_DPAD_DOWN, PAD_DPAD_LEFT, PAD_DPAD_RIGHT,
    PAD_START, PAD_SELECT, PAD_LSTICK, PAD_RSTICK,
    PAD_BUTTON_COUNT
};

enum pad_axis {
    PAD_AXIS_LX = 0, PAD_AXIS_LY, PAD_AXIS_RX, PAD_AXIS_RY,
    PAD_AXIS_LTRIGGER, PAD_AXIS_RTRIGGER,
    PAD_AXIS_COUNT
};

struct event {
    event_type type;

    // EVENT_RESIZE — drawable pixels, backing scale already applied.
    int32_t width;
    int32_t height;

    // EVENT_KEY_*
    key      k;
    bool     repeat;

    // EVENT_MOUSE_* — position in drawable pixels, delta since last move.
    int32_t  mouse_x;
    int32_t  mouse_y;
    int32_t  mouse_dx;
    int32_t  mouse_dy;
    mouse_button button;
    float        wheel;         // EVENT_MOUSE_WHEEL — positive is scroll up

    // EVENT_PAD_*
    int32_t   pad_index;
    pad_button pad_btn;
    bool      pad_pressed;
    pad_axis  axis;
    float     axis_value;   // [-1,1], or [0,1] for triggers
};

// ---------------------------------------------------------------- lifecycle

struct config {
    const char* title;
    int32_t     width;          // logical points; drawable may be larger on HiDPI
    int32_t     height;
    bool        vsync;
    bool        fullscreen;
    int32_t     gl_major;       // 3
    int32_t     gl_minor;       // 3  (macOS supports up to 4.1 core)
};

// Returns false and leaves nothing allocated on failure.
bool init(const config& cfg);
void shutdown();

// Pops one queued event. Returns false when the queue is empty this frame.
bool poll_event(event& out);

// The first connected gamepad, polled. Sticks are -1..1 with y up, triggers
// 0..1; `connected` is false when there is none. Polling rather than events:
// a game reads sticks every frame anyway.
struct pad_state {
    bool  connected;
    float axis[PAD_AXIS_COUNT];
    bool  button[PAD_BUTTON_COUNT];
    // A gyroscope in the pad or the device (the Vita's): angular velocity in
    // radians per second about the device's x (pitch), y (yaw) and z (roll).
    bool  has_motion;
    float motion[3];
};
void pad_poll(pad_state& out);

// Present. Blocks for vsync when config::vsync was set.
void swap_buffers();

// Make the GL context current on the calling thread. Called once at startup for
// single-threaded rendering; exposed for a future loader thread.
void make_context_current();

// GL entry point resolution, for the loader in gfx.
void* gl_get_proc_address(const char* name);

// Current drawable size in pixels (HiDPI aware). Cheap; safe to poll per frame.
void drawable_size(int32_t& width, int32_t& height);

// Opaque native handle. NSWindow* on macOS. Only a backend that already knows
// the platform should interpret this — e.g. a future Metal backend attaching a
// CAMetalLayer. Nothing in gfx.h may use it.
void* native_window_handle();

// Monotonic seconds since init(). Not wall clock.
double time_seconds();

// ---------------------------------------------------------------- process

// Arguments to run with when the program was started without any (a console
// launching it from its home screen): data root, front end, controls.
// Empty on a desktop, where the command line says what to do.
void default_args(std::vector<std::string>& out);

// Directory for saves and settings, created if missing. ~/.k2boy on a
// desktop, the app's data folder on a console.
std::string user_dir();

// Starts this program again with `args` (argv[1..]), replacing the running
// one. `argv0` is the path it was started with. Returns only on failure.
void relaunch(const char* argv0, const std::vector<std::string>& args);

// ---------------------------------------------------------------- threads
//
// Worker threads for engine/jobs. `core` pins the thread to one CPU core
// where the platform allows it (the Vita's cores 0-2), -1 for any.

typedef void (*thread_fn)(void* arg);
bool thread_start(thread_fn fn, void* arg, int core, const char* name);
// Cores the game may use, the calling (main) thread's included.
int  cpu_count();

// Counting semaphores, for waking workers and waiting on them. The Vita's
// kernel threads have no pthread state, so engine/jobs uses these rather
// than std::mutex / std::condition_variable.
typedef void* sema;
sema sema_create(int initial);
void sema_wait(sema s);
void sema_signal(sema s, int count);

// ---------------------------------------------------------------- audio

// Pull model, matching CoreAudio's render callback and every other backend.
// Called on a realtime thread: no allocation, no locks, no file I/O.
// Writes `frames` interleaved stereo float samples into `out`.
typedef void (*audio_callback)(float* out, int32_t frames, void* user);

bool audio_open(int32_t sample_rate, audio_callback cb, void* user);
void audio_close();

// ---------------------------------------------------------------- files
//
// A VFS is mandatory, not a convenience. Game data mixes `.K2A` / `.k2a` /
// `.K2a` within one directory, embeds Windows-style backslash paths
// (`geometry\rooms\prisonfix\prison3.k2a`) and doubled backslashes in .lvl
// [Bodies] entries, and must resolve case-insensitively on a case-sensitive
// volume. See docs/plan/04-platform-layer.md.

// Indexes `root_path` once so later lookups are case-insensitive and cheap.
bool fs_mount(const char* root_path);

// Normalises separators, collapses doubles, resolves case. `path` is a game
// path such as "geometry\\rooms\\prisonfix\\prison3.k2a".
// Writes a native absolute path into `out`. False if no such file.
bool fs_resolve(const char* path, char* out, size_t out_size);

// Reads a whole file. Caller frees with fs_free. Null on failure.
// The buffer is NUL-terminated past size_out, which the many text formats use.
uint8_t* fs_read(const char* path, size_t& size_out);
void     fs_free(uint8_t* data);

// Resolves by filename alone, ignoring any directory part. Needed because
// .K2A materials embed absolute authoring paths such as
// "D:\\K2BoyDemo\\Geometry\\Rooms\\solar1\\basewallpanel .bmp".
// Verified: all 3184 bitmap references across 40 room files resolve this way.
bool fs_resolve_basename(const char* name, char* out, size_t out_size);

// Reads a file located by basename alone. Caller frees with fs_free.
uint8_t* fs_read_basename(const char* name, size_t& size_out);

// Collects the basenames of every indexed file whose name ends with `ext`
// (compared case-insensitively, e.g. ".bmp"). Names that occur in more than one
// directory appear ONCE — this is the right granularity for textures, which are
// looked up by name, but wrong for enumerating files such as master.dat that
// share a basename across many directories. Use fs_list_paths_by_extension for
// those.
void fs_list_by_extension(const char* ext, std::vector<std::string>& out);

// As above but yields full relative paths, so every file is listed even when
// basenames collide.
void fs_list_paths_by_extension(const char* ext, std::vector<std::string>& out);

// Number of files indexed by the last fs_mount. Diagnostic.
size_t fs_file_count();

} // namespace plat
} // namespace k2

#endif // K2_PLATFORM_H
