// Interprete del FIFO de GX: comandos de CP, XF y BP y dibujo de primitivas con formato de vertice.
#include "gx.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <cstdlib>

GxState g_gx;

u8* g_gxf_buf = nullptr; size_t g_gxf_end = 0, g_gxf_cap = 0, g_gxf_trig = 1;   // flujo pendiente (se rellena desde GXF32, native_lift.h)
static size_t g_pos = 0, g_need = 0;       // g_pos: inicio del comando en curso; g_need: bytes que necesita el comando en curso (evita reanalizarlo en cada escritura)
void gx_fifo_grow() { g_gxf_cap = g_gxf_cap ? g_gxf_cap * 2 : (1u << 20); g_gxf_buf = (u8*)realloc(g_gxf_buf, g_gxf_cap); }
static u64 g_hist[256];                    // histograma de comandos (diagnostico)

static inline u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static inline u16 be16(const u8* p) { return (u16)(((u32)p[0] << 8) | p[1]); }
static inline float bef(const u8* p) { u32 v = be32(p); float f; memcpy(&f, &v, 4); return f; }

// ---- descripcion del formato de vertice ----
struct AttrFmt { u32 vcd; u32 cnt; u32 fmt; u32 shift; };       // vcd: 0 ninguno, 1 directo, 2 indice8, 3 indice16

static u32 comp_size(u32 fmt) { switch (fmt) { case 0: case 1: return 1; case 2: case 3: return 2; default: return 4; } }

static u32 color_size(u32 fmt) { switch (fmt) { case 0: return 2; case 1: return 3; case 2: return 4; case 3: return 2; case 4: return 3; default: return 4; } }

struct VtxFmt {
    AttrFmt pmidx, tmidx[8], pos, nrm, col[2], tex[8];
    u32 nrmIndex3 = 0;
};

static VtxFmt make_fmt(u32 vat) {
    VtxFmt f{};
    u32 lo = g_gx.vcdLo, hi = g_gx.vcdHi, a = g_gx.vatA[vat], b = g_gx.vatB[vat], c = g_gx.vatC[vat];
    f.pmidx.vcd = lo & 1;
    for (int i = 0; i < 8; i++) f.tmidx[i].vcd = (lo >> (1 + i)) & 1;
    f.pos.vcd = (lo >> 9) & 3; f.nrm.vcd = (lo >> 11) & 3; f.col[0].vcd = (lo >> 13) & 3; f.col[1].vcd = (lo >> 15) & 3;
    for (int i = 0; i < 8; i++) f.tex[i].vcd = (hi >> (2 * i)) & 3;
    f.pos.cnt = a & 1; f.pos.fmt = (a >> 1) & 7; f.pos.shift = (a >> 4) & 31;
    f.nrm.cnt = (a >> 9) & 1; f.nrm.fmt = (a >> 10) & 7;
    f.col[0].cnt = (a >> 13) & 1; f.col[0].fmt = (a >> 14) & 7;
    f.col[1].cnt = (a >> 17) & 1; f.col[1].fmt = (a >> 18) & 7;
    f.tex[0].cnt = (a >> 21) & 1; f.tex[0].fmt = (a >> 22) & 7; f.tex[0].shift = (a >> 25) & 31;
    f.nrmIndex3 = (a >> 31) & 1;
    f.tex[1].cnt = b & 1; f.tex[1].fmt = (b >> 1) & 7; f.tex[1].shift = (b >> 4) & 31;
    f.tex[2].cnt = (b >> 9) & 1; f.tex[2].fmt = (b >> 10) & 7; f.tex[2].shift = (b >> 13) & 31;
    f.tex[3].cnt = (b >> 18) & 1; f.tex[3].fmt = (b >> 19) & 7; f.tex[3].shift = (b >> 22) & 31;
    f.tex[4].cnt = (b >> 27) & 1; f.tex[4].fmt = (b >> 28) & 7; f.tex[4].shift = c & 31;
    f.tex[5].cnt = (c >> 5) & 1; f.tex[5].fmt = (c >> 6) & 7; f.tex[5].shift = (c >> 9) & 31;
    f.tex[6].cnt = (c >> 14) & 1; f.tex[6].fmt = (c >> 15) & 7; f.tex[6].shift = (c >> 18) & 31;
    f.tex[7].cnt = (c >> 23) & 1; f.tex[7].fmt = (c >> 24) & 7; f.tex[7].shift = (c >> 27) & 31;
    return f;
}

