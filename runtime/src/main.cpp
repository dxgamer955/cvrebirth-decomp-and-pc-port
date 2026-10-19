// Punto de entrada del runtime nativo: reserva la memoria del invitado, carga game.dol y lo ejecuta.
#include "hle.h"
#include <sys/stat.h>
#include "../gen/decls.h"
#include "../gen/syms.h"
#include <cstdio>
#include <string>
#include "gx.h"

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
    ST32(0x80003110, 0x81800000);         // fin del arena de MEM1
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
int main(int argc, char** argv) {
    const char* dol = argc > 1 ? argv[1] : "orig/game.dol";
    // Carpeta con los ficheros extraidos del juego: 2.o argumento, RT_FILES, ../orig/files si existe o la ruta local por defecto
    if (argc > 2) g_files_root = argv[2];
    else if (const char* f = getenv("RT_FILES")) g_files_root = f;
    else { struct stat st; if (stat("../orig/files", &st) == 0) g_files_root = "../orig/files"; }
    if (const char* n = getenv("RT_NAND")) g_nand_root = n;
    rt_init_memory();
    gx_video_init();
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
