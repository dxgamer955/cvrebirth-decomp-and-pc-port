// Variables globales del sistema de colision. En game.dol viven en direcciones fijas
// (ver include/engine/collision.h); las pruebas diferenciales las enlazan alli.
#include "engine/collision.h"

ENDamaged* g_enDamagedHead[4];
EcoScratch* g_ecoScratchHead[4];
ENDamager* g_enDamagerHead;
ENRestricted* g_enRestrictedHead;