static u32 attr_bytes(const AttrFmt& a, int kind) {
    if (a.vcd == 0) return 0;
    if (a.vcd == 2) return 1;
    if (a.vcd == 3) return 2;
    switch (kind) {
    case 0: return (a.cnt ? 3 : 2) * comp_size(a.fmt);                    // posicion
    case 1: return 3 * comp_size(a.fmt);                                  // normal (solo 3 componentes aqui)
    case 2: return color_size(a.fmt);                                     // color
    default: return (a.cnt ? 2 : 1) * comp_size(a.fmt);                   // texcoord
    }
}

static u32 vertex_size(const VtxFmt& f) {
    u32 n = f.pmidx.vcd;
    for (int i = 0; i < 8; i++) n += f.tmidx[i].vcd;
    n += attr_bytes(f.pos, 0) + attr_bytes(f.nrm, 1);
    for (int i = 0; i < 2; i++) n += attr_bytes(f.col[i], 2);
    for (int i = 0; i < 8; i++) n += attr_bytes(f.tex[i], 3);
    return n;
}

static float read_comp(const u8* p, u32 fmt, u32 shift) {
    float scale = 1.0f / (float)(1u << shift);
    switch (fmt) {
    case 0: return (float)p[0] * scale;
    case 1: return (float)(s8)p[0] * scale;
    case 2: return (float)be16(p) * scale;
    case 3: return (float)(s16)be16(p) * scale;
    default: return bef(p);
    }
}

static void read_color(const u8* p, u32 fmt, float out[4]) {
    switch (fmt) {
    case 0: { u32 v = be16(p); out[0] = ((v >> 11) & 31) / 31.f; out[1] = ((v >> 5) & 63) / 63.f; out[2] = (v & 31) / 31.f; out[3] = 1; break; }
    case 1: case 2: out[0] = p[0] / 255.f; out[1] = p[1] / 255.f; out[2] = p[2] / 255.f; out[3] = 1; break;
    case 3: { u32 v = be16(p); out[0] = ((v >> 12) & 15) / 15.f; out[1] = ((v >> 8) & 15) / 15.f; out[2] = ((v >> 4) & 15) / 15.f; out[3] = (v & 15) / 15.f; break; }
    case 4: { u32 v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2]; out[0] = ((v >> 18) & 63) / 63.f; out[1] = ((v >> 12) & 63) / 63.f; out[2] = ((v >> 6) & 63) / 63.f; out[3] = (v & 63) / 63.f; break; }
    default: out[0] = p[0] / 255.f; out[1] = p[1] / 255.f; out[2] = p[2] / 255.f; out[3] = p[3] / 255.f; break;
    }
}

// Devuelve puntero a los datos del atributo (directo en el flujo o indirecto por indice en un array del invitado).
static const u8* attr_ptr(const AttrFmt& a, int array, const u8*& cur, u32 direct_bytes) {
    if (a.vcd == 1) { const u8* p = cur; cur += direct_bytes; return p; }
    u32 idx = a.vcd == 2 ? *cur : be16(cur);
    cur += a.vcd == 2 ? 1 : 2;
    return HP(g_gx.arrayBase[array] & 0x1FFFFFFF ? (g_gx.arrayBase[array] | 0x80000000u) + idx * g_gx.arrayStride[array] : 0);
}

