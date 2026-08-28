// module.cpp — see module.h.

#include "engine/module.h"

#include <cstdio>

namespace k2 {
namespace engine {

module_manager::module_manager()
    : m_current(0), m_current_id(-1), m_previous_id(-1) {}

const char* module_manager::id_name(int32_t id) {
    const module_entry* e = module_at(id);
    return e ? e->name : "<none>";
}

void module_manager::goto_module(int32_t id, module* m) {
    m_previous_id = m_current_id;
    m_current_id  = id;
    std::printf("GOTO:%s FROM %s\n", id_name(m_current_id), id_name(m_previous_id));
    set(m);
}

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
