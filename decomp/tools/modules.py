#!/usr/bin/env python3
"""Asigna funciones a modulos del SDK segun a que rangos de datos hacen referencia
(el orden de enlazado de .text y .data coincide) y resume rangos contiguos."""
import re, glob
# Rangos de datos por modulo, deducidos de los banners '<< RVL_SDK - X' (inicio) en .data5
mods = [("pre", 0x8135BEA0), ("OS", 0x8135C4B8), ("EXI", 0x8136A008), ("SI", 0x8136A050),
        ("VI", 0x8136A170), ("DVD", 0x8136ACD8), ("NAND", 0x8136C8E0), ("SC", 0x8136CA78),
        ("PAD", 0x8136CCA8), ("post", 0x8136D3A0)]
def mod_of(a):
    r = None
    for n, s in mods:
        if a >= s: r = n
    return r if 0x8135BEA0 <= a < 0x8136D3A0 else None

import collections
funcs = []  # [addr, name, Counter]
cur = None
pending = None
for f in sorted(glob.glob("build/asm/*text.s")):
    for l in open(f):
        m = re.match(r"# \.\w+:0x[0-9A-F]+ \| 0x([0-9A-F]{8}) \|", l)
        if m:
            pending = int(m.group(1), 16); continue
        m = re.match(r"\.fn (\S+),", l)
        if m:
            if m.group(1).startswith("gap_"):
                cur = None; continue
            cur = [pending, m.group(1), collections.Counter()]
            funcs.append(cur); continue
        for m in re.finditer(r"lbl_([0-9A-F]{8})@ha", l):
            mo = mod_of(int(m.group(1), 16))
            if mo and cur: cur[2][mo] += 1
funcs.sort(key=lambda x: x[0])
assign = [c.most_common(1)[0][0] if c else None for a, n, c in funcs]
known = [i for i, v in enumerate(assign) if v]
for i in range(len(assign)):
    if assign[i] is None:
        prev = next((assign[j] for j in range(i-1, -1, -1) if assign[j]), None)
        nxt = next((assign[j] for j in range(i+1, len(assign)) if assign[j]), None)
        assign[i] = prev or nxt
# resume
runs, start = [], 0
for i in range(1, len(funcs)+1):
    if i == len(funcs) or assign[i] != assign[start]:
        a0 = funcs[start][0]; a1 = funcs[i-1][0]
        runs.append((assign[start], a0, a1, i-start)); start = i
for m, a0, a1, n in runs:
    print(f"{m:12} {a0:#010x}-{a1:#010x}  {n:4} funciones")
