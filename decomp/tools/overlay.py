#!/usr/bin/env python3
"""Genera el overlay del decomp para Dolphin.

El codigo decompilado (src/gameplay) se compila a PowerPC, se enlaza en la cima de MEM1
(0x817C0000) y se inyecta como parches de memoria de Dolphin ([OnFrame] del
GameSettings/WD9EA4.ini). Tiene que ser MEM1: el SDK del juego solo deja ejecutar codigo en
MEM1 (IBAT4-7 de MEM2 desactivadas) y sus DBAT de MEM2 acaban en 0x93400000. Para que el
juego no use esa zona como memoria dinamica, el parche baja el tope del arena de MEM1
(0x80003110, que el OS lee al arrancar game.dol) hasta BASE. Cada funcion sustituida lleva en su entrada un
trampolin de 4 instrucciones (lis/ori/mtctr/bctr, usa r12 y CTR, ambos volatiles) hacia su
version decompilada.

Uso: overlay.py <elf> <hooks.txt> <salida.ini>     (o importar: parse_hooks, trampoline, patches)
"""
import struct, subprocess, sys

BASE = 0x817C0000            # cima de MEM1; el arena del juego termina aqui
SIZE_LIMIT = 0x40000          # 256 KB reservados (0x817C0000-0x81800000)
ARENA_END_ADDR = 0x80003110   # tope del arena de MEM1 que escribe el HLE de IOS

def parse_hooks(path):
    out = []
    for l in open(path):
        l = l.split("#")[0].strip()
        if l:
            p = l.split()
            out.append((int(p[0], 16), p[1], p[2] if len(p) > 2 else p[1]))
    return out

def symtab(elf):
    t = {}
    for l in subprocess.run(["llvm-nm", elf], capture_output=True, text=True, check=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3: t[p[2]] = int(p[0], 16)
    return t

def trampoline(target):
    hi, lo = target >> 16, target & 0xFFFF
    return [0x3D800000 | hi,          # lis   r12, hi
            0x618C0000 | lo,          # ori   r12, r12, lo
            0x7D8903A6,               # mtctr r12
            0x4E800420]               # bctr

def image(elf):
    """Imagen plana del overlay (desde BASE)."""
    import tempfile, os
    with tempfile.TemporaryDirectory() as d:
        b = os.path.join(d, "o.bin")
        subprocess.run(["llvm-objcopy", "-O", "binary"] + [x for s in (".text", ".rodata", ".data", ".sdata", ".sdata2") for x in ("-j", s)] + [elf, b], check=True)
        return open(b, "rb").read()

def patches(elf, hooks):
    """[(direccion, palabra)] en orden: imagen y despues trampolines."""
    syms = symtab(elf)
    out = []
    img = image(elf)
    img += bytes(-len(img) % 4)
    if len(img) > SIZE_LIMIT: sys.exit(f"el overlay ({len(img):#x} bytes) no cabe en {SIZE_LIMIT:#x}")
    out.append((ARENA_END_ADDR, BASE))        # reserva la cima de MEM1 antes de que game.dol la use
    for i in range(0, len(img), 4):
        out.append((BASE + i, struct.unpack(">I", img[i:i + 4])[0]))
    for addr, sym, _ in hooks:
        if sym not in syms: sys.exit(f"simbolo no encontrado en el overlay: {sym}")
        for k, w in enumerate(trampoline(syms[sym])):
            out.append((addr + 4 * k, w))
    return out

def write_ini(path, ps, name="Decomp overlay"):
    with open(path, "w") as f:
        f.write("# Generado por decomp/tools/overlay.py: codigo decompilado inyectado en el juego.\n")
        f.write("[OnFrame]\n$%s\n" % name)
        for a, w in ps:
            f.write("0x%08X:dword:0x%08X\n" % (a, w))
        f.write("[OnFrame_Enabled]\n$%s\n" % name)

if __name__ == "__main__":
    elf, hooks, ini = sys.argv[1:4]
    h = parse_hooks(hooks)
    ps = patches(elf, h)
    write_ini(ini, ps)
    print(f"{len(h)} funciones sustituidas, {len(ps)} palabras -> {ini}")
