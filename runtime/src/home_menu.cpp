// Menu HOME propio del port: barra superior, botones (volver, guardar, cargar, ajustes, reiniciar, salir), creditos de la compilacion,
// ranuras de guardado con miniatura y el selector de fases secreto.
// Diseno original con la fuente 5x7 del runtime (no usa graficos de Nintendo ni del juego).
#include "home_menu.h"
#include "hle.h"
#include "ui_font.h"
#include "game_state.h"
#include "build_info.h"                    // generado por el Makefile: RT_BUILD_ID (commit) y RT_BUILD_DATE
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <GL/glew.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/time.h>
#include <unistd.h>
#endif

int g_argc = 0; char** g_argv = nullptr;   // main.cpp los guarda para rt_restart


// La fuente solo tiene mayusculas, cifras y . : - < > / ( ) + , !  -> lo demas se cambia por espacios
static std::string clean(const char* s) {
    std::string o; for (const char* q = s; *q; q++) { char c = *q; if (c >= 'a' && c <= 'z') c -= 32; o += strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-<>/()+,! ", c) ? c : ' '; }
    return o;
}
static std::string compiler() {
#if defined(__clang__)
    return clean(("CLANG " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." + std::to_string(__clang_patchlevel__)).c_str());
#elif defined(__GNUC__)
    return clean(("GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__)).c_str());
#else
    return "UNKNOWN";
#endif
}
static const char* platform() {
#if defined(_WIN32)
    return "WINDOWS X86-64";
#elif defined(__APPLE__)
    return "MACOS";
#else
    return "LINUX X86-64";
#endif
}

// ---- ranuras (solo hilo de render) ----
static GLuint s_tex[SAVE_SLOTS]; static bool s_used[SAVE_SLOTS], s_has_thumb[SAVE_SLOTS]; static char s_lines[SAVE_SLOTS][4][40];
void home_slot_set(int i, bool used, const std::vector<unsigned char>& rgb, const char* const lines[4]) {
    if (i < 0 || i >= SAVE_SLOTS) return;
    s_used[i] = used; for (int k = 0; k < 4; k++) snprintf(s_lines[i][k], sizeof s_lines[i][k], "%s", lines[k] ? clean(lines[k]).c_str() : "");
    s_has_thumb[i] = rgb.size() == (size_t)THUMB_W * THUMB_H * 3;
    if (!s_has_thumb[i]) return;
    if (!s_tex[i]) glGenTextures(1, &s_tex[i]);
    glBindTexture(GL_TEXTURE_2D, s_tex[i]); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, THUMB_W, THUMB_H, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static void box(float x, float y, float w, float h, float border, bool sel) {
    if (sel) ui_rect(x - border, y - border, w + 2 * border, h + 2 * border, 1.f, 1.f, 1.f, 1.f);
    else ui_rect(x - border, y - border, w + 2 * border, h + 2 * border, 0.45f, 0.50f, 0.62f, 1.f);
    if (sel) ui_rect(x, y, w, h, 0.22f, 0.48f, 0.92f, 1.f); else ui_rect(x, y, w, h, 0.13f, 0.15f, 0.24f, 1.f);
}
static void centered(float cx, float y, int sc, const char* t, float r, float g, float b) { ui_text(cx - ui_text_width(t, sc) / 2, y, sc, t, r, g, b); }

static void draw_credits(int w, int h, int sc) {
    std::string l_build = clean(("BUILD " RT_BUILD_ID "   " RT_BUILD_DATE));
    std::string l_comp = "COMPILER: " + compiler() + "   PLATFORM: " + platform();
    const char* lines[] = {
        "CASTLEVANIA: THE ADVENTURE REBIRTH - NATIVE PC PORT",
        "AUTHOR: DXG95",
        l_build.c_str(),
        l_comp.c_str(),
        "TOOLS: GHIDRA, DECOMP-TOOLKIT (DTK), PPC TO C++ LIFTER (PYTHON)",
        "SDL2, OPENGL 3.3, GLEW, DOLPHIN (HARDWARE REFERENCE)",
        "UNOFFICIAL FAN PORT. ORIGINAL GAME (C) 2009 KONAMI.",
    };
    const int nl = sizeof lines / sizeof lines[0];
    int cs = h >= 1000 ? 3 : 2;                                     // los creditos, tan grandes como quepan a lo ancho
    float maxw = 0; for (const char* l : lines) { float lw = ui_text_width(l, cs); if (lw > maxw) maxw = lw; }
    while (cs > 1 && maxw + 24.f * cs > w) { cs--; maxw = 0; for (const char* l : lines) { float lw = ui_text_width(l, cs); if (lw > maxw) maxw = lw; } }
    float lh = (ui_char_h() + 4.f) * cs, ph = nl * lh + 16.f * cs, py = h - ph;
    ui_rect(0, py - sc, (float)w, (float)sc, 0.35f, 0.60f, 1.00f, 1.f);
    ui_rect(0, py, (float)w, ph, 0.10f, 0.12f, 0.20f, 0.97f);
    for (int i = 0; i < nl; i++) {
        float r = 0.85f, g = 0.88f, b = 0.95f;
        if (i == 0) { r = 1.f; g = 0.85f; b = 0.35f; }            // titulo
        else if (i == 1) { r = 0.55f; g = 0.80f; b = 1.f; }       // autor
        centered(w / 2.f, py + 8.f * cs + i * lh, cs, lines[i], r, g, b);
    }
}

void home_draw(int w, int h, const HomeView& v) {
    const int cw = ui_char_w(), ch = ui_char_h();
    const float bwu = 8.f * cw + 14.f, bhu = ch + 19.f;            // boton: 8 caracteres de ancho + margen (62 x 26 con la fuente 5x7)
    int sc = h >= 1000 ? 4 : (h >= 700 ? 3 : 2);
    while (sc > 1 && (6 * bwu + 5 * 6 + 16) * sc > w) sc--;
    ui_rect(0, 0, (float)w, (float)h, 0.f, 0.f, 0.f, 0.62f);     // el juego queda detras, oscurecido

    // Barra superior
    static const char* titles[] = { "HOME MENU", "SAVE GAME", "LOAD GAME", "STAGE SELECT" };
    float bar = (ch + 13.f) * sc;
    ui_rect(0, 0, (float)w, bar, 0.10f, 0.12f, 0.20f, 0.97f);
    ui_rect(0, bar, (float)w, (float)sc, 0.35f, 0.60f, 1.00f, 1.f);
    ui_text(8.f * sc, (bar - ch * sc) / 2, sc, titles[v.view & 3], 1.f, 1.f, 1.f);
    const char* hint = v.view == HOME_VIEW_MAIN ? "H / ESC: CLOSE" : "ESC / B: BACK";
    ui_text(w - 8.f * sc - ui_text_width(hint, sc), (bar - ch * sc) / 2, sc, hint, 0.65f, 0.75f, 0.95f);
    int ns = sc > 1 ? sc - 1 : 1;

    if (v.view == HOME_VIEW_MAIN) {
        static const char* const LABELS[HOME_N] = { "RESUME", "SAVE", "LOAD", "SETTINGS", "RESET", "QUIT" };
        float bw = bwu * sc, bh = bhu * sc, gap = 6.f * sc, total = HOME_N * bw + (HOME_N - 1) * gap;
        float x0 = (w - total) / 2, y0 = h * 0.33f - bh / 2;
        for (int i = 0; i < HOME_N; i++) {
            float x = x0 + i * (bw + gap); bool s = i == v.sel;
            box(x, y0, bw, bh, s ? 3.f * sc : (float)sc, s);
            centered(x + bw / 2, y0 + (bh - ch * sc) / 2, sc, LABELS[i], 1.f, 1.f, s ? 1.f : 0.85f);
        }
        centered(w / 2.f, y0 + bh + 10.f * sc, ns, "LEFT/RIGHT: SELECT   ENTER / 2 / A: CONFIRM", 0.70f, 0.75f, 0.85f);
        if (v.msg[0]) centered(w / 2.f, y0 + bh + 24.f * sc, ns, v.msg, 1.f, 0.85f, 0.35f);
        draw_credits(w, h, sc);
        return;
    }
    if (v.view == HOME_VIEW_STAGE) {
        float bw = bwu * sc, bh = bhu * sc, gap = 6.f * sc, total = 6 * bw + 5 * gap;
        float x0 = (w - total) / 2, y0 = h * 0.42f - bh / 2;
        for (int i = 0; i < 6; i++) {
            char t[16]; snprintf(t, sizeof t, "STAGE %d", i + 1);
            float x = x0 + i * (bw + gap); bool s = i + 1 == v.stage;
            box(x, y0, bw, bh, s ? 3.f * sc : (float)sc, s);
            centered(x + bw / 2, y0 + (bh - ch * sc) / 2, sc, t, 1.f, 1.f, s ? 1.f : 0.85f);
        }
        centered(w / 2.f, y0 - 20.f * sc, ns, "SECRET UNLOCKED: START ANY STAGE", 1.f, 0.85f, 0.35f);
        centered(w / 2.f, y0 + bh + 10.f * sc, ns, "LEFT/RIGHT: STAGE   ENTER / 2 / A: START   ESC / B: BACK", 0.70f, 0.75f, 0.85f);
        return;
    }
    // Guardar / cargar: cinco tarjetas con miniatura
    float gap = 8.f * sc, avail_w = w - 2 * 12.f * sc - (SAVE_SLOTS - 1) * gap, avail_h = h - 2 * bar - 90.f * sc;
    float ts = avail_w / (SAVE_SLOTS * THUMB_W); if (ts * THUMB_H > avail_h) ts = avail_h / THUMB_H; if (ts > 3.f) ts = 3.f;
    float tw = THUMB_W * ts, th = THUMB_H * ts, total = SAVE_SLOTS * tw + (SAVE_SLOTS - 1) * gap;
    int cs = ts >= 1.6f ? 2 : 1; float lh = (ch + 3.f) * cs;
    float x0 = (w - total) / 2, y0 = bar + (h - bar - (th + 4 * lh + 40.f * sc)) / 2;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        float x = x0 + i * (tw + gap); bool s = i == v.slot; float b = s ? 3.f * sc : (float)sc;
        box(x, y0, tw, th + 4 * lh + 6.f * cs, b, s);
        if (s_used[i] && s_has_thumb[i]) ui_image(s_tex[i], x, y0, tw, th);
        else { ui_rect(x, y0, tw, th, 0.05f, 0.05f, 0.08f, 1.f); centered(x + tw / 2, y0 + th / 2 - ch * cs / 2.f, cs, s_used[i] ? "NO IMAGE" : "EMPTY", 0.55f, 0.6f, 0.7f); }
        for (int k = 0; k < 4; k++) centered(x + tw / 2, y0 + th + 4.f * cs + k * lh, cs, s_lines[i][k], k == 0 ? 1.f : 0.85f, k == 0 ? 0.85f : 0.88f, k == 0 ? 0.35f : 0.95f);
    }
    float hy = y0 + th + 4 * lh + 6.f * cs + 12.f * sc;
    centered(w / 2.f, hy, ns, v.view == HOME_VIEW_SAVE ? "LEFT/RIGHT: SLOT   ENTER / 2 / A: SAVE HERE   ESC / B: BACK" : "LEFT/RIGHT: SLOT   ENTER / 2 / A: LOAD   ESC / B: BACK", 0.70f, 0.75f, 0.85f);
    if (v.msg[0]) centered(w / 2.f, hy + 14.f * sc, ns, v.msg, 1.f, 0.85f, 0.35f);
}

// Reinicio: el estado del juego (memoria del invitado, hilos, audio, GPU) vive en este proceso, asi que se vuelve a lanzar el
// ejecutable con los mismos argumentos, entorno y directorio. Partidas y ajustes estan en disco.
void rt_restart() {
    rt_log("[home] reiniciando\n");
    fflush(stdout); fflush(stderr);
#ifdef _WIN32
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = GetCommandLineW();
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi;
    if (CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); ExitProcess(0);
    }
#else
    // Los temporizadores de intervalo sobreviven a execv y SIGALRM/SIGPROF volverian a la accion por defecto (terminar):
    // se paran y se ignoran (una senal ignorada pendiente se descarta); el proceso nuevo instala sus manejadores.
    struct itimerval off; memset(&off, 0, sizeof off); setitimer(ITIMER_REAL, &off, nullptr); setitimer(ITIMER_PROF, &off, nullptr);
    signal(SIGALRM, SIG_IGN); signal(SIGPROF, SIG_IGN);
    if (g_argv) execv("/proc/self/exe", g_argv);
#endif
    rt_fatal("no se pudo reiniciar el juego");
}
