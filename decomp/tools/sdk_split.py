#!/usr/bin/env python3
"""Clasifica funciones de un DOL en 'game' o modulo del SDK RVL.
Uso (desde decomp/game): sdk_split.py <dol> [--list modulo]
Idea: el orden de enlazado de .text y .data coincide; los banners '<< RVL_SDK - X'
marcan el inicio de los datos de cada modulo del SDK, y las funciones se asignan
segun a que datos hacen referencia (@ha), rellenando huecos con el vecino."""
import re, sys, glob, subprocess, collections

dol = sys.argv[1]
d = open(dol, "rb").read()
r = subprocess.run(["dtk", "dol", "info", dol], capture_output=True, text=True)
secs = []
for l in (r.stdout + r.stderr).splitlines():
    m = re.match(r"\s*(\S+)\s*\|\s*0x([0-9A-F]+)\s*\|\s*0x([0-9A-F]+)\s*\|\s*0x([0-9A-F]+)", l)
    if m and m.group(1) != "Name":
        secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))
data = next(s for s in secs if s[0] == ".data")
data_lo, data_hi = data[1], data[1] + data[2]

def to_addr(off):
    for n, a, s, o in secs:
        if o and o <= off < o + s and n == ".data":
            return a + off - o
banners = sorted((to_addr(m.start()), m.group(1).decode())
                 for m in re.finditer(rb"<< RVL_SDK - (\w+)", d) if to_addr(m.start()))
marks = [("game", data_lo)] + [(n, a) for a, n in banners]

def mod_of(a):
    if not data_lo <= a < data_hi: return None
    r = None
    for n, s in marks:
        if a >= s: r = n
    return r

funcs, cur, pending = [], None, None
for f in sorted(glob.glob("build/asm/*.s")):
    for l in open(f):
        m = re.match(r"# \.text:0x[0-9A-F]+ \| 0x([0-9A-F]{8}) \| size: 0x([0-9A-F]+)", l)
        if m: pending = (int(m.group(1), 16), int(m.group(2), 16)); continue
        m = re.match(r"\.fn (\S+),", l)
        if m:
            if m.group(1).startswith("gap_") or pending is None: cur = None; continue
            cur = [pending[0], pending[1], m.group(1), collections.Counter()]
            funcs.append(cur); pending = None; continue
        if cur:
            for m in re.finditer(r"lbl_([0-9A-F]{8})@ha", l):
                mo = mod_of(int(m.group(1), 16))
                if mo: cur[3][mo] += 1
funcs.sort(key=lambda x: x[0])
assign = [c.most_common(1)[0][0] if c else None for *_, c in funcs]
for i in range(len(assign)):
    if assign[i] is None:
        assign[i] = next((assign[j] for j in range(i-1, -1, -1) if assign[j]), None) \
                    or next((assign[j] for j in range(i+1, len(assign)) if assign[j]), "game")

if len(sys.argv) > 3 and sys.argv[2] == "--list":
    for (a, sz, n, _), m in zip(funcs, assign):
        if m == sys.argv[3]: print(f"{a:#010x} {sz:6} {n}")
    sys.exit()

tot = collections.defaultdict(lambda: [0, 0])
for (a, sz, n, _), m in zip(funcs, assign):
    tot[m][0] += 1; tot[m][1] += sz
print(f"{'modulo':8} {'funcs':>6} {'bytes':>9}")
for m, (n, b) in sorted(tot.items(), key=lambda x: -x[1][1]):
    print(f"{m:8} {n:6} {b:9}")
runs, s = [], 0
for i in range(1, len(funcs) + 1):
    if i == len(funcs) or assign[i] != assign[s]:
        runs.append((assign[s], funcs[s][0], funcs[i-1][0], i - s)); s = i
print()
for m, a0, a1, n in runs:
    if n >= 15: print(f"{m:6} {a0:#010x}-{a1:#010x} {n:5} funcs")
