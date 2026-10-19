// Renderizador de GX con OpenGL 3.3 (SDL2): el EFB es un framebuffer de 640x480 y los dibujos del juego
// (quads 2D con texturas y un TEV simple) se transforman en CPU a coordenadas de pantalla.
#include "gx.h"
#include "hle.h"
#include "pad.h"
#include "ui_font.h"
#include "tex_decode.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

static bool g_video = false;
static int g_frame_no = 0;
static bool frame_log() { static long want = -2; if (want == -2) { const char* e = getenv("RT_GXFRAME"); want = e ? atol(e) : -1; } return want >= 0 && g_frame_no == want; }
static SDL_Window* g_win = nullptr;
static SDL_GLContext g_ctx = nullptr;
static GLuint g_fbo = 0, g_efb_tex = 0, g_depth_rb = 0, g_xfb_fbo = 0, g_xfb_tex = 0;
static u64 g_draws = 0, g_verts = 0, g_copies = 0;
static bool g_show_fps = getenv("RT_SHOW_FPS") != nullptr, g_xfb_dirty = false;          // contador de FPS (F3) y 'hay un fotograma nuevo en el XFB'
bool gx_xfb_pending() { return g_xfb_dirty; }
u64 gx_draw_count() { return g_draws; }
double g_t_wait_present = 0, g_max_interval_ms = 0; unsigned g_slow_frames = 0;       // espera del juego al hilo de render, intervalo maximo entre presentaciones, fotogramas > 20 ms
double g_t_draw = 0, g_t_copy = 0, g_t_present = 0, g_t_sec_a = 0, g_t_sec_b = 0, g_t_sec_c = 0;           // segundos acumulados (RT_FPS)
static inline double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static const bool g_diag = getenv("RT_FPS") != nullptr;      // las mediciones de tiempo (RT_FPS) solo se hacen si se piden: leer el reloj en cada dibujo cuesta
struct TimeAcc { double& acc; double t0; TimeAcc(double& a) : acc(a), t0(g_diag ? now_s() : 0) {} ~TimeAcc() { if (g_diag) acc += now_s() - t0; } };
u64 gx_copy_count() { return g_copies; }
static std::map<std::tuple<u32, u32, u32, u32>, u64> g_draw_kinds;

static const int LW = 640, LH = 480;                         // resolucion logica del EFB (la del juego)
static int g_sc = 1;                                          // factor de resolucion interna (menu F3)
#define EFB_W (LW * g_sc)
#define EFB_H (LH * g_sc)
struct Settings { int show_fps = 0, vsync = 0, unlimited = 0, screen_filter = 1, tex_filter = 0, scale = 1, fullscreen = 0, pause_bg = 1; };   // menu F3
static Settings g_set;
struct PresentInfo { Settings set; bool show_fps; bool menu_open; int menu_sel; char fps_text[32]; int frame; };   // lo que el hilo de render necesita para presentar
static bool g_menu_open = getenv("RT_MENU") != nullptr, g_filter_dirty = true;
static int g_filter_applied = -1;
static void settings_load();
static void settings_apply(bool initial);

// ---------------------------------------------------------------------------------------------------
// Hilo de render: toda la API de OpenGL corre en un hilo aparte. El hilo del juego solo prepara datos (vertices, estado, texturas
// decodificadas) y los encola como tareas; el hilo de render las ejecuta en orden. Solo tareas RT_HEADLESS=1 / pre-arranque se ejecutan al instante.
// ---------------------------------------------------------------------------------------------------
static std::mutex g_q_mtx;
static std::condition_variable g_q_work, g_q_room;
static std::deque<std::function<void()>> g_q;
static std::atomic<int> g_pending_presents{0};
static bool g_render_thread_on = false, g_gl_ready = false;
double g_t_render = 0;                                           // segundos de trabajo del hilo de render (RT_FPS)
static void rq_push(std::function<void()> f) {
    if (!g_render_thread_on) { f(); return; }
    { std::unique_lock<std::mutex> lk(g_q_mtx); g_q_room.wait(lk, [] { return g_q.size() < 4096; }); g_q.push_back(std::move(f)); }
    g_q_work.notify_one();
}
static void rq_wait_presents(int max_pending) {
    std::unique_lock<std::mutex> lk(g_q_mtx);
    g_q_room.wait(lk, [&] { return g_pending_presents.load() <= max_pending; });
}
static void render_thread_main();                                // (definida mas abajo)

// ---------------------------------------------------------------------------------------------------
// Texturas (lado del juego: decodifica y reparte manejadores; el hilo de render las sube a OpenGL)
// ---------------------------------------------------------------------------------------------------
struct TexRef { u8 kind = 0; u32 key = 0; u16 w = 0, h = 0; };    // 0 = nada, 1 = textura de la cache (key = manejador), 2 = copia de EFB (key = direccion)
struct HostTex { u32 handle; u64 hash; int w, h; u32 frame; u64 tlh; };
extern u32 g_vi_frames;
static std::unordered_map<u64, HostTex> g_texcache;
struct CopyInfo { int w = 0, h = 0; };
static std::unordered_map<u32, CopyInfo> g_copyinfo;             // copias EFB->textura ya hechas, por direccion de destino
static u32 g_next_handle = 1;
static void render_upload_tex(u32 handle, int w, int h, const std::vector<u8>& rgba);

static u32 tex_bytes(u32 fmt, int w, int h) {
    switch (fmt) {
    case 0: case 8: return ((w + 7) / 8) * ((h + 7) / 8) * 32;
    case 1: case 2: case 9: return ((w + 7) / 8) * ((h + 3) / 4) * 32;
    case 3: case 4: case 5: case 10: return ((w + 3) / 4) * ((h + 3) / 4) * 32;
    case 6: return ((w + 3) / 4) * ((h + 3) / 4) * 64;
    case 14: return ((w + 7) / 8) * ((h + 7) / 8) * 32;
    }
    return 0;
}
static u64 hash_bytes(const u8* p, size_t n) { u64 h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }

static void flush_batch();                                   // encola los vertices acumulados (gx_draw)
static void batch_invalidate() {}
static TexRef get_texture(int map) {
    static const int imgBase[2] = { 0x88, 0xA8 };
    int grp = map >> 2, k = map & 3;
    u32 img0 = g_gx.bp[imgBase[grp] + k], img3 = g_gx.bp[imgBase[grp] + 12 + k];
    int w = (img0 & 0x3FF) + 1, h = ((img0 >> 10) & 0x3FF) + 1; u32 fmt = (img0 >> 20) & 15;
    u32 addr = (img3 & 0xFFFFFF) << 5; u32 tl = g_gx.bp[(grp ? 0xB8 : 0x98) + k];
    u32 tlutKey = tl & 0x3FF, tlutFmt = (tl >> 10) & 3;
    TexRef r; r.w = (u16)w; r.h = (u16)h;
    { auto ci = g_copyinfo.find(addr);                          // textura que el juego rendeo desde el EFB (se resuelve en el hilo de render)
      if (ci != g_copyinfo.end() && ci->second.w && ((ci->second.w == w && ci->second.h == h) || (w >= ci->second.w && h >= ci->second.h))) { r.kind = 2; r.key = addr; return r; } }
    u32 nb = tex_bytes(fmt, w, h);
    if (!nb || !addr) return TexRef();
    u64 key = ((u64)addr << 20) ^ ((u64)fmt << 8) ^ (u64)w * 7919 ^ (u64)h * 104729 ^ ((u64)tlutKey << 40);
    auto it = g_texcache.find(key);
    // La paleta (pequena) se hashea siempre; el contenido de la textura, una sola vez por fotograma de VI y textura
    // (hashear MB enteros en cada dibujo hundia el juego a ~1 fps).
    u64 tlh = 0;
    if (fmt >= 8 && fmt <= 10) { auto tt = g_tlut.find(tlutKey); if (tt != g_tlut.end()) tlh = hash_bytes((const u8*)tt->second.data(), tt->second.size() * 2); }
    r.kind = 1;
    if (it != g_texcache.end() && it->second.frame == g_vi_frames && it->second.tlh == tlh) { r.key = it->second.handle; return r; }
    u64 hsh = hash_bytes(HP(addr | 0x80000000u), nb) ^ tlh;
    if (it != g_texcache.end() && it->second.hash == hsh) { it->second.frame = g_vi_frames; it->second.tlh = tlh; r.key = it->second.handle; return r; }
    flush_batch();                                               // lo ya dibujado debe verse con el contenido anterior
    std::vector<u8> rgba;
    if (!decode_texture(addr | 0x80000000u, fmt, w, h, tlutKey, tlutFmt, rgba)) { static int warned = 0; if (warned++ < 5) fprintf(stderr, "[gx] formato de textura %u no soportado\n", fmt); return TexRef(); }
    if (getenv("RT_DUMP_TEX")) { static int n = 0; if (n < 12) { char fn[128]; snprintf(fn, sizeof fn, "/tmp/claude-1000/tex_%d_f%u_%dx%d.ppm", n++, fmt, w, h); FILE* f = fopen(fn, "wb");
        fprintf(f, "P6 %d %d 255\n", w, h); for (int i = 0; i < w * h; i++) fwrite(&rgba[i * 4], 1, 3, f); fclose(f); } }
    u32 handle = it != g_texcache.end() ? it->second.handle : g_next_handle++;
    auto px = std::make_shared<std::vector<u8>>(std::move(rgba));
    rq_push([handle, w, h, px] { render_upload_tex(handle, w, h, *px); });
    g_texcache[key] = { handle, hsh, w, h, g_vi_frames, tlh };
    r.key = handle;
    return r;
}

