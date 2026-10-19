// OSFont: en una Wii real OSInitFont (OSInitFont) carga la fuente del sistema desde la ROM; aqui la hoja ANSI se
// rasteriza con scripts/make_osfont.py (runtime/gen/osfont_ansi.bin: 224 anchos + hoja I4 512x512) y se deja
// la cabecera OSFontHeader lista en el bufer del invitado.
#include "hle.h"
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<u8>& font_blob() {
    static std::vector<u8> blob;
    if (blob.empty()) {
        static const char* paths[] = { "gen/osfont_ansi.bin", "runtime/gen/osfont_ansi.bin", "../runtime/gen/osfont_ansi.bin" };
        for (const char* p : paths) {
            FILE* f = fopen(p, "rb");
            if (!f) continue;
            blob.resize(224 + 0x20000);
            size_t n = fread(blob.data(), 1, blob.size(), f);
            fclose(f);
            if (n == blob.size()) return blob;
            blob.clear();
        }
        rt_log("[font] falta runtime/gen/osfont_ansi.bin (ejecuta scripts/make_osfont.py); la fuente del sistema quedara en blanco");
        blob.assign(224 + 0x20000, 0);
        for (int i = 0; i < 224; i++) blob[i] = 12;
    }
    return blob;
}

static void st16(u32 a, u32 v) { ST8(a, v >> 8); ST8(a + 1, v); }

HLE_FN(OSInitFont) {                    // OSInitFont(OSFontHeader* buffer) -> TRUE
    u32 buf = a0;
    const std::vector<u8>& b = font_blob();
    st16(0x80315F08, 0);                 // codificacion: ANSI
    ST32(0x803167B4, buf);               // cabecera ANSI activa (leida por fn_8018CB00)
    ST32(0x803167B0, buf);               // la SJIS usa la misma: no hay glifos japoneses
    for (u32 i = 0; i < 0x120; i += 4) ST32(buf + i, 0);
    st16(buf + 0x00, 0);                 // fontType = ANSI
    st16(buf + 0x02, 0x20); st16(buf + 0x04, 0xFF); st16(buf + 0x06, 0x20);   // firstChar, lastChar, invalChar
    st16(buf + 0x08, 28); st16(buf + 0x0A, 8); st16(buf + 0x0C, 32); st16(buf + 0x0E, 2);   // ascent, descent, width, leading
    st16(buf + 0x10, 32); st16(buf + 0x12, 36);                               // cellWidth, cellHeight
    ST32(buf + 0x14, 0x20000);           // sheetSize
    st16(buf + 0x18, 0);                 // sheetFormat = GX_TF_I4
    st16(buf + 0x1A, 16); st16(buf + 0x1C, 14);                               // sheetColumn, sheetRow
    st16(buf + 0x1E, 512); st16(buf + 0x20, 512);                             // sheetWidth, sheetHeight
    st16(buf + 0x22, 0x30);              // widthTable (desplazamiento)
    ST32(buf + 0x24, 0x120);             // sheetImage (desplazamiento)
    ST32(buf + 0x28, 0x20000);           // sheetFullSize
    for (u32 i = 0; i < 224; i++) ST8(buf + 0x30 + i, b[i]);
    for (u32 i = 0; i < 0x20000; i++) ST8(buf + 0x120 + i, b[224 + i]);
    return RET_I(1);
}
