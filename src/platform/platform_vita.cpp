// platform_vita.cpp — PlayStation Vita implementation of platform.h.
//
// VitaSDK system libraries only. The screen belongs to the GXM renderer
// (gfx_gxm.cpp), which owns the display buffers and presents in
// gfx::end_frame; swap_buffers here is a no-op, and there is no GL context.
//
// Input, as the original's pad layout maps onto the Vita:
//   left stick / d-pad  move              right stick       camera
//   Cross   jump (PAD_A)                  Circle    attack (PAD_B)
//   Square  action (PAD_X)                Triangle  draw / sheathe (PAD_Y)
//   L       block (PAD_LSHOULDER)         R         run (PAD_RSHOULDER)
//   Start   game menu                     Select    motion camera on / off
//   front touch  the mouse: tap menus, drag to look
//   rear touch   drag to look (reported as a right-button drag)
//   gyroscope    pad_state::motion, for the motion camera
// L2/R2/L3/R3 come through on a PS TV with a DualShock.
//
// Threads: the main thread runs on core 0; engine/jobs workers ask for
// cores 1 and 2; the audio thread runs on any core at a high priority.

#include "platform/platform.h"

#include <psp2/appmgr.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/touch.h>

#include <sys/reent.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

extern "C" struct _reent* __getreent(void);

// Main memory for newlib's heap: the engine keeps every room of a level,
// its textures decoded, in memory. The GPU's own heaps are allocated as
// memblocks by gfx_gxm.cpp, outside this.
extern "C" {
unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;     // the parsers recurse
}

// This newlib's printf has no C99 length modifiers: "%zu" prints "zu" and
// leaves its argument on the list, misreading every one after it. The
// engine prints size_t with %zu, so printf and friends are wrapped
// (-Wl,--wrap) and the 'z' dropped - size_t is unsigned int here.
namespace {
const char* fix_format(const char* in, char* buf, size_t n) {
    if (!std::strstr(in, "z")) return in;
    size_t o = 0;
    bool changed = false;
    for (size_t i = 0; in[i] && o + 1 < n; ++i) {
        buf[o++] = in[i];
        if (in[i] != '%') continue;
        // flags, width, precision
        size_t j = i + 1;
        while (in[j] && std::strchr("-+ #0123456789.*", in[j])) buf[o++] = in[j++];
        if (in[j] == 'z') { ++j; changed = true; }
        i = j - 1;
        if (o + 1 >= n) break;
    }
    buf[o] = 0;
    return changed ? buf : in;
}
}

extern "C" {
int __real_vfprintf(FILE*, const char*, va_list);
int __real_vsnprintf(char*, size_t, const char*, va_list);

int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    char b[1024];
    return __real_vfprintf(f, fix_format(fmt, b, sizeof(b)), ap);
}
int __wrap_vprintf(const char* fmt, va_list ap) { return __wrap_vfprintf(stdout, fmt, ap); }
int __wrap_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = __wrap_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = __wrap_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char b[1024];
    return __real_vsnprintf(s, n, fix_format(fmt, b, sizeof(b)), ap);
}
int __wrap_snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = __wrap_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
}