// ---------------------------------------------------------------------------------------------------
// Shaders (TEV generico)
// ---------------------------------------------------------------------------------------------------
struct Prog { GLuint id; GLint uTex[8]; GLint uK[4]; GLint uC[4]; GLint uKonst; };
static std::unordered_map<std::string, Prog> g_progs;

static const char* VS = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol0; layout(location=2) in vec4 aCol1;
layout(location=3) in vec2 aTex0; layout(location=4) in vec2 aTex1; layout(location=5) in vec2 aTex2; layout(location=6) in vec2 aTex3;
out vec4 vCol0; out vec4 vCol1; out vec2 vTex[4];
void main() { gl_Position = vec4(aPos.x / 320.0 - 1.0, 1.0 - aPos.y / 240.0, aPos.z * 2.0 - 1.0, 1.0);
  vCol0 = aCol0; vCol1 = aCol1; vTex[0] = aTex0; vTex[1] = aTex1; vTex[2] = aTex2; vTex[3] = aTex3; })";

static std::string tev_in_c(int sel, int stage, u32 kc) {
    (void)stage;
    switch (sel) {
    case 0: return "prev.rgb"; case 1: return "vec3(prev.a)"; case 2: return "c0.rgb"; case 3: return "vec3(c0.a)";
    case 4: return "c1.rgb"; case 5: return "vec3(c1.a)"; case 6: return "c2.rgb"; case 7: return "vec3(c2.a)";
    case 8: return "texc.rgb"; case 9: return "vec3(texc.a)"; case 10: return "rasc.rgb"; case 11: return "vec3(rasc.a)";
    case 12: return "vec3(1.0)"; case 13: return "vec3(0.5)"; case 14: {
        static const char* kv[32] = { "vec3(1.0)", "vec3(0.875)", "vec3(0.75)", "vec3(0.625)", "vec3(0.5)", "vec3(0.375)", "vec3(0.25)", "vec3(0.125)",
            "vec3(1.0)", "vec3(1.0)", "vec3(1.0)", "vec3(1.0)", "k0.rgb", "k1.rgb", "k2.rgb", "k3.rgb",
            "vec3(k0.r)", "vec3(k1.r)", "vec3(k2.r)", "vec3(k3.r)", "vec3(k0.g)", "vec3(k1.g)", "vec3(k2.g)", "vec3(k3.g)",
            "vec3(k0.b)", "vec3(k1.b)", "vec3(k2.b)", "vec3(k3.b)", "vec3(k0.a)", "vec3(k1.a)", "vec3(k2.a)", "vec3(k3.a)" };
        return kv[kc & 31]; }
    default: return "vec3(0.0)";
    }
}
static std::string tev_in_a(int sel, u32 ka) {
    switch (sel) {
    case 0: return "prev.a"; case 1: return "c0.a"; case 2: return "c1.a"; case 3: return "c2.a"; case 4: return "texc.a"; case 5: return "rasc.a";
    case 6: {
        static const char* kv[32] = { "1.0", "0.875", "0.75", "0.625", "0.5", "0.375", "0.25", "0.125", "1.0", "1.0", "1.0", "1.0", "1.0", "1.0", "1.0", "1.0",
            "k0.r", "k1.r", "k2.r", "k3.r", "k0.g", "k1.g", "k2.g", "k3.g", "k0.b", "k1.b", "k2.b", "k3.b", "k0.a", "k1.a", "k2.a", "k3.a" };
        return kv[ka & 31]; }
    default: return "0.0";
    }
}
static const char* dest_name(int d) { static const char* n[4] = { "prev", "c0", "c1", "c2" }; return n[d & 3]; }

static std::string build_key_and_source(const u32* bp, int& nstages, std::string& src) {
    u32 genmode = bp[0x00];
    nstages = ((genmode >> 10) & 15) + 1;
    u32 acomp = bp[0xF3];
    char b[256]; std::string key;
    snprintf(b, sizeof b, "n%d,a%06x;", nstages, acomp); key += b;
    src = "#version 330 core\nin vec4 vCol0; in vec4 vCol1; in vec2 vTex[4]; out vec4 oCol;\nuniform sampler2D uTex[8]; uniform vec4 uK[4]; uniform vec4 uC[4];\n"
          "void main() { vec4 prev = uC[0]; vec4 c0 = uC[1]; vec4 c1 = uC[2]; vec4 c2 = uC[3]; vec4 texc = vec4(1.0); vec4 rasc = vCol0;\n"
          "  vec4 k0 = uK[0]; vec4 k1 = uK[1]; vec4 k2 = uK[2]; vec4 k3 = uK[3]; vec3 t3; float t1;\n";
    for (int s = 0; s < nstages; s++) {
        u32 order = bp[0x28 + (s >> 1)];
        u32 o = (s & 1) ? (order >> 12) : order;
        u32 texmap = o & 7, texcoord = (o >> 3) & 7, texen = (o >> 6) & 1, chan = (o >> 7) & 7;
        u32 cc = bp[0xC0 + 2 * s], ac = bp[0xC1 + 2 * s];
        u32 ks = bp[0xF6 + (s >> 1)];
        u32 kcsel = (s & 1) ? ((ks >> 14) & 31) : ((ks >> 4) & 31), kasel = (s & 1) ? ((ks >> 19) & 31) : ((ks >> 9) & 31);
        snprintf(b, sizeof b, "s%d:%x,%x,%x,%x,%x;", s, order & 0xFFFFFF, cc, ac, kcsel, kasel); key += b;
        std::string t;
        if (texen) { snprintf(b, sizeof b, "  texc = texture(uTex[%u], vTex[%u & 3]);\n", texmap, texcoord); t += b; } else t += "  texc = vec4(1.0);\n";
        t += chan == 4 || chan == 5 ? "  rasc = vCol0;\n" : chan == 7 ? "  rasc = vec4(0.0);\n" : "  rasc = vCol0;\n";
        int ca = (cc >> 12) & 15, cb = (cc >> 8) & 15, ccc = (cc >> 4) & 15, cd = cc & 15;
        int bias = (cc >> 16) & 3, sub = (cc >> 18) & 1, clampc = (cc >> 19) & 1, shift = (cc >> 20) & 3, dst = (cc >> 22) & 3;
        static const char* biasv[4] = { "0.0", "0.5", "-0.5", "0.0" }; static const float scl[4] = { 1, 2, 4, 0.5f };
        snprintf(b, sizeof b, "  t3 = (%s %s (%s * (1.0 - %s) + %s * %s) + vec3(%s)) * %f;\n", tev_in_c(cd, s, kcsel).c_str(), sub ? "-" : "+", tev_in_c(ca, s, kcsel).c_str(), tev_in_c(ccc, s, kcsel).c_str(), tev_in_c(cb, s, kcsel).c_str(), tev_in_c(ccc, s, kcsel).c_str(), biasv[bias], scl[shift]);
        t += b; if (sub) t += ""; if (clampc) t += "  t3 = clamp(t3, 0.0, 1.0);\n";
        int aa = (ac >> 13) & 7, ab = (ac >> 10) & 7, acc = (ac >> 7) & 7, ad = (ac >> 4) & 7;
        int abias = (ac >> 16) & 3, asub = (ac >> 18) & 1, aclamp = (ac >> 19) & 1, ashift = (ac >> 20) & 3, adst = (ac >> 22) & 3;
        snprintf(b, sizeof b, "  t1 = (%s %s (%s * (1.0 - %s) + %s * %s) + %s) * %f;\n", tev_in_a(ad, kasel).c_str(), asub ? "-" : "+", tev_in_a(aa, kasel).c_str(), tev_in_a(acc, kasel).c_str(), tev_in_a(ab, kasel).c_str(), tev_in_a(acc, kasel).c_str(), biasv[abias], scl[ashift]);
        t += b; if (aclamp) t += "  t1 = clamp(t1, 0.0, 1.0);\n";
        snprintf(b, sizeof b, "  %s.rgb = t3;\n  %s.a = t1;\n", dest_name(dst), dest_name(adst)); t += b;
        src += t;
    }
    // prueba alfa
    int c0 = (acomp >> 16) & 7, c1 = (acomp >> 19) & 7, lg = (acomp >> 22) & 3; float r0 = (acomp & 255) / 255.f, r1 = ((acomp >> 8) & 255) / 255.f;
    auto cmp = [&](int c, float r) { static const char* ops[8] = { "false", "<", "==", "<=", ">", "!=", ">=", "true" }; char q[128];
        if (c == 0) return std::string("false"); if (c == 7) return std::string("true");
        snprintf(q, sizeof q, "(floor(prev.a * 255.0 + 0.5) %s %.1f)", ops[c], floor(r * 255.0 + 0.5)); return std::string(q); };
    static const char* lgs[4] = { "&&", "||", "!=", "==" };
    src += "  bool pass = " + cmp(c0, r0) + " " + lgs[lg] + " " + cmp(c1, r1) + ";\n  if (!pass) discard;\n  oCol = prev; }\n";
    return key;
}

// Clave numerica barata de la configuracion del TEV (los mismos registros que usa el generador de codigo): evita construir el
// texto GLSL completo en cada dibujo; el codigo solo se genera cuando la configuracion es nueva.
static std::unordered_map<u64, Prog*> g_prog_fast;
static u64 prog_fast_key(const u32* bp, int& nstages) {
    u32 genmode = bp[0x00]; nstages = ((genmode >> 10) & 15) + 1;
    u64 h = 1469598103934665603ull;
    auto mix = [&](u32 v) { h ^= v; h *= 1099511628211ull; h ^= h >> 29; };
    mix(nstages); mix(bp[0xF3]);
    for (int st = 0; st < nstages; st++) {
        u32 order = bp[0x28 + (st >> 1)]; u32 o = (st & 1) ? (order >> 12) : order;
        u32 ks = bp[0xF6 + (st >> 1)];
        u32 kcsel = (st & 1) ? ((ks >> 14) & 31) : ((ks >> 4) & 31), kasel = (st & 1) ? ((ks >> 19) & 31) : ((ks >> 9) & 31);
        mix(o & 0xFFF); mix(bp[0xC0 + 2 * st]); mix(bp[0xC1 + 2 * st]); mix(kcsel | (kasel << 8));
    }
    return h;
}

