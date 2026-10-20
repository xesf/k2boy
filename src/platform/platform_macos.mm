// platform_macos.mm — Cocoa window, OpenGL 3.3 core context, input, timing, audio.
//
// Objective-C++. Everything above this file is plain C++11.
//
// Frame pacing note: CVDisplayLink is deprecated and CADisplayLink on macOS is
// recent. We deliberately use neither — a plain loop with vsync via
// NSOpenGLCPSwapInterval has fewer moving parts and no API churn to track.

#include "platform/platform.h"

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <OpenGL/OpenGL.h>
#import <AudioUnit/AudioUnit.h>

#include <dlfcn.h>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <deque>
#include <vector>

// The close button does not by itself end the run loop, and polling
// -isVisible is not a reliable substitute: the window can stay "visible"
// through the close, so the app simply kept running. A delegate is the
// supported way to hear about it.
//
// -windowShouldClose: returns NO deliberately. The window stays alive and the
// main loop tears everything down in order on the next poll, which keeps the
// GL context valid until gfx::shutdown runs. Combined with
// releasedWhenClosed:NO, nothing is freed behind ARC's back.
//
// Obj-C classes cannot live in a C++ namespace, so this sits at file scope.
static bool g_k2_close_requested = false;

@interface K2WindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation K2WindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    g_k2_close_requested = true;
    return NO;
}
@end

// setDelegate: holds a weak reference, so the delegate needs an owner.
static K2WindowDelegate* g_k2_delegate = nil;

