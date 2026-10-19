#ifndef GAMEPLAY_BAT_H
#define GAMEPLAY_BAT_H

#include "gameplay/subweapon.h"   // Sprite, HitObject, fn_80009070...

// Murcielago que vuela en onda senoidal (vtable 0x802495E4, RTTI "DrcBatGenerator"; Update
// 0x8004FC10). Mientras vive avanza en x y oscila en y; cuando m_dying pasa el umbral
// reproduce el efecto de muerte y deja una tarea que borra el sprite.
class DrcBatGenerator {
public:
    virtual ~DrcBatGenerator();      // 0x8004FBA0
    virtual void vf1();              // 0x8013AF90
    virtual void vf2();              // 0x8013B070
    virtual void Update();

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminado
    /* 0x0C */ u8 pad0c[4];
    /* 0x10 */ u8 m_tasks[0x68];     // lista de tareas del objeto (fn_8013A800)
    /* 0x78 */ Vec3 m_pos;
    /* 0x84 */ Vec3 m_vel;
    /* 0x90 */ u8 pad90[0x4C];
    /* 0xDC */ f32 m_dying;          // > umbral: muriendo
    /* 0xE0 */ u32 m_angle;          // fase de la onda (sin signo); +4 por frame
    /* 0xE4 */ f32 m_e4;
    /* 0xE8 */ s32 m_e8;
    /* 0xEC */ u8 m_frozen;          // no se mueve
    /* 0xED */ u8 padED[3];
    /* 0xF0 */ Sprite* m_sprite;
    /* 0xF4 */ HitObject* m_hit;
};

// Tarea que borra el sprite al terminar (operator_new(0x14), inicializada en linea).
struct SpriteTaskRaw {
    /* 0x00 */ u32 vptr;
    /* 0x04 */ u32 pad04;
    /* 0x08 */ s32 f08;
    /* 0x0C */ u8 f0C;
    /* 0x0D */ u8 pad0D[3];
    /* 0x10 */ void* sprite;
};

extern "C" {
void fn_8000B4A0(void* obj, const char* name, s32 arg);
void fn_80128340(f32 value);
void fn_80043080(void* self, const Vec2* v);
void fn_800A4CF0(HitObject* hit);
f64 fn_802162F4(f64 x);                       // seno
extern void* lbl_80316480;
extern char lbl_80249570[], lbl_80314440[], lbl_80245678[];
extern f32 lbl_803174D0;      // 0.0
extern f32 lbl_803174DC;
extern f32 lbl_803174F4;
extern f32 lbl_803174F8;
extern f32 lbl_803174FC;      // margen de visibilidad
}

#endif
