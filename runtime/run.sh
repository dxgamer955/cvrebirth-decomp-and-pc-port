#!/usr/bin/env bash
# Regenera el codigo elevado (solo reescribe lo que cambia), compila y ejecuta el runtime nativo.
# Uso: ./run.sh [segundos]      (variables: LINES_MAX=n lineas de salida, NOMMIO=1 oculta los [mmio])
cd "$(dirname "$0")"
[ -f gen/osfont_ansi.bin ] || python3 ../scripts/make_osfont.py
python3 ../decomp/tools/lift_native.py | head -1
make -j"$(nproc)" 2>&1 | grep -E " error|Error "
out=$(timeout -s INT "${1:-20}" ./build/castlevania ../orig/game.dol 2>&1)
if [ -n "$NOMMIO" ]; then out=$(echo "$out" | grep -v '^\[mmio\]'); fi
echo "$out" | tail -${LINES_MAX:-40}
