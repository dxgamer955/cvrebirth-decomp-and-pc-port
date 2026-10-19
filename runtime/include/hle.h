#pragma once
// Sustitutos escritos a mano de las funciones del SDK de Wii (y de lo que no se puede elevar).
#include "native_lift.h"

struct FuncEntry { u32 addr; RetRegs (*fn)(NATIVE_PARAMS); const char* name; };
extern const FuncEntry g_functable[];
extern const unsigned g_functable_count;

// Una funcion sustituida se declara con HLE_FN(NombreDelSimbolo) en cualquier runtime/src/*.cpp;
// decomp/tools/lift_native.py la enlaza con la direccion del simbolo y no eleva el original.
#define HLE_FN(name) RetRegs HLE_##name(NATIVE_PARAMS)

static inline RetRegs RET_NONE()      { return MKRET(0, 0, 0.0); }
static inline RetRegs RET_I(u32 v)    { return MKRET(v, 0, 0.0); }
static inline RetRegs RET_L(u64 v)    { return MKRET((u32)(v >> 32), (u32)v, 0.0); }
static inline RetRegs RET_F(f64 v)    { return MKRET(0, 0, v); }

RetRegs rt_unimplemented_named(u32 addr, const char* name, const char* why);

// ---- utilidades del runtime ----
void rt_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void rt_fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_backtrace(int max = 14);                         // imprime la pila de llamadas elevadas
const char* rt_guest_cstr(u32 addr);                    // cadena C del invitado (puntero valido mientras no se escriba)
void rt_load_dol(const char* path);
void rt_init_memory();
u32  rt_symbol(const char* name);                       // direccion de un simbolo de game.dol (0 si no existe)
