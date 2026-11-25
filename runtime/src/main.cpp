// Punto de entrada del runtime nativo: reserva la memoria del invitado, carga game.dol y lo ejecuta.
#include "hle.h"
#include <sys/stat.h>
#include "../gen/decls.h"
#include "../gen/syms.h"
#include <cstdio>
#include <string>
#include "gx.h"
#include "game_state.h"
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

static void init_low_memory() {
    // Valores que deja el HLE de IOS de Dolphin para un titulo WiiWare (ver Source/Core/Core/IOS/IOS.cpp).
    ST32(0x80000000, 0x57443945);         // "WD9E"
    ST32(0x80000020, 0x0D15EA5E);         // palabra magica
    ST32(0x80000024, 1);
    ST32(0x80000028, 0x01800000);         // MEM1 24 MB
    ST32(0x800000F8, 0x0E7BE2C0);         // reloj del bus
    ST32(0x800000FC, 0x2B73A840);         // reloj de la CPU
    ST32(0x80003100, 0x01800000);         // MEM1 fisica
    ST32(0x80003104, 0x01800000);         // MEM1 simulada
    ST32(0x80003108, 0x81800000);         // fin de MEM1
    ST32(0x8000310C, 0);                  // inicio del arena de MEM1
    ST32(0x80003110, RT_SCRATCH);         // fin del arena de MEM1: los ultimos 4 KB quedan para el port (RT_SCRATCH, hle.h)
    ST32(0x80003118, 0x04000000);         // MEM2 fisica 64 MB
    ST32(0x8000311C, 0x04000000);
    ST32(0x80003120, 0x93400000);         // fin de MEM2
    ST32(0x80003124, 0x90000800);         // inicio del arena de MEM2
    ST32(0x80003128, 0x933E0000);         // fin del arena de MEM2
    ST32(0x80003130, 0x933E0000);         // buffer IPC
    ST32(0x80003134, 0x93400000);
    ST32(0x800030E6, 0x8000);             // BI2: no depuracion
    ST32(0x800030F0, 0);
}

extern std::string g_files_root;
extern std::string g_nand_root;
extern std::string g_shared_root;
bool g_wii_home = false;                   // RT_WII_HOME=1 y hay contenido compartido: la tecla HOME va al juego (menu HOME de la Wii)
extern int g_argc; extern char** g_argv;   // home_menu.cpp (reiniciar)
int main(int argc, char** argv) {
    g_argc = argc; g_argv = argv;
    // Sin argumentos (doble clic en Windows, paquete portable): si junto al ejecutable hay orig/game.dol, se trabaja desde esa
    // carpeta (datos, partidas en nand/, settings.cfg y gen/osfont_ansi.bin), venga de donde venga el directorio de trabajo.
    struct stat st;
    if (argc <= 1) if (char* base = SDL_GetBasePath()) {
        if (stat((std::string(base) + "orig/game.dol").c_str(), &st) == 0 && chdir(base) != 0) rt_log("[rt] no se pudo entrar en %s\n", base);
        SDL_free(base);
    }
    const char* dol = argc > 1 ? argv[1] : "orig/game.dol";
    if (stat(dol, &st) != 0)
        rt_fatal("No se encuentra %s.\nCopia game.dol y la carpeta files (orig/ de tu compilacion) en una carpeta orig junto al ejecutable.\n"
                 "%s not found: put game.dol and the files folder in an orig folder next to the executable.", dol, dol);
    // Carpeta con los ficheros extraidos del juego: 2.o argumento, RT_FILES, orig/files o ../orig/files si existen, o la ruta local por defecto
    if (argc > 2) g_files_root = argv[2];
    else if (const char* f = getenv("RT_FILES")) g_files_root = f;
    else if (stat("orig/files", &st) == 0) g_files_root = "orig/files";
    else if (stat("../orig/files", &st) == 0) g_files_root = "../orig/files";
    // Contenido compartido (menu HOME): 3.er argumento, RT_SHARED o la carpeta "shared" junto a la de los datos
    if (argc > 3) g_shared_root = argv[3];
    else if (const char* s = getenv("RT_SHARED")) g_shared_root = s;
    else { size_t sl = g_files_root.find_last_of("/\\"); g_shared_root = (sl == std::string::npos ? std::string(".") : g_files_root.substr(0, sl)) + "/shared"; }
    if (stat((g_shared_root + "/HomeButton3").c_str(), &st) != 0) g_shared_root.clear();
    // Por defecto H abre el menu HOME propio del port (gx_render.cpp / home_menu.cpp). RT_WII_HOME=1 envia HOME al juego, que abre el
    // de la Wii; necesita el contenido compartido (sin el, el juego muestra "Could not access Wii system memory").
    if (getenv("RT_WII_HOME")) {
        g_wii_home = !g_shared_root.empty();
        if (!g_wii_home) rt_log("[rt] RT_WII_HOME: falta el contenido compartido del menu HOME de la Wii (carpeta shared junto a files); se usa el del port\n");
    }
    if (const char* n = getenv("RT_NAND")) g_nand_root = n;

    rt_init_memory();
    gx_video_init();
    game_state_startup();                                    // ranura o fase pendientes (cargar / selector de fases del menu HOME)
    rt_load_dol(dol);
    init_low_memory();
    RT_MTSPR(914, 0x00040004); RT_MTSPR(915, 0x00050005); RT_MTSPR(916, 0x00060006); RT_MTSPR(917, 0x00070007);   // GQR2-5 (OSInitFastCast)
    g_sp = 0x80329750u;               // pila del hilo principal (como __init_registers)
    // Efectos visibles de OSInit: arenas de memoria (MEM1 y MEM2) segun la memoria baja.
    auto call = [](u32 fn, u32 a0 = 0, u32 a1 = 0) { return rt_call_indirect(fn, a0, a1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); };
    call(SYM_OSSetMEM1ArenaLo, 0x8032B760u);
    call(SYM_OSSetMEM1ArenaHi, LD32(0x80003110));
    call(SYM_OSSetMEM2ArenaLo, LD32(0x80003124));
    call(SYM_OSSetMEM2ArenaHi, LD32(0x80003128));
    call(SYM___OSThreadInit);                                // hilo principal e inactivo del planificador
    rt_log("[rt] game.dol cargado; ejecutando __init_user y main\n");
    void rt_start_vi_timer(); rt_start_vi_timer();
    rt_call_indirect(SYM___init_user, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rt_log("[rt] __init_user terminado\n");
    call(SYM___OSInitIPCBuffer);
    call(SYM___OSInitSTM);                                    // abre /dev/stm y registra el hook de eventos                              // arena de IPC para los buffers de IOS
    rt_log("[rt] IPCGetBufferLo=%08X Hi=%08X\n", call(SYM_IPCGetBufferLo).r3, call(SYM_IPCGetBufferHi).r3);
    rt_log("[rt] IPC buffer %08X-%08X; IPCCltInit -> %d\n", LD32(0x80315F18), LD32(0x80316800), (int)call(SYM_IPCCltInit).r3);
    rt_log("[rt] NANDInit -> %d\n", (int)call(SYM_NANDInit).r3);
    rt_call_indirect(SYM_main, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return 0;
}
