// DrcSubWeaponBase: logica por frame de la base de las subarmas (efecto de desvanecido).
#include "gameplay/subweapon.h"

// DrcSubWeaponBase_vf3 (0x800A7180)
void DrcSubWeaponBase::Update() {
    if (m_visible != 0 && m_active != 0) {
        fn_800B6740(lbl_80316544, lbl_80315408);

        void* anime = operator_new(0x78);
        if (anime != 0) anime = ValAnime_unsigned_char_ctor(anime);
        void* animeTask = fn_8013A4D0(anime);
        void* fadeTask = fn_800A6410(anime, -1, -256, 16);

        // tarea de espera construida en linea
        WaitTaskRaw* wait = (WaitTaskRaw*)operator_new(0x30);
        if (wait != 0) {
            s32 zero = 0;
            wait->f08 = zero;
            wait->f0C = zero;
            wait->vptr = (u32)lbl_8024570C;
            fn_8018E910(wait->sub);
            wait->f0D = zero;
            wait->f0F = zero;
            wait->f10 = zero;
            wait->f14 = &wait->f10;
            wait->vptr = (u32)lbl_8025F0E4;
            wait->f0E = 1;
        }
        fn_8013A800(wait, fadeTask);
        fn_8013A800(wait, animeTask);

        void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
        typedef void* (*Vf20)(void*, int);
        void* group = ((Vf20)(*(void***)mgr)[0x20 / 4])(mgr, 1);
        fn_8013A800(group, anime);
        fn_8013A800(group, wait);
        m_state = 2;
    }
}
