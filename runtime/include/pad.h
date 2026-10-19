#pragma once
// Mando (SDL_GameController) mapeado al Wii Remote en horizontal.
#include "native_lift.h"
#include <SDL2/SDL.h>

void pad_open_any();                                      // abre el primer mando disponible
void pad_event(const SDL_Event& e);                       // altas y bajas de mandos
bool pad_btn(SDL_GameControllerButton b);
void pad_dirs(bool& l, bool& r, bool& u, bool& d);        // cruceta o palanca izquierda (direcciones de pantalla)
u32 pad_hold(bool upright);                               // botones del Wii Remote que corresponden al estado del mando
