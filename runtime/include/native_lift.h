#ifndef NATIVE_LIFT_H
#define NATIVE_LIFT_H

// Cabecera comun del runtime nativo: memoria del invitado (Wii, big-endian, direcciones de 32 bits),
// convenio de llamada entre funciones elevadas (RetRegs) y ayudantes de instrucciones PowerPC.
#include <cstdint>
#include <cstring>
#include <csetjmp>
#include "platform.h"

typedef uint8_t u8;   typedef int8_t s8;
typedef uint16_t u16; typedef int16_t s16;
typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64;
typedef float f32;    typedef double f64;

// ---- memoria del invitado ------------------------------------------------------------------
// Una sola reserva de 0x14000000 bytes: MEM1 (24 MB) en 0x0, MEM2 (64 MB) en 0x10000000 y la zona
// 0x0C000000 para los registros de hardware (0xCC000000). Las direcciones del invitado se reducen
// con `& 0x1FFFFFFF` (cubre el espejo cacheado 0x80000000/0x90000000 y el no cacheado 0xC0000000).
extern u8* g_mem;
extern u32 g_sp;                       // r1 del invitado entre llamadas

u32  rt_mmio_read(u32 addr, u32 size);
void rt_mmio_write(u32 addr, u32 size, u32 value);

// FIFO de GX (0xCC008000..F): ruta rapida inline. Las palabras se acumulan en un buffer (orden de memoria big-endian) y el interprete solo se
// invoca cuando hay bytes suficientes para completar el comando en curso (g_gxf_trig).
extern u8* g_gxf_buf; extern size_t g_gxf_end, g_gxf_cap, g_gxf_trig;
void gx_fifo_grow(); void gx_fifo_parse();
static inline bool IS_GXF(u32 a) { return (a & ~0xFu) == 0xCC008000u; }
static inline void GXF32(u32 v) {
    if (__builtin_expect(g_gxf_end + 4 > g_gxf_cap, 0)) gx_fifo_grow();
    u32 t = __builtin_bswap32(v); memcpy(g_gxf_buf + g_gxf_end, &t, 4); g_gxf_end += 4;
    if (g_gxf_end >= g_gxf_trig) gx_fifo_parse();
}

static inline u8* HP(u32 a) { return g_mem + (a & 0x1FFFFFFFu); }
static inline bool IS_MMIO(u32 a) { u32 t = a >> 24; return t == 0xCC || t == 0xCD; }

static inline u32 LD8(u32 a)   { return IS_MMIO(a) ? rt_mmio_read(a, 1) : *HP(a); }
static inline u32 LD16(u32 a)  { if (IS_MMIO(a)) return rt_mmio_read(a, 2); u16 v; memcpy(&v, HP(a), 2); return __builtin_bswap16(v); }
static inline u32 LD16S(u32 a) { return (u32)(s32)(s16)LD16(a); }
static inline u32 LD32(u32 a)  { if (IS_MMIO(a)) return rt_mmio_read(a, 4); u32 v; memcpy(&v, HP(a), 4); return __builtin_bswap32(v); }
static inline f32 LDF(u32 a)   { u32 v = LD32(a); f32 f; memcpy(&f, &v, 4); return f; }
static inline f64 LDD(u32 a)   { u64 v; memcpy(&v, HP(a), 8); v = __builtin_bswap64(v); f64 d; memcpy(&d, &v, 8); return d; }
static inline void ST8(u32 a, u32 v)  { if (IS_MMIO(a)) { rt_mmio_write(a, 1, v); return; } *HP(a) = (u8)v; }
static inline void ST16(u32 a, u32 v) { if (IS_MMIO(a)) { rt_mmio_write(a, 2, v); return; } u16 t = __builtin_bswap16((u16)v); memcpy(HP(a), &t, 2); }
static inline void ST32(u32 a, u32 v) { if (IS_MMIO(a)) { if (IS_GXF(a)) GXF32(v); else rt_mmio_write(a, 4, v); return; } u32 t = __builtin_bswap32(v); memcpy(HP(a), &t, 4); }
static inline void STF(u32 a, f32 f)  { u32 v; memcpy(&v, &f, 4); ST32(a, v); }
static inline void STD(u32 a, f64 d)  { if (IS_MMIO(a)) { u64 b; memcpy(&b, &d, 8); if (IS_GXF(a)) { GXF32((u32)(b >> 32)); GXF32((u32)b); return; } rt_mmio_write(a, 4, (u32)(b >> 32)); rt_mmio_write(a + 4, 4, (u32)b); return; }
                                         u64 v; memcpy(&v, &d, 8); v = __builtin_bswap64(v); memcpy(HP(a), &v, 8); }

