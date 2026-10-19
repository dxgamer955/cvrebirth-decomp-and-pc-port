// DrcSubWeaponHolyWater: logica por frame del agua bendita (efecto con fases y desvanecido).
#include "gameplay/subweapon.h"

// Resta 8 a la opacidad; cuando llega a 8 o menos marca el arma como terminada.
static inline void FadeOut(DrcSubWeaponHolyWater* self) {
    u32 alpha = fn_80128330(self->m_sprite);
    if (alpha == 0 || alpha <= 8) {
        self->m_state = 2;
    } else {
        fn_80128320(self->m_sprite, alpha - 8);
    }
}

// DrcSubWeaponHolyWater_vf3 (0x800A84C0)
void DrcSubWeaponHolyWater::Update() {
    if (m_phase == 0) {
        fn_800B68B0(lbl_80316544, lbl_80318828);
        void* obj = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
        fn_8001CB20(obj, 1);
        m_timer = 0;
        m_phase = m_phase + 1;
    } else if (m_phase == 1) {
        m_timer = m_timer + 1;
        if (m_timer % 60 == 0) {
            if (m_timer >= 240) {
                fn_800B68B0(lbl_80316544, lbl_80318864);
                void* obj = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
                fn_8001CB20(obj, 0);
                m_phase = m_phase + 1;
            } else if (m_flag != 0) {
                m_flag = 0;
            }
        }
    } else if (m_phase == 2) {
        FadeOut(this);
    } else if (m_phase == 100) {
        FadeOut(this);
    }
}
