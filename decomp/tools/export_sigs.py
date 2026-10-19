#!/usr/bin/env python3
"""Saves the function signatures that the translator reads from Ghidra's pseudo-C into decomp/game/config/sigs.json
(address -> return type and class of each parameter; no code). With that file Ghidra is no longer needed to translate.
Usage (with decomp/game/pseudo_c/all.c present): python3 decomp/tools/export_sigs.py"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lift import ghidra_sigs, ROOT
sigs = ghidra_sigs()
json.dump({"%08x" % a: [r, "".join(p)] for a, (r, p) in sorted(sigs.items())}, open(f"{ROOT}/decomp/game/config/sigs.json", "w"), separators=(",", ":"))
print(len(sigs), "signatures")
