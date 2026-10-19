#!/usr/bin/env python3
"""Applies decomp/game/config/renames.txt: replaces each fn_XXXXXXXX with its readable name in symbols.txt and in the runtime code."""
import re, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[2]
ren = {}
for line in (root / "decomp/game/config/renames.txt").read_text().splitlines():
    line = line.split("#", 1)[0].strip()
    if line:
        old, new = line.split()[:2]
        ren[old] = new
pat = re.compile(r"\b(" + "|".join(map(re.escape, ren)) + r")\b")
targets = [root / "decomp/game/config/symbols.txt", root / "tests/difftest/lift_status.txt", root / "PROGRESS.md"] + sorted((root / "runtime/src").glob("*.cpp")) + sorted((root / "runtime/include").glob("*.h"))
total = 0
for t in targets:
    if not t.exists(): continue
    s = t.read_text()
    s2, n = pat.subn(lambda m: ren[m.group(1)], s)
    if n: t.write_text(s2); total += n; print(f"{t.relative_to(root)}: {n}")
print(f"{total} replacements")