static Prog& get_prog(const u32* bp, int& nstages) {
    u64 fk = prog_fast_key(bp, nstages);
    { auto f = g_prog_fast.find(fk); if (f != g_prog_fast.end()) return *f->second; }
    std::string src, key = build_key_and_source(bp, nstages, src);
    auto it = g_progs.find(key);
    if (it != g_progs.end()) { g_prog_fast[fk] = &it->second; return it->second; }
    auto compile = [](GLenum t, const char* s) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok); if (!ok) { char log[2048]; glGetShaderInfoLog(sh, 2048, nullptr, log); fprintf(stderr, "[gx] error de shader: %s\n%s\n", log, s); } return sh; };
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, src.c_str());
    Prog p; p.id = glCreateProgram(); glAttachShader(p.id, vs); glAttachShader(p.id, fs); glLinkProgram(p.id);
    for (int i = 0; i < 8; i++) { char n[16]; snprintf(n, sizeof n, "uTex[%d]", i); p.uTex[i] = glGetUniformLocation(p.id, n); }
    p.uKonst = glGetUniformLocation(p.id, "uK"); p.uK[0] = glGetUniformLocation(p.id, "uC");
    Prog& ref = g_progs[key]; ref = p; g_prog_fast[fk] = &ref; return ref;
}

// ---------------------------------------------------------------------------------------------------
// Estado de dibujo y primitivas
// ---------------------------------------------------------------------------------------------------
static GLuint g_vao = 0, g_vbo = 0;
static const int FLOATS = 3 + 4 + 4 + 8;       // pos, col0, col1, tex0..tex3

static void color_from_bp(u32 ra, u32 bg, float out[4]) {      // registros de color del TEV: R/A en 0xE0+2i, B/G en 0xE1+2i
    out[0] = (ra & 0x7FF) / 255.f; out[3] = ((ra >> 12) & 0x7FF) / 255.f; out[2] = (bg & 0x7FF) / 255.f; out[1] = ((bg >> 12) & 0x7FF) / 255.f;
}

static void apply_blend(const u32* bp) {
    u32 z = bp[0x40], bm = bp[0x41];
    if (z & 1) { glEnable(GL_DEPTH_TEST); static const GLenum f[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS }; glDepthFunc(f[(z >> 1) & 7]); glDepthMask((z >> 4) & 1 ? GL_TRUE : GL_FALSE); }
    else glDisable(GL_DEPTH_TEST);
    static const GLenum sf[8] = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
    static const GLenum df[8] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
    if (bm & 1) { glEnable(GL_BLEND); glBlendFunc(sf[(bm >> 8) & 7], df[(bm >> 5) & 7]); glBlendEquation((bm >> 11) & 1 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD); }
    else glDisable(GL_BLEND);
    glColorMask((bm >> 3) & 1, (bm >> 3) & 1, (bm >> 3) & 1, (bm >> 4) & 1);
    // recorte
    u32 tl = bp[0x20], br = bp[0x21];
    int x0 = (int)((tl >> 12) & 0x7FF) - 342, y0 = (int)(tl & 0x7FF) - 342, x1 = (int)((br >> 12) & 0x7FF) - 342, y1 = (int)(br & 0x7FF) - 342;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0 * g_sc, (LH - 1 - y1) * g_sc, (x1 - x0 + 1) * g_sc, (y1 - y0 + 1) * g_sc);
}

static void transform(const GxVertex& v, float out[3]) {
    // matriz de posicion (3x4) del XF y proyeccion: x' = A*x + B (ortografica) o con division por -z (perspectiva)
    int idx = (g_gx.vcdLo & 1) ? v.posMtxIdx : (g_gx.xfReg[0x18] & 63);
    const float* m = &g_gx.xfMem[idx * 4];
    float x = m[0] * v.pos[0] + m[1] * v.pos[1] + m[2] * v.pos[2] + m[3];
    float y = m[4] * v.pos[0] + m[5] * v.pos[1] + m[6] * v.pos[2] + m[7];
    float z = m[8] * v.pos[0] + m[9] * v.pos[1] + m[10] * v.pos[2] + m[11];
    float P[6]; for (int i = 0; i < 6; i++) { u32 w = g_gx.xfReg[0x20 + i]; memcpy(&P[i], &w, 4); }
    bool ortho = g_gx.xfReg[0x26] != 0;
    float cx, cy, cz, cw;
    if (ortho) { cx = P[0] * x + P[1]; cy = P[2] * y + P[3]; cz = P[4] * z + P[5]; cw = 1; }
    else { cx = P[0] * x + P[1] * z; cy = P[2] * y + P[3] * z; cz = P[4] * z + P[5]; cw = -z; }
    float vp[6]; for (int i = 0; i < 6; i++) { u32 w = g_gx.xfReg[0x1A + i]; memcpy(&vp[i], &w, 4); }
    float ndcx = cx / cw, ndcy = cy / cw, ndcz = cz / cw;
    out[0] = ndcx * vp[0] + vp[3] - 342.0f;
    out[1] = ndcy * vp[1] + vp[4] - 342.0f;
    out[2] = vp[5] != 0 ? (ndcz * vp[2] + vp[5]) / 16777215.0f : 0.5f;
}

// Generacion de coordenadas de textura (XF texgen): devuelve las coordenadas de la unidad i.
static void texgen(const GxVertex& v, int i, float out[2]) {
    u32 tg = g_gx.xfReg[0x40 + i];
    bool proj = (tg >> 1) & 1; u32 form = (tg >> 2) & 3, type = (tg >> 4) & 7, src = (tg >> 7) & 31;
    float c[4] = { 0, 0, 1, 1 };
    if (src == 0) { c[0] = v.pos[0]; c[1] = v.pos[1]; c[2] = form ? v.pos[2] : 1.0f; }
    else if (src == 1) { c[0] = v.nrm[0]; c[1] = v.nrm[1]; c[2] = v.nrm[2]; }
    else if (src >= 5 && src < 13) { c[0] = v.tex[src - 5][0]; c[1] = v.tex[src - 5][1]; }
    if (type != 0) { out[0] = out[1] = 0; return; }                  // emboss/color: no usados
    int mi = (g_gx.vcdLo >> (1 + i)) & 1 ? v.texMtxIdx[i] : (i < 4 ? (g_gx.xfReg[0x18] >> (6 + 6 * i)) & 63 : (g_gx.xfReg[0x19] >> (6 * (i - 4))) & 63);
    const float* m = &g_gx.xfMem[mi * 4];
    float s = m[0] * c[0] + m[1] * c[1] + m[2] * c[2] + m[3] * c[3];
    float t = m[4] * c[0] + m[5] * c[1] + m[6] * c[2] + m[7] * c[3];
    if (proj) { float q = m[8] * c[0] + m[9] * c[1] + m[10] * c[2] + m[11] * c[3]; if (q != 0) { s /= q; t /= q; } }
    u32 dt = g_gx.xfReg[0x50 + i];                                   // matriz posterior (dual-tex)
    const float* d = &g_gx.xfMem[0x500 + (dt & 63) * 4];
    float s2 = d[0] * s + d[1] * t + d[2] + d[3], t2 = d[4] * s + d[5] * t + d[6] + d[7];
    out[0] = s2; out[1] = t2;
}

