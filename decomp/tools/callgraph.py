#!/usr/bin/env python3
"""Rankea funciones por numero de llamadores usando el desensamblado de dtk.
Uso: callgraph.py [N]   (desde decomp/, tras `dtk dol split`)"""
import re, sys, glob, collections

syms = {}  # nombre -> (addr, size)
for l in open("config/symbols.txt"):
    m = re.match(r"(\S+) = \.\w+:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)", l)
    if m:
        syms[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))

callers = collections.defaultdict(set)
cur = None
for f in sorted(glob.glob("build/asm/*.s")):
    for l in open(f):
        m = re.match(r"\.fn (\S+),", l)
        if m:
            cur = m.group(1)
            continue
        m = re.search(r"\sbl (\S+)", l)
        if m and cur:
            callers[m.group(1)].add(cur)

top = int(sys.argv[1]) if len(sys.argv) > 1 else 40
rows = sorted(((len(c), n) for n, c in callers.items()), reverse=True)
print(f"{'callers':>7}  {'size':>6}  {'addr':>10}  nombre")
for n_callers, name in rows[:top]:
    addr, size = syms.get(name, (0, 0))
    print(f"{n_callers:>7}  {size:>6}  {addr:#010x}  {name}")
