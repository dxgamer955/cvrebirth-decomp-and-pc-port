# Building and running

> English | [Español](#espanol)

This repository contains **no game data**. You need your own legally obtained copy of
*Castlevania: The Adventure ReBirth* (WiiWare), as a `.wad` file. The native runtime is built by
translating **your** game executable into C++ on your own machine; that generated code is never committed
(it is listed in `.gitignore`).

## Quick start

**Linux**

```
./build.sh "/path/to/Castlevania - The Adventure ReBirth (USA) (WiiWare).wad"
```

**Windows**: install [MSYS2](https://www.msys2.org) once, then drag your `.wad` onto `build.bat` or run

```
build.bat "C:\path\to\game.wad" --install-deps
```

`--install-deps` lets the script install the missing MSYS2 packages with `pacman`.

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

Options: `--opt=-O2` (optimization level), `-jN` (parallel jobs), `--run` (start the game when done). If
`orig/game.dol` and `orig/files/` already exist you can run the script without arguments. If you already have the
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

## Controls

The game is played with the Wii Remote held sideways.

| Action | Keyboard | Gamepad (Xbox layout) |
|--------|----------|-----------------------|
| Move | Arrows or WASD | D-pad or left stick |
| Jump (button 1) | Z or 1 | X |
| Attack (button 2) | X or 2 | A |
| Button A / B | C / V | LB / RB |
| Plus / Minus | Space or Enter / Backspace | Start / Back |
| Settings menu | F3 | Guide button |
| Unlimited frame rate | F4 | |

The settings menu (F3) has VSync, frame limit, filters, internal resolution (1x to 4x), fullscreen and an
FPS counter. Settings are stored in `runtime/settings.cfg`. Saves go to `runtime/nand/`.

### Useful environment variables

`RT_FPS=1` prints frame statistics, `RT_SAMPLE=1` profiles the game thread (Linux and Windows),
`RT_AUDIO=0` disables audio, `RT_HEADLESS=1` runs without a window, `RT_FILES` and `RT_NAND` choose the game
files and save folders. See `PROGRESS.md` for the rest.

## Manual steps (what the scripts do)

```
python3 decomp/tools/prepare_game.py game.wad orig       # or a folder with 00000004.app ... 00000009.app
python3 decomp/tools/lift_native.py                      # writes runtime/gen/ from orig/game.dol
python3 scripts/make_osfont.py                           # system font from a free TTF
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

**Linux:** `./build.sh "/ruta/a/tu/juego.wad"`

**Windows:** instala [MSYS2](https://www.msys2.org) una vez, y luego arrastra tu `.wad` sobre `build.bat` o ejecuta
`build.bat "C:\ruta\juego.wad" --install-deps` (instala con `pacman` lo que falte).

El script lo hace todo: desencripta el WAD (con el paquete de Python `libWiiPy`, en un `.venv` local; este repositorio no
guarda ninguna clave), extrae `orig/game.dol` y `orig/files/`, traduce el juego a C++, genera la fuente del sistema y compila.
El resultado es `runtime/build/castlevania` en Linux y `runtime\build-win\castlevania.exe` en Windows. Para jugar:
`cd runtime && ./build/castlevania ../orig/game.dol` (Linux) o `runtime\play-windows.bat` (Windows).

Opciones: `--opt=-O2`, `-jN`, `--run`. Si ya tienes la carpeta de contenido desencriptada (por ejemplo la de Dolphin), pásala en
lugar del WAD. Los controles son los de la tabla de arriba (el juego se maneja con el Wii Remote en horizontal). Las
partidas se guardan en `runtime/nand/`.

## Renombrar funciones

La mayoría de las funciones aún se llaman `fn_8000C910`. Los nombres legibles están en
`decomp/game/config/renames.txt`; aplícalos con `python3 decomp/tools/rename_fn.py` y vuelve a compilar.
