#pragma once
// Decodificacion de texturas de GX a RGBA8 (memoria del invitado -> bytes de 8 bits por canal).
#include "native_lift.h"
#include <map>
#include <vector>

extern std::map<u32, std::vector<u16>> g_tlut;            // paletas (TLUT) cargadas, por campo tmem de LOADTLUT1
// Decodifica la textura de 'addr' (direccion del invitado) de w x h con el formato GX 'fmt'; false si el formato no esta soportado.
bool decode_texture(u32 addr, u32 fmt, int w, int h, u32 tlutKey, u32 tlutFmt, std::vector<u8>& out);
