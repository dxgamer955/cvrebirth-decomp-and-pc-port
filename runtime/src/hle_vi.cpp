// Video Interface (VI): el retrazado vertical se emula como un hito de 1/60 s.
#include "hle.h"
#include "game_state.h"
#include <chrono>
#include <atomic>
#include <algorithm>
#include <thread>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <sys/time.h>
#endif
#include "gx.h"
#include "audio.h"

// Variables de VI del SDK (sbss): callbacks y contador de retrazados.
static const u32 VI_POST_CB = 0x80316934, VI_PRE_CB = 0x80316938, VI_RETRACE_COUNT = 0x80316964;

u32 g_vi_frames = 0;
int g_vi_period_us = getenv("RT_VI_HZ") ? (int)(1e6 / atof(getenv("RT_VI_HZ"))) : 16683;   // periodo del retrazado; 0 = sin limite (F4)
bool g_vi_uncapped = getenv("RT_UNCAPPED") != nullptr;
extern u32 g_intr_enabled();
extern void rt_set_intr(u32);
volatile int g_irq_pending = 0;
std::atomic<bool> g_disp_sync{false};                          // retrazado acompasado al monitor (lo decide gx_render.cpp)
std::atomic<int> g_vblank_ticks{0};                            // intercambios terminados desde el ultimo retrazado (hilo de render)
bool g_disp_sync_bad = false;                                  // el intercambio no frena: no volver a intentarlo
double g_svc_alarm_max_ms = 0, g_svc_audio_max_ms = 0;          // maximo coste (ms) de una atencion de alarmas / de audio (RT_FPS)

// Temporizador de 1 kHz: levanta la marca de interrupcion que atienden los bucles del codigo elevado (RT_POLL). Atiende las alarmas
// de OS y, a 59.94 Hz, el retrazado de VI.
#ifdef _WIN32
static DWORD WINAPI timer_thread(LPVOID) {
    if (!getenv("RT_NO_PRIORITY")) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);   // el reloj de interrupciones no debe retrasarse
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);      // resolucion de ~1 ms
    if (!t) t = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = -10000;                                                                           // 1 ms (100 ns, relativo)
    SetWaitableTimer(t, &due, 1, nullptr, nullptr, FALSE);
    for (;;) { WaitForSingleObject(t, INFINITE); g_irq_pending = 1; }
    return 0;
}
void rt_start_vi_timer() { timeBeginPeriod(1); CreateThread(nullptr, 0, timer_thread, nullptr, 0, nullptr); }
#else
static void on_alarm(int) { g_irq_pending = 1; }
void rt_start_vi_timer() {
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_alarm; sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, nullptr);
    struct itimerval it; it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 1000; it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, nullptr);
}
#endif

