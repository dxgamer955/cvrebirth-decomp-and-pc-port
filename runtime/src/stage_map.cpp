// Mapa de la fase (tecla M / SELECT): las salas de la fase vistas desde lejos en el centro de la pantalla, con el juego en pausa
// detras. Solo se ven las salas visitadas (en azul, la del jefe en rojo; la actual mas clara), cada una en su sitio de la fase
// entera, con una rejilla por pantalla (320x240) y un punto que parpadea donde esta el jugador. Abajo, las salas visitadas y el
// porcentaje.
// La disposicion de las salas la calcula scripts/make_map.py (el juego no tiene mapa); sin gen/ui_stagemap.h el mapa lo avisa.
#include "stage_map.h"
#include "game_state.h"
#include "ui_font.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#if __has_include("ui_stagemap.h")
#include "ui_stagemap.h"
#define RT_HAVE_STAGEMAP 1
#endif

static const int SCREEN_W = 320, SCREEN_H = 240;

#ifdef RT_HAVE_STAGEMAP
static const MapStage* stage_of(int stage) {
    if (stage < 1 || stage > 6) return nullptr;
    const MapStage* s = &MAP_STAGES[stage - 1];
    return s->rooms && s->n > 0 && s->n <= 64 ? s : nullptr;
}
static int room_index(const MapStage* s, const std::string& name) {           // sala dibujada (sigue los alias), o -1
    for (int i = 0; i < s->n; i++)
        if (name == s->rooms[i].name) { int a = s->rooms[i].alias; return a >= 0 && a < s->n ? a : i; }
    return -1;
}
#endif

bool map_view_build(MapView& v) {
    memset(&v, 0, sizeof v); v.cur = -1;
    std::string room; float x = 0, y = 0;
    if (!game_map_where(v.stage, room, x, y)) return false;
#ifdef RT_HAVE_STAGEMAP
    const MapStage* s = stage_of(v.stage);
    if (!s) return true;
    v.data = true;
    for (int i = 0; i < s->n; i++)
        if (game_map_visited(v.stage, s->rooms[i].name)) { int a = room_index(s, s->rooms[i].name); if (a >= 0) v.visited |= 1ull << a; }
    v.cur = room_index(s, room);
    if (v.cur >= 0) {
        const MapRoom& r = s->rooms[v.cur];
        v.visited |= 1ull << v.cur;
        float rx = std::fmin(std::fmax(x, 0.f), (float)r.w), ry = std::fmin(std::fmax(y - 16.f, 0.f), (float)r.h);   // el cuerpo, no los pies
        v.px = r.x + rx; v.py = r.y + ry;
    }
#endif
    return true;
}

static void frame_rect(float x, float y, float w, float h, float t, float r, float g, float b, float a) {   // solo el borde
    ui_rect(x, y, w, t, r, g, b, a); ui_rect(x, y + h - t, w, t, r, g, b, a);
    ui_rect(x, y + t, t, h - 2 * t, r, g, b, a); ui_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}
static void centered(float cx, float y, int sc, const char* t, float r, float g, float b) { ui_text(cx - ui_text_width(t, sc) / 2, y, sc, t, r, g, b); }

