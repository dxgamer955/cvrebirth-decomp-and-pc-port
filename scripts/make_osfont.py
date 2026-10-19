#!/usr/bin/env python3
"""Genera la fuente ANSI del sistema (OSFont) que el runtime nativo entrega a OSInitFont.
En una Wii real la fuente sale de la ROM del sistema (copyright de Nintendo, no se puede distribuir); aqui se
rasteriza una TTF libre del equipo (DejaVu/Liberation/Noto) en una hoja I4 de 512x512 (16 columnas x 14 filas
de celdas 32x36 para los caracteres 0x20-0xFF).
Salida: runtime/gen/osfont_ansi.bin = 224 bytes de anchos + hoja I4 en teselas de 8x8 (0x20000 bytes)."""
import os, sys, subprocess
from PIL import Image, ImageDraw, ImageFont

CANDS = ["DejaVuSans.ttf", "LiberationSans-Regular.ttf", "NotoSans-Regular.ttf", "FreeSans.ttf"]
def find_font():
    try:
        out = subprocess.run(["fc-list", ":", "file"], capture_output=True, text=True).stdout
    except FileNotFoundError:
        out = ""
    files = [l.split(":")[0].strip() for l in out.splitlines()]
    for c in CANDS:
        for f in files:
            if f.endswith("/" + c): return f
    return None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CW, CH, COLS, ROWS, W, H = 32, 36, 16, 14, 512, 512
path = find_font()
sheet = Image.new("L", (W, H), 0)
widths = bytearray(224)
if path:
    font = ImageFont.truetype(path, 26)
    d = ImageDraw.Draw(sheet)
    for i in range(224):
        c = 0x20 + i
        if 0x7F <= c < 0xA0: continue
        ch = chr(c)
        x, y = (i % COLS) * CW, (i // COLS) * CH
        d.text((x + 2, y + 28), ch, font=font, fill=255, anchor="ls")
        widths[i] = max(1, min(CW, int(round(font.getlength(ch))) + 3)) if c != 0x20 else 10
else:
    print("aviso: no hay TTF; la fuente del sistema quedara en blanco", file=sys.stderr)
    for i in range(224): widths[i] = 12
px = sheet.load()
data = bytearray()
for ty in range(0, H, 8):                      # I4: teselas de 8x8, 4 bits por texel
    for tx in range(0, W, 8):
        for y in range(8):
            for x in range(0, 8, 2):
                data.append(((px[tx + x, ty + y] >> 4) << 4) | (px[tx + x + 1, ty + y] >> 4))
out = os.path.join(ROOT, "runtime", "gen", "osfont_ansi.bin")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "wb").write(bytes(widths) + bytes(data))
print(f"{out}: {len(widths) + len(data)} bytes ({path or 'sin fuente'})")
