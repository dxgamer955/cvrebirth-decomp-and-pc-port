#pragma once
// Mapa de la fase del port (tecla M / SELECT del mando): pausa el juego y dibuja en el centro de la pantalla las salas de la
// fase (gen/ui_stagemap.h, que scripts/make_map.py saca de tu copia del juego), las visitadas, la actual y donde esta el jugador.
#include <cstdint>
struct MapView {                           // POD: viaja en PresentInfo al hilo de render
    bool data;                             // hay mapa de esta fase (gen/ui_stagemap.h)
    int stage;                             // 1-6
    int cur;                               // sala dibujada en la que esta el jugador (-1 ninguna)
    float px, py;                          // posicion del jugador en el mapa (pixeles de la fase)
    uint64_t visited;                      // bit i: sala dibujada i visitada
};
bool map_view_build(MapView& v);           // hilo del juego: false si no hay una fase en curso
void map_draw(int w, int h, const MapView& v, int frame);   // hilo de render, sobre el fotograma ya presentado
