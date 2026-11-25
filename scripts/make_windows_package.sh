#!/usr/bin/env bash
# Builds castlevania.exe and packs it as a portable Windows folder (+ .zip) that runs with a double click.
# Run it from the MSYS2 UCRT64 shell, or simply double-click package-windows.bat in the repository root.
#
#   scripts/make_windows_package.sh [options]
#
#   --embed-game     also copy the game data (orig/game.dol + orig/files) into the package: ready to play.
#                    Without it, copy your orig folder (game.dol + files) next to castlevania.exe yourself.
#   --console        keep the console window with the log (by default the .exe has no console; errors show in a message box)
#   --no-build       pack what is already built (by default the runtime is rebuilt first if needed)
#   --no-zip         only the folder, no .zip
#   --output=DIR     output folder (default: build/Castlevania_ReBirth-win64, and the .zip next to it)
#
# The WAD (only needed when nothing is built yet) is taken like build.sh does: RT_WAD, a .wad next to build.sh or in orig/.
# The FPS counter starts hidden (no settings.cfg is shipped; F3 opens the settings menu, saved next to the .exe).
#
# IMPORTANT: the executable contains code translated from YOUR game.dol (and --embed-game adds the game data), so the
# package is for your personal use only. Do not share or upload it, just like your WAD.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
EMBED=0; GUI=1; BUILD=1; ZIP=1; OUT="$ROOT/build/Castlevania_ReBirth-win64"
for a in "$@"; do
  case "$a" in
    --embed-game) EMBED=1 ;;
    --console) GUI=0 ;;
    --no-build) BUILD=0 ;;
    --no-zip) ZIP=0 ;;
    --output=*) OUT="$(realpath -m "${a#--output=}")" ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    *) echo "unknown option: $a (see --help)" >&2; exit 1 ;;
  esac
done
say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
MSYS=0; case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) MSYS=1 ;; esac
PREFIX="${MINGW_PREFIX:-/ucrt64}"

# ---- 1. build -------------------------------------------------------------------------------------------------------
EXE_NAME=castlevania-gui.exe; [ "$GUI" = 1 ] || EXE_NAME=castlevania.exe
EXE="$ROOT/runtime/build-win/$EXE_NAME"
if [ "$BUILD" = 1 ]; then
  [ "$MSYS" = 1 ] || die "the .exe is built on Windows, in the MSYS2 UCRT64 shell (or double-click package-windows.bat)"
  if [ ! -f runtime/gen/decls.h ] || [ -n "${RT_WAD:-}" ]; then say "Running build.sh"; ./build.sh; fi
  say "Building $EXE_NAME"
  if [ "$GUI" = 1 ]; then make -C runtime -j"$(nproc)" WINGUI=1 >/dev/null; else make -C runtime -j"$(nproc)" >/dev/null; fi
fi
[ -f "$EXE" ] || die "$EXE does not exist: run build.bat first (or drop --no-build)"
FONT="$ROOT/runtime/gen/osfont_ansi.bin"
[ -f "$FONT" ] || die "$FONT does not exist: run build.bat first"

# ---- 2. package folder ----------------------------------------------------------------------------------------------
say "Preparing $OUT"
rm -rf "$OUT"; mkdir -p "$OUT/gen"
cp "$EXE" "$OUT/castlevania.exe"
cp "$FONT" "$OUT/gen/osfont_ansi.bin"

# DLLs: everything ldd resolves inside the MSYS2 prefix (SDL2, GLEW, winpthread...); Windows' own DLLs stay out
dlls="$(ldd "$EXE" 2>/dev/null | awk -v p="$PREFIX/" 'index($3, p) == 1 {print $3}' || true)"
[ -n "$dlls" ] || dlls="$(for d in SDL2.dll glew32.dll libwinpthread-1.dll; do echo "$PREFIX/bin/$d"; done)"
for d in $dlls; do
  [ -f "$d" ] || die "missing DLL: $d"
  cp "$d" "$OUT/"; echo "  $(basename "$d")"
done

if [ "$EMBED" = 1 ]; then
  FILES=""
  for d in orig/files tools/ModernGekko-Template/extracted/castlevania-rebirth/files; do if [ -d "$d" ]; then FILES="$d"; break; fi; done
  [ -f orig/game.dol ] && [ -n "$FILES" ] || die "game data not found (orig/game.dol and orig/files): run build.bat with your WAD first"
  say "Copying the game data ($FILES)"
  mkdir -p "$OUT/orig"
  cp orig/game.dol "$OUT/orig/game.dol"
  cp -r "$FILES" "$OUT/orig/files"
fi

# Instructions (CRLF so Notepad shows them right)
sed 's/$/\r/' > "$OUT/README.txt" <<'EOF'
Castlevania: The Adventure ReBirth - native PC port (built from your own copy of the game)

PLAY: double-click castlevania.exe.

Game data: the orig folder next to castlevania.exe must contain game.dol and the files folder
(they are in orig\ of the repository after running build.bat with your WAD).

Controls: arrows/WASD move, Z or 1 jump, X or 2 attack, Enter = +, Backspace = -.
H (or the controller's guide button) opens the HOME menu: resume, save, load (5 slots with a screenshot,
in the saves folder), settings, reset, quit and credits.
A game controller also works. F3 opens the settings menu (FPS counter, VSync, window/fullscreen,
resolution, filters). F4 toggles the 60 FPS limit.
Saves: the nand folder. Settings: settings.cfg (created when you change something with F3).

PERSONAL USE ONLY: castlevania.exe contains code translated from your game.dol. Do not share or upload
this folder, just like your WAD.

------------------------------------------------------------------------------------------------------

JUGAR: doble clic en castlevania.exe.

Datos del juego: la carpeta orig junto a castlevania.exe debe contener game.dol y la carpeta files
(estan en orig\ del repositorio despues de ejecutar build.bat con tu WAD).

Controles: flechas/WASD mover, Z o 1 saltar, X o 2 atacar, Intro = +, Retroceso = -.
H (o el boton central del mando) abre el menu HOME: volver, guardar, cargar (5 ranuras con captura,
en la carpeta saves), ajustes, reiniciar, salir y creditos.
Tambien funciona un mando. F3 abre el menu de ajustes (contador de FPS, VSync, ventana/pantalla completa,
resolucion, filtros). F4 activa/desactiva el limite de 60 FPS.
Partidas: la carpeta nand. Ajustes: settings.cfg (se crea al cambiar algo con F3).

SOLO PARA USO PERSONAL: castlevania.exe contiene codigo traducido de tu game.dol. No compartas ni subas
esta carpeta, igual que tu WAD.
EOF

# ---- 3. zip ---------------------------------------------------------------------------------------------------------
if [ "$ZIP" = 1 ]; then
  Z="$OUT.zip"; rm -f "$Z"
  if [ "$MSYS" = 1 ] && command -v powershell.exe >/dev/null; then
    say "Compressing $(basename "$Z")"
    powershell.exe -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$OUT")' -DestinationPath '$(cygpath -w "$Z")' -Force"
  elif command -v zip >/dev/null; then
    say "Compressing $(basename "$Z")"
    (cd "$(dirname "$OUT")" && zip -qr "$Z" "$(basename "$OUT")")
  else
    echo "(no zip tool found: only the folder was created)"
  fi
fi

say "Done: $OUT"
[ "$EMBED" = 1 ] && echo "Ready to play: double-click castlevania.exe" || echo "Copy your orig folder (game.dol + files) next to castlevania.exe, then double-click it."
echo "Personal use only: it contains code translated from your game.dol. Do not share it."
