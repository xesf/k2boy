// script.cpp — see script.h.

#include "engine/script.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace k2 {
namespace engine {

namespace {

std::string lower(const std::string& s) {
    std::string r(s);
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = char(std::tolower(static_cast<unsigned char>(r[i])));
    return r;
}

bool equals_ci(const std::string& a, const std::string& b) {
    return lower(a) == lower(b);
}

// 0x50BB11: atof, 0.0 when empty.
double to_double(const std::string& s) {
    return s.empty() ? 0.0 : std::atof(s.c_str());
}

// 0x50BDB2: removes and returns the first comma field of `s`.
std::string pop_field(std::string& s) {
    const size_t c = s.find(',');
    std::string head = s.substr(0, c);
    s = (c == std::string::npos) ? std::string() : s.substr(c + 1);
    return head;
}

} // namespace

// --- trap_object --------------------------------------------------------------

trap_object::trap_object(script_world& w, const std::string& name, const trap_header& h)
    : script_target(name), m_world(w), m_header(h), m_state(h.initial_state),
      m_enabled(true), m_visible(h.visible), m_collideable(h.collideable) {}

bool trap_object::receive(const std::string& msg, script_target* sender) {
    return handle(msg, sender);
}

bool trap_object::handle(const std::string& msg, script_target*) {
    if (equals_ci(msg, "reset")) { reset(); return true; }

    // Only "key:value" with exactly two fields is a base message.
    const size_t colon = msg.find(':');
    if (colon == std::string::npos || msg.find(':', colon + 1) != std::string::npos)
        return false;
    const std::string key = msg.substr(0, colon);
    const std::string val = msg.substr(colon + 1);

    if (equals_ci(key, "state")) { set_state(val); return true; }

    bool b = true;
    const bool parsed = parse_trap_bool(val, b);
    // The original logs "Trap bug: Invalid '<key>' message" and still applies
    // the value its lenient reader (0x50BC99) produced; we skip it instead.
    if (equals_ci(key, "enable"))  { if (parsed) set_enabled(b);     return true; }
    if (equals_ci(key, "visible")) { if (parsed) set_visible(b);     return true; }
    if (equals_ci(key, "collide")) { if (parsed) set_collideable(b); return true; }
    return false;
}

void trap_object::execute(const std::string& script) {
    m_world.execute(script, this);
}

// --- area_trap ----------------------------------------------------------------

area_trap::area_trap(script_world& w, const std::string& name, const trap_header& h,
                     const std::vector<std::string>& argv, const trap_volume& volume)
    : trap_object(w, name, h), m_exit_delay(0), m_interval(-1.0), m_inside(false),
      m_acted(false), m_next_repeat(0), m_exit_time(0) {
    const std::string none;
    m_enter      = argv.size() > 1 ? argv[1] : none;
    m_exit_delay = argv.size() > 2 ? to_double(argv[2]) : 0.0;
    m_exit       = argv.size() > 3 ? argv[3] : none;
    m_action     = argv.size() > 4 ? argv[4] : none;
    m_repeat     = argv.size() > 5 ? argv[5] : none;
    // An empty repeat message means no repeat: interval -1 (dbl_744588).
    m_interval   = m_repeat.empty() ? -1.0 : (argv.size() > 6 ? to_double(argv[6]) : 0.0);
    m_volume     = volume;
}

// --- trap_volume ----------------------------------------------------------------

void trap_volume::build(const float* xyz, size_t stride, size_t vcount,
                        const uint16_t* idx, size_t icount) {
    m_tris.clear();
    for (int k = 0; k < 3; ++k) { m_min[k] = 1e30f; m_max[k] = -1e30f; }
    for (size_t v = 0; v < vcount; ++v)
        for (int k = 0; k < 3; ++k) {
            const float c = xyz[v * stride + size_t(k)];
            if (c < m_min[k]) m_min[k] = c;
            if (c > m_max[k]) m_max[k] = c;
        }
    for (size_t i = 0; i + 2 < icount; i += 3) {
        const float* a = xyz + size_t(idx[i]) * stride;
        const float* b = xyz + size_t(idx[i + 1]) * stride;
        const float* c = xyz + size_t(idx[i + 2]) * stride;
        // y of (b - a) x (c - a): keep up-facing faces only.
        const float ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
        if (ny <= 0.0f) continue;
        const float t[6] = { a[0], a[2], b[0], b[2], c[0], c[2] };
        m_tris.insert(m_tris.end(), t, t + 6);
    }
}

bool trap_volume::contains(const float* p) const {
    if (p[1] < m_min[1] || p[1] > m_max[1]) return false;
    if (p[0] < m_min[0] || p[0] > m_max[0] || p[2] < m_min[2] || p[2] > m_max[2]) return false;
    for (size_t i = 0; i + 5 < m_tris.size(); i += 6) {
        const float* t = &m_tris[i];
        // Same side of all three edges, either winding.
        float s[3];
        for (int e = 0; e < 3; ++e) {
            const float ax = t[e * 2], az = t[e * 2 + 1];
            const float bx = t[((e + 1) % 3) * 2], bz = t[((e + 1) % 3) * 2 + 1];
            s[e] = (bx - ax) * (p[2] - az) - (bz - az) * (p[0] - ax);
        }
        if ((s[0] >= 0 && s[1] >= 0 && s[2] >= 0) || (s[0] <= 0 && s[1] <= 0 && s[2] <= 0))
            return true;
    }
    return false;
}

// --- pressure_plate ---------------------------------------------------------------

pressure_plate::pressure_plate(script_world& w, const std::string& name, const trap_header& h,
                               const std::vector<std::string>& argv, const trap_volume& volume,
                               double on_seconds, double off_seconds)
    : trap_object(w, name, h), m_delay(0), m_on_len(on_seconds), m_off_len(off_seconds),
      m_volume(volume), m_st(IDLE), m_down(false), m_t(0) {
    m_on    = argv.size() > 2 ? argv[2] : std::string();
    m_delay = argv.size() > 3 ? to_double(argv[3]) : 0.0;
    m_off   = argv.size() > 4 ? argv[4] : std::string();
}

void pressure_plate::update(double now, bool pressed) {
    // 0x483D53: a disabled plate feels nothing.
    if (!m_enabled) pressed = false;
    switch (m_st) {
    case IDLE:                                      // 0x483DD2 -> 0x483DEC
        if (pressed) { m_down = true; m_t = now; m_st = PRESS_WAIT; }
        break;
    case PRESS_WAIT:                                // 0x483E1E
        if (now - m_t >= m_on_len) {
            execute(m_on);
            m_st = m_delay >= 0.0 ? HELD : STUCK;
        }
        break;
    case HELD:                                      // 0x483E62
        if (!pressed) { m_t = now + m_delay; m_st = RELEASE; }
        break;
    case RELEASE:                                   // 0x483E8E
        if (pressed) { m_t = 0.0; m_st = HELD; }
        else if (now > m_t) { m_down = false; m_t = now; m_st = OFF_WAIT; }
        break;
    case OFF_WAIT:                                  // 0x483ED2 -> 0x483F04
        if (now - m_t >= m_off_len) { execute(m_off); m_st = IDLE; }
        break;
    case STUCK:                                     // state pointer 0
        break;
    }
}

messenger_trap::messenger_trap(script_world& w, const std::string& name, const trap_header& h,
                               const std::vector<std::string>& argv)
    : trap_object(w, name, h) {
    for (size_t i = 1; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        const size_t c = a.find(':');
        if (c == std::string::npos) continue;
        const std::string state = a.substr(0, c);
        size_t at = c + 1;
        for (;;) {                                  // "<msg>:{items}" pairs
            const size_t open = a.find('{', at);
            if (open == std::string::npos) break;
            std::string m = a.substr(at, open - at);
            while (!m.empty() && (m[m.size() - 1] == ':' || m[m.size() - 1] == ' ')) m.erase(m.size() - 1);
            while (!m.empty() && (m[0] == ' ' || m[0] == ';')) m.erase(0, 1);
            int depth = 0;
            size_t close = open;
            for (; close < a.size(); ++close) {
                if (a[close] == '{') ++depth;
                else if (a[close] == '}' && --depth == 0) break;
            }
            entry e;
            e.state = lower(state);
            e.msg = lower(m);
            e.items = a.substr(open + 1, close - open - 1);
            m_entries.push_back(e);
            if (close >= a.size()) break;
            at = close + 1;
        }
    }
}

bool messenger_trap::handle(const std::string& msg, script_target* sender) {
    const std::string st = lower(m_state), m = lower(msg);
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].state == st && m_entries[i].msg == m) {
            if (m_enabled) execute(m_entries[i].items);
            return true;
        }
    return trap_object::handle(msg, sender);
}