static bool gxlog_visible(const GxVertex* v, u32 count);
static bool gxlog_on() { static long lo = -1, hi = -1; if (lo < 0) { const char* e = getenv("RT_GXLOG"); lo = hi = 0; if (e) sscanf(e, "%ld,%ld", &lo, &hi); } return g_draws >= (u64)lo && g_draws < (u64)hi && hi > 0; }
static bool gxlog_visible(const GxVertex* v, u32 count) {
    if (!getenv("RT_GXVIS")) return true;
    float x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; for (u32 i = 0; i < count; i++) { float p[3]; transform(v[i], p); x0 = fminf(x0, p[0]); x1 = fmaxf(x1, p[0]); y0 = fminf(y0, p[1]); y1 = fmaxf(y1, p[1]); }
    return x1 > 0 && x0 < 640 && y1 > 0 && y0 < 480;
}
// Lote de dibujo: vertices con el mismo estado completo (programa TEV, texturas, mezcla, recorte, colores) van en un solo glDrawArrays.
struct Batch { u32 bp[256]; int nst; TexRef tex[8]; u32 used; u32 mode[8]; std::vector<float> verts; u32 n = 0; };
struct DrawSig { u64 pkey; TexRef tex[8]; u32 mode[8]; u32 z, bm, tl, br; float C[16], K[16]; };
static DrawSig g_last_state; static std::shared_ptr<Batch> g_cur;
static bool g_state_dirty = true;                             // algun registro BP que cambia el estado de dibujo se escribio desde el ultimo dibujo
static void exec_draw(Batch& b);
void gx_draw(u32 prim, u32 vat, const GxVertex* v, u32 count) {
    TimeAcc ta_(g_t_draw);
    g_draws++; g_verts += count;
    static const char* e_dm = getenv("RT_DUMPMEM"); static const char* e_bt = getenv("RT_BT_DRAW");
    if (const char* e = e_dm) if (g_draws == (u64)atol(e)) { u32 p = LD32(0x803166CC); fprintf(stderr, "[gx] *0x803166CC = %08x:", p); for (u32 i = 0; i < 0x40; i++) fprintf(stderr, "%s%02x", i % 16 ? " " : "\n  ", LD8(p + i)); fputc(10, stderr); }
    if (const char* e = e_bt) if (g_draws == (u64)atol(e)) { fprintf(stderr, "[gx] pila en el dibujo %llu:\n", (unsigned long long)g_draws); rt_backtrace(30); }
    static const bool kinds = getenv("RT_GXKINDS") != nullptr; if (kinds) g_draw_kinds[std::make_tuple(prim, g_gx.vcdLo, g_gx.vcdHi, g_gx.vatA[vat])]++;
    if (gxlog_on() && gxlog_visible(v, count)) {
        float vp[6], P[6]; for (int i = 0; i < 6; i++) { u32 w = g_gx.xfReg[0x1A + i]; memcpy(&vp[i], &w, 4); w = g_gx.xfReg[0x20 + i]; memcpy(&P[i], &w, 4); }
        fprintf(stderr, "[gxlog] draw %llu prim=%u n=%u vp=%g %g %g %g %g %g proj=%g %g %g %g %g %g type=%u scis=%08x %08x genmode=%08x\n", (unsigned long long)g_draws, prim, count, vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], P[0], P[1], P[2], P[3], P[4], P[5], g_gx.xfReg[0x26], g_gx.bp[0x20], g_gx.bp[0x21], g_gx.bp[0]);
        for (u32 i = 0; i < count && i < 4; i++) fprintf(stderr, "   v%u pos=%g %g %g col=%g %g %g %g tex=%g %g mtx=%d\n", i, v[i].pos[0], v[i].pos[1], v[i].pos[2], v[i].col[0][0], v[i].col[0][1], v[i].col[0][2], v[i].col[0][3], v[i].tex[0][0], v[i].tex[0][1], v[i].posMtxIdx);
        fprintf(stderr, "   vat=%u vcd=%08x/%08x A=%08x B=%08x C=%08x\n", vat, g_gx.vcdLo, g_gx.vcdHi, g_gx.vatA[vat], g_gx.vatB[vat], g_gx.vatC[vat]);
        { u32 o = g_gx.bp[0x28]; fprintf(stderr, "   n=%u order0=%x cc0=%x ac0=%x z=%x blend=%x acomp=%x img0=%x/%x addr=%x ks=%x rgba prev=%x %x\n", (g_gx.bp[0]>>10&15)+1, o, g_gx.bp[0xC0], g_gx.bp[0xC1], g_gx.bp[0x40], g_gx.bp[0x41], g_gx.bp[0xF3], g_gx.bp[0x88], g_gx.bp[0x94], g_gx.bp[0x94] & 0xFFFFFF, g_gx.bp[0xF6], g_gx.bp[0xE0], g_gx.bp[0xE1]);
          fprintf(stderr, "   texgen0=%x dual0=%x mtxidx=%x tm: %g %g %g %g / %g %g %g %g (idx %u)\n", g_gx.xfReg[0x40], g_gx.xfReg[0x50], g_gx.xfReg[0x18], 0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f, 0u); }
        fprintf(stderr, "   xf mtx0: %g %g %g %g / %g %g %g %g\n", g_gx.xfMem[0], g_gx.xfMem[1], g_gx.xfMem[2], g_gx.xfMem[3], g_gx.xfMem[4], g_gx.xfMem[5], g_gx.xfMem[6], g_gx.xfMem[7]);
    }
    if (frame_log()) { float x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; for (u32 i = 0; i < count; i++) { float p[3]; transform(v[i], p); x0 = fminf(x0, p[0]); x1 = fmaxf(x1, p[0]); y0 = fminf(y0, p[1]); y1 = fmaxf(y1, p[1]); }
        u32 o = g_gx.bp[0x28]; fprintf(stderr, "[frame] draw prim=%u n=%u bbox=(%.0f,%.0f)-(%.0f,%.0f) order0=%x cc0=%x ac0=%x blend=%x z=%x tex=%x/%x scis=%x/%x\n", prim, count, x0, y0, x1, y1, o, g_gx.bp[0xC0], g_gx.bp[0xC1], g_gx.bp[0x41], g_gx.bp[0x40], g_gx.bp[0x88 + (o & 3)], g_gx.bp[0x94 + (o & 3)], g_gx.bp[0x20], g_gx.bp[0x21]); }
    if (!g_video || count == 0) return;
    double tA = g_diag ? now_s() : 0;
    u32 n = prim == 0 ? (count / 4) * 6 : prim == 2 ? (count / 3) * 3 : (prim == 3 || prim == 4) ? (count >= 3 ? (count - 2) * 3 : 0) : 0;     // vertices de salida (triangulos); lineas y puntos no se usan
    if (!n) return;
    // Registros de vertice (posicion en pantalla, colores, texcoords) calculados una sola vez por vertice con constantes del dibujo
    static std::vector<float> rec; rec.resize((size_t)count * FLOATS);
    {
        const bool pidx = g_gx.vcdLo & 1;
        const float* mdef = &g_gx.xfMem[(g_gx.xfReg[0x18] & 63) * 4];
        float P[6], vp[6]; memcpy(P, &g_gx.xfReg[0x20], sizeof P); memcpy(vp, &g_gx.xfReg[0x1A], sizeof vp);
        const bool ortho = g_gx.xfReg[0x26] != 0;
        u32 mat = g_gx.xfReg[0x0C], cc = g_gx.xfReg[0x0E], ac = g_gx.xfReg[0x10];     // color de material 0 / control de canal
        const float mr = ((mat >> 24) & 255) / 255.f, mg = ((mat >> 16) & 255) / 255.f, mb = ((mat >> 8) & 255) / 255.f, ma = (mat & 255) / 255.f;
        const int ntg = g_gx.xfReg[0x09] & 15;
        for (u32 i = 0; i < count; i++) {
            const GxVertex& vi = v[i]; float* o = &rec[(size_t)i * FLOATS];
            const float* m = pidx ? &g_gx.xfMem[vi.posMtxIdx * 4] : mdef;
            float x = m[0] * vi.pos[0] + m[1] * vi.pos[1] + m[2] * vi.pos[2] + m[3];
            float y = m[4] * vi.pos[0] + m[5] * vi.pos[1] + m[6] * vi.pos[2] + m[7];
            float z = m[8] * vi.pos[0] + m[9] * vi.pos[1] + m[10] * vi.pos[2] + m[11];
            float cx, cy, cz, cw;
            if (ortho) { cx = P[0] * x + P[1]; cy = P[2] * y + P[3]; cz = P[4] * z + P[5]; cw = 1; }
            else { cx = P[0] * x + P[1] * z; cy = P[2] * y + P[3] * z; cz = P[4] * z + P[5]; cw = -z; }
            float ndcx = cx / cw, ndcy = cy / cw, ndcz = cz / cw;
            o[0] = ndcx * vp[0] + vp[3] - 342.0f;
            o[1] = ndcy * vp[1] + vp[4] - 342.0f;
            o[2] = vp[5] != 0 ? (ndcz * vp[2] + vp[5]) / 16777215.0f : 0.5f;
            o[3] = (cc & 1) ? vi.col[0][0] : mr; o[4] = (cc & 1) ? vi.col[0][1] : mg; o[5] = (cc & 1) ? vi.col[0][2] : mb; o[6] = (ac & 1) ? vi.col[0][3] : ma;
            memcpy(o + 7, vi.col[1], 16);
            for (int t = 0; t < 4; t++) { if (t < ntg) texgen(vi, t, o + 11 + 2 * t); else { o[11 + 2 * t] = 0; o[12 + 2 * t] = 0; } }
        }
    }
    // Ensambla los triangulos del lote copiando registros de vertice ya calculados
    auto assemble = [&](Batch& b) {
        size_t base = b.verts.size(); b.verts.resize(base + (size_t)n * FLOATS); float* d = &b.verts[base]; const float* r = rec.data();
        auto put = [&](u32 k) { memcpy(d, r + (size_t)k * FLOATS, FLOATS * sizeof(float)); d += FLOATS; };
        if (prim == 0) { for (u32 i = 0; i + 3 < count; i += 4) { put(i); put(i + 1); put(i + 2); put(i); put(i + 2); put(i + 3); } }
        else if (prim == 2) { for (u32 i = 0; i + 2 < count; i += 3) { put(i); put(i + 1); put(i + 2); } }
        else if (prim == 3) { for (u32 i = 0; i + 2 < count; i++) { if (i & 1) { put(i + 1); put(i); } else { put(i); put(i + 1); } put(i + 2); } }
        else { for (u32 i = 1; i + 1 < count; i++) { put(0); put(i); put(i + 1); } }
        b.n += n;
    };
    if (gxlog_on() && gxlog_visible(v, count)) { for (u32 i = 0; i < count && i < 4; i++) { float p[3], tc[2]; transform(v[i], p); texgen(v[i], 0, tc); fprintf(stderr, "   T v%u screen=%g %g %g tex0=%g %g\n", i, p[0], p[1], p[2], tc[0], tc[1]); }
        const float* m = &g_gx.xfMem[((g_gx.xfReg[0x18] >> 6) & 63) * 4]; fprintf(stderr, "   texmtx: %g %g %g %g / %g %g %g %g / %g %g %g %g ; mv row2: %g %g %g %g\n", m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11], g_gx.xfMem[8], g_gx.xfMem[9], g_gx.xfMem[10], g_gx.xfMem[11]); }
    // Atajo: sin escrituras de estado desde el dibujo anterior (y en el mismo fotograma de VI) el estado es el mismo: se anade al lote vigente.
    static u32 last_vi = ~0u;
    if (!g_state_dirty && g_cur && last_vi == g_vi_frames) { assemble(*g_cur); if (g_diag) g_t_sec_a += now_s() - tA; return; }
    g_state_dirty = false; last_vi = g_vi_frames;
    double tB = g_diag ? now_s() : 0; if (g_diag) g_t_sec_a += tB - tA;
    int nst; u64 pkey = prog_fast_key(g_gx.bp, nst);
    // estado de dibujo completo: si no cambia respecto al dibujo anterior, los vertices se acumulan en el mismo lote
    int used = 0; TexRef tex[8];
    for (int s = 0; s < nst; s++) { u32 order = g_gx.bp[0x28 + (s >> 1)]; u32 o = (s & 1) ? (order >> 12) : order; if ((o >> 6) & 1) { int m = o & 7; if (!tex[m].kind) tex[m] = get_texture(m); used |= 1 << m; } }
    DrawSig st; memset(&st, 0, sizeof st);
    st.pkey = pkey; st.z = g_gx.bp[0x40]; st.bm = g_gx.bp[0x41]; st.tl = g_gx.bp[0x20]; st.br = g_gx.bp[0x21];
    for (int m = 0; m < 8; m++) { if (!(used & (1 << m))) continue; st.tex[m].kind = tex[m].kind; st.tex[m].key = tex[m].key; st.tex[m].w = tex[m].w; st.tex[m].h = tex[m].h;
        u32 md = g_gx.bp[((m >> 2) ? 0xA0 : 0x80) + (m & 3)] & 0x1F; if (g_set.tex_filter == 1) md &= ~0x10u; else if (g_set.tex_filter == 2) md |= 0x10u; st.mode[m] = md; }
    for (int i = 0; i < 4; i++) color_from_bp(g_gx.bp[0xE0 + 2 * i], g_gx.bp[0xE1 + 2 * i], &st.C[i * 4]);
    memcpy(st.K, st.C, sizeof st.K);
    double tC = g_diag ? now_s() : 0; if (g_diag) g_t_sec_b += tC - tB;
    if (!g_cur || memcmp(&st, &g_last_state, sizeof st)) {
        flush_batch();
        g_cur = std::make_shared<Batch>();
        memcpy(g_cur->bp, g_gx.bp, sizeof g_cur->bp); g_cur->nst = nst; g_cur->used = used;
        for (int m = 0; m < 8; m++) { g_cur->tex[m] = tex[m]; g_cur->mode[m] = st.mode[m]; }
        g_cur->verts.reserve(4096);
        g_last_state = st;
    }
    assemble(*g_cur);
    if (g_diag) g_t_sec_c += now_s() - tC;
}

