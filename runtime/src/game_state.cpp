// Ranuras de guardado y selector de fases del port.
//
// Un guardado exacto del instante no es posible en un port recompilado (los registros del juego viven en la pila nativa), asi que
// cada ranura guarda el punto restaurable mas reciente (fase + "mapa:entrada", el mapa por el que entraste) y los datos del
// jugador. Cargar relanza el ejecutable, avanza solo por los menus pulsando "confirmar" y los ganchos (game_hooks.cpp) hacen que
// el juego empiece en esa fase y ese mapa; despues se restauran vidas, puntuacion, tiempo, vida, corazones, subarma y latigo.
#include "game_state.h"
#include "hle.h"
#include "home_menu.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(p, m) _mkdir(p)
#endif

PendingStart g_pending_start;
extern std::string g_nand_root;
extern std::atomic<bool> g_audio_quiet;

static u32 s_flow_obj = 0;                 // objeto del flujo de partida (fn_8002A700 / fn_8002BF50): +0xc8 stage_start, +0xd4 start_map
static SlotInfo s_load;                    // ranura que se esta cargando
static bool s_load_active = false, s_apply_player = false;
static bool s_autostart = false;
void gx_fast_forward(bool on);             // gx_render.cpp: sin limite y sin vsync mientras dura el arranque automatico
static unsigned s_autostart_frames = 0;
static int s_point_stage = 0; static std::string s_point;        // ultimo punto restaurable
static std::string s_cur_map; static bool s_arrival = false;
// Jugador vivo: el que el juego inicializo (fn_8009F0C0) despues del ultimo gestor de campo. Justo tras construirse el gestor,
// su +0x220 aun apunta al jugador anterior (p. ej. el de la pantalla del mapa del castillo, ya liberado): escribir ahi corrompia el heap.
static u32 s_live_player = 0; static unsigned s_live_ticks = 0;

// ---- utilidades ----
// Cadena del juego (12 bytes): bit 31 de la primera palabra = en el heap (+4 longitud, +8 puntero); si no, byte 0 = longitud y el
// texto va en linea a partir de +1.
static std::string game_str(u32 a) {
    if (!a) return "";
    u32 w = LD32(a), n, p;
    if (w & 0x80000000u) { n = LD32(a + 4); p = LD32(a + 8); } else { n = LD8(a) & 0x7F; p = a + 1; if (n > 11) n = 11; }
    if (n > 200) return "";
    std::string s; for (u32 i = 0; i < n; i++) s += (char)LD8(p + i); return s;
}
static std::vector<std::string> game_vec(u32 vec) {         // vector de cadenas del juego {datos, tamano, capacidad}
    std::vector<std::string> v; if (!vec) return v;
    u32 data = LD32(vec), n = LD32(vec + 4); if (!data || n > 1000) return v;
    for (u32 i = 0; i < n; i++) v.push_back(game_str(data + i * 12));
    return v;
}
static bool in_stage(u32& g, u32& field, u32& player) {
    g = LD32(GAME_PTR); field = LD32(FIELD_PTR); player = 0;
    if (!g || LD32(g) != GAME_VTABLE || !field || LD32(field) != FIELD_VTABLE) return false;
    u32 st = LD32(g + 0x28); if (st < 1 || st > 6) return false;
    player = LD32(field + 0x220);
    return player && player == s_live_player && s_live_ticks >= 2 && LD32(player) == PLAYER_VTABLE;
}
static std::string saves_dir() {
    std::string n = g_nand_root; while (n.size() > 1 && (n.back() == '/' || n.back() == '\\')) n.pop_back();
    size_t s = n.find_last_of("/\\");
    std::string d = (s == std::string::npos ? std::string(".") : n.substr(0, s)) + "/saves";
    mkdir(d.c_str(), 0755);
    return d;
}
static std::string slot_path(int slot, const char* ext) { return saves_dir() + "/slot" + std::to_string(slot + 1) + ext; }

// ---- punto restaurable: el mapa actual con la entrada por la que se llego (lista start_map del juego) ----
static std::string side_of(const std::string& e) {           // link_l / link_ld / link_r1 ... -> 'l' 'r' 'u' 'd'
    if (e.compare(0, 5, "link_") == 0 && e.size() > 5) return std::string(1, e[5]);
    return "";
}
static void record_point(u32 g, u32 field, u32 player) {
    std::string map = game_str(field + 0x168), entry = game_str(field + 0x174);
    float px = LDF(player + 0x78), py = LDF(player + 0x7c);
    std::vector<std::string> cands;
    for (const std::string& s : game_vec(s_flow_obj ? s_flow_obj + 0xd4 : 0)) {
        size_t c = s.find(':'); if (c != std::string::npos && s.compare(0, c, map) == 0 && c == map.size()) cands.push_back(s.substr(c + 1));
    }
    std::string pick;
    for (const std::string& c : cands) if (!entry.empty() && c == entry) pick = c;
    if (pick.empty() && !cands.empty()) {
        // Entrada por la posicion de llegada: borde izquierdo -> link_l, arriba -> link_u; si no, derecha / abajo; si no, la primera
        auto find_side = [&](char s) { for (const std::string& c : cands) if (side_of(c) == std::string(1, s)) return c; return std::string(); };
        if (px < 120) pick = find_side('l');
        if (pick.empty() && py < 100) pick = find_side('u');
        if (pick.empty() && px >= 120) pick = find_side('r');
        if (pick.empty()) pick = find_side('d');
        if (pick.empty()) pick = cands[0];
    }
    if (pick.empty()) { rt_log("[game] mapa %s sin entrada conocida: se mantiene el punto %s\n", map.c_str(), s_point.c_str()); return; }
    s_point = map + ":" + pick; s_point_stage = (int)LD32(g + 0x28);
    rt_log("[game] punto restaurable: fase %d %s (llegada x=%.0f y=%.0f)\n", s_point_stage, s_point.c_str(), px, py);
}

