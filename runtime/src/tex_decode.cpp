// Decodificacion de texturas de GX (I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/C4/C8/C14X2/CMPR) a RGBA8.
#include "tex_decode.h"
#include "hle.h"
#include <cstring>

std::map<u32, std::vector<u16>> g_tlut;               // clave = campo tmem de LOADTLUT1 / SETTLUT

static void put(std::vector<u8>& out, int w, int x, int y, u8 r, u8 g, u8 b, u8 a) {
    u8* p = &out[((size_t)y * w + x) * 4]; p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}
static inline u16 rd16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }
static inline u8 e3(u32 v) { return (u8)((v << 5) | (v << 2) | (v >> 1)); }
static inline u8 e4(u32 v) { return (u8)(v * 17); }
static inline u8 e5(u32 v) { return (u8)((v << 3) | (v >> 2)); }
static inline u8 e6(u32 v) { return (u8)((v << 2) | (v >> 4)); }

static void rgb565(u16 v, u8& r, u8& g, u8& b) { r = e5(v >> 11); g = e6((v >> 5) & 63); b = e5(v & 31); }
static void rgb5a3(u16 v, u8& r, u8& g, u8& b, u8& a) {
    if (v & 0x8000) { r = e5((v >> 10) & 31); g = e5((v >> 5) & 31); b = e5(v & 31); a = 255; }
    else { r = e4((v >> 8) & 15); g = e4((v >> 4) & 15); b = e4(v & 15); a = e3((v >> 12) & 7); }
}

static void pal_color(u16 entry, u32 tlutFmt, u8& r, u8& g, u8& b, u8& a) {
    if (tlutFmt == 0) { a = entry >> 8; r = g = b = entry & 255; }                    // IA8
    else if (tlutFmt == 1) { rgb565(entry, r, g, b); a = 255; }
    else rgb5a3(entry, r, g, b, a);
}

bool decode_texture(u32 addr, u32 fmt, int w, int h, u32 tlutKey, u32 tlutFmt, std::vector<u8>& out) {
    out.assign((size_t)w * h * 4, 255);
    const u8* src = HP(addr);
    auto pal = [&](u32 idx, u8& r, u8& g, u8& b, u8& a) {
        auto it = g_tlut.find(tlutKey); u16 e = 0;
        if (it != g_tlut.end() && idx < it->second.size()) e = it->second[idx];
        pal_color(e, tlutFmt, r, g, b, a);
    };
    switch (fmt) {
    case 0:  // I4 (tiles 8x8)
        for (int ty = 0; ty < h; ty += 8) for (int tx = 0; tx < w; tx += 8) { for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x += 2) {
            u8 v = *src++; if (tx + x < w && ty + y < h) { u8 a = e4(v >> 4), b = e4(v & 15); put(out, w, tx + x, ty + y, a, a, a, a); if (tx + x + 1 < w) put(out, w, tx + x + 1, ty + y, b, b, b, b); } } }
        break;
    case 1:  // I8 (8x4)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 8) for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) {
            u8 v = *src++; if (tx + x < w && ty + y < h) put(out, w, tx + x, ty + y, v, v, v, v); }
        break;
    case 2:  // IA4 (8x4)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 8) for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) {
            u8 v = *src++; if (tx + x < w && ty + y < h) { u8 i = e4(v & 15), a = e4(v >> 4); put(out, w, tx + x, ty + y, i, i, i, a); } }
        break;
    case 3:  // IA8 (4x4)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            u8 a = src[0], i = src[1]; src += 2; if (tx + x < w && ty + y < h) put(out, w, tx + x, ty + y, i, i, i, a); }
        break;
    case 4:  // RGB565
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            u8 r, g, b; rgb565(rd16(src), r, g, b); src += 2; if (tx + x < w && ty + y < h) put(out, w, tx + x, ty + y, r, g, b, 255); }
        break;
    case 5:  // RGB5A3
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            u8 r, g, b, a; rgb5a3(rd16(src), r, g, b, a); src += 2; if (tx + x < w && ty + y < h) put(out, w, tx + x, ty + y, r, g, b, a); }
        break;
    case 6:  // RGBA8: bloque 4x4 = 16 (A,R) + 16 (G,B)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) {
            for (int i = 0; i < 16; i++) { int x = i & 3, y = i >> 2; if (tx + x < w && ty + y < h) { u8* p = &out[(((size_t)(ty + y)) * w + tx + x) * 4]; p[3] = src[i * 2]; p[0] = src[i * 2 + 1]; } }
            for (int i = 0; i < 16; i++) { int x = i & 3, y = i >> 2; if (tx + x < w && ty + y < h) { u8* p = &out[(((size_t)(ty + y)) * w + tx + x) * 4]; p[1] = src[32 + i * 2]; p[2] = src[32 + i * 2 + 1]; } }
            src += 64; }
        break;
    case 8:  // C4 (8x8)
        for (int ty = 0; ty < h; ty += 8) for (int tx = 0; tx < w; tx += 8) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x += 2) {
            u8 v = *src++; u8 r, g, b, a;
            if (tx + x < w && ty + y < h) { pal(v >> 4, r, g, b, a); put(out, w, tx + x, ty + y, r, g, b, a); }
            if (tx + x + 1 < w && ty + y < h) { pal(v & 15, r, g, b, a); put(out, w, tx + x + 1, ty + y, r, g, b, a); } }
        break;
    case 9:  // C8 (8x4)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 8) for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) {
            u8 v = *src++, r, g, b, a; if (tx + x < w && ty + y < h) { pal(v, r, g, b, a); put(out, w, tx + x, ty + y, r, g, b, a); } }
        break;
    case 10: // C14X2 (4x4)
        for (int ty = 0; ty < h; ty += 4) for (int tx = 0; tx < w; tx += 4) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            u16 v = rd16(src) & 0x3FFF; src += 2; u8 r, g, b, a; if (tx + x < w && ty + y < h) { pal(v, r, g, b, a); put(out, w, tx + x, ty + y, r, g, b, a); } }
        break;
    case 14: // CMPR (DXT1 en bloques de 8x8 formados por 4 subbloques de 4x4)
        for (int ty = 0; ty < h; ty += 8) for (int tx = 0; tx < w; tx += 8) for (int sb = 0; sb < 4; sb++) {
            int bx = tx + (sb & 1) * 4, by = ty + (sb >> 1) * 4;
            u16 c0 = rd16(src), c1 = rd16(src + 2); u8 col[4][4];
            rgb565(c0, col[0][0], col[0][1], col[0][2]); col[0][3] = 255;
            rgb565(c1, col[1][0], col[1][1], col[1][2]); col[1][3] = 255;
            if (c0 > c1) { for (int k = 0; k < 3; k++) { col[2][k] = (2 * col[0][k] + col[1][k]) / 3; col[3][k] = (col[0][k] + 2 * col[1][k]) / 3; } col[2][3] = col[3][3] = 255; }
            else { for (int k = 0; k < 3; k++) { col[2][k] = (col[0][k] + col[1][k]) / 2; col[3][k] = 0; } col[2][3] = 255; col[3][3] = 0; }
            for (int y = 0; y < 4; y++) { u8 bits = src[4 + y]; for (int x = 0; x < 4; x++) { int idx = (bits >> (6 - 2 * x)) & 3;
                if (bx + x < w && by + y < h) put(out, w, bx + x, by + y, col[idx][0], col[idx][1], col[idx][2], col[idx][3]); } }
            src += 8; }
        break;
    default:
        return false;
    }
    return true;
}