action_trap::action_trap(script_world& w, const std::string& name, const trap_header& h,
                         const std::vector<std::string>& argv, const trap_volume& volume)
    : trap_object(w, name, h), m_argv(argv), m_volume(volume) {
    m_center[0] = m_center[1] = m_center[2] = 0.0f;
}

std::string action_trap::pick(field f) const {
    if (size_t(f) >= m_argv.size()) return std::string();
    const std::string& a = m_argv[size_t(f)];
    if (a.find('{') == std::string::npos) return a;
    // "{state:...}" blocks: the one for this state, its body without braces.
    std::string la = a, key = "{" + m_state + ":";
    for (size_t i = 0; i < la.size(); ++i) la[i] = char(std::tolower(static_cast<unsigned char>(la[i])));
    for (size_t i = 0; i < key.size(); ++i) key[i] = char(std::tolower(static_cast<unsigned char>(key[i])));
    const size_t at = la.find(key);
    if (at == std::string::npos) return std::string();
    const size_t start = at + key.size();
    const size_t end = a.find('}', start);
    std::string body = a.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!body.empty() && (body[body.size() - 1] == ';' || body[body.size() - 1] == ' ')) body.erase(body.size() - 1);
    return body;
}

bool action_trap::can_act() const {
    if (!m_enabled) return false;
    for (int f = PRINCE_REACH; f <= ACTION_FINISH; ++f)
        if (!pick(field(f)).empty()) return true;
    return false;
}

