// DrcSubWeaponAxe: logica por frame del hacha (subarma).
#include "gameplay/subweapon.h"

// DrcSubWeaponAxe_vf3 (0x800A7A80)
void DrcSubWeaponAxe::Update() {
    if (m_hit->m_hitFlag != 0) {
        // ha impactado: sonido y fin
        fn_800B6740(lbl_80316544, lbl_80315424);
        m_state = 2;
        return;
    }

    if (fn_80009070(lbl_80318848, lbl_8031647C, &m_pos) == 0) {
        m_state = 2;   // fuera de pantalla: terminar
        return;
    }

    // avanzar segun la velocidad y mover el sprite
    f32 x = m_pos.x + m_vel.x;
    f32 y = m_pos.y + m_vel.y;
    Sprite* sprite = m_sprite;
    m_pos.x = x;
    m_pos.y = y;
    m_pos.z = m_pos.z + m_vel.z;
    fn_801280B0(x, y, sprite);
    sprite->m_x = x;
    sprite->m_y = y;
}
