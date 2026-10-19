#ifndef GUEST_MEM_H
#define GUEST_MEM_H

#include "types.h"

// Acceso a la memoria "del invitado" (direcciones de 32 bits de la Wii) desde el codigo
// decompilado automaticamente. Hoy son punteros directos (el overlay corre dentro del juego);
// el puerto nativo los redefinira para traducir a su arena de memoria.
static inline u32 LD8(u32 a)   { return *(volatile u8*)a; }
static inline u32 LD16(u32 a)  { return *(volatile u16*)a; }
static inline u32 LD16S(u32 a) { return (u32)(s32)*(volatile s16*)a; }
static inline u32 LD32(u32 a)  { return *(volatile u32*)a; }
static inline f32 LDF(u32 a)   { return *(volatile f32*)a; }
static inline f64 LDD(u32 a)   { return *(volatile f64*)a; }
static inline void ST8(u32 a, u32 v)  { *(volatile u8*)a = (u8)v; }
static inline void ST16(u32 a, u32 v) { *(volatile u16*)a = (u16)v; }
static inline void ST32(u32 a, u32 v) { *(volatile u32*)a = v; }
static inline void STF(u32 a, f32 v)  { *(volatile f32*)a = v; }
static inline void STD(u32 a, f64 v)  { *(volatile f64*)a = v; }

// Instrucciones PowerPC sin equivalente directo en C.
static inline u32 CNTLZW(u32 x) { return x == 0 ? 32 : (u32)__builtin_clz(x); }
static inline u32 SLW(u32 x, u32 n) { return (n & 0x20) ? 0 : x << (n & 0x1F); }
static inline u32 SRW(u32 x, u32 n) { return (n & 0x20) ? 0 : x >> (n & 0x1F); }

#endif