static void flush_batch() {
    if (g_cur && g_cur->n) { std::shared_ptr<Batch> b = g_cur; rq_push([b] { exec_draw(*b); }); }
    g_cur.reset();
}

void gx_bp_write(u32 reg, u32 value) {
    if (reg <= 0x21 || (reg >= 0x28 && reg <= 0x2B) || reg == 0x40 || reg == 0x41 || (reg >= 0x64 && reg <= 0x65) || reg >= 0x80) g_state_dirty = true;   // genmode, recorte, TEV, mezcla, texturas, TLUT...
    if (reg == 0x65 && g_video) {              // LOADTLUT1: copia la paleta ya apuntada por LOADTLUT0 (direccion >> 5)
        u32 addr = (g_gx.bp[0x64] & 0x1FFFFF) << 5; u32 tmem = value & 0x3FF, count = (value >> 10) & 0x3FF;
        std::vector<u16> pal(count * 16);
        for (u32 i = 0; i < pal.size(); i++) pal[i] = (u16)((LD8((addr | 0x80000000u) + 2 * i) << 8) | LD8((addr | 0x80000000u) + 2 * i + 1));
        g_tlut[tmem] = pal;
    }
    if (reg == 0x52) gx_copy_efb(value);
}
void gx_xf_write(u32 addr, u32 value) { (void)addr; (void)value; }

static void render_copy(u32 value, const u32* bp);
void gx_copy_efb(u32 value) {
    TimeAcc ta_(g_t_copy);
    g_copies++;
    flush_batch();
    if (frame_log()) fprintf(stderr, "[frame] COPY value=%x src=%x dim=%x dst=%x stride=%x\n", value, g_gx.bp[0x49], g_gx.bp[0x4A], g_gx.bp[0x4B], g_gx.bp[0x4D]);
    if (!g_video) return;
    if (!((value >> 14) & 1)) {                  // copia a textura: se registra por direccion de destino
        u32 dim = g_gx.bp[0x4A];
        int w = (dim & 0x3FF) + 1, h = ((dim >> 10) & 0x3FF) + 1;
        bool half = (value >> 9) & 1; int dw = half ? w / 2 : w, dh = half ? h / 2 : h;
        u32 addr = (g_gx.bp[0x4B] & 0xFFFFFF) << 5;
        if (dw > 0 && dh > 0 && addr) { CopyInfo& ci = g_copyinfo[addr]; ci.w = dw; ci.h = dh; }
    }
    if ((value >> 14) & 1) g_xfb_dirty = true;   // copia a XFB: lo que se presenta en el siguiente retrazado
    auto bp = std::make_shared<std::array<u32, 256>>(); memcpy(bp->data(), g_gx.bp, sizeof g_gx.bp);
    rq_push([value, bp] { render_copy(value, bp->data()); });
}

// (Re)crea el EFB y el XFB con la resolucion interna actual.
static void alloc_efb() {
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glBindTexture(GL_TEXTURE_2D, g_efb_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W, EFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_efb_tex, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depth_rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, EFB_W, EFB_H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depth_rb);
    glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1); glDepthMask(GL_TRUE); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, g_xfb_fbo);
    glBindTexture(GL_TEXTURE_2D, g_xfb_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W, EFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_xfb_tex, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    g_filter_dirty = true;
}

