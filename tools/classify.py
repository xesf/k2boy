#!/usr/bin/env python3
"""Classify every function in pop3dreal.exe as skip / ported / needed / unknown.

Writes docs/reference/function-status.json (committed); tools/coverage.py reads it.
Inputs (not committed): .ida-export/index.json and .ida-export/c/*.c (see
docs/plan/02-ida-mcp-playbook.md), .ida-mcp/funcs.json (addr, name, size).

  skip     library or middleware we never port: NetImmerse (Ni*), CRT/STL/MSVC and other
           IDA-named library code, trivial stubs, the dormant Piccolo VM, and any unnamed
           function whose every caller is already skip (propagated to a fixpoint).
  ported   an address our source cites (0x4ADBA0 in a comment) - the function, or the
           function that contains it.
  needed   game code we do not port yet: not skip, not ported, reachable by a static call
           from a ported function or one this project named.
  unknown  the rest - mostly virtual methods nobody calls by name. Triage by hand.

Run: python3 tools/classify.py
"""
import bisect, json, os, re, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, 'tools'))
from coverage import OURS, OURS_SUFFIX          # the project's own name prefixes

AUTO = re.compile(r'^(sub_|nullsub_|loc_|j_sub_|unknown_)')
# Library-ish name shapes: MSVC mangled (?), CRT (_x, __x), thunks (j_), std containers.
LIB_NAME = re.compile(r'^(\?|_|j_|std::|operator|@)')
DORMANT = ('Script_Register_', 'MfSc', 'MfPo')       # the Piccolo script VM: dormant in retail


def load_funcs():
    funcs = {}
    for b in json.load(open(os.path.join(HERE, '.ida-mcp', 'funcs.json')))['result']:
        for x in b['data']:
            funcs[int(x['addr'], 16)] = (x['name'], int(x['size'], 16))
    return funcs


def is_ours(n):
    return n.startswith(OURS) or n.endswith(OURS_SUFFIX)


def seed_skip(name, size):
    if name.startswith('nullsub_') or (AUTO.match(name) and size <= 6):
        return 'stub'
    if name.startswith(DORMANT):
        return 'dormant Piccolo VM'
    if name.startswith('Ni') or '__' in name and name.split('__')[0].startswith('Ni'):
        return 'NetImmerse'
    if LIB_NAME.match(name):
        return 'CRT / STL / thunk'
    if not AUTO.match(name) and not is_ours(name):
        return 'IDA-named library'
    return None


def cited_addresses():
    """Every 0x4xxxxx-0x7xxxxx address in src/ - what we say we ported."""
    out = set()
    for root, _, files in os.walk(os.path.join(HERE, 'src')):
        for fn in files:
            if fn.endswith(('.cpp', '.h', '.mm')):
                for m in re.finditer(r'0x([4-7][0-9A-Fa-f]{5})\b', open(os.path.join(root, fn), errors='replace').read()):
                    out.add(int(m.group(1), 16))
    return out


def call_graph(funcs):
    """callers[addr] -> set(addr), from the pseudocode: any known name or sub_XXXXXX used."""
    by_name = {n: a for a, (n, _) in funcs.items()}
    cdir = os.path.join(HERE, '.ida-export', 'c')
    callees = defaultdict(set)
    word = re.compile(r'[A-Za-z_?@$][\w?@$]*')
    for fn in os.listdir(cdir):
        caller = int(fn[:8], 16)
        for w in set(word.findall(open(os.path.join(cdir, fn), errors='replace').read())):
            m = re.match(r'^sub_([0-9A-Fa-f]{6})$', w)
            a = int(m.group(1), 16) if m else by_name.get(w)
            if a is not None and a != caller:
                callees[caller].add(a)
    callers = defaultdict(set)
    for c, s in callees.items():
        for a in s:
            callers[a].add(c)
    return callees, callers


