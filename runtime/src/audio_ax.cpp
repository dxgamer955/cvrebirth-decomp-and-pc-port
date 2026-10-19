// Microcodigo AX del DSP emulado por software (sin cargar el codigo real del DSP).
//
// Protocolo (el que habla la biblioteca AX del juego, ver fn_801AEAA0 = __AXNextFrame y fn_801AE000 = lista de comandos):
//   - arranque de la tarea: la CPU envia la imagen de IRAM/DRAM por correos 0x80F3xxxx y el vector de inicio
//     (0x80F3D001, vector); el DSP responde 0xDCD10000 ("tarea iniciada" -> init_cb).
//   - cada fotograma de 3 ms (96 muestras a 32 kHz): la CPU envia 0xBABExxxx (tamano de la lista) y la direccion de la
//     lista de comandos; el DSP la ejecuta (mezcla las voces, aux, salida) y responde 0xDCD10001 (-> res_cb).
// La lista de comandos son palabras de 16 bits: 0 SETUP(u32), 1/2/3 DL(u32), 4 PB(u32: cabeza de la lista de voces),
// 5/6/7 AUXA/B/C (vol, u32 salida, u32 retorno), 8/9 (modo DPL2, aux con 6 punteros), 0xA compresor, 0xB/0xC salida
// (vol, u32 envolvente S, u32 buffer LR), 0xD salida de altavoces de WPAD, 0xE fin.
// Cada voz es un AXPB de 0x140 bytes (bloque de parametros que mantiene la CPU): ver los desplazamientos abajo.
#include "hle.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>

static const int NS = 96;                                  // muestras por fotograma
static int g_log_cmds = -1;

// ---- estado de la tarea ----
static bool g_booted = false, g_expect_param = false;
static u32 g_last_cmd = 0;
static bool g_expect_cl = false;
static u32 g_cl_size = 0;

// ---- buses ----
static s32 g_main[3][NS];                                  // L, R, S
static s32 g_aux[3][3][NS];                                // A/B/C x L/R/S

