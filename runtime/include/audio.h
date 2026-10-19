#pragma once
// Audio nativo: registros de hardware de AI (DMA de muestras) y del DSP (buzones), mezclador del microcodigo
// AX por software y salida SDL2. El SDK de audio (AI/DSP/AX/NW4R SND) se ejecuta elevado tal cual; aqui solo
// se emula lo que hay "debajo": registros, microcodigo del DSP y altavoces.
#include "native_lift.h"

bool audio_mmio_claims(u32 addr);                  // 0x0C005000-0x0C00503F (DSP/AI-DMA) y 0x0D006C00-0x0D006C3F (AI)
u32  audio_mmio_read(u32 addr, u32 size);
void audio_mmio_write(u32 addr, u32 size, u32 value);
void rt_service_audio();                           // atiende interrupciones de DSP y de DMA de AI (desde rt_service_irq)

// Lado "DSP" (audio_ax.cpp / audio_hw.cpp)
void dsp_mail_from_cpu(u32 mail);                  // correo escrito por la CPU al DSP (el microcodigo AX lo interpreta)
void dsp_post_mail(u32 mail, bool irq);            // correo del DSP a la CPU (irq = levanta la interrupcion de DSP)
void dsp_reset_ucode();
