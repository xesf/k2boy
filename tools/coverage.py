#!/usr/bin/env python3
"""Regenerate docs/reference/coverage.md.

Function counts come from an ida-mcp list_funcs dump (see docs/plan/02-ida-mcp-playbook.md):
    list_funcs {"offset":0,"count":0}   -> download the JSON to .ida-mcp/funcs.json
Subsystem status is maintained by hand below - it is a judgement, not a metric.
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# name prefixes applied by this project, as opposed to IDA/FLIRT library naming
OURS = ('vtbl_', 'Script_Register_', 'g_Trap', 'g_Vm', 'g_k2a',
        'MfSc', 'MfPo', 'MfProtoActor__', 'MfString__', 'TrapFactory__',
        'TrapBuilderMap__', 'LevelFileParser__', 'NeighborParser__',
        'LevelGroup__', 'StringParser__', 'FileParser__', 'CommandLineParser__',
        'ChunkRegistry__', 'MfScVM__', 'MfScValue__', 'Door__', 'AudioMessage__',
        'ForceTrap__', 'RoomAttributes__', 'MSVC_string_',
        'NiRTTI', 'module__', 'gameModule__', 'sMenuModule__',
        'Game__', 'LevelLoadRequest__', 'AvatarBody__', 'AnimationNode__',
        'PrinceObject__', 'PrinceStart__', 'EnemyStart__', 'CharacterObject__',
        'K2ArticulatedObject__', 'Trap__', 'Actor__', 'NiStream__',
        'MfProtoNode__', 'Act__', 'Game__', 'Message__', 'Module__', 'Mofac__')

# names this project applied that share no common prefix
OURS_SUFFIX = ('__GetRTTI', '__LoadBinary', '__LinkObject',
               '__RegisterStreamables', '__SaveBinary', '__IsEqual',
               '__scalar_deleting_dtor')

def func_stats():
    p = os.path.join(HERE, '.ida-mcp', 'funcs.json')
    if not os.path.exists(p):
        return None
    blocks = json.load(open(p)).get('result', [])
    seen = {}
    for b in blocks:
        for f in b.get('data', []):
            seen[f['addr']] = f['name']
    total = len(seen)
    auto = sum(1 for n in seen.values()
               if re.match(r'^(sub_|nullsub_|loc_|j_sub_|unknown_)', n))
    ours = sum(1 for n in seen.values()
               if n.startswith(OURS) or n.endswith(OURS_SUFFIX))
    return total, auto, ours, total - auto - ours

FORMATS = [
    # name, status, note
    ('.sfx',              'confirmed', 'header + loop points, validated on every file'),
    ('.K2A',              'confirmed', '77-keyword grammar recovered from the binary'),
    ('.act',              'confirmed', 'property table validated on 115/115 APFM files'),
    ('.lvl',              'confirmed', 'command line + 14 bracketed sections, 20/20 files'),
    ('.nbr',              'confirmed', '16/16 files; cross-checked against savegame'),
    ('.sef',              'confirmed', 'effect name -> nif table'),
    ('.bhvr',             'confirmed', 'trivial; dormant in retail'),
    ('linkmap / loadmap', 'confirmed', 'text object table'),
    ('.mov',              'confirmed', 'QuickTime + SVQ1 + twos/QDM2 identified'),
    ('text / tuning',     'confirmed', 'GameText, Traps.txt, master.dat, fight.ini, enemy.dat, camera.dat'),
    ('.pop savegame',     'partial',   'framing + 5 of 11 sections; stream buffer partial'),
    ('.skl',              'partial',   'JKey record confirmed; 7-float channel meaning open'),
    ('save.bin',          'partial',   'link table understood, state stream not'),
    ('.nif',              'external',  'NetImmerse 2.3, use NifTools schema'),
    ('.pobj',             'partial',   'container only; dormant in retail'),
    ('.fms',              'unknown',   'absent from the install; dormant in retail'),
]

SUBSYSTEMS = [
    ('RTTI class recovery',      100, '909/909 vtables resolved, 853 renamed'),
    ('NiRTTI hierarchy',         100, '169 classes with exact parent links; 169/169 GetRTTI named'),
    ('NiObject root interface',  100, 'all 7 virtuals identified from their bodies; 503 overrides named'),
    ('NIF streaming (code side)', 40, 'the 5-method protocol is understood; the NiStream container is not'),
    ('Trap factory',             100, 'all 33 registry keys + mechanism'),
    ('Chunk registry',           100, '78 types with creator and sizeof'),
    ('Piccolo script API',       100, '29 classes, 250 methods (dormant subsystem)'),
    ('K2A keyword grammar',      100, '77 keywords incl. K2_TRAP_* and TRI_* variant'),
    ('K2A loader (engine code)', 100, 'parser + mesh build; 501/501 parse; per-channel material '
                                      'reading (MAP_DIFFUSE bitmap, MAP_OPACITY = blended)'),
    ('LVL + NBR loaders',        100, 'all 21 levels parse; 18 resolve every path; '
                                      'fog factors are far-clip fractions'),
    ('master.dat loader',        100, '42/42 parse, 0 unresolved geometry or actor'),
    ('ACT skeleton hierarchy',    65, 'transforms are local-to-parent, bind rotations identity, '
                                      'this-adjust resolved (0x14); full reader field order mapped; count-prefixed '
                                      '20-byte records located; node-walk still unknown'),
    ('ACT loader',                75, 'properties 145/145; node layout + write order recovered'),
    ('Mf serialization iface',    90, 'interface, tags, write order, and the +0x14 MI this-adjust'),
    ('Image loaders',            100, 'BMP + TGA, 1035/1035 decode'),
    ('Platform layer (macOS)',    80, 'window, GL 3.3, input, audio sink, VFS; gamepad stubbed'),
    ('Renderer (GL 3.3 core)',    45, 'meshes, textures, state, fullscreen quad; no materials yet'),
    ('Level / neighbour parsers', 90, 'validated; bracketed-section readers not traced'),
    ('Piccolo VM',                40, 'dispatch + machine model; operator mnemonics open'),
    ('Command-line dispatcher',   35, 'tree mapped, ~6 of 18 handlers decoded'),
    ('Savegame',                  45, 'framing + 5 sections'),
    ('Animation (.skl)',          95, 'loader done: 2363 files, 0 failures, 99.89% quats valid'),
    ('CharacterStateTable',       20, 'placed in the hierarchy; base of Prince, camera and Trap. '
                                      'Only 7 of its 51 slots are overridden - 51 slots are the '
                                      'inherited NiNode interface, not states'),
    ('Boot sequence / WinMain',   90, 'entry -> WinMain -> event loop traced; distribution.dat + '
                                      'Tsunami.ttf; distribution.dat parsed; module table decoded - '
                                      'ids are alphabetical, 0x0C = prison = prisonfix.lvl'),
    ('Module state machine',      75, '6-virtual interface, 5 subclasses, slot 3 = event handler; '
                                      '20 modules with names + levels, table + startup id implemented and verified'),
    ('Scene graph (NiNode)',      60, 'transform layout measured from NiAVObject__LoadBinary; 8/8 self-test'),
    ('Actor spawn / New Game',    70, 'chain traced; ported and validated: 134/142 spawn points resolve'),
    ('K2*Activity family',         5, 'identified and named only'),
    ('Motivate planner',           0, 'MfLocoPlanner / MfManipActivity - not started'),

]

def bar(pct, width=20):
    fill = int(round(pct / 100.0 * width))
    return '#' * fill + '.' * (width - fill)

def main():
    out = []
    out.append('# Reverse-engineering coverage\n')
    out.append('Regenerate with `python3 tools/coverage.py`. '
               'Subsystem percentages are judgement calls, not measurements; '
               'the function counts are exact.\n')

    st = func_stats()
    if st:
        total, auto, ours, other = st
        out.append('## Functions in `pop3dreal.exe`\n')
        out.append('| Bucket | Count | Share |')
        out.append('|---|---:|---:|')
        out.append(f'| Total | {total:,} | 100.0% |')
        out.append(f'| Named by this project | {ours:,} | {100.0*ours/total:.1f}% |')
        out.append(f'| Named by IDA / FLIRT (library code) | {other:,} | {100.0*other/total:.1f}% |')
        out.append(f'| Still `sub_*` | {auto:,} | {100.0*auto/total:.1f}% |')
        out.append('')
        out.append('The headline number is deliberately unflattering. Most of the binary is '
                   'NetImmerse and Motivate middleware that does not need naming to be '
                   'reimplemented - see `docs/plan/03-reimplementation.md`. Progress is better '
                   'read per subsystem below.\n')

    out.append('## File formats\n')
    out.append('| Format | Status | Notes |')
    out.append('|---|---|---|')
    order = {'confirmed': 0, 'partial': 1, 'external': 2, 'unknown': 3}
    for n, s, note in sorted(FORMATS, key=lambda r: (order[r[1]], r[0])):
        badge = {'confirmed': '**confirmed**', 'partial': 'partial',
                 'external': 'external', 'unknown': 'unknown'}[s]
        out.append(f'| `{n}` | {badge} | {note} |')
    done = sum(1 for _, s, _ in FORMATS if s == 'confirmed')
    out.append('')
    out.append(f'**{done} of {len(FORMATS)} formats confirmed** '
               f'({100.0*done/len(FORMATS):.0f}%).\n')

    out.append('## Subsystems\n')
    out.append('```')
    for n, p, note in SUBSYSTEMS:
        out.append(f'{n:28} [{bar(p)}] {p:3d}%  {note}')
    out.append('```')
    avg = sum(p for _, p, _ in SUBSYSTEMS) / float(len(SUBSYSTEMS))
    out.append('')
    out.append(f'Unweighted mean across subsystems: **{avg:.0f}%**.\n')
    out.append('## What the numbers say about ordering\n')
    out.append('The three zeroes - `CharacterStateTable`, the Motivate planner, and the '
               'renderer - are the actual game. Everything at 100% is scaffolding that makes '
               'those three tractable. Formats are close to done; behaviour has barely '
               'started.\n')

    path = os.path.join(HERE, 'docs', 'reference', 'coverage.md')
    open(path, 'w').write('\n'.join(out))
    print('wrote', path)

if __name__ == '__main__':
    sys.exit(main())
