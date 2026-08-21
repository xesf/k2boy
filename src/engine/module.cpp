// module.cpp — see module.h.

#include "engine/module.h"

namespace k2 {
namespace engine {

module_manager::module_manager() : m_current(0) {}

module_manager::~module_manager() {
    delete m_current;
    m_current = 0;
}

void module_manager::set(module* m) {
    if (m == m_current) return;
    delete m_current;
    m_current = m;
    if (m_current) m_current->vf0();
}

void module_manager::dispatch_event(const plat::event& e) {
    if (m_current) m_current->on_event(e);
}

void module_manager::frame() {
    if (!m_current) return;
    m_current->vf1();
    m_current->vf2();
    m_current->vf4();
    m_current->vf5();
}

} // namespace engine
} // namespace k2
