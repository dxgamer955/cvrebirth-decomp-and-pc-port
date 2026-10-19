# Castlevania: The Adventure ReBirth - Decompilation & PC Port

> English | [Español](#espanol)

> **Status: work in progress.** This repository hosts the tools and the native runtime of a PC port of
> *Castlevania: The Adventure ReBirth* (WiiWare, 2009), and the readable, documented decompilation that is being
> written on top of it. The port runs the game up to Stage 1; copy your own `.wad` next to `build.sh` and run it (`build.bat` on Windows) to build it; see [BUILD.md](BUILD.md).

## What is this project?

*Castlevania: The Adventure ReBirth* is a 2D remake of the original *Castlevania: The Adventure* (Game Boy),
developed by Konami / M2 for WiiWare. This project aims to:

1. **Decompile** the game (`game.dol`, PowerPC / Metrowerks CodeWarrior) into **readable, documented C/C++**
   that can be rebuilt and is understandable by humans.
2. Build a **native PC port** on top of it (SDL2 + OpenGL), without an emulator, with modern options
   (any resolution, VSync, unlocked frame rate, filters, keyboard / gamepad input...).
3. Document the game's engine, file formats (`.psb`, `.brsar`, `.brstm`...) and the Wii SDK pieces it uses, so the
   work can serve as a reference for similar projects.

## Current state

Where it stands:

- Done: The whole of `game.dol` is lifted to C++ and **runs natively on x86-64 Linux**, with no emulator:
  boot, threads, timers, file/NAND access, intro, title screen, map and Stage 1.
- Done: **Native renderer** (OpenGL): GX to OpenGL, texture decoding, TEV shaders, internal resolution up to 4x,
  60 FPS in Stage 1.
- Done: **Audio**: the real Wii audio SDK (AX / NW4R SND) running on top of a software DSP mixer,
  with SDL2 output (music and sound effects).
- Done: Keyboard and gamepad input (the game is played with the Wii Remote held sideways), in-game settings menu
  (VSync, frame limit, filters, resolution, fullscreen) and FPS counter.
- Done: Saving works (emulated NAND in a local folder).
- In progress: Replacing lifted machine code with readable, hand-written, documented C/C++ function by function.
- Done: Windows build (MSYS2), stable 60 FPS in heavy scenes. The runtime also has a built-in sampling profiler.
- In progress: macOS build, Classic Controller / GameCube controller mapping.
- In progress: Remaining unlifted functions, renderer details and performance work.

About 1,100 of the roughly 6,300 unnamed functions have readable names so far (`decomp/game/config/renames.txt`).

## Repository layout

```
runtime/    Platform layer: SDL2 window + OpenGL renderer, audio mixer, input, file system, SDK replacements
decomp/     Tools that translate game.dol to C++ (lifter), symbol tables, renames, Ghidra import script
src/        Readable, hand-written decompiled game code (gameplay, engine)
include/    Headers for the above
scripts/    Helper scripts
tests/      Differential tests for the translator
build.sh / build.bat   One-step build from your own WAD (Linux / Windows with MSYS2)
BUILD.md    How to build and run (needs your own copy of the game)
LICENSE     MIT license (project code only)
PROGRESS.md Detailed development notes (Spanish)
```

## Legal notice

- This project contains **no game assets and no original game binaries**: no `game.dol`, no art, audio, text,
  scripts or other copyrighted data from Konami / M2 will ever be distributed here.
- To build or run anything you will need **your own legally obtained copy** of the game (extracted by you from
  your own console / WAD). The tools in this repository only read files you provide. The C++ the translator generates from your `game.dol` is
  written to a git-ignored folder on your machine and is never committed.
- *Castlevania* and *Castlevania: The Adventure ReBirth* are trademarks / property of Konami Digital Entertainment.
  This is an **unofficial, non-commercial fan project** for preservation and educational purposes. It is not
  affiliated with, endorsed by or sponsored by Konami, M2 or Nintendo.

## Acknowledgements

Open-source tools and projects that make this kind of work possible: Ghidra, decomp-toolkit (dtk),
Dolphin (as a reference for Wii hardware behaviour), SDL2 and OpenGL / GLEW, and the wider Wii / GameCube
decompilation community.

## License

The code in this repository is released under the [MIT License](LICENSE). The license covers only the project's own code
and tools. It does not cover the game, its assets or its trademarks, which belong to Konami Digital Entertainment (see the legal notice above).

## Contributing

Issues and pull requests (function naming, documentation, bug reports) are welcome. Guidelines will be added later.

---

<a id="espanol"></a>

## Español

> **Estado: trabajo en curso.** Este repositorio aloja las herramientas y el runtime nativo de un port para PC de
> *Castlevania: The Adventure ReBirth* (WiiWare, 2009), y la decompilación legible y documentada que se escribe
> encima. El port ejecuta el juego hasta el Stage 1; copia tu propio `.wad` junto a `build.sh` y ejecútalo (`build.bat` en Windows) para compilarlo; mira [BUILD.md](BUILD.md).

### ¿Qué es este proyecto?

*Castlevania: The Adventure ReBirth* es un remake 2D del *Castlevania: The Adventure* original (Game Boy),
desarrollado por Konami / M2 para WiiWare. El proyecto busca:

1. **Decompilar** el juego (`game.dol`, PowerPC / Metrowerks CodeWarrior) a **C/C++ legible y documentado**,
   recompilable y comprensible para personas.
2. Construir encima un **port nativo para PC** (SDL2 + OpenGL), sin emulador y con opciones modernas
   (cualquier resolución, VSync, framerate sin límite, filtros, teclado / mando...).
3. Documentar el motor del juego, sus formatos de archivo (`.psb`, `.brsar`, `.brstm`...) y las partes del SDK de
   Wii que usa, como referencia para proyectos similares.

### Estado actual

Situación:

- Hecho: Todo `game.dol` está elevado a C++ y **corre de forma nativa en x86-64 Linux**, sin emulador:
  arranque, hilos, temporizadores, acceso a archivos/NAND, intro, título, mapa y Stage 1.
- Hecho: **Renderizador nativo** (OpenGL): GX to OpenGL, decodificación de texturas, shaders TEV, resolución interna
  hasta 4x, 60 FPS en el Stage 1.
- Hecho: **Audio**: el SDK de audio real de Wii (AX / NW4R SND) sobre un mezclador DSP por software, con salida
  por SDL2 (música y efectos).
- Hecho: Entrada por teclado y mando (el juego se maneja con el Wii Remote en horizontal), menú de ajustes dentro del juego
  (VSync, límite de FPS, filtros, resolución, pantalla completa) y contador de FPS.
- Hecho: El guardado funciona (NAND emulada en una carpeta local).
- En curso: Sustituir el código máquina elevado por C/C++ legible, escrito a mano y documentado, función a función.
- Hecho: Build para Windows (MSYS2), 60 FPS estables en escenas con mucha acción. El runtime incluye un perfilador por muestreo.
- En curso: Build para macOS, mapeo del Classic Controller / mando de GameCube.
- En curso: Funciones aún sin elevar, detalles del renderizador y rendimiento.

Unas 1.100 de las aproximadamente 6.300 funciones sin nombre ya tienen un nombre legible (`decomp/game/config/renames.txt`).

### Estructura del repositorio

```
runtime/    Capa de plataforma: ventana SDL2 + renderizador OpenGL, mezclador de audio, entrada, archivos, sustitutos del SDK
decomp/     Herramientas que traducen game.dol a C++ (elevador), tablas de símbolos, renombres, script de importación de Ghidra
src/        Código del juego decompilado, legible y escrito a mano (gameplay, motor)
include/    Cabeceras de lo anterior
scripts/    Scripts auxiliares
tests/      Pruebas diferenciales del traductor
build.sh / build.bat   Compilación en un paso a partir de tu propio WAD (Linux / Windows con MSYS2)
BUILD.md    Cómo compilar y ejecutar (hace falta tu propia copia del juego)
LICENSE     Licencia MIT (solo el código del proyecto)
PROGRESS.md Notas de desarrollo detalladas
```

### Aviso legal

- Este proyecto **no contiene recursos ni binarios originales del juego**: nunca se distribuirá aquí ningún
  `game.dol`, ni arte, audio, textos, scripts u otros datos con copyright de Konami / M2.
- Para compilar o ejecutar algo necesitarás **tu propia copia legal** del juego (extraída por ti de tu consola /
  WAD). Las herramientas de este repositorio solo leen archivos que tú aportas. El C++ que el traductor genera a partir de tu
  `game.dol` se escribe en una carpeta ignorada por git en tu equipo y nunca se sube.
- *Castlevania* y *Castlevania: The Adventure ReBirth* son marcas / propiedad de Konami Digital Entertainment.
  Este es un **proyecto de fans no oficial y sin ánimo de lucro**, con fines de preservación y educativos. No está
  afiliado, respaldado ni patrocinado por Konami, M2 ni Nintendo.

### Agradecimientos

Herramientas y proyectos de código abierto que hacen posible este trabajo: Ghidra, decomp-toolkit (dtk),
Dolphin (como referencia del comportamiento del hardware de Wii), SDL2 y OpenGL / GLEW, y la comunidad de
decompilación de Wii / GameCube.

### Licencia

El código de este repositorio se publica bajo la [licencia MIT](LICENSE). La licencia cubre solo el código y las herramientas
del proyecto; no cubre el juego, sus recursos ni sus marcas, que pertenecen a Konami Digital Entertainment (ver el aviso legal).

### Contribuir

Los issues y pull requests (nombres de funciones, documentación, reportes de errores) son bienvenidos. Las guías se añadirán más adelante.
