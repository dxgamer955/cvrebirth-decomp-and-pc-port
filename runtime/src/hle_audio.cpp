// Audio: el SDK real (AI/DSP/AX/NW4R SND) corre elevado; el hardware esta en audio_hw.cpp y el DSP en audio_ax.cpp.
#include "hle.h"

// __AI_SRC_INIT: calibra el reloj del audio (AIS, __AI_SRC_INIT) esperando contadores del Hollywood; el resultado solo
// ajusta el flujo de streaming de AI (no se usa), asi que se omite.
HLE_FN(__AI_SRC_INIT) { return RET_NONE(); }

// BTE_InitStack (arranque de la pila Bluetooth) sigue sin emularse.
HLE_FN(BTE_InitStack) { return RET_NONE(); }
