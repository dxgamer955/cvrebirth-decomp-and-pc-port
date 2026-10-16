# Castlevania: The Adventure ReBirth - Decompilation & PC Port

> English | [Español](#espanol)

> **Status: placeholder / work in progress.** This repository will host the readable, documented
> decompilation of *Castlevania: The Adventure ReBirth* (WiiWare, 2009) and a native PC port built from it.
> The source code will be published here once it is clean and documented enough to be useful to others.

## What is this project?

*Castlevania: The Adventure ReBirth* is a 2D remake of the original *Castlevania: The Adventure* (Game Boy),
developed by Konami / M2 for WiiWare. This project aims to:

1. **Decompile** the game (`game.dol`, PowerPC / Metrowerks CodeWarrior) into **readable, documented C/C++**
   that can be rebuilt and is understandable by humans.
2. Build a **native PC port** on top of it (SDL2 + OpenGL), without an emulator, with modern options
   (any resolution, VSync, unlocked frame rate, filters, keyboard / gamepad input...).
3. Document the game's engine, file formats (`.psb`, `.brsar`, `.brstm`...) and the Wii SDK pieces it uses, so the
   work can serve as a reference for similar projects.

## Current state (private development)

The work done so far lives outside this repository and is being cleaned up. Where it stands:

- Done: The whole of `game.dol` is lifted to C++ and **runs natively on x86-64 Linux**, with no emulator:
  boot, threads, timers, file/NAND access, intro, title screen, map and Stage 1.
- Done: **Native renderer** (OpenGL): GX to OpenGL, texture decoding, TEV shaders, internal resolution up to 4x,
  60 FPS in Stage 1.
- Done: **Audio**: the real Wii audio SDK (AX / NW4R SND) running on top of a software DSP mixer,
  with SDL2 output (music and sound effects).
- Done: Keyboard input (the game is played with the Wii Remote held sideways), in-game settings menu
  (VSync, frame limit, filters, resolution, fullscreen) and FPS counter.
- Done: Saving works (emulated NAND in a local folder).
- In progress: Replacing lifted machine code with readable, hand-written, documented C/C++ function by function.
- In progress: Windows / macOS builds, gamepad support, Classic Controller / GameCube controller mapping.
- In progress: Remaining unlifted functions, renderer details and performance work.

> Nothing in the list above is released yet. This README will be updated when the source is.

## Planned repository layout

```
src/        Decompiled / ported game code (readable, documented)
runtime/    Platform layer: SDL2 window + OpenGL renderer, audio mixer, input, file system
tools/      Scripts used to extract data, split the binary and verify the decompilation
docs/       Notes on the engine, file formats and the Wii SDK
```

(The layout may change as the project takes shape.)

## Legal notice

- This project contains **no game assets and no original game binaries**: no `game.dol`, no art, audio, text,
  scripts or other copyrighted data from Konami / M2 will ever be distributed here.
- To build or run anything you will need **your own legally obtained copy** of the game (extracted by you from
  your own console / WAD). The tools in this repository only read files you provide.
- *Castlevania* and *Castlevania: The Adventure ReBirth* are trademarks / property of Konami Digital Entertainment.
  This is an **unofficial, non-commercial fan project** for preservation and educational purposes. It is not
  affiliated with, endorsed by or sponsored by Konami, M2 or Nintendo.

## Acknowledgements

Open-source tools and projects that make this kind of work possible: Ghidra, decomp-toolkit (dtk),
Dolphin (as a reference for Wii hardware behaviour), SDL2 and OpenGL / GLEW, and the wider Wii / GameCube
decompilation community.

## Contributing

While this is a placeholder there is nothing to contribute to yet. Once the code is published, issues and
pull requests (function matching, documentation, bug reports) will be welcome; guidelines will be added then.

---

<a id="espanol"></a>

## Español

> **Estado: marcador de posición / trabajo en curso.** Este repositorio alojará la decompilación legible y
> documentada de *Castlevania: The Adventure ReBirth* (WiiWare, 2009) y un port nativo para PC construido a
> partir de ella. El código se publicará aquí cuando esté lo bastante limpio y documentado para ser útil a otros.

### ¿Qué es este proyecto?

*Castlevania: The Adventure ReBirth* es un remake 2D del *Castlevania: The Adventure* original (Game Boy),
desarrollado por Konami / M2 para WiiWare. El proyecto busca:

1. **Decompilar** el juego (`game.dol`, PowerPC / Metrowerks CodeWarrior) a **C/C++ legible y documentado**,
   recompilable y comprensible para personas.
2. Construir encima un **port nativo para PC** (SDL2 + OpenGL), sin emulador y con opciones modernas
   (cualquier resolución, VSync, framerate sin límite, filtros, teclado / mando...).
3. Documentar el motor del juego, sus formatos de archivo (`.psb`, `.brsar`, `.brstm`...) y las partes del SDK de
   Wii que usa, como referencia para proyectos similares.

### Estado actual (desarrollo privado)

El trabajo realizado vive fuera de este repositorio y se está depurando. Situación:

- Hecho: Todo `game.dol` está elevado a C++ y **corre de forma nativa en x86-64 Linux**, sin emulador:
  arranque, hilos, temporizadores, acceso a archivos/NAND, intro, título, mapa y Stage 1.
- Hecho: **Renderizador nativo** (OpenGL): GX to OpenGL, decodificación de texturas, shaders TEV, resolución interna
  hasta 4x, 60 FPS en el Stage 1.
- Hecho: **Audio**: el SDK de audio real de Wii (AX / NW4R SND) sobre un mezclador DSP por software, con salida
  por SDL2 (música y efectos).
- Hecho: Entrada por teclado (el juego se maneja con el Wii Remote en horizontal), menú de ajustes dentro del juego
  (VSync, límite de FPS, filtros, resolución, pantalla completa) y contador de FPS.
- Hecho: El guardado funciona (NAND emulada en una carpeta local).
- En curso: Sustituir el código máquina elevado por C/C++ legible, escrito a mano y documentado, función a función.
- En curso: Builds para Windows / macOS, soporte de mando, mapeo del Classic Controller / mando de GameCube.
- En curso: Funciones aún sin elevar, detalles del renderizador y rendimiento.

> Nada de lo anterior está publicado todavía. Este README se actualizará cuando lo esté el código.

### Estructura prevista

```
src/        Código del juego decompilado / portado (legible y documentado)
runtime/    Capa de plataforma: ventana SDL2 + renderizador OpenGL, mezclador de audio, entrada, archivos
tools/      Scripts para extraer datos, dividir el binario y verificar la decompilación
docs/       Notas sobre el motor, los formatos de archivo y el SDK de Wii
```

### Aviso legal

- Este proyecto **no contiene recursos ni binarios originales del juego**: nunca se distribuirá aquí ningún
  `game.dol`, ni arte, audio, textos, scripts u otros datos con copyright de Konami / M2.
- Para compilar o ejecutar algo necesitarás **tu propia copia legal** del juego (extraída por ti de tu consola /
  WAD). Las herramientas de este repositorio solo leen archivos que tú aportas.
- *Castlevania* y *Castlevania: The Adventure ReBirth* son marcas / propiedad de Konami Digital Entertainment.
  Este es un **proyecto de fans no oficial y sin ánimo de lucro**, con fines de preservación y educativos. No está
  afiliado, respaldado ni patrocinado por Konami, M2 ni Nintendo.

### Agradecimientos

Herramientas y proyectos de código abierto que hacen posible este trabajo: Ghidra, decomp-toolkit (dtk),
Dolphin (como referencia del comportamiento del hardware de Wii), SDL2 y OpenGL / GLEW, y la comunidad de
decompilación de Wii / GameCube.

### Contribuir

Mientras sea un marcador de posición todavía no hay nada a lo que contribuir. Cuando se publique el código, los
issues y pull requests (equivalencia de funciones, documentación, reportes de errores) serán bienvenidos; las
guías se añadirán entonces.