bool area_trap::can_act() const {
    if (!m_enabled || !m_inside || m_acted || m_exit_delay < 0.0 || m_action.empty()) return false;
    if (m_action.find('{') == std::string::npos) return true;
    std::string a = m_action, key = "{" + m_state + ":";
    for (size_t i = 0; i < a.size(); ++i) a[i] = char(std::tolower(static_cast<unsigned char>(a[i])));
    for (size_t i = 0; i < key.size(); ++i) key[i] = char(std::tolower(static_cast<unsigned char>(key[i])));
    return a.find(key) != std::string::npos;
}

void area_trap::update(double now, bool inside, bool action) {
    if (!m_enabled) return;

    if (inside) {
        if (!m_inside) {                                 // 0x46453B
            execute(m_enter);
            m_acted  = false;
            m_inside = true;
            m_exit_time = 0.0;
            if (m_interval != -1.0) {                    // 0x46458A
                execute(m_repeat);
                m_next_repeat = now + m_interval;
            }
        }
        if (m_exit_delay >= 0.0 && !m_acted && action) { // 0x464572
            execute(m_action);
            m_acted = true;
        }
    } else if (m_inside) {
        m_next_repeat = 0.0;
        if (m_exit_delay >= 0.0) {
            if (m_exit_delay != 0.0 && m_exit_time == 0.0)
                m_exit_time = now + m_exit_delay;
            if (now >= m_exit_time) {                    // 0x46455A
                execute(m_exit);
                m_inside = false;
                m_exit_time = 0.0;
            }
        }
    }

    if (m_next_repeat != 0.0 && now > m_next_repeat) {
        execute(m_repeat);
        m_next_repeat = now + m_interval;
    }
}

// --- script_world -------------------------------------------------------------

void script_world::clear() {
    m_targets.clear();
    m_queue.clear();
    m_unhandled.clear();
    m_now = 0.0;
}

void script_world::add(script_target* t) {
    m_targets.insert(std::make_pair(lower(t->name()), t));
}

void script_world::send(const std::string& target, const std::string& msg,
                        script_target* sender) {
    if (trace)
        std::printf("TRAP MSG [%s to %s]: %s\n", sender ? sender->name().c_str() : "<NO SOURCE>",
                    target.c_str(), msg.c_str());
    if (target.empty()) {
        if (sender && !sender->receive(msg, sender)) ++m_unhandled["(self)," + msg];
        return;
    }
    const std::string key = lower(target);

    // DelayMessage is a trap named "Delay" in the original; it lives here.
    if (key == "delay") {
        std::string rest = msg;
        const double t = to_double(pop_field(rest));
        delay(t, rest, sender);
        return;
    }

    typedef std::multimap<std::string, script_target*>::iterator it_t;
    std::pair<it_t, it_t> r = m_targets.equal_range(key);
    if (r.first == r.second) { ++m_unhandled["(no target) " + target]; return; }
    for (it_t it = r.first; it != r.second; ++it) {
        if (!it->second->receive(msg, sender)) {
            std::string m = msg;
            ++m_unhandled[target + "," + pop_field(m)];
        }
    }
}

