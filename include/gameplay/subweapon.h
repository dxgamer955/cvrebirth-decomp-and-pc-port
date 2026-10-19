#ifndef GAMEPLAY_SUBWEAPON_H
#define GAMEPLAY_SUBWEAPON_H

#include "types.h"

// Objeto de golpe del arma (operator_new(0xA0), lo construye fn_800A44F0 junto con un
// ENDamaged y un EcoScratch). Solo se conoce m_hitFlag: se activa cuando el arma impacta.
struct HitObject {
    /* 0x00 */ u8 pad00[0x0C];
    /* 0x0C */ Vec3 m_vel;          // velocidad del golpe (la copia el boomerang cada frame)
    /* 0x18 */ u8 pad18[0x2C];
    /* 0x44 */ Vec2 m_pos;          // posicion del golpe (la fija el fuego del latigo cada frame)
    /* 0x4C */ u8 pad4C[8];
    /* 0x54 */ u8 m_active;         // 1 = recibe golpes
    /* 0x55 */ u8 pad55[0x47];
    /* 0x9C */ u8 m_hitFlag;
};

// Sprite del arma (operator_new(0x19C)): posicion en pantalla en +0x190/+0x194.
struct Sprite {
    /* 0x000 */ u8 pad000[0x178];
    /* 0x178 */ f32 m_f178;
    /* 0x17C */ f32 m_f17C;
    /* 0x180 */ u8 pad180[0x10];
    /* 0x190 */ f32 m_x;
    /* 0x194 */ f32 m_y;
};

// Base generica de las subarmas (vtable 0x80253AE4; ctor 0x800A70F0; Update 0x800A7180).
// Cuando el arma esta marcada como activa (+0x10C) y visible (+0x10D) crea un efecto de
// desvanecido (animacion + tareas de espera) y se marca como terminada.
class DrcSubWeaponBase {
public:
    virtual ~DrcSubWeaponBase();     // 0x800A9430
    virtual void vf1();              // 0x8013AF90
    virtual void vf2();              // 0x8013B070
    virtual void Update();

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;          // 2 = terminada
    /* 0x0C */ u8 pad0c[0x100];
    /* 0x10C */ u8 m_active;
    /* 0x10D */ u8 m_visible;
};

// Tarea de espera (operator_new(0x30)) que el juego inicializa en linea: dos vtables de
// CodeWarrior consecutivas, una lista enlazada (+0x10/+0x14) y un subobjeto en +0x18.
struct WaitTaskRaw {
    /* 0x00 */ u32 vptr;
    /* 0x04 */ u32 pad04;
    /* 0x08 */ s32 f08;
    /* 0x0C */ u8 f0C, f0D, f0E, f0F;
    /* 0x10 */ s32 f10;
    /* 0x14 */ void* f14;
    /* 0x18 */ u8 sub[0x18];
};

// Base comun de las subarmas arrojadizas (hacha, cruz...): todas se inicializan con
// fn_800A73D0 (hereda de DrcMotionWatch; un ENDamager en +0xC0). Nombre provisional.
// Solo se declaran los campos usados. m_state == 2 significa "terminada/destruir".
class DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponThrown();   // slot 0
    virtual void vf1();              // slot 1  0x8013AF90 (heredado)
    virtual void vf2();              // slot 2  0x8013B070 (heredado)
    virtual void Update();           // slot 3: logica por frame (la define cada arma)

    /* 0x04 */ u8 pad04[4];
    /* 0x08 */ s32 m_state;
    /* 0x0C */ u8 pad0c[0x6C];
    /* 0x78 */ Vec3 m_pos;
    /* 0x84 */ Vec3 m_vel;
    /* 0x90 */ f32 m_dir;           // signo = direccion a la que mira (< 0 = izquierda)
    /* 0x94 */ u8 pad94[0x3C];
    /* 0xD0 */ void* m_aux;        // el fuego del latigo guarda aqui su CTMapTerrain
    /* 0xD4 */ Sprite* m_sprite;
    /* 0xD8 */ HitObject* m_hit;
};

