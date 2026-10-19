// Alarmas de OS (OSSetAlarm/OSSetPeriodicAlarm/OSCancelAlarm): la cola vive en la memoria del invitado y el SDK
// elevado la mantiene; el "decrementador" lo emula el temporizador del anfitrion: cuando vence la cabecera se
// extrae, se reinserta si es periodica y se llama al manejador (alarma, contexto), como DecrementerExceptionCallback.
#include "hle.h"
#include "syms.h"

static const u32 ALARM_QUEUE = 0x80316778;              // cabeza (+0) y cola (+4) de la lista doble
static u64 LD64(u32 a) { return ((u64)LD32(a) << 32) | LD32(a + 4); }
static void ST64(u32 a, u64 v) { ST32(a, (u32)(v >> 32)); ST32(a + 4, (u32)v); }

static u64 system_time() {
    static const u32 fn = SYM___OSGetSystemTime;
    RetRegs r = rt_call_indirect(fn, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    return ((u64)r.r3 << 32) | r.r4;
}

// Inserta ordenada por instante de disparo (como InsertAlarm, sin tocar el decrementador).
static void insert_alarm(u32 alarm, u64 fire, u32 handler) {
    ST32(alarm + 0, handler);
    ST64(alarm + 8, fire);
    u32 next = LD32(ALARM_QUEUE);
    u32 prev = 0;
    while (next && LD64(next + 8) <= fire) { prev = next; next = LD32(next + 0x14); }
    ST32(alarm + 0x10, prev); ST32(alarm + 0x14, next);
    if (prev) ST32(prev + 0x14, alarm); else ST32(ALARM_QUEUE, alarm);
    if (next) ST32(next + 0x10, alarm); else ST32(ALARM_QUEUE + 4, alarm);
}

void rt_service_alarms() {
    static u32 ctx = 0;
    for (int guard = 0; guard < 8; guard++) {
        u32 head = LD32(ALARM_QUEUE);
        if (!head) return;
        u64 now = system_time();
        if (LD64(head + 8) > now) return;
        u32 next = LD32(head + 0x14);                    // desengancha la cabecera
        ST32(ALARM_QUEUE, next);
        if (next) ST32(next + 0x10, 0); else ST32(ALARM_QUEUE + 4, 0);
        u32 handler = LD32(head);
        ST32(head, 0);
        u64 period = LD64(head + 0x18);
        if (period) {                                    // periodica: siguiente multiplo del periodo desde el inicio
            u64 start = LD64(head + 0x20);
            u64 fire = start + period * ((now - start) / period + 1);
            insert_alarm(head, fire, handler);
        }
        if (handler) {
            if (!ctx) ctx = 0x80003F00;                  // contexto ficticio: los manejadores de alarma no lo usan
            rt_call_indirect(handler, head, ctx, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }
}
