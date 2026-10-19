#ifndef TYPES_H
#define TYPES_H

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef float f32;
typedef double f64;
typedef unsigned long long u64;
typedef signed long long s64;

struct Vec2 {
    f32 x, y;
};

struct Vec3 {
    f32 x, y, z;
};

// Allocador del juego (direcciones en game.dol: operator_new = 0x8015CB60,
// operator_delete = 0x8015D040). Con tamano/puntero unicamente.
void* operator new(unsigned long size);
// El allocator del juego PUEDE devolver nulo (0x8015CB60). El `operator new` de C++ no puede,
// y el compilador eliminaria las comprobaciones: para esos casos se llama a esta funcion.
extern "C" void* operator_new(unsigned long size);
void operator delete(void* ptr);

#endif
