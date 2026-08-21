// module_table.js — the 20 modules, in the order that assigns their ids.
//
// Mirrors src/engine/module_table.cpp. The ids are not constants in the
// original: module::module(name, x) at 0x43475C inserts itself into
// dword_7F2A10 sorted case-insensitively by name and stores its own index, so
// a module's id is its position in the sorted list, fixed at static-init time.
// That is why the table has to stay in strcmpi order — see
// docs/plan/05-boot-to-gameplay.md.
//
// WinMain posts 0x0C, which is why the retail game boots straight into the
// dungeon rather than a title screen.

export const MODULE_KIND_GAME = 0;
export const MODULE_KIND_MENU = 1;
export const MODULE_KIND_NIS = 2;
export const MODULE_KIND_TITLE = 3;

export const MODULE_ID_STARTUP = 0x0c;

const MODULES = [
    { name: 'burning', level: 'geometry\\rooms\\DirigFinale\\DirigFinale.lvl', kind: MODULE_KIND_GAME },
    { name: 'cistern', level: 'geometry\\rooms\\Cistern\\cistern.lvl', kind: MODULE_KIND_GAME },
    { name: 'cliffs', level: 'geometry\\rooms\\cliffs\\cliffs.lvl', kind: MODULE_KIND_GAME },
    { name: 'docks', level: 'geometry\\rooms\\cityanddocks\\cityanddocks.lvl', kind: MODULE_KIND_GAME },
    { name: 'finale', level: 'geometry\\rooms\\finale\\finale.lvl', kind: MODULE_KIND_GAME },
    { name: 'lower', level: 'geometry\\rooms\\dirig1a\\dirig1a.lvl', kind: MODULE_KIND_GAME },
    { name: 'lower2', level: 'geometry\\rooms\\dirig1b\\dirig1b.lvl', kind: MODULE_KIND_GAME },
    { name: 'menu', level: '', kind: MODULE_KIND_MENU },
    { name: 'NIS', level: '', kind: MODULE_KIND_NIS },
    { name: 'palace2', level: 'geometry\\rooms\\palace2\\palace2.lvl', kind: MODULE_KIND_GAME },
    { name: 'palace3', level: 'geometry\\rooms\\palace3\\palace3.lvl', kind: MODULE_KIND_GAME },
    { name: 'palace4', level: 'geometry\\rooms\\palace4\\palace4.lvl', kind: MODULE_KIND_GAME },
    { name: 'prison', level: 'geometry\\rooms\\prisonfix\\prisonfix.lvl', kind: MODULE_KIND_GAME },
    { name: 'rooftops', level: 'geometry\\rooms\\roof1\\roof1.lvl', kind: MODULE_KIND_GAME },
    { name: 'ruins1', level: 'geometry\\rooms\\ruins\\ruins.lvl', kind: MODULE_KIND_GAME },
    { name: 'stronghold1', level: 'geometry\\rooms\\moontemple\\moontemple.lvl', kind: MODULE_KIND_GAME },
    { name: 'temple', level: 'geometry\\rooms\\solar1\\solar1.lvl', kind: MODULE_KIND_GAME },
    { name: 'title', level: '', kind: MODULE_KIND_TITLE },
    { name: 'tower', level: 'geometry\\rooms\\ivorytwr\\ivorytwr.lvl', kind: MODULE_KIND_GAME },
    { name: 'upper', level: 'geometry\\rooms\\dirig2\\dirig2.lvl', kind: MODULE_KIND_GAME },
];

const icmp = (a, b) => {
    const la = a.toLowerCase(), lb = b.toLowerCase();
    return la < lb ? -1 : la > lb ? 1 : 0;
};

export const module_table = () => MODULES;
export const module_at = (id) => (id >= 0 && id < MODULES.length ? MODULES[id] : null);
export const module_find = (name) =>
    MODULES.findIndex((m) => icmp(m.name, name) === 0);

// The ordering *is* the id assignment, so a mis-sorted literal would silently
// renumber every module. Check it rather than trust it.
export function module_table_verify() {
    for (let i = 1; i < MODULES.length; ++i)
        if (icmp(MODULES[i - 1].name, MODULES[i].name) >= 0)
            return 'table is not in strcmpi order';

    const s = module_at(MODULE_ID_STARTUP);
    if (!s || icmp(s.name, 'prison') !== 0)
        return 'MODULE_ID_STARTUP does not resolve to \'prison\'';
    if (module_find('PRISON') !== MODULE_ID_STARTUP)
        return 'lookup is not case-insensitive';
    return null;
}
