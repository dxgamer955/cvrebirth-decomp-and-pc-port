# Castlevania: The Adventure ReBirth — estado del proyecto

Meta: decomp del WiiWare original para hacer un PC port.

## Pipeline actual (funcional)

1. **Extracción del WAD**: se instaló el WAD en la NAND virtual de Dolphin
   (`Tools > Install WAD`), que lo desencripta como parte de su funcionamiento
   normal. El contenido decodificado queda en
   `~/.local/share/dolphin-emu/Wii/title/00010001/57443945/content/`.
   - `00000004.app` = `main.dol` real (entry point `0x81330000`, compilado con
     Metrowerks CodeWarrior — confirmado por símbolos `_savegpr_XX`/`_SDA_BASE_`).
   - `00000007.app` = banner (IMET), contiene el string interno
     `wiidracula.0911021432` (nombre de proyecto + fecha de build, 2009-11-02).
   - `00000008.app` = sin identificar aún (no es un apploader real — WiiWare no
     usa apploader de disco).
   - `00000009.app` = archivo U8 (32MB) con los assets: mapas `.psb.l`
     (formato de script, sugiere motor de M2), audio `.brsar`/`.brstm`.
   - Copiados a `orig/` (gitignored, material con copyright).

2. **Fix del header del DOL**: el header original tenía el código real
   (`.init`/`.text`/etc, ~170KB) etiquetado como sección de **datos** (`data0`)
   en vez de texto. Se reclasificó como `text1` en una copia parcheada
   (`build/main_patched.dol`) para que las herramientas de análisis lo acepten.

3. **DolRecomp** (`github.com/ExpansionPak/DolRecomp`): recompilador estático
   que traduce el PowerPC del DOL directamente a C — no requiere el compilador
   CodeWarrior original (bloqueante inicial del proyecto). Genera ~654K líneas
   de C a partir de 77,760 instrucciones (806 "desconocidas", pendiente
   investigar).

4. **ModernGekko-Template** (`github.com/ExpansionPak/ModernGekko-Template`):
   runtime basado en el núcleo de Dolphin que compila el C generado en un
   módulo nativo (`.so`) y lo ejecuta. **Funciona — el juego corre y es
   jugable** (confirmado: Stage 1, HUD, física, controles).

## Bugs encontrados y arreglados en ModernGekko (ver `patches/moderngekko-wiiware-boot.patch`)

Todos en `tools/ModernGekko-Template/lib/ModernGekko/src/runtime/dolphin_runtime.cpp`
(repo clonado, gitignored — por eso el patch se guarda aparte).

1. **Crash de ImGui** (`WaitForShadersBeforeStarting` forzado a `true`): la
   barra de progreso "Compiling Shaders" llama `ImGui::GetIO()` antes de que
   exista el contexto de ImGui → `SIGILL`. Cambiado a `false`.

2. **Detección errónea de "disco extraído"**: la carpeta `sys/boot.bin` +
   `files/` (requerida por la propia validación de `moderngekko-port`) hace
   que Dolphin la reconozca como un disco GC/Wii real
   (`IsValidDirectoryBlob`), y busca un apploader real que WiiWare nunca tiene
   (se lanza vía Menú de Wii, no disco). Arreglado forzando boot directo vía
   `BootParameters::NANDTitle` (como lanzar un canal) en vez de
   `BootParameters::GenerateFromFile` (que auto-detecta disco).

3. **NAND falsa incompleta**: había que instalar el título también en el NAND
   propio de ModernGekko (`~/.local/share/moderngekko/Wii/`), no solo en el
   de Dolphin:
   - Copiar `title/00010001/57443945/{content,data}` y
     `ticket/00010001/57443945.tik` desde el NAND de Dolphin.
   - Agregar la entrada del título a `sys/uid.sys` (12 bytes:
     title_id_high + title_id_low + uid, formato big-endian u32×3).

4. **Controles**: `~/.local/share/moderngekko/Config/WiimoteNew.ini` no
   existía. Se creó manualmente (Dolphin no trae un perfil por defecto en
   este frontend). El D-Pad necesitó mapeo rotado porque el juego se sostiene
   con el Wiimote de lado — mapeo final funcional:
   ```ini
   D-Pad/Up = `Down`
   D-Pad/Down = `Up`
   D-Pad/Left = `Right`
   D-Pad/Right = `Left`
   ```

## Hallazgo clave: `main.dol` (content 4) es solo un cargador

- `orig/main.dol` (170 KB) contiene únicamente el SDK de Nintendo (OS, SI, VI,
  DVD, NAND, SC, PAD) + arranque. No tiene gráficos, audio ni lógica de juego.
- El juego real es `00000008.app` (antes mal llamado apploader): comprimido con
  **Nintendo LZ77 extendido (tipo 0x11)**, 1,368,416 B -> 2,644,832 B. Se
  descomprime con `decomp/tools/lz_ex.py orig/apploader.img orig/game.dol`.
- `orig/game.dol`: entry `0x80004050`, `.text` 2.2 MB, tablas `extab` (C++ con
  excepciones, clases `Drc*`/`M2*`), SDK completo (OS, GX, AX, DSP, AI, KPAD,
  WPAD, HBM, CNT, ENC, DVD, NAND, EXI, SC, SI, VI, PAD, PAD).
- dtk detecta **7,243 funciones** en 139 objetos. Proyectos dtk:
  `decomp/game/` (objetivo real) y `decomp/loader/` (solo SDK, no hace falta
  decompilarlo).

**Corrección sobre rendimiento:** el módulo recompilado solo cubre las
direcciones del cargador (`0x80003400` y `0x81330000`); el código del juego
(`0x80004000+`) no está cubierto, así que se ejecuta con el JIT64 de respaldo.
Por eso el módulo no aportaba velocidad y la afirmación "el backend C es más
lento que el JIT" no está demostrada. Pendiente: recompilar `game.dol` con
DolRecomp y volver a medir.

