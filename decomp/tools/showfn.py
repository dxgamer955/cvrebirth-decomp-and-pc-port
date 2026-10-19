#!/usr/bin/env python3
"""Muestra una o varias funciones del desensamblado de dtk. Uso: showfn.py fn_8133xxxx ..."""
import re, sys, glob
want = set(sys.argv[1:])
for f in sorted(glob.glob("build/asm/*.s")):
    out, on = [], False
    for l in open(f):
        m = re.match(r"\.fn (\S+),", l)
        if m:
            on = m.group(1) in want
        if on:
            l = re.sub(r"/\* ([0-9A-F]+) [0-9A-F]+\s+([0-9A-F ]+?) \*/\t", r"\1  ", l.rstrip())
            out.append(l)
        if l.startswith(".endfn"):
            if on: out.append("")
            on = False
    print("\n".join(out), end="")