// ---- convenio de llamada --------------------------------------------------------------------
// Toda funcion del invitado recibe r3-r10 y f1-f8 y devuelve (r3, r4, f1): asi no hace falta saber
// su prototipo y el llamador recoge lo que lea.
struct RetRegs { u32 r3; u32 r4; f64 f1; };
static inline RetRegs MKRET(u32 r3, u32 r4, f64 f1) { RetRegs r; r.r3 = r3; r.r4 = r4; r.f1 = f1; return r; }

#define NATIVE_PARAMS u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, f64 fa0, f64 fa1, f64 fa2, f64 fa3, f64 fa4, f64 fa5, f64 fa6, f64 fa7
#define NATIVE_ARGS_T u32, u32, u32, u32, u32, u32, u32, u32, f64, f64, f64, f64, f64, f64, f64, f64
#define NATIVE_REGS r3, r4, r5, r6, r7, r8, r9, r10, f1, f2, f3, f4, f5, f6, f7, f8

// Pila de llamadas "en la sombra" para diagnosticos (direcciones de las funciones activas).
extern u32* g_cs;                  // pila de llamadas del hilo activo (cada hilo del invitado tiene la suya)
extern int g_callstack_n;
extern bool g_profile;
void rt_profile_hit(u32 addr);
struct RtFrame {
    explicit RtFrame(u32 a) { if (g_callstack_n < 512) g_cs[g_callstack_n] = a; g_callstack_n++; if (g_profile) rt_profile_hit(a); }
    ~RtFrame() { g_callstack_n--; }
};

void rt_trace(u32 addr, u32 a0, u32 a1, u32 a2, u32 a3);
void rt_trace_ret(u32 addr, u32 r3);                            // (retorno de las funciones de trace.txt)       // runtime/trace.txt: funciones cuyas llamadas se imprimen
// Interrupciones del invitado (retrazado de VI...): un temporizador del host levanta g_irq_pending y se atiende
// en los bucles del codigo elevado (RT_POLL en las ramas hacia atras), como una interrupcion en un bucle de espera.
extern volatile int g_irq_pending;
void rt_service_irq();
// g_sp = r1 - 0x100: el manejador de la interrupcion apila por debajo del marco vivo de la funcion interrumpida.
#define RT_POLL() do { if (__builtin_expect(g_irq_pending, 0)) { g_sp = r1 - 0x100; rt_service_irq(); } } while (0)

RetRegs rt_call_indirect(u32 addr, NATIVE_PARAMS);
RetRegs rt_unimplemented(u32 addr);

#define CALLD(fn)  do { g_sp = r1; RetRegs rr_ = fn(NATIVE_REGS); r3 = rr_.r3; r4 = rr_.r4; f1 = rr_.f1; } while (0)
#define CALLI(tgt) do { g_sp = r1; RetRegs rr_ = rt_call_indirect((tgt), NATIVE_REGS); r3 = rr_.r3; r4 = rr_.r4; f1 = rr_.f1; } while (0)
#define TAILD(fn)  do { g_sp = r1; return fn(NATIVE_REGS); } while (0)
#define TAILI(tgt) do { g_sp = r1; return rt_call_indirect((tgt), NATIVE_REGS); } while (0)

