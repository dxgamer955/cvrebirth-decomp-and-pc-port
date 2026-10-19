// DrcSubWeaponWhipFire: logica por frame del fuego del latigo.
#include "gameplay/subweapon.h"

// DrcSubWeaponWhipFire_vf3 (0x800A8870)
void DrcSubWeaponWhipFire::Update() {
    // fuera de pantalla (solo se comprueba si no sube): terminada
    if (m_vel.y > lbl_80318828) {
        if (fn_80009070(lbl_80318848, lbl_8031647C, &m_pos) == 0) {
            m_state = 2;
            return;
        }
    }

    if (m_phase == 0) {
        // vuelo: traza hasta dos segmentos contra el terreno; si choca, se pega
        if (lbl_80318828 != m_vel.x || lbl_80318828 != m_vel.y) {
            u32 i = 0;
            do {
                Vec2 vel;
                vel.x = m_vel.x;
                vel.y = m_vel.y;
                Vec2 p0 = GetTrackedPos(this);
                Vec2 d;
                d.x = p0.x - vel.x;
                d.y = p0.y - vel.y;
                Vec2 b = GetTrackedPos(this);
                if (i != 0) {
                    b.x = b.x - m_vel.x;
                    d.x = d.x - m_vel.x;
                }

                void* finder = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
                TerrainHit out;
                if (fn_800084C0(*(void**)((u8*)finder + 0xF0), &d, &b, &out) != 0) {
                    fn_800B6740(lbl_80316544, lbl_8031544C);
                    fn_800B6740(lbl_80316544, lbl_80315450);
                    fn_800A4D00(m_hit);

                    // punto de contacto = origen + direccion de la velocidad * distancia de empuje
                    f32 push = out.f3;
                    f32 len = (f32)fn_80216698((f64)(m_vel.x * m_vel.x + m_vel.y * m_vel.y));
                    f32 ny = m_vel.y / len;
                    f32 nx = m_vel.x / len;
                    Vec2 contact;
                    contact.x = d.x + nx * push;
                    contact.y = d.y + ny * push;
                    typedef void (*SetPosFn)(void*, const Vec2*);
                    ((SetPosFn)(*(void***)this)[0x28 / 4])(this, &contact);

                    m_phase = m_phase + 1;
                    if (out.f1 >= lbl_80318828) {
                        fn_8002EE40(m_sprite, lbl_8025359C, 0);
                    } else {
                        fn_8002EE40(m_sprite, lbl_80315454, 0);
                    }

                    void* mem = operator_new(0x54);
                    CTMapTerrain* terrain = 0;
                    if (mem != 0) terrain = CTMapTerrain_ctor(mem, this);   // OOM: el original escribiria en la direccion 4
                    m_aux = terrain;
                    terrain->m_a = lbl_80318858;
                    ((CTMapTerrain*)m_aux)->m_b = lbl_80318840;
                    m_flag = 1;
                    lbl_80316528 = lbl_80316528 + 1;
                    break;
                }
                i = i + 1;
            } while (i < 2);
        }

        if (m_flag != 0) {
            // pegado: la velocidad pasa a la de la tabla
            m_vel.x = lbl_802439C0[6];
            m_vel.y = lbl_802439C0[7];
            m_vel.z = lbl_802439C0[8];
        } else {
            f32 vy = m_vel.y;
            m_pos.x = m_pos.x + m_vel.x;
            m_pos.y = m_pos.y + vy;
            m_pos.z = m_pos.z + m_vel.z;
            // frena la velocidad vertical hacia el objetivo, paso lbl_80318870 por frame
            if (vy != lbl_80318824) {
                if (vy > lbl_80318824) {
                    if (vy - lbl_80318824 > lbl_80318870) m_vel.y = vy - lbl_80318870;
                    else m_vel.y = lbl_80318824;
                } else {
                    if (lbl_80318824 - vy > lbl_80318870) m_vel.y = vy + lbl_80318870;
                    else m_vel.y = lbl_80318824;
                }
            }
        }
    } else if (m_phase == 1) {
        if (fn_80127D90(m_sprite) == 0) {
            m_state = 2;
            return;
        }
        if (m_flag != 0) {
            f32 limit = (f32)fn_80133A50(m_sprite) - lbl_8031885C;
            if (fn_80127EE0(m_sprite) > limit) {
                lbl_80316528 = lbl_80316528 - 1;
                m_flag = 0;
            }
        }
    }

    // el objeto de golpe y el sprite siguen a la posicion del arma
    Vec2 tracked = GetTrackedPos(this);
    m_hit->m_pos.x = tracked.x;
    m_hit->m_pos.y = tracked.y;
    f32 y = m_pos.y;
    f32 x = m_pos.x;
    Sprite* sprite = m_sprite;
    fn_801280B0(x, y, sprite);
    sprite->m_x = x;
    sprite->m_y = y;
}