// Interrupcion de retrazado: callback previo, contador, callback posterior y despertar a los hilos que esperan
// (lo que haria __VIRetraceHandler). Solo se atiende con las interrupciones habilitadas.
void rt_service_alarms();
void rt_service_irq() {
    if (!g_intr_enabled()) return;                           // se queda pendiente hasta que se habiliten
    g_irq_pending = 0;
    rt_set_intr(0);
    u32 saved_sp = g_sp;
    auto ts0 = std::chrono::steady_clock::now();
    rt_service_alarms();                                     // decrementador: alarmas de OS vencidas
    auto ts1 = std::chrono::steady_clock::now();
    rt_service_audio();                                      // DSP (correo) y DMA de AI (bloques de 3 ms)
    { auto ts2 = std::chrono::steady_clock::now(); extern double g_svc_alarm_max_ms, g_svc_audio_max_ms;
      double a = std::chrono::duration<double, std::milli>(ts1 - ts0).count(), b = std::chrono::duration<double, std::milli>(ts2 - ts1).count();
      if (a > g_svc_alarm_max_ms) g_svc_alarm_max_ms = a; if (b > g_svc_audio_max_ms) g_svc_audio_max_ms = b; }
    static auto next_vi = std::chrono::steady_clock::now();
    static auto last_vi = next_vi;
    auto now = std::chrono::steady_clock::now();
    extern bool gx_xfb_pending();                           // sin limite: un retrazado por cada fotograma que el juego termina
    if (g_disp_sync.load(std::memory_order_relaxed) && !g_vi_uncapped) {
        // Acompasado al monitor (vsync con pantalla de ~60 Hz): un retrazado por cada intercambio terminado en el hilo de render, en vez del
        // reloj de 59,94 Hz. Con el reloj, la fase entre ambos se desplaza y cada pocos segundos un fotograma se repite y otro se salta.
        // Minimo 12 ms entre retrazados (por si el intercambio no frena) y respaldo a los 50 ms (ventana atascada).
        auto since = now - last_vi;
        bool fire = (g_vblank_ticks.load(std::memory_order_relaxed) > 0 && since >= std::chrono::milliseconds(12)) || since >= std::chrono::milliseconds(50);
        if (!fire) { g_sp = saved_sp; rt_set_intr(1); return; }
        g_vblank_ticks.store(0, std::memory_order_relaxed);
        next_vi = now + std::chrono::microseconds(g_vi_period_us);      // si se vuelve al reloj, sin salto
        // Salvaguarda: si el intercambio no frena al ritmo de la pantalla (vsync ignorado, VRR...), el juego iria acelerado: se vuelve al reloj.
        // Mediana de 30 intervalos (no la media): al arrancar o tras una carga la cadena de intercambio tiene buferes libres y unos cuantos
        // retrazados salen al minimo de 12 ms aunque el monitor si frene; sin freno real casi todos los intervalos rondan 12-13 ms.
        static double iv[30]; static int n = 0;
        iv[n++] = std::chrono::duration<double, std::milli>(since).count();
        if (n == 30) { n = 0; std::nth_element(iv, iv + 15, iv + 30); double med = iv[15];
            if (med < 15.0) { g_disp_sync_bad = true; g_disp_sync = false; rt_log("[vi] el intercambio no frena a ~60 Hz (mediana %.2f ms): retrazado por reloj\n", med); } }
    } else {
        if (now < next_vi && !(g_vi_uncapped && gx_xfb_pending())) {   // (sin limite: ademas, un retrazado por fotograma terminado; el reloj normal sigue de reserva)
            g_sp = saved_sp; rt_set_intr(1); return; }
        next_vi += std::chrono::microseconds(g_vi_period_us);
        if (g_vi_uncapped || now - next_vi > std::chrono::milliseconds(100)) next_vi = now + std::chrono::microseconds(g_vi_uncapped ? g_vi_period_us : 0);   // sin acumular retraso si el anfitrion se atasca
    }
    last_vi = now;
    u32 cnt = LD32(VI_RETRACE_COUNT);
    u32 pre = LD32(VI_PRE_CB);
    if (pre) rt_call_indirect(pre, cnt, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    cnt++;
    ST32(VI_RETRACE_COUNT, cnt);
    u32 post = LD32(VI_POST_CB);
    if (post) rt_call_indirect(post, cnt, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    g_vi_frames++;
    game_state_tick();                                       // punto restaurable y datos de una ranura cargada (game_state.cpp)
    if (const char* md = getenv("RT_MEMDUMP")) {             // diagnostico: RT_MEMDUMP=f1,f2,... vuelca MEM1 (24 MB) + MEM2 (64 MB) en esos retrazados
        for (const char* q = md; *q; ) { char* e; unsigned long f = strtoul(q, &e, 10); if (e == q) break;
            if (f == g_vi_frames) { char fn[512]; const char* pre = getenv("RT_MEMDUMP_PREFIX"); snprintf(fn, sizeof fn, "%smem_%u.bin", pre ? pre : "", g_vi_frames);
                if (FILE* o = fopen(fn, "wb")) { fwrite(HP(0x80000000), 1, 0x01800000, o); fwrite(HP(0x90000000), 1, 0x04000000, o); fclose(o); rt_log("[rt] memoria volcada en %s\n", fn); } }
            q = *e ? e + 1 : e; }
    }
    if (getenv("RT_FPS")) {                                  // fotogramas VI por segundo cada ~2 s (diagnostico)
        u64 gx_draw_count(); u64 gx_copy_count(); extern u64 g_kpad_reads; extern double g_t_draw, g_t_copy, g_t_present, g_t_render; static u64 d0 = 0, c0 = 0, k0 = 0; static double td0 = 0, tc0 = 0, tp0 = 0, tr0 = 0;
        static auto t0 = std::chrono::steady_clock::now(); static u32 f0 = 0;
        auto t = std::chrono::steady_clock::now(); double dt = std::chrono::duration<double>(t - t0).count();
        if (dt >= 2.0) { rt_log("[fps] VI %.1f  juego(KPAD) %.1f  dibujos/s %.0f  copias/s %.1f (VI %u)  ms/s: juego-dibujo %.0f copia %.0f presentar %.0f | hilo de render %.0f\n", (g_vi_frames - f0) / dt, (g_kpad_reads - k0) / dt, (gx_draw_count() - d0) / dt, (gx_copy_count() - c0) / dt, g_vi_frames, (g_t_draw - td0) / dt * 1000, (g_t_copy - tc0) / dt * 1000, (g_t_present - tp0) / dt * 1000, (g_t_render - tr0) / dt * 1000); { extern unsigned g_ft_hist[5]; extern double g_ft_max_ms; rt_log("      tiempo entre fotogramas del juego: <14ms %u | 14-17 %u | 17-20 %u | 20-30 %u | >30 %u | max %.1f ms; servicio max: alarmas %.1f ms, audio %.1f ms\n", g_ft_hist[0], g_ft_hist[1], g_ft_hist[2], g_ft_hist[3], g_ft_hist[4], g_ft_max_ms, g_svc_alarm_max_ms, g_svc_audio_max_ms); for (int i = 0; i < 5; i++) g_ft_hist[i] = 0; g_ft_max_ms = 0; g_svc_alarm_max_ms = g_svc_audio_max_ms = 0; }
        { extern double g_t_wait_present, g_max_interval_ms; extern unsigned g_slow_frames; static double w0 = 0; static unsigned sl0 = 0; rt_log("      espera al render %.0f ms/s | intervalo maximo entre fotogramas %.1f ms | fotogramas >20 ms: %u\n", (g_t_wait_present - w0) / dt * 1000, g_max_interval_ms, g_slow_frames - sl0); w0 = g_t_wait_present; sl0 = g_slow_frames; g_max_interval_ms = 0; }
        { extern double g_t_sec_a, g_t_sec_b, g_t_sec_c; static double a0 = 0, b0 = 0, c0_ = 0; rt_log("      secciones de gx_draw (ms/s): vertices %.0f  estado/texturas %.0f  lote %.0f\n", (g_t_sec_a - a0) / dt * 1000, (g_t_sec_b - b0) / dt * 1000, (g_t_sec_c - c0_) / dt * 1000); a0 = g_t_sec_a; b0 = g_t_sec_b; c0_ = g_t_sec_c; }
        { extern u64 g_batches, g_hashed_bytes, g_pal_hashes; static u64 b0 = 0, h0 = 0, p0 = 0; double fr = (g_vi_frames - f0) ? (double)(g_vi_frames - f0) : 1.0;
          { extern std::atomic<unsigned> g_swap_hist[4]; unsigned h[4]; for (int i = 0; i < 4; i++) h[i] = g_swap_hist[i].exchange(0);
            rt_log("      intercambios (swap): <12ms %u | 12-21 %u | 21-30 %u | >30 %u\n", h[0], h[1], h[2], h[3]); }
          rt_log("      por fotograma: lotes %.0f  textura hasheada %.0f KB  paletas hasheadas %.0f\n", (g_batches - b0) / fr, (g_hashed_bytes - h0) / fr / 1024.0, (g_pal_hashes - p0) / fr); b0 = g_batches; h0 = g_hashed_bytes; p0 = g_pal_hashes; } tr0 = g_t_render; k0 = g_kpad_reads; td0 = g_t_draw; tc0 = g_t_copy; tp0 = g_t_present; t0 = t; f0 = g_vi_frames; d0 = gx_draw_count(); c0 = gx_copy_count(); }
    }
    gx_frame_present();
    void rt_ios_deliver(); rt_ios_deliver();                                                  // callbacks de IOS pendientes (interrupcion de IPC)
    rt_call_indirect(0x801914C0, 0x80316940, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);   // OSWakeupThread(cola de VIWaitForRetrace)
    g_sp = saved_sp;
    rt_set_intr(1);
}

// Engine_WaitFrameTime: limitador de fotogramas del motor (Mercury). a0 = objeto: +4 = fotogramas de 1/60 s que como minimo dura cada
// fotograma de juego, +0xC = (salida) fotogramas de 1/60 s transcurridos (el juego avanza su logica en pasos de 1/60 s).
// Espera retrazados hasta que haya pasado el tiempo; con F4/RT_UNCAPPED no espera mas que al fotograma terminado y informa de
// 1 paso, de modo que el juego va tan rapido como pueda el anfitrion (mas rapido que el tiempo real).
HLE_FN(Engine_WaitFrameTime) {
    static const u32 LAST_TICK = 0x803166A0;
    u32 tick;
    for (;;) {
        rt_call_indirect(0x801993B0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);       // VIWaitForRetrace
        tick = RT_MFTB(268);
        u32 us = ((tick - LD32(LAST_TICK)) * 8) / (LD32(0x800000F8) / 500000);
        float frames = (float)(s32)(0.5f + (60.0f * (float)us) / 1000000.0f);
        if (g_vi_uncapped) frames = 1.0f;
        STF(a0 + 0xC, frames);
        if (!(frames < (float)LD32(a0 + 4))) break;
    }
    ST32(LAST_TICK, tick);
    return RET_NONE();
}
