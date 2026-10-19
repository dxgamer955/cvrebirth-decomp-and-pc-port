#!/usr/bin/env python3
"""Fuzzer diferencial generico: una funcion ORIGINAL de game.dol contra su version decompilada
(build/tests/decomp.elf), las dos en Unicorn PowerPC sobre la misma memoria aleatoria.

Lo que se hace solo:
  * los destinos de cada `bl`/`b` fuera de la funcion se sustituyen por senuelos que registran
    sus argumentos (la aridad sale de las firmas de Ghidra, decomp/game/pseudo_c/all.c) y
    devuelven valores deterministas; _savegpr/_restgpr se ejecutan de verdad;
  * los globales de datos que lee o escribe la funcion se descubren ejecutando el original y se
    aleatorizan (flotantes, enteros, punteros) y se comparan al final;
  * se comprueba que los registros no volatiles (r1, r14-r31, f14-f31) se conservan.
Lo que define cada caso (tests/difftest/cases.py): como rellenar el objeto y que devuelven los
senuelos que no sean escalares (punteros, etc.).

Uso: fuzz.py [n_pruebas] [semilla] [filtro_de_nombre] [-v]"""
import os, random, re, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
from unicorn import UcError, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.ppc_const import *
from emu import Emu, SENTINEL, STACK_TOP, SDA, SDA2

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
# Con OVERLAY=1 se prueba el overlay real (build/overlay/overlay.elf, scripts/build_overlay.sh):
# "nuestro" es la propia direccion original ya parcheada con el trampolin de src/hooks.txt.
OVERLAY = os.environ.get("OVERLAY") == "1"
ELF = f"{ROOT}/build/overlay/overlay.elf" if OVERLAY else os.environ.get("DECOMP_ELF", f"{ROOT}/build/tests/decomp.elf")
SCR, SCR_SIZE = 0x80900000, 0x8000            # estado de entrada aleatorio (objeto + subobjetos)
HEAP, HEAP_SIZE = 0x80920000, 0x4000          # memoria que devuelven los "operator_new" senuelo
STK_LO = STACK_TOP - 0x4000
MOCK_BASE = 0x81710000                         # direcciones de los metodos virtuales senuelo
DATA_LO, DATA_HI = 0x8022DB60, 0x80500000      # .data/.sdata/.bss de game.dol
OPERATOR_NEW = 0x8015CB60

e = Emu(f"{ROOT}/orig/game.dol", f"{ROOT}/decomp/game/config/symbols.txt", ELF)
PATCH, in_overlay = {}, [0]
if OVERLAY:
    sys.path.insert(0, f"{ROOT}/decomp/tools")
    import overlay as ov
    _hooks = ov.parse_hooks(f"{ROOT}/src/hooks.txt")
    for _a, _w in ov.patches(ELF, _hooks):
        if _a < ov.BASE and _a != ov.ARENA_END_ADDR:
            PATCH.setdefault(max(h[0] for h in _hooks if h[0] <= _a), []).append(_w)
    from unicorn import UC_HOOK_CODE
    e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, sz, ud: in_overlay.__setitem__(0, in_overlay[0] + 1),
                  begin=ov.BASE, end=ov.BASE + ov.SIZE_LIMIT - 1)
NAMES, SIZES = {}, {}
for _l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
    _m = re.match(r"^(\S+) = \.\w+:0x([0-9A-Fa-f]+);.*size:0x([0-9A-Fa-f]+)", _l)
    if _m: NAMES[int(_m.group(2), 16)] = _m.group(1); SIZES[int(_m.group(2), 16)] = int(_m.group(3), 16)

def sigs_from_ghidra():
    """addr -> lista de clases de parametro ('i' entero/puntero, 'f' float/double)."""
    out, cur = {}, None
    for l in open(f"{ROOT}/decomp/game/pseudo_c/all.c", errors="replace"):
        m = re.match(r"^// ==== ([0-9a-f]{8}) ", l)
        if m: cur = int(m.group(1), 16); continue
        if cur is not None and l.strip():
            p = re.match(r"^[\w \*]+?\(([^)]*)\)", l.strip())
            if p:
                a = p.group(1).strip()
                out[cur] = [] if a in ("", "void") else ["f" if re.match(r"\s*(float|double)\b", x) and "*" not in x else "i" for x in a.split(",")]
            cur = None
    return out
GHIDRA = sigs_from_ghidra()
try:
    from lift_cases import CALLEE_KINDS
except ImportError:
    CALLEE_KINDS = {}