// Hacha (vtable 0x80253974; ctor 0x800A7960; Update 0x800A7A80).
class DrcSubWeaponAxe : public DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponAxe();      // 0x800A94A0
    virtual void Update();
};

// Cruz / boomerang con gravedad (vtable 0x802538BC; ctor 0x800A7B70; Update 0x800A7C90).
class DrcSubWeaponCross : public DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponCross();    // 0x800A9570
    virtual void Update();
};

// Reloj: boomerang que sale, se frena y vuelve hacia el jugador (vtable 0x80253800; ctor
// 0x800A7DA0; Update 0x800A7ED0). Suelta particulas cada 4 frames; termina al llegar a
// menos de 16x20 pixeles del jugador o al salir de pantalla.
class DrcSubWeaponWatch : public DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponWatch();    // 0x800A9640
    virtual void Update();

    /* 0xDC */ u8 padDC[8];
    /* 0xE4 */ s32 m_homingDelay;    // frames antes de empezar a perseguir
    /* 0xE8 */ s32 m_particleTimer;  // cuenta atras para la siguiente particula
};

// Agua bendita (ctor/Update en 0x800A84C0): maquina de estados en +0xE0. 0 = sonido y
// objeto inicial; 1 = espera (cada 60 frames; a los 240 pasa de fase); 2 y 100 = desvanece el
// sprite restando 8 de opacidad por frame hasta terminar.
class DrcSubWeaponHolyWater : public DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponHolyWater();   // 0x800A83D0
    virtual void Update();

    /* 0xDC */ u8 padDC[4];
    /* 0xE0 */ s32 m_phase;
    /* 0xE4 */ u8 padE4[4];
    /* 0xE8 */ s32 m_timer;
    /* 0xEC */ u8 padEC[4];
    /* 0xF0 */ u8 m_flag;
};

// Posicion (x, y) del objeto: metodo virtual en el slot +0x30 de la vtable de CodeWarrior,
// devuelve el Vec2 empaquetado en r3:r4 (struct de 8 bytes devuelto en registros).
inline Vec2 GetTrackedPos(void* obj) {
    typedef u64 (*Fn)(void*);
    void** vtable = *(void***)obj;
    Fn fn = (Fn)vtable[0x30 / 4];
    u64 r = fn(obj);
    u32 hi = (u32)(r >> 32), lo = (u32)r;
    Vec2 v;
    __builtin_memcpy(&v.x, &hi, 4);
    __builtin_memcpy(&v.y, &lo, 4);
    return v;
}

// Resultado de fn_800084C0 (trazado de un segmento contra el terreno): f1 = signo/indicador,
// f3 = distancia de empuje. Los demas campos no se usan aqui.
struct TerrainHit {
    f32 f0, f1, f2, f3;
};

// Terreno del mapa (operator_new(0x54), lo construye CTMapTerrain_ctor). Solo +4 y +8.
struct CTMapTerrain {
    /* 0x00 */ u8 pad00[4];
    /* 0x04 */ f32 m_a;
    /* 0x08 */ f32 m_b;
};

// Fuego del latigo (vtable 0x80253684; ctor 0x800A8680; Update 0x800A8870). Avanza en linea
// recta mientras se frena su velocidad vertical; si el trazado contra el terreno choca se
// "pega" (m_flag) y se desvanece/ajusta por fases (0 = vuelo, 1 = pegado).
class DrcSubWeaponWhipFire : public DrcSubWeaponThrown {
public:
    virtual ~DrcSubWeaponWhipFire();   // 0x800A8790
    virtual void Update();

    /* 0xDC */ u8 padDC[4];
    /* 0xE0 */ s32 m_phase;
    /* 0xE4 */ u8 padE4[0xC];
    /* 0xF0 */ u8 m_flag;
};

