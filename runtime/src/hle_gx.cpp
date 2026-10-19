// GX (GPU): los comandos van por el FIFO de escritura (0xCC008000) y se interpretaran en el modulo de video.
// Aqui solo se simulan las esperas de la GPU.
#include "hle.h"

// GXDrawDone: GXDrawDone: envia PE_DONE y duerme hasta la interrupcion de fin; la GPU emulada termina al instante.
HLE_FN(GXDrawDone) { return RET_NONE(); }