for _l in open(f"{ROOT}/decomp/game/config/symbols.txt"):               # etiquetas sin tamano (_savegpr_N...): son ayudantes reales
    _m = re.match(r"^(\S+) = \.text:0x([0-9A-Fa-f]+); // type:label", _l)
    if _m: NAMES.setdefault(int(_m.group(2), 16), _m.group(1))
ORIG_DATA = e.read(DATA_LO, DATA_HI - DATA_LO)      # datos originales: se restauran al empezar cada caso

# ---------------------------------------------------------------- estado de una prueba
class Ctx:
    """Estado aleatorio de una prueba. Los casos lo rellenan en su `setup`."""
    def __init__(self, rng):
        self.rng = rng
        self.mem = bytearray(rng.randbytes(SCR_SIZE))
        self.heap = bytearray(rng.randbytes(HEAP_SIZE))
        self.obj = SCR + 0x100
        self.next_alloc = SCR + 0x1000
        self.iargs, self.fargs = [self.obj], []
        self.fixed = set()                      # globales que el caso fija (no se aleatorizan)
        self.globals = {}                       # addr -> palabra
        self.pool = [SCR + 0x3000 + 0x400 * k for k in range(4)]   # objetos de reserva: candidatos a punteros en globales nulos
        self.heap_next = HEAP
        self.salt = rng.getrandbits(32)         # varia lo que devuelven los senuelos entre pruebas
    # --- escritura en el estado (offsets absolutos) ---
    def w32(self, a, v): struct.pack_into(">I", self.mem, a - SCR, v & 0xFFFFFFFF)
    def w8(self, a, v): self.mem[a - SCR] = v & 0xFF
    def wf(self, a, v): struct.pack_into(">f", self.mem, a - SCR, v)
    # --- campos del objeto por offset ---
    def f(self, off, lo, hi=None):
        v = self.rng.uniform(lo, hi) if hi is not None else lo
        self.wf(self.obj + off, v); return v
    def fc(self, off, choices): v = self.rng.choice(choices); self.wf(self.obj + off, v); return v
    def i(self, off, choices): v = self.rng.choice(choices); self.w32(self.obj + off, v); return v
    def b(self, off, choices): v = self.rng.choice(choices); self.w8(self.obj + off, v); return v
    def alloc(self, size=0x100):
        a = self.next_alloc; self.next_alloc += (size + 15) & ~15; self.pool.append(a); return a
    def ptr(self, off, size=0x200):
        a = self.alloc(size); self.w32(self.obj + off, a); return a
    def mock_vtable(self, nslots=64):
        """Vtable de CodeWarrior (slot i en vptr+8+4i) cuyas entradas son senuelos que registran."""
        t = self.alloc(8 + 4 * nslots + 8)
        for s in range(nslots): self.w32(t + 8 + 4 * s, vstub(s))
        return t
    def vptr(self, off, nslots=64, base=None):
        t = self.mock_vtable(nslots); self.w32((base or self.obj) + off, t); return t
    def shared_vtable(self):
        if not hasattr(self, "_vt"): self._vt = self.mock_vtable()
        return self._vt
    def fn(self, slot): return vstub(slot)    # direccion de un senuelo (para punteros a funcion)
    def gset(self, addr, v): self.globals[addr] = v & 0xFFFFFFFF; self.fixed.add(addr)
    def gf(self, addr, v): self.gset(addr, struct.unpack(">I", struct.pack(">f", v))[0])

CUR = [None]                                     # contexto de la prueba en curso (lo usan los senuelos)
LOG, COUNTS, CASE = [], {}, [None]
stubbed = set()
VSIG = ["i"]                                    # los metodos virtuales senuelo solo comparan `this` (cambialo con vsig)

def h32(a, n): return (a * 2654435761 + n * 40503 + 12345) & 0xFFFFFFFF
def f2b(v): return struct.unpack(">Q", struct.pack(">d", struct.unpack(">f", struct.pack(">f", v))[0]))[0]

def norm_arg(m, v):
    if STK_LO <= v < STACK_TOP:                  # puntero a un local: importa el contenido, no la direccion
        b = bytearray(m.read(v, CASE[0].get("stack", {}).get(None, 8)))
        for o in range(0, len(b) - 3, 4):        # punteros a la pila dentro del bloque: dependen del tamano del marco
            if STK_LO <= struct.unpack(">I", b[o:o + 4])[0] < STACK_TOP: b[o:o + 4] = b"\xff\xff\xff\xff"
        return ("stk", bytes(b))
    return v