static inline s32 clamp16(s32 v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline u32 mem_ok(u32 a, u32 n) { return (a & 0x1FFFFFFF) + n <= 0x14000000; }
static inline u32 RD16(u32 a) { return LD16(a); }
static inline u32 RD32(u32 a) { return LD32(a); }

// ---- AXPB: desplazamientos (hex) ----
enum {
    PB_NEXT = 0x00, PB_MIXCTRL = 0x0C, PB_STATE = 0x10, PB_TYPE = 0x12, PB_MIX = 0x14,
    PB_VE_VOL = 0x6A, PB_VE_DELTA = 0x6C,
    PB_LOOPFLAG = 0x6E, PB_FORMAT = 0x70, PB_LOOPADDR = 0x72, PB_ENDADDR = 0x76, PB_CURADDR = 0x7A,
    PB_ADPCM_COEF = 0x7E, PB_ADPCM_GAIN = 0x9E, PB_ADPCM_PS = 0xA0, PB_ADPCM_YN1 = 0xA2, PB_ADPCM_YN2 = 0xA4,
    PB_SRC_RATIO = 0xA6, PB_SRC_FRAC = 0xAA, PB_SRC_HIST = 0xAC,
    PB_LOOP_PS = 0xB4, PB_LOOP_YN1 = 0xB6, PB_LOOP_YN2 = 0xB8,
    PB_LPF_ON = 0xBA, PB_LPF_YN1 = 0xBC, PB_LPF_A0 = 0xBE, PB_LPF_B0 = 0xC0,
};
enum { FMT_ADPCM = 0, FMT_PCM16 = 10, FMT_PCM8 = 25 };

struct Voice {
    u32 cur, loop, end; bool loopflag; int fmt;
    s16 coef[16]; int gain, ps, yn1, yn2, loop_ps, loop_yn1, loop_yn2;
    bool running;
};

// Siguiente muestra del origen (decodificada). Devuelve false si la voz ya termino.
static inline bool next_sample(Voice& v, s32* out) {
    if (!v.running) { *out = 0; return false; }
    s32 s = 0;
    if (v.fmt == FMT_ADPCM) {
        if ((v.cur & 15) == 0) { v.ps = *HP(v.cur >> 1); v.cur += 2; }           // cabecera de marco (8 bytes = 14 muestras)
        u32 ba = v.cur >> 1;
        int n = (v.cur & 1) ? (*HP(ba) & 15) : (*HP(ba) >> 4);
        n = (n ^ 8) - 8;
        int idx = (v.ps >> 4) & 7;
        s64 acc = ((s64)(n << (v.ps & 15)) << 11) + 1024 + (s64)v.coef[idx * 2] * v.yn1 + (s64)v.coef[idx * 2 + 1] * v.yn2;
        s = clamp16((s32)(acc >> 11));
        v.yn2 = v.yn1; v.yn1 = s;
    } else if (v.fmt == FMT_PCM16) {
        const u8* p = HP(v.cur * 2); s = (s16)((p[0] << 8) | p[1]);
        s = (s * (v.gain ? v.gain : 0x800)) >> 11; s = clamp16(s);
    } else if (v.fmt == FMT_PCM8) {
        s = (s32)(s8)*HP(v.cur);
        s = clamp16(s * (v.gain ? v.gain : 0x100));
    }
    if (v.cur == v.end) {
        if (v.loopflag) { v.cur = v.loop; v.ps = v.loop_ps; v.yn1 = v.loop_yn1; v.yn2 = v.loop_yn2; }
        else v.running = false;
    } else v.cur++;
    *out = s;
    return true;
}

static int g_active_voices = 0;
static bool g_seen[256];

static void process_voice(u32 pb) {
    if (!RD16(pb + PB_STATE)) { g_seen[((pb & 0x1FFFFFFF) >> 6) & 0xFF] = false; return; }
    g_active_voices++;
    Voice v;
    v.fmt = RD16(pb + PB_FORMAT); v.loopflag = RD16(pb + PB_LOOPFLAG) != 0;
    v.loop = RD32(pb + PB_LOOPADDR); v.end = RD32(pb + PB_ENDADDR); v.cur = RD32(pb + PB_CURADDR);
    for (int i = 0; i < 16; i++) v.coef[i] = (s16)RD16(pb + PB_ADPCM_COEF + 2 * i);
    v.gain = RD16(pb + PB_ADPCM_GAIN); v.ps = RD16(pb + PB_ADPCM_PS); v.yn1 = (s16)RD16(pb + PB_ADPCM_YN1); v.yn2 = (s16)RD16(pb + PB_ADPCM_YN2);
    v.loop_ps = RD16(pb + PB_LOOP_PS); v.loop_yn1 = (s16)RD16(pb + PB_LOOP_YN1); v.loop_yn2 = (s16)RD16(pb + PB_LOOP_YN2);
    v.running = true;
    if (g_log_cmds > 0) {
        u32 idx = ((pb & 0x1FFFFFFF) >> 6) & 0xFF;
        if (!g_seen[idx]) {
            g_seen[idx] = true;
            rt_log("[ax] voz %08X: fmt=%d tipo=%d ctrl=%08X cur=%08X loop=%08X end=%08X lf=%d ratio=%08X ve=%04X/%04X mix L=%04X/%04X R=%04X/%04X S=%04X ps=%02X gain=%04X\n", pb, v.fmt, RD16(pb + PB_TYPE), RD32(pb + PB_MIXCTRL), v.cur, v.loop, v.end, v.loopflag,
                   (RD16(pb + PB_SRC_RATIO) << 16) | RD16(pb + PB_SRC_RATIO + 2), RD16(pb + PB_VE_VOL), RD16(pb + PB_VE_DELTA), RD16(pb + 0x14), RD16(pb + 0x16), RD16(pb + 0x18), RD16(pb + 0x1A), RD16(pb + 0x34), v.ps, v.gain);
        }
    }
    // direcciones: fisicas, en muestras (ADPCM: nibbles; PCM16: palabras; PCM8: bytes); comprobar que apuntan a memoria
    u32 bytes_end = v.fmt == FMT_ADPCM ? (v.end >> 1) : (v.fmt == FMT_PCM16 ? v.end * 2 : v.end);
    u32 bytes_cur = v.fmt == FMT_ADPCM ? (v.cur >> 1) : (v.fmt == FMT_PCM16 ? v.cur * 2 : v.cur);
    u32 bytes_loop = v.fmt == FMT_ADPCM ? (v.loop >> 1) : (v.fmt == FMT_PCM16 ? v.loop * 2 : v.loop);
    if (!mem_ok(bytes_end, 2) || !mem_ok(bytes_cur, 2) || !mem_ok(bytes_loop, 2) || (v.fmt != FMT_ADPCM && v.fmt != FMT_PCM16 && v.fmt != FMT_PCM8)) {
        static int warned = 0;
        if (warned++ < 8) rt_log("[ax] voz %08X con direcciones invalidas: fmt=%d cur=%08X loop=%08X end=%08X\n", pb, v.fmt, v.cur, v.loop, v.end);
        ST16(pb + PB_STATE, 0);
        return;
    }
    u32 ratio = (RD16(pb + PB_SRC_RATIO) << 16) | RD16(pb + PB_SRC_RATIO + 2);
    u32 frac = RD16(pb + PB_SRC_FRAC);
    s32 hist[4]; for (int i = 0; i < 4; i++) hist[i] = (s16)RD16(pb + PB_SRC_HIST + 2 * i);
    int ve = RD16(pb + PB_VE_VOL), ved = (s16)RD16(pb + PB_VE_DELTA);
    u32 ctrl = RD32(pb + PB_MIXCTRL);
    // volumenes de la mezcla: [canal] (valor, delta); bits de control (habilitar, rampa)
    // mixerCtrl (Wii): grupos de 5 bits {L=1, R=2, S=4, rampa L/R=8, rampa S=16}: principal en el bit 0, aux A/B/C en 14/19/24
    // (la tabla de costes de __AXSyncPBs, fn_801AFC70, indexa esos cuatro campos con la misma tabla).
    struct Dst { s32* buf; int vol_off; int grp; int ch; } dsts[12] = {
        { g_main[0], 0x14 + 0x00, 0, 0 }, { g_main[1], 0x14 + 0x04, 0, 1 }, { g_main[2], 0x14 + 0x20, 0, 2 },
        { g_aux[0][0], 0x14 + 0x08, 14, 0 }, { g_aux[0][1], 0x14 + 0x0C, 14, 1 }, { g_aux[0][2], 0x14 + 0x24, 14, 2 },
        { g_aux[1][0], 0x14 + 0x10, 19, 0 }, { g_aux[1][1], 0x14 + 0x14, 19, 1 }, { g_aux[1][2], 0x14 + 0x28, 19, 2 },
        { g_aux[2][0], 0x14 + 0x18, 24, 0 }, { g_aux[2][1], 0x14 + 0x1C, 24, 1 }, { g_aux[2][2], 0x14 + 0x2C, 24, 2 },
    };
    int act[12], nact = 0; s32 mv[12], md[12]; bool ramp[12];
    for (int d = 0; d < 12; d++) {
        if (!((ctrl >> (dsts[d].grp + dsts[d].ch)) & 1)) continue;
        act[nact] = d; mv[nact] = RD16(pb + dsts[d].vol_off); md[nact] = (s16)RD16(pb + dsts[d].vol_off + 2);
        ramp[nact] = (ctrl >> (dsts[d].grp + (dsts[d].ch == 2 ? 4 : 3))) & 1; nact++;
    }
    bool lpf = RD16(pb + PB_LPF_ON) != 0; s32 lpf_y = (s16)RD16(pb + PB_LPF_YN1), lpf_a0 = RD16(pb + PB_LPF_A0), lpf_b0 = RD16(pb + PB_LPF_B0);
    for (int i = 0; i < NS; i++) {
        s32 s = hist[2] + (s32)(((s64)(hist[3] - hist[2]) * frac) >> 16);              // SRC lineal
        frac += ratio;
        while (frac >= 0x10000) {
            frac -= 0x10000;
            s32 ns; next_sample(v, &ns);
            hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3]; hist[3] = ns;
        }
        if (lpf) { lpf_y = (lpf_a0 * s + lpf_b0 * lpf_y) >> 15; s = lpf_y; }
        s = (s * ve) >> 15;
        ve += ved; if (ve < 0) ve = 0; if (ve > 0x8000) ve = 0x8000;
        for (int k = 0; k < nact; k++) {
            dsts[act[k]].buf[i] += (s * mv[k]) >> 15;
            if (ramp[k]) { mv[k] += md[k]; if (mv[k] < 0) mv[k] = 0; if (mv[k] > 0x8000) mv[k] = 0x8000; }
        }
    }
    // escribir de vuelta el estado que cambia el DSP
    ST32(pb + PB_CURADDR, v.cur);
    ST16(pb + PB_SRC_FRAC, frac);
    for (int i = 0; i < 4; i++) ST16(pb + PB_SRC_HIST + 2 * i, (u32)hist[i] & 0xFFFF);
    ST16(pb + PB_ADPCM_PS, v.ps); ST16(pb + PB_ADPCM_YN1, (u32)v.yn1 & 0xFFFF); ST16(pb + PB_ADPCM_YN2, (u32)v.yn2 & 0xFFFF);
    ST16(pb + PB_VE_VOL, ve);
    if (lpf) ST16(pb + PB_LPF_YN1, (u32)lpf_y & 0xFFFF);
    for (int k = 0; k < nact; k++) if (ramp[k]) ST16(pb + dsts[act[k]].vol_off, mv[k]);
    if (!v.running) ST16(pb + PB_STATE, 0);
}