### Mapa de `game.dol` (herramienta: `decomp/tools/sdk_split.py`)

El orden de enlazado de `.text` y `.data` coincide, y los banners
`<< RVL_SDK - X` marcan el inicio de los datos de cada módulo. Resultado:

| Rango de `.text` | Funcs | Tamaño | Qué es |
|---|---|---|---|
| `0x80006F20-0x800BAF30` | 2,476 | ~723 KB | **Código del juego y su motor** (clases `CT*`, `EN*`, `MProcess`, `Drc*`) |
| `0x800BB300-0x80164800` | ~1,950 | ~678 KB | CNT (SDK) + **NW4R** (sonido/UI/matemáticas) + middleware sin banner |
| `0x80164810-0x801D09D0` | ~1,400 | ~440 KB | HBM, OS, VI, GX, DVD, AX, DSP, NAND, SC, PAD, WPAD, KPAD |
| `0x801D0A70-0x8021E028` | ~1,200 | ~330 KB | Pila USB, runtime C/C++ (MSL) |

**El objetivo real del decomp son ~2,500 funciones (~720 KB)**; el resto es
SDK/NW4R, que se sustituye por SDL/OpenGL o se toma de decomps públicos de NW4R.
La división es aproximada en las fronteras (las funciones sin referencias a
datos se asignan por vecindad).

### Nombrado automático por vtables (`decomp/tools/classes.py`)

El motor tiene RTTI propio: `vtable[0]` -> `typeinfo+8` -> primer campo = puntero
al nombre de la clase. Layout de vtable: `[rtti, 0, dtor, vf1, vf2, ...]`
(`vfN` = slot N; el objeto guarda la vtable en el offset 0 en su constructor).
Resultado: **212 clases con nombre, 935 de 7,243 funciones nombradas**
(`decomp/game/config/symbols.txt`; respaldo previo no versionado).

Fiabilidad: destructor y `vfN` son fiables (la vtable lista la función).
`ctor` solo se etiqueta si la clase tiene <=2 candidatos; con más, son dueños
que contienen la clase como miembro con el constructor inline. Las funciones
compartidas entre varias vtables (métodos heredados) quedan sin nombrar.
Ejemplo verificado: `CTEngine_vf1` = bucle de update del motor (recorre listas
de tareas y llama al virtual +0xC).

Arquitectura vista: sistema de tareas (`MTask`, `MTaskSet`, `MCallTask0<...>`),
clases del juego `Drc*` (81: jefes `DrcEnemyBoss*`, subarmas `DrcSubWeapon*`,
`DrcPlayerBase`, `DrcMain`, `DrcEngine`...), capas `CT*`, backups `MBackup*`.
Para regenerar tras `dtk dol split`: `cd decomp/game && python3 ../tools/classes.py ../../orig/game.dol --apply`.

### Código decompilado (`src/`, `include/`)

Primera pieza: sistema de colisión/daño del motor, ~8 funciones de nodos más
`CTEngine::Update` (`src/engine/collision.cpp`, `src/engine/CTEngine.cpp`,
`include/engine/collision.h`). Se comprueba con `scripts/check_ppc.sh`
(clang -> PowerPC; compila, **no** hay matching byte a byte: el compilador
original es CodeWarrior). Equivalencia revisada a mano contra el desensamblado
(mismos offsets de memoria, mismas listas, mismo orden de pares) y VERIFICADA con
`tests/difftest` (ver abajo).

- `ENDamaged` (lista A, `0x802845C0[4]`, next `+0x40`) y `EcoScratch` (lista B,
  `0x802845D0[4]`, next `+0x10`) son **bases abstractas**: slots 1..3 son
  virtuales puros que implementan las clases derivadas.
- `CTEngine::Update`: ejecuta `Exec()` de los nodos activos y luego resuelve 8
  pares (danable, golpe): (0,1) (1,0) (0,3) (0,2) (1,3) (2,1) (3,0) (3,1);
  si `scratch->Hit(damaged)` entonces `damaged->OnHit()`.
- Hipótesis sin confirmar: los grupos 0..3 son bandos (p. ej. 0=jugador,
  1=enemigos, 2=subarmas). `DrcPlayerPowerupEffect` usa grupo 0 y
  `DrcSubWeaponBase` usa grupo 2.
- Nombres provisionales: `Exec`, `OnHit`, `Hit`.
- `operator_new` (`0x8015CB60`) y `operator_delete` (`0x8015D040`): allocator
  propio sobre MEM1/MEM2.

### Flujo con Ghidra (pseudo-C base)

Ghidra 12 no trae cargador de DOL, así que `decomp/ghidra/ImportDol.java`
importa el DOL crudo, crea los bloques de memoria desde la cabecera, fija
`r13`/`r2` (`_SDA_BASE_ 0x8031B7A0`, `_SDA2_BASE_ 0x8031EF60`), aplica los
nombres de `symbols.txt` y exporta el pseudo-C (`decomp/game/pseudo_c/all.c`,
no versionado). ~90 s para 4,405 funciones (`0x80006F20-0x80164800`), 0 fallos.

```
/opt/ghidra/support/analyzeHeadless $PWD/decomp/game/ghidra_proj castlevania \
  -import $PWD/orig/game.dol -loader BinaryLoader -processor PowerPC:BE:32:default \
  -noanalysis -overwrite -scriptPath $PWD/decomp/ghidra \
  -postScript ImportDol.java $PWD/orig/game.dol $PWD/decomp/game/config/symbols.txt \
  $PWD/decomp/game/pseudo_c 80006F20 80164800 8031B7A0 8031EF60
python3 decomp/tools/pc.py CTEngine_vf1        # ver una funcion (o 'ENDamaged_*')
```