def stub_fn(addr, label, sig, vcall=False):
    def fn(m):
        n = COUNTS.get(addr, 0); COUNTS[addr] = n + 1
        ints = [i for i, k in enumerate(sig) if k == "i"]
        flts = [i for i, k in enumerate(sig) if k == "f"]
        li = [norm_arg(m, m.reg(3 + j)) for j in range(len(ints))]
        lf = [m.fpr(1 + j) for j in range(len(flts))]
        LOG.append((label, tuple(li), tuple(lf)))
        h = h32(addr ^ CUR[0].salt, n)
        r = CASE[0].get("rets", {}).get(addr)
        if addr == OPERATOR_NEW and r is None:
            r = lambda c, k: (0 if (h & 1) else c.heap_alloc(m.reg(3)), 0, 0.0)
        if callable(r): r = r(CUR[0], n)
        if r is None:                              # sin informacion: a menudo un puntero valido (se usara como objeto), si no un entero pequeno
            pool = CUR[0].pool
            r = ((pool[(h >> 16) % len(pool)] if (h >> 12) % 10 < 7 else h % 4), None, ((h >> 4) % 2001 - 1000) / 20.0)
        if isinstance(r, int): r = (r, None, 0.0)
        if r[1] is not None and (CALLEE_KINDS.get(addr) is None or "l" in CALLEE_KINDS.get(addr, "") or not CASE[0]["name"].startswith("lift:")): m.uc.reg_write(UC_PPC_REG_4, r[1] & 0xFFFFFFFF)     # None = r4 intacto (el llamador puede pasarlo de basura)
        ck = CALLEE_KINDS.get(addr) if CASE[0]["name"].startswith("lift:") and CASE[0].get("rets", {}).get(addr) is None else None
        if ck is None or "f" in ck: m.uc.reg_write(UC_PPC_REG_FPR1, f2b(r[2]))       # el llamador solo ve f1/r3 si los usa
        if ck is not None and "i" not in ck and "l" not in ck: return None
        return r[0]
    return fn

DIRTY = set()                                     # palabras de datos escritas durante la ejecucion actual
def _data_write_hook(uc, access, addr, size, value, ud):
    for w in range(addr & ~3, addr + size, 4): DIRTY.add(w)
e.uc.hook_add(UC_HOOK_MEM_WRITE, _data_write_hook, begin=DATA_LO, end=DATA_HI - 1)

MMIO_LO, MMIO_HI = 0xCC000000, 0xCC010000
def _mmio_hook(uc, access, addr, size, value, ud):
    LOG.append(("mmio", addr, size, value & 0xFFFFFFFFFFFFFFFF))
e.uc.hook_add(UC_HOOK_MEM_WRITE, _mmio_hook, begin=MMIO_LO, end=MMIO_HI - 1)

ACTIVE = set()                                    # senuelos activos en el caso actual (el resto ejecuta codigo real)
SIGS_NOW = {}                                     # addr -> (label, sig) del caso actual

def install_stub(addr, label, sig):
    """Los hooks de Unicorn no se pueden quitar con facilidad: se instala uno por direccion y solo
    actua si la direccion esta en ACTIVE; la firma/etiqueta se leen de SIGS_NOW en cada caso."""
    ACTIVE.add(addr); SIGS_NOW[addr] = (label, sig)
    if addr in stubbed: return
    stubbed.add(addr)
    body = stub_fn(addr, label, sig)
    def cb(uc, a, size, ud):
        if addr not in ACTIVE: return
        lab, sg = SIGS_NOW[addr]
        r = stub_fn(addr, lab, sg)(e)
        if r is not None: uc.reg_write(UC_PPC_REG_3, r & 0xFFFFFFFF)
        uc.reg_write(UC_PPC_REG_PC, uc.reg_read(UC_PPC_REG_LR))
    e.uc.hook_add(UC_HOOK_CODE, cb, begin=addr, end=addr)

def vstub(slot):
    a = MOCK_BASE + 16 * slot
    install_stub(a, f"vcall{slot}", CASE[0].get("vsig", VSIG))
    return a

def heap_alloc(self, size):
    a = self.heap_next; self.heap_next += (max(size, 4) + 15) & ~15
    return a if a + 16 <= HEAP + HEAP_SIZE else 0
Ctx.heap_alloc = heap_alloc