static void process_voices(u32 head) {
    memset(g_main, 0, sizeof g_main); memset(g_aux, 0, sizeof g_aux);
    g_active_voices = 0;
    u32 pb = head;
    for (int guard = 0; pb && guard < 128; guard++) {
        process_voice(pb);
        pb = (RD16(pb + PB_NEXT) << 16) | RD16(pb + PB_NEXT + 2);
    }
}

static void put_s32_buf(u32 addr, const s32* L, const s32* R, const s32* S) {
    if (!addr || !mem_ok(addr, NS * 12)) return;
    for (int i = 0; i < NS; i++) { ST32(addr + 4 * i, (u32)L[i]); ST32(addr + NS * 4 + 4 * i, (u32)R[i]); ST32(addr + NS * 8 + 4 * i, (u32)S[i]); }
}
static void add_s32_buf(u32 addr, int vol) {
    if (!addr || !mem_ok(addr, NS * 12)) return;
    for (int i = 0; i < NS; i++) {
        g_main[0][i] += (s32)(((s64)(s32)LD32(addr + 4 * i) * vol) >> 15);
        g_main[1][i] += (s32)(((s64)(s32)LD32(addr + NS * 4 + 4 * i) * vol) >> 15);
        g_main[2][i] += (s32)(((s64)(s32)LD32(addr + NS * 8 + 4 * i) * vol) >> 15);
    }
}

