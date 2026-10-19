#pragma once
// Capa de plataforma: saltos no locales para el cambio de contexto de los hilos del invitado (OSSaveContext / OSLoadContext).
// Linux: setjmp/longjmp de libc. Windows: versiones propias en ensamblador (el longjmp de la CRT de Windows desenrolla la pila con SEH
// y falla al saltar entre las pilas de distintos hilos del invitado), mas fibras de Windows para tener una pila por hilo.
#include <csetjmp>

#ifdef _WIN32
struct rt_jmpbuf { unsigned long long r[32]; };                      // rbx rbp rdi rsi rsp r12-r15 rip mxcsr + xmm6-15
extern "C" int  rt_setjmp(rt_jmpbuf*) __attribute__((returns_twice));
extern "C" void rt_longjmp(rt_jmpbuf*, int) __attribute__((noreturn));
#else
typedef jmp_buf rt_jmpbuf;
#define rt_setjmp(b) _setjmp(*(b))
#define rt_longjmp(b, v) _longjmp(*(b), (v))
#endif