void script_world::send_item(const std::string& item, script_target* sender) {
    const size_t comma = item.find(',');
    if (comma == std::string::npos) {
        ++m_unhandled["(missing comma) " + item];
        return;
    }
    const std::string target = item.substr(0, comma);
    const std::string msgs   = item.substr(comma + 1);
    size_t start = 0;
    for (;;) {
        const size_t bar = msgs.find('|', start);
        send(target, msgs.substr(start, bar == std::string::npos ? std::string::npos : bar - start),
             sender);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
}

void script_world::execute(const std::string& script, trap_object* self) {
    std::string s = script;
    bool matched = false;
    while (!s.empty()) {
        size_t semi;
        if (s[0] == '{') {
            const size_t close = s.find('}');
            if (close == std::string::npos) { ++m_unhandled["(missing '}') " + s]; return; }
            if (!matched && self) {
                // 0x454FE2: "state:items"
                const std::string block = s.substr(1, close - 1);
                const size_t colon = block.find(':');
                if (colon == std::string::npos) {
                    ++m_unhandled["(missing ':') " + block];
                } else if (equals_ci(block.substr(0, colon), self->state())) {
                    execute(block.substr(colon + 1), self);
                    matched = true;
                }
            }
            semi = s.find(';', close);
        } else {
            semi = s.find(';');
            send_item(s.substr(0, semi), self);
        }
        if (semi == std::string::npos) break;
        s = s.substr(semi + 1);
    }
}

void script_world::delay(double seconds, const std::string& item, script_target* sender) {
    pending p;
    p.at = m_now + seconds;
    p.item = item;
    p.sender = sender;
    m_queue.push_back(p);
}

void script_world::update(double now) {
    m_now = now;
    for (;;) {
        // Earliest due item first; ties keep queue order.
        size_t best = m_queue.size();
        for (size_t i = 0; i < m_queue.size(); ++i)
            if (m_queue[i].at <= now && (best == m_queue.size() || m_queue[i].at < m_queue[best].at))
                best = i;
        if (best == m_queue.size()) return;
        const pending p = m_queue[best];
        m_queue.erase(m_queue.begin() + long(best));
        send_item(p.item, p.sender);
    }
}

// --- translate_motion -----------------------------------------------------------

bool translate_motion::parse(const std::string& args) {
    std::string s = args;
    const std::string axis = lower(pop_field(s));
    if (axis == "target") return ok = false;          // moves toward a node: not ported
    distance = to_double(pop_field(s));
    const float d = float(distance);
    dir[0] = dir[1] = dir[2] = 0.0f;
    if      (axis.compare(0, 3, "red") == 0)   dir[0] = d;
    else if (axis.compare(0, 4, "blue") == 0)  dir[1] = d;
    else if (axis.compare(0, 5, "green") == 0) dir[2] = -d;
    else return ok = false;                            // "Bad direction"
    total = to_double(pop_field(s));
    accel = to_double(pop_field(s));
    decel = to_double(pop_field(s));
    // Optional Loop / Crush / HalfCrush follow; they do not change the path.
    return ok = true;
}

void translate_motion::begin(double now) {
    start = now;
    const double cruise = total - (accel + decel) * 0.5;
    v = cruise != 0.0 ? distance / cruise : 0.0;
}

double translate_motion::fraction(double now) const {
    const double t = now - start;
    if (t <= 0.0) return 0.0;
    if (distance == 0.0 || t >= total) return 1.0;
    double s;
    if (t < accel)
        s = v / accel * t * t * 0.5;
    else if (t < total - decel)
        s = v * t - v * accel * 0.5;
    else
        s = distance - v / decel * (total - t) * (total - t) * 0.5;
    return s / distance;
}

} // namespace engine
} // namespace k2
