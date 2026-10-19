// Enemigos pequenos: DrcEnemyBat2, DrcDokuroShot.
#include "gameplay/enemy.h"

// DrcEnemyBat2_vf1 (0x8004FF20)
void DrcEnemyBat2::Update() {
    void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
    u32 alpha = fn_80014A90(mgr);
    fn_80128320(m_sprite, alpha);
}

// DrcDokuroShot_vf3 (0x80062630)
void DrcDokuroShot::Update() {
    if (m_body->m_dying != 0) {
        u32 alpha = fn_80128330(m_sprite);
        if (alpha == 0 || alpha <= 8) {
            m_state = 2;
            return;
        }
        fn_80128320(m_sprite, alpha - 8);
    }

    if (m_phase == 0) {
        f32 last = (f32)(fn_80133A50(m_sprite) - 1);
        if (fn_80127EE0(m_sprite) >= last) {
            m_phase = m_phase + 1;
        }
    } else if (m_phase == 1) {
        if (m_body->m_hit != 0) {
            m_phase = m_phase + 1;
        }
        // frena m_speed hacia el objetivo, paso lbl_8031793C por frame
        f32 speed = m_speed;
        if (speed != lbl_8031787C) {
            if (speed > lbl_8031787C) {
                if (speed - lbl_8031787C > lbl_8031793C) m_speed = speed - lbl_8031793C;
                else m_speed = lbl_8031787C;
            } else {
                if (lbl_8031787C - speed > lbl_8031793C) m_speed = speed + lbl_8031793C;
                else m_speed = lbl_8031787C;
            }
        }
        m_angle = m_angle + m_speed;
    } else if (m_phase == 2) {
        f32 before = m_angle;
        f32 after = before + m_speed;
        s32 a = (s32)before;
        s32 b = (s32)after;
        m_angle = after;
        if (a / 120 != b / 120) {
            m_angle = lbl_80317878;
            m_speed = lbl_80317878;
            fn_8002EE40(m_sprite, lbl_8024B260, 0);
            m_phase = m_phase + 1;
        }
    } else if (m_phase == 3) {
        f32 last = (f32)(fn_80133A50(m_sprite) - 1);
        if (fn_80127EE0(m_sprite) >= last) {
            m_state = 2;
        }
    }

    fn_80128230(m_angle, m_sprite);
    Sprite* sprite = m_sprite;
    Vec2 pos;
    pos.x = m_body->m_pos.x;
    pos.y = m_body->m_pos.y;
    fn_801280F0(sprite, &pos);
    sprite->m_x = pos.x;
    sprite->m_y = pos.y;
}

// DrcMedousaGenerator_vf3 (0x80082FE0)
void DrcMedousaGenerator::Update() {
    if (m_invuln != 0) {
        m_invuln = m_invuln - 1;
        if (m_invuln == 0) {
            // termina el parpadeo: el cuerpo vuelve a recibir golpes
            m_hit->m_active = 1;
            Sprite* sprite = m_sprite;
            sprite->m_f17C = lbl_80317E34;
            fn_80128340(lbl_80317E34 * sprite->m_f178);
        }
    }

    if (m_damage > lbl_80317E28) {
        m_hit->m_active = 0;
        f32 damage = m_damage;
        f32 hp = m_hp;
        m_damage = lbl_80317E28;
        m_hp = hp - damage;
        if (m_hp <= lbl_80317E28) {
            // muerte
            if (m_big != 0) fn_800B6740(lbl_80316544, lbl_80314CC0);
            m_hp = lbl_80317E28;
            fn_8000B4A0(lbl_80316480, m_big != 0 ? lbl_8024F2B8 : lbl_8024F2CC, m_effectArg);
            Sprite* sprite = m_sprite;
            sprite->m_f17C = lbl_80317E34;
            fn_80128340(lbl_80317E34 * sprite->m_f178);
            void* effect = operator_new(0x74);
            if (effect != 0) {
                Vec2 where = GetTrackedPos(this);
                EffectTaskSet_ctor(effect, &where);
            }
            Vec2 zero;
            zero.x = lbl_80317E28;
            zero.y = lbl_80317E28;
            fn_80043160(this, &zero);
            fn_800A4CF0(m_hit);
            fn_8002EE40(m_sprite, lbl_80314CC4, 0);

            Sprite* owned = m_sprite;
            SpriteTaskRaw* task = (SpriteTaskRaw*)operator_new(0x14);
            if (task != 0) {
                task->f08 = 0;
                task->f0C = 0;
                task->vptr = (u32)lbl_80245678;
                task->sprite = owned;
            }
            void* animeTask = fn_8013A4D0(this);
            fn_8013A800(m_tasks, task);
            fn_8013A800(m_tasks, animeTask);
        } else {
            // herido: parpadeo de 20 frames
            m_invuln = 20;
            fn_80043810(m_flash, 0xFF000000, 0xFF, 0x20);
            Sprite* sprite = m_sprite;
            sprite->m_f17C = lbl_80317E28;
            fn_80128340(lbl_80317E28 * sprite->m_f178);
        }
    }

    if (m_frozen == 0 && m_invuln == 0) {
        // onda senoidal vertical: vel.y = amp * sin(phase * a / b)
        f32 wave = (f32)fn_802162F4((f64)((lbl_80317E5C * m_phase) / lbl_80317E60));
        f32 phase = m_phase + m_phaseStep;
        m_phase = phase;
        m_vel.y = m_amp * wave;
        if (phase >= lbl_80317E44) {
            m_phase = phase - lbl_80317E44;
        }
        f32 x = m_pos.x + m_vel.x;
        f32 y = m_pos.y + m_vel.y;
        Sprite* sprite = m_sprite;
        m_pos.x = x;
        m_pos.y = y;
        fn_801280B0(x, y, sprite);
        sprite->m_x = x;
        sprite->m_y = y;
        fn_80128160(m_sprite, m_vel.x > lbl_80317E28, 0);
    }

    // fuera de los limites visibles: terminado
    Vec2 limits;
    limits.x = lbl_80317E2C;
    limits.y = lbl_80317E64;
    void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
    if (fn_8001AD70(mgr, &m_pos, &limits) == 0) {
        m_state = 2;
    }
}