namespace k2 {
namespace plat {

namespace {

NSWindow*        g_window  = nil;
NSOpenGLContext* g_context = nil;
NSView*          g_view    = nil;

std::deque<event> g_events;
bool              g_quit = false;
// EVENT_QUIT is edge-triggered. The close conditions below are level-triggered
// (the flag stays set, the window stays hidden), so without this latch every
// poll would push another EVENT_QUIT, main's inner `while (poll_event(e))`
// would never drain, and the outer `while (running)` would never be re-tested.
// The app then hangs instead of quitting - which is exactly what it did.
bool              g_quit_sent = false;

std::chrono::steady_clock::time_point g_start;

int32_t g_drawable_w = 0;
int32_t g_drawable_h = 0;

int32_t g_last_mouse_x = 0;
int32_t g_last_mouse_y = 0;

// A window position in drawable pixels, top-left origin (Cocoa's is
// bottom-left), as platform.h promises.
void mouse_pos(NSEvent* ev, NSView* view, int32_t& x, int32_t& y) {
    const NSPoint bp = [view convertPointToBacking:[ev locationInWindow]];
    const NSRect b = [view convertRectToBacking:[view bounds]];
    x = int32_t(bp.x);
    y = int32_t(b.size.height - bp.y);
}
bool    g_have_mouse   = false;

// audio
AudioUnit      g_audio_unit = NULL;
audio_callback g_audio_cb   = NULL;
void*          g_audio_user = NULL;
bool           g_audio_open = false;

void push(const event& e) { g_events.push_back(e); }

event make_event(event_type t) {
    event e;
    std::memset(&e, 0, sizeof(e));
    e.type = t;
    return e;
}

// macOS virtual keycodes (Carbon kVK_*) -> our position-based enum.
key map_keycode(unsigned short kc) {
    switch (kc) {
        case 0x35: return KEY_ESCAPE;
        case 0x31: return KEY_SPACE;
        case 0x24: return KEY_RETURN;
        case 0x30: return KEY_TAB;
        case 0x33: return KEY_BACKSPACE;
        case 0x7B: return KEY_LEFT;
        case 0x7C: return KEY_RIGHT;
        case 0x7E: return KEY_UP;
        case 0x7D: return KEY_DOWN;
        case 0x38: return KEY_LSHIFT;
        case 0x3C: return KEY_RSHIFT;
        case 0x3B: return KEY_LCTRL;
        case 0x3E: return KEY_RCTRL;
        case 0x3A: return KEY_LALT;
        case 0x3D: return KEY_RALT;

        case 0x00: return KEY_A; case 0x0B: return KEY_B; case 0x08: return KEY_C;
        case 0x02: return KEY_D; case 0x0E: return KEY_E; case 0x03: return KEY_F;
        case 0x05: return KEY_G; case 0x04: return KEY_H; case 0x22: return KEY_I;
        case 0x26: return KEY_J; case 0x28: return KEY_K; case 0x25: return KEY_L;
        case 0x2E: return KEY_M; case 0x2D: return KEY_N; case 0x1F: return KEY_O;
        case 0x23: return KEY_P; case 0x0C: return KEY_Q; case 0x0F: return KEY_R;
        case 0x01: return KEY_S; case 0x11: return KEY_T; case 0x20: return KEY_U;
        case 0x09: return KEY_V; case 0x0D: return KEY_W; case 0x07: return KEY_X;
        case 0x10: return KEY_Y; case 0x06: return KEY_Z;

        case 0x1D: return KEY_0; case 0x12: return KEY_1; case 0x13: return KEY_2;
        case 0x14: return KEY_3; case 0x15: return KEY_4; case 0x17: return KEY_5;
        case 0x16: return KEY_6; case 0x1A: return KEY_7; case 0x1C: return KEY_8;
        case 0x19: return KEY_9;

        case 0x7A: return KEY_F1;  case 0x78: return KEY_F2;  case 0x63: return KEY_F3;
        case 0x76: return KEY_F4;  case 0x60: return KEY_F5;  case 0x61: return KEY_F6;
        case 0x62: return KEY_F7;  case 0x64: return KEY_F8;  case 0x65: return KEY_F9;
        case 0x6D: return KEY_F10; case 0x67: return KEY_F11; case 0x6F: return KEY_F12;
        default:   return KEY_UNKNOWN;
    }
}

void update_drawable_size() {
    if (!g_view) return;
    NSRect bounds = [g_view bounds];
    NSRect px     = [g_view convertRectToBacking:bounds];
    int32_t w = int32_t(px.size.width);
    int32_t h = int32_t(px.size.height);
    if (w != g_drawable_w || h != g_drawable_h) {
        g_drawable_w = w;
        g_drawable_h = h;
        event e = make_event(EVENT_RESIZE);
        e.width = w;
        e.height = h;
        push(e);
        [g_context update];
    }
}

OSStatus audio_render(void* /*ref*/,
                      AudioUnitRenderActionFlags* /*flags*/,
                      const AudioTimeStamp* /*ts*/,
                      UInt32 /*bus*/,
                      UInt32 frames,
                      AudioBufferList* io) {
    float* out = static_cast<float*>(io->mBuffers[0].mData);
    if (g_audio_cb) {
        g_audio_cb(out, int32_t(frames), g_audio_user);
    } else {
        std::memset(out, 0, frames * 2 * sizeof(float));
    }
    return noErr;
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool init(const config& cfg) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(0, 0, cfg.width, cfg.height);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        g_window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        if (!g_window) return false;

        // NSWindow defaults releasedWhenClosed to YES, which predates ARC and
        // conflicts with it: closing the window (by the close button, or by
        // -close in shutdown) releases an object ARC is still holding through
        // g_window. The next -isVisible in poll_event then reads freed memory.
        // That was a hard crash on quit; it is not optional.
        [g_window setReleasedWhenClosed:NO];

        g_k2_close_requested = false;
        g_quit_sent = false;
        g_k2_delegate = [[K2WindowDelegate alloc] init];
        [g_window setDelegate:g_k2_delegate];

        [g_window setTitle:[NSString stringWithUTF8String:(cfg.title ? cfg.title : "k2boy")]];
        [g_window center];

        // macOS tops out at OpenGL 4.1 core. NSOpenGLProfileVersion3_2Core yields
        // a 3.3-capable core context, which is what gfx targets.
        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
            NSOpenGLPFAColorSize,     24,
            NSOpenGLPFAAlphaSize,     8,
            NSOpenGLPFADepthSize,     24,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            0
        };

        NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!pf) {
            NSLog(@"k2boy: no suitable OpenGL 3.2+ core pixel format");
            return false;
        }

        g_context = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
        if (!g_context) return false;

        g_view = [[NSView alloc] initWithFrame:frame];
        [g_view setWantsBestResolutionOpenGLSurface:YES];
        [g_window setContentView:g_view];
        [g_window makeFirstResponder:g_view];

        [g_context setView:g_view];
        [g_context makeCurrentContext];

        GLint interval = cfg.vsync ? 1 : 0;
        [g_context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];

        if (cfg.fullscreen) [g_window toggleFullScreen:nil];

        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp finishLaunching];

        g_start = std::chrono::steady_clock::now();
        g_quit  = false;
        g_events.clear();
        update_drawable_size();
        return true;
    }
}

void shutdown() {
    audio_close();
    @autoreleasepool {
        [NSOpenGLContext clearCurrentContext];
        g_context = nil;
        g_view    = nil;
        if (g_window) {
            [g_window setDelegate:nil];
            [g_window close];
            g_window = nil;
        }
        g_k2_delegate = nil;
    }
}

// GameController framework: Xbox, PlayStation and MFi pads, with the
// extended (two-stick) profile.
void pad_poll(pad_state& out) {
    std::memset(&out, 0, sizeof(out));
    @autoreleasepool {
        NSArray<GCController*>* pads = [GCController controllers];
        for (GCController* c in pads) {
            GCExtendedGamepad* g = [c extendedGamepad];
            if (!g) continue;
            out.connected = true;
            out.axis[PAD_AXIS_LX] = g.leftThumbstick.xAxis.value;
            out.axis[PAD_AXIS_LY] = g.leftThumbstick.yAxis.value;
            out.axis[PAD_AXIS_RX] = g.rightThumbstick.xAxis.value;
            out.axis[PAD_AXIS_RY] = g.rightThumbstick.yAxis.value;
            out.axis[PAD_AXIS_LTRIGGER] = g.leftTrigger.value;
            out.axis[PAD_AXIS_RTRIGGER] = g.rightTrigger.value;
            out.button[PAD_A] = g.buttonA.pressed;
            out.button[PAD_B] = g.buttonB.pressed;
            out.button[PAD_X] = g.buttonX.pressed;
            out.button[PAD_Y] = g.buttonY.pressed;
            out.button[PAD_LSHOULDER] = g.leftShoulder.pressed;
            out.button[PAD_RSHOULDER] = g.rightShoulder.pressed;
            out.button[PAD_DPAD_UP] = g.dpad.up.pressed;
            out.button[PAD_DPAD_DOWN] = g.dpad.down.pressed;
            out.button[PAD_DPAD_LEFT] = g.dpad.left.pressed;
            out.button[PAD_DPAD_RIGHT] = g.dpad.right.pressed;
            if (@available(macOS 10.15, *)) {
                out.button[PAD_START]  = g.buttonMenu.pressed;
                out.button[PAD_SELECT] = g.buttonOptions ? g.buttonOptions.pressed : false;
            }
            if (@available(macOS 10.14.1, *)) {
                out.button[PAD_LSTICK] = g.leftThumbstickButton ? g.leftThumbstickButton.pressed : false;
                out.button[PAD_RSTICK] = g.rightThumbstickButton ? g.rightThumbstickButton.pressed : false;
            }
            break;
        }
    }
}