Validación: el pseudo-C de `CTEngine_vf1` coincide con la lógica deducida a mano.
Paired-singles: en este juego solo aparecen `psq_l`/`psq_st` (2,574; ninguna
aritmética `ps_*`). Con `r1` son guardado/restauración de la mitad alta de
`f30`/`f31` (el script las pone a `nop`); las demás son copias de un par de
floats (`lfd`/`stfd`) o de un float (`lfs`/`stfs`). Sin ese parche fallaban
416 funciones (300 del juego); con él, 0 de 4,405.

**Corrección de alcance:** la librería de motor `M*` (`MTask`, `MProcess`...)
tiene métodos en `0x8013xxxx`, es decir, en el tramo que había clasificado como
"CNT + NW4R". El código decompilable propio es mayor que los ~720 KB iniciales;
la frontera juego/motor/SDK sigue siendo aproximada.

Nota: `MTask`/otras clases del motor embeben subobjetos en `+0x10` y `+0x40`;
`classes.py` atribuye mal algunas funciones genéricas compartidas (p. ej. el
destructor de subobjeto aparece como `DrcMotionPlayer__WaitStopTask2_ctor`).

### Cómo probar el decomp dentro del juego (pendiente)

ModernGekko trae mods (`include/moderngekko/mod_abi.h`): un `.so` con
`RECOMP_PATCH(direccion, fn)` reemplaza una función del juego por código del
host (`void fn(CPUState*)`: args en `gpr[3..10]`, `moderngekko_mod_read/write`
para memoria, `moderngekko_mod_return_u32`). Permite sustituir funciones
decompiladas una a una y comprobar que el juego sigue igual. Limitaciones a
resolver: no hay API para llamar a código del juego desde el parche (hace falta
para funciones con llamadas virtuales como `CTEngine::Update`), y el código
decompilado debe acceder a la memoria del juego (big-endian, punteros de 32
bits), no a punteros del host. No verificado aún que los parches se apliquen
con el core JIT (puede requerir el core StaticRecomp).

### Banco de pruebas diferencial (`tests/difftest`)

Ejecuta la funcion ORIGINAL de `game.dol` y nuestro codigo decompilado (compilado a
PowerPC con clang, enlazado en `0x81000000` con las globales y el allocator en las
direcciones reales) en **Unicorn** (`pacman -S python-unicorn`) sobre la misma memoria
aleatoria, y compara memoria final y llamadas a `delete`. Los `psq_l/psq_st` se parchean
como en Ghidra. `CTEngine::Update` se prueba con tablas virtuales senuelo (las mismas
funciones en los slots de CodeWarrior y de Itanium) que registran el orden de llamadas.

```
tests/difftest/build.sh                    # compila src/ a PowerPC -> build/tests/decomp.elf
python3 -I tests/difftest/difftest.py 500 1  # n pruebas por funcion, semilla
```

Resultado: ctor/dtor de `ENDamaged`, `EcoScratch`, `ENDamager`, `ENRestricted` y
`CTEngine::Update`: 0 fallos en 1,800 casos aleatorios por funcion. Pruebas de mutacion
(bugs inyectados a proposito) confirman que el banco los detecta.

El banco encontro un error real en `CTEngine::Update`: el ultimo par (danables del grupo 3
contra golpes de los grupos 0 y 1) es un solo bucle y el original no comprueba `m_active`
del danable antes del bucle del grupo 1 (`0x80008AD4` salta a `0x80008B2C`). Se replica.

Cubierto ademas: `DrcSubWeaponAxe::Update` y `DrcSubWeaponCross::Update` (y el reloj, mas abajo) (`src/gameplay/`,
`include/gameplay/subweapon.h`, prueba `tests/difftest/test_thrown.py`). Ambas heredan de
`DrcSubWeaponThrown` (nombre provisional; se inicializan con `fn_800A73D0`, ENDamager en `+0xC0`,
sprite en `+0xD4`, objeto de golpe en `+0xD8`). La cruz suma una gravedad a `vel.y` cada frame y
solo comprueba "dentro de pantalla" cuando `vel.y > umbral`; al impactar limpia el flag
`+0x9C` del objeto de golpe y sigue (el hacha, en cambio, termina).
Las funciones no decompiladas que llama (`fn_80009070` = "esta (x,y)+-margen dentro del
rectangulo visible de la camara", `fn_801280B0` = fijar posicion del sprite, `fn_800B6740` =
reproducir sonido por nombre) se sustituyen en Unicorn por senuelos que registran sus
argumentos. `tests/difftest/build.sh` genera un script de enlazado con TODOS los simbolos
de `symbols.txt` (funciones y datos), asi que cualquier cosa sin decompilar se enlaza al
original. `tests/difftest/run_all.sh` compila y ejecuta todo.

`DrcSubWeaponWatch::Update` (el reloj, 0x800A7ED0) es un boomerang teledirigido: tras
`m_homingDelay` frames se acelera hacia el objeto rastreado (llamada virtual en slot `+0x30`
que devuelve un `Vec2` empaquetado en r3:r4), maximo +-4 de velocidad, aceleracion 0.1,
"llegada" con |dx|<16 y |dy|<20 (estado 2), y suelta una particula cada 4 frames
(`m_particleTimer`). Lecciones del banco: (1) el orden de suma en coma flotante importa
(`x + (a + k)`, no `(x + a) + k`); (2) CodeWarrior usa `fmuls`/`fadds` separados, asi que
se compila con `-ffp-contract=off` (clang fusionaba 2 fmadd); (3) el asignador del juego
puede devolver NULL y clang asume `operator new` no nulo, por eso se usa `operator_new`
extern "C"; (4) Ghidra confunde tipos de retorno float/double, hay que mirar el ensamblador.
0 fallos en 1000 casos x 3 semillas; 5 mutaciones inyectadas (orden de suma, `<` vs `<=`,
temporizador 4->5, no copiar la velocidad al objeto de golpe, omitir el chequeo de NULL)
se detectan todas.

