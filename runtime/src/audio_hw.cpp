// Hardware de audio de la Wii bajo el SDK: interfaz de audio (AI: DMA de muestras a 32 kHz), buzones del DSP y
// la salida SDL2. Los registros se leen/escriben desde el codigo elevado (AIInit, AIInitDMA, DSPSendMailToDSP...);
// el DSP en si (microcodigo AX) esta en audio_ax.cpp.
//
// Reloj: el consumidor es SDL (o el reloj del anfitrion si no hay dispositivo). Cada vez que la cola de muestras
// baja de RT_AUDIO_BLOCKS bloques se "reproduce" otro bloque del DMA (se copia al anillo) y se levanta la
// interrupcion de AI (callback de AIRegisterDMACallback), como haria el hardware al recargar el DMA.
#include "hle.h"
#include "audio.h"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>

// ---- registros ----
static u32 g_aicr = 0x40, g_aivr = 0, g_aiit = 0;            // AICR: bit6 = DSP a 32 kHz (lo deja AIInit)
static u16 g_dspcr = 0;
static u16 g_dspregs[0x40];                                    // 0x5000..0x507F sin funcion especial
static u16 g_mail_hi = 0;                                      // CPU -> DSP: mitad alta pendiente de la lectura de la baja
static std::deque<u32> g_dsp_out;                              // DSP -> CPU
static int g_dsp_irq = 0;                                      // interrupciones de DSP pendientes de entrega
static bool g_ai_run = false;
static u32 g_dma_addr = 0, g_dma_len = 0;                      // registros (direccion fisica, longitud en bloques de 32 bytes)
static u32 g_blk_addr = 0, g_blk_bytes = 0;                    // bloque que "suena"
static u64 g_blocks_played = 0;
static int g_log = 0;
static struct DspBoot { DspBoot() { g_dsp_out.push_back(0x8071FEED); } } g_dsp_boot;   // la ROM del DSP anuncia que arranco (sin interrupcion)

bool audio_mmio_claims(u32 addr) {
    u32 t = addr >> 24, o = addr & 0xFFFFFF;
    if (t != 0xCC && t != 0xCD) return false;
    return (o >= 0x005000 && o < 0x005040) || (o >= 0x006C00 && o < 0x006C20);
}

// ---- salida SDL ----
static const u32 RING = 1 << 15;                               // fotogramas (L,R) del anillo
static s16 g_ring[RING * 2];
static std::atomic<u64> g_wr{0}, g_rd{0};
static SDL_AudioDeviceID g_dev = 0;
static bool g_audio_opened = false, g_virtual_clock = false;
static std::chrono::steady_clock::time_point g_t0;
static FILE* g_dump = nullptr;
static u64 g_underruns = 0;

static void sdl_callback(void*, Uint8* stream, int len) {
    s16* out = (s16*)stream;
    u32 want = len / 4;
    u64 rd = g_rd.load(std::memory_order_relaxed), wr = g_wr.load(std::memory_order_acquire);
    u32 avail = (u32)(wr - rd);
    u32 n = avail < want ? avail : want;
    for (u32 i = 0; i < n; i++) { u32 k = (u32)((rd + i) & (RING - 1)); out[2 * i] = g_ring[2 * k]; out[2 * i + 1] = g_ring[2 * k + 1]; }
    if (n < want) { memset(out + 2 * n, 0, (want - n) * 4); g_underruns++; }
    g_rd.store(rd + n, std::memory_order_release);
}

static void audio_open() {
    if (g_audio_opened) return;
    g_audio_opened = true;
    g_t0 = std::chrono::steady_clock::now();
    if (const char* d = getenv("RT_AUDIO_DUMP")) g_dump = fopen(d, "wb");
    const char* e = getenv("RT_AUDIO");
    if (e && e[0] == '0') { g_virtual_clock = true; return; }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { rt_log("[audio] SDL: %s (reloj virtual)\n", SDL_GetError()); g_virtual_clock = true; return; }
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = 32000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = sdl_callback;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);     // SDL convierte si el dispositivo no admite 32 kHz
    if (!g_dev) { rt_log("[audio] no se pudo abrir el dispositivo: %s (reloj virtual)\n", SDL_GetError()); g_virtual_clock = true; return; }
    SDL_PauseAudioDevice(g_dev, 0);
    rt_log("[audio] dispositivo SDL abierto (%s)\n", SDL_GetCurrentAudioDriver());
}

