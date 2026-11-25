#!/usr/bin/env python3
"""Generates runtime/gen/ui_gamefont.h: the game's own 8x8 font (font/small1.psb.l, the one of the HUD) for the port's
interface (HOME menu, F3 settings, FPS counter, "LOADING...").
The font is read from YOUR copy of the game (orig/files), so it is not in the repository; without it the runtime keeps its
built-in 5x7 font. The few characters the game's font lacks (< > / ( ) + ! ? ' " ; %) are drawn here in the same style
(white fill and a dark shadow one pixel down and to the right).

  python3 scripts/make_uifont.py [files_folder]       (default: orig/files, or the extracted ModernGekko files)

.psb.l = "mdf\\0" + size (LE) + zlib; .psb = M2 "PSB" v2 tree. Texture: CI8 in 8x4 tiles with an RGB5A3 palette."""
import os, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "runtime", "gen", "ui_gamefont.h")

# ---- PSB reader (only what the font needs) -------------------------------------------------------------------------------
class PSB:
    def __init__(s, d):
        if d[:4] != b"PSB\0": raise ValueError("not a PSB file")
        s.d = d
        (_, s.o_names, s.o_strs, s.o_sdata, s.o_coff, s.o_clen, s.o_cdata, s.o_root) = struct.unpack_from("<8I", d, 8)
        charset, p = s.arr(s.o_names); names, p = s.arr(p); tree, p = s.arr(p)
        s.names = []
        for t in tree:                          # names are stored as a trie: walk from the leaf up to the root
            buf = []; c = names[t]
            while c != 0:
                code = names[c]; buf.append(c - charset[code]); c = code
            s.names.append(bytes(reversed(buf)).decode("latin1"))
        s.stroffs, _ = s.arr(s.o_strs); s.coffs, _ = s.arr(s.o_coff); s.clens, _ = s.arr(s.o_clen)
    def uint(s, p, n): return int.from_bytes(s.d[p:p + n], "little")
    def arr(s, p):
        n = s.d[p] - 0xC; cnt = s.uint(p + 1, n); p += 1 + n; es = s.d[p] - 0xC; p += 1
        return [s.uint(p + i * es, es) for i in range(cnt)], p + cnt * es
    def chunk(s, i): o = s.o_cdata + s.coffs[i]; return s.d[o:o + s.clens[i]]
    def val(s, p):
        t = s.d[p]
        if t == 1: return None
        if t in (2, 3): return t == 3
        if 4 <= t <= 0xC:
            n = t - 4; v = s.uint(p + 1, n)
            return v - (1 << 8 * n) if n and v >= 1 << (8 * n - 1) else v
        if 0xD <= t <= 0x14: return s.arr(p)[0]
        if 0x15 <= t <= 0x18:
            o = s.o_sdata + s.stroffs[s.uint(p + 1, t - 0x14)]; return s.d[o:s.d.index(b"\0", o)].decode("utf8", "replace")
        if 0x19 <= t <= 0x1C: return ("res", s.uint(p + 1, t - 0x18))
        if t == 0x1D: return 0.0
        if t == 0x1E: return struct.unpack_from("<f", s.d, p + 1)[0]
        if t == 0x1F: return struct.unpack_from("<d", s.d, p + 1)[0]
        if t == 0x20:
            offs, q = s.arr(p + 1); return [s.val(q + o) for o in offs]
        if t == 0x21:
            nm, q = s.arr(p + 1); offs, q = s.arr(q); return {s.names[n]: s.val(q + o) for n, o in zip(nm, offs)}
        raise ValueError("PSB type 0x%x at 0x%x" % (t, p))