`DrcSubWeaponHolyWater::Update` (agua bendita, 0x800A84C0) es una maquina de estados en `+0xE0`:
fase 0 reproduce un sonido y crea un objeto (`fn_80207650`/`fn_8001CB20`), fase 1 cuenta
frames en `+0xE8` y cada multiplo de 60 (modulo con signo) limpia el byte `+0xF0`, y a los
240 frames pasa a la fase 2; las fases 2 y 100 desvanecen el sprite restando 8 de opacidad por
frame (`GetU32_C0_200_80128330`/`fn_80128320`) y marcan el arma como terminada con opacidad <= 8.
0 fallos en 1000 casos x 3 semillas; 7 mutaciones inyectadas detectadas.

`DrcSubWeaponWhipFire::Update` (fuego del latigo, 0x800A8870, la mas grande: 0x4D0 bytes) en la
fase 0 avanza en linea recta; si la velocidad no es nula traza hasta 2 segmentos contra el
terreno (`fn_800084C0`, posicion obtenida por el slot virtual `+0x30`; en la 2a vuelta se resta
`vel.x` a ambos extremos). Si choca: dos sonidos, `fn_800A4D00`, punto de contacto = origen +
direccion de la velocidad * distancia de empuje (se pasa al slot virtual `+0x28`), fase 1,
sprite segun el signo del resultado, crea un `CTMapTerrain` (0x54 bytes, campos `+4`/`+8`), pone
`m_flag` (`+0xF0`) y suma 1 a un contador global (`0x80316528`). Pegado: la velocidad pasa a la
tabla `0x802439D8..E0`. Si no: integra la posicion y frena `vel.y` hacia un objetivo con un paso
fijo. Fase 1: termina cuando el sprite muere; si estaba pegado y la animacion supera
`frame - 20` se despega y baja el contador. Cada frame mueve el objeto de golpe (`+0x44/+0x48`)
y el sprite a la posicion del arma. `+0xD0` es un campo de la base (la base lo inicializa; el
latigo guarda ahi su terreno). Limite conocido: si `operator_new` devolviese NULL el original
escribe en la direccion 4; el banco no lo cubre (Unicorn no mapea esa pagina) y clang elimina
la comprobacion. 0 fallos en 1000 casos x 3 semillas; 10 mutaciones utiles detectadas (dos mas
eran equivalentes).

`DrcSubWeaponBase::Update` (0x800A7180): si el arma esta activa (`+0x10C`) y visible (`+0x10D`)
reproduce un sonido, crea una animacion (`ValAnime`, 0x78 bytes) con dos tareas derivadas y una
tarea de espera construida en linea (0x30 bytes, dos vtables consecutivas), las cuelga de un
grupo (slot virtual `+0x20` del gestor) y se marca terminada. Las estructuras con vtable de
CodeWarrior se modelan como structs crudos (`WaitTaskRaw`). Con esto las 5 subarmas con logica
propia y la base estan cubiertas; el cuchillo no tiene `Update` propio (slot 3 vacio).
0 fallos en 800 casos; 8 mutaciones detectadas.

Limites: compara el comportamiento del codigo compilado con clang, no bytes; el layout de
vtable es el de Itanium (el original usa el de CodeWarrior), asi que las llamadas virtuales
se prueban con senuelos; el destructor "con delete" se modela (clang emite `trap` para D0
de la clase abstracta).

### Fuzzer diferencial generico (`tests/difftest/fuzz.py`, casos en `cases.py`)

Para no escribir senuelos a mano por cada funcion: dado el original (direccion) y el simbolo del
decomp, el fuzzer (a) sustituye por senuelos que registran argumentos todos los `bl`/`b` fuera de
la funcion (la aridad sale de las firmas de Ghidra en `pseudo_c/all.c`; fuera del rango
decompilado hay que dar `sigs`), (b) descubre ejecutando el original que globales de datos
lee/escribe y los aleatoriza y compara (flotantes, enteros, punteros), (c) comprueba que se
conservan r1, r14-r31 y f14-f31, (d) varia lo que devuelven los senuelos en cada prueba (`salt`) y
simula el heap de `operator_new` (a veces NULL). Un caso nuevo son ~10 lineas en `cases.py`
(rellenar los campos del objeto con `c.f/c.i/c.b/c.ptr/c.vptr`). `OVERLAY=1` lo ejecuta contra el
overlay real con sus trampolines. Primer caso: `DrcBatGenerator::Update` (0x8004FC10, murcielago
de onda senoidal): encontro de inmediato un error mio (el contador de fase se convierte a float
SIN signo; yo lo habia traducido con signo) y, con 8+3 mutaciones, detecto todas las inyectadas
(dos sobrevivieron hasta anadir la sal por prueba).

### Runtime nativo (`runtime/`): el juego corriendo en x86-64 sin Dolphin

`decomp/tools/lift_native.py` eleva TODAS las funciones de `game.dol` (7183 de 7243; las demas
usan instrucciones no soportadas o llamadas internas) a C++ con otro convenio: `RetRegs
N_<direccion>(r3-r10, f1-f8)` devuelve (r3, r4, f1); las llamadas indirectas (vtables, punteros
a funcion) pasan por `rt_call_indirect` (tabla direccion -> funcion); la pila del invitado vive en
su memoria (`g_sp`). `runtime/include/native_lift.h`: memoria del invitado big-endian (una sola
reserva de 320 MB, MEM1 en 0, MEM2 en 0x10000000, registros de hardware 0xCC/0xCD interceptados),
ayudantes PowerPC, pares simples con GQR (`psq_l/psq_st`, `ps_*`), `OSSaveContext` como setjmp en el
marco del llamador. Las funciones que se sustituyen a mano se declaran con `HLE_FN(simbolo)` en
`runtime/src/hle_*.cpp` (el generador las enlaza por nombre y no eleva el original); lo no elevable
o sin simbolo aborta con un mensaje y una pila de llamadas ("trap-driven bring-up"). Compilar:
`python3 decomp/tools/lift_native.py && make -C runtime -j8`; ejecutar: `runtime/run.sh [seg]`
(`NOMMIO=1` oculta los accesos a registros). `runtime/trace.txt` lista funciones cuyas llamadas
se imprimen.