// DrcEnemyHoushi_vf3 (0x80081570)
void DrcEnemyHoushi::Update() {
    if (m_damage > lbl_80317DE8) {
        // golpeado: efecto de muerte y fase 5 (esperar a que acabe la animacion)
        m_damage = lbl_80317DE8;
        m_hp = lbl_80317DE8;
        m_dead = 0;
        Sprite* sprite = m_sprite;
        sprite->m_f17C = lbl_80317DEC;
        fn_80128340(lbl_80317DEC * sprite->m_f178);
        fn_8002EE20(m_sprite, 0x20, 0);
        fn_800A4CF0(m_hit);
        fn_8000B4A0(lbl_80316480, lbl_8024F100, m_effectArg);
        void* effect = operator_new(0x74);
        if (effect != 0) {
            Vec2 where = GetTrackedPos(this);
            EffectTaskSet_ctor(effect, &where);
        }
        Vec2 zero;
        zero.x = lbl_80317DE8;
        zero.y = lbl_80317DE8;
        fn_80043160(this, &zero);
        m_phase = 5;
    }

    if (m_dead == 0) {
        if (m_body->m_grounded == 0) {
            // en el aire: la gravedad (tabla) acelera la velocidad
            f32 scale = lbl_80317E00;
            m_vel.x = m_vel.x + scale * lbl_802439C0[1];
            m_vel.y = m_vel.y + scale * lbl_802439C0[2];
            m_vel.z = m_vel.z + scale * lbl_802439C0[3];
        }
        f32 x = m_pos.x + m_vel.x;
        f32 y = m_pos.y + m_vel.y;
        f32 z = m_pos.z + m_vel.z;
        m_pos.x = x;
        m_pos.y = y;
        m_pos.z = z;

        if (m_timer != 0) {
            m_timer = m_timer - 1;
        } else {
            switch (m_phase) {
            case 0: {
                u32 r = fn_80139770(lbl_80316460);
                m_phase = m_phase + 1;
                m_timer = (r % 120) + 120;
                break;
            }
            case 1:
                m_phase = m_phase + 1;
                m_timer = 120;
                // continua en el caso 2
            case 2:
                fn_8002EE40(m_sprite, lbl_80314C64, 0);
                m_phase = m_phase + 1;
                break;
            case 3:
                if (fn_80127D90(m_sprite) == 0) {
                    fn_8002EE40(m_sprite, lbl_80314C6C, 0);
                    m_phase = 0;
                }
                break;
            case 5:
                if (fn_80127D90(m_sprite) == 0) {
                    m_state = 2;
                    return;
                }
                break;
            default:
                break;
            }
        }
    }

    Vec2 pos;
    pos.x = m_pos.x;
    pos.y = m_pos.y;
    Sprite* sprite = m_sprite;
    fn_801280F0(sprite, &pos);
    sprite->m_x = pos.x;
    sprite->m_y = pos.y;
}

