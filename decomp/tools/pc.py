#!/usr/bin/env python3
"""Consulta el pseudo-C de Ghidra (decomp/game/pseudo_c/all.c).
Uso: pc.py <nombre|0xdireccion|prefijo*> ...   p.ej.  pc.py MTask_dtor  /  pc.py 'ENDamaged_*'"""
import re, sys, fnmatch
t = open("decomp/game/pseudo_c/all.c").read()
parts = re.split(r"^// ==== ([0-9a-f]{8}) (\S+) ====\n", t, flags=re.M)
funcs = {}
for i in range(1, len(parts), 3):
    funcs[(parts[i], parts[i + 1])] = parts[i + 2].strip("\n")
for pat in sys.argv[1:]:
    hit = 0
    for (a, n), body in funcs.items():
        if fnmatch.fnmatch(n, pat) or a == pat.lower().replace("0x", "").zfill(8):
            print(f"// ==== {a} {n} ====\n{body}\n"); hit += 1
    if not hit: print(f"// sin resultados: {pat}")
