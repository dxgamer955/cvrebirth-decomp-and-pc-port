#ifndef GAMEPLAY_ENEMY_H
#define GAMEPLAY_ENEMY_H

#include "gameplay/misc.h"
#include "gameplay/bat.h"      // SpriteTaskRaw, fn_8000B4A0...

// Murcielago pequeno (RTTI "DrcEnemyBat2", vtable 0x8024973C; Update 0x8004FF20): solo
// sincroniza la opacidad de su sprite con un valor del gestor.
class DrcEnemyBat2 {
public:
    virtual ~DrcEnemyBat2();         // 0x80050940
    virtual void Update();           // 0x8004FF20

    /* 0x04 */ u8 pad04[0xC];
    /* 0x10 */ Sprite* m_sprite;
};

// Cuerpo de colision del Dokuro (calavera): solo los campos que lee DrcDokuroShot.
struct DokuroBody {
    /* 0x00 */ u8 pad00[0x10];
    /* 0x10 */ u8 m_dying;           // desvanecer y terminar
    /* 0x11 */ u8 pad11[7];
    /* 0x18 */ Vec2 m_pos;
    /* 0x20 */ u8 pad20[0x25];
    /* 0x45 */ u8 m_hit;
};

// Proyectil de calavera (vtable 0x8024B910; Update 0x80062630): fases 0 espera a que acabe la
// animacion de aparicion, 1 vuela (frena m_speed hacia un objetivo) hasta chocar, 2 gira m_angle
// hasta cruzar un multiplo de 120 grados, 3 espera a que acabe la animacion final.
class DrcDokuroShot {
public:
    virtual ~DrcDokuroShot();        // 0x80062580
    virtual void vf1();
    virtual void vf2();
    virtual void Update();

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminado
    /* 0x0C */ u8 pad0c[0xB4];
    /* 0xC0 */ DokuroBody* m_body;
    /* 0xC4 */ Sprite* m_sprite;
    /* 0xC8 */ u8 padC8[4];
    /* 0xCC */ s32 m_phase;
    /* 0xD0 */ u8 padD0[8];
    /* 0xD8 */ f32 m_angle;
    /* 0xDC */ f32 m_speed;
};

// Cabeza de Medusa (RTTI "DrcMedousaGenerator", vtable 0x8024F3D0; Update 0x80082FE0): vuela en
// onda senoidal; al recibir dano (m_damage) pierde vida, parpadea m_invuln frames y, sin vida,
// explota y deja una tarea que borra el sprite.
class DrcMedousaGenerator {
public:
    virtual ~DrcMedousaGenerator();  // 0x80082DF0
    virtual void vf1();
    virtual void vf2();
    virtual void Update();

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminado
    /* 0x0C */ u8 pad0c[4];
    /* 0x10 */ u8 m_tasks[0x68];     // lista de tareas del objeto (fn_8013A800)
    /* 0x78 */ Vec3 m_pos;
    /* 0x84 */ Vec3 m_vel;
    /* 0x90 */ u8 pad90[0x4C];
    /* 0xDC */ s32 m_invuln;
    /* 0xE0 */ f32 m_hp;
    /* 0xE4 */ f32 m_damage;
    /* 0xE8 */ f32 m_amp;
    /* 0xEC */ f32 m_phaseStep;
    /* 0xF0 */ f32 m_phase;
    /* 0xF4 */ Sprite* m_sprite;
    /* 0xF8 */ HitObject* m_hit;
    /* 0xFC */ void* m_flash;
    /* 0x100 */ u8 m_frozen;
    /* 0x101 */ u8 m_big;
    /* 0x102 */ u8 pad102[2];
    /* 0x104 */ s32 m_effectArg;
};

// Cuerpo fisico del Houshi: solo el flag de "en el suelo".
struct HoushiBody {
    /* 0x00 */ u8 pad00[0x20];
    /* 0x20 */ u8 m_grounded;
};

// Esporas (RTTI "DrcEnemyHoushi", vtable 0x8024F1D4; Update 0x80081570): cae con gravedad, y
// cada cierto tiempo cicla animaciones (fase 0 espera, 1-2 disparo, 3 vuelta, 5 muriendo).
class DrcEnemyHoushi {
public:
    virtual ~DrcEnemyHoushi();       // 0x80081370
    virtual void vf1();
    virtual void vf2();
    virtual void Update();

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminado
    /* 0x0C */ u8 pad0c[0x6C];
    /* 0x78 */ Vec3 m_pos;
    /* 0x84 */ u8 pad84[0x18];
    /* 0x9C */ Vec3 m_vel;
    /* 0xA8 */ u8 padA8[0x2C];
    /* 0xD4 */ s32 m_phase;
    /* 0xD8 */ f32 m_damage;
    /* 0xDC */ s32 m_timer;
    /* 0xE0 */ f32 m_hp;
    /* 0xE4 */ Sprite* m_sprite;
    /* 0xE8 */ HitObject* m_hit;
    /* 0xEC */ u8 padEC[4];
    /* 0xF0 */ HoushiBody* m_body;
    /* 0xF4 */ u8 m_dead;
    /* 0xF5 */ u8 padF5[3];
    /* 0xF8 */ s32 m_effectArg;
};