# ---------------------------------------------------------------- analisis de la funcion
def callees(orig):
    """Destinos de b/bl fuera de la funcion original, salvo los ayudantes de registros."""
    size = SIZES[orig]; code = e.read(orig, size); out = set()
    for o in range(0, size, 4):
        w = struct.unpack(">I", code[o:o + 4])[0]
        if w >> 26 == 18 and not (w & 2):
            li = w & 0x03FFFFFC
            if li & 0x02000000: li -= 0x04000000
            t = (orig + o + li) & 0xFFFFFFFF
            if not orig <= t < orig + size: out.add(t)
    return sorted(t for t in out if not NAMES.get(t, "").startswith(("_savegpr", "_restgpr", "_savefpr", "_restfpr")))

def setup_stubs(case):
    CASE[0] = case
    ACTIVE.clear()
    e.write(DATA_LO, ORIG_DATA)
    for t in callees(case["orig"]):
        if t in case.get("real", ()): continue      # se ejecuta el original (p.ej. el thunk de __ptmf)
        sig = case.get("sigs", {}).get(t, GHIDRA.get(t))
        if sig is None:                          # fuera del rango decompilado por Ghidra: solo se compara que se llame
            sig = []
            if "-v" in sys.argv: print(f"  aviso: firma desconocida de {NAMES.get(t, hex(t))}: corrigela en 'sigs'")
        install_stub(t, NAMES.get(t, hex(t)), sig)

# ---------------------------------------------------------------- ejecucion
MAGIC_I = [0xC0DE0000 + i for i in range(32)]
def run(addr, ctx, case, glob_words, regions):
    CASE[0], CUR[0] = case, ctx
    LOG.clear(); COUNTS.clear()
    for a in DIRTY: e.write(a, ORIG_DATA[a - DATA_LO:a - DATA_LO + 4])      # deshace lo que escribio la ejecucion anterior
    DIRTY.clear()
    e.write(SCR, bytes(ctx.mem)); e.write(HEAP, bytes(ctx.heap)); e.write(STK_LO, bytes(STACK_TOP - STK_LO))
    for a, v in glob_words.items(): e.w32(a, v)
    DIRTY.clear()
    ctx.heap_next = HEAP
    uc = e.uc
    for i in range(32): uc.reg_write(UC_PPC_REG_0 + i, MAGIC_I[i])
    for i in range(32): uc.reg_write(UC_PPC_REG_FPR0 + i, 0xFEED000000000000 | i)
    for i, a in enumerate(ctx.iargs): uc.reg_write(UC_PPC_REG_3 + i, a & 0xFFFFFFFF)
    for i, a in enumerate(ctx.fargs): uc.reg_write(UC_PPC_REG_FPR1 + i, f2b(a))
    uc.reg_write(UC_PPC_REG_1, STACK_TOP); uc.reg_write(UC_PPC_REG_2, SDA2); uc.reg_write(UC_PPC_REG_13, SDA)
    uc.reg_write(UC_PPC_REG_LR, SENTINEL); uc.reg_write(UC_PPC_REG_MSR, 0x2000)
    fault = None
    try:
        uc.emu_start(addr, SENTINEL, count=60000)
        if uc.reg_read(UC_PPC_REG_PC) != SENTINEL: fault = (uc.reg_read(UC_PPC_REG_PC), "limite de instrucciones")
    except UcError as ex: fault = (uc.reg_read(UC_PPC_REG_PC), str(ex))
    bad = []
    if fault is None:
        for i in list(range(14, 32)) + [1]:
            if uc.reg_read(UC_PPC_REG_0 + i) != (STACK_TOP if i == 1 else MAGIC_I[i]): bad.append(f"r{i}")
        for i in range(14, 32):
            if uc.reg_read(UC_PPC_REG_FPR0 + i) != 0xFEED000000000000 | i: bad.append(f"f{i}")
    ret = None if fault else (uc.reg_read(UC_PPC_REG_3), uc.reg_read(UC_PPC_REG_4), uc.reg_read(UC_PPC_REG_FPR1))
    ret_kind = case.get("ret")
    if ret is not None and STK_LO <= ret[0] < STACK_TOP: ret = (0xFFFFFFFF,) + tuple(ret[1:])       # direccion de un local: depende del marco
    rv = None if ret is None else {None: None, "int": ret[0], "float": ret[2], "vec2": ret[:2], "i64": ret[:2], "both": (ret[0], ret[2])}[ret_kind]
    mem = [e.read(SCR, SCR_SIZE), e.read(HEAP, HEAP_SIZE)] + [e.read(a, 4) for a in regions]
    dirty = {a: e.read(a, 4) for a in DIRTY if a not in glob_words}          # escrituras a globales no previstas
    return dict(mem=mem, log=list(LOG), ret=rv, fault=fault, bad=bad, dirty=dirty)

