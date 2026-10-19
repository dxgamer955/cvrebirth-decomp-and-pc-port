#ifndef PPC_LIFT_H
#define PPC_LIFT_H

// Ayudantes para el codigo generado por decomp/tools/lift.py (elevacion de PowerPC a C++).
// Cada ayudante reproduce exactamente una instruccion (o un patron) del Broadway.
#include "guest_mem.h"

static inline u32 ROTL(u32 x, u32 n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static inline u32 MASKMB(u32 mb, u32 me) {            // mascara de rlwinm (con vuelta cuando mb > me)
    u32 a = 0xFFFFFFFFu >> mb, b = 0xFFFFFFFFu << (31 - me);
    return mb <= me ? (a & b) : (a | b);
}
static inline u32 CRSET(u32 cr, u32 k, u32 v) { u32 sh = 28 - 4 * k; return (cr & ~(0xFu << sh)) | (v << sh); }
static inline u32 CRBIT(u32 cr, u32 bi) { return (cr >> (31 - bi)) & 1; }
static inline u32 CRBITSET(u32 cr, u32 bi, u32 v) { u32 m = 0x80000000u >> bi; return v ? (cr | m) : (cr & ~m); }
static inline u32 CMPS(s32 a, s32 b) { return a < b ? 8 : (a > b ? 4 : 2); }
static inline u32 CMPU(u32 a, u32 b) { return a < b ? 8 : (a > b ? 4 : 2); }
static inline u32 CR0V(u32 v)        { s32 s = (s32)v; return s < 0 ? 8 : (s > 0 ? 4 : 2); }
static inline u32 FCMP(f64 a, f64 b) { return (a != a || b != b) ? 1 : (a < b ? 8 : (a > b ? 4 : 2)); }

static inline f64 FRSP(f64 x) { return (f64)(f32)x; }
static inline f64 F64FROMBITS(u64 b) { f64 d; __builtin_memcpy(&d, &b, 8); return d; }
static inline u64 F64BITS(f64 d) { u64 b; __builtin_memcpy(&b, &d, 8); return b; }
// fctiwz: la palabra baja del registro recibe el entero (saturado); la alta es 0xFFF80000 en el emulador.
static inline f64 FCTIWZ(f64 x) {
    s32 v;
    if (x != x) v = (s32)0x80000000;
    else if (x >= 2147483648.0) v = 0x7FFFFFFF;
    else if (x < -2147483648.0) v = (s32)0x80000000;
    else v = (s32)x;
    return F64FROMBITS(0xFFF8000000000000ull | (u32)v);
}
static inline f64 FSEL(f64 a, f64 b, f64 c) { return a >= 0.0 ? b : c; }

static inline u32 MULHW(u32 a, u32 b)  { return (u32)(((s64)(s32)a * (s64)(s32)b) >> 32); }
static inline u32 MULHWU(u32 a, u32 b) { return (u32)(((u64)a * (u64)b) >> 32); }
static inline u32 DIVW(u32 a, u32 b)   { return (b == 0 || ((s32)a == (s32)0x80000000 && (s32)b == -1)) ? (u32)((s32)a >> 31) : (u32)((s32)a / (s32)b); }
static inline u32 DIVWU(u32 a, u32 b)  { return b == 0 ? 0 : a / b; }

static inline u32 ADDC(u32 a, u32 b, u32* ca)         { u64 t = (u64)a + b; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 ADDE(u32 a, u32 b, u32* ca)         { u64 t = (u64)a + b + *ca; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SUBFC(u32 a, u32 b, u32* ca)        { u64 t = (u64)(u32)~a + b + 1; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SUBFE(u32 a, u32 b, u32* ca)        { u64 t = (u64)(u32)~a + b + *ca; *ca = (u32)(t >> 32); return (u32)t; }
static inline u32 SRAWI(u32 x, u32 n, u32* ca)        { s32 s = (s32)x; *ca = (s < 0) && n && ((x & ((1u << n) - 1)) != 0); return (u32)(s >> n); }
static inline u32 SRAW(u32 x, u32 n, u32* ca)         { n &= 0x3F; if (n & 0x20) { *ca = (s32)x < 0; return (u32)((s32)x >> 31); } return SRAWI(x, n, ca); }

static inline void DCBZ(u32 a) { a &= ~31u; for (u32 i = 0; i < 32; i += 4) ST32(a + i, 0); }

// Llamada a un puntero a metodo miembro de CodeWarrior (__ptmf_scall, fn_80207628): r3 = this, r12 = &ptmf.
static inline u32 PTMF_TARGET(u32* self, u32 ptmf) {
    u32 delta = LD32(ptmf), index = LD32(ptmf + 4), func = LD32(ptmf + 8);
    *self += delta;
    if ((s32)index >= 0) { u32 vptr = LD32(*self + func); return LD32(vptr + index); }
    return func;
}

#endif
