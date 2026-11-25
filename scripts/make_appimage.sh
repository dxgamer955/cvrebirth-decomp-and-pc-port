#!/usr/bin/env bash
# Packs the native PC port (Linux x86-64) as an AppImage.
#
#   scripts/make_appimage.sh [options]
#
#   --embed-game          also put the game data (game.dol + files/) inside the AppImage: one self-contained file.
#                         Without it, the AppImage looks for the data when it starts (see "Game data" below).
#   --no-build            pack runtime/build/castlevania as it is (by default it is rebuilt first if needed)
#   --appimagetool=PATH   appimagetool to use (default: in PATH, $APPIMAGETOOL or tools/appimagetool-x86_64.AppImage)
#   --download-tool       if appimagetool is not found, download it from the official AppImage releases on GitHub
#   --output=FILE         output file (default: build/Castlevania_ReBirth-x86_64.AppImage)
#
# Game data, when it is not embedded: the AppImage uses the first folder that has game.dol and files/:
#   $CASTLEVANIA_GAME_DIR, orig/ next to the AppImage, the AppImage's own folder, ~/.local/share/castlevania-rebirth/game
# Saves go to ~/.local/share/castlevania-rebirth/nand and the F3 settings to ~/.config/castlevania-rebirth/settings.cfg.
#
# IMPORTANT: the executable contains code translated from YOUR game.dol (and --embed-game adds the game data), so the
# AppImage is for your personal use only. Do not share or upload it, just like your WAD.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
APP=castlevania-rebirth
EMBED=0; BUILD=1; TOOL="${APPIMAGETOOL:-}"; DOWNLOAD=0; OUT="$ROOT/build/Castlevania_ReBirth-x86_64.AppImage"
for a in "$@"; do
  case "$a" in
    --embed-game) EMBED=1 ;;
    --no-build) BUILD=0 ;;
    --appimagetool=*) TOOL="${a#--appimagetool=}" ;;
    --download-tool) DOWNLOAD=1 ;;
    --output=*) OUT="$(realpath -m "${a#--output=}")" ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $a (see --help)" >&2; exit 1 ;;
  esac
done
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
[ "$(uname -s)" = Linux ] || die "AppImages are for Linux (on Windows use build.bat)"
[ "$(uname -m)" = x86_64 ] || die "only x86-64 is supported"

# ---- 1. executable --------------------------------------------------------------------------------------------------
EXE="$ROOT/runtime/build/castlevania"
if [ "$BUILD" = 1 ]; then
  if [ -f runtime/gen/decls.h ]; then
    say "Updating the build (make)"
    make -C runtime -j"$(nproc)" >/dev/null
  else
    say "No build yet: running build.sh"
    ./build.sh
  fi
fi
[ -x "$EXE" ] || die "$EXE does not exist: run ./build.sh first"
FONT="$ROOT/runtime/gen/osfont_ansi.bin"
[ -f "$FONT" ] || die "$FONT does not exist: run  python3 scripts/make_osfont.py"

# ---- 2. game data (only with --embed-game) --------------------------------------------------------------------------
if [ "$EMBED" = 1 ]; then
  DOL=orig/game.dol; FILES=""
  for d in orig/files tools/ModernGekko-Template/extracted/castlevania-rebirth/files; do [ -d "$d" ] && { FILES="$d"; break; }; done
  [ -f "$DOL" ] && [ -n "$FILES" ] || die "game data not found (orig/game.dol and orig/files): run ./build.sh with your WAD first"
fi

# ---- 3. AppDir ------------------------------------------------------------------------------------------------------
DIR="$ROOT/build/appimage/AppDir"
say "Preparing $DIR"
rm -rf "$DIR"; mkdir -p "$DIR/usr/bin" "$DIR/usr/lib" "$DIR/usr/share/$APP"
cp "$EXE" "$DIR/usr/bin/castlevania"
strip -s "$DIR/usr/bin/castlevania" 2>/dev/null || true
cp "$FONT" "$DIR/usr/share/$APP/osfont_ansi.bin"

# Libraries that differ between distributions (GLEW changes its soname, SDL2 may be missing or be sdl2-compat on top of SDL3).
# The rest (libc, libstdc++, OpenGL/GLX/Mesa, X11, Wayland, audio) comes from the system: bundling the graphics driver stack breaks it.
lib_path() { ldd "$EXE" | awk -v n="$1" '$1 ~ "^"n && !f {print $3; f = 1}'; }     # (awk reads everything: an early exit + pipefail = SIGPIPE)
bundle() { local p; p="$(readlink -f "$1")"; cp "$p" "$DIR/usr/lib/$(basename "$1")"; echo "  $(basename "$1")"; }
for n in libSDL2-2.0.so libGLEW.so libGLU.so; do
  p="$(lib_path "$n")"
  if [ -n "$p" ] && [ -f "$p" ]; then bundle "$p"; fi
done
SDL2="$(lib_path libSDL2-2.0.so)"
if [ -n "$SDL2" ] && grep -aq "libSDL3.so.0" "$SDL2"; then                     # sdl2-compat loads SDL3 with dlopen
  SDL3="$( { ldconfig -p 2>/dev/null || /sbin/ldconfig -p 2>/dev/null; } | awk '/libSDL3\.so\.0 .*x86-64/ && !f {print $NF; f = 1}')"
  if [ -z "$SDL3" ]; then for d in "$(dirname "$SDL2")" /usr/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/local/lib; do
    if [ -f "$d/libSDL3.so.0" ]; then SDL3="$d/libSDL3.so.0"; break; fi; done; fi
  [ -n "$SDL3" ] || die "the system SDL2 is sdl2-compat but libSDL3.so.0 was not found"
  bundle "$SDL3"
fi

if [ "$EMBED" = 1 ]; then
  say "Embedding the game data ($FILES)"
  mkdir -p "$DIR/usr/share/$APP/game"
  cp "$DOL" "$DIR/usr/share/$APP/game/game.dol"
  cp -r "$FILES" "$DIR/usr/share/$APP/game/files"
fi

cat > "$DIR/AppRun" <<'EOF'
#!/bin/sh
# Starts the port: game data, writable folders (saves, settings) and the bundled libraries.
HERE="$(dirname "$(readlink -f "$0")")"
APP=castlevania-rebirth
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/$APP"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/$APP"
mkdir -p "$DATA/nand" "$CONF"
NEXT="${APPIMAGE:+$(dirname "$APPIMAGE")}"
GAME=""
for d in "${CASTLEVANIA_GAME_DIR:-}" "$HERE/usr/share/$APP/game" "${NEXT:+$NEXT/orig}" "$NEXT" "$DATA/game"; do
  if [ -n "$d" ] && [ -f "$d/game.dol" ] && [ -d "$d/files" ]; then GAME="$d"; break; fi
done
if [ -z "$GAME" ]; then
  MSG="Game data not found.

Copy game.dol and the files folder (from orig/ in the repository, after running ./build.sh with your WAD) to:
  $DATA/game/
or put them in an orig/ folder next to the AppImage."
  echo "$MSG" >&2
  if command -v zenity >/dev/null; then zenity --error --title="Castlevania ReBirth" --text="$MSG" 2>/dev/null
  elif command -v kdialog >/dev/null; then kdialog --error "$MSG" 2>/dev/null; fi
  exit 1
fi
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export RT_NAND="${RT_NAND:-$DATA/nand}"
export RT_SETTINGS="${RT_SETTINGS:-$CONF/settings.cfg}"
export RT_OSFONT="${RT_OSFONT:-$HERE/usr/share/$APP/osfont_ansi.bin}"
cd "$DATA" || exit 1
exec "$HERE/usr/bin/castlevania" "$GAME/game.dol" "$GAME/files"
EOF
chmod +x "$DIR/AppRun"

cat > "$DIR/$APP.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Castlevania: The Adventure ReBirth
Comment=Native PC port built from your own copy of the game
Exec=castlevania
Icon=$APP
Categories=Game;ActionGame;
Terminal=false
EOF

# Icon drawn here (a crescent moon), so no artwork from the game is used
python3 - "$DIR/$APP.png" <<'EOF'
import sys, zlib, struct
S = 256; px = bytearray()
for y in range(S):
    px.append(0)
    for x in range(S):
        dx, dy = x - 128, y - 128
        r = (dx * dx + dy * dy) ** 0.5
        cut = ((x - 160) ** 2 + (y - 104) ** 2) ** 0.5
        if r < 120:
            if r < 84 and cut > 70: px += bytes((236, 230, 200, 255))   # moon
            else: px += bytes((44, 18, 58, 255))                          # night circle
        else: px += bytes((0, 0, 0, 0))
c = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" + c(b"IHDR", struct.pack(">IIBBBBB", S, S, 8, 6, 0, 0, 0)) + c(b"IDAT", zlib.compress(bytes(px), 9)) + c(b"IEND", b""))
EOF
ln -s "$APP.png" "$DIR/.DirIcon"

# ---- 4. appimagetool ------------------------------------------------------------------------------------------------
if [ -z "$TOOL" ]; then
  if command -v appimagetool >/dev/null; then TOOL="$(command -v appimagetool)"
  elif [ -x tools/appimagetool-x86_64.AppImage ]; then TOOL="$ROOT/tools/appimagetool-x86_64.AppImage"
  elif [ "$DOWNLOAD" = 1 ]; then
    URL=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    say "Downloading appimagetool from $URL"
    mkdir -p tools
    curl -fL --progress-bar -o tools/appimagetool-x86_64.AppImage.part "$URL" || die "download failed"
    mv tools/appimagetool-x86_64.AppImage.part tools/appimagetool-x86_64.AppImage
    chmod +x tools/appimagetool-x86_64.AppImage
    TOOL="$ROOT/tools/appimagetool-x86_64.AppImage"
  else
    die "appimagetool not found. Install it (Arch: the appimagetool package from the AUR), pass --appimagetool=PATH,
or run again with --download-tool to download it from https://github.com/AppImage/appimagetool/releases into tools/"
  fi
fi

# ---- 5. AppImage ----------------------------------------------------------------------------------------------------
say "Building $OUT"
mkdir -p "$(dirname "$OUT")"
# APPIMAGE_EXTRACT_AND_RUN: appimagetool is itself an AppImage and this way it also works without FUSE
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$DIR" "$OUT"
say "Done: $OUT ($(du -h "$OUT" | cut -f1))"
if [ "$EMBED" = 1 ]; then echo "The game data is inside: run it with  $OUT"
else echo "Game data: put game.dol and files/ in ~/.local/share/$APP/game/ or in orig/ next to the AppImage."; fi
echo "Personal use only: it contains code translated from your game.dol. Do not share it (ignore the appimage.github.io suggestion above)."