bool poll_event(event& out) {
    if (g_events.empty()) {
        @autoreleasepool {
            for (;;) {
                NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                 untilDate:[NSDate distantPast]
                                                    inMode:NSDefaultRunLoopMode
                                                   dequeue:YES];
                if (!ev) break;

                switch ([ev type]) {
                    case NSEventTypeKeyDown: {
                        event e = make_event(EVENT_KEY_DOWN);
                        e.k = map_keycode([ev keyCode]);
                        e.repeat = [ev isARepeat] ? true : false;
                        push(e);
                        break;  // swallow: no key-equivalent handling needed yet
                    }
                    case NSEventTypeKeyUp: {
                        event e = make_event(EVENT_KEY_UP);
                        e.k = map_keycode([ev keyCode]);
                        push(e);
                        break;
                    }
                    // Shift, Ctrl and Alt never arrive as key down/up on macOS;
                    // they come as a change of modifier flags, with the key
                    // that changed in keyCode. Without this, run (Shift) and
                    // every other modifier was dead.
                    case NSEventTypeFlagsChanged: {
                        const key k = map_keycode([ev keyCode]);
                        NSEventModifierFlags mask = 0;
                        if (k == KEY_LSHIFT || k == KEY_RSHIFT) mask = NSEventModifierFlagShift;
                        else if (k == KEY_LCTRL || k == KEY_RCTRL) mask = NSEventModifierFlagControl;
                        else if (k == KEY_LALT || k == KEY_RALT) mask = NSEventModifierFlagOption;
                        if (mask) {
                            event e = make_event(([ev modifierFlags] & mask) ? EVENT_KEY_DOWN
                                                                             : EVENT_KEY_UP);
                            e.k = k;
                            push(e);
                        }
                        break;
                    }
                    case NSEventTypeMouseMoved:
                    case NSEventTypeLeftMouseDragged:
                    case NSEventTypeRightMouseDragged: {
                        event e = make_event(EVENT_MOUSE_MOVE);
                        mouse_pos(ev, g_view, e.mouse_x, e.mouse_y);
                        e.mouse_dx = g_have_mouse ? e.mouse_x - g_last_mouse_x : 0;
                        e.mouse_dy = g_have_mouse ? e.mouse_y - g_last_mouse_y : 0;
                        g_last_mouse_x = e.mouse_x;
                        g_last_mouse_y = e.mouse_y;
                        g_have_mouse = true;
                        push(e);
                        [NSApp sendEvent:ev];
                        break;
                    }
                    case NSEventTypeScrollWheel: {
                        event e = make_event(EVENT_MOUSE_WHEEL);
                        // Trackpads report fractional precise deltas; a mouse
                        // wheel reports whole lines. Normalise to "notches".
                        e.wheel = float([ev hasPreciseScrollingDeltas]
                                            ? [ev scrollingDeltaY] * 0.1
                                            : [ev scrollingDeltaY]);
                        push(e);
                        [NSApp sendEvent:ev];
                        break;
                    }
                    case NSEventTypeLeftMouseDown:
                    case NSEventTypeRightMouseDown: {
                        event e = make_event(EVENT_MOUSE_DOWN);
                        mouse_pos(ev, g_view, e.mouse_x, e.mouse_y);
                        e.button = ([ev type] == NSEventTypeLeftMouseDown) ? MOUSE_LEFT
                                                                          : MOUSE_RIGHT;
                        push(e);
                        [NSApp sendEvent:ev];
                        break;
                    }
                    case NSEventTypeLeftMouseUp:
                    case NSEventTypeRightMouseUp: {
                        event e = make_event(EVENT_MOUSE_UP);
                        mouse_pos(ev, g_view, e.mouse_x, e.mouse_y);
                        e.button = ([ev type] == NSEventTypeLeftMouseUp) ? MOUSE_LEFT
                                                                        : MOUSE_RIGHT;
                        push(e);
                        [NSApp sendEvent:ev];
                        break;
                    }
                    default:
                        [NSApp sendEvent:ev];
                        break;
                }
            }

            // Close button (via the delegate), or the window going away by any
            // other route.
            if (!g_quit_sent) {
                if (g_k2_close_requested) g_quit = true;
                if (g_window && ![g_window isVisible]) g_quit = true;
            }
            update_drawable_size();
        }

        if (g_quit) {
            push(make_event(EVENT_QUIT));
            g_quit = false;
            g_quit_sent = true;
        }
    }

    if (g_events.empty()) return false;
    out = g_events.front();
    g_events.pop_front();
    return true;
}

