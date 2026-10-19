# Building and running

> English | [Español](#espanol)

This repository contains **no game data**. You need your own legally obtained copy of
*Castlevania: The Adventure ReBirth* (WiiWare), extracted by you. The native runtime is built by
translating **your** `game.dol` into C++ on your own machine; that generated code is never committed
(it is listed in `.gitignore`).

## 1. Your game files

Put them here (these folders are ignored by git):

```
orig/game.dol          the main executable (SHA-1 69795df60bdeae8faa8905f20f9c8af8455f3c53)
orig/files/            the files of the game's content archive, extracted from your WAD
```

The runtime looks for the extracted files in `../orig/files` (relative to `runtime/`). You can point it
elsewhere with the second command line argument or the `RT_FILES` environment variable.

## 2. Requirements

| Tool | Used for |
|------|----------|
| Python 3 with Pillow | the translator and the helper that builds the system font |
| Ghidra 12 (headless) and a JDK | one-off export of the decompiler output the translator uses for function signatures |
| g++ or clang++ (C++17), make | the runtime |
| SDL2, GLEW, OpenGL 3.3 | window, input, rendering, audio output |

Linux (Arch): `sudo pacman -S base-devel python-pillow sdl2 glew`.
Windows: MSYS2 UCRT64 terminal, `pacman -S mingw-w64-ucrt-x86_64-gcc make pkgconf mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-glew`
and Pillow for Python.

## 3. Export the decompiler output (once)

The translator reads `decomp/game/pseudo_c/all.c`, which comes from your `game.dol`:

```
analyzeHeadless $PWD/decomp/game/ghidra_proj castlevania \
  -import $PWD/orig/game.dol -loader BinaryLoader -processor PowerPC:BE:32:default \
  -noanalysis -overwrite -scriptPath $PWD/decomp/ghidra \
  -postScript ImportDol.java $PWD/orig/game.dol $PWD/decomp/game/config/symbols.txt \
  $PWD/decomp/game/pseudo_c 80006F20 80164800 8031B7A0 8031EF60
```

(`analyzeHeadless` is in Ghidra's `support` folder.) It takes about two minutes.

## 4. Translate and build

```
python3 decomp/tools/lift_native.py        # writes runtime/gen/ from orig/game.dol
python3 scripts/make_osfont.py             # builds the system font from a free TTF on your machine
cd runtime
make -j$(nproc)                            # Linux: build/castlevania
```

On Windows, run the same commands from an MSYS2 UCRT64 terminal; the result is `build-win/castlevania.exe`
(copy `SDL2.dll`, `glew32.dll` and `libwinpthread-1.dll` next to it). `play-windows.bat` starts it.

## 5. Run

```
cd runtime
./build/castlevania ../orig/game.dol       # optional 2nd argument: folder with the extracted files
```

Saves go to `runtime/nand/`. The first boot takes a few seconds.

### Controls

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
FPS counter. Settings are stored in `runtime/settings.cfg`.

### Useful environment variables

`RT_FPS=1` prints frame statistics, `RT_SAMPLE=1` profiles the game thread (Linux and Windows),
`RT_AUDIO=0` disables audio, `RT_HEADLESS=1` runs without a window, `RT_FILES` and `RT_NAND` choose the game
files and save folders. See `PROGRESS.md` for the rest.

## Renaming functions

Most functions still have names like `fn_8000C910`. Readable names are listed in
`decomp/game/config/renames.txt`. Run `python3 decomp/tools/rename_fn.py` to apply them, then translate and
build again.

---

<a id="espanol"></a>

# Compilar y ejecutar

Este repositorio **no contiene datos del juego**. Necesitas tu propia copia legal de
*Castlevania: The Adventure ReBirth* (WiiWare), extraída por ti. El runtime nativo se construye traduciendo
**tu** `game.dol` a C++ en tu máquina; ese código generado nunca se sube (está en `.gitignore`).

## 1. Tus archivos del juego

```
orig/game.dol          el ejecutable principal (SHA-1 69795df60bdeae8faa8905f20f9c8af8455f3c53)
orig/files/            los archivos del contenido del juego, extraídos de tu WAD
```

El runtime busca los archivos extraídos en `../orig/files` (relativo a `runtime/`). Puedes indicar otra carpeta
con el segundo argumento de la línea de comandos o con la variable de entorno `RT_FILES`.

## 2. Requisitos

Python 3 con Pillow, Ghidra 12 (modo headless) con un JDK, g++ o clang++ (C++17), make, SDL2, GLEW y OpenGL 3.3.
En Arch: `sudo pacman -S base-devel python-pillow sdl2 glew`. En Windows: terminal MSYS2 UCRT64 con los
paquetes de la tabla de arriba.

## 3 a 5. Pasos

1. Exporta la salida del decompilador una vez (comando `analyzeHeadless` de la sección 3 en inglés).
2. `python3 decomp/tools/lift_native.py` y `python3 scripts/make_osfont.py`.
3. `cd runtime && make -j$(nproc)` y ejecuta `./build/castlevania ../orig/game.dol`.

Los controles son los de la tabla de arriba (el juego se maneja con el Wii Remote en horizontal). Las
partidas se guardan en `runtime/nand/`.

## Renombrar funciones

La mayoría de las funciones aún se llaman `fn_8000C910`. Los nombres legibles están en
`decomp/game/config/renames.txt`; aplícalos con `python3 decomp/tools/rename_fn.py` y vuelve a traducir y compilar.