Estado: **el juego corre de forma nativa, sin Dolphin, con imagen, entrada y audio**:
arranca, ejecuta los constructores estaticos y `main`, inicializa VI, GX (el codigo real de `GXInit`
corre sobre el FIFO), SC, WPAD/KPAD/PAD, CNT, AI y NW4R SND, lanza los hilos del juego (planificador
del SDK elevado + cambio de contexto con coroutines y `setjmp`), carga todos sus recursos
(`setting.csv`, `dracula_sound.brsar`, `motion/*.psb`, los `.brstm` a MEM2...) y muestra en una
ventana SDL2/OpenGL: el aviso de seguridad de Wii, el logo de Konami, el dialogo de datos de guardado,
el titulo y la introduccion con texto y arte, a ~60 fotogramas/s.
- Tiempo: el temporizador del anfitrion (SIGALRM, 1 kHz) atiende en los bucles del codigo elevado
  (`RT_POLL`, que fija `g_sp = r1 - 0x100`) las alarmas de OS vencidas (`hle_alarm.cpp`: cola real del
  SDK, emulacion del decrementador) y, a 59.94 Hz, la interrupcion de retrazado de VI.
- IOS a nivel de API (`hle_ios.cpp`: `/dev/fs` con `runtime/nand/`, `/dev/es`, `/dev/stm`).
- GX: `gx_fifo.cpp` interpreta el FIFO completo; `gx_render.cpp` lo dibuja con OpenGL 3.3: EFB de
  640x480 con profundidad, texgen de XF, shaders TEV generados por configuracion, mezcla/z/alfa,
  texturas (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR/C4/C8/C14X2 con TLUT) en cache, copias EFB->XFB
  (lo que se presenta en cada retrazado) y EFB->textura (con vista rellenada a potencia de 2).
  Depuracion: `RT_DUMP_FRAME=n` (vuelca el XFB cada n fotogramas a /tmp/claude-1000), `RT_GXFRAME=n`
  (lista dibujos/copias de un fotograma), `RT_GXLOG=a,b`, `RT_RAW=a,b`, `RT_BT_DRAW=n`.
- Entrada (`hle_input.cpp`): se sustituye `KPADRead`; teclado = Wii Remote en horizontal (el juego se maneja asi): flechas/WASD =
  cruceta girada 90 grados (izquierda de pantalla = WPAD_UP, derecha = WPAD_DOWN, arriba = WPAD_RIGHT, abajo = WPAD_LEFT;
  comprobado en el nivel 1; `RT_PAD_UPRIGHT=1` la deja sin girar), Z/2 = boton 2, X/1 = boton 1, C = A, V = B,
  espacio/Intro = +, Retroceso = -, H = HOME, raton = puntero. Pruebas sin teclado: `RT_AUTO_SEQ="n:mascara[p],..."`. El motor
  Bluetooth del WPAD no corre. Pruebas sin teclado: `RT_AUTO_BTN=<mascara hex> RT_AUTO_AFTER=n
  RT_AUTO_PTR=x,y`.
- Fuente del sistema (OSFont): `scripts/make_osfont.py` rasteriza una TTF libre del equipo a
  `runtime/gen/osfont_ansi.bin`; `hle_font.cpp` sustituye `OSInitFont` (la ROM de fuentes es de Nintendo).
- Audio (`audio_hw.cpp`, `audio_ax.cpp`, `include/audio.h`): el SDK real (AIInit, AXInit, DSP, NW4R SND) corre elevado tal cual
  y solo se emula el hardware de debajo. Registros de AI (`0xCD006C00`: AICR/AIVR/contador) y del DSP (`0xCC005000`:
  buzones CPU<->DSP, DSPCR, DMA de AI en `0x5030/32/36`). El DSP se emula sin microcodigo: la CPU "arranca" la tarea con los
  correos 0x80F3xxxx (se responde 0xDCD10000 -> `init_cb`) y por fotograma envia `0xBABExxxx` + direccion de la lista de
  comandos de AX (los comandos 0-0xE que arma `fn_801AE000`); el DSP la ejecuta (mezcla de las 96 voces del bloque de
  parametros AXPB de 0x140 bytes: ADPCM GC/PCM16/PCM8, SRC lineal, envolvente, volumenes L/R/S/aux con rampas por
  muestra, bucles) y contesta 0xDCD10001 (`res_cb`, `__DSPHandler`). Fotogramas de 3 ms (96 muestras a 32 kHz): el
  "DMA de AI" es consumo de SDL (`SDL_OpenAudioDevice` a 32 kHz estereo; sin dispositivo, reloj del anfitrion
  con `RT_AUDIO=0`): cuando la cola baja de `RT_AUDIO_BLOCKS` (8) bloques se reproduce otro y se invoca el callback de
  `AIRegisterDMACallback` (`rt_service_audio` desde `rt_service_irq`). Layout del mixerCtrl de Wii deducido: grupos de 5 bits
  {L=1,R=2,S=4,rampa L/R=8,rampa S=16} en los bits 0 (principal), 14, 19 y 24 (aux A/B/C). Sin implementar: ITD, filtro
  biquad, compresor, DPL2 (modo 2), altavoces de WPAD, dpop. Depuracion: `RT_AUDIO_LOG=1` (voces nuevas, estadisticas),
  `RT_AUDIO_DUMP=fichero.raw` (PCM s16 estereo 32 kHz), `RT_AUDIO=0`. Se quito el parche `fn_800E6260`.
