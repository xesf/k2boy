#!/usr/bin/env python3
"""Triage the 'unknown' functions of pop3dreal.exe: score each by evidence that it is game code.
Reads docs/reference/function-status.json and .ida-export/c/*.c; writes docs/reference/unknown-triage.json
and prints a summary. Evidence: IDA's string names (aPrince, aMfSoundClearqu...) read as keywords,
calls into needed/ported code, and the callee mix (library-only callers are library).
Run: python3 tools/triage.py"""
import json, os, re, collections
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
st = json.load(open(os.path.join(HERE,'docs/reference/function-status.json')))['functions']
cdir = os.path.join(HERE,'.ida-export','c')
files = {fn[:8]: fn for fn in os.listdir(cdir)}
GAME = re.compile(r'Prince|Guard|Enemy|Fight|Combat|Trap|Pickup|Potion|Door|Skill|Level|Module|Menu|Save|Sound|Sfx|Movie|Health|Sword|Arrow|Bow|Ledge|Climb|Jump|Rope|Crate|Switch|Gate|Aperture|Mover|Force|Action|Area|Difficult|Cheat|Cons|Script|Piccolo|Loco|Manip|Activity|Planner|Tune|Param|Camera|Actor|Avatar|Skel', re.I)
LIB = re.compile(r'Ni[A-Z]|Direct|DX|Mem|Heap|Thread|Mutex|Crit|Float|Locale|Wide|Mbc|Lcmap|Iob|Printf|Scanf|Stream|Fopen|Stdio|Setlocale|Exception|Rtti|Unwind|Alloc', re.I)
out = {}
cnt = collections.Counter()
for a,(s,w,n) in st.items():
    if s != 'unknown': continue
    fn = files.get(a)
    if not fn: out[a] = ['nofile',0,[]]; cnt['nofile'] += 1; continue
    src = open(os.path.join(cdir,fn),errors='replace').read()
    names = set(re.findall(r'\ba([A-Z][A-Za-z0-9_]{3,})\b', src))
    game = sorted(x for x in names if GAME.search(x))
    lib = sorted(x for x in names if LIB.search(x))
    calls = set(c[4:].upper() for c in re.findall(r'\bsub_[0-9A-Fa-f]{6}\b', src))
    cs = collections.Counter(st[c][0] for c in calls if c in st)
    addr = int(a,16)
    score = 0
    if game: score += 2
    if cs['needed'] or cs['ported']: score += 2
    if cs['skip'] and not (cs['needed'] or cs['ported']) and not game: score -= 1
    if lib and not game: score -= 2
    if addr >= 0x540000: score -= 1
    if len(src) < 300: score -= 1
    if len(src) > 1500: score += 1
    cat = 'likely game' if score >= 3 else 'maybe' if score >= 1 else 'likely library'
    out[a] = [cat, len(src), game[:8]]
    cnt[cat] += 1
json.dump(out, open(os.path.join(HERE,'docs/reference/unknown-triage.json'),'w'), separators=(',',':'))
print(dict(cnt))
