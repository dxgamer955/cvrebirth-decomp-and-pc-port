#ifndef ENGINE_COLLISION_H
#define ENGINE_COLLISION_H

#include "types.h"

// Miembro tipo array {datos, tamano, capacidad}; su destructor libera los datos.
struct ArrayHdr {
    void* data;
    s32 size;
    s32 capacity;
    ~ArrayHdr();
};

class EcoScratch;

// Cuerpo "danable". Lista intrusiva doble por grupo (0..3) en g_enDamagedHead.
// Tamano 0x4C. Los slots 1..3 son virtuales puros en la base (vtable 0x80243A24).
class ENDamaged {
public:
    ENDamaged(s32 group);
    virtual ~ENDamaged();            // slot 0  0x800078D0
    virtual void Exec() = 0;         // slot 1 (+0xC)  nombre provisional: se llama cada frame si activo
    virtual void vf2() = 0;          // slot 2 (+0x10)
    virtual void OnHit() = 0;        // slot 3 (+0x14) nombre provisional: reaccion al golpe
    virtual void vf4();              // slot 4  0x80007B70 (vacio)

    /* 0x04 */ s32 m_group;
    /* 0x08 */ u8 m_active;
    /* 0x0C */ Vec3 m_pos;
    /* 0x18 */ f32 m_scale;
    /* 0x1C */ ArrayHdr m_arr1;
    /* 0x28 */ ArrayHdr m_arr2;
    /* 0x34 */ s32 m_x34;
    /* 0x38 */ s32 m_x38;
    /* 0x3C */ ENDamaged* m_prev;
    /* 0x40 */ ENDamaged* m_next;
    /* 0x44 */ f32 m_x44;
    /* 0x48 */ f32 m_x48;
};

// Zona de golpe. Lista intrusiva doble por grupo (0..3) en g_ecoScratchHead.
// Tamano 0x14. Vtable 0x80243A00.
class EcoScratch {
public:
    EcoScratch(s32 group);
    virtual ~EcoScratch();           // slot 0  0x80007AC0
    virtual void Exec() = 0;         // slot 1 (+0xC)
    virtual void vf2() = 0;          // slot 2 (+0x10)
    virtual bool Hit(ENDamaged* target) = 0;  // slot 3 (+0x14) nombre provisional

    /* 0x04 */ s32 m_group;
    /* 0x08 */ u8 m_active;
    /* 0x0C */ EcoScratch* m_prev;
    /* 0x10 */ EcoScratch* m_next;
};

// Lista global unica de "damager" (vtable 0x80243A4C). Cabeza en 0x80316474.
class ENDamager {
public:
    ENDamager();
    virtual ~ENDamager();            // slot 0  0x80007710
    virtual void vf1() = 0;
    virtual void vf2() = 0;
    virtual void vf3() = 0;
    virtual void vf4();              // slot 4  0x80007B80: salta a vf1

    /* 0x04 */ u8 m_x04;
    /* 0x08 */ ENDamager* m_prev;
    /* 0x0C */ ENDamager* m_next;
};

// Lista global unica de "restricted" (vtable 0x80243A78). Cabeza en 0x80316470.
class ENRestricted {
public:
    ENRestricted();
    virtual ~ENRestricted();         // slot 0  0x80007610

    /* 0x04 */ u8 m_x04;
    /* 0x08 */ Vec3 m_vec;
    /* 0x14 */ ENRestricted* m_prev;
    /* 0x18 */ ENRestricted* m_next;
};

extern ENDamaged* g_enDamagedHead[4];     // 0x802845C0
extern EcoScratch* g_ecoScratchHead[4];   // 0x802845D0
extern ENDamager* g_enDamagerHead;        // 0x80316474 (sdata)
extern ENRestricted* g_enRestrictedHead;  // 0x80316470 (sdata)

// Motor: actualiza los nodos y resuelve las colisiones golpe/danable.
// Vtable 0x80243B30: dtor, Update, vf2 (vacio).
class CTEngine {
public:
    CTEngine();
    virtual ~CTEngine();             // slot 0  0x80009280
    virtual void Update();           // slot 1  0x800086F0
    virtual void vf2();              // slot 2  0x8013A560 (vacio)

    /* 0x08 */ s32 m_x08;
    /* 0x0C */ u8 m_x0c;
};

#endif