static void decode_vertex(const VtxFmt& f, const u8*& cur, GxVertex& v) {
    memset(&v, 0, sizeof v);
    v.col[0][3] = v.col[1][3] = 1.0f;
    if (f.pmidx.vcd) v.posMtxIdx = *cur++;
    for (int i = 0; i < 8; i++) if (f.tmidx[i].vcd) v.texMtxIdx[i] = *cur++;
    if (f.pos.vcd) {
        u32 n = f.pos.cnt ? 3 : 2, cs = comp_size(f.pos.fmt);
        const u8* p = attr_ptr(f.pos, 0, cur, n * cs);
        for (u32 i = 0; i < n; i++) v.pos[i] = read_comp(p + i * cs, f.pos.fmt, f.pos.shift);
    }
    if (f.nrm.vcd) {
        u32 cs = comp_size(f.nrm.fmt);
        const u8* p = attr_ptr(f.nrm, 1, cur, 3 * cs);
        for (u32 i = 0; i < 3; i++) v.nrm[i] = read_comp(p + i * cs, f.nrm.fmt, f.nrm.fmt == 1 ? 6 : f.nrm.fmt == 3 ? 14 : 0);
    }
    for (int c = 0; c < 2; c++) if (f.col[c].vcd) {
        const u8* p = attr_ptr(f.col[c], 2 + c, cur, color_size(f.col[c].fmt));
        read_color(p, f.col[c].fmt, v.col[c]);
    }
    for (int t = 0; t < 8; t++) if (f.tex[t].vcd) {
        u32 n = f.tex[t].cnt ? 2 : 1, cs = comp_size(f.tex[t].fmt);
        const u8* p = attr_ptr(f.tex[t], 4 + t, cur, n * cs);
        for (u32 i = 0; i < n; i++) v.tex[t][i] = read_comp(p + i * cs, f.tex[t].fmt, f.tex[t].shift);
    }
}

// ---- interprete de comandos ----
static void cp_write(u32 reg, u32 val) {
    if (reg == 0x50) g_gx.vcdLo = val;
    else if (reg == 0x60) g_gx.vcdHi = val;
    else if (reg >= 0x70 && reg < 0x78) g_gx.vatA[reg - 0x70] = val;
    else if (reg >= 0x80 && reg < 0x88) g_gx.vatB[reg - 0x80] = val;
    else if (reg >= 0x90 && reg < 0x98) g_gx.vatC[reg - 0x90] = val;
    else if (reg >= 0xA0 && reg < 0xB0) g_gx.arrayBase[reg - 0xA0] = val;
    else if (reg >= 0xB0 && reg < 0xC0) g_gx.arrayStride[reg - 0xB0] = val & 0xFF;
}

