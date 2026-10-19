#!/usr/bin/env python3
"""Decompresses Nintendo LZ77 (type 0x10) and extended LZ77 (type 0x11). Usage: lz_ex.py in out"""
import sys, struct
def decompress(d):
    t = d[0]; size = d[1] | d[2] << 8 | d[3] << 16
    assert t in (0x10, 0x11), hex(t)
    ext = t == 0x11
    out = bytearray(); i = 4
    while len(out) < size:
        flags = d[i]; i += 1
        for bit in range(8):
            if len(out) >= size: break
            if not flags & (0x80 >> bit):
                out.append(d[i]); i += 1; continue
            b0 = d[i]
            if ext:
                n = b0 >> 4
                if n == 0:
                    ln = ((b0 & 0xF) << 4 | d[i+1] >> 4) + 0x11; disp = ((d[i+1] & 0xF) << 8 | d[i+2]) + 1; i += 3
                elif n == 1:
                    ln = ((b0 & 0xF) << 12 | d[i+1] << 4 | d[i+2] >> 4) + 0x111; disp = ((d[i+2] & 0xF) << 8 | d[i+3]) + 1; i += 4
                else:
                    ln = n + 1; disp = ((b0 & 0xF) << 8 | d[i+1]) + 1; i += 2
            else:
                ln = (b0 >> 4) + 3; disp = ((b0 & 0xF) << 8 | d[i+1]) + 1; i += 2
            for _ in range(ln):
                out.append(out[-disp])
    return bytes(out[:size]), i
if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    out, used = decompress(d)
    open(sys.argv[2], "wb").write(out)
    print(f"type {d[0]:#x}, decompressed {len(out)} bytes (consumed {used}/{len(d)} of the input)")
