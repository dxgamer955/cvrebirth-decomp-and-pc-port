// Mando (SDL_GameController): se mapea al Wii Remote en horizontal igual que el teclado.
#include "pad.h"
#include <cstdio>

static SDL_GameController* g_pad = nullptr;
void pad_open_any() {
    if (g_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) {
        g_pad = SDL_GameControllerOpen(i);
        if (g_pad) { fprintf(stderr, "[input] mando: %s\n", SDL_GameControllerName(g_pad)); return; }
    }
}
void pad_event(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERDEVICEADDED) pad_open_any();
    else if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)) == e.cdevice.which) {
        fprintf(stderr, "[input] mando desconectado\n"); SDL_GameControllerClose(g_pad); g_pad = nullptr; pad_open_any();
    }
}
bool pad_btn(SDL_GameControllerButton b) { return g_pad && SDL_GameControllerGetButton(g_pad, b) != 0; }
static float pad_axis(SDL_GameControllerAxis a) { return g_pad ? SDL_GameControllerGetAxis(g_pad, a) / 32767.f : 0.f; }
// Direcciones (de pantalla) del mando: cruceta o palanca izquierda.
void pad_dirs(bool& l, bool& r, bool& u, bool& d) {
    float lx = pad_axis(SDL_CONTROLLER_AXIS_LEFTX), ly = pad_axis(SDL_CONTROLLER_AXIS_LEFTY);
    l = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -0.5f; r = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 0.5f;
    u = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -0.5f;   d = pad_btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 0.5f;
}
// X = boton 1 (saltar), A = boton 2 (atacar), LB = A, RB = B, Start = +, Back = -. (El boton central abre el menu F3.)
u32 pad_hold(bool upright) {
    if (!g_pad) return 0;
    u32 h = 0; bool l, r, u, d; pad_dirs(l, r, u, d);
    if (upright) { if (l) h |= 0x0001; if (r) h |= 0x0002; if (d) h |= 0x0004; if (u) h |= 0x0008; }
    else { if (l) h |= 0x0008; if (r) h |= 0x0004; if (u) h |= 0x0002; if (d) h |= 0x0001; }
    if (pad_btn(SDL_CONTROLLER_BUTTON_X)) h |= 0x0200;
    if (pad_btn(SDL_CONTROLLER_BUTTON_A)) h |= 0x0100;
    if (pad_btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) h |= 0x0800;
    if (pad_btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) h |= 0x0400;
    if (pad_btn(SDL_CONTROLLER_BUTTON_START)) h |= 0x0010;
    if (pad_btn(SDL_CONTROLLER_BUTTON_BACK)) h |= 0x1000;
    return h;
}

