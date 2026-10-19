// Nodos del sistema de colision/dano del motor (ENDamaged, EcoScratch,
// ENDamager, ENRestricted). Todas son listas intrusivas dobles.
// Direcciones de game.dol entre parentesis.
#include "engine/collision.h"

// Estructura global de 0x802439C0: el vector en +0x18 es el valor inicial de
// posicion (todo 0.0f).
struct InitVecs {
    u8 pad[0x18];
    Vec3 vec;
};
extern const InitVecs lbl_802439C0;

ArrayHdr::~ArrayHdr() {
    if (data != 0) {
        size = 0;
        operator delete(data);
    }
}

// ENDamaged_ctor (0x800077A0)
ENDamaged::ENDamaged(s32 group) {
    m_arr1.data = 0;
    m_arr1.size = 0;
    m_arr1.capacity = 0;
    m_arr2.data = 0;
    m_arr2.size = 0;
    m_arr2.capacity = 0;
    m_x44 = 0.0f;
    m_x48 = 0.0f;
    m_group = group;
    m_active = 0;
    m_prev = 0;
    m_next = g_enDamagedHead[group];
    g_enDamagedHead[group] = this;
    if (m_next != 0) {
        m_next->m_prev = this;
    }
    m_pos = lbl_802439C0.vec;
    m_scale = 1.0f;
    m_x34 = -1;
    m_x38 = 0;
}

// ENDamaged_dtor (0x800078D0): desenlaza de la lista del grupo
ENDamaged::~ENDamaged() {
    if (m_next != 0) {
        m_next->m_prev = m_prev;
    }
    if (m_prev != 0) {
        m_prev->m_next = m_next;
    } else {
        g_enDamagedHead[m_group] = m_next;
    }
}

// ENDamaged_vf4 (0x80007B70)
void ENDamaged::vf4() {}

// EcoScratch_ctor (0x80007A70)
EcoScratch::EcoScratch(s32 group) {
    m_group = group;
    m_active = 0;
    m_prev = 0;
    m_next = g_ecoScratchHead[group];
    g_ecoScratchHead[group] = this;
    if (m_next != 0) {
        m_next->m_prev = this;
    }
}

// EcoScratch_dtor (0x80007AC0)
EcoScratch::~EcoScratch() {
    if (m_next != 0) {
        m_next->m_prev = m_prev;
    }
    if (m_prev != 0) {
        m_prev->m_next = m_next;
    } else {
        g_ecoScratchHead[m_group] = m_next;
    }
}

// ENDamager_ctor (0x800076D0)
ENDamager::ENDamager() {
    m_prev = 0;
    m_next = g_enDamagerHead;
    g_enDamagerHead = this;
    if (m_next != 0) {
        m_next->m_prev = this;
    }
    m_x04 = 0;
}

// ENDamager_dtor (0x80007710)
ENDamager::~ENDamager() {
    if (m_next != 0) {
        m_next->m_prev = m_prev;
    }
    if (m_prev != 0) {
        m_prev->m_next = m_next;
    } else {
        g_enDamagerHead = m_next;
    }
}

// ENDamager_vf4 (0x80007B80): reenvia a vf1
void ENDamager::vf4() {
    vf1();
}

// ENRestricted_ctor (0x800075B0)
ENRestricted::ENRestricted() {
    m_prev = 0;
    m_next = g_enRestrictedHead;
    g_enRestrictedHead = this;
    if (m_next != 0) {
        m_next->m_prev = this;
    }
    m_x04 = 1;
    m_vec = lbl_802439C0.vec;
}

// ENRestricted_dtor (0x80007610)
ENRestricted::~ENRestricted() {
    if (m_next != 0) {
        m_next->m_prev = m_prev;
    }
    if (m_prev != 0) {
        m_prev->m_next = m_next;
    } else {
        g_enRestrictedHead = m_next;
    }
}