- Rendimiento: el nivel 1 iba a ~1 fotograma/s porque cada dibujo hasheaba el contenido completo de las texturas con paleta
  (C8...) para detectar cambios; ahora la paleta se hashea siempre (es pequena) y el contenido una vez por fotograma de VI y
  textura. Ademas: clave numerica del programa TEV (antes se generaba el GLSL completo en cada dibujo), cache de
  parametros de muestreo y acumulacion de dibujos con el mismo estado en un solo `glDrawArrays`. Lo que mas pesaba era el lector del FIFO
  de GX (`gx_fifo.cpp`: borraba del principio de un `vector` por comando y reanalizaba el comando en cada palabra escrita; ahora
  avanza un indice y recuerda cuantos bytes faltan). Con eso el nivel 1 (con enemigos) va a 60 fotogramas de juego/s
  (antes se quedaba en 30 y bajaba a ~15: el juego espera al retrazado y perdia uno si un fotograma pasaba de 16,7 ms). Pendiente: invalidacion explicita (GXInvalidateTex*) en vez de hashear.
  Diagnostico: `RT_FPS=1` (VI, dibujos y copias por segundo), `RT_SAMPLE=1` (muestreo de CPU: funcion elevada activa y
  funciones del anfitrion; el muestreo usa ITIMER_PROF).
- Limite de fotogramas: el motor (Mercury) tiene un limitador propio (`Engine_WaitFrameTime`, sustituido en `hle_vi.cpp`): espera retrazados
  hasta que pasen `+4` pasos de 1/60 s y escribe en `+0xC` los pasos transcurridos; la logica avanza en pasos fijos de 1/60 s.
  Sin limite (F4) el HLE no espera y informa de 1 paso: va mas rapido que 60 Hz (~83 fps de juego en el nivel 1 = 1,4x de velocidad).
  Mas fluidez sin acelerar el juego exigiria interpolar los dibujos entre pasos de logica.
- Fallo de "Return to title" (pantalla en negro): `MCNTFileReadTaskFactory` descomprimia `title.psb.l` con zlib y fallaba
  ("incorrect data check"). Causa: el elevador solo omitia las llamadas a los ayudantes `_savegpr_N/_restgpr_N`, pero el simbolo de la
  direccion de `_savegpr_14` se llama `__save_gpr` (idem `__restore_gpr`, `__save_fpr`, `__restore_fpr`), asi que esas llamadas se
  elevaban como funciones normales y escribian los 18 registros sin inicializar (0xC0DE00nn) en `r11 - 0x48`, con `r11` basura que la
  mascara de direcciones llevaba a MEM1: corrompia lo que hubiera alli (la segunda carga, 72 bytes de la salida). `lift.py` ya los omite.
  Depuracion util: `trace.txt` (entrada y retorno de funciones, con `sp`), `RT_CNTLOG`, `RT_THREADLOG`, `RT_SETTINGS=ruta` (otro fichero de ajustes).
- Mando (SDL_GameController, con conexion en caliente): la cruceta o la palanca izquierda se mapean girados como el teclado, X = boton 1
  (saltar), A = boton 2 (atacar), LB = A, RB = B, Start = +, Back = -, boton central = abrir/cerrar el menu F3 (dentro del menu: cruceta
  navega, A cambia, B cierra). Probado con un mando virtual de SDL: `RT_TEST_VPAD=<lectura>:<boton>`. Sin mapear aun: Classic Controller,
  GameCube, Nunchuk, puntero/giroscopio.
- Windows (x64, MSYS2 UCRT64): el mismo runtime compila con `make -j4` desde una terminal UCRT64 (paquetes `gcc make pkgconf SDL2 glew`) y
  genera `runtime/build-win/castlevania.exe` (+ `SDL2.dll`, `glew32.dll`, `libwinpthread-1.dll` copiados al lado); `runtime/play-windows.bat` lo lanza con
  `orig\game.dol`. Diferencias con Linux (`include/platform.h`, `src/platform_win.cpp`): memoria con `VirtualAlloc`, cada hilo del invitado es una
  fibra de Windows, `setjmp/longjmp` propios en ensamblador (el de la CRT desenrolla con SEH y no salta entre pilas), temporizador de 1 ms en un hilo
  (`CreateWaitableTimerEx` de alta resolucion) en vez de `SIGALRM`, y `MoveFileEx` en lugar de `rename`. `RT_SAMPLE=1` tambien funciona en Windows (hilo que suspende el hilo del juego cada ~1 ms; ver `play-windows-sample.bat`; los RIP se resuelven con `nm`). Probado
  sin ventana (`RT_HEADLESS=1`): arranque, hilos y 60 VI/s hasta el titulo; la ventana, el audio y el mando se prueban a mano.
- Rendimiento en Windows (2026-10-17): en escenas con ~6000 dibujos por fotograma el cuello de botella era la CPU del hilo del juego (escritura palabra a palabra al FIFO de GX y sobrecarga por dibujo), no la GPU. Ahora `ST32/STF/STD` a 0xCC008000 son una escritura inline a un buffer (`GXF32`, `native_lift.h`) y el interprete solo corre cuando el comando esta completo; `gx_draw` calcula cada vertice una vez y sin reloj ni `std::map` por dibujo. Resultado confirmado: 60 FPS estables en Windows.
- Hilo de render: toda la API de OpenGL corre en un hilo aparte (`gx_render.cpp`: cola de tareas `rq_push`, `render_thread_main`). El hilo del juego
  solo decodifica texturas (manejadores `TexRef`), transforma vertices, compone lotes de dibujo con una copia de los registros BP (`Batch`) y encola
  tambien las copias EFB (`render_copy`) y la presentacion (`render_present`, como mucho 2 fotogramas por delante). Un atajo en `gx_draw` evita
  recalcular el estado si no se escribio ningun registro BP relevante desde el dibujo anterior (`g_state_dirty`). Efecto: la presentacion/VSync ya no
  bloquea al juego y el coste por fotograma baja (Linux sin limite: ~65 -> ~92 fps de juego). `RT_FPS` muestra ahora "hilo de render" y las secciones
  de `gx_draw`. Solo `RT_HEADLESS=1` ejecuta las tareas al instante.
