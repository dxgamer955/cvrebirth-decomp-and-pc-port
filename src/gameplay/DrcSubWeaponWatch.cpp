// DrcSubWeaponWatch: logica por frame del reloj (boomerang que vuelve al jugador).
#include "gameplay/subweapon.h"

// DrcSubWeaponWatch_vf3 (0x800A7ED0)
void DrcSubWeaponWatch::Update() {
    if (m_hit->m_hitFlag != 0) {
        m_hit->m_hitFlag = 0;
        fn_800B6740(lbl_80316544, lbl_8031543C);
    }

    // mientras no cae mas rapido que el umbral no se comprueba la pantalla
    if (!(m_vel.y <= lbl_80318828) && fn_80009070(lbl_80318848, lbl_8031647C, &m_pos) == 0) {
        m_state = 2;
        return;
    }

    // particula cada 4 frames, en una posicion aleatoria alrededor del reloj
    m_particleTimer = m_particleTimer - 1;
    if (m_particleTimer < 1) {
        void* particle = operator_new(0x78);   // puede ser nulo
        if (particle != 0) {
            f32 r1 = fn_801398F0(lbl_80316460);
            f32 py = m_pos.y;
            f32 offsetY = lbl_80318854 + lbl_80318858 * r1;
            f32 r2 = fn_801398F0(lbl_80316460);
            Vec2 spawn;
            // OJO al orden: x + (a + k), no (x + a) + k (diferencia de 1 ULP, lo vio el banco)
            spawn.x = m_pos.x + (lbl_80318854 + lbl_80318858 * r2);
            spawn.y = py + offsetY;
            fn_800AE870(lbl_80318830, particle, lbl_80253530, lbl_80315410, &spawn);
        }
        m_particleTimer = 4;
    }

    if (m_homingDelay == 0) {
        // perseguir al jugador: velocidad horizontal hacia el objetivo con aceleracion limitada
        f32 px = m_pos.x;
        f32 py = m_pos.y;
        void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
        Vec2 target = GetTrackedPos(*(void**)((u8*)mgr + 0x220));
        f32 dx = target.x - px;
        f32 dy = (target.y - py) - lbl_8031885C;

        f32 adx = dx < lbl_80318828 ? -dx : dx;
        if (adx < lbl_80318858) {
            f32 ady = dy < lbl_80318828 ? -dy : dy;
            if (ady < lbl_8031885C) {
                m_state = 2;   // ha llegado al jugador
                return;
            }
        }

        f32 desired = lbl_8031884C;
        if (m_dir < lbl_80318828) {
            desired = lbl_80318824;
        }
        f32 vx = m_vel.x;
        if (vx != desired) {
            if (vx <= desired) {
                if (desired - vx <= lbl_80318860) {
                    m_vel.x = desired;
                } else {
                    m_vel.x = vx + lbl_80318860;
                }
            } else if (vx - desired <= lbl_80318860) {
                m_vel.x = desired;
            } else {
                m_vel.x = vx - lbl_80318860;
            }
        }
    } else {
        m_homingDelay = m_homingDelay - 1;
    }

    // si va en la misma direccion a la que mira no se comprueba la pantalla
    f32 facing = lbl_80318868;
    if (lbl_80318828 <= m_dir) {
        facing = lbl_80318864;
    }
    f32 moving = lbl_80318868;
    if (lbl_80318828 <= m_vel.x) {
        moving = lbl_80318864;
    }

    if (moving == facing || fn_80009070(lbl_80318848, lbl_8031647C, &m_pos) != 0) {
        HitObject* hit = m_hit;
        hit->m_vel = m_vel;
        f32 x = m_pos.x + m_vel.x;
        f32 y = m_pos.y + m_vel.y;
        Sprite* sprite = m_sprite;
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