static u32 ring_queued() {
    if (g_virtual_clock) {                                     // sin dispositivo: el reloj del anfitrion "consume" las muestras
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
        u64 consumed = (u64)(s * 32000.0), wr = g_wr.load();
        if (consumed > wr) consumed = wr;
        g_rd.store(consumed);
    }
    return (u32)(g_wr.load() - g_rd.load());
}

// Copia el bloque de DMA actual (muestras s16 big-endian L,R) al anillo.
static void play_block() {
    u32 frames = g_blk_bytes / 4;
    u64 wr = g_wr.load(std::memory_order_relaxed);
    if (g_blk_bytes == 0 || (g_blk_addr & 0x1FFFFFFF) + g_blk_bytes > 0x14000000) {   // sin direccion valida: silencio
        for (u32 i = 0; i < 96; i++) { u32 k = (u32)((wr + i) & (RING - 1)); g_ring[2 * k] = g_ring[2 * k + 1] = 0; }
        g_wr.store(wr + 96, std::memory_order_release);
        return;
    }
    const u8* p = HP(g_blk_addr);
    if (g_wr.load() - g_rd.load() + frames > RING) return;            // anillo lleno (no deberia ocurrir)
    for (u32 i = 0; i < frames; i++) {
        u32 k = (u32)((wr + i) & (RING - 1));
        g_ring[2 * k] = (s16)((p[4 * i] << 8) | p[4 * i + 1]);
        g_ring[2 * k + 1] = (s16)((p[4 * i + 2] << 8) | p[4 * i + 3]);
    }
    if (g_dump) for (u32 i = 0; i < frames; i++) fwrite(&g_ring[2 * ((wr + i) & (RING - 1))], 4, 1, g_dump);
    g_wr.store(wr + frames, std::memory_order_release);
}

// ---- registros ----
u32 audio_mmio_read(u32 addr, u32 size) {
    u32 o = addr & 0xFFFFFF;
    if (o >= 0x006C00) {
        u32 r = o & 0x1C, v = 0;
        if (r == 0x00) v = g_aicr & ~0x20u;                            // SCRESET se lee como 0
        else if (r == 0x04) v = g_aivr;
        else if (r == 0x08) v = (u32)(g_blocks_played * 96);           // contador de muestras
        else if (r == 0x0C) v = g_aiit;
        return size == 4 ? v : (size == 2 ? ((o & 2) ? v & 0xFFFF : v >> 16) : 0);
    }
    if (size == 4) return (audio_mmio_read(addr, 2) << 16) | audio_mmio_read(addr + 2, 2);
    switch (o) {
    case 0x005000: return 0;                                           // buzon CPU->DSP siempre libre (el DSP lo consume al instante)
    case 0x005002: return 0;
    case 0x005004: return g_dsp_out.empty() ? 0 : (((g_dsp_out.front() >> 16) & 0xFFFF) | 0x8000);
    case 0x005006: { if (g_dsp_out.empty()) return 0; u32 v = g_dsp_out.front() & 0xFFFF; g_dsp_out.pop_front(); return v; }
    case 0x00500A: return g_dspcr;
    case 0x005030: return (g_dma_addr >> 16) & 0x1FFF;
    case 0x005032: return g_dma_addr & 0xFFE0;
    case 0x005036: return (g_dma_len & 0x7FFF) | (g_ai_run ? 0x8000 : 0);
    case 0x00503A: return g_ai_run ? ((g_blk_bytes / 2) >> 5) & 0x7FFF : 0;   // bloques de 32 bytes por reproducir
    }
    return o < 0x005080 ? g_dspregs[(o - 0x005000) >> 1] : 0;
}

