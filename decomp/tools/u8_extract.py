#!/usr/bin/env python3
"""Extracts a U8 archive (the container of the game data, 00000009.app) to a folder. Usage: u8_extract.py in.app outdir"""
import os, struct, sys

def extract(data, outdir):
    if data[:4] != b"\x55\xAA\x38\x2D":
        i = data.find(b"\x55\xAA\x38\x2D")                  # some .app files have a header in front
        if i < 0: raise SystemExit("not a U8 archive")
        data = data[i:]
    root_off, hdr_size, data_off = struct.unpack(">III", data[4:16])
    n = struct.unpack(">I", data[root_off + 8:root_off + 12])[0]          # number of entries (including the root)
    names_off = root_off + n * 12
    def entry(i):
        t_n, a, b = struct.unpack(">III", data[root_off + i * 12:root_off + i * 12 + 12])
        end = data.index(b"\0", names_off + (t_n & 0xFFFFFF))
        return t_n >> 24, data[names_off + (t_n & 0xFFFFFF):end].decode("utf-8", "replace"), a, b
    stack = [(n, outdir)]                                                   # (index where the folder ends, path)
    os.makedirs(outdir, exist_ok=True)
    for i in range(1, n):
        while i >= stack[-1][0]: stack.pop()
        typ, name, a, b = entry(i)
        path = os.path.join(stack[-1][1], name)
        if typ == 1:
            os.makedirs(path, exist_ok=True); stack.append((b, path))
        else:
            open(path, "wb").write(data[a:a + b])

if __name__ == "__main__":
    extract(open(sys.argv[1], "rb").read(), sys.argv[2])