- Segundo plano: al perder el foco, ocultarse o minimizarse la ventana (p. ej. cambiar de espacio de trabajo) el juego y el audio se
  pausan (`pause_bg`, ajuste del menu F3) y el tiempo en pausa se descuenta del reloj del juego (`rt_time_skip`); antes el reloj saltaba al volver
  y el intercambio de buffers de una ventana oculta podia bloquearse. Prueba sin ventana: `RT_TEST_PAUSE=<fotograma>`.
- Pendiente: las 37 funciones sin elevar, ajustar detalles del renderizador (intensidades de copias EFB, Z, mas formatos),
  mando real/Classic/GameCube, rendimiento y los extras de audio de arriba.

### Elevador PowerPC -> C++ (`decomp/tools/lift.py`)

Traduce instruccion a instruccion las funciones de `game.dol` con ramas, llamadas y bucles:
registros como variables locales (`r0-r31` u32, `f0-f31` f64, `cr/ctr/lr/ca`), `goto` + etiquetas
en destinos, marco de pila como array local, llamadas con 8 enteros + 8 floats, retorno de 32 bits,
64 bits (r3:r4) o float segun como lo usa el llamador, tablas de saltos como `switch (ctr)`,
ayudantes `_savegpr/_restgpr` omitidos, `__ptmf_scall` expandido. Ayudantes en `include/ppc_lift.h`
(rotaciones, CR, carry, `fctiwz`, `mulhw`, `fma`...). Salida (gitignored, se regenera desde tu
DOL): `src/auto/lifted/lifted_NNN.cpp` (100 funciones por fichero, compila entero en ~10 s) y
`tests/difftest/lift_cases.py`. `scripts`: `python3 decomp/tools/lift.py --skip-trivial --max 100000
--out src/auto/lifted/lifted.cpp --shard 100`, `tests/difftest/build.sh`, `python3 tests/difftest/
fuzz.py 16 3 lift: -q -o tests/difftest/lift_status.txt`.

Estado (3000 funciones, 16 pruebas cada una, semilla 3; ver `tests/difftest/lift_status.txt`):
**2628 verificadas (87.6 %)**, 110 sin ejercitar (todas las pruebas se omitieron), 214 que el
fuzzer no sabe preparar (punteros/indices leidos de memoria que exigen estructuras concretas) y 47
con diferencias. Las diferencias restantes son en buena parte artefactos del banco: Unicorn
satura `fctiwz` de +desbordamiento a 0x80000000 (el hardware da 0x7FFFFFFF; el elevador sigue al
hardware), direcciones de pila que se filtran por punteros, y `divwu` por cero. 36 funciones no se
elevan (llamadas internas, `mtspr` de GQR...). Mejoras del banco que salieron de esto: stubs que solo
escriben los registros de retorno que el llamador lee, restauracion de TODAS las escrituras a datos
globales entre ejecuciones, estructuras `__ptmf` en la preparacion, deteccion de ayudantes de
registros, normalizacion de punteros a pila.

### Traductor automatico de funciones triviales (`decomp/tools/trivial.py`)

Casi un cuarto de las 4404 funciones del juego son getters/setters/constantes/thunks de 4 a 64
bytes sin ramas. `trivial.py` las ejecuta simbolicamente (r3-r10 y f1-f4 son argumentos; se pliegan
constantes; las direcciones SDA se resuelven) y emite C++ con accesores `LD32/ST32...` de
`include/guest_mem.h` (hoy punteros directos; el port nativo los redefinira sobre su arena de
memoria). Los saltos de cola (`b destino`, p. ej. thunks de `this`) se emiten como llamada con la
firma fija de 8 enteros + 4 floats. Salida: `src/auto/trivial.cpp` (funciones `dc_<nombre>`, aun
sin enlazar al juego) y `tests/difftest/auto_cases.py` (un caso de fuzzer por funcion). Resultado:
**1368 funciones**, todas verificadas con el fuzzer (30 pruebas cada una, 2 semillas, 0 fallos);
un cambio inyectado a mano en una de ellas se detecta. Las 8 que el fuzzer no sabe preparar
(indices leidos de memoria) estan en `decomp/tools/trivial_skip.txt`. Los pendientes por ramas,
llamadas o stack (`stwu`, `cmpwi`, `fcmpo`...) son los siguientes en la lista.
Mejoras del fuzzer: stubs por caso (`ACTIVE`), restauracion de datos entre casos, cadenas de
punteros, escrituras a registros de hardware (FIFO de GX en 0xCC008000) registradas en orden,
descubrimiento en dos fases para punteros nulos en BSS.

### Overlay: el decomp ejecutandose dentro del juego real (Dolphin)

Camino mas corto para "correr el decomp" sin recompilar `game.dol`: `scripts/build_overlay.sh`
compila `src/gameplay` a PowerPC y lo enlaza en la cima de MEM1 (`0x817C0000`, 256 KB
reservados; hoy ~3.5 KB). `decomp/tools/overlay.py` genera `GameSettings/WD9EA4.ini` de Dolphin
con parches `[OnFrame]`: la imagen del overlay y, en la entrada de cada funcion sustituida
(`src/hooks.txt`), un trampolin `lis/ori/mtctr/bctr` (r12 y CTR son volatiles). Sustituye la
funcion entera, no llama a la original. `scripts/play_overlay.sh [normal|selftest|nohook|off]`
instala el parche y lanza Dolphin con el WAD.

