#!/usr/bin/env python3
"""Generates the system ANSI font (OSFont) that the native runtime hands to OSInitFont.
On a real Wii the font comes from the system ROM (Nintendo copyright, it cannot be distributed); here a TTF that is
already on your machine (DejaVu/Liberation/Noto, or Arial on Windows) is rasterized into a 512x512 I4 sheet (16 columns x 14 rows
of 32x36 cells for the characters 0x20-0xFF).
Output: runtime/gen/osfont_ansi.bin = 224 bytes of widths + the I4 sheet in 8x8 tiles (0x20000 bytes)."""
import os, sys, subprocess
from PIL import Image, ImageDraw, ImageFont

CANDS = ["DejaVuSans.ttf", "LiberationSans-Regular.ttf", "NotoSans-Regular.ttf", "FreeSans.ttf", "arial.ttf", "segoeui.ttf", "tahoma.ttf", "verdana.ttf", "Arial.ttf", "Helvetica.ttc"]   # the last ones: system fonts on Windows/macOS
def find_font():
    try:
        out = subprocess.run(["fc-list", ":", "file"], capture_output=True, text=True).stdout
    except FileNotFoundError:
        out = ""
    files = [l.split(":")[0].strip() for l in out.splitlines()]
    for c in CANDS:
        for f in files:
            if f.endswith("/" + c): return f
    # without fc-list (Windows, macOS): search the usual font folders
    dirs = [os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts"), "/c/Windows/Fonts", "/usr/share/fonts", "/System/Library/Fonts", "/Library/Fonts"]
    for c in CANDS:
        for d in dirs:
            for root, _, names in os.walk(d):
                for n in names:
                    if n.lower() == c.lower(): return os.path.join(root, n)
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
    print("warning: no TTF found; the system font will be blank", file=sys.stderr)
    for i in range(224): widths[i] = 12
px = sheet.load()
data = bytearray()
for ty in range(0, H, 8):                      # I4: 8x8 tiles, 4 bits per texel
    for tx in range(0, W, 8):
        for y in range(8):
            for x in range(0, 8, 2):
                data.append(((px[tx + x, ty + y] >> 4) << 4) | (px[tx + x + 1, ty + y] >> 4))
out = os.path.join(ROOT, "runtime", "gen", "osfont_ansi.bin")
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, "wb").write(bytes(widths) + bytes(data))
print(f"{out}: {len(widths) + len(data)} bytes ({path or 'no font'})")
