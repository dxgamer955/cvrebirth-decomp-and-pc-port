// Ganchos de entrada en funciones del juego (runtime/hooks.txt): el port actua en puntos concretos del flujo del juego para
// el selector de fases y las ranuras de guardado (game_state.cpp).
#include "hle.h"
#include "game_state.h"

void rt_hook(u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    (void)a1; (void)a2; (void)a3;
    switch (addr) {
    case 0x8002BF50: game_hook_start(a0); break;   // empezar partida: fase = obj+0xc4 (0 = partida nueva desde la fase 1 con su demo)
    case 0x8000E090: game_hook_field(); break;     // gestor de campo (DrcDebugPrimitive_ctor): lee el ajuste start_map ("mapa:entrada")
    case 0x8009F0C0: game_hook_player(a0); break;  // inicializacion del jugador (vida, corazones, subarma y latigo desde la partida)
    }
}
