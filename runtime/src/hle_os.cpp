// Sustitutos de funciones del sistema operativo del SDK (OS): interrupciones, tiempo, mensajes.
#include "hle.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>

static u32 g_intr = 1;                     // 1 = interrupciones habilitadas
static u32 g_spr[1024];

u32 g_intr_enabled() { return g_intr; }
void rt_set_intr(u32 v) { g_intr = v; }
u32  RT_MFMSR() { return 0x00002032u | (g_intr ? 0x8000u : 0u); }       // FP, IR, DR y EE segun el estado
void RT_MTMSR(u32 v) { g_intr = (v >> 15) & 1; }
u32  RT_MFSPR(u32 spr) { return spr < 1024 ? g_spr[spr] : 0; }
void RT_MTSPR(u32 spr, u32 v) { if (spr < 1024) g_spr[spr] = v; }

HLE_FN(OSDisableInterrupts) { u32 old = g_intr; g_intr = 0; return RET_I(old); }
HLE_FN(OSEnableInterrupts)  { u32 old = g_intr; g_intr = 1; return RET_I(old); }
HLE_FN(OSRestoreInterrupts) { u32 old = g_intr; g_intr = a0 ? 1 : 0; return RET_I(old); }

// Reloj de la Wii: base de tiempo de 60.75 MHz (bus 243 MHz / 4).
static double g_skew_s = 0;                       // tiempo en pausa (ventana en segundo plano) que el juego no debe ver pasar
void rt_time_skip(double seconds) { g_skew_s += seconds; }
static u64 timebase() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    double s = duration<double>(steady_clock::now() - t0).count() - g_skew_s;
    return (u64)(s * 60750000.0);
}
u32 RT_MFTB(u32 tbr) { u64 t = timebase(); return tbr == 269 ? (u32)(t >> 32) : (u32)t; }
HLE_FN(OSGetTime) { return RET_L(timebase()); }
HLE_FN(OSGetTick) { return RET_I((u32)timebase()); }

// ---- formateo de printf con argumentos del invitado ----
// Los enteros vienen en r4.. (a1..), los floats en f1.. (fa0..); mas alla de los registros se leeria de la pila.
std::string rt_format(const char* fmt, const u32* ints, int nints, const f64* flts, int nflts, u32 stack_args) {
    std::string out; int ii = 0, fi = 0; u32 sp_off = 0;
    auto next_int = [&]() -> u32 { if (ii < nints) return ints[ii++]; u32 v = LD32(stack_args + 8 + sp_off); sp_off += 4; return v; };
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { out += *p; continue; }
        std::string spec = "%"; p++;
        while (*p && strchr("-+ #0123456789.*", *p)) { if (*p == '*') spec += std::to_string((int)next_int()); else spec += *p; p++; }
        bool l = false, ll = false, h = false;
        while (*p == 'l' || *p == 'h') { if (*p == 'l') { if (l) ll = true; l = true; } else h = true; p++; }
        char buf[512];
        switch (*p) {
        case 'd': case 'i': spec += "d"; snprintf(buf, sizeof buf, spec.c_str(), (int)next_int()); out += buf; break;
        case 'u': spec += "u"; snprintf(buf, sizeof buf, spec.c_str(), next_int()); out += buf; break;
        case 'x': spec += "x"; snprintf(buf, sizeof buf, spec.c_str(), next_int()); out += buf; break;
        case 'X': spec += "X"; snprintf(buf, sizeof buf, spec.c_str(), next_int()); out += buf; break;
        case 'p': snprintf(buf, sizeof buf, "0x%08x", next_int()); out += buf; break;
        case 'c': out += (char)next_int(); break;
        case 's': { u32 a = next_int(); out += a ? rt_guest_cstr(a) : "(null)"; break; }
        case 'f': case 'g': case 'e': { spec += *p; double v = fi < nflts ? flts[fi++] : 0.0; snprintf(buf, sizeof buf, spec.c_str(), v); out += buf; break; }
        case '%': out += '%'; break;
        case 0: return out;
        default: out += '?'; break;
        }
        (void)ll; (void)h;
    }
    return out;
}

HLE_FN(OSReport) {
    u32 ints[7] = { a1, a2, a3, a4, a5, a6, a7 };
    f64 flts[8] = { fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7 };
    std::string s = rt_format(rt_guest_cstr(a0), ints, 7, flts, 8, g_sp);
    fputs(s.c_str(), stderr);
    return RET_NONE();
}

HLE_FN(OSPanic) {
    u32 ints[5] = { a3, a4, a5, a6, a7 };
    f64 flts[8] = { fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7 };
    std::string s = rt_format(rt_guest_cstr(a2), ints, 5, flts, 8, g_sp);
    rt_fatal("OSPanic %s:%u: %s", rt_guest_cstr(a0), a1, s.c_str());
}