DISC_FAULT = [None]
ncases, nbad = [0], [0]

def discover(case, n, seed):
    """Ejecuta el original sin aleatorizar globales y apunta las direcciones de datos que toca."""
    reads, writes, wide = set(), set(), set()
    def hook(uc, access, addr, size, value, ud):
        if not DATA_LO <= addr < DATA_HI: return
        for w in range(addr & ~3, addr + size, 4):
            (writes if access == 1 else reads).add(w)
            if size == 8: wide.add(w)
    h1 = e.uc.hook_add(UC_HOOK_MEM_READ, lambda uc, a, addr, s, v, u: hook(uc, 0, addr, s, v, u), begin=DATA_LO, end=DATA_HI - 1)
    h2 = e.uc.hook_add(UC_HOOK_MEM_WRITE, lambda uc, a, addr, s, v, u: hook(uc, 1, addr, s, v, u), begin=DATA_LO, end=DATA_HI - 1)
    rng = random.Random(seed ^ 0x5EED)
    ok = 0
    for rnd in range(5 * n):
        ctx = make_ctx(case, rng, {})
        gw = dict(ctx.globals)
        if rnd >= n and ok == 0:               # 2a pasada: globales ya vistos, aleatorizados (p. ej. punteros nulos en BSS)
            for a in reads | writes:
                if a not in ctx.fixed and a not in wide: gw[a] = rand_global(rng, ctx, a, e.r32(a), wide)
        r = run(case["orig"], ctx, case, gw, [])
        ok += r["fault"] is None
        if r["fault"]: DISC_FAULT[0] = r["fault"]
        if rnd == n - 1 and ok > 0: break
    e.uc.hook_del(h1); e.uc.hook_del(h2)
    return sorted(reads | writes), wide, ok

def make_ctx(case, rng, gvals):
    ctx = Ctx(rng)
    case["setup"](ctx)
    return ctx

def rand_global(rng, ctx, a, orig, wide):
    if a in ctx.fixed: return ctx.globals[a]
    if a in wide: return orig
    f = struct.unpack(">f", struct.pack(">I", orig))[0]
    looks_float = orig == 0 or (f == f and 1e-6 < abs(f) < 1e6)
    c = rng.random()
    if 0x80000000 <= orig < 0x81800000 and ctx.pool:           # parece un puntero: a veces, a un objeto nuestro
        return orig if c < 0.5 else rng.choice(ctx.pool)
    if orig == 0 and ctx.pool and c < 0.5:                       # puntero nulo en BSS: a veces, un objeto valido
        return rng.choice(ctx.pool)
    if looks_float and orig != 0:
        v = [orig, f * rng.uniform(0.5, 2), rng.uniform(-30, 30), 0.0, -f][rng.randrange(5)]
        return struct.unpack(">I", struct.pack(">f", v))[0]
    return rng.choice([orig, 0, 1, 2, 0xFFFFFFFF, rng.randrange(0, 8), rng.randrange(0, 2 ** 32)])