**Por que MEM1 y no MEM2 (error de la primera version, 0x93F00000):** el SDK del juego
(`BATConfig` -> `fn_8018E2B0`, 0x8018E2B0) deja las IBAT4-7 a 0, asi que NO se puede ejecutar
codigo en MEM2, y sus DBAT acaban en 0x93400000 (el parche ni siquiera escribia). El modo
`nohook` no fallaba porque nadie saltaba alli; `selftest` si saltaba y el juego se colgaba al
lanzar un arma. Lo unico ejecutable es MEM1 (IBAT0). Dolphin (HLE de IOS) pone el tope del
arena de MEM1 en 0x81800000 (`0x80003110`, lo lee el OS en 0x801872E8/0x8018BCC4), asi que el
parche baja ese valor a 0x817C0000 para que el juego no use la zona. Depende de que algun
parche se aplique entre que el cargador (`main.dol`) empieza y `game.dol` ejecuta su OSInit:
Dolphin aplica los `[OnFrame]` en cada frame de VI y el cargador tarda varios en descomprimir.

Prueba: `OVERLAY=1 tests/difftest/test_thrown.py` (incluida en `run_all.sh`) carga el overlay
real, pone los trampolines en las direcciones originales y compara la funcion original contra la
misma direccion ya parcheada, comprobando ademas que se ejecutan instrucciones del overlay. OJO:
Unicorn no modela las BAT, por eso esta prueba no detecto el error de MEM2; tampoco invalida su
cache de bloques al escribir codigo (`ctl_flush_tb`).

Restricciones para anadir un hook: la funcion debe estar verificada con difftest y sus llamadas
virtuales deben usar el layout de CodeWarrior (slot crudo, como `GetTrackedPos`), no vtables de
clang. Por eso `CTEngine::Update` y las clases de colision (usan virtuales de C++) aun no
estan en `src/hooks.txt`. Los constructores/destructores tampoco: fijarian el vptr de clang.
Nota: la clase que el RTTI llama `DrcSubWeaponKnife` (vtable 0x80253A30, instalada en
`fn_800A73D0`) es en realidad la base de las armas arrojadizas (hacha, cruz, reloj, agua bendita,
fuego); hay ademas una septima clase (fabrica, id 0x16, ctor `fn_800A8D40`, Update `fn_800A9030`).

### Mods de ModernGekko (pruebas dentro del juego): estado

`mods/decomp_test` + `scripts/play_mods.sh`. Los hooks funcionan para llamadas indirectas,
no para `bl` directos: el JIT de Dolphin sigue las ramas e incrusta la funcion llamada en
el bloque del llamador (`PPCAnalyst`, "Always follow BX"). Desactivar el seguimiento
(`MODERNGEKKO_MODS_NO_BRANCH_FOLLOW=1`, opt-in) hace que el juego ejecute una instruccion
invalida (`SIGILL` en `Interpreter::unknown_instruction`) al arrancar: causa sin
investigar. Por eso las pruebas se hacen con Unicorn (arriba).

## Pendiente / próximos pasos

- **Rendimiento: RESUELTO (~60 FPS)**. Dos causas, ninguna era la CPU:
  1. El ID del módulo debe coincidir con el que Dolphin calcula desde el TMD
     (`WD9E` + `group_id` = **`WD9EA4`**). Con un ID distinto
     (`WD9E01`, inventado en el `sys/boot.bin` sintético) el core
     `StaticRecomp` delega todo al JIT64 y los contadores `native=` quedan en 0.
     `sys/boot.bin` debe empezar con `WD9EA4`.
  2. `~/.local/share/moderngekko/config.ini` traía `resolution=1920x1080`
     (escala interna x3). Con `resolution=640x528` (x1, como Dolphin por
     defecto) corre a ~60 FPS con el backend C.
- **Módulo recompilado vs JIT**: en el Stage 1 el JIT de Dolphin
  (`MODERNGEKKO_STATICRECOMP=0`) da 60 FPS estables; el módulo recompilado
  (backend C) da ~46 con GCC `-O2` y ~49 con clang `-O3` + ThinLTO. No es un
  problema de flags: el backend C es más lento que el JIT. Para jugar se usa
  `scripts/play.sh` (JIT).
- **Backend LLVM (descartado por ahora)**: requiere `llvm19` (repo `extra`).
  Hay que añadir `ppc_native_region_available` a `GXRuntime/src/core/cpu.c`
  (`patches/dolphin-gxruntime-native-region-available.patch`) para que el
  módulo cargue, pero el juego se queda en 0 FPS en bucles de espera del
  arranque (probable fallo de interrupciones/tiempo en el backend LLVM).
- Investigar las 806 instrucciones "desconocidas" que DolRecomp no pudo
  decodificar.
- Verificar que los assets (sonido, niveles) carguen desde donde el juego
  realmente los busca — por ahora están en `extracted/castlevania-rebirth/files/`
  pero no está confirmado que el juego los lea de ahí correctamente (podría
  explicar diferencias de audio/timing).
- Investigar formato PSB (scripts del motor, usado extensivamente en los
  assets) si se necesita editar/entender la lógica de niveles.

## Cómo volver a correrlo

```sh
cd tools/ModernGekko-Template
make run GAME=castlevania-rebirth
```

Si se re-clona `ModernGekko-Template` desde cero, hay que reaplicar
`patches/moderngekko-wiiware-boot.patch` dentro de `lib/ModernGekko/` antes
de compilar, y recrear manualmente los archivos de NAND/config descritos
arriba (no están en este repo por ser derivados de material con copyright,
excepto `WiimoteNew.ini` que no tiene nada del juego).
