#!/usr/bin/env python3
"""Guarda las firmas de funciones que el traductor lee del pseudo-C de Ghidra en decomp/game/config/sigs.json
(direccion -> tipo de retorno y clase de cada parametro; sin codigo). Con ese fichero ya no hace falta Ghidra para traducir.
Uso (con decomp/game/pseudo_c/all.c presente): python3 decomp/tools/export_sigs.py"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lift import ghidra_sigs, ROOT
sigs = ghidra_sigs()
json.dump({"%08x" % a: [r, "".join(p)] for a, (r, p) in sorted(sigs.items())}, open(f"{ROOT}/decomp/game/config/sigs.json", "w"), separators=(",", ":"))
print(len(sigs), "firmas")