void audio_mmio_write(u32 addr, u32 size, u32 v) {
    u32 o = addr & 0xFFFFFF;
    if (o >= 0x006C00) {
        u32 r = o & 0x1C;
        if (size != 4) { if (g_log++ < 5) rt_log("[audio] escritura de %u bytes en AI 0x%X\n", size, o); return; }
        if (r == 0x00) {
            u32 old = g_aicr;
            g_aicr = (v & ~0x28u) | (old & 0x08u);                      // AIINT (bit3) se limpia escribiendo 1; SCRESET no se guarda
            if (v & 0x08) g_aicr &= ~0x08u;
            if (v & 0x20) g_blocks_played = 0;
        } else if (r == 0x04) g_aivr = v;
        else if (r == 0x0C) g_aiit = v;
        return;
    }
    if (size == 4) { audio_mmio_write(addr, 2, v >> 16); audio_mmio_write(addr + 2, 2, v & 0xFFFF); return; }
    v &= 0xFFFF;
    switch (o) {
    case 0x005000: g_mail_hi = (u16)v; return;
    case 0x005002: dsp_mail_from_cpu(((u32)g_mail_hi << 16) | v); return;
    case 0x00500A: g_dspcr = (u16)((v & ~0xA8u) | (g_dspcr & 0xA8u & ~v)); return;   // AIDINT/ARINT/DSPINT: escribir 1 limpia
    case 0x005030: g_dma_addr = (g_dma_addr & 0xFFFF) | ((v & 0x1FFF) << 16); return;
    case 0x005032: g_dma_addr = (g_dma_addr & 0xFFFF0000u) | (v & 0xFFE0); return;
    case 0x005036: {
        g_dma_len = v & 0x7FFF;
        bool run = (v & 0x8000) != 0;
        if (run && getenv("RT_NOAUDIO")) run = false;                  // diagnostico: sin audio (AX no avanza)
        if (run && !g_ai_run) { audio_open(); g_irq_pending = 1; if (getenv("RT_AUDIO_LOG")) rt_log("[audio] AIStartDMA addr=%08X len=%u bloques\n", g_dma_addr, g_dma_len); }
        g_ai_run = run;
        return;
    }
    }
    if (o < 0x005080) g_dspregs[(o - 0x005000) >> 1] = (u16)v;
}

// ---- correo del DSP a la CPU ----
void dsp_post_mail(u32 mail, bool irq) { g_dsp_out.push_back(mail); if (irq) { g_dsp_irq++; g_irq_pending = 1; } }

// Interrupciones: DSP (correo -> __DSPHandler) y DMA de AI (callback registrado con AIRegisterDMACallback).
static void deliver_dsp_irq() {
    static const u32 DSP_HANDLER = 0x801B2BF0;                         // __DSPHandler (manejador de la interrupcion 7)
    while (g_dsp_irq > 0) {
        g_dsp_irq--;
        u32 ctx = LD32(0x800000D4); ctx = ctx ? (ctx | 0x80000000u) : 0x80003F00u;     // contexto interrumpido (se restaura al salir)
        rt_call_indirect(DSP_HANDLER, 7, ctx, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}

static void fire_ai_irq() {
    static const u32 AI_CB = 0x80316B18, AI_BUSY = 0x80316AE4;         // callback de DMA y su guarda de reentrada (fn_801ACED0)
    g_aicr |= 0x08;
    u32 cb = LD32(AI_CB);
    if (cb && !LD32(AI_BUSY)) {
        ST32(AI_BUSY, 1);
        rt_call_indirect(cb, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        ST32(AI_BUSY, 0);
    }
}

void rt_service_audio() {
    static bool busy = false;
    if (busy) return;
    busy = true;
    deliver_dsp_irq();
    if (g_ai_run) {
        static u32 target = 0;
        if (!target) { const char* e = getenv("RT_AUDIO_BLOCKS"); target = (e ? atoi(e) : 8) * 96; if (target < 192) target = 192; }
        for (int n = 0; n < 12 && ring_queued() < target; n++) {
            g_blk_addr = g_dma_addr | 0x80000000u; g_blk_bytes = g_dma_len * 32;      // el DMA recarga sus registros al empezar el bloque
            play_block();
            g_blocks_played++;
            fire_ai_irq();
            deliver_dsp_irq();
        }
    }
    busy = false;
}

// Pausa/reanuda la salida de sonido (ventana en segundo plano). secs = duracion de la pausa, para el reloj virtual.
void audio_pause(bool pause, double secs) {
    if (g_dev) SDL_PauseAudioDevice(g_dev, pause ? 1 : 0);
    if (!pause && g_virtual_clock) g_t0 += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(secs));
}

void audio_stats_print() {
    if (g_audio_opened) rt_log("[audio] bloques=%llu infraflujos=%llu\n", (unsigned long long)g_blocks_played, (unsigned long long)g_underruns);
}
