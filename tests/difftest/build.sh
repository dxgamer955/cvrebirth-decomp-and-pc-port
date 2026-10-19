#!/usr/bin/env bash
# Compila el codigo decompilado (src/) a PowerPC y lo enlaza en 0x81000000 con las
# globales y el allocator apuntando a las direcciones reales de game.dol.
# Salida: build/tests/decomp.elf
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/tests; mkdir -p "$out"
objs=()
for f in src/engine/collision.cpp src/engine/CTEngine.cpp $(ls src/gameplay/*.cpp src/auto/*.cpp 2>/dev/null) tests/difftest/stubs.cpp; do
  o="$out/$(basename "$f" .cpp).o"
  clang++ --target=powerpc-unknown-eabi -std=c++03 -O2 -fno-exceptions -fno-rtti \
    -fno-asynchronous-unwind-tables -ffunction-sections -ffp-contract=off -Iinclude -c "$f" -o "$o"
  objs+=("$o")
done
# Funciones elevadas automaticamente (src/auto/lifted/*.cpp, generadas por decomp/tools/lift.py): en paralelo.
if ls src/auto/lifted/*.cpp >/dev/null 2>&1; then
  ls src/auto/lifted/*.cpp | xargs -P "$(nproc)" -I{} sh -c 'clang++ --target=powerpc-unknown-eabi -std=c++03 -O2 -fno-exceptions -fno-rtti \
    -fno-asynchronous-unwind-tables -ffunction-sections -ffp-contract=off -Iinclude -w -c {} -o '"$out"'/$(basename {} .cpp).o'
  for f in src/auto/lifted/*.cpp; do objs+=("$out/$(basename "$f" .cpp).o"); done
fi
# Todos los simbolos de game.dol (funciones y datos) a su direccion real: lo no decompilado
# se enlaza al original.
python3 - <<'PY' > "$out/symbols.ld"
import re
seen = set()
for l in open("decomp/game/config/symbols.txt"):
    m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*) = \.\w+:0x([0-9A-Fa-f]+);", l)
    if m and m.group(1) not in seen:
        seen.add(m.group(1)); print(f"{m.group(1)} = 0x{m.group(2)};")
PY
ld.lld -m elf32ppc -nostdlib -static -Ttext=0x81000000 -Tdata=0x81100000 \
  --defsym=_ZdlPv=0x8015D040 \
  --defsym=__cxa_pure_virtual=0x81001000 \
  --defsym=_Znwm=0x8015CB60 \
  --defsym=g_enDamagedHead=0x802845C0 \
  --defsym=g_ecoScratchHead=0x802845D0 \
  --defsym=g_enRestrictedHead=0x80316470 \
  --defsym=g_enDamagerHead=0x80316474 \
 "$out/symbols.ld" "${objs[@]}" -o "$out/decomp.elf" 2>&1 | head -20
llvm-nm "$out/decomp.elf" | grep -E " [tTdDrR] " | c++filt | head -40 || true