namespace k2 {
namespace plat {

namespace {

const int SCREEN_W = 960, SCREEN_H = 544;

std::deque<event> g_events;
uint64_t          g_t0 = 0;
bool              g_resized = false;

uint32_t          g_buttons = 0;         // last frame's, for edges
SceTouchPanelInfo g_front, g_back;
bool              g_front_down = false, g_back_down = false;
int               g_front_x = 0, g_front_y = 0, g_back_x = 0, g_back_y = 0;
bool              g_motion = false;

audio_callback    g_audio_cb = 0;
void*             g_audio_user = 0;
int               g_audio_port = -1;
volatile bool     g_audio_run = false;
SceUID            g_audio_thread = -1;
const int         AUDIO_FRAMES = 512;    // per sceAudioOutOutput, a multiple of 64

event blank(event_type t) {
    event e;
    std::memset(&e, 0, sizeof(e));
    e.type = t;
    return e;
}

float stick(unsigned char v) {
    const float f = (float(v) - 128.0f) / 127.5f;
    if (std::fabs(f) < 0.12f) return 0.0f;      // the sticks rest a little off centre
    return f < -1.0f ? -1.0f : f > 1.0f ? 1.0f : f;
}

struct button_map { uint32_t bit; pad_button b; };
const button_map BUTTONS[] = {
    { SCE_CTRL_CROSS, PAD_A },        { SCE_CTRL_CIRCLE, PAD_B },
    { SCE_CTRL_SQUARE, PAD_X },       { SCE_CTRL_TRIANGLE, PAD_Y },
    { SCE_CTRL_L1, PAD_LSHOULDER },   { SCE_CTRL_R1, PAD_RSHOULDER },
    { SCE_CTRL_UP, PAD_DPAD_UP },     { SCE_CTRL_DOWN, PAD_DPAD_DOWN },
    { SCE_CTRL_LEFT, PAD_DPAD_LEFT }, { SCE_CTRL_RIGHT, PAD_DPAD_RIGHT },
    { SCE_CTRL_START, PAD_START },    { SCE_CTRL_SELECT, PAD_SELECT },
    { SCE_CTRL_L3, PAD_LSTICK },      { SCE_CTRL_R3, PAD_RSTICK },
};

// sceCtrlPeekBufferPositiveExt2 reports the Vita's L/R as L1/R1 and adds a
// DualShock's L2/R2/L3/R3 on a PS TV.
SceCtrlData read_ctrl() {
    SceCtrlData c;
    std::memset(&c, 0, sizeof(c));
    sceCtrlPeekBufferPositiveExt2(0, &c, 1);
    return c;
}

// Touch panel coordinates to screen pixels.
void touch_to_screen(const SceTouchPanelInfo& p, int tx, int ty, int& x, int& y) {
    const int w = p.maxAaX - p.minAaX, h = p.maxAaY - p.minAaY;
    x = w > 0 ? (tx - p.minAaX) * SCREEN_W / w : tx / 2;
    y = h > 0 ? (ty - p.minAaY) * SCREEN_H / h : ty / 2;
}

void poll_touch() {
    SceTouchData t;
    // Front: the mouse, left button.
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) >= 0) {
        const bool down = t.reportNum > 0;
        int x = g_front_x, y = g_front_y;
        if (down) touch_to_screen(g_front, t.report[0].x, t.report[0].y, x, y);
        if (down && (x != g_front_x || y != g_front_y || !g_front_down)) {
            event e = blank(EVENT_MOUSE_MOVE);
            e.mouse_x = x; e.mouse_y = y;
            e.mouse_dx = g_front_down ? x - g_front_x : 0;
            e.mouse_dy = g_front_down ? y - g_front_y : 0;
            g_events.push_back(e);
        }
        if (down != g_front_down) {
            event e = blank(down ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP);
            e.mouse_x = x; e.mouse_y = y;
            e.button = MOUSE_LEFT;
            g_events.push_back(e);
        }
        g_front_down = down;
        g_front_x = x; g_front_y = y;
    }
    // Rear: a right-button drag, for looking around with a finger behind.
    if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &t, 1) >= 0) {
        const bool down = t.reportNum > 0;
        int x = g_back_x, y = g_back_y;
        if (down) touch_to_screen(g_back, t.report[0].x, t.report[0].y, x, y);
        if (down != g_back_down) {
            event e = blank(down ? EVENT_MOUSE_DOWN : EVENT_MOUSE_UP);
            e.mouse_x = x; e.mouse_y = y;
            e.button = MOUSE_RIGHT;
            g_events.push_back(e);
        } else if (down && (x != g_back_x || y != g_back_y)) {
            event e = blank(EVENT_MOUSE_MOVE);
            e.mouse_x = x; e.mouse_y = y;
            e.mouse_dx = 2 * (x - g_back_x);    // a small pad: a full turn in a short swipe
            e.mouse_dy = 2 * (y - g_back_y);
            g_events.push_back(e);
        }
        g_back_down = down;
        g_back_x = x; g_back_y = y;
    }
}

void poll_buttons() {
    const SceCtrlData c = read_ctrl();
    const uint32_t changed = c.buttons ^ g_buttons;
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i) {
        if (!(changed & BUTTONS[i].bit)) continue;
        event e = blank(EVENT_PAD_BUTTON);
        e.pad_btn = BUTTONS[i].b;
        e.pad_pressed = (c.buttons & BUTTONS[i].bit) != 0;
        g_events.push_back(e);
    }
    g_buttons = c.buttons;
}

