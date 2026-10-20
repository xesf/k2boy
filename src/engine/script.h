// script.h — the trap message language that drives level progression.
//
// Traps carry scripts in their argv, for example an Area's enter message:
//
//     prince,snooze:5.3;P1_chambergate04,move,blue,3,.25,0,0;
//     delay,1,camera,tripod,(@AngryGuardCam01)X;P1_chamber2_block04,collide:F;
//
// Recovered from pop3dreal.exe:
//
//   0x454DE5  Trap::Execute     ';'-separated items; a "{state:items}" block
//                               runs only when `state` is the trap's current
//                               state, and only the first matching block runs
//   0x454FE2  (state block)     "state:items" -> compare against Trap +0x148
//   0x4550BD  (send item)       "target,msg": split at the FIRST comma, then
//                               split msg on '|' and send each part
//   0x455198  SendMessage       every trap registered under `target` (a
//                               multimap, unk_7F63F0) gets vslot 0xF4; an
//                               empty target means the sender itself
//   0x4552B7  Trap::Handle      reset, state:X, enable:b, visible:b, collide:b
//   0x469E98  DelayMessage      a trap named "Delay": "t,target,msg" is queued
//   0x46A014  (delay pump)      and sent through 0x4550BD once `t` has passed
//   0x4643AD  Area::Update      enter / action / repeat / exit, see area_trap
//
// No gfx or platform dependency. What a message *does* to the world (draw,
// collide, move) is reported through trap_object's virtual setters, which the
// game layer overrides.
//
// C++11.

#ifndef K2_ENGINE_SCRIPT_H
#define K2_ENGINE_SCRIPT_H

#include <map>
#include <string>
#include <vector>

#include "engine/trap.h"

namespace k2 {
namespace engine {

class script_world;

class script_target {
public:
    explicit script_target(const std::string& name) : m_name(name) {}
    virtual ~script_target() {}
    const std::string& name() const { return m_name; }
    // vslot 0xF4. True when the message was understood.
    virtual bool receive(const std::string& msg, script_target* sender) = 0;
private:
    std::string m_name;
};

// The Trap base: state, flags, and the messages every trap understands.
class trap_object : public script_target {
public:
    trap_object(script_world& w, const std::string& name, const trap_header& h);

    bool receive(const std::string& msg, script_target* sender);

    // 0x4552B7. Derived traps try their own messages first, then call this.
    virtual bool handle(const std::string& msg, script_target* sender);

    // 0x454DE5, with this trap's state selecting "{state:...}" blocks.
    void execute(const std::string& script);

    // The virtual slots 0x4552B7 dispatches to (0xC0, 0x100 .. 0x10C).
    virtual void reset()                   { m_state = m_header.initial_state; }
    virtual void set_state(const std::string& s) { m_state = s; }
    virtual void set_enabled(bool b)       { m_enabled = b; }
    virtual void set_visible(bool b)       { m_visible = b; }
    virtual void set_collideable(bool b)   { m_collideable = b; }

    const std::string& state() const { return m_state; }
    bool enabled() const     { return m_enabled; }
    bool visible() const     { return m_visible; }
    bool collideable() const { return m_collideable; }

protected:
    script_world& m_world;
    trap_header   m_header;
    std::string   m_state;          // +0x148
    bool          m_enabled;
    bool          m_visible;        // +0x159
    bool          m_collideable;    // +0x15A
};

// The volume Areas and pressure plates test characters against
// (0x47517A build, 0x4753D3 test). Not a box: the footprint of the trap
// mesh's up-facing triangles (0x47526C keeps faces whose normal has y > 0),
// extruded over the mesh's height. A point is inside when its y lies within
// the mesh's y range (0x47536F, exact) and its xz falls in any kept face
// (0x47553B with a margin of 0, which is what both traps pass).
class trap_volume {
public:
    void build(const float* xyz, size_t stride, size_t vcount,
               const uint16_t* idx, size_t icount);
    bool contains(const float* p) const;
    bool empty() const { return m_tris.empty(); }
private:
    std::vector<float> m_tris;      // xz of kept faces, 6 floats each
    float              m_min[3], m_max[3];
};

// An Area: a trigger volume. Port of 0x4640EC (argv) and 0x4643AD (update).
//
//   argv1  enter message         +0x18C
//   argv2  exit delay, seconds   +0x1E0   negative: never exits
//   argv3  exit message          +0x19C
//   argv4  action message        +0x1AC   once per visit, on the action input
//   argv5  repeat message        +0x1BC   empty: no repeat
//   argv6  repeat interval       +0x1D0   -1 when argv5 is empty
class area_trap : public trap_object {
public:
    area_trap(script_world& w, const std::string& name, const trap_header& h,
              const std::vector<std::string>& argv, const trap_volume& volume);