void gx_video_init() {
    if (getenv("RT_HEADLESS")) return;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "[gx] SDL: %s\n", SDL_GetError()); return; }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) pad_open_any(); else fprintf(stderr, "[input] sin soporte de mandos: %s\n", SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    g_win = SDL_CreateWindow("Castlevania: The Adventure ReBirth (port nativo)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_win) { fprintf(stderr, "[gx] ventana: %s\n", SDL_GetError()); return; }
    g_ctx = SDL_GL_CreateContext(g_win);
    SDL_GL_SetSwapInterval(0);
    settings_load(); settings_apply(true);                       // g_sc antes de crear el EFB
    glewExperimental = GL_TRUE; glewInit();
    fprintf(stderr, "[gx] OpenGL %s / %s; copy_image=%d\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), (int)GLEW_ARB_copy_image);
    glGenFramebuffers(1, &g_fbo); glGenTextures(1, &g_efb_tex); glGenFramebuffers(1, &g_xfb_fbo); glGenTextures(1, &g_xfb_tex); glGenRenderbuffers(1, &g_depth_rb);
    alloc_efb();
    glGenVertexArrays(1, &g_vao); glBindVertexArray(g_vao); glGenBuffers(1, &g_vbo); glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    for (int i = 0, off = 0; i < 7; i++) { int comps = i == 0 ? 3 : (i < 3 ? 4 : 2); glEnableVertexAttribArray(i); glVertexAttribPointer(i, comps, GL_FLOAT, GL_FALSE, FLOATS * 4, (void*)(size_t)(off * 4)); off += comps; }
    g_gl_ready = true;
    // El contexto de OpenGL pasa al hilo de render: desde aqui el hilo del juego no puede llamar a OpenGL.
    SDL_GL_MakeCurrent(g_win, nullptr);
    g_render_thread_on = true;
    std::thread(render_thread_main).detach();
    settings_apply(false);                                       // vsync / pantalla completa / limite
    g_video = true;
}

// Presenta el EFB en la ventana (en cada retrazado de VI) y atiende los eventos de SDL.
static const char* BLIT_VS = "#version 330 core\nout vec2 uv; void main() { vec2 p = vec2((gl_VertexID & 1) * 2.0, (gl_VertexID >> 1) * 2.0); uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }";
static const char* BLIT_FS = "#version 330 core\nin vec2 uv; out vec4 o; uniform sampler2D t; void main() { o = vec4(texture(t, uv).rgb, 1.0); }";

// ---------------------------------------------------------------------------------------------------
// Menu de configuracion (F3) y contador de FPS: texto con una fuente de mapa de bits 5x7 dibujada con OpenGL sobre la ventana
// ---------------------------------------------------------------------------------------------------
extern bool g_vi_uncapped;                                    // hle_vi.cpp: sin limite de fotogramas
static int g_menu_sel = 0;
static const char* settings_file() { const char* e = getenv("RT_SETTINGS"); return e ? e : "settings.cfg"; }   // en el directorio de trabajo (runtime/)

static void settings_save() {
    FILE* f = fopen(settings_file(), "w"); if (!f) return;
    fprintf(f, "show_fps=%d\nvsync=%d\nunlimited=%d\nscreen_filter=%d\ntex_filter=%d\nscale=%d\nfullscreen=%d\npause_bg=%d\n", g_set.show_fps, g_set.vsync, g_set.unlimited, g_set.screen_filter, g_set.tex_filter, g_set.scale, g_set.fullscreen, g_set.pause_bg);
    fclose(f);
}
static void settings_load() {
    FILE* f = fopen(settings_file(), "r"); if (!f) return;
    char k[64]; int v;
    while (fscanf(f, " %63[^=]=%d", k, &v) == 2) {
        if (!strcmp(k, "show_fps")) g_set.show_fps = v ? 1 : 0; else if (!strcmp(k, "vsync")) g_set.vsync = v ? 1 : 0;
        else if (!strcmp(k, "unlimited")) g_set.unlimited = v ? 1 : 0; else if (!strcmp(k, "screen_filter")) g_set.screen_filter = v ? 1 : 0;
        else if (!strcmp(k, "tex_filter")) g_set.tex_filter = v < 0 ? 0 : (v > 2 ? 2 : v); else if (!strcmp(k, "scale")) g_set.scale = v < 1 ? 1 : (v > 4 ? 4 : v);
        else if (!strcmp(k, "fullscreen")) g_set.fullscreen = v ? 1 : 0; else if (!strcmp(k, "pause_bg")) g_set.pause_bg = v ? 1 : 0;
    }
    fclose(f);
}
static int g_sc_want = 1;                                     // resolucion interna pedida (g_sc es del hilo de render)
static void settings_apply(bool initial) {
    g_state_dirty = true;
    g_show_fps = g_set.show_fps != 0 || getenv("RT_SHOW_FPS") != nullptr;
    g_vi_uncapped = g_set.unlimited != 0 || getenv("RT_UNCAPPED") != nullptr;
    if (g_win) {
        int vs = g_set.vsync ? 1 : 0;
        rq_push([vs] { SDL_GL_SetSwapInterval(vs); });            // OpenGL solo se toca desde el hilo de render
        SDL_SetWindowFullscreen(g_win, g_set.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    if (g_set.scale != g_sc_want) {                           // resolucion interna: se recrea el EFB/XFB en el hilo de render
        g_sc_want = g_set.scale;
        int sc = g_sc_want;
        if (!initial) flush_batch();
        rq_push([sc] { g_sc = sc; if (g_gl_ready) alloc_efb(); g_filter_dirty = true; });
    }
}

static const char* const MENU_NAMES[] = { "FPS COUNTER", "VSYNC", "FRAME LIMIT", "SCREEN FILTER", "TEXTURE FILTER", "RESOLUTION", "WINDOW", "PAUSE IN BACKGROUND", "CLOSE" };
static const int MENU_N = 9;
static Settings r_set; static int r_menu_sel = 0;              // copia de los ajustes / seleccion del menu que ve el hilo de render
static void menu_value(int i, char* out, size_t n) {
    static const char* tf[3] = { "GAME", "NEAREST", "LINEAR" };
    switch (i) {
    case 0: snprintf(out, n, "%s", r_set.show_fps ? "ON" : "OFF"); break;
    case 1: snprintf(out, n, "%s", r_set.vsync ? "ON" : "OFF"); break;
    case 2: snprintf(out, n, "%s", r_set.unlimited ? "UNLIMITED" : "60 FPS"); break;
    case 3: snprintf(out, n, "%s", r_set.screen_filter ? "LINEAR" : "NEAREST"); break;
    case 4: snprintf(out, n, "%s", tf[r_set.tex_filter]); break;
    case 5: snprintf(out, n, "%dX (%dX%d)", r_set.scale, LW * r_set.scale, LH * r_set.scale); break;
    case 6: snprintf(out, n, "%s", r_set.fullscreen ? "FULLSCREEN" : "WINDOWED"); break;
    case 7: snprintf(out, n, "%s", r_set.pause_bg ? "ON" : "OFF"); break;
    default: out[0] = 0;
    }
}
static void menu_change(int i, int dir) {
    switch (i) {
    case 0: g_set.show_fps ^= 1; break;
    case 1: g_set.vsync ^= 1; break;
    case 2: g_set.unlimited ^= 1; break;
    case 3: g_set.screen_filter ^= 1; g_filter_dirty = true; break;
    case 4: g_set.tex_filter = (g_set.tex_filter + 3 + dir) % 3; break;
    case 5: g_set.scale = g_set.scale + dir; if (g_set.scale < 1) g_set.scale = 4; if (g_set.scale > 4) g_set.scale = 1; break;
    case 6: g_set.fullscreen ^= 1; break;
    case 7: g_set.pause_bg ^= 1; break;
    case 8: g_menu_open = false; break;
    }
    settings_apply(false); settings_save();
}
static void menu_draw(int w, int h) {
    int sc = h >= 1000 ? 4 : (h >= 700 ? 3 : 2);
    while (sc > 1 && (46 * 6 + 12) * sc > w) sc--;                  // que el panel quepa en ventanas estrechas
    float lh = 11.f * sc, pad = 6.f * sc, tw = 46 * 6 * sc;
    float pw = tw + 2 * pad, ph = lh * (MENU_N + 3) + 2 * pad, px = (w - pw) / 2, py = (h - ph) / 2;
    ui_rect(px - sc, py - sc, pw + 2 * sc, ph + 2 * sc, 0.9f, 0.8f, 0.2f, 1.f);
    ui_rect(px, py, pw, ph, 0.04f, 0.04f, 0.12f, 0.92f);
    ui_text(px + pad, py + pad, sc, "SETTINGS", 1.f, 0.95f, 0.3f);
    for (int i = 0; i < MENU_N; i++) {
        char val[48]; menu_value(i, val, sizeof val); float y = py + pad + lh * (i + 1.5f); bool sel = i == r_menu_sel;
        float cr = sel ? 1.f : 0.85f, cg = sel ? 0.95f : 0.85f, cb = sel ? 0.3f : 0.85f;
        if (sel) ui_text(px + pad, y, sc, ">", cr, cg, cb);
        ui_text(px + pad + 2 * 6 * sc, y, sc, MENU_NAMES[i], cr, cg, cb);
        if (val[0]) { char buf[64]; snprintf(buf, sizeof buf, "%s%s%s", sel ? "< " : "  ", val, sel ? " >" : ""); ui_text(px + pad + tw - ui_text_width(buf, sc), y, sc, buf, cr, cg, cb); }
    }
    ui_text(px + pad, py + ph - pad - 7 * sc, sc, "UP/DOWN SELECT  LEFT/RIGHT CHANGE  F3 CLOSE", 0.6f, 0.7f, 0.9f);
}

// ---------------------------------------------------------------------------------------------------
// Lado del hilo de render (OpenGL)
// ---------------------------------------------------------------------------------------------------
static std::unordered_map<u32, GLuint> g_gltex;                 // manejador de textura del juego -> textura GL
static std::unordered_map<GLuint, u32> g_texmode;               // parametros de muestreo por objeto de textura
struct CopyTex { GLuint id = 0; int w = 0, h = 0; u32 fmt = 0; GLuint pad = 0; int pw = 0, ph = 0; u32 version = 0, pversion = 0; };
static std::unordered_map<u32, CopyTex> g_copytex;              // copias EFB->textura por direccion de destino
static GLuint g_copy_fbo = 0;
struct RState { GLuint prog; GLuint tex[8]; u32 mode[8]; u32 z, bm, tl, br; float C[16], K[16]; };
static RState g_rlast; static bool g_rstate_valid = false;      // estado GL que dejo el ultimo dibujo (se invalida cuando otra operacion lo toca)

static void render_upload_tex(u32 handle, int w, int h, const std::vector<u8>& rgba) {
    GLuint& id = g_gltex[handle];
    if (!id) glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    g_rstate_valid = false;
}

static GLuint resolve_tex(const TexRef& r) {
    if (r.kind == 1) { auto it = g_gltex.find(r.key); return it == g_gltex.end() ? 0 : it->second; }
    if (r.kind != 2) return 0;
    auto ct = g_copytex.find(r.key);
    if (ct == g_copytex.end() || !ct->second.id) return 0;
    CopyTex& c = ct->second; int w = r.w, h = r.h;
    if (c.w == w && c.h == h) return c.id;
    if (w >= c.w && h >= c.h) {                  // el juego la ve rellenada a potencia de 2: vista con el contenido arriba a la izquierda
        if (!c.pad) glGenTextures(1, &c.pad);
        if (c.pw != w || c.ph != h) { glBindTexture(GL_TEXTURE_2D, c.pad); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); c.pw = w; c.ph = h; c.pversion = 0; }
        if (c.pversion != c.version) {
            static GLuint fr = 0, fd = 0; if (!fr) { glGenFramebuffers(1, &fr); glGenFramebuffers(1, &fd); }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fr); glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.id, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fd); glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.pad, 0);
            glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1);
            glBlitFramebuffer(0, 0, c.w, c.h, 0, 0, c.w, c.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            c.pversion = c.version;
        }
        g_rstate_valid = false;
        return c.pad;
    }
    return 0;
}