static void exec_cmdlist(u32 cl) {
    static const bool skip = getenv("RT_AX_SKIP") != nullptr; if (skip) return;
    if (g_log_cmds < 0) g_log_cmds = getenv("RT_AUDIO_LOG") ? 1 : 0;
    u32 pos = cl;
    auto W = [&]() { u32 v = LD16(pos); pos += 2; return v; };
    auto W32 = [&]() { u32 hi = W(); u32 lo = W(); return (hi << 16) | lo; };
    for (int guard = 0; guard < 64; guard++) {
        u32 cmd = W();
        if (g_log_cmds > 0 && cmd != 0xE && g_log_cmds < 30) { g_log_cmds++; rt_log("[ax] cmd %X\n", cmd); }
        switch (cmd) {
        case 0: W32(); break;                                              // SETUP: bloque de parametros del estudio (rampas de retorno)
        case 1: case 2: case 3: W32(); break;                              // DL de buses LRS (siempre ceros)
        case 4: process_voices(W32()); break;
        case 5: case 6: case 7: {
            int a = cmd - 5; int vol = W(); u32 out = W32(), in = W32();
            put_s32_buf(out, g_aux[a][0], g_aux[a][1], g_aux[a][2]);
            add_s32_buf(in, vol);
            break;
        }
        case 8: case 9: {                                                  // modo DPL2: aux con 6 punteros (se usan salida L y retorno)
            int a = cmd - 8; int vol = W(); u32 p[6]; for (int i = 0; i < 6; i++) p[i] = W32();
            put_s32_buf(p[0], g_aux[a][0], g_aux[a][1], g_aux[a][2]);
            add_s32_buf(p[2], vol);
            break;
        }
        case 0xA: W(); W(); W32(); break;                                  // compresor (tabla): sin efecto, el recorte lo hace la salida
        case 0xB: case 0xC: {
            int vol = W(); u32 sur = W32(), lr = W32();
            if (sur && mem_ok(sur, NS * 4)) for (int i = 0; i < NS; i++) ST32(sur + 4 * i, (u32)g_main[2][i]);
            if (lr && mem_ok(lr, NS * 4)) for (int i = 0; i < NS; i++) {
                s32 l = clamp16((s32)(((s64)g_main[0][i] * vol) >> 15)), r = clamp16((s32)(((s64)g_main[1][i] * vol) >> 15));
                ST16(lr + 4 * i, (u32)l & 0xFFFF); ST16(lr + 4 * i + 2, (u32)r & 0xFFFF);
            }
            break;
        }
        case 0xD: for (int i = 0; i < 8; i++) W(); break;                  // salida de los altavoces de los mandos (no emulados)
        case 0xE: if (g_log_cmds > 0) { static int fr = 0; if (++fr % 333 == 0) rt_log("[ax] fotograma %d: %d voces activas\n", fr, g_active_voices); } return;
        default: rt_log("[ax] comando de lista desconocido 0x%X en %08X\n", cmd, pos - 2); return;
        }
    }
}

void dsp_mail_from_cpu(u32 mail) {
    if (g_expect_cl) {                                                     // segundo correo del fotograma: direccion de la lista
        g_expect_cl = false;
        exec_cmdlist(mail);
        dsp_post_mail(0xDCD10001, true);                                   // "reanudar": fotograma terminado
        return;
    }
    if (!g_booted) {                                                       // arranque: pares (orden, parametro)
        if (g_expect_param) {
            g_expect_param = false;
            if (g_last_cmd == 0x80F3D001) {                                // vector de inicio: la tarea corre
                g_booted = true;
                dsp_post_mail(0xDCD10000, true);
            }
        } else if ((mail >> 16) == 0x80F3) { g_last_cmd = mail; g_expect_param = true; }
        return;
    }
    if ((mail >> 16) == 0xBABE) { g_cl_size = mail & 0xFFFF; g_expect_cl = true; return; }
    static int warned = 0;
    if (warned++ < 8) rt_log("[dsp] correo desconocido 0x%08X\n", mail);
}

void dsp_reset_ucode() { g_booted = false; g_expect_param = false; g_expect_cl = false; }
