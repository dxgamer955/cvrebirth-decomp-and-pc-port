// Texto e interfaz sencilla (menu HOME, menu F3, contador de FPS) dibujados con OpenGL sobre la ventana.
// Fuente: la de 8x8 del propio juego (gen/ui_gamefont.h, que scripts/make_uifont.py saca de font/small1.psb.l de tu copia)
// o, si no se ha generado (o con RT_UI_FONT=builtin), una de mapa de bits 5x7 incluida aqui.
#include "ui_font.h"
#include "native_lift.h"
#include <GL/glew.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>
#if __has_include("ui_gamefont.h")
#include "ui_gamefont.h"
#define UI_HAVE_GAME_FONT 1
#endif

static const char* GLYPH_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-<>/()+,!#";   // "#" = bloque macizo (ui_rect usa el ultimo glifo)
static const char* GLYPHS[] = {   // 5x7: 35 caracteres por glifo (filas de arriba a abajo)
    "00000000000000000000000000000000000",
    "01110100011000110001111111000110001", "11110100011000111110100011000111110", "01110100011000010000100001000101110", "11110100011000110001100011000111110",
    "11111100001000011110100001000011111", "11111100001000011110100001000010000", "01110100011000010111100011000101111", "10001100011000111111100011000110001",
    "01110001000010000100001000010001110", "00111000100001000010000101001001100", "10001100101010011000101001001010001", "10000100001000010000100001000011111",
    "10001110111010110101100011000110001", "10001110011010110011100011000110001", "01110100011000110001100011000101110", "11110100011000111110100001000010000",
    "01110100011000110001101011001001101", "11110100011000111110101001001010001", "01111100001000001110000010000111110", "11111001000010000100001000010000100",
    "10001100011000110001100011000101110", "10001100011000110001100010101000100", "10001100011000110101101011101110001", "10001100010101000100010101000110001",
    "10001100010101000100001000010000100", "11111000010001000100010001000011111",
    "01110100011001110101110011000101110", "00100011000010000100001000010001110", "01110100010000100010001000100011111", "11110000010000101110000010000111110",
    "00010001100101010010111110001000010", "11111100001111000001000011000101110", "00110010001000011110100011000101110", "11111000010001000100010000100001000",
    "01110100011000101110100011000101110", "01110100011000101111000010001001100",
    "00000000000000000000000000000000100", "00000001000000000000000000100000000", "00000000000000011111000000000000000",
    "00010001000100010000010000010000010", "01000001000001000001000100010001000", "00001000010001000100010001000010000",     // < > /
    "00010001000100001000010000010000100", "01000001000001000010000100001000100",                                            // ( )
    "00000001000010011111001000010000000", "00000000000000000000001100010001000", "00100001000010000100001000000000100",     // + , !
    "11111111111111111111111111111111111" };
static const int NGLYPH = 49;
static_assert(sizeof(GLYPHS) / sizeof(GLYPHS[0]) == NGLYPH, "glifos");

// Fuente activa: atlas RGBA de celdas cw x ch en cols columnas; cada glifo mide gw x gh y avanza adv.
// La del juego trae su sombra pintada; la 5x7 se dibuja dos veces (sombra negra desplazada y luego el color).
struct UiFont { int cols, cw, ch, gw, gh, adv, aw, ah; bool shadow_pass; int index[128]; int solid; };
static UiFont g_font;
static std::vector<u8> g_font_rgba;
static void font_setup() {
    static bool done = false; if (done) return; done = true;
    const char* e = getenv("RT_UI_FONT"); bool builtin = e && !strcmp(e, "builtin");
#ifdef UI_HAVE_GAME_FONT
    if (!builtin) {
        g_font = { UIF_COLS, UIF_CELL, UIF_CELL, UIF_CELL, UIF_CELL, UIF_CELL, UIF_AW, UIF_AH, false, {}, UIF_INDEX[0x7f] };
        for (int i = 0; i < 128; i++) g_font.index[i] = UIF_INDEX[i] == 255 ? -1 : UIF_INDEX[i];
        g_font_rgba.assign(UIF_RGBA, UIF_RGBA + sizeof UIF_RGBA);
        return;
    }
#endif
    (void)builtin;
    g_font = { NGLYPH, 6, 7, 5, 7, 6, NGLYPH * 6, 7, true, {}, NGLYPH - 1 };   // cada glifo ocupa 6 columnas (5 + 1 de separacion)
    for (int i = 0; i < 128; i++) { const char* f = i ? strchr(GLYPH_CHARS, i) : nullptr; g_font.index[i] = f && i != '#' ? (int)(f - GLYPH_CHARS) : -1; }
    g_font_rgba.assign((size_t)g_font.aw * g_font.ah * 4, 0);
    for (int g = 0; g < NGLYPH; g++) for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++)
        if (GLYPHS[g][y * 5 + x] == '1') memset(&g_font_rgba[((size_t)y * g_font.aw + g * 6 + x) * 4], 255, 4);
    for (int y = 0; y < 7; y++) memset(&g_font_rgba[((size_t)y * g_font.aw + (NGLYPH - 1) * 6 + 5) * 4], 255, 4);   // bloque macizo sin hueco
}
int ui_char_w() { font_setup(); return g_font.adv; }
int ui_char_h() { font_setup(); return g_font.gh; }