int audio_thread(SceSize, void*) {
    static float   mix[AUDIO_FRAMES * 2];
    static int16_t out[2][AUDIO_FRAMES * 2];
    int which = 0;
    while (g_audio_run) {
        if (g_audio_cb) g_audio_cb(mix, AUDIO_FRAMES, g_audio_user);
        else std::memset(mix, 0, sizeof(mix));
        int16_t* o = out[which];
        for (int i = 0; i < AUDIO_FRAMES * 2; ++i) {
            float v = mix[i] * 32767.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            o[i] = int16_t(v);
        }
        sceAudioOutOutput(g_audio_port, o);     // blocks until the port takes it
        which ^= 1;
    }
    return 0;
}

struct thread_args { thread_fn fn; void* arg; };

int thread_entry(SceSize, void* argp) {
    const thread_args* a = static_cast<const thread_args*>(argp);   // a copy, made by the kernel
    a->fn(a->arg);
    return 0;
}

// No console: printf goes to ux0:data/k2boy/log.txt and stderr to err.txt,
// unbuffered so a crash keeps everything up to it. This newlib keeps the
// standard streams per thread (__getreent()) as well as globally
// (_impure_ptr), so both are replaced, on the main thread, from the first
// platform call main() makes (default_args).
void ensure_log() {
    static bool done = false;
    if (done) return;
    done = true;
    sceIoMkdir("ux0:data/k2boy", 0777);
    struct _reent* r = __getreent();   // this thread's
    static FILE* out = 0;
    static FILE* err = 0;
    out = std::fopen("ux0:data/k2boy/log.txt", "w");
    err = std::fopen("ux0:data/k2boy/err.txt", "w");
    // Both this thread's streams and the global ones: code compiled to
    // either view of stdout/stderr ends up in the files.
    if (out) { std::setvbuf(out, 0, _IOLBF, 1024); r->_stdout = out; _impure_ptr->_stdout = out; }
    if (err) { std::setvbuf(err, 0, _IONBF, 0); r->_stderr = err; _impure_ptr->_stderr = err; }
    // Closed at exit: an emulator may hold writes until the close.
    std::atexit([] { if (out) std::fclose(out); if (err) std::fclose(err); });
    std::printf("k2boy: started\n");
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool init(const config& cfg) {
    (void)cfg;
    ensure_log();
    // Full clocks: CPU 444 MHz, bus and GPU 222, crossbar 166.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &g_front);
    sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &g_back);
    g_motion = sceMotionStartSampling() >= 0;

    g_t0 = sceKernelGetProcessTimeWide();
    g_buttons = read_ctrl().buttons;            // held at launch: not a press
    return true;
}

void shutdown() {
    if (g_motion) sceMotionStopSampling();
    g_motion = false;
}

bool poll_event(event& out) {
    if (g_events.empty()) {
        if (!g_resized) {
            g_resized = true;
            event e = blank(EVENT_RESIZE);
            e.width = SCREEN_W;
            e.height = SCREEN_H;
            g_events.push_back(e);
        }
        poll_buttons();
        poll_touch();
        if (g_events.empty()) return false;
        // One batch per frame: the caller drains until false, so mark the
        // end by returning false once this batch is out.
        out = g_events.front();
        g_events.pop_front();
        if (g_events.empty()) g_events.push_back(blank(EVENT_NONE));
        return true;
    }
    out = g_events.front();
    g_events.pop_front();
    return out.type != EVENT_NONE;
}

void pad_poll(pad_state& out) {
    std::memset(&out, 0, sizeof(out));
    const SceCtrlData c = read_ctrl();
    out.connected = true;
    out.axis[PAD_AXIS_LX] = stick(c.lx);
    out.axis[PAD_AXIS_LY] = -stick(c.ly);       // y up
    out.axis[PAD_AXIS_RX] = stick(c.rx);
    out.axis[PAD_AXIS_RY] = -stick(c.ry);
    out.axis[PAD_AXIS_LTRIGGER] = (c.buttons & SCE_CTRL_L2) ? 1.0f : 0.0f;
    out.axis[PAD_AXIS_RTRIGGER] = (c.buttons & SCE_CTRL_R2) ? 1.0f : 0.0f;
    for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); ++i)
        out.button[BUTTONS[i].b] = (c.buttons & BUTTONS[i].bit) != 0;
    if (g_motion) {
        SceMotionState m;
        if (sceMotionGetState(&m) >= 0) {
            out.has_motion = true;
            out.motion[0] = m.angularVelocity.x;
            out.motion[1] = m.angularVelocity.y;
            out.motion[2] = m.angularVelocity.z;
        }
    }
}

