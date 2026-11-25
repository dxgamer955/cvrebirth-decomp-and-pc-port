# Building and running

> English | [Español](#espanol)

This repository contains **no game data**. You need your own legally obtained copy of
*Castlevania: The Adventure ReBirth* (WiiWare), as a `.wad` file. The native runtime is built by
translating **your** game executable into C++ on your own machine; that generated code is never committed
(it is listed in `.gitignore`).

## Quick start

You need your game as a `.wad` file. It can be anywhere on your computer, and there are two ways to give it to the script.

**Option 1: put the WAD in the repository folder (easiest).** Copy your `.wad` next to `build.sh` (the top folder of this
repository, the one that contains `README.md`). Any file name works. Then run the script with no arguments.
The `.wad` is ignored by git, so it will never be committed by mistake.

```
./build.sh                       # Linux
build.bat                        # Windows (double click it)
```

**Option 2: pass the path of the WAD.** Use quotes if the path has spaces.

```
./build.sh "/home/me/Downloads/Castlevania - The Adventure ReBirth (USA) (WiiWare).wad"    # Linux
build.bat "C:\Users\me\Downloads\game.wad"                                              # Windows (or drag the WAD onto build.bat)
```

**Windows only**: install [MSYS2](https://www.msys2.org) once first (step-by-step instructions are in the
[README](README.md#how-to-build-quick-guide)). Add `--install-deps` (for example `build.bat --install-deps`) to let the script
install the missing MSYS2 packages with `pacman`.

The script does everything:

1. decrypts the WAD (with the `libWiiPy` Python package, installed into a local `.venv`; no keys are stored in this repository),
2. extracts `orig/game.dol` (the real executable, which is LZ77 compressed inside the WAD) and `orig/files/` (the game data, a U8 archive),
3. translates `game.dol` to C++ (`runtime/gen/`),
4. builds the system font from a free font on your machine, and
5. compiles the runtime: `runtime/build/castlevania` on Linux, `runtime\build-win\castlevania.exe` on Windows.

Then run it:

```
cd runtime && ./build/castlevania ../orig/game.dol     # Linux
runtime\play-windows.bat                                # Windows
```

Options: `--opt=-O2` (optimization level), `-jN` (parallel jobs), `--run` (start the game when done). If you already have the
decrypted content folder (for example Dolphin's `Wii/title/00010001/<title id>/content/`), pass that folder instead of the WAD.

The SHA-1 of the expected `game.dol` (US release) is `69795df60bdeae8faa8905f20f9c8af8455f3c53`; the script warns if yours differs.

### Requirements

| Tool | Used for |
|------|----------|
| Python 3 | the translator and the extraction scripts (Pillow is used for the font) |
| g++ or clang++ (C++17), make, pkg-config | the runtime |
| SDL2, GLEW, OpenGL 3.3 | window, input, rendering, audio output |

Linux (Arch): `sudo pacman -S --needed base-devel python-pillow sdl2 glew`.
Debian/Ubuntu: `sudo apt install build-essential pkg-config python3-venv libsdl2-dev libglew-dev`.
Windows: MSYS2 (UCRT64 environment); `build.bat --install-deps` installs the rest.


### Windows package (portable folder)

`package-windows.bat` (or `scripts/make_windows_package.sh` in the MSYS2 UCRT64 shell) builds `castlevania.exe` without a console
window and packs it in `build\Castlevania_ReBirth-win64\` plus a `.zip`: the .exe, its DLLs (everything `ldd` finds in MSYS2:
SDL2, GLEW, winpthread), `gen\osfont_ansi.bin` and a `README.txt`. Double-click `castlevania.exe` to play.

- `--embed-game` copies `orig\game.dol` and `orig\files` into the package (ready to play). Without it, copy your `orig` folder next
  to `castlevania.exe`.
- When it is started without arguments, the executable uses the folder it is in: `orig\` for the game, `nand\` for the saves and
  `settings.cfg` for the F3 settings. No `settings.cfg` is shipped, so the FPS counter starts hidden.
- `--console` keeps the console window with the log; otherwise fatal errors are shown in a message box. Other options: `--no-build`,
  `--no-zip`, `--output=DIR`.

The executable contains code translated from your `game.dol` (and `--embed-game` adds the game data): the package is for your
personal use only, do not share or upload it.

### AppImage (Linux)

`scripts/make_appimage.sh` packs the build as `build/Castlevania_ReBirth-x86_64.AppImage`. It bundles the executable, the
system font and the libraries that differ between distributions (SDL2, SDL3 when SDL2 is sdl2-compat, GLEW, GLU); glibc,
libstdc++ and the OpenGL drivers come from the system. It needs `appimagetool` (in `PATH`, `--appimagetool=PATH`, or
`--download-tool` to fetch it from the official AppImage releases into `tools/`).

- Without options the game data is not inside: the AppImage uses the first folder with `game.dol` and `files/` among
  `$CASTLEVANIA_GAME_DIR`, `orig/` next to the AppImage, the AppImage's own folder and `~/.local/share/castlevania-rebirth/game/`.
- `--embed-game` puts `game.dol` and `files/` inside: one self-contained file (~40 MB).
- Saves: `~/.local/share/castlevania-rebirth/nand/` (copy `runtime/nand/*` there to keep your progress). F3 settings:
  `~/.config/castlevania-rebirth/settings.cfg`.
- It runs on distributions with a glibc at least as new as the one it was built on; build it on the oldest one you target.

The executable contains code translated from your `game.dol` (and `--embed-game` adds the game data), so the AppImage is for
your personal use only: do not share or upload it, just like your WAD (ignore appimagetool's suggestion to submit it to appimage.github.io).

## Controls

The game is played with the Wii Remote held sideways.

| Action | Keyboard | Gamepad (Xbox layout) |
|--------|----------|-----------------------|
| Move | Arrows or WASD | D-pad or left stick |
| Jump (button 1) | Z or 1 | X |
| Attack (button 2) | X or 2 | A |
| Button A / B | C / V | LB / RB |
| Plus / Minus | Space or Enter / Backspace | Start / Back |
| HOME menu (pause, settings, reset, quit, credits) | H | Guide button |
| Settings menu | F3 (or HOME menu > Settings) | |
| Unlimited frame rate | F4 | |

The settings menu (F3) has VSync, frame limit, filters, internal resolution (1x to 4x), fullscreen and an
FPS counter (off by default). Settings are stored in `runtime/settings.cfg`. Saves go to `runtime/nand/`.

H opens the port's own HOME menu: it pauses the game and the audio and offers Resume, Save, Load, Settings, Reset (restarts
the game) and Quit, with the build credits. Save and Load use 5 slots with a screenshot each (`runtime/saves/`, next to
`nand/`). A slot keeps the stage, the room you entered last (the last room with a known entrance, e.g. the one before a boss
room) and your lives, score, time, health, hearts, sub-weapon and whip level. Loading restarts the game and takes you there
in a couple of seconds; it does not restore the exact moment, because the recompiled game keeps its CPU state on the native
stack. The Wii's HOME menu needs the console's shared HOME Menu content, which a WAD does not
install, so the game would only show "Could not access Wii system memory". `RT_WII_HOME=1` sends HOME to the game instead
(for testing, with that content extracted to a `shared` folder next to `files`).

### Useful environment variables

`RT_FPS=1` prints frame statistics, `RT_SAMPLE=1` profiles the game thread (Linux and Windows),
`RT_AUDIO=0` disables audio, `RT_HEADLESS=1` runs without a window, `RT_FILES` and `RT_NAND` choose the game
files and save folders. See `PROGRESS.md` for the rest.

## Manual steps (what the scripts do)

```
python3 decomp/tools/prepare_game.py game.wad orig       # or a folder with 00000004.app ... 00000009.app
python3 decomp/tools/lift_native.py                      # writes runtime/gen/ from orig/game.dol
python3 scripts/make_osfont.py                           # system font from a free TTF
python3 scripts/make_uifont.py                           # the game's own font for the port's menus (from orig/files)
cd runtime && make -j$(nproc)
```

The translator needs the function signatures in `decomp/game/config/sigs.json`, which are included. To regenerate them
from your own `game.dol` with Ghidra 12 (optional, only for development):

```
mkdir -p decomp/game/ghidra_proj decomp/game/pseudo_c
analyzeHeadless $PWD/decomp/game/ghidra_proj castlevania \
  -import $PWD/orig/game.dol -loader BinaryLoader -processor PowerPC:BE:32:default \
  -noanalysis -overwrite -scriptPath $PWD/decomp/ghidra \
  -postScript ImportDol.java $PWD/orig/game.dol $PWD/decomp/game/config/symbols.txt \
  $PWD/decomp/game/pseudo_c 80006F20 80164800 8031B7A0 8031EF60
python3 decomp/tools/export_sigs.py
```

## Renaming functions

Most functions still have names like `fn_8000C910`. Readable names are listed in
`decomp/game/config/renames.txt`. Run `python3 decomp/tools/rename_fn.py` to apply them, then run the build again.

---

<a id="espanol"></a>

# Compilar y ejecutar

Este repositorio **no contiene datos del juego**. Necesitas tu propia copia legal de
*Castlevania: The Adventure ReBirth* (WiiWare), como archivo `.wad`. El runtime nativo se construye traduciendo
el ejecutable de **tu** juego a C++ en tu máquina; ese código generado nunca se sube (está en `.gitignore`).

## Inicio rápido

Necesitas tu juego como archivo `.wad`. Puede estar en cualquier sitio y hay dos formas de dárselo al script.

**Opción 1: copia el WAD a la carpeta del repositorio (lo más fácil).** Copia tu `.wad` junto a `build.sh` (la carpeta principal del
repositorio, la que contiene `README.md`); cualquier nombre sirve. Luego ejecuta el script sin argumentos: `./build.sh` en Linux o
`build.bat` en Windows (doble clic). El `.wad` está ignorado por git, así que nunca se sube por error.

**Opción 2: indica la ruta del WAD**, entre comillas si tiene espacios: `./build.sh "/ruta/a/tu/juego.wad"` en Linux, o
`build.bat "C:\ruta\juego.wad"` en Windows (también puedes arrastrar el WAD sobre `build.bat`).

**Solo en Windows:** instala [MSYS2](https://www.msys2.org) una vez antes. Con `--install-deps` el script instala con `pacman` lo
que falte.

El script lo hace todo: desencripta el WAD (con el paquete de Python `libWiiPy`, en un `.venv` local; este repositorio no
guarda ninguna clave), extrae `orig/game.dol` y `orig/files/`, traduce el juego a C++, genera la fuente del sistema y compila.
El resultado es `runtime/build/castlevania` en Linux y `runtime\build-win\castlevania.exe` en Windows. Para jugar:
`cd runtime && ./build/castlevania ../orig/game.dol` (Linux) o `runtime\play-windows.bat` (Windows).

Opciones: `--opt=-O2`, `-jN`, `--run`. Si ya tienes la carpeta de contenido desencriptada (por ejemplo la de Dolphin), pásala en
lugar del WAD. Los controles son los de la tabla de arriba (el juego se maneja con el Wii Remote en horizontal). Las
partidas se guardan en `runtime/nand/`.


### Paquete de Windows (carpeta portable)

`package-windows.bat` (o `scripts/make_windows_package.sh` en la terminal UCRT64 de MSYS2) compila `castlevania.exe` sin ventana de
consola y lo empaqueta en `build\Castlevania_ReBirth-win64\` y un `.zip`: el .exe, sus DLL (todo lo que `ldd` encuentra en MSYS2:
SDL2, GLEW, winpthread), `gen\osfont_ansi.bin` y un `README.txt`. Para jugar, doble clic en `castlevania.exe`.

- `--embed-game` copia `orig\game.dol` y `orig\files` al paquete (listo para jugar). Sin esa opción, copia tu carpeta `orig` junto a
  `castlevania.exe`.
- Si se abre sin argumentos, el ejecutable usa su propia carpeta: `orig\` para el juego, `nand\` para las partidas y `settings.cfg`
  para los ajustes de F3. No se incluye ningún `settings.cfg`, así que el contador de FPS empieza oculto.
- `--console` mantiene la ventana de consola con el registro; si no, los errores graves salen en un cuadro de mensaje. Otras opciones:
  `--no-build`, `--no-zip`, `--output=CARPETA`.

El ejecutable contiene código traducido de tu `game.dol` (y `--embed-game` añade los datos del juego): el paquete es solo para tu
uso personal, no lo compartas ni lo subas.

### AppImage (Linux)

`scripts/make_appimage.sh` empaqueta la compilación como `build/Castlevania_ReBirth-x86_64.AppImage`. Incluye el ejecutable, la
fuente del sistema y las librerías que cambian entre distribuciones (SDL2, SDL3 si SDL2 es sdl2-compat, GLEW, GLU); glibc,
libstdc++ y los drivers de OpenGL vienen del sistema. Necesita `appimagetool` (en el `PATH`, `--appimagetool=RUTA`, o
`--download-tool` para descargarlo de las versiones oficiales de AppImage en `tools/`).

- Sin opciones, los datos del juego no van dentro: el AppImage usa la primera carpeta con `game.dol` y `files/` entre
  `$CASTLEVANIA_GAME_DIR`, `orig/` junto al AppImage, la carpeta del propio AppImage y `~/.local/share/castlevania-rebirth/game/`.
- `--embed-game` mete `game.dol` y `files/` dentro: un único archivo autosuficiente (~40 MB).
- Partidas: `~/.local/share/castlevania-rebirth/nand/` (copia ahí `runtime/nand/*` para conservar tu progreso). Ajustes de F3:
  `~/.config/castlevania-rebirth/settings.cfg`.
- Funciona en distribuciones con una glibc igual o más nueva que la de la máquina donde se generó; genéralo en la más antigua que quieras usar.

El ejecutable contiene código traducido de tu `game.dol` (y `--embed-game` añade los datos del juego), así que el AppImage es solo
para tu uso personal: no lo compartas ni lo subas a ningún sitio, igual que tu WAD (ignora la sugerencia de appimagetool de enviarlo a appimage.github.io).

## Renombrar funciones

La mayoría de las funciones aún se llaman `fn_8000C910`. Los nombres legibles están en
`decomp/game/config/renames.txt`; aplícalos con `python3 decomp/tools/rename_fn.py` y vuelve a compilar.