static void exec_draw(Batch& b) {
    int nst; Prog& pg = get_prog(b.bp, nst);
    GLuint tex[8] = {};
    for (int m = 0; m < 8; m++) if (b.used & (1 << m)) tex[m] = resolve_tex(b.tex[m]);
    RState rs; memset(&rs, 0, sizeof rs);
    rs.prog = pg.id; rs.z = b.bp[0x40]; rs.bm = b.bp[0x41]; rs.tl = b.bp[0x20]; rs.br = b.bp[0x21];
    for (int m = 0; m < 8; m++) { if (!(b.used & (1 << m))) continue; rs.tex[m] = tex[m]; rs.mode[m] = b.mode[m]; }
    for (int i = 0; i < 4; i++) color_from_bp(b.bp[0xE0 + 2 * i], b.bp[0xE1 + 2 * i], &rs.C[i * 4]);
    memcpy(rs.K, rs.C, sizeof rs.K);
    if (!g_rstate_valid || memcmp(&rs, &g_rlast, sizeof rs)) {
        glUseProgram(pg.id);
        for (int m = 0; m < 8; m++) { if (!(b.used & (1 << m))) continue; glActiveTexture(GL_TEXTURE0 + m); glBindTexture(GL_TEXTURE_2D, tex[m]);
            u32 mode0 = rs.mode[m]; u32 ws = mode0 & 3, wt = (mode0 >> 2) & 3; bool lin = (mode0 >> 4) & 1;
            u32& cached = g_texmode[tex[m]];
            if (cached != (mode0 | 0x100)) {
                static const GLenum wrap[3] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT };
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[ws > 2 ? 0 : ws]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[wt > 2 ? 0 : wt]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, lin ? GL_LINEAR : GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, lin ? GL_LINEAR : GL_NEAREST);
                cached = mode0 | 0x100; }
            glUniform1i(pg.uTex[m], m); }
        glUniform4fv(pg.uK[0], 4, rs.C); glUniform4fv(pg.uKonst, 4, rs.K);
        apply_blend(b.bp);
        g_rlast = rs; g_rstate_valid = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo); glViewport(0, 0, EFB_W, EFB_H);
    glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, b.verts.size() * 4, b.verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, b.n);
}

static void render_copy(u32 value, const u32* bp) {
    g_rstate_valid = false;
    if (!((value >> 14) & 1)) {                  // copia a textura
        u32 src = bp[0x49], dim = bp[0x4A];
        int sx = src & 0x3FF, sy = (src >> 10) & 0x3FF, w = (dim & 0x3FF) + 1, h = ((dim >> 10) & 0x3FF) + 1;
        bool half = (value >> 9) & 1; int dw = half ? w / 2 : w, dh = half ? h / 2 : h;
        u32 addr = (bp[0x4B] & 0xFFFFFF) << 5;
        if (dw > 0 && dh > 0 && addr) {
            CopyTex& ct = g_copytex[addr];
            if (!ct.id || ct.w != dw || ct.h != dh) {
                if (!ct.id) glGenTextures(1, &ct.id);
                glBindTexture(GL_TEXTURE_2D, ct.id);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                ct.w = dw; ct.h = dh;
            }
            ct.fmt = ((value >> 4) & 7) | ((value >> 3) & 8);
            if (!g_copy_fbo) glGenFramebuffers(1, &g_copy_fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_copy_fbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct.id, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo); glDisable(GL_SCISSOR_TEST);
            // EFB y=0 es la fila superior (abajo en GL); la textura guarda la fila superior primero
            glBlitFramebuffer(sx * g_sc, (LH - sy - h) * g_sc, (sx + w) * g_sc, (LH - sy) * g_sc, 0, dh, dw, 0, GL_COLOR_BUFFER_BIT, (half || g_sc > 1) ? GL_LINEAR : GL_NEAREST);
            ct.version++;
        }
    }
    if ((value >> 14) & 1) {                     // copia a XFB: lo que se presenta en el siguiente retrazado
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_xfb_fbo); glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, EFB_W, EFB_H, 0, 0, EFB_W, EFB_H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    if (value & 0x800) {                         // borrar el EFB tras la copia
        u32 ar = bp[0x4F], gb = bp[0x50];
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo); glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1); glDepthMask(GL_TRUE);
        glClearColor((ar & 255) / 255.f, ((gb >> 8) & 255) / 255.f, (gb & 255) / 255.f, ((ar >> 8) & 255) / 255.f); glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

static void render_present(const PresentInfo& pi) {
    g_rstate_valid = false;
    r_set = pi.set; r_menu_sel = pi.menu_sel;
    static GLuint prog = 0, vao = 0;
    if (!prog) {
        auto c = [](GLenum t, const char* s) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh); return sh; };
        prog = glCreateProgram(); glAttachShader(prog, c(GL_VERTEX_SHADER, BLIT_VS)); glAttachShader(prog, c(GL_FRAGMENT_SHADER, BLIT_FS)); glLinkProgram(prog); glGenVertexArrays(1, &vao);
    }
    int frame = pi.frame;
    static const int dump_max = getenv("RT_DUMP_MAX") ? atoi(getenv("RT_DUMP_MAX")) : 8; static const char* dump_every = getenv("RT_DUMP_FRAME");
    bool dump = dump_every && frame % atoi(dump_every) == 0 && frame <= dump_max * atoi(dump_every);
    std::string prefix = getenv("RT_DUMP_PREFIX") ? getenv("RT_DUMP_PREFIX") : "/tmp/claude-1000/efb_";
    if (dump) {
        std::vector<u8> px(EFB_W * EFB_H * 4); glBindFramebuffer(GL_FRAMEBUFFER, g_xfb_fbo); glReadPixels(0, 0, EFB_W, EFB_H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        FILE* f = fopen((prefix + std::to_string(frame) + ".ppm").c_str(), "wb"); fprintf(f, "P6 %d %d 255\n", EFB_W, EFB_H);
        for (int y = EFB_H - 1; y >= 0; y--) for (int x = 0; x < EFB_W; x++) fwrite(&px[(y * EFB_W + x) * 4], 1, 3, f); fclose(f);
        fprintf(stderr, "[gx] EFB volcado (fotograma %d), glError=%x\n", frame, glGetError());
    }
    int w, h; SDL_GL_GetDrawableSize(g_win, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glColorMask(1, 1, 1, 1);
    glViewport(0, 0, w, h); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    int vw = w, vh = w * 3 / 4; if (vh > h) { vh = h; vw = h * 4 / 3; }
    glViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
    glUseProgram(prog); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_xfb_tex); glUniform1i(glGetUniformLocation(prog, "t"), 0);
    if (g_filter_dirty || pi.set.screen_filter != g_filter_applied) { GLint f = pi.set.screen_filter ? GL_LINEAR : GL_NEAREST; glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f); g_filter_dirty = false; g_filter_applied = pi.set.screen_filter; }
    glBindVertexArray(vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    if (pi.show_fps || pi.menu_open) {
        ui_begin(w, h);
        if (pi.show_fps) { int sc = h >= 1000 ? 4 : (h >= 500 ? 3 : 2); ui_text((float)(4 * sc), (float)(4 * sc), sc, pi.fps_text, 1.f, 0.95f, 0.2f); }
        if (pi.menu_open) menu_draw(w, h);
        glDisable(GL_BLEND);
    }
    if (dump) {                                                    // captura de la ventana (con el contador)
        std::vector<u8> px((size_t)w * h * 4); glBindFramebuffer(GL_FRAMEBUFFER, 0); glReadBuffer(GL_BACK); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        FILE* f = fopen((prefix + "win" + std::to_string(frame) + ".ppm").c_str(), "wb"); fprintf(f, "P6 %d %d 255\n", w, h);
        for (int y = h - 1; y >= 0; y--) for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f); fclose(f);
    }
    SDL_GL_SwapWindow(g_win);
}

static void render_thread_main() {
#ifdef _WIN32
    if (!getenv("RT_NO_PRIORITY")) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
    SDL_GL_MakeCurrent(g_win, g_ctx);
    for (;;) {
        std::function<void()> f;
        { std::unique_lock<std::mutex> lk(g_q_mtx); g_q_work.wait(lk, [] { return !g_q.empty(); }); f = std::move(g_q.front()); g_q.pop_front(); }
        g_q_room.notify_all();
        double t0 = now_s(); f(); g_t_render += now_s() - t0;
    }
}