// Registros de maquina/SPR/timebase que el codigo del SDK lee y escribe (ver runtime/src/hle_os.cpp).
u32  RT_MFMSR();
void RT_MTMSR(u32 v);
u32  RT_MFSPR(u32 spr);
void RT_MTSPR(u32 spr, u32 v);
u32  RT_MFTB(u32 tbr);

// OSSaveContext devuelve dos veces (como setjmp): 0 al guardar y 1 al reanudarse con OSLoadContext.
// Se expande en el marco del llamador para que el setjmp siga vivo mientras el hilo esta suspendido.
rt_jmpbuf* rt_ctx_buf(u32 ctx, int sp_depth);                 // (tambien guarda la pila de llamadas del hilo)
#define RT_SAVECTX(ctx, out) do { u32 c_ = (ctx); if (rt_setjmp(rt_ctx_buf(c_, g_callstack_n)) == 0) (out) = 0; else (out) = 1; } while (0)

// ---- instrucciones PowerPC ------------------------------------------------------------------
static inline u32 ROTL(u32 x, u32 n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static inline u32 CRSET(u32 cr, u32 k, u32 v) { u32 sh = 28 - 4 * k; return (cr & ~(0xFu << sh)) | (v << sh); }
static inline u32 CRBIT(u32 cr, u32 bi) { return (cr >> (31 - bi)) & 1; }
static inline u32 CRBITSET(u32 cr, u32 bi, u32 v) { u32 m = 0x80000000u >> bi; return v ? (cr | m) : (cr & ~m); }
static inline u32 CMPS(s32 a, s32 b) { return a < b ? 8 : (a > b ? 4 : 2); }
static inline u32 CMPU(u32 a, u32 b) { return a < b ? 8 : (a > b ? 4 : 2); }
static inline u32 CR0V(u32 v)        { s32 s = (s32)v; return s < 0 ? 8 : (s > 0 ? 4 : 2); }
static inline u32 FCMP(f64 a, f64 b) { return (a != a || b != b) ? 1 : (a < b ? 8 : (a > b ? 4 : 2)); }

static inline f64 FRSP(f64 x) { return (f64)(f32)x; }
static inline f64 F64FROMBITS(u64 b) { f64 d; memcpy(&d, &b, 8); return d; }
static inline u64 F64BITS(f64 d) { u64 b; memcpy(&b, &d, 8); return b; }
static inline f64 FCTIWZ(f64 x) {
    s32 v;
    if (x != x) v = (s32)0x80000000;
    else if (x >= 2147483648.0) v = 0x7FFFFFFF;
    else if (x < -2147483648.0) v = (s32)0x80000000;
    else v = (s32)x;
    return F64FROMBITS(0xFFF8000000000000ull | (u32)v);
}
static inline f64 FSEL(f64 a, f64 b, f64 c) { return a >= 0.0 ? b : c; }

static inline u32 CNTLZW(u32 x) { return x == 0 ? 32 : (u32)__builtin_clz(x); }
static inline u32 SLW(u32 x, u32 n) { return (n & 0x20) ? 0 : x << (n & 0x1F); }
static inline u32 SRW(u32 x, u32 n) { return (n & 0x20) ? 0 : x >> (n & 0x1F); }
static inline u32 MULHW(u32 a, u32 b)  { return (u32)(((s64)(s32)a * (s64)(s32)b) >> 32); }
static inline u32 MULHWU(u32 a, u32 b) { return (u32)(((u64)a * (u64)b) >> 32); }
static inline u32 DIVW(u32 a, u32 b)   { return (b == 0 || ((s32)a == (s32)0x80000000 && (s32)b == -1)) ? (u32)((s32)a >> 31) : (u32)((s32)a / (s32)b); }
static inline u32 DIVWU(u32 a, u32 b)  { return b == 0 ? 0 : a / b; }

static inline u32 ADDC(u32 a, u32 b, u32* ca)  { u64 t = (u64)a + b; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 ADDE(u32 a, u32 b, u32* ca)  { u64 t = (u64)a + b + *ca; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SUBFC(u32 a, u32 b, u32* ca) { u64 t = (u64)(u32)~a + b + 1; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SUBFE(u32 a, u32 b, u32* ca) { u64 t = (u64)(u32)~a + b + *ca; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SRAWI(u32 x, u32 n, u32* ca) { s32 s = (s32)x; *ca = (s < 0) && n && ((x & ((1u << n) - 1)) != 0); return (u32)(s >> n); }
static inline u32 SRAW(u32 x, u32 n, u32* ca)  { n &= 0x3F; if (n & 0x20) { *ca = (s32)x < 0; return (u32)((s32)x >> 31); } return SRAWI(x, n, ca); }
static inline void DCBZ(u32 a) { a &= ~31u; for (u32 i = 0; i < 32; i += 4) ST32(a + i, 0); }

// ---- pares simples: psq_l / psq_st con los registros de cuantificacion (GQR0-7 = SPR 912-919) ----
static inline f64 GQ_DEQ(u32 raw, u32 type, s32 scale) {
    f64 v;
    switch (type) { case 4: v = (u8)raw; break; case 5: v = (u16)raw; break; case 6: v = (s8)raw; break; case 7: v = (s16)raw; break; default: { u32 t = raw; f32 f; memcpy(&f, &t, 4); return (f64)f; } }
    return v * __builtin_ldexp(1.0, -scale);
}
static inline s32 GQ_SC(u32 g) { return (s32)((g & 0x3F) ^ 0x20) - 0x20; }
static inline void PSQ_L(u32 ea, u32 w, u32 gi, f64* p0, f64* p1) {
    u32 g = RT_MFSPR(912 + gi); u32 type = (g >> 16) & 7; s32 sc = GQ_SC(g >> 24);
    u32 sz = (type == 0) ? 4 : (type == 5 || type == 7) ? 2 : 1;
    u32 e0 = sz == 4 ? LD32(ea) : sz == 2 ? LD16(ea) : LD8(ea);
    *p0 = GQ_DEQ(e0, type, sc);
    if (w) { *p1 = 1.0; return; }
    u32 e1 = sz == 4 ? LD32(ea + 4) : sz == 2 ? LD16(ea + 2) : LD8(ea + 1);
    *p1 = GQ_DEQ(e1, type, sc);
}
static inline u32 GQ_ENQ(f64 v, u32 type, s32 scale) {
    if (type == 0) { f32 f = (f32)v; u32 t; memcpy(&t, &f, 4); return t; }
    v = v * __builtin_ldexp(1.0, scale);
    f64 lo, hi; switch (type) { case 4: lo = 0; hi = 255; break; case 5: lo = 0; hi = 65535; break; case 6: lo = -128; hi = 127; break; default: lo = -32768; hi = 32767; }
    if (v != v) v = 0; if (v < lo) v = lo; if (v > hi) v = hi;
    return (u32)(s32)v;
}
static inline void PSQ_ST(u32 ea, u32 w, u32 gi, f64 p0, f64 p1) {
    u32 g = RT_MFSPR(912 + gi); u32 type = g & 7; s32 sc = GQ_SC(g >> 8);
    u32 sz = (type == 0) ? 4 : (type == 5 || type == 7) ? 2 : 1;
    u32 a = GQ_ENQ(p0, type, sc), b = GQ_ENQ(p1, type, sc);
    if (sz == 4) { ST32(ea, a); if (!w) ST32(ea + 4, b); }
    else if (sz == 2) { ST16(ea, a); if (!w) ST16(ea + 2, b); }
    else { ST8(ea, a); if (!w) ST8(ea + 1, b); }
}

// __ptmf_scall (fn_80207628): r3 = this, r12 = &ptmf {delta, indice, func}; devuelve la direccion a llamar.
static inline u32 PTMF_TARGET(u32* self, u32 ptmf) {
    u32 delta = LD32(ptmf), index = LD32(ptmf + 4), func = LD32(ptmf + 8);
    *self += delta;
    if ((s32)index >= 0) { u32 vptr = LD32(*self + func); return LD32(vptr + index); }
    return func;
}

#endif