// Funciones del juego aun sin decompilar (se enlazan a su direccion en game.dol).
extern "C" {
// true si pos (x,y) +- margin esta dentro del rectangulo visible de la camara (160x120 +- ...).
int fn_80009070(f32 margin, void* camera, const Vec3* pos);
// fija la posicion en pantalla del sprite
void fn_801280B0(f32 x, f32 y, Sprite* sprite);
// reproduce un sonido por nombre
void fn_800B6740(void* soundObj, const char* name);
extern void* lbl_8031647C;    // objeto global que contiene la camara
extern void* lbl_80316544;    // objeto global de sonido
extern f32 lbl_80318848;      // margen de visibilidad (64.0f)
extern f32 lbl_80318828;      // umbral de velocidad vertical de la cruz
extern f32 lbl_80318850;      // aceleracion (gravedad) de la cruz por frame
extern char lbl_80315424[];   // sonido al descartar el hacha
extern char lbl_8031542C[];   // sonido al impactar la cruz
// --- base ---
void* ValAnime_unsigned_char_ctor(void* mem);
void* fn_8013A4D0(void* anime);
void* fn_800A6410(void* anime, s32 a, s32 b, s32 c);
void fn_8018E910(void* sub);
void fn_8013A800(void* task, void* child);
extern char lbl_80315408[];
extern char lbl_8024570C[], lbl_8025F0E4[];
// --- fuego del latigo ---
int fn_800084C0(void* terrainObj, const Vec2* from, const Vec2* to, TerrainHit* out);
void fn_800A4D00(HitObject* hit);
void fn_8002EE40(Sprite* sprite, const char* name, int arg);
int fn_80127D90(Sprite* sprite);               // sprite sigue vivo
s32 fn_80133A50(Sprite* sprite);               // fotograma actual
f32 fn_80127EE0(Sprite* sprite);               // duracion de la animacion
f64 fn_80216698(f64 x);                        // raiz cuadrada
CTMapTerrain* CTMapTerrain_ctor(void* mem, void* owner);
extern s32 lbl_80316528;      // contador global de fuegos pegados
extern f32 lbl_802439C0[];    // tabla; [6..8] = velocidad al pegarse
extern char lbl_8031544C[], lbl_80315450[], lbl_80315454[], lbl_8025359C[];
extern f32 lbl_80318840;
extern f32 lbl_80318870;      // paso de frenado de la velocidad vertical por frame
extern f32 lbl_8031885C;
// --- agua bendita ---
void fn_800B68B0(void* soundObj, f32 value);
void fn_8001CB20(void* obj, int arg);
u32 fn_80128330(Sprite* sprite);               // opacidad actual del sprite
void fn_80128320(Sprite* sprite, u32 alpha);   // fija la opacidad
// --- reloj ---
f32 fn_801398F0(void* rng);                                      // numero aleatorio en [0,1) (float)
void fn_800AE870(f32 scale, void* obj, const char* classInfo, const char* name, const Vec2* pos);
void* fn_80207650(void* mgr, int a, const void* b, const void* c, int d);   // busca un objeto
extern void* lbl_80316460;    // generador aleatorio global
extern char lbl_8031543C[];   // sonido al impactar el reloj
extern char lbl_80253530[];   // informacion de clase de la particula
extern char lbl_80315410[];   // nombre de la particula
extern char lbl_80313A20[];
extern char lbl_803137D8[];
extern f32 lbl_80318824;      // velocidad horizontal objetivo hacia la izquierda (4.0 segun el codigo)
extern f32 lbl_8031884C;      // velocidad horizontal objetivo (4.0 / -4.0)
extern f32 lbl_80318830;      // escala de la particula (2.0)
extern f32 lbl_80318854;      // desplazamiento minimo de la particula (-8.0)
extern f32 lbl_80318858;      // rango del desplazamiento y umbral de llegada en x (16.0)
extern f32 lbl_8031885C;      // umbral de llegada en y / desplazamiento de objetivo (20.0)
extern f32 lbl_80318860;      // aceleracion maxima por frame (0.1)
extern f32 lbl_80318864;      // +1.0
extern f32 lbl_80318868;      // -1.0
}

#endif
