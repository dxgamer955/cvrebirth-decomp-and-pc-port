// Carga de game.dol en la memoria del invitado.
#include "hle.h"
#include <cstdio>
#include <vector>

static u32 be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

void rt_load_dol(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) rt_fatal("no se puede abrir %s", path);
    std::vector<u8> d; u8 buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    for (int i = 0; i < 18; i++) {
        u32 off = be32(&d[i * 4]), addr = be32(&d[0x48 + i * 4]), size = be32(&d[0x90 + i * 4]);
        if (size) memcpy(HP(addr), &d[off], size);
    }
    // .bss: la reserva ya esta a cero
}
