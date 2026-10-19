"""Entorno Unicorn PowerPC para ejecutar funciones de game.dol y codigo decompilado
(compilado a PowerPC con tests/difftest/build.sh) sobre la misma memoria."""
import re, struct, subprocess
from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.ppc_const import *

RAM_BASE, RAM_SIZE = 0x80000000, 0x01800000
SENTINEL = 0x81700000          # direccion de retorno: la emulacion se detiene al llegar
STACK_TOP = 0x80FF0000
SDA, SDA2 = 0x8031B7A0, 0x8031EF60   # _SDA_BASE_ / _SDA2_BASE_ de game.dol

class Emu:
    def __init__(self, dol, symbols, elf=None):
        self.uc = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
        self.uc.mem_map(RAM_BASE, RAM_SIZE, UC_PROT_ALL)
        self.uc.mem_map(0xCC000000, 0x10000, UC_PROT_ALL)   # registros de hardware (FIFO de GX...): memoria normal
        self.deletes = []
        self._load_dol(dol, symbols)
        self.syms = {}
        if elf:
            self._load_elf(elf)
        # operator_delete de game.dol se sustituye por un registro
        self.stub(0x8015D040, lambda emu: emu.deletes.append(emu.reg(3)))

    def reg(self, n): return self.uc.reg_read(UC_PPC_REG_0 + n)
    def fpr(self, n): return self.uc.reg_read(UC_PPC_REG_FPR0 + n)

    def stub(self, addr, fn):
        """Sustituye la funcion en addr por fn(emu): vuelve al llamador; si fn devuelve un
        entero, se deja en r3."""
        def cb(uc, a, size, ud):
            r = fn(self)
            if r is not None: uc.reg_write(UC_PPC_REG_3, r & 0xFFFFFFFF)
            uc.reg_write(UC_PPC_REG_PC, uc.reg_read(UC_PPC_REG_LR))
        self.uc.hook_add(UC_HOOK_CODE, cb, begin=addr, end=addr)

    def _load_dol(self, path, symbols):
        d = open(path, "rb").read()
        h = lambda o: struct.unpack(">I", d[o:o + 4])[0]
        for i in range(18):
            off, addr, size = h(i * 4), h(0x48 + i * 4), h(0x90 + i * 4)
            if size: self.uc.mem_write(addr, d[off:off + size])
        self._patch_psq(symbols)

    def _patch_psq(self, symbols):
        """psq_l/psq_st (Broadway) no existen en Unicorn: r1 -> nop; resto -> lfd/stfd/lfs/stfs."""
        for line in open(symbols):
            m = re.match(r"^\S+ = \.text:0x([0-9A-Fa-f]+);.*type:function size:0x([0-9A-Fa-f]+)", line)
            if not m: continue
            base, size = int(m.group(1), 16), int(m.group(2), 16)
            code = bytearray(self.uc.mem_read(base, size))
            ch = False
            for o in range(0, size - 3, 4):
                w = struct.unpack(">I", code[o:o + 4])[0]
                op = w >> 26
                if op not in (56, 60) or ((w >> 12) & 7) != 0: continue
                ra, frd, wb = (w >> 16) & 31, (w >> 21) & 31, (w >> 15) & 1
                if ra == 1: nw = 0x60000000
                else:
                    d12 = w & 0xFFF; d16 = d12 | 0xF000 if d12 & 0x800 else d12
                    nw = ((50 if op == 56 else 54) if wb == 0 else (48 if op == 56 else 52)) << 26 | frd << 21 | ra << 16 | d16
                code[o:o + 4] = struct.pack(">I", nw); ch = True
            if ch: self.uc.mem_write(base, bytes(code))

    def _load_elf(self, path):
        d = open(path, "rb").read()
        phoff, phentsize, phnum = struct.unpack(">I", d[0x1C:0x20])[0], struct.unpack(">H", d[0x2A:0x2C])[0], struct.unpack(">H", d[0x2C:0x2E])[0]
        for i in range(phnum):
            t, off, va, pa, fsz, msz = struct.unpack(">IIIIII", d[phoff + i * phentsize: phoff + i * phentsize + 24])
            if t == 1 and va >= RAM_BASE:
                self.uc.mem_write(va, bytes(msz))
                self.uc.mem_write(va, d[off:off + fsz])
        out = subprocess.run(["llvm-nm", path], capture_output=True, text=True).stdout
        for l in out.splitlines():
            p = l.split()
            if len(p) == 3: self.syms[p[2]] = int(p[0], 16)

    # ---- utilidades de memoria ----
    def r32(self, a): return struct.unpack(">I", bytes(self.uc.mem_read(a, 4)))[0]
    def w32(self, a, v): self.uc.mem_write(a, struct.pack(">I", v & 0xFFFFFFFF))
    def read(self, a, n): return bytes(self.uc.mem_read(a, n))
    def write(self, a, b): self.uc.mem_write(a, bytes(b))

    def call(self, addr, args=(), max_insns=20000):
        """Ejecuta la funcion en addr con args en r3..; devuelve r3. Registra los delete()."""
        uc = self.uc
        self.deletes = []
        for i in range(32): uc.reg_write(UC_PPC_REG_0 + i, 0)
        for i, a in enumerate(args): uc.reg_write(UC_PPC_REG_3 + i, a & 0xFFFFFFFF)
        uc.reg_write(UC_PPC_REG_1, STACK_TOP)
        uc.reg_write(UC_PPC_REG_2, SDA2); uc.reg_write(UC_PPC_REG_13, SDA)
        uc.reg_write(UC_PPC_REG_LR, SENTINEL)
        uc.reg_write(UC_PPC_REG_MSR, 0x2000)   # FP habilitado
        uc.emu_start(addr, SENTINEL, count=max_insns)
        return uc.reg_read(UC_PPC_REG_3)
