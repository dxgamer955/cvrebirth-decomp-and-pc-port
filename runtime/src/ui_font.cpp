// Texto e interfaz sencilla (menu F3, contador de FPS): fuente de mapa de bits 5x7 dibujada con OpenGL sobre la ventana.
#include "ui_font.h"
#include "native_lift.h"
#include <GL/glew.h>
#include <cctype>
#include <cstring>
#include <vector>

static const char* GLYPH_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-<>/()#";
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
    "11111111111111111111111111111111111" };
static const int NGLYPH = 46;
static_assert(sizeof(GLYPHS) / sizeof(GLYPHS[0]) == NGLYPH, "glifos");

static GLuint g_ui_prog = 0, g_ui_vao = 0, g_ui_vbo = 0, g_ui_tex = 0;
static void ui_init() {
    auto c = [](GLenum t, const char* src) { GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &src, nullptr); glCompileShader(sh); return sh; };
    const char* vs = "#version 330 core\nlayout(location=0) in vec2 p; layout(location=1) in vec2 uv; uniform vec2 sz; out vec2 vuv;\n"
                     "void main() { gl_Position = vec4(p.x / sz.x * 2.0 - 1.0, 1.0 - p.y / sz.y * 2.0, 0.0, 1.0); vuv = uv; }";
    const char* fs = "#version 330 core\nin vec2 vuv; uniform sampler2D t; uniform vec4 col; out vec4 o;\nvoid main() { float a = texture(t, vuv).r; if (a < 0.5) discard; o = col; }";
    g_ui_prog = glCreateProgram(); glAttachShader(g_ui_prog, c(GL_VERTEX_SHADER, vs)); glAttachShader(g_ui_prog, c(GL_FRAGMENT_SHADER, fs)); glLinkProgram(g_ui_prog);
    glGenVertexArrays(1, &g_ui_vao); glGenBuffers(1, &g_ui_vbo); glBindVertexArray(g_ui_vao); glBindBuffer(GL_ARRAY_BUFFER, g_ui_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    std::vector<u8> atlas(NGLYPH * 6 * 7, 0);                          // cada glifo ocupa 6 columnas (5 + 1 de separacion)
    for (int g = 0; g < NGLYPH; g++) for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) atlas[y * NGLYPH * 6 + g * 6 + x] = GLYPHS[g][y * 5 + x] == '1' ? 255 : 0;
    glGenTextures(1, &g_ui_tex); glBindTexture(GL_TEXTURE_2D, g_ui_tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, NGLYPH * 6, 7, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
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
static void quad(std::vector<float>& v, float x0, float y0, float x1, float y1, float u0, float u1) {
    float q[6][4] = { { x0, y0, u0, 0 }, { x1, y0, u1, 0 }, { x1, y1, u1, 1 }, { x0, y0, u0, 0 }, { x1, y1, u1, 1 }, { x0, y1, u0, 1 } };
    for (auto& t : q) v.insert(v.end(), t, t + 4);
}
void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    std::vector<float> v; float u = (NGLYPH - 1) * 6 + 2.5f; quad(v, x, y, x + w, y + h, u / (NGLYPH * 6), u / (NGLYPH * 6)); ui_draw(v, r, g, b, a);
}
float ui_text_width(const char* t, int sc) { return (float)strlen(t) * 6 * sc; }
void ui_text(float x, float y, int sc, const char* text, float r, float g, float b) {
    std::vector<float> v; float pen = 0;
    for (const char* q = text; *q; q++) {
        char ch = (char)toupper((unsigned char)*q); const char* f = strchr(GLYPH_CHARS, ch);
        if (f && ch) { int gi = (int)(f - GLYPH_CHARS); quad(v, x + pen, y, x + pen + 5 * sc, y + 7 * sc, (gi * 6) / (float)(NGLYPH * 6), (gi * 6 + 5) / (float)(NGLYPH * 6)); }
        pen += 6 * sc;
    }
    std::vector<float> sh = v; for (size_t i = 0; i < sh.size(); i += 4) { sh[i] += sc; sh[i + 1] += sc; }
    ui_draw(sh, 0, 0, 0, 1); ui_draw(v, r, g, b, 1);
}