// DrcYokokamaEffect_vf3 (0x8005FA20)
void DrcYokokamaEffect::Update() {
    if (m_invuln != 0) {
        m_invuln = m_invuln - 1;
        if (m_invuln == 0) m_hit->m_active = 1;
    }
    if (m_lowHp == 0) {
        if (m_body->m_hp <= m_maxHp * lbl_80317898) m_lowHp = 1;
    }

    m_body->m_hurt = 0;
    if (m_damage != lbl_80317878) {
        m_body->m_hurt = 1;
        m_hit->m_active = 0;
        if (fn_800B42E0() != 0) {
            m_body->m_hp = m_body->m_hp - m_damage;
            m_damage = lbl_80317878;
            if (m_body->m_hp <= lbl_80317878) {
                // muerte
                m_body->m_dying = 1;
                fn_8000B4A0(lbl_80316480, lbl_8024B0B0, m_effectArg);
                fn_800B6740(lbl_80316544, lbl_80314694);
                fn_800A4CF0(m_hit);
                fn_8002EE50(m_sprite, lbl_8024B0C4, lbl_80314698, 0);
                void* animeTask = fn_8013A4D0(this);
                FloatTaskRaw* t1 = (FloatTaskRaw*)operator_new(0x14);
                if (t1 != 0) {
                    t1->f08 = 0;
                    t1->vptr = (u32)lbl_8025F120;
                    t1->f0C = 0;
                    t1->value = lbl_8031789C;
                }
                Sprite* owned = m_sprite;
                SpriteTaskRaw* t2 = (SpriteTaskRaw*)operator_new(0x14);
                if (t2 != 0) {
                    t2->f08 = 0;
                    t2->f0C = 0;
                    t2->vptr = (u32)lbl_802456B4;
                    t2->sprite = owned;
                }
                fn_8013A800(m_tasks, t2);
                fn_8013A800(m_tasks, t1);
                fn_8013A800(m_tasks, animeTask);
                return;
            }
            // herido
            fn_800B6740(lbl_80316544, lbl_803146A0);
            m_invuln = 20;
            if (m_flash != 0) fn_80043810(m_flash, 0xFF000000, 0xFF, 0x20);
        } else {
            m_damage = lbl_80317878;
            m_invuln = 20;
        }
    }

    if (m_dead == 0) {
        if (*(s32*)((u8*)m_sub + 8) == 0) {
            typedef void (*VoidFn)(void*);
            ((VoidFn)CwVtable(m_sub)[0xC / 4])(m_sub);
        }
        CallPtmf(this, &m_callback);
        f32 x = m_pos.x + m_vel.x;
        f32 y = m_pos.y + m_vel.y;
        f32 z = m_pos.z + m_vel.z;
        m_pos.x = x;
        m_pos.y = y;
        m_pos.z = z;
        if (m_turns != 0) {
            // se orienta segun este el objetivo a su izquierda o derecha
            void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
            void* target = *(void**)((u8*)mgr + 0x220);
            typedef u64 (*PosFn)(void*);
            u64 raw = ((PosFn)CwVtable(target)[0x30 / 4])(target);
            u32 hi = (u32)(raw >> 32);
            f32 targetX;
            __builtin_memcpy(&targetX, &hi, 4);
            Vec2 self = GetTrackedPos(this);
            const f32* dir = (self.x > targetX) ? &lbl_802439C0[5] : &lbl_802439C0[2];
            m_dir.x = dir[0];
            m_dir.y = dir[1];
            m_dir.z = dir[2];
            fn_80128160(m_sprite, dir[0] < lbl_80317878, 0);
            m_body->m_dir.x = m_dir.x;
            m_body->m_dir.y = m_dir.y;
            m_body->m_dir.z = m_dir.z;
        }
    }

    Vec2 pos;
    pos.x = m_pos.x;
    pos.y = m_pos.y;
    m_body->m_pos.x = pos.x;
    m_body->m_pos.y = pos.y;
    f32 y = m_pos.y;
    m_wobble = lbl_803178A0 + m_wobble;
    f32 wave = (f32)fn_802162F4((f64)((lbl_803178A4 * m_wobble) / lbl_803178A8));
    Sprite* sprite = m_sprite;
    f32 x = m_pos.x;
    y = y + lbl_803178AC * wave;
    fn_801280B0(x, y, sprite);
    sprite->m_x = x;
    sprite->m_y = y;
    m_f124 = lbl_80317878;
    m_f128 = lbl_80317878;
}
