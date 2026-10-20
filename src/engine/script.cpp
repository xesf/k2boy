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
                     const std::vector<std::string>& argv, const float bbox[6])
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
    for (int i = 0; i < 6; ++i) m_box[i] = bbox[i];
}

bool area_trap::contains(const float* p) const {
    return p[0] >= m_box[0] && p[0] <= m_box[3] &&
           p[1] >= m_box[1] && p[1] <= m_box[4] &&
           p[2] >= m_box[2] && p[2] <= m_box[5];
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