static bool process_one() {
    const u8* p = g_gxf_buf + g_pos;
    size_t n = g_gxf_end - g_pos;
    if (n == 0) return false;
    u8 op = p[0];
    g_hist[op >= 0x80 ? (op & 0xF8) : op]++;
    if (op == 0x00 || op == 0x44 || op == 0x48 || op == 0x45) { g_pos += 1; return true; }
    if (op == 0x08) { if (n < 6) return false; cp_write(p[1], be32(p + 2)); g_pos += 6; return true; }
    if (op == 0x10) {
        if (n < 5) return false;
        u32 h = be32(p + 1); u32 cnt = (h >> 16) + 1, addr = h & 0xFFFF;
        if (n < 5 + 4 * cnt) { g_need = 5 + 4 * cnt; return false; }
        for (u32 i = 0; i < cnt; i++) {
            u32 v = be32(p + 5 + 4 * i), a = addr + i;
            if (a < 0x1000) { float f; memcpy(&f, &v, 4); g_gx.xfMem[a] = f; }
            else if (a < 0x1060) g_gx.xfReg[a - 0x1000] = v;
            gx_xf_write(a, v);
        }
        g_pos += 5 + 4 * cnt; return true;
    }
    if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) { if (n < 5) return false; g_pos += 5; return true; }   // carga indexada de XF (no usada aun)
    if (op == 0x40) {                                    // CALL_DL: lista de visualizacion en memoria del invitado
        if (n < 9) return false;
        u32 addr = be32(p + 1), size = be32(p + 5);
        g_pos += 9;
        while (g_gxf_end + size > g_gxf_cap) gx_fifo_grow();
        memmove(g_gxf_buf + g_pos + size, g_gxf_buf + g_pos, g_gxf_end - g_pos); memcpy(g_gxf_buf + g_pos, HP(addr), size); g_gxf_end += size;
        return true;
    }
    if (op == 0x61) {
        if (n < 5) return false;
        u32 w = be32(p + 1); u32 reg = w >> 24;
        g_gx.bp[reg] = w & 0xFFFFFF;
        g_pos += 5;
        gx_bp_write(reg, w & 0xFFFFFF);
        return true;
    }
    if (op >= 0x80) {
        if (n < 3) return false;
        u32 prim = (op >> 3) & 7, vat = op & 7, cnt = be16(p + 1);
        static int dbg = 0;
        if (dbg < 8 && dbg++ >= 0) { fprintf(stderr, "[gx] draw op=%02X prim=%u vat=%u cnt=%u vcd=%08X/%08X vatA=%08X bytes:", op, prim, vat, cnt, g_gx.vcdLo, g_gx.vcdHi, g_gx.vatA[vat]); for (int i = 0; i < 16 && (size_t)i < n; i++) fprintf(stderr, " %02X", p[i]); fputc('\n', stderr); }
        VtxFmt f = make_fmt(vat);
        u32 vs = vertex_size(f);
        static const char* e_raw = getenv("RT_RAW");
        if (const char* e = e_raw) { static long lo = -1, hi = 0, nd = 0; if (lo < 0) sscanf(e, "%ld,%ld", &lo, &hi);
            if (nd >= lo && nd < hi) { fprintf(stderr, "[raw] draw %ld op=%02X cnt=%u vs=%u:", nd, op, cnt, vs); for (u32 i = 0; i < vs * 2 && 3 + i < n; i++) fprintf(stderr, " %02X", p[3 + i]); fputc('\n', stderr); } nd++; }
        if (n < 3 + (size_t)cnt * vs) { g_need = 3 + (size_t)cnt * vs; return false; }
        static std::vector<GxVertex> verts; verts.resize(cnt);
        const u8* cur = p + 3;
        for (u32 i = 0; i < cnt; i++) decode_vertex(f, cur, verts[i]);
        gx_draw(prim, vat, verts.data(), cnt);
        g_pos += 3 + (size_t)cnt * vs;
        return true;
    }
    fprintf(stderr, "[gx] comando desconocido 0x%02X (resto %zu bytes)\n", op, n);
    g_pos += 1;
    return true;
}

void gx_fifo_parse() {
    g_need = 0;
    while (process_one()) { g_need = 0; }
    if (g_pos == g_gxf_end) { g_gxf_end = 0; g_pos = 0; }
    else if (g_pos > (1u << 20)) { memmove(g_gxf_buf, g_gxf_buf + g_pos, g_gxf_end - g_pos); g_gxf_end -= g_pos; g_pos = 0; }
    g_gxf_trig = g_pos + (g_need ? g_need : 1);
}

void gx_fifo_write(const u8* data, u32 n) {
    while (g_gxf_end + n > g_gxf_cap) gx_fifo_grow();
    memcpy(g_gxf_buf + g_gxf_end, data, n); g_gxf_end += n;
    if (g_gxf_end >= g_gxf_trig) gx_fifo_parse();
}

void gx_stats_print() {
    fprintf(stderr, "[gx] histograma de comandos:");
    for (int i = 0; i < 256; i++) if (g_hist[i]) fprintf(stderr, " %02X:%llu", i, (unsigned long long)g_hist[i]);
    fputc('\n', stderr);
}
