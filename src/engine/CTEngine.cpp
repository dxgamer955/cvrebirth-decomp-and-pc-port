// CTEngine: actualiza los nodos del motor y resuelve colisiones golpe/danable.
#include "engine/collision.h"

// CTEngine_ctor (0x800141E0)
CTEngine::CTEngine() {
    m_x08 = 0;
    m_x0c = 0;
}

// CTEngine_dtor (0x80009280)
CTEngine::~CTEngine() {}

// 0x8013A560 (vacio)
void CTEngine::vf2() {}

// Para cada danable activo del grupo `damagedGroup`, prueba contra cada zona de
// golpe activa del grupo `scratchGroup`; si hay golpe, el danable reacciona.
static void ResolveHits(s32 damagedGroup, s32 scratchGroup) {
    for (ENDamaged* d = g_enDamagedHead[damagedGroup]; d != 0; d = d->m_next) {
        if (d->m_active == 0) {
            continue;
        }
        for (EcoScratch* s = g_ecoScratchHead[scratchGroup]; s != 0; s = s->m_next) {
            if (s->m_active != 0 && s->Hit(d)) {
                d->OnHit();
            }
        }
    }
}

// CTEngine_vf1 (0x800086F0)
void CTEngine::Update() {
    s32 i;

    for (i = 0; i < 4; i++) {
        for (ENDamaged* d = g_enDamagedHead[i]; d != 0; d = d->m_next) {
            if (d->m_active != 0) {
                d->Exec();
            }
        }
    }
    for (i = 0; i < 4; i++) {
        for (EcoScratch* s = g_ecoScratchHead[i]; s != 0; s = s->m_next) {
            if (s->m_active != 0) {
                s->Exec();
            }
        }
    }

    // pares (danable, golpe) en el orden del binario; verificado con tests/difftest
    ResolveHits(0, 1);
    ResolveHits(1, 0);
    ResolveHits(0, 3);
    ResolveHits(0, 2);
    ResolveHits(1, 3);
    ResolveHits(2, 1);
    // El ultimo par esta fusionado en un solo bucle externo (grupo 3 contra golpes de los
    // grupos 0 y 1). OJO: el original solo comprueba d->m_active antes del bucle del grupo 0;
    // el bucle del grupo 1 se ejecuta aunque el danable este inactivo (0x80008AD4 salta a
    // 0x80008B2C). Se replica tal cual (lo detecto tests/difftest).
    for (ENDamaged* d = g_enDamagedHead[3]; d != 0; d = d->m_next) {
        if (d->m_active != 0) {
            for (EcoScratch* s = g_ecoScratchHead[0]; s != 0; s = s->m_next) {
                if (s->m_active != 0 && s->Hit(d)) {
                    d->OnHit();
                }
            }
        }
        for (EcoScratch* s = g_ecoScratchHead[1]; s != 0; s = s->m_next) {
            if (s->m_active != 0 && s->Hit(d)) {
                d->OnHit();
            }
        }
    }
}