// ---- cada retrazado ----
void game_state_tick() {
    if (s_autostart && ++s_autostart_frames > 60 * 120) {     // seguro: si en 2 minutos no empezo la partida, se devuelve el control
        rt_log("[game] el arranque automatico no llego a la partida; se cancela\n");
        s_autostart = false; gx_fast_forward(false); g_audio_quiet = false;
    }
    if (s_live_player) s_live_ticks++;
    u32 g, field, player;
    if (!in_stage(g, field, player)) { if (g && field) { std::string m = game_str(field + 0x168); if (m != s_cur_map) { s_cur_map = m; s_arrival = true; } } return; }
    std::string map = game_str(field + 0x168);
    if (map != s_cur_map) { s_cur_map = map; s_arrival = true; }
    if (s_arrival) { s_arrival = false; record_point(g, field, player); }
    if (s_apply_player) {
        s_apply_player = false;
        ST32(player + 0x208, s_load.hearts); STF(player + 0x204, s_load.hp); ST32(player + 0x20c, s_load.whip);
        ST32(g + 0x2c, s_load.time);
        rt_log("[game] ranura aplicada: vidas %u puntuacion %u tiempo %u vida %.0f corazones %u subarma %u latigo %u\n",
               s_load.lives, s_load.score, s_load.time, s_load.hp, s_load.hearts, s_load.subweapon, s_load.whip);
    }
}

