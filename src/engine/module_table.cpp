// module_table.cpp — see module_table.h.
//
// Extracted from the 20 module constructor call sites in pop3dreal.exe. The
// gameModule ctor (0x526213) is called with (name, level, ...); the other three
// modules pass only a name.

#include "engine/module_table.h"

#include <cctype>
#include <cstring>

namespace k2 {
namespace engine {

namespace {

// Sorted by name under strcmpi, so the index is the original's module id.
const module_entry k_modules[] = {
    { "burning",     "geometry\\rooms\\DirigFinale\\DirigFinale.lvl", MODULE_KIND_GAME  },
    { "cistern",     "geometry\\rooms\\Cistern\\cistern.lvl",         MODULE_KIND_GAME  },
    { "cliffs",      "geometry\\rooms\\cliffs\\cliffs.lvl",           MODULE_KIND_GAME  },
    { "docks",       "geometry\\rooms\\cityanddocks\\cityanddocks.lvl", MODULE_KIND_GAME },
    { "finale",      "geometry\\rooms\\finale\\finale.lvl",           MODULE_KIND_GAME  },
    { "lower",       "geometry\\rooms\\dirig1a\\dirig1a.lvl",         MODULE_KIND_GAME  },
    { "lower2",      "geometry\\rooms\\dirig1b\\dirig1b.lvl",         MODULE_KIND_GAME  },
    { "menu",        "",                                              MODULE_KIND_MENU  },
    { "NIS",         "",                                              MODULE_KIND_NIS   },
    { "palace2",     "geometry\\rooms\\palace2\\palace2.lvl",         MODULE_KIND_GAME  },
    { "palace3",     "geometry\\rooms\\palace3\\palace3.lvl",         MODULE_KIND_GAME  },
    { "palace4",     "geometry\\rooms\\palace4\\palace4.lvl",         MODULE_KIND_GAME  },
    { "prison",      "geometry\\rooms\\prisonfix\\prisonfix.lvl",     MODULE_KIND_GAME  },
    { "rooftops",    "geometry\\rooms\\roof1\\roof1.lvl",             MODULE_KIND_GAME  },
    { "ruins1",      "geometry\\rooms\\ruins\\ruins.lvl",             MODULE_KIND_GAME  },
    { "stronghold1", "geometry\\rooms\\moontemple\\moontemple.lvl",   MODULE_KIND_GAME  },
    { "temple",      "geometry\\rooms\\solar1\\solar1.lvl",           MODULE_KIND_GAME  },
    { "title",       "",                                              MODULE_KIND_TITLE },
    { "tower",       "geometry\\rooms\\ivorytwr\\ivorytwr.lvl",       MODULE_KIND_GAME  },
    { "upper",       "geometry\\rooms\\dirig2\\dirig2.lvl",           MODULE_KIND_GAME  }
};

const size_t K_COUNT = sizeof(k_modules) / sizeof(k_modules[0]);

int icmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = std::tolower((unsigned char)*a);
        const int cb = std::tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
}

} // namespace

const module_entry* module_table(size_t& count) {
    count = K_COUNT;
    return k_modules;
}

int32_t module_find(const char* name) {
    if (!name) return -1;
    for (size_t i = 0; i < K_COUNT; ++i)
        if (icmp(k_modules[i].name, name) == 0) return int32_t(i);
    return -1;
}

const module_entry* module_at(int32_t id) {
    if (id < 0 || size_t(id) >= K_COUNT) return 0;
    return &k_modules[size_t(id)];
}

bool module_table_verify(const char** why) {
    // The ordering *is* the id assignment, so a mis-sorted literal would
    // silently renumber every module. Check it rather than trust it.
    for (size_t i = 1; i < K_COUNT; ++i) {
        if (icmp(k_modules[i - 1].name, k_modules[i].name) >= 0) {
            if (why) *why = "table is not in strcmpi order";
            return false;
        }
    }
    const module_entry* s = module_at(MODULE_ID_STARTUP);
    if (!s || icmp(s->name, "prison") != 0) {
        if (why) *why = "MODULE_ID_STARTUP does not resolve to 'prison'";
        return false;
    }
    if (module_find("PRISON") != MODULE_ID_STARTUP) {
        if (why) *why = "lookup is not case-insensitive";
        return false;
    }
    if (why) *why = 0;
    return true;
}

} // namespace engine
} // namespace k2
