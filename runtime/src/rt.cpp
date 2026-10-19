// Nucleo del runtime: memoria del invitado, llamadas indirectas, registro de errores.
#include "hle.h"
#include "audio.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <algorithm>
#include <map>
#include <vector>
#include <csignal>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#include <dlfcn.h>
#include <ucontext.h>
#include <sys/time.h>
#endif

u8* g_mem = nullptr;
const char* rt_name_of(u32 addr);
static u32 g_main_cs[512];
u32* g_cs = g_main_cs;
bool g_profile = false;
static std::map<u32, u64> g_prof;
void rt_profile_hit(u32 a) { g_prof[a]++; }
static void print_profile() {
    std::vector<std::pair<u64, u32>> v; for (auto& kv : g_prof) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    fprintf(stderr, "[perfil] %zu funciones distintas ejecutadas; mas llamadas:\n", v.size());
    for (size_t i = 0; i < v.size() && i < (size_t)(getenv("RT_PROFILE_N") ? atoi(getenv("RT_PROFILE_N")) : 60); i++) fprintf(stderr, "  %8llu %s\n", (unsigned long long)v[i].first, rt_name_of(v[i].second));
}
int g_callstack_n = 0;
#ifndef _WIN32
// Muestreo estadistico (RT_SAMPLE=1): ITIMER_PROF a 250 Hz registra la funcion elevada activa (y su llamador) y se vuelca al salir.
static u32 g_samp_fn[4096], g_samp_n[4096]; static u32 g_nsamples = 0;
static const int NRIP = 1 << 16; static void* g_rip_k[NRIP]; static u32 g_rip_v[NRIP];
static void on_prof(int, siginfo_t*, void* uc_) {
    void* ip = (void*)((ucontext_t*)uc_)->uc_mcontext.gregs[REG_RIP]; u32 hr = (u32)(((uintptr_t)ip >> 2) * 2654435761u) & (NRIP - 1);
    for (int i = 0; i < 64; i++, hr = (hr + 1) & (NRIP - 1)) { if (g_rip_k[hr] == ip || !g_rip_k[hr]) { g_rip_k[hr] = ip; g_rip_v[hr]++; break; } }
    int n = g_callstack_n; u32 f = (n < 1 || n > 512) ? 0 : g_cs[n - 1]; g_nsamples++;
    u32 h = (f >> 2) & 4095; for (int i = 0; i < 64; i++, h = (h + 1) & 4095) { if (g_samp_fn[h] == f || !g_samp_fn[h]) { g_samp_fn[h] = f; g_samp_n[h]++; break; } }
}
static void print_samples() {
    if (!g_nsamples) return;
    std::map<std::string, u32> hs; for (int i = 0; i < NRIP; i++) { if (!g_rip_k[i]) continue; struct { void* first; u32 second; } kv = { g_rip_k[i], g_rip_v[i] }; Dl_info di; std::string n = dladdr(kv.first, &di) && di.dli_sname ? di.dli_sname : "?"; hs[n] += kv.second; }
    std::vector<std::pair<u32, std::string>> hv; for (auto& kv : hs) hv.push_back({kv.second, kv.first}); std::sort(hv.rbegin(), hv.rend());
    fprintf(stderr, "[muestreo] funciones del anfitrion:\n");
    for (size_t i = 0; i < hv.size() && i < 25; i++) fprintf(stderr, "  %5.1f%% %s\n", 100.0 * hv[i].first / g_nsamples, hv[i].second.c_str());
    std::map<u32, u32> self; for (int i = 0; i < 4096; i++) if (g_samp_fn[i] || g_samp_n[i]) self[g_samp_fn[i]] += g_samp_n[i];
    std::vector<std::pair<u32, u32>> v; for (auto& kv : self) v.push_back({kv.second, kv.first});
    std::sort(v.rbegin(), v.rend());
    fprintf(stderr, "[muestreo] %u muestras; funcion activa:\n", g_nsamples);
    for (size_t i = 0; i < v.size() && i < 25; i++) fprintf(stderr, "  %5.1f%% %s\n", 100.0 * v[i].first / g_nsamples, rt_name_of(v[i].second));
}
#else
// Muestreo en Windows (RT_SAMPLE=1): un hilo suspende el hilo del juego cada ~1 ms y anota RIP + funcion elevada activa, separando
// los fotogramas normales (cubo 0) de los lentos (cubo 1, ultimo intervalo > 25 ms). Los RIP se imprimen como desplazamiento
// respecto a la imagen; se resuelven fuera con `nm -n castlevania.exe`.
extern volatile double g_ft_last_ms;
static const int NRIP = 1 << 14; static u64 g_rip_k[2][NRIP]; static u32 g_rip_v[2][NRIP]; static u32 g_wfn_k[2][4096], g_wfn_v[2][4096]; static u32 g_wtotal[2];
static HANDLE g_samp_target;
static void win_hit(int b, u64 ip, u32 f) {
    u32 hr = (u32)((ip >> 2) * 2654435761u) & (NRIP - 1);
    for (int i = 0; i < 64; i++, hr = (hr + 1) & (NRIP - 1)) { if (g_rip_k[b][hr] == ip || !g_rip_k[b][hr]) { g_rip_k[b][hr] = ip; g_rip_v[b][hr]++; break; } }
    u32 h = (f >> 2) & 4095; for (int i = 0; i < 64; i++, h = (h + 1) & 4095) { if (g_wfn_k[b][h] == f || !g_wfn_v[b][h]) { g_wfn_k[b][h] = f; g_wfn_v[b][h]++; break; } }
    g_wtotal[b]++;
}
static DWORD WINAPI sampler_main(LPVOID) {
    for (;;) {
        Sleep(1);
        if (SuspendThread(g_samp_target) == (DWORD)-1) continue;
        CONTEXT c; c.ContextFlags = CONTEXT_CONTROL; u64 ip = 0; bool ok = GetThreadContext(g_samp_target, &c); if (ok) ip = c.Rip;
        int n = g_callstack_n; u32 f = (n < 1 || n > 512) ? 0 : g_cs[n - 1];
        ResumeThread(g_samp_target);
        if (ok) win_hit(g_ft_last_ms > 25.0 ? 1 : 0, ip, f);
    }
    return 0;
}
static void print_samples() {
    u64 base = (u64)GetModuleHandleA(nullptr);
    for (int b = 0; b < 2; b++) {
        if (!g_wtotal[b]) continue;
        std::vector<std::pair<u32, u64>> hv; std::map<std::string, u32> out;
        for (int i = 0; i < NRIP; i++) if (g_rip_k[b][i]) { u64 off = g_rip_k[b][i] - base; char t[40]; snprintf(t, sizeof t, off < 0x4000000 ? "image+0x%llx" : "otro-modulo", (unsigned long long)off); out[t] += g_rip_v[b][i]; if (off < 0x4000000) hv.push_back({g_rip_v[b][i], off}); }
        std::sort(hv.rbegin(), hv.rend());
        fprintf(stderr, "[muestreo-%s] %u muestras (%.0f%%) RIP:\n", b ? "LENTO" : "normal", g_wtotal[b], 0.0);
        for (size_t i = 0; i < hv.size() && i < 2500; i++) fprintf(stderr, "  RIP %6.2f%% 0x%llx\n", 100.0 * hv[i].first / g_wtotal[b], (unsigned long long)hv[i].second);
        fprintf(stderr, "  fuera de la imagen (ntdll/kernel/driver): %u\n", out.count("otro-modulo") ? out["otro-modulo"] : 0);
        std::vector<std::pair<u32, u32>> v; for (int i = 0; i < 4096; i++) if (g_wfn_v[b][i]) v.push_back({g_wfn_v[b][i], g_wfn_k[b][i]});
        std::sort(v.rbegin(), v.rend());
        fprintf(stderr, "[muestreo-%s] funcion elevada activa:\n", b ? "LENTO" : "normal");
        for (size_t i = 0; i < v.size() && i < 30; i++) fprintf(stderr, "  %5.1f%% %s\n", 100.0 * v[i].first / g_wtotal[b], rt_name_of(v[i].second));
    }
}
#endif
u32 g_sp = 0;

