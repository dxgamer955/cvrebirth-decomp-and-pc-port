#!/usr/bin/env bash
# Compila el port nativo a PC de principio a fin a partir de TU copia del juego (Linux o MSYS2 UCRT64 en Windows).
#
#   ./build.sh [juego.wad | carpeta_con_los_.app] [opciones]
#
#   juego.wad       tu WAD de Castlevania: The Adventure ReBirth (se desencripta con libWiiPy)
#   carpeta         o la carpeta de contenido ya desencriptada (p. ej. la de la NAND de Dolphin)
#   (sin argumento) usa orig/game.dol y orig/files si ya existen
#   --install-deps  instala con el gestor de paquetes lo que falte (pacman en MSYS2; en Linux solo muestra el comando)
#   --opt=-O2       nivel de optimizacion (por defecto -O2 en Windows y -O1 en Linux)
#   -jN             trabajos de make (por defecto: todos los nucleos)
#   --run           ejecuta el juego al terminar
#
# Resultado: runtime/build/castlevania (Linux) o runtime/build-win/castlevania.exe (Windows).
set -e
cd "$(dirname "$0")"
ROOT="$PWD"
SRC="${RT_WAD:-}"; OPT=""; JOBS="$(nproc 2>/dev/null || echo 4)"; RUN=0; INSTALL=0
for a in "$@"; do
  case "$a" in
    --install-deps) INSTALL=1 ;;
    --opt=*) OPT="${a#--opt=}" ;;
    -j*) JOBS="${a#-j}" ;;
    --run) RUN=1 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) SRC="$a" ;;
  esac
done
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

MSYS=0; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) MSYS=1 ;; esac
if [ "$MSYS" = 1 ] && [ -n "$SRC" ] && command -v cygpath >/dev/null; then SRC="$(cygpath -u "$SRC")"; fi

# ---- 1. herramientas ----------------------------------------------------------------------------------------------
say "Comprobando herramientas"
missing=()
for t in python3 make g++ pkg-config; do
  if [ "$MSYS" = 1 ] && [ "$t" = python3 ]; then command -v python >/dev/null || missing+=(python); else command -v "$t" >/dev/null || missing+=("$t"); fi
done
if [ "$MSYS" = 1 ]; then
  # Se comprueban comandos y modulos reales (los paquetes pueden llamarse distinto segun la instalacion)
  P=mingw-w64-ucrt-x86_64
  need=""
  command -v g++ >/dev/null || need="$need $P-gcc"
  command -v make >/dev/null || need="$need make"
  command -v pkg-config >/dev/null || need="$need $P-pkgconf"
  command -v python >/dev/null || need="$need $P-python"
  pkg-config --exists sdl2 2>/dev/null || need="$need $P-SDL2"
  pkg-config --exists glew 2>/dev/null || need="$need $P-glew"
  if command -v python >/dev/null; then
    python -c "import PIL" 2>/dev/null || need="$need $P-python-pillow"
    if [ -f "$SRC" ]; then python -c "import Crypto" 2>/dev/null || need="$need $P-python-pycryptodome"; python -c "import requests" 2>/dev/null || need="$need $P-python-requests"; fi   # libWiiPy (solo con un WAD)
  fi
  if [ -n "$need" ]; then
    if [ "$INSTALL" = 1 ]; then say "Instalando con pacman:$need"; pacman -S --needed --noconfirm $need
    else die "faltan en MSYS2:$need
Instalalos con: pacman -S --needed$need
(o vuelve a ejecutar con --install-deps)"; fi
  fi
else
  pkg-config --exists sdl2 glew gl 2>/dev/null || missing+=("SDL2/GLEW/OpenGL (paquetes de desarrollo)")
  python3 -c "import PIL" 2>/dev/null || python3 -c "import venv" 2>/dev/null || missing+=("python3-venv")
  if [ ${#missing[@]} -gt 0 ]; then
    die "faltan: ${missing[*]}
Arch:   sudo pacman -S --needed base-devel python-pillow sdl2 glew
Debian: sudo apt install build-essential pkg-config python3-venv libsdl2-dev libglew-dev
Fedora: sudo dnf install gcc-c++ make pkgconf python3 SDL2-devel glew-devel mesa-libGL-devel"
  fi
fi
PY=python3; [ "$MSYS" = 1 ] && PY=python

# ---- 2. entorno de Python (libWiiPy para el WAD, Pillow para la fuente) ------------------------------------------------
if [ ! -f orig/game.dol ] || [ ! -d orig/files ]; then
  [ -n "$SRC" ] || die "no hay orig/game.dol ni orig/files: indica tu WAD, por ejemplo  ./build.sh \"Castlevania.wad\""
  say "Preparando el entorno de Python (.venv)"
  [ -d .venv ] || $PY -m venv --system-site-packages .venv
  VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe
  if [ -f "$SRC" ]; then "$VPY" -c "import libWiiPy" 2>/dev/null || "$VPY" -m pip install --quiet libWiiPy; fi
  say "Extrayendo el juego de: $SRC"
  "$VPY" decomp/tools/prepare_game.py "$SRC" orig
else
  say "orig/game.dol y orig/files ya existen: se omite la extraccion"
fi
"$PY" -c "import PIL" 2>/dev/null || { [ -d .venv ] || $PY -m venv --system-site-packages .venv; VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe; "$VPY" -m pip install --quiet Pillow; PY="$VPY"; }

# ---- 3. firmas de funciones: sigs.json (incluido en el repositorio) o la exportacion de Ghidra ----------------------------
if [ ! -f decomp/game/pseudo_c/all.c ] && [ ! -f decomp/game/config/sigs.json ]; then
  die "falta decomp/game/config/sigs.json; exporta el pseudo-C con Ghidra (ver BUILD.md) o actualiza el repositorio"
fi

# ---- 4. traducir game.dol a C++ -----------------------------------------------------------------------------------------
say "Traduciendo game.dol a C++ (runtime/gen)"
"$PY" decomp/tools/lift_native.py > /dev/null
say "Generando la fuente del sistema"
"$PY" scripts/make_osfont.py

# ---- 5. compilar --------------------------------------------------------------------------------------------------------
say "Compilando (-j$JOBS)"
cd runtime
if [ -n "$OPT" ]; then make -j"$JOBS" OPT="$OPT"; else make -j"$JOBS"; fi
if [ "$MSYS" = 1 ]; then
  EXE=build-win/castlevania.exe
  for d in SDL2.dll glew32.dll libwinpthread-1.dll; do cp -f "${MINGW_PREFIX:-/ucrt64}/bin/$d" build-win/; done
else
  EXE=build/castlevania
fi
[ -f "$EXE" ] || die "no se genero $EXE"

say "Listo: runtime/$EXE"
if [ "$MSYS" = 1 ]; then echo "Ejecuta:  runtime\\play-windows.bat"; else echo "Ejecuta:  cd runtime && ./build/castlevania ../orig/game.dol"; fi
echo "Controles y opciones: BUILD.md (F3 abre el menu de ajustes)."
if [ "$RUN" = 1 ]; then exec "./$EXE" ../orig/game.dol; fi