void gx_frame_present() {
    if (getenv("RT_WATCH_DISPLAY")) { static u32 last_obj = 0, last_w = 0xFFFFFFFF; u32 o = LD32(0x803166CC);
        if (o != last_obj) { fprintf(stderr, "[watch] MDisplay obj = %08x\n", o); last_obj = o; last_w = 0xFFFFFFFF; }
        if (o) { u32 w = LD32(o + 0x10); if (w != last_w) { fprintf(stderr, "[watch] obj+0x10 = %08x (antes %08x)\n", w, last_w); last_w = w; } } }
    if (!g_video) return;
    TimeAcc ta_(g_t_present);
    flush_batch();
    int& frame = g_frame_no; frame++;
    PresentInfo pi; memset(&pi, 0, sizeof pi);
    { // menu F3 y contador de FPS (fotogramas de juego distintos = copias a XFB presentadas por segundo)
        static unsigned frames = 0; static double t0 = now_s(); static int shown = 0;
        if (g_xfb_dirty) { frames++; g_xfb_dirty = false; }
        double t = now_s(); if (t - t0 >= 0.5) { shown = (int)((frames / (t - t0)) + 0.5); frames = 0; t0 = t; }
        const u8* ks = SDL_GetKeyboardState(nullptr);
        auto edge = [&](SDL_Scancode sc) { static bool prev[512]; bool now = ks[sc] != 0, e = now && !prev[sc]; prev[sc] = now; return e; };
        bool f3 = edge(SDL_SCANCODE_F3), f4 = edge(SDL_SCANCODE_F4), up = edge(SDL_SCANCODE_UP), dn = edge(SDL_SCANCODE_DOWN), lf = edge(SDL_SCANCODE_LEFT), rt = edge(SDL_SCANCODE_RIGHT), en = edge(SDL_SCANCODE_RETURN), es = edge(SDL_SCANCODE_ESCAPE);
        {   // mando: el boton central abre/cierra el menu; cruceta/palanca navegan, A confirma, B cierra
            static bool pv[8]; bool l, r, u, d; pad_dirs(l, r, u, d);
            bool now[8] = { pad_btn(SDL_CONTROLLER_BUTTON_GUIDE), u, d, l, r, pad_btn(SDL_CONTROLLER_BUTTON_A), pad_btn(SDL_CONTROLLER_BUTTON_B), false };
            bool e_[8]; for (int i = 0; i < 8; i++) { e_[i] = now[i] && !pv[i]; pv[i] = now[i]; }
            if (e_[0]) f3 = true;
            if (g_menu_open) { up |= e_[1]; dn |= e_[2]; lf |= e_[3]; rt |= e_[4]; en |= e_[5]; es |= e_[6]; }
        }
        if (f3) g_menu_open = !g_menu_open;
        if (f4) { g_set.unlimited ^= 1; settings_apply(false); settings_save(); fprintf(stderr, "[rt] limite de 60 Hz %s\n", g_set.unlimited ? "DESACTIVADO" : "activado"); }
        if (g_menu_open) {
            if (es) g_menu_open = false;
            if (up) g_menu_sel = (g_menu_sel + MENU_N - 1) % MENU_N;
            if (dn) g_menu_sel = (g_menu_sel + 1) % MENU_N;
            if (lf) menu_change(g_menu_sel, -1);
            if (rt || en) menu_change(g_menu_sel, 1);
        }
        pi.set = g_set; pi.show_fps = g_show_fps; pi.menu_open = g_menu_open; pi.menu_sel = g_menu_sel; pi.frame = frame;
        snprintf(pi.fps_text, sizeof pi.fps_text, "FPS %d", shown);
    }
    g_pending_presents++;
    rq_push([pi] { render_present(pi); g_pending_presents--; { std::lock_guard<std::mutex> lk(g_q_mtx); } g_q_room.notify_all(); });
    { static const int depth = getenv("RT_PRESENT_DEPTH") ? atoi(getenv("RT_PRESENT_DEPTH")) : 4;   // fotogramas por delante del hilo de render antes de esperar
      double tw = now_s(); rq_wait_presents(depth); g_t_wait_present += now_s() - tw;
      static double last = 0; double nowt = now_s(); if (last > 0) { double ms = (nowt - last) * 1000; if (ms > g_max_interval_ms) g_max_interval_ms = ms; if (ms > 20.0) g_slow_frames++; } last = nowt; }
    // Eventos de la ventana. Si deja de estar en primer plano (otro espacio de trabajo, minimizada, otra ventana) el juego se pausa:
    // asi no se bloquea el intercambio de buffers de una ventana oculta y el reloj del juego no salta al volver.
    SDL_Event e; bool lost = false;
    static const char* test_pause = getenv("RT_TEST_PAUSE");            // prueba: RT_TEST_PAUSE=n simula perder el foco en el fotograma n
    if (test_pause && frame == atoi(test_pause)) { SDL_Event f; SDL_zero(f); f.type = SDL_WINDOWEVENT; f.window.event = SDL_WINDOWEVENT_FOCUS_LOST; SDL_PushEvent(&f); }
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) { fprintf(stderr, "[rt] ventana cerrada\n"); exit(0); }
        pad_event(e);
        if (e.type == SDL_WINDOWEVENT) {
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_HIDDEN || e.window.event == SDL_WINDOWEVENT_MINIMIZED) lost = true;
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_RESTORED) lost = false;
        }
    }
    if (lost && g_set.pause_bg) {
        extern void rt_time_skip(double); extern void audio_pause(bool, double);
        double t0 = now_s(); audio_pause(true, 0);
        for (bool back = false; !back;) {
            if (test_pause && now_s() - t0 > 5.0) { SDL_Event f; SDL_zero(f); f.type = SDL_WINDOWEVENT; f.window.event = SDL_WINDOWEVENT_FOCUS_GAINED; SDL_PushEvent(&f); }
            if (!SDL_WaitEventTimeout(&e, 250)) continue;
            if (e.type == SDL_QUIT) { fprintf(stderr, "[rt] ventana cerrada\n"); exit(0); }
            if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_SHOWN || e.window.event == SDL_WINDOWEVENT_RESTORED)) back = true;
        }
        double dt = now_s() - t0; rt_time_skip(dt); audio_pause(false, dt);
        rq_push([] { g_filter_dirty = true; });
    }
}

void gx_render_stats() {
    for (auto& kv : g_draw_kinds) fprintf(stderr, "[gx] dibujo prim=%u vcd=%08X/%08X vatA=%08X: %llu\n", std::get<0>(kv.first), std::get<1>(kv.first), std::get<2>(kv.first), std::get<3>(kv.first), (unsigned long long)kv.second);
    fprintf(stderr, "[gx] dibujos=%llu vertices=%llu copias_efb=%llu\n", (unsigned long long)g_draws, (unsigned long long)g_verts, (unsigned long long)g_copies);
}

// Teclado (Wii Remote en horizontal): flechas/WASD = cruceta girada, Z/1 = boton 1 (saltar), X/2 = boton 2 (atacar), C = A, V = B, Espacio/Intro = +, Backspace = -, H = HOME. Raton: puntero y clic izquierdo = A.
u32 gx_input_buttons() {
    // Pruebas sin teclado: RT_AUTO_SEQ="n:mascara[p],..." (lecturas de mando n; mascara en hex; p = pulsar 20 lecturas si / 20 no)
    if (const char* sq = getenv("RT_AUTO_SEQ")) {
        static unsigned n = 0; n++;
        unsigned best = 0; u32 mask = 0; bool pulse = false; const char* q = sq;
        while (*q) { char* e; unsigned at = strtoul(q, &e, 10); if (*e != ':') break; u32 m = strtoul(e + 1, &e, 16); bool p = false; if (*e == 'p') { p = true; e++; }
            if (n >= at && at >= best) { best = at; mask = m; pulse = p; }
            if (*e == ',') q = e + 1; else break; }
        static const bool up2 = getenv("RT_PAD_UPRIGHT") != nullptr;
        // prueba de mando virtual: RT_TEST_VPAD=n:boton (indice SDL de boton de mando, p. ej. 14 = cruceta derecha) lo mantiene pulsado desde la lectura n
        static SDL_Joystick* vjs = nullptr; static int vat = -1, vbtn = 0;
        if (const char* tv = getenv("RT_TEST_VPAD")) {
            if (!vjs) { SDL_VirtualJoystickDesc d; SDL_zero(d); d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION; d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER; d.naxes = 6; d.nbuttons = 15;
                d.button_mask = 0x7FFF; d.axis_mask = 0x3F; int id = SDL_JoystickAttachVirtualEx(&d); if (id >= 0) { vjs = SDL_JoystickOpen(id); pad_open_any(); } sscanf(tv, "%d:%d", &vat, &vbtn); }
            if (vjs && (int)n == vat) SDL_JoystickSetVirtualButton(vjs, vbtn, 1);
        }
        return (pulse ? ((((n - best) / 20) & 1) ? mask : 0) : mask) | pad_hold(up2);
    }
    if (const char* e = getenv("RT_AUTO_BTN")) { static unsigned n = 0; n++; static unsigned after = getenv("RT_AUTO_AFTER") ? atoi(getenv("RT_AUTO_AFTER")) : 0; if (n < after) return 0; return (((n - after) / 20) & 1) ? (u32)strtoul(e, nullptr, 16) : 0; }
    if (!g_video || g_menu_open) return 0;
    const u8* k = SDL_GetKeyboardState(nullptr); u32 h = 0;
    // El juego se maneja con el Wii Remote en horizontal (como un mando de NES): la cruceta fisica esta girada 90 grados, asi que
    // izquierda de pantalla = WPAD_UP, derecha = WPAD_DOWN, arriba = WPAD_RIGHT y abajo = WPAD_LEFT. RT_PAD_UPRIGHT=1 deja la cruceta sin girar.
    static const bool upright = getenv("RT_PAD_UPRIGHT") != nullptr;
    bool kl = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A], kr = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D], ku = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W], kd = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
    if (upright) { if (kl) h |= 0x0001; if (kr) h |= 0x0002; if (kd) h |= 0x0004; if (ku) h |= 0x0008; }
    else { if (kl) h |= 0x0008; if (kr) h |= 0x0004; if (ku) h |= 0x0002; if (kd) h |= 0x0001; }
    // Botones: Z / 1 = boton 1 (saltar), X / 2 = boton 2 (atacar, confirmar en los menus), C = A, V = B, Espacio/Intro = +, Retroceso = -, H = HOME
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN]) h |= 0x0010;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_2]) h |= 0x0100; if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_1]) h |= 0x0200;
    if (k[SDL_SCANCODE_V]) h |= 0x0400; if (k[SDL_SCANCODE_C]) h |= 0x0800; if (k[SDL_SCANCODE_BACKSPACE]) h |= 0x1000; if (k[SDL_SCANCODE_H]) h |= 0x8000;
    h |= pad_hold(upright);
    u32 m = SDL_GetMouseState(nullptr, nullptr); if (m & SDL_BUTTON(SDL_BUTTON_LEFT)) h |= 0x0800; if (m & SDL_BUTTON(SDL_BUTTON_RIGHT)) h |= 0x0400;
    return h;
}
void gx_input_pointer(float* x, float* y) {
    *x = *y = 0; if (const char* e = getenv("RT_AUTO_PTR")) { sscanf(e, "%f,%f", x, y); return; }
    if (!g_video) return;
    int mx, my, w, h; SDL_GetMouseState(&mx, &my); SDL_GetWindowSize(g_win, &w, &h);
    *x = w ? 2.0f * mx / w - 1.0f : 0; *y = h ? 1.0f - 2.0f * my / h : 0;
}