    // `inside` is whether any character is in the volume this frame;
    // `action` is that character's action input (flag 0x8000000).
    void update(double now, bool inside, bool action);

    bool contains(const float* p) const { return m_volume.contains(p); }

private:
    std::string m_enter, m_exit, m_action, m_repeat;
    double      m_exit_delay, m_interval;
    trap_volume m_volume;
    bool        m_inside;           // +0x160
    bool        m_acted;            // +0x161
    double      m_next_repeat;      // +0x1D8
    double      m_exit_time;        // +0x1E8
};

// A PressurePlate (0x483925 argv, states 0x483DD2 .. 0x483F04):
//
//   argv1  actor whose "On" / "Off" skills animate the plate (TRAP_PressurePlate)
//   argv2  on message     +0x168
//   argv3  delay, seconds +0x190    negative: stays down for good
//   argv4  off message    +0x178
//
// Idle -> (a character on it) play On -> when On ends run the on message ->
// Held -> (nobody on it) wait `delay` -> play Off -> when Off ends run the off
// message -> Idle. Stepping back on while waiting returns to Held.
class pressure_plate : public trap_object {
public:
    pressure_plate(script_world& w, const std::string& name, const trap_header& h,
                   const std::vector<std::string>& argv, const trap_volume& volume,
                   double on_seconds, double off_seconds);

    void update(double now, bool pressed);
    bool contains(const float* p) const { return m_volume.contains(p); }
    bool down() const { return m_down; }

private:
    enum state_t { IDLE, PRESS_WAIT, HELD, RELEASE, OFF_WAIT, STUCK };
    std::string m_on, m_off;
    double      m_delay;            // +0x190
    double      m_on_len, m_off_len;
    trap_volume m_volume;
    state_t     m_st;
    bool        m_down;             // +0x188
    double      m_t;                // skill start, or release time (+0x198)
};

// Registry, delivery and the delay queue.
class script_world {
public:
    script_world() : trace(false), m_now(0.0) {}

    // Prints every delivery as the original's debug build did (0x455198):
    // "TRAP MSG [<sender> to <target>]: <msg>".
    bool trace;

    void clear();
    void add(script_target* t);         // not owned

    // 0x455198
    void send(const std::string& target, const std::string& msg, script_target* sender);
    // 0x4550BD
    void send_item(const std::string& item, script_target* sender);
    // 0x454DE5 without a trap: no state blocks match.
    void execute(const std::string& script, trap_object* self);

    // DelayMessage: queue `item` for `seconds` from now.
    void delay(double seconds, const std::string& item, script_target* sender);
    // Advances the clock and sends whatever has come due (0x46A014).
    void update(double now);
    double now() const { return m_now; }

    // Messages nobody handled, and targets nobody owns. Counted, not lost.
    const std::map<std::string, int>& unhandled() const { return m_unhandled; }

private:
    struct pending { double at; std::string item; script_target* sender; };

    std::multimap<std::string, script_target*> m_targets;   // lower-case names
    std::vector<pending>                       m_queue;
    double                                     m_now;
    std::map<std::string, int>                 m_unhandled;
};

// 0x47AC58 / 0x47AF7F / 0x47B107: "move,<axis>,distance,T,Ta,Td".
// The axis is a colour, in the exporter's frame: red +x, blue +y, green -z.
// The profile is a trapezoid: accelerate over Ta, cruise, decelerate over Td,
// covering `distance` in T seconds.
struct translate_motion {
    float  dir[3];          // +0x5C, already scaled by the distance
    double distance;        // +0x68
    double v;               // +0x70, set at start: D / (T - (Ta + Td) / 2)
    double total;           // +0x78
    double accel;           // +0x80
    double decel;           // +0x88
    double start;           // +0x18
    bool   ok;

    translate_motion() : distance(0), v(0), total(0), accel(0), decel(0), start(0), ok(false) {
        dir[0] = dir[1] = dir[2] = 0.0f;
    }

    // `args` is the message after "move,". False on a parse error.
    bool parse(const std::string& args);
    void begin(double now);
    // Fraction of the path covered at `now`, 0..1.
    double fraction(double now) const;
    bool done(double now) const { return now - start >= total; }
};

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_SCRIPT_H
