import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from emu import Emu
root = os.path.join(os.path.dirname(__file__), "..", "..")
e = Emu(f"{root}/orig/game.dol", f"{root}/decomp/game/config/symbols.txt", f"{root}/build/tests/decomp.elf")
OBJ = 0x80900000
e.write(OBJ, bytes([0xAA]) * 0x80)
e.call(0x800077A0, (OBJ, 2))                    # ENDamaged_ctor original
print("original: vptr=%08x group=%d prev=%08x next=%08x +18=%08x" % (e.r32(OBJ), e.r32(OBJ+4), e.r32(OBJ+0x3C), e.r32(OBJ+0x40), e.r32(OBJ+0x18)))
print("cabeza grupo 2:", hex(e.r32(0x802845C8)))