// Cuerpo del Yokokama: vida (+0x2C), flags y posicion/direccion que sincroniza con el enemigo.
struct YokoBody {
    /* 0x00 */ u8 pad00[0x10];
    /* 0x10 */ u8 m_dying;
    /* 0x11 */ u8 pad11[7];
    /* 0x18 */ Vec2 m_pos;
    /* 0x20 */ Vec3 m_dir;
    /* 0x2C */ f32 m_hp;
    /* 0x30 */ u8 pad30[0x16];
    /* 0x46 */ u8 m_hurt;
};

// Tarea con un valor float en +0x10 (hermana de SpriteTaskRaw).
struct FloatTaskRaw {
    /* 0x00 */ u32 vptr;
    /* 0x04 */ u32 pad04;
    /* 0x08 */ s32 f08;
    /* 0x0C */ u8 f0C;
    /* 0x0D */ u8 pad0D[3];
    /* 0x10 */ f32 value;
};

// Efecto Yokokama (RTTI "DrcYokokamaEffect", vtable 0x8024BA20; Update 0x8005FA20): enemigo que
// flota con un balanceo senoidal, recibe dano (m_damage -> vida del cuerpo), parpadea y al morir
// deja varias tareas.
class DrcYokokamaEffect {
public:
    virtual ~DrcYokokamaEffect();    // 0x8005F910
    virtual void vf1();
    virtual void vf2();
    virtual void Update();

    /* 0x04 */ u8 pad04[0xC];
    /* 0x10 */ u8 m_tasks[0x68];
    /* 0x78 */ Vec3 m_pos;
    /* 0x84 */ u8 pad84[0xC];
    /* 0x90 */ Vec3 m_dir;
    /* 0x9C */ Vec3 m_vel;
    /* 0xA8 */ u8 padA8[0x24];
    /* 0xCC */ Ptmf m_callback;
    /* 0xD8 */ u8 padD8[4];
    /* 0xDC */ f32 m_damage;
    /* 0xE0 */ s32 m_invuln;
    /* 0xE4 */ s32 m_dead;
    /* 0xE8 */ u8 padE8[0xC];
    /* 0xF4 */ f32 m_wobble;
    /* 0xF8 */ u8 padF8[4];
    /* 0xFC */ s32 m_lowHp;
    /* 0x100 */ u8 pad100[8];
    /* 0x108 */ f32 m_maxHp;
    /* 0x10C */ u8 pad10C[0x18];
    /* 0x124 */ f32 m_f124;
    /* 0x128 */ f32 m_f128;
    /* 0x12C */ HitObject* m_hit;
    /* 0x130 */ Sprite* m_sprite;
    /* 0x134 */ u8 pad134[4];
    /* 0x138 */ void* m_flash;
    /* 0x13C */ u8 pad13C[4];
    /* 0x140 */ YokoBody* m_body;
    /* 0x144 */ void* m_sub;
    /* 0x148 */ u8 pad148[0x11];
    /* 0x159 */ u8 m_turns;
    /* 0x15A */ u8 pad15A[2];
    /* 0x15C */ s32 m_effectArg;
};
typedef char check_yoko_body[(__builtin_offsetof(DrcYokokamaEffect, m_body) == 0x140) ? 1 : -1];
typedef char check_yoko_arg[(__builtin_offsetof(DrcYokokamaEffect, m_effectArg) == 0x15C) ? 1 : -1];
typedef char check_yoko_cb[(__builtin_offsetof(DrcYokokamaEffect, m_callback) == 0xCC) ? 1 : -1];
typedef char check_yoko_f124[(__builtin_offsetof(DrcYokokamaEffect, m_f124) == 0x124) ? 1 : -1];

extern "C" {
int fn_800B42E0();
void fn_8002EE50(Sprite* sprite, const char* a, const char* b, int c);
extern f32 lbl_80317898, lbl_8031789C, lbl_803178A0, lbl_803178A4, lbl_803178A8, lbl_803178AC;
extern char lbl_8024B0B0[], lbl_8024B0C4[], lbl_80314694[], lbl_80314698[], lbl_803146A0[], lbl_8025F120[], lbl_802456B4[];
void fn_8002EE20(Sprite* sprite, int a, int b);
extern f32 lbl_80317DE8, lbl_80317DEC, lbl_80317E00;
extern char lbl_8024F100[], lbl_80314C64[], lbl_80314C6C[];
extern f32 lbl_802439C0[];
void* EffectTaskSet_ctor(void* mem, const Vec2* pos);
void fn_80043160(void* self, const Vec2* v);
void fn_80043810(void* flash, u32 color, u32 a, u32 b);
void fn_80128160(Sprite* sprite, int flag, int arg);
int fn_8001AD70(void* mgr, const Vec3* pos, const Vec2* limits);
extern f32 lbl_80317E34, lbl_80317E28, lbl_80317E5C, lbl_80317E60, lbl_80317E44, lbl_80317E2C, lbl_80317E64;
extern char lbl_80314CC0[], lbl_80314CC4[], lbl_8024F2CC[], lbl_8024F2B8[];
u32 fn_80014A90(void* mgr);
void fn_80128230(f32 value, Sprite* sprite);
void fn_801280F0(Sprite* sprite, const Vec2* v);
extern f32 lbl_8031787C;      // velocidad objetivo
extern f32 lbl_8031793C;      // paso de frenado por frame
extern f32 lbl_80317878;      // 0.0
extern char lbl_8024B260[];
}

#endif