// GXM presents from gfx::end_frame, waiting on the display queue for vsync.
void swap_buffers() {}
void make_context_current() {}
void* gl_get_proc_address(const char*) { return 0; }

void drawable_size(int32_t& width, int32_t& height) {
    width = SCREEN_W;
    height = SCREEN_H;
}

void* native_window_handle() { return 0; }

double time_seconds() {
    return double(sceKernelGetProcessTimeWide() - g_t0) / 1000000.0;
}

// ---------------------------------------------------------------- process

void default_args(std::vector<std::string>& out) {
    ensure_log();
    out.clear();
    out.push_back("--boot");
    out.push_back("--menu");
    out.push_back("--notrig");
    out.push_back("ux0:data/k2boy/data");
}

std::string user_dir() {
    const std::string d = "ux0:data/k2boy";
    sceIoMkdir(d.c_str(), 0777);
    return d;
}

void relaunch(const char* argv0, const std::vector<std::string>& args) {
    (void)argv0;
    std::vector<char*> v;
    for (size_t i = 0; i < args.size(); ++i) v.push_back(const_cast<char*>(args[i].c_str()));
    v.push_back(0);
    sceAppMgrLoadExec("app0:eboot.bin", &v[0], 0);
}

// ---------------------------------------------------------------- threads

bool thread_start(thread_fn fn, void* arg, int core, const char* name) {
    int mask = 0;           // the process default
    if (core == 0) mask = SCE_KERNEL_CPU_MASK_USER_0;
    else if (core == 1) mask = SCE_KERNEL_CPU_MASK_USER_1;
    else if (core == 2) mask = SCE_KERNEL_CPU_MASK_USER_2;
    const SceUID t = sceKernelCreateThread(name ? name : "k2 thread", thread_entry,
                                           0x10000100, 256 * 1024, 0, mask, 0);
    if (t < 0) return false;
    thread_args a = { fn, arg };
    return sceKernelStartThread(t, sizeof(a), &a) >= 0;
}

int cpu_count() { return 3; }       // a game gets cores 0-2; the fourth is the system's

sema sema_create(int initial) {
    const SceUID id = sceKernelCreateSema("k2 sema", 0, initial, 0x7fffffff, 0);
    return id < 0 ? 0 : reinterpret_cast<sema>(intptr_t(id));
}

void sema_wait(sema s) { sceKernelWaitSema(SceUID(intptr_t(s)), 1, 0); }

void sema_signal(sema s, int count) { sceKernelSignalSema(SceUID(intptr_t(s)), count); }

void sema_destroy(sema s) { if (s) sceKernelDeleteSema(SceUID(intptr_t(s))); }

// ---------------------------------------------------------------- audio

bool audio_open(int32_t sample_rate, audio_callback cb, void* user) {
    // The BGM port takes the mixer's own rate (the main port is 48 kHz only).
    g_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_FRAMES, sample_rate,
                                       SCE_AUDIO_OUT_MODE_STEREO);
    if (g_audio_port < 0) return false;
    int vol[2] = { 32768, 32768 };
    sceAudioOutSetVolume(g_audio_port, SceAudioOutChannelFlag(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    g_audio_cb = cb;
    g_audio_user = user;
    g_audio_run = true;
    // High priority, so a long frame on the main thread does not starve it.
    g_audio_thread = sceKernelCreateThread("k2 audio", audio_thread, 64, 64 * 1024, 0, 0, 0);
    if (g_audio_thread < 0 || sceKernelStartThread(g_audio_thread, 0, 0) < 0) {
        g_audio_run = false;
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
        return false;
    }
    return true;
}

void audio_close() {
    if (g_audio_thread >= 0) {
        g_audio_run = false;
        sceKernelWaitThreadEnd(g_audio_thread, 0, 0);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }
    if (g_audio_port >= 0) sceAudioOutReleasePort(g_audio_port);
    g_audio_port = -1;
    g_audio_cb = 0;
}

} // namespace plat
} // namespace k2