// ---- ganchos ----
void game_hook_start(u32 flow_obj) {
    s_flow_obj = flow_obj;
    if (g_pending_start.stage) {
        ST32(flow_obj + 0xc4, (u32)g_pending_start.stage);
        rt_log("[game] empezar en la fase %d\n", g_pending_start.stage);
        g_pending_start.stage = 0;
    }
}
void game_hook_player(u32 player) { s_live_player = player; s_live_ticks = 0; }
void game_hook_field() {
    s_live_player = 0;                         // el jugador anterior deja de valer hasta que el juego inicialice el nuevo
    if (g_pending_start.map[0]) {
        size_t n = strlen(g_pending_start.map);
        for (size_t i = 0; i <= n; i++) ST8(RT_SCRATCH + (u32)i, (u8)g_pending_start.map[i]);
        rt_call_indirect(0x800AD0F0, LD32(GAME_SETTINGS_PTR), 0x80244F08 /* "start_map" */, RT_SCRATCH, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        rt_log("[game] mapa inicial %s\n", g_pending_start.map);
        g_pending_start.map[0] = 0;
    }
    if (s_load_active) {                       // lo que el jugador copia de la partida al crearse (vidas, puntuacion, subarma, latigo)
        s_load_active = false;
        u32 g = LD32(GAME_PTR);
        if (g && LD32(g) == GAME_VTABLE) {
            ST32(g + 0x1c, s_load.lives); ST32(g + 0x20, s_load.score); ST32(g + 0x1fc, s_load.subweapon); ST32(g + 0x200, s_load.whip);
            s_apply_player = true;             // vida, corazones, tiempo: en cuanto exista el jugador (game_state_tick)
        }
    }
    if (s_autostart) { s_autostart = false; gx_fast_forward(false); g_audio_quiet = false; rt_log("[game] arranque automatico terminado\n"); }
}
bool game_autostart_active() { return s_autostart; }

// ---- ranuras ----
bool game_can_save() { u32 g, f, p; return in_stage(g, f, p) && !s_point.empty(); }

bool game_save_slot(int slot, const std::vector<u8>& thumb) {
    u32 g, field, player;
    if (!in_stage(g, field, player) || s_point.empty()) return false;
    SlotInfo s; s.used = true; s.stage = s_point_stage; s.map = s_point;
    s.lives = LD32(g + 0x1c); s.score = LD32(g + 0x20); s.time = LD32(g + 0x2c);
    s.hp = LDF(player + 0x204); s.hearts = LD32(player + 0x208); s.subweapon = LD32(player + 0x234); s.whip = LD32(player + 0x20c);
    char date[32]; time_t t = time(nullptr); strftime(date, sizeof date, "%Y-%m-%d %H:%M", localtime(&t));
    FILE* f = fopen(slot_path(slot, ".sav").c_str(), "w"); if (!f) return false;
    fprintf(f, "version=1\nstage=%d\nmap=%s\nlives=%u\nscore=%u\ntime=%u\nhp=%g\nhearts=%u\nsubweapon=%u\nwhip=%u\ndate=%s\n",
            s.stage, s.map.c_str(), s.lives, s.score, s.time, s.hp, s.hearts, s.subweapon, s.whip, date);
    fclose(f);
    if (thumb.size() == (size_t)THUMB_W * THUMB_H * 3) {
        if (FILE* p = fopen(slot_path(slot, ".ppm").c_str(), "wb")) { fprintf(p, "P6\n%d %d\n255\n", THUMB_W, THUMB_H); fwrite(thumb.data(), 1, thumb.size(), p); fclose(p); }
    }
    rt_log("[game] guardado en la ranura %d: fase %d %s\n", slot + 1, s.stage, s.map.c_str());
    return true;
}

bool game_slot_read(int slot, SlotInfo& s, std::vector<u8>* thumb) {
    s = SlotInfo();
    FILE* f = fopen(slot_path(slot, ".sav").c_str(), "r"); if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '='); if (!eq) continue; *eq = 0; std::string k = line, v = eq + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        if (k == "stage") s.stage = atoi(v.c_str()); else if (k == "map") s.map = v; else if (k == "date") s.date = v;
        else if (k == "lives") s.lives = (u32)strtoul(v.c_str(), nullptr, 10); else if (k == "score") s.score = (u32)strtoul(v.c_str(), nullptr, 10);
        else if (k == "time") s.time = (u32)strtoul(v.c_str(), nullptr, 10); else if (k == "hp") s.hp = (float)atof(v.c_str());
        else if (k == "hearts") s.hearts = (u32)strtoul(v.c_str(), nullptr, 10); else if (k == "subweapon") s.subweapon = (u32)strtoul(v.c_str(), nullptr, 10);
        else if (k == "whip") s.whip = (u32)strtoul(v.c_str(), nullptr, 10);
    }
    fclose(f);
    s.used = s.stage >= 1 && s.stage <= 6 && s.map.find(':') != std::string::npos && s.map.size() < sizeof(PendingStart::map);
    if (thumb) {
        thumb->clear();
        if (FILE* p = fopen(slot_path(slot, ".ppm").c_str(), "rb")) {
            int w = 0, h = 0, mx = 0;
            if (fscanf(p, "P6 %d %d %d", &w, &h, &mx) == 3 && w == THUMB_W && h == THUMB_H) {
                fgetc(p); thumb->resize((size_t)w * h * 3);
                if (fread(thumb->data(), 1, thumb->size(), p) != thumb->size()) thumb->clear();
            }
            fclose(p);
        }
    }
    return s.used;
}

static void set_env(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v) setenv(k, v, 1); else unsetenv(k);
#endif
}
void game_load_slot(int slot) {
    char b[8]; snprintf(b, sizeof b, "%d", slot + 1);
    set_env("RT_LOAD_SLOT", b); set_env("RT_TEST_HOME", nullptr);       // (la prueba automatica del menu no se repite al relanzar)
    rt_restart();
}
void game_start_stage(int stage) {
    char b[8]; snprintf(b, sizeof b, "%d", stage);
    set_env("RT_START_STAGE", b); set_env("RT_TEST_HOME", nullptr);
    rt_restart();
}

// Arranque tras relanzar: ranura o fase pendientes -> pulsar "confirmar" a velocidad sin limite (sin sonido) hasta la partida
void game_state_startup() {
    bool go = false;
    if (const char* e = getenv("RT_LOAD_SLOT")) {
        int slot = atoi(e) - 1;
        if (slot >= 0 && slot < SAVE_SLOTS && game_slot_read(slot, s_load, nullptr)) {
            g_pending_start.stage = s_load.stage;
            snprintf(g_pending_start.map, sizeof g_pending_start.map, "%s", s_load.map.c_str());
            s_load_active = true; go = true;
            rt_log("[game] cargando la ranura %d: fase %d %s\n", slot + 1, s_load.stage, s_load.map.c_str());
        }
        set_env("RT_LOAD_SLOT", nullptr);
    }
    if (const char* e = getenv("RT_START_STAGE")) {
        int st = atoi(e);
        if (st >= 1 && st <= 6) { g_pending_start.stage = st; go = true; rt_log("[game] selector de fases: fase %d\n", st); }
        set_env("RT_START_STAGE", nullptr);
    }
    // Pruebas: RT_TEST_STAGE=n empieza en la fase n; RT_TEST_MAP=mapa:entrada en ese mapa (sin arranque automatico)
    if (const char* t = getenv("RT_TEST_STAGE")) g_pending_start.stage = atoi(t);
    if (const char* t = getenv("RT_TEST_MAP")) snprintf(g_pending_start.map, sizeof g_pending_start.map, "%s", t);
    if (go) { s_autostart = true; gx_fast_forward(true); g_audio_quiet = true; }
}
