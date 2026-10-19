// Biblioteca de contenido (CNT): los archivos del juego se leen de una carpeta del host
// (el contenido extraido del WAD) en vez de la NAND/ES del Wii. La lleva la rutina del juego
// MCNTFileReadTaskFactory (inicializar, abrir, medir, leer, cerrar).
#include "hle.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

std::string g_files_root = "../tools/ModernGekko-Template/extracted/castlevania-rebirth/files";

static std::vector<FILE*> g_fds(1, nullptr);

// CNTInitHandle (0x800BB5D0): (directorio, handle, heap)
HLE_FN(CNTInitHandle) {
    ST8(a1 + 0x24, 3);                       // tipo propio: archivos del host
    rt_log("[cnt] CNTInitHandle dir='%s'\n", a0 ? rt_guest_cstr(a0) : "(null)");
    return RET_I(0);
}

// CNTReleaseHandle (0x800BB5E0)
HLE_FN(CNTReleaseHandle) { return RET_I(0); }

// CNTOpen (0x800BB650): (handle, ruta, info) -> 0 = bien
HLE_FN(CNTOpen) {
    std::string path = g_files_root + "/" + rt_guest_cstr(a1);
    rt_log("[cnt] abrir %s\n", rt_guest_cstr(a1));
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { rt_log("[cnt] CNTOpen: no existe '%s'\n", path.c_str()); return RET_I((u32)-0x1388); }
    struct stat st; stat(path.c_str(), &st);
    for (u32 o = 0; o < 0x44; o += 4) ST32(a2 + o, 0);
    ST32(a2 + 0x00, a0);
    ST32(a2 + 0x08, (u32)st.st_size);
    ST32(a2 + 0x0C, 0);
    ST8(a2 + 0x40, 3);
    g_fds.push_back(f);
    ST32(a2 + 0x10, (u32)g_fds.size() - 1);
    return RET_I(0);
}

// CNTGetLength (0x800BBAB0)
HLE_FN(CNTGetLength) { return RET_I(LD32(a0 + 0x08)); }

// CNTRead (0x800BB990): (info, buffer, longitud) -> bytes leidos
HLE_FN(CNTRead) {
    u32 idx = LD32(a0 + 0x10);
    if (idx == 0 || idx >= g_fds.size() || !g_fds[idx]) return RET_I((u32)-0x1391);
    u32 pos = LD32(a0 + 0x0C), len = LD32(a0 + 0x08);
    if (pos > len) return RET_I((u32)-0x1391);
    u32 want = a2; if (pos + want > len) want = len - pos;
    fseek(g_fds[idx], pos, SEEK_SET);
    size_t n = fread(HP(a1), 1, want, g_fds[idx]);
    ST32(a0 + 0x0C, pos + (u32)n);
    if (getenv("RT_CNTLOG")) rt_log("[cnt] leer idx=%u pos=%u pedido=%u -> %zu bytes a %08X (long=%u)\n", idx, pos, a2, n, a1, len);
    return RET_I((u32)n);
}

// CNTClose (0x800BBAE0)
HLE_FN(CNTClose) {
    u32 idx = LD32(a0 + 0x10);
    if (idx && idx < g_fds.size() && g_fds[idx]) { fclose(g_fds[idx]); g_fds[idx] = nullptr; }
    return RET_I(0);
}
