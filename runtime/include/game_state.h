#pragma once
// Estado de la partida del juego (Castlevania ReBirth) que usa el port: ranuras de guardado del menu HOME y selector de fases.
// Direcciones y campos encontrados con el runtime (ver PROGRESS.md, "Guardado y selector de fases").
#include "native_lift.h"
#include <string>
#include <vector>

static const u32 GAME_PTR = 0x80316480;            // -> objeto de partida: +0x1c vidas, +0x20 puntuacion, +0x28 fase (1-6),
                                                   //    +0x2c tiempo (fotogramas), +0x1fc subarma, +0x200 nivel del latigo
static const u32 GAME_VTABLE = 0x80244820;
static const u32 GAME_SETTINGS_PTR = 0x80316530;   // -> ajustes de setting.csv (start_map, stage_start...)
static const u32 FIELD_PTR = 0x8031647C;           // -> gestor de campo: +0x168 mapa actual, +0x174 entrada, +0x220 jugador, +0x2b4 mapa anterior
static const u32 FIELD_VTABLE = 0x802455B0;
static const u32 PLAYER_VTABLE = 0x802530E0;       // jugador: +0x78/+0x7c posicion, +0x204 vida (float), +0x208 corazones,
                                                   //    +0x20c latigo, +0x234 subarma

enum { SAVE_SLOTS = 5, THUMB_W = 160, THUMB_H = 120 };

// Arranque pendiente: lo aplican los ganchos (game_hooks.cpp) cuando el juego empieza una partida
struct PendingStart { int stage = 0; char map[64] = {}; };
extern PendingStart g_pending_start;

struct SlotInfo {
    bool used = false;
    int stage = 0;
    std::string map, date;                         // map = "mapa:entrada"
    u32 lives = 0, score = 0, time = 0, hearts = 0, subweapon = 0, whip = 0;
    float hp = 0;
};

bool game_slot_read(int slot, SlotInfo& out, std::vector<u8>* thumb_rgb);   // thumb: THUMB_W x THUMB_H RGB
bool game_can_save();                              // hay una fase en curso con jugador
bool game_save_slot(int slot, const std::vector<u8>& thumb_rgb);
[[noreturn]] void game_load_slot(int slot);        // relanza el juego y lo lleva a la ranura
[[noreturn]] void game_start_stage(int stage);     // selector de fases: relanza y empieza en esa fase
void game_state_startup();                         // main.cpp: RT_LOAD_SLOT / RT_START_STAGE (puestos antes de relanzar)
void game_state_tick();                            // cada retrazado (hle_vi.cpp)
bool game_autostart_active();                      // gx_input_buttons: pulsar "confirmar" hasta que empiece la partida
void game_hook_start(u32 flow_obj);                // gancho: fn_8002BF50 (empezar partida en la fase de obj+0xc4)
void game_hook_field();                            // gancho: DrcDebugPrimitive_ctor (gestor de campo; lee start_map)
void game_hook_player(u32 player);                 // gancho: fn_8009F0C0 (inicializacion del jugador)
