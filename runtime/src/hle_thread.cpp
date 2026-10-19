// Hilos del sistema operativo: el planificador del SDK esta elevado tal cual; solo se emula el cambio
// de contexto (OSSaveContext / OSLoadContext). Cada hilo del invitado corre en su propia pila del host.
#include "hle.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif

struct CtxRec { rt_jmpbuf buf; bool valid = false; int depth = 0; u32* cs = nullptr; void* fiber = nullptr; };
static std::map<u32, CtxRec*> g_ctxs;

rt_jmpbuf* rt_ctx_buf(u32 ctx, int depth) {
    CtxRec*& r = g_ctxs[ctx];
    if (!r) r = new CtxRec();
    r->valid = true; r->depth = depth; r->cs = g_cs;
#ifdef _WIN32
    r->fiber = GetCurrentFiber();                         // la pila (fibra) donde vive este contexto guardado
#endif
    return &r->buf;
}

extern const char* rt_name_of(u32 addr);
struct StartInfo { u32 entry, sp, r3, lr; };
static StartInfo g_start;

static void thread_main() {
    StartInfo st = g_start;
    g_cs = (u32*)calloc(512, 4); g_callstack_n = 0;
    g_sp = st.sp;
    RetRegs r = rt_call_indirect(st.entry, st.r3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!st.lr) rt_fatal("el hilo termino sin direccion de salida");
    rt_call_indirect(st.lr, r.r3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);     // OSExitThread
    rt_fatal("OSExitThread volvio");
}

// OSLoadContext (0x80189430): reanuda un hilo guardado o arranca uno nuevo; no vuelve.
#ifdef _WIN32
// Windows: cada hilo del invitado es una fibra. Un contexto guardado vive en la fibra que lo guardo; para reanudarlo se salta a esa fibra
// (SwitchToFiber) y alli, ya en su propia pila, se hace el rt_longjmp al punto donde se guardo.
static u32 g_resume_ctx = 0;
static void resume_saved_ctx() {                          // se ejecuta en la fibra duena del contexto g_resume_ctx
    CtxRec* r = g_ctxs[g_resume_ctx];
    g_callstack_n = r->depth; g_cs = r->cs;
    rt_longjmp(&r->buf, 1);
}
static VOID CALLBACK fiber_main(PVOID) { thread_main(); }
HLE_FN(OSLoadContext) {
    static bool converted = false;
    if (!converted) { converted = true; if (!IsThreadAFiber()) ConvertThreadToFiber(nullptr); }
    auto it = g_ctxs.find(a0);
    if (it != g_ctxs.end() && it->second->valid) {
        CtxRec* r = it->second;
        if (r->fiber == GetCurrentFiber()) { g_resume_ctx = a0; resume_saved_ctx(); }
        g_resume_ctx = a0;
        SwitchToFiber(r->fiber);
        resume_saved_ctx();                               // si alguien nos devuelve el control es para reanudar el contexto que pidio
    }
    g_start = { LD32(a0 + 0x198), LD32(a0 + 4), LD32(a0 + 0xC), LD32(a0 + 0x84) };
    if (getenv("RT_THREADLOG")) rt_log("[thread] nuevo contexto %08X entrada=%08X (%s) sp=%08X\n", a0, g_start.entry, rt_name_of(g_start.entry), g_start.sp);
    void* f = CreateFiber(2 << 20, fiber_main, nullptr);
    if (!f) rt_fatal("no se pudo crear la fibra del hilo");
    SwitchToFiber(f);                                     // el hilo nuevo corre; si volvemos aqui es para reanudar un contexto guardado
    resume_saved_ctx();
    return RET_NONE();
}
#else
HLE_FN(OSLoadContext) {
    auto it = g_ctxs.find(a0);
    if (it != g_ctxs.end() && it->second->valid) {
        CtxRec* r = it->second;
        g_callstack_n = r->depth; g_cs = r->cs;
        rt_longjmp(&r->buf, 1);
    }
    // contexto nuevo (creado por OSInitContext): campos estandar de OSContext
    g_start = { LD32(a0 + 0x198), LD32(a0 + 4), LD32(a0 + 0xC), LD32(a0 + 0x84) };
    if (getenv("RT_THREADLOG")) rt_log("[thread] nuevo contexto %08X entrada=%08X (%s) sp=%08X\n", a0, g_start.entry, rt_name_of(g_start.entry), g_start.sp);
    static std::vector<void*> stacks;
    const size_t sz = 1 << 20;
    void* stack = malloc(sz); stacks.push_back(stack);
    static ucontext_t uc;
    getcontext(&uc);
    uc.uc_stack.ss_sp = stack; uc.uc_stack.ss_size = sz; uc.uc_link = nullptr;
    makecontext(&uc, thread_main, 0);
    setcontext(&uc);
    return RET_NONE();
}
#endif

// Pilas de llamadas de todos los hilos suspendidos (diagnostico).
extern const char* rt_name_of(u32 addr);
void rt_backtrace_all() {
    for (auto& kv : g_ctxs) {
        CtxRec* r = kv.second; if (!r->valid || !r->cs) continue;
        fprintf(stderr, "  hilo (contexto 0x%08X), %d marcos:", kv.first, r->depth);
        for (int i = r->depth - 1, k = 0; i >= 0 && k < 6; i--, k++) fprintf(stderr, " %s", rt_name_of(r->cs[i]));
        fputc('\n', stderr);
    }
}
