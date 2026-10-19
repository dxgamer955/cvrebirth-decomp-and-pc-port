// Entrada: el Wii Remote de la canal 0 se emula con teclado y raton. Se sustituye KPADRead
// (KPADReadEx: KPADReadEx(chan, KPADStatus* buf, count, s32* err)) en lugar de la pila Bluetooth/WPAD.
#include "hle.h"
#include "gx.h"
#include <cstdlib>
#include <chrono>

enum { WPAD_LEFT = 0x0001, WPAD_RIGHT = 0x0002, WPAD_DOWN = 0x0004, WPAD_UP = 0x0008, WPAD_PLUS = 0x0010, WPAD_2 = 0x0100,
       WPAD_1 = 0x0200, WPAD_B = 0x0400, WPAD_A = 0x0800, WPAD_MINUS = 0x1000, WPAD_HOME = 0x8000 };

static u32 g_prev_hold = 0;
u64 g_kpad_reads = 0;
// Histograma del tiempo entre fotogramas del juego (lecturas del mando del canal 0): <14, 14-17, 17-20, 20-30, >30 ms y maximo
unsigned g_ft_hist[5] = {}; double g_ft_max_ms = 0; volatile double g_ft_last_ms = 0;
static void frame_time_stat() {
    static auto last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now(); double ms = std::chrono::duration<double, std::milli>(now - last).count(); last = now; g_ft_last_ms = ms;
    int b = ms < 14 ? 0 : ms < 17 ? 1 : ms < 20 ? 2 : ms < 30 ? 3 : 4; g_ft_hist[b]++; if (ms > g_ft_max_ms) g_ft_max_ms = ms;
}

HLE_FN(KPADReadEx) {
    u32 chan = a0, buf = a1, errp = a3;
    static unsigned calls[4]; if (getenv("RT_INLOG") && chan < 4 && (calls[chan]++ % 300) == 0) { rt_log("[kpad] chan=%u llamada %u count=%u", chan, calls[chan], a2); if (chan == 0 && calls[0] == 301) rt_backtrace(10); }
    if (chan == 0) { g_kpad_reads++; frame_time_stat(); }
    if (chan != 0) { if (errp) ST32(errp, (u32)-1); return RET_I(0); }
    u32 hold = gx_input_buttons();
    for (u32 i = 0; i < 0xB0; i += 4) ST32(buf + i, 0);
    ST32(buf + 0x00, hold);
    ST32(buf + 0x04, hold & ~g_prev_hold);                // pulsados en este muestreo
    ST32(buf + 0x08, g_prev_hold & ~hold);                // soltados
    g_prev_hold = hold;
    STF(buf + 0x10, 0.0f); STF(buf + 0x14, 1.0f);          // acelerometro: gravedad en el eje z
    STF(buf + 0x18, 1.0f);
    float px = 0, py = 0; gx_input_pointer(&px, &py);
    STF(buf + 0x20, px); STF(buf + 0x24, py);
    STF(buf + 0x34, 1.0f);                                 // horizon.x
    ST8(buf + 0x5C, 0); ST8(buf + 0x5D, 0); ST8(buf + 0x5E, 1); ST8(buf + 0x5F, 0);
    if (errp) ST32(errp, 0);
    return RET_I(1);
}

// El motor Bluetooth (BTE) del WPAD corre en una pila propia disparado por una alarma: sin Bluetooth no se ejecuta.
HLE_FN(BTE_EngineRun) { return RET_NONE(); }

// OSSwitchFiber(arg0..arg3, func, stack): llama a func(arg0..arg3) sobre otra pila.
HLE_FN(OSSwitchFiber) {
    u32 saved = g_sp;
    g_sp = a5 - 8;
    RetRegs r = rt_call_indirect(a4, a0, a1, a2, a3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    g_sp = saved;
    return r;
}