def vtable_classes(funcs):
    """addr -> class, for every function that sits in a vtable (.ida-export/pop3d.lst)."""
    by_name = {n: a for a, (n, _) in funcs.items()}
    out = {}
    lst = os.path.join(HERE, '.ida-export', 'pop3d.lst')
    label = re.compile(r'^\.rdata:[0-9A-F]{8} \?\?_7(.+?)@@6B')
    entry = re.compile(r' dd offset (\S+)')
    cls = None
    for line in open(lst, errors='replace'):
        if not line.startswith('.rdata:'):
            continue
        m = label.match(line)
        if m:
            cls = m.group(1)
        e = entry.search(line)
        if not e:
            cls = None                  # the table ended
            continue
        if cls:
            a = by_name.get(e.group(1))
            if a is None:
                mm = re.match(r'^sub_([0-9A-Fa-f]{6})$', e.group(1))
                a = int(mm.group(1), 16) if mm else None
            if a is not None:
                out.setdefault(a, cls)
    return out


def main():
    funcs = load_funcs()
    addrs = sorted(funcs)
    status, why = {}, {}

    for a in addrs:
        n, s = funcs[a]
        r = seed_skip(n, s)
        if r:
            status[a], why[a] = 'skip', r

    # Ported: a cited address, or the function that contains it.
    for c in cited_addresses():
        i = bisect.bisect_right(addrs, c) - 1
        if i >= 0:
            a = addrs[i]
            if c - a < max(funcs[a][1], 1) and status.get(a) != 'skip':
                status[a], why[a] = 'ported', 'cited in src/'

    callees, callers = call_graph(funcs)

    # Virtual methods: the class of the vtable they sit in decides.
    vt = vtable_classes(funcs)
    for a, c in vt.items():
        if a in status:
            continue
        if c.startswith('Ni') or c.startswith(('?$', '_')) or '@std' in c:
            status[a], why[a] = 'skip', 'vtable of %s' % c
        elif c.startswith(('MfSc', 'MfPo')):
            status[a], why[a] = 'skip', 'dormant Piccolo VM (%s)' % c

    # A tiny function nobody calls by name (a getter, a thunk, a one-line virtual): not worth
    # a line of its own.
    for a in addrs:
        if a not in status and not callers.get(a) and funcs[a][1] <= 0x10:
            status[a], why[a] = 'skip', 'tiny, no direct callers'

    # Skip propagation: an unclassified function called only from skip code is skip.
    changed = True
    while changed:
        changed = False
        for a in addrs:
            if a in status:
                continue
            cs = callers.get(a)
            if cs and all(status.get(c) == 'skip' for c in cs):
                status[a], why[a] = 'skip', 'only called from skip code'
                changed = True

    # Needed: reachable from ported or project-named game code, and not skip.
    work = [a for a in addrs if status.get(a) == 'ported' or (is_ours(funcs[a][0]) and a not in status)]
    for a in vt:                                # game classes' virtual methods are game code
        if a not in status:
            status[a], why[a] = 'needed', 'vtable of %s' % vt[a]
            work.append(a)
    for a in work:
        if a not in status:
            status[a], why[a] = 'needed', 'project-named, not cited in src/'
    seen = set(work)
    while work:
        a = work.pop()
        for b in callees.get(a, ()):
            if b in seen:
                continue
            seen.add(b)
            if b not in status:
                status[b], why[b] = 'needed', 'called from game code'
            if status[b] != 'skip':
                work.append(b)

    out = {}
    counts = defaultdict(int)
    for a in addrs:
        st = status.get(a, 'unknown')
        counts[st] += 1
        out['%08X' % a] = [st, why.get(a, ''), funcs[a][0]]
    path = os.path.join(HERE, 'docs', 'reference', 'function-status.json')
    json.dump({'counts': counts, 'functions': out}, open(path, 'w'), indent=0, separators=(',', ':'))
    total = len(addrs)
    for k in ('skip', 'ported', 'needed', 'unknown'):
        print('%-8s %6d  %5.1f%%' % (k, counts[k], 100.0 * counts[k] / total))
    print('wrote', path)


if __name__ == '__main__':
    main()
