#!/usr/bin/env python3
"""Prepara orig/ a partir de TU copia del juego: orig/game.dol (el ejecutable real) y orig/files/ (los datos).
Uso: prepare_game.py <archivo.wad | carpeta con los 0000000N.app ya desencriptados> [carpeta_de_salida]
Con un WAD usa la libreria libWiiPy (pip install libWiiPy) para desencriptarlo; con una carpeta (por ejemplo la de contenido de
la NAND de Dolphin) no hace falta ninguna libreria. No se incluye ninguna clave en este repositorio."""
import hashlib, os, struct, sys
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
from lz_ex import decompress
from u8_extract import extract

EXPECTED_SHA1 = "69795df60bdeae8faa8905f20f9c8af8455f3c53"       # game.dol de la version USA (decomp/game/config/config.yml)

def contents_from_wad(path):
    try:
        import libWiiPy
    except ImportError:
        raise SystemExit("falta libWiiPy: pip install libWiiPy (o pasa la carpeta de contenido ya desencriptada de Dolphin)")
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
        if len(d) > 4 and d[0] in (0x10, 0x11) and dol is None:               # ejecutable del juego: LZ77 de Nintendo que da un DOL
            try:
                o, _ = decompress(d)
                if looks_like_dol(o): dol = o
            except Exception: pass
        if d.find(b"\x55\xAA\x38\x2D", 0, 0x100) >= 0 and (u8 is None or len(d) > len(u8)): u8 = d      # archivo de datos: U8 (el mayor)
    if dol is None or u8 is None: raise SystemExit("no se encontraron el ejecutable y los datos del juego en: " + src)
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "game.dol"), "wb").write(dol)
    extract(u8, os.path.join(out, "files"))
    sha = hashlib.sha1(dol).hexdigest()
    print(f"orig/game.dol listo ({len(dol)} bytes, SHA-1 {sha})" + ("" if sha == EXPECTED_SHA1 else "  AVISO: no es la version esperada; la traduccion podria fallar"))
    print("orig/files:", sum(len(f) for _, _, f in os.walk(os.path.join(out, "files"))), "archivos")

if __name__ == "__main__":
    main()