static GLuint g_ui_prog = 0, g_ui_vao = 0, g_ui_vbo = 0, g_ui_tex = 0;
static void ui_init() {
    font_setup();
    auto c = [](GLenum t, const char* src) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh); return sh; };
    const char* vs = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 sz; out vec2 vuv;\n"
                     "void main() { gl_Position = vec4(p.x / sz.x * 2.0 - 1.0, 1.0 - p.y / sz.y * 2.0, 0.0, 1.0); vuv = uv; }";
    const char* fs = "#version 330 core\nin vec2 vuv; uniform sampler2D t; uniform vec4 col; out vec4 o;\n"
                     "void main() { vec4 c = texture(t, vuv); if (c.a < 0.5) discard; o = vec4(c.rgb * col.rgb, col.a); }";
    g_ui_prog = glCreateProgram(); glAttachShader(g_ui_prog, c(GL_VERTEX_SHADER, vs)); glAttachShader(g_ui_prog, c(GL_FRAGMENT_SHADER, fs)); glLinkProgram(g_ui_prog);
    glGenVertexArrays(1, &g_ui_vao); glGenBuffers(1, &g_ui_vbo); glBindVertexArray(g_ui_vao); glBindBuffer(GL_ARRAY_BUFFER, g_ui_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    glGenTextures(1, &g_ui_tex); glBindTexture(GL_TEXTURE_2D, g_ui_tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_font.aw, g_font.ah, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_font_rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
void ui_begin(int w, int h) {
    if (!g_ui_prog) ui_init();
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glColorMask(1, 1, 1, 1);
    glViewport(0, 0, w, h); glUseProgram(g_ui_prog); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_ui_tex);
    glUniform1i(glGetUniformLocation(g_ui_prog, "t"), 0); glUniform2f(glGetUniformLocation(g_ui_prog, "sz"), (float)w, (float)h);
    glBindVertexArray(g_ui_vao); glBindBuffer(GL_ARRAY_BUFFER, g_ui_vbo);
}
static void ui_draw(const std::vector<float>& v, float r, float g, float b, float a) {
    if (v.empty()) return;
    glUniform4f(glGetUniformLocation(g_ui_prog, "col"), r, g, b, a);
    glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 4));
}
static void quad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    float q[6][4] = { { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x1, y1, u1, v1 }, { x0, y0, u0, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 } };
    for (auto& t : q) v.insert(v.end(), t, t + 4);
}
void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    // Centro de la celda maciza del atlas (blanca): el color sale de "col"
    float u = ((g_font.solid % g_font.cols) * g_font.cw + 2.5f) / g_font.aw, vv = ((g_font.solid / g_font.cols) * g_font.ch + 2.5f) / g_font.ah;
    std::vector<float> v; quad(v, x, y, x + w, y + h, u, vv, u, vv); ui_draw(v, r, g, b, a);
}
void ui_image(unsigned tex, float x, float y, float w, float h) {
    static GLuint prog = 0;
    if (!prog) {
        auto c = [](GLenum t, const char* src) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh); return sh; };
        const char* vs = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 sz; out vec2 vuv;\n"
                         "void main() { gl_Position = vec4(p.x / sz.x * 2.0 - 1.0, 1.0 - p.y / sz.y * 2.0, 0.0, 1.0); vuv = uv; }";
        const char* fs = "#version 330 core\nin vec2 vuv; uniform sampler2D t; out vec4 o;\nvoid main() { o = vec4(texture(t, vuv).rgb, 1.0); }";
        prog = glCreateProgram(); glAttachShader(prog, c(GL_VERTEX_SHADER, vs)); glAttachShader(prog, c(GL_FRAGMENT_SHADER, fs)); glLinkProgram(prog);
    }
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    glUseProgram(prog); glUniform2f(glGetUniformLocation(prog, "sz"), (float)vp[2], (float)vp[3]); glUniform1i(glGetUniformLocation(prog, "t"), 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
    std::vector<float> v; quad(v, x, y, x + w, y + h, 0.f, 0.f, 1.f, 1.f);
    glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW); glDrawArrays(GL_TRIANGLES, 0, 6);
    glUseProgram(g_ui_prog); glBindTexture(GL_TEXTURE_2D, g_ui_tex);           // de vuelta al estado de ui_begin
}
float ui_text_width(const char* t, int sc) { return (float)strlen(t) * ui_char_w() * sc; }
void ui_text(float x, float y, int sc, const char* text, float r, float g, float b) {
    font_setup();
    std::vector<float> v; float pen = 0;
    for (const char* q = text; *q; q++) {
        int ch = toupper((unsigned char)*q); int gi = ch < 128 ? g_font.index[ch] : -1;
        if (gi >= 0 && ch != ' ') {
            float cx = (float)(gi % g_font.cols * g_font.cw), cy = (float)(gi / g_font.cols * g_font.ch);
            quad(v, x + pen, y, x + pen + g_font.gw * sc, y + g_font.gh * sc,
                 cx / g_font.aw, cy / g_font.ah, (cx + g_font.gw) / g_font.aw, (cy + g_font.gh) / g_font.ah);
        }
        pen += g_font.adv * sc;
    }
    if (g_font.shadow_pass) { std::vector<float> sh = v; for (size_t i = 0; i < sh.size(); i += 4) { sh[i] += sc; sh[i + 1] += sc; } ui_draw(sh, 0, 0, 0, 1); }
    ui_draw(v, r, g, b, 1);
}