void map_draw(int w, int h, const MapView& v, int frame) {
    int sc = h >= 1000 ? 4 : (h >= 700 ? 3 : 2);
    float lh = (float)ui_char_h() * sc;
    float t = (float)(h >= 1000 ? 3 : (h >= 700 ? 2 : 1));                    // grosor de los bordes
    // Semitransparente: el juego se sigue viendo detras del mapa (las letras y el jugador, opacos)
    const float A_PANEL = 0.45f, A_BORDER = 0.75f, A_ROOM = 0.62f, A_ROOM_EDGE = 0.85f;
    float pw = std::floor(w * 0.86f), ph = std::floor(h * 0.80f), px0 = std::floor((w - pw) / 2), py0 = std::floor((h - ph) / 2);
    frame_rect(px0 - t, py0 - t, pw + 2 * t, ph + 2 * t, t, 0.62f, 0.66f, 0.80f, A_BORDER);
    ui_rect(px0, py0, pw, ph, 0.02f, 0.02f, 0.07f, A_PANEL);
    char title[32]; snprintf(title, sizeof title, "STAGE %d", v.stage);
    centered(px0 + pw / 2, py0 + lh, sc, title, 1.f, 0.85f, 0.35f);
    centered(px0 + pw / 2, py0 + ph - lh * 1.8f, sc, "M: CLOSE", 0.6f, 0.7f, 0.9f);
#ifdef RT_HAVE_STAGEMAP
    const MapStage* s = v.data ? stage_of(v.stage) : nullptr;
#else
    const void* s = nullptr;
#endif
    if (!s) {
        centered(px0 + pw / 2, py0 + ph / 2 - lh / 2, sc, "NO MAP DATA (SEE BUILD.MD)", 0.9f, 0.9f, 0.95f);
        return;
    }
#ifdef RT_HAVE_STAGEMAP
    // Area del mapa: entre el titulo y el pie, con margen; la escala que hace caber la fase entera
    float ax = px0 + lh * 2, ay = py0 + lh * 3, aw = pw - lh * 4, ah = ph - lh * 6.5f;
    float k = std::fmin(aw / s->w, ah / s->h);
    k = std::fmin(k, (h / 7.f) / SCREEN_H);                                   // una pantalla del juego como mucho 1/7 de la ventana de alto
    float ox = std::floor(ax + (aw - s->w * k) / 2), oy = std::floor(ay + (ah - s->h * k) / 2);
    auto X = [&](float x) { return std::floor(ox + x * k + 0.5f); };
    auto Y = [&](float y) { return std::floor(oy + y * k + 0.5f); };
    int total = 0, seen = 0;
    for (int i = 0; i < s->n; i++) {
        const MapRoom& r = s->rooms[i];
        if (r.alias >= 0) continue;
        total++;
        bool vis = (v.visited >> i) & 1, cur = i == v.cur;
        if (vis) seen++;
        float x0 = X((float)r.x), y0 = Y((float)r.y), x1 = X((float)(r.x + r.w)), y1 = Y((float)(r.y + r.h));
        float rw = x1 - x0, rh = y1 - y0;
        if (rw < 2 * t + 1 || rh < 2 * t + 1) continue;
        if (!vis) continue;                                                   // por explorar: no se ve
        float fr, fg, fb;
        if (r.boss) { fr = 0.72f; fg = 0.12f; fb = 0.16f; }
        else if (cur) { fr = 0.22f; fg = 0.42f; fb = 0.95f; }
        else { fr = 0.10f; fg = 0.24f; fb = 0.72f; }
        frame_rect(x0, y0, rw, rh, t, 0.88f, 0.90f, 0.96f, A_ROOM_EDGE);
        ui_rect(x0 + t, y0 + t, rw - 2 * t, rh - 2 * t, fr, fg, fb, A_ROOM);
        if (SCREEN_W * k >= 10) {                                             // rejilla de pantallas, como las casillas de un mapa clasico
            for (int gx = SCREEN_W; gx < r.w - 8; gx += SCREEN_W) ui_rect(X((float)(r.x + gx)), y0 + t, 1, rh - 2 * t, fr * 0.6f, fg * 0.6f, fb * 0.6f, A_ROOM);
            for (int gy = SCREEN_H; gy < r.h - 8; gy += SCREEN_H) ui_rect(x0 + t, Y((float)(r.y + gy)), rw - 2 * t, 1, fr * 0.6f, fg * 0.6f, fb * 0.6f, A_ROOM);
        }
    }
    if (v.cur >= 0 && (frame / 16) % 3 != 2) {                               // el jugador: parpadea
        float d = std::fmax(3 * t + 4, std::floor(SCREEN_H * k / 3.5f)), cx = X(v.px), cy = Y(v.py);
        ui_rect(cx - d / 2 - t, cy - d / 2 - t, d + 2 * t, d + 2 * t, 0.f, 0.f, 0.f, 1.f);
        ui_rect(cx - d / 2, cy - d / 2, d, d, 1.f, 0.95f, 0.55f, 1.f);
    }
    char info[48]; snprintf(info, sizeof info, "ROOMS %d/%d   %d%%", seen, total, total ? seen * 100 / total : 0);
    centered(px0 + pw / 2, py0 + ph - lh * 3.3f, sc, info, 0.9f, 0.9f, 0.95f);
#endif
}
