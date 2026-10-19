#!/usr/bin/env python3
"""Prepares orig/ from YOUR copy of the game: orig/game.dol (the real executable) and orig/files/ (the game data).
Usage: prepare_game.py <game.wad | folder with the already decrypted 0000000N.app files> [output_folder]
With a WAD it uses the libWiiPy library (pip install libWiiPy) to decrypt it; with a folder (for example Dolphin's NAND content
folder) no library is needed. No keys are stored in this repository."""
import hashlib, os, struct, sys
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from lz_ex import decompress
from u8_extract import extract

EXPECTED_SHA1 = "69795df60bdeae8faa8905f20f9c8af8455f3c53"       # game.dol of the US release (decomp/game/config/config.yml)

def contents_from_wad(path):
    try:
        import libWiiPy
    except ImportError:
        raise SystemExit("libWiiPy is missing: pip install libWiiPy (or pass the already decrypted content folder from Dolphin)")
    t = libWiiPy.title.Title(); t.load_wad(open(path, "rb").read())
    return [t.get_content_by_index(r.index) for r in t.content.content_records]

def contents_from_dir(path):
    return [open(os.path.join(path, f), "rb").read() for f in sorted(os.listdir(path)) if f.lower().endswith(".app")]

def looks_like_dol(d):
    if len(d) < 0x100: return False
    h = lambda o: struct.unpack(">I", d[o:o + 4])[0]
    end = max((h(i * 4) + h(0x90 + i * 4) for i in range(18) if h(0x90 + i * 4)), default=0)
    return end == len(d)

def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    src = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else "orig"
    cs = contents_from_dir(src) if os.path.isdir(src) else contents_from_wad(src)
    dol = u8 = None
    for d in cs:
        if len(d) > 4 and d[0] in (0x10, 0x11) and dol is None:               # the game executable: Nintendo LZ77 that decompresses to a DOL
            try:
                o, _ = decompress(d)
                if looks_like_dol(o): dol = o
            except Exception: pass
        if d.find(b"\x55\xAA\x38\x2D", 0, 0x100) >= 0 and (u8 is None or len(d) > len(u8)): u8 = d      # the data archive: U8 (the largest one)
    if dol is None or u8 is None: raise SystemExit("could not find the game executable and data in: " + src)
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "game.dol"), "wb").write(dol)
    extract(u8, os.path.join(out, "files"))
    sha = hashlib.sha1(dol).hexdigest()
    print(f"orig/game.dol ready ({len(dol)} bytes, SHA-1 {sha})" + ("" if sha == EXPECTED_SHA1 else "  WARNING: not the expected version; the translation may fail"))
    print("orig/files:", sum(len(f) for _, _, f in os.walk(os.path.join(out, "files"))), "files")

if __name__ == "__main__":
    main()
