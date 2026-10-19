#!/usr/bin/env bash
# Compila el codigo decompilado a PowerPC y ejecuta todas las pruebas diferenciales.
# Uso: tests/difftest/run_all.sh [n_pruebas] [semilla]
set -euo pipefail
cd "$(dirname "$0")/../.."
n="${1:-300}"; seed="${2:-1}"
tests/difftest/build.sh >/dev/null
for t in difftest test_thrown; do
  echo "== $t =="; python3 -I "tests/difftest/$t.py" "$n" "$seed" | tail -6
done
echo "== fuzz (casos manuales) =="; python3 -I tests/difftest/fuzz.py "$n" "$seed" -manual | tail -20
echo "== fuzz (funciones triviales auto-traducidas, $((n<30?n:30)) pruebas cada una) =="; python3 -I tests/difftest/fuzz.py "$((n<30?n:30))" "$seed" auto: -q -auto | tail -5
echo "== overlay (0x817C0000 + trampolines) =="
scripts/build_overlay.sh >/dev/null 2>&1
OVERLAY=1 python3 -I tests/difftest/test_thrown.py "$n" "$seed" | tail -7
OVERLAY=1 python3 -I tests/difftest/fuzz.py "$n" "$seed" | tail -4
