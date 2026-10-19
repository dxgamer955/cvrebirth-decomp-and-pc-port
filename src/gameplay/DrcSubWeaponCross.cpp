// DrcSubWeaponCross: logica por frame de la cruz (boomerang con gravedad).
#include "gameplay/subweapon.h"

// DrcSubWeaponCross_vf3 (0x800A7C90)
void DrcSubWeaponCross::Update() {
    if (m_hit->m_hitFlag != 0) {
        // impacto: se consume el flag y suena, pero la cruz sigue su camino
        m_hit->m_hitFlag = 0;
        fn_800B6740(lbl_80316544, lbl_8031542C);
    }

    // mientras sube (vel.y <= umbral) no se comprueba la pantalla; al caer, si sale, termina
    if (m_vel.y <= lbl_80318828 || fn_80009070(lbl_80318848, lbl_8031647C, &m_pos) != 0) {
        Sprite* sprite = m_sprite;
        f32 vy = m_vel.y + lbl_80318850;   // gravedad
        f32 x = m_pos.x + m_vel.x;
        f32 y = m_pos.y + vy;
        m_vel.y = vy;
        m_pos.x = x;
        m_pos.y = y;
        m_pos.z = m_pos.z + m_vel.z;
        fn_801280B0(x, y, sprite);
        sprite->m_x = x;
        sprite->m_y = y;
    } else {
        m_state = 2;
    }
}