def mangle(name):
    """'Clase::Metodo' -> _ZN<n>Clase<m>MetodoEv (metodo sin argumentos)."""
    c, m = name.split("::")
    return f"_ZN{len(c)}{c}{len(m)}{m}Ev"

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-") and (sys.argv[sys.argv.index(a) - 1] not in ("-f", "-o"))]
    verbose = "-v" in sys.argv
    quiet = "-q" in sys.argv
    outf = open(sys.argv[sys.argv.index("-o") + 1], "w") if "-o" in sys.argv else None
    n = int(args[0]) if args else 300
    seed = int(args[1]) if len(args) > 1 else 1
    flt = args[2] if len(args) > 2 else ""
    import cases
    ONLY = set(l.strip() for l in open(sys.argv[sys.argv.index('-f') + 1])) if '-f' in sys.argv else set()
    total = 0
    for case in cases.CASES:
        if flt not in case["name"]: continue
        if "-f" in sys.argv and case["name"] not in ONLY: continue
        if case["orig"] == 0x8015D040: continue            # operator_delete: Emu lo sustituye por un registro
        if "-auto" in sys.argv and not case["name"].startswith("auto:"): continue
        if "-manual" in sys.argv and case["name"].startswith("auto:"): continue
        setup_stubs(case)
        gl, wide, ok = discover(case, case.get("discover", 40), seed)
        if ok == 0:
            f = DISC_FAULT[0]
            print(f"{case['name']}: el original falla siempre en el descubrimiento (revisa setup/senuelos): pc={f[0]:#x} {f[1]}"); total += 1; ncases[0] += 1; nbad[0] += 1
            if outf: outf.write(f"{case['name']} sin_preparar\n")
            continue
        if verbose:
            print(f"[{case['name']}] globales tocados: " + ", ".join(f"{NAMES.get(a & ~0, hex(a))}" for a in gl))
        if OVERLAY and case["orig"] not in PATCH:
            print(f"{case['name']}: sin hook en src/hooks.txt, omitido"); continue
        ours = case["orig"] if OVERLAY else e.syms[case.get("sym") or mangle(case["name"])]
        orig_words = {a: e.r32(a) for a in gl}
        rng = random.Random(seed)
        bad = skipped = shown = 0
        for t in range(n):
            ctx = make_ctx(case, rng, {})
            gw = {a: rand_global(rng, ctx, a, orig_words[a], wide) for a in gl}
            gw.update(ctx.globals)
            ro = run(case["orig"], ctx, case, gw, gl)
            if ro["fault"]: skipped += 1; continue
            if OVERLAY:
                saved = e.read(ours, 16)
                for k, w in enumerate(PATCH[ours]): e.w32(ours + 4 * k, w)
                e.uc.ctl_flush_tb()
                rn = run(ours, ctx, case, gw, gl)
                e.write(ours, saved); e.uc.ctl_flush_tb()
            else:
                rn = run(ours, ctx, case, gw, gl)
            why = []
            if rn["fault"]: why.append(f"nuestro falla en {rn['fault'][0]:#x}: {rn['fault'][1]}")
            else:
                if rn["bad"]: why.append("no conserva " + ",".join(rn["bad"]))
                if ro["log"] != rn["log"]:
                    k = next((i for i in range(min(len(ro["log"]), len(rn["log"]))) if ro["log"][i] != rn["log"][i]), min(len(ro["log"]), len(rn["log"])))
                    why.append(f"llamadas distintas (orig {len(ro['log'])}, nuestro {len(rn['log'])}) primera dif #{k}: "
                               f"orig={ro['log'][k] if k < len(ro['log']) else None} nuestro={rn['log'][k] if k < len(rn['log']) else None}")
                if ro["ret"] != rn["ret"]: why.append(f"retorno orig={ro['ret']} nuestro={rn['ret']}")
                for name, x, y, base in [("scr", ro["mem"][0], rn["mem"][0], SCR), ("heap", ro["mem"][1], rn["mem"][1], HEAP)]:
                    d = [(hex(base + o), x[o:o + 4].hex(), y[o:o + 4].hex()) for o in range(0, len(x), 4) if x[o:o + 4] != y[o:o + 4]]
                    if d: why.append(f"memoria {name}: {d[:4]}")
                dg = [(NAMES.get(a, hex(a)), x.hex(), y.hex()) for a, x, y in zip(gl, ro["mem"][2:], rn["mem"][2:]) if x != y]
                for a in set(ro["dirty"]) | set(rn["dirty"]):
                    x, y = ro["dirty"].get(a, ORIG_DATA[a - DATA_LO:a - DATA_LO + 4]), rn["dirty"].get(a, ORIG_DATA[a - DATA_LO:a - DATA_LO + 4])
                    if x != y: dg.append((NAMES.get(a, hex(a)), x.hex(), y.hex()))
                if dg: why.append(f"globales: {dg[:4]}")
            if why:
                bad += 1
                if shown < 3: shown += 1; print(f"  [{case['name']} #{t}] " + " | ".join(why))
        ncases[0] += 1; nbad[0] += bad > 0
        if outf: outf.write(f"{case['name']} {'FALLA' if bad else ('ok' if skipped < n else 'sin_ejercitar')}\n")
        if quiet and not bad: continue
        print(f"{case['name']}: pruebas {n} | fallos {bad} | omitidas {skipped} | globales {len(gl)}"
              + (f" | instrucciones en overlay {in_overlay[0]}" if OVERLAY else ""))
        if OVERLAY and in_overlay[0] == 0: print(f"{case['name']}: el trampolin no llego al overlay"); bad += 1
        total += bad
    print(f"-- {ncases[0]} casos ejecutados, {nbad[0]} con fallos, {total} fallos/errores en total")
    return 1 if total else 0

if __name__ == "__main__":
    sys.exit(main())
