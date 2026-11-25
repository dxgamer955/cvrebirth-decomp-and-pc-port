#pragma once
// Menu HOME propio del port (tecla H / boton central del mando): pausa el juego; volver, guardar y cargar (5 ranuras con
// miniatura), ajustes, reiniciar, salir y creditos. El codigo Konami abre el selector de fases.
// Sustituye al menu HOME de la Wii, que necesita el contenido compartido del sistema (ver hle_cnt.cpp y RT_WII_HOME).
#include <vector>
enum { HOME_RESUME, HOME_SAVE, HOME_LOAD, HOME_SETTINGS, HOME_RESET, HOME_QUIT, HOME_N };
enum { HOME_VIEW_MAIN, HOME_VIEW_SAVE, HOME_VIEW_LOAD, HOME_VIEW_STAGE };
struct HomeView { int view; int sel; int slot; int stage; char msg[64]; };     // POD: viaja en PresentInfo al hilo de render
void home_draw(int w, int h, const HomeView& v);                                // hilo de render, sobre el fotograma ya presentado
// Hilo de render: datos de una ranura para las vistas de guardar/cargar (miniatura THUMB_W x THUMB_H RGB, o vacia)
void home_slot_set(int slot, bool used, const std::vector<unsigned char>& rgb, const char* const lines[4]);
[[noreturn]] void rt_restart();            // reinicia el juego desde cero (vuelve a lanzar el ejecutable con los mismos argumentos)
