#!/usr/bin/env bash
# Development helper: regenerates the translated code (only rewrites what changed), builds and runs the native runtime.
# Usage: ./run.sh [seconds]      (variables: LINES_MAX=n output lines, NOMMIO=1 hides the [mmio] lines)
cd "$(dirname "$0")"
[ -f gen/osfont_ansi.bin ] || python3 ../scripts/make_osfont.py
python3 ../decomp/tools/lift_native.py | head -1
make -j"$(nproc)" 2>&1 | grep -E " error|Error "
out=$(timeout -s INT "${1:-20}" ./build/castlevania ../orig/game.dol 2>&1)
if [ -n "$NOMMIO" ]; then out=$(echo "$out" | grep -v '^\[mmio\]'); fi
echo "$out" | tail -${LINES_MAX:-40}