void swap_buffers() {
    [g_context flushBuffer];
}

void make_context_current() {
    [g_context makeCurrentContext];
}

void* gl_get_proc_address(const char* name) {
    // On macOS the GL 3.3 core entry points are linked directly from
    // OpenGL.framework, so gfx does not actually need a loader. Provided anyway
    // so the interface stays honest for platforms that do.
    static void* handle = dlopen(
        "/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL",
        RTLD_LAZY | RTLD_LOCAL);
    return handle ? dlsym(handle, name) : NULL;
}

void drawable_size(int32_t& width, int32_t& height) {
    width  = g_drawable_w;
    height = g_drawable_h;
}

void* native_window_handle() {
    return (__bridge void*)g_window;
}

double time_seconds() {
    std::chrono::duration<double> d = std::chrono::steady_clock::now() - g_start;
    return d.count();
}

// ---------------------------------------------------------------- process

void default_args(std::vector<std::string>& out) { out.clear(); }

std::string user_dir() {
    const char* h = std::getenv("HOME");
    std::string d = std::string(h ? h : ".") + "/.k2boy";
    ::mkdir(d.c_str(), 0755);
    return d;
}

void relaunch(const char* argv0, const std::vector<std::string>& args) {
    std::vector<char*> v;
    v.push_back(const_cast<char*>(argv0));
    for (size_t i = 0; i < args.size(); ++i) v.push_back(const_cast<char*>(args[i].c_str()));
    v.push_back(0);
    ::execv(argv0, &v[0]);
}

// ---------------------------------------------------------------- threads

bool thread_start(thread_fn fn, void* arg, int core, const char* name) {
    (void)core; (void)name;         // macOS has no affinity API worth using
    std::thread(fn, arg).detach();
    return true;
}

int cpu_count() {
    const unsigned n = std::thread::hardware_concurrency();
    return n ? int(n) : 1;
}

// ---------------------------------------------------------------- audio

bool audio_open(int32_t sample_rate, audio_callback cb, void* user) {
    if (g_audio_open) audio_close();

    AudioComponentDescription desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) return false;
    if (AudioComponentInstanceNew(comp, &g_audio_unit) != noErr) return false;

    AudioStreamBasicDescription fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate       = double(sample_rate);
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket  = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = fmt.mChannelsPerFrame * sizeof(float);
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;

    if (AudioUnitSetProperty(g_audio_unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) != noErr) {
        AudioComponentInstanceDispose(g_audio_unit);
        g_audio_unit = NULL;
        return false;
    }

    AURenderCallbackStruct rc;
    rc.inputProc       = audio_render;
    rc.inputProcRefCon = NULL;
    if (AudioUnitSetProperty(g_audio_unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &rc, sizeof(rc)) != noErr) {
        AudioComponentInstanceDispose(g_audio_unit);
        g_audio_unit = NULL;
        return false;
    }

    g_audio_cb   = cb;
    g_audio_user = user;

    if (AudioUnitInitialize(g_audio_unit) != noErr) return false;
    if (AudioOutputUnitStart(g_audio_unit) != noErr) return false;

    g_audio_open = true;
    return true;
}

void audio_close() {
    if (!g_audio_open) return;
    AudioOutputUnitStop(g_audio_unit);
    AudioUnitUninitialize(g_audio_unit);
    AudioComponentInstanceDispose(g_audio_unit);
    g_audio_unit = NULL;
    g_audio_cb   = NULL;
    g_audio_user = NULL;
    g_audio_open = false;
}

} // namespace plat
} // namespace k2
