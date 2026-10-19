#!/usr/bin/env bash
# Builds the native PC port end to end from YOUR copy of the game (Linux, or the MSYS2 UCRT64 shell on Windows).
#
#   ./build.sh [game.wad | folder_with_the_.app_files] [options]
#
#   game.wad        path to your WAD of Castlevania: The Adventure ReBirth (decrypted with libWiiPy). Use quotes if the path has spaces.
#                   If you omit it, the script looks for a *.wad next to this script or in orig/
#   folder          or a folder with the already decrypted content (for example Dolphin's NAND content folder)
#   (no argument)   uses orig/game.dol and orig/files if they already exist, otherwise looks for a .wad as described above
#   --install-deps  install what is missing with the package manager (pacman on MSYS2; on Linux it only prints the command)
#   --opt=-O2       optimization level (default: -O2 on Windows, -O1 on Linux)
#   -jN             make jobs (default: all cores)
#   --run           run the game when the build finishes
#
# Result: runtime/build/castlevania (Linux) or runtime/build-win/castlevania.exe (Windows).
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
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) SRC="$a" ;;
  esac
done
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

MSYS=0; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) MSYS=1 ;; esac
if [ "$MSYS" = 1 ] && [ -n "$SRC" ] && command -v cygpath >/dev/null; then SRC="$(cygpath -u "$SRC")"; fi

# ---- 1. tools -----------------------------------------------------------------------------------------------------
say "Checking tools"
missing=()
for t in python3 make g++ pkg-config; do
  if [ "$MSYS" = 1 ] && [ "$t" = python3 ]; then command -v python >/dev/null || missing+=(python); else command -v "$t" >/dev/null || missing+=("$t"); fi
done
if [ "$MSYS" = 1 ]; then
  # Check real commands and modules (the package names can differ between installations)
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
    if [ -f "$SRC" ]; then python -c "import Crypto" 2>/dev/null || need="$need $P-python-pycryptodome"; python -c "import requests" 2>/dev/null || need="$need $P-python-requests"; fi   # libWiiPy (only needed with a WAD)
  fi
  if [ -n "$need" ]; then
    if [ "$INSTALL" = 1 ]; then say "Installing with pacman:$need"; pacman -S --needed --noconfirm $need
    else die "missing in MSYS2:$need
Install them with: pacman -S --needed$need
(or run again with --install-deps)"; fi
  fi
else
  pkg-config --exists sdl2 glew gl 2>/dev/null || missing+=("SDL2/GLEW/OpenGL (development packages)")
  python3 -c "import PIL" 2>/dev/null || python3 -c "import venv" 2>/dev/null || missing+=("python3-venv")
  if [ ${#missing[@]} -gt 0 ]; then
    die "missing: ${missing[*]}
Arch:   sudo pacman -S --needed base-devel python-pillow sdl2 glew
Debian: sudo apt install build-essential pkg-config python3-venv libsdl2-dev libglew-dev
Fedora: sudo dnf install gcc-c++ make pkgconf python3 SDL2-devel glew-devel mesa-libGL-devel"
  fi
fi
PY=python3; [ "$MSYS" = 1 ] && PY=python

# If no WAD was given and the game is not extracted yet, look for a .wad next to this script or in orig/
if [ -z "$SRC" ] && { [ ! -f orig/game.dol ] || [ ! -d orig/files ]; }; then
  for f in ./*.wad orig/*.wad; do [ -f "$f" ] && { SRC="$f"; say "Found a WAD: $f"; break; }; done
fi

# ---- 2. Python environment (libWiiPy for the WAD, Pillow for the font) and game extraction -------------------------------
if [ ! -f orig/game.dol ] || [ ! -d orig/files ]; then
  [ -n "$SRC" ] || die "no game found. Put your .wad in this folder (next to build.sh) and run ./build.sh again, or pass its path:  ./build.sh \"/path/to/game.wad\""
  say "Preparing the Python environment (.venv)"
  [ -d .venv ] || $PY -m venv --system-site-packages .venv
  VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe
  if [ -f "$SRC" ]; then "$VPY" -c "import libWiiPy" 2>/dev/null || "$VPY" -m pip install --quiet libWiiPy; fi
  say "Extracting the game from: $SRC"
  "$VPY" decomp/tools/prepare_game.py "$SRC" orig
else
  say "orig/game.dol and orig/files already exist: skipping extraction"
fi
"$PY" -c "import PIL" 2>/dev/null || { [ -d .venv ] || $PY -m venv --system-site-packages .venv; VPY=.venv/bin/python; [ -x "$VPY" ] || VPY=.venv/Scripts/python.exe; "$VPY" -m pip install --quiet Pillow; PY="$VPY"; }

# ---- 3. function signatures: sigs.json (shipped in the repository) or a Ghidra export -------------------------------------
if [ ! -f decomp/game/pseudo_c/all.c ] && [ ! -f decomp/game/config/sigs.json ]; then
  die "decomp/game/config/sigs.json is missing; export the pseudo-C with Ghidra (see BUILD.md) or update the repository"
fi

# ---- 4. translate game.dol to C++ ----------------------------------------------------------------------------------------
say "Translating game.dol to C++ (runtime/gen)"
"$PY" decomp/tools/lift_native.py > /dev/null
say "Generating the system font"
"$PY" scripts/make_osfont.py

# ---- 5. build ------------------------------------------------------------------------------------------------------------
say "Building (-j$JOBS)"
cd runtime
if [ -n "$OPT" ]; then make -j"$JOBS" OPT="$OPT"; else make -j"$JOBS"; fi
if [ "$MSYS" = 1 ]; then
  EXE=build-win/castlevania.exe
  for d in SDL2.dll glew32.dll libwinpthread-1.dll; do cp -f "${MINGW_PREFIX:-/ucrt64}/bin/$d" build-win/; done
else
  EXE=build/castlevania
fi
[ -f "$EXE" ] || die "$EXE was not produced"

say "Done: runtime/$EXE"
if [ "$MSYS" = 1 ]; then echo "Run:  runtime\\play-windows.bat"; else echo "Run:  cd runtime && ./build/castlevania ../orig/game.dol"; fi
echo "Controls and options: BUILD.md (F3 opens the settings menu)."
if [ "$RUN" = 1 ]; then exec "./$EXE" ../orig/game.dol; fi