void rt_log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

const char* rt_name_of(u32 addr);
static const char* name_of(u32 addr) { return rt_name_of(addr); }
const char* rt_name_of(u32 addr) {
    const FuncEntry* b = g_functable; const FuncEntry* e = g_functable + g_functable_count;
    const FuncEntry* it = std::lower_bound(b, e, addr, [](const FuncEntry& x, u32 a) { return x.addr < a; });
    return (it != e && it->addr == addr) ? it->name : "?";
}

void rt_backtrace(int max) {
    int n = g_callstack_n < 512 ? g_callstack_n : 512;
    for (int i = n - 1, k = 0; i >= 0 && k < max; i--, k++) fprintf(stderr, "    #%d 0x%08X %s\n", k, g_cs[i], name_of(g_cs[i]));
}

[[noreturn]] void rt_fatal(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); fputs("[FATAL] ", stderr); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
    rt_backtrace();
    exit(2);
}

extern u32 g_vi_frames;
void gx_stats_print(); void gx_render_stats();
static void on_interrupt(int sig) {
#ifndef _WIN32
    struct itimerval off; memset(&off, 0, sizeof off); setitimer(ITIMER_PROF, &off, nullptr);
#endif
    gx_stats_print(); gx_render_stats(); void audio_stats_print(); audio_stats_print();
    fprintf(stderr, "\n[rt] senal %d (fotogramas VI: %u): pila de llamadas del invitado:\n", sig, g_vi_frames);
    rt_backtrace(30);
    void rt_backtrace_all(); rt_backtrace_all();
    if (g_profile) print_profile();
    print_samples();
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 3);
#endif
    _exit(3);
}

