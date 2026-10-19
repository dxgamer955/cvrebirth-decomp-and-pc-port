// Funciones pequenas de gameplay: DrcBigEyeGenerator, DrcDamage, DrcEnemySpawner.
#include "gameplay/misc.h"

// DrcBigEyeGenerator_vf3 (0x80051A80)
void DrcBigEyeGenerator::Update() {
    CallPtmf(this, &m_callback);
}

// DrcDamage_vf1 (0x800A6C60)
void DrcDamage::Update() {
    if (m_delay != 0) {
        m_delay = m_delay - 1;
        return;
    }
    m_cur = m_cur + 1;
    if (m_cur > m_max) {
        m_cur = m_max;
    }
    typedef u8 (*CurveFn)(void*);
    CurveFn fn = (CurveFn)CwVtable(&m_curveVptr)[8 / 4];
    *m_out = fn(&m_curveVptr);
    if (m_cur >= m_max) {
        m_state = 2;
    }
}

// fn_800AEEB0 (0x800AEEB0)
void DrcEnemySpawner::Update() {
    void* mgr = fn_80207650(lbl_8031647C, 0, lbl_80313A20, lbl_803137D8, 0);
    if (fn_80017340(mgr) != 0) {
        return;
    }
    typedef int (*BoolFn)(void*);
    typedef void (*VoidFn)(void*);
    if (m_disabled == 0) {
        if (((BoolFn)CwVtable(this)[0x28 / 4])(this) != 0) {
            m_timer = m_timer - 1;
            if (m_timer <= 0) {
                m_timer = m_interval;
                if (m_jitter != 0) {
                    u32 r = fn_80139770(lbl_80316460);
                    m_timer = m_timer + (r % m_jitter);
                }
                ((VoidFn)CwVtable(this)[0x2C / 4])(this);
            }
        }
    }
    if (m_disabled == 0 && *(s32*)((u8*)m_sub + 8) == 0) {
        ((VoidFn)CwVtable(m_sub)[0xC / 4])(m_sub);
    }
}
