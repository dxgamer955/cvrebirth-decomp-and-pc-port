// DrcBatGenerator: murcielago de onda senoidal.
#include "gameplay/bat.h"

// DrcBatGenerator_vf3 (0x8004FC10)
void DrcBatGenerator::Update() {
    if (m_dying > lbl_803174D0) {
        // muerte: efecto, parar el sprite y dejar una tarea que lo borre
        m_e4 = lbl_803174D0;
        fn_8000B4A0(lbl_80316480, lbl_80249570, m_e8);
        Sprite* sprite = m_sprite;
        sprite->m_f17C = lbl_803174DC;
        fn_80128340(lbl_803174DC * sprite->m_f178);
        Vec2 zero;
        zero.x = lbl_803174D0;
        zero.y = lbl_803174D0;
        fn_80043080(this, &zero);
        fn_800A4CF0(m_hit);
        fn_8002EE40(m_sprite, lbl_80314440, 0);

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
    } else if (m_frozen == 0) {
        // onda: y += k * sin(angle * a / b), x avanza con la velocidad
        f32 phase = (lbl_803174F4 * (f32)m_angle) / lbl_803174F8;
        f32 wave = (f32)fn_802162F4((f64)phase);
        f32 y = m_pos.y + wave;
        f32 x = m_pos.x + m_vel.x;
        Sprite* sprite = m_sprite;
        m_vel.y = wave;
        m_angle = m_angle + 4;
        m_pos.x = x;
        m_pos.y = y;
        fn_801280B0(x, y, sprite);
        sprite->m_x = x;
        sprite->m_y = y;
        if (fn_80009070(lbl_803174FC, lbl_8031647C, &m_pos) == 0) {
            m_state = 2;
        }
    }
}