void rt_init_memory() {
    g_profile = getenv("RT_PROFILE") != nullptr;
#ifndef _WIN32
    if (getenv("RT_SAMPLE")) {
        struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_RESTART | SA_SIGINFO; sigaction(SIGPROF, &sa, nullptr);
        struct itimerval it; it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 4000; it.it_value = it.it_interval; setitimer(ITIMER_PROF, &it, nullptr);
    }
#endif
    signal(SIGINT, on_interrupt); signal(SIGTERM, on_interrupt); signal(SIGSEGV, on_interrupt);
    const size_t size = 0x14000000;
#ifdef _WIN32
    void* p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);      // 320 MB: MEM1 + MEM2 + registros (se comprometen al usarse)
    if (!p) rt_fatal("no se pudo reservar la memoria del invitado");
    if (!IsThreadAFiber()) ConvertThreadToFiber(nullptr);                                  // el hilo principal es una fibra mas (hilos del invitado)
    if (!getenv("RT_NO_PRIORITY")) {                                                       // menos tirones por otros procesos (antivirus, navegador...)
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    }
    if (getenv("RT_SAMPLE")) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_samp_target, 0, FALSE, DUPLICATE_SAME_ACCESS);
        HANDLE t = CreateThread(nullptr, 0, sampler_main, nullptr, 0, nullptr); SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL);
        atexit(print_samples);
    }
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) rt_fatal("no se pudo reservar la memoria del invitado");
#endif
    g_mem = (u8*)p;
}

RetRegs rt_unimplemented(u32 addr) { rt_fatal("funcion no implementada en 0x%08X", addr); }

RetRegs rt_unimplemented_named(u32 addr, const char* name, const char* why) {
    rt_fatal("funcion no implementada: %s (0x%08X) [%s]", name, addr, why);
}

RetRegs rt_call_indirect(u32 addr, NATIVE_PARAMS) {
    const FuncEntry* b = g_functable;
    const FuncEntry* e = g_functable + g_functable_count;
    const FuncEntry* it = std::lower_bound(b, e, addr, [](const FuncEntry& x, u32 a) { return x.addr < a; });
    if (it == e || it->addr != addr) rt_fatal("llamada indirecta a una direccion que no es inicio de funcion: 0x%08X", addr);
    return it->fn(a0, a1, a2, a3, a4, a5, a6, a7, fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7);
}

void rt_trace_ret(u32 addr, u32 r3) { fprintf(stderr, "[trace]   %s -> 0x%X\n", name_of(addr), r3); }   // retorno de las funciones de trace.txt

void rt_trace(u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    fprintf(stderr, "[trace] %s(0x%X, 0x%X, 0x%X, 0x%X) sp=%08X <- ", name_of(addr), a0, a1, a2, a3, g_sp);
    int n = g_callstack_n < 512 ? g_callstack_n : 512;
    for (int i = n - 2, k = 0; i >= 0 && k < 4; i--, k++) fprintf(stderr, "%s ", name_of(g_cs[i]));
    fputc('\n', stderr);
}

const char* rt_guest_cstr(u32 addr) { return (const char*)HP(addr); }

// ---- registros de hardware: por ahora solo se registran ----
static int g_mmio_log = 0;
u32 rt_mmio_read(u32 addr, u32 size) {
    if (audio_mmio_claims(addr)) return audio_mmio_read(addr, size);
    if (g_mmio_log++ < 60) rt_log("[mmio] lectura %u bytes de 0x%08X (%s)\n", size, addr, g_callstack_n ? "" : "");
    return 0;
}
void gx_fifo_write(const u8* data, u32 n);
void rt_mmio_write(u32 addr, u32 size, u32 value) {
    if (audio_mmio_claims(addr)) { audio_mmio_write(addr, size, value); return; }
    if (addr >= 0xCC008000 && addr < 0xCC008010) {   // FIFO de escritura de la GPU (stfd y psq_st escriben a +4): bytes en orden de memoria
        u8 b[4]; for (u32 i = 0; i < size; i++) b[i] = (u8)(value >> (8 * (size - 1 - i)));
        gx_fifo_write(b, size);
    }
}
