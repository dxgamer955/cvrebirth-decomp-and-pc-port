// System Config (SC): ajustes de la consola. Normalmente se leen de SYSCONF via IOS; aqui son valores fijos.
#include "hle.h"

HLE_FN(SCInit)        { return RET_NONE(); }
HLE_FN(SCCheckStatus) { return RET_I(0); }        // SC_STATUS_OK
HLE_FN(SCGetLanguage) { return RET_I(1); }        // ingles