def rgb5a3(v):
    if v & 0x8000: return ((v >> 10 & 31) * 255 // 31, (v >> 5 & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return ((v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17, (v >> 12 & 7) * 255 // 7)

def load_font(path):
    d = open(path, "rb").read()
    if d[:4] == b"mdf\0": d = zlib.decompress(d[8:])
    p = PSB(d); root = p.val(p.o_root); src = root["source"][0]
    if src["type"] != "CI8": raise ValueError("unexpected texture type " + src["type"])
    w, h = src["width"], src["height"]; pix = p.chunk(src["pixel"][1]); pal = p.chunk(src["pal"][1])
    pal = [rgb5a3(struct.unpack_from(">H", pal, i * 2)[0]) for i in range(len(pal) // 2)]
    img = [[(0, 0, 0, 0)] * w for _ in range(h)]; o = 0
    for ty in range(0, h, 4):                   # CI8: 8x4 tiles
        for tx in range(0, w, 8):
            for y in range(4):
                for x in range(8):
                    i = pix[o] if o < len(pix) else 0; o += 1   # the data stops after the last row of glyphs
                    img[ty + y][tx + x] = pal[i] if i < len(pal) else (0, 0, 0, 0)
    return img, root["code"], pal

# Characters the game's font does not have: 7x7 fill masks; the shadow (+1, +1) is added like in the game's glyphs
EXTRA = {
    "/": [".....##", "....##.", "...##..", "..##...", ".##....", "##.....", "......."],
    "<": ["....##.", "...##..", "..##...", ".##....", "..##...", "...##..", "....##."],
    ">": [".##....", "..##...", "...##..", "....##.", "...##..", "..##...", ".##...."],
    "(": ["...##..", "..##...", "..##...", "..##...", "..##...", "..##...", "...##.."],
    ")": ["..##...", "...##..", "...##..", "...##..", "...##..", "...##..", "..##..."],
    "+": [".......", "...#...", "...#...", ".#####.", "...#...", "...#...", "......."],
    "!": ["...##..", "...##..", "...##..", "...##..", "...##..", ".......", "...##.."],
    "?": [".####..", "##..##.", "....##.", "...##..", "...##..", ".......", "...##.."],
    "'": ["...##..", "...##..", "..##...", ".......", ".......", ".......", "......."],
    '"': [".##.##.", ".##.##.", ".#..#..", ".......", ".......", ".......", "......."],
    ";": [".......", "...##..", "...##..", ".......", ".......", "...##..", "..##..."],
    "%": ["##...#.", "##..#..", "...#...", "..#....", ".#..##.", "#...##.", "......."],
}

def main():
    cands = [sys.argv[1]] if len(sys.argv) > 1 else [os.path.join(ROOT, "orig", "files"),
             os.path.join(ROOT, "tools", "ModernGekko-Template", "extracted", "castlevania-rebirth", "files")]
    path = next((os.path.join(c, "font", "small1.psb.l") for c in cands if os.path.isfile(os.path.join(c, "font", "small1.psb.l"))), None)
    if not path:
        print("make_uifont: font/small1.psb.l not found (the interface keeps the built-in font)"); return 0
    img, code, pal = load_font(path)
    fill, shadow = pal[2], pal[3]               # white and the dark shadow of the letters

    cells = []                                  # (char, 8x8 RGBA rows)
    for ch, g in sorted(code.items()):
        if len(ch) != 1 or ch.islower() or ch == "#": continue   # lowercase = red copy of the capitals, "#" = heart
        cells.append((ch, [[img[g["y"] + y][g["x"] + x] for x in range(8)] for y in range(8)]))
    for ch, m in EXTRA.items():
        if ch in code: continue
        cell = [[(0, 0, 0, 0)] * 8 for _ in range(8)]
        for y in range(7):
            for x in range(7):
                if m[y][x] == "#": cell[y][x] = fill
        for y in range(7):
            for x in range(7):
                if m[y][x] == "#" and cell[y + 1][x + 1][3] == 0: cell[y + 1][x + 1] = shadow
        cells.append((ch, cell))
    cells.append(("\x7f", [[(255, 255, 255, 255)] * 8 for _ in range(8)]))   # solid cell for ui_rect

    COLS = 16; rows = (len(cells) + COLS - 1) // COLS; AW, AH = COLS * 8, rows * 8
    atlas = bytearray(AW * AH * 4); index = [255] * 128
    for n, (ch, cell) in enumerate(cells):
        cx, cy = n % COLS * 8, n // COLS * 8; index[ord(ch)] = n
        for y in range(8):
            for x in range(8):
                o = ((cy + y) * AW + cx + x) * 4; atlas[o:o + 4] = bytes(cell[y][x])

    out = ["// Generated by scripts/make_uifont.py from font/small1.psb.l of YOUR copy of the game: do not commit it.",
           "#pragma once",
           "static const int UIF_COLS = %d, UIF_AW = %d, UIF_AH = %d, UIF_CELL = 8;" % (COLS, AW, AH),
           "static const unsigned char UIF_INDEX[128] = { %s };" % ", ".join(map(str, index)),
           "static const unsigned char UIF_RGBA[%d] = {" % len(atlas)]
    for i in range(0, len(atlas), 32): out.append("    " + ",".join(str(b) for b in atlas[i:i + 32]) + ",")
    out.append("};")
    text = "\n".join(out) + "\n"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    if not (os.path.isfile(OUT) and open(OUT).read() == text):
        open(OUT, "w").write(text)
    print("make_uifont: %s (%d glyphs from %s)" % (os.path.relpath(OUT, ROOT), len(cells) - 1, os.path.relpath(path, ROOT)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
