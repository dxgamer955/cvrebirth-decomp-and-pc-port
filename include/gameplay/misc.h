#ifndef GAMEPLAY_MISC_H
#define GAMEPLAY_MISC_H

#include "gameplay/subweapon.h"

// Llamada virtual por offset crudo en la vtable de CodeWarrior (slot i en vptr+8+4i).
inline void** CwVtable(const void* obj) { return *(void***)obj; }

// Puntero a metodo miembro de CodeWarrior (__ptmf): {delta de this, indice de vtable o -1, funcion}.
// Si indice >= 0, `func` es el offset del vptr dentro del objeto y `index` el offset en la vtable.
struct Ptmf {
    s32 delta;
    s32 index;
    void* func;
};

inline void CallPtmf(void* self, const Ptmf* p) {
    typedef void (*Fn)(void*);
    u8* obj = (u8*)self + p->delta;
    Fn fn;
    if (p->index < 0) {
        fn = (Fn)p->func;
    } else {
        u8* vtable = *(u8**)(obj + (u32)p->func);
        fn = *(Fn*)(vtable + p->index);
    }
    fn(obj);
}

// Generador de ojos grandes (vtable 0x802499DC): su Update solo llama a un callback guardado.
class DrcBigEyeGenerator {
public:
    virtual ~DrcBigEyeGenerator();   // 0x80051770
    virtual void vf1();
    virtual void vf2();
    virtual void Update();           // 0x80051A80

    /* 0x04 */ u8 pad04[0xCC];
    /* 0xD0 */ u8 padD0[4];
    /* 0xD4 */ Ptmf m_callback;
};

// Animador de un valor de 8 bits (RTTI "DrcDamage", vtable 0x80253460; Update 0x800A6C60): tras
// m_delay frames sube m_cur hasta m_max, copia el valor de la curva a *m_out y termina.
class DrcDamage {
public:
    virtual ~DrcDamage();            // 0x800A70B0
    virtual void Update();           // 0x800A6C60

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminado
    /* 0x0C */ u8 pad0c[4];
    /* 0x10 */ void* m_curveVptr;    // subobjeto "curva" (solo su vptr importa aqui)
    /* 0x14 */ u8 pad14[4];
    /* 0x18 */ u32 m_cur;
    /* 0x1C */ u32 m_max;
    /* 0x20 */ u8 pad20[8];
    /* 0x28 */ u8* m_out;
    /* 0x2C */ s32 m_delay;
};

// Base de los enemigos con temporizador de aparicion (Mudman, BubbleEyeFire: Update 0x800AEEB0).
// Nombre provisional.
class DrcEnemySpawner {
public:
    virtual ~DrcEnemySpawner();
    virtual void vf1();
    virtual void vf2();
    virtual void Update();

    /* 0x04 */ u8 pad04[0x70];
    /* 0x74 */ void* m_sub;          // objeto con vptr y un contador en +8
    /* 0x78 */ u8 pad78[0x14];
    /* 0x8C */ s32 m_interval;
    /* 0x90 */ u32 m_jitter;
    /* 0x94 */ s32 m_timer;
    /* 0x98 */ u8 pad98[4];
    /* 0x9C */ u8 m_disabled;
};

extern "C" {
int fn_80017340(void* mgr);
u32 fn_80139770(void* rng);                  // entero aleatorio de 32 bits
extern void* lbl_80316460;
}

#endif
