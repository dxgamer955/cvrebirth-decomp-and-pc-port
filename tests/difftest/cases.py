"""Casos del fuzzer generico (fuzz.py). Cada caso: name, orig (direccion en game.dol), sym
(simbolo mangled del decomp; si falta se deduce de 'Clase::Metodo'), setup(ctx) que rellena el objeto, y opcionales:
  ret   'int' | 'float' | 'vec2'   que registros de retorno se comparan
  rets  {direccion_senuelo: (r3, r4, f1) | int | callable(ctx, n)}
  sigs  {direccion: ['i','f',...]} corrige la firma que dedujo Ghidra
  real  [direcciones] de callees que se ejecutan de verdad (thunks como __ptmf_scall)
  vsig  firma de los metodos virtuales senuelo
  stack {None: n_bytes}   bytes que se comparan de los argumentos que apuntan a la pila
"""
import math

def bat_setup(c):
    c.f(0x78, -300, 300); c.f(0x7C, -300, 300); c.f(0x80, -300, 300)
    c.f(0x84, -5, 5); c.f(0x88, -5, 5)
    c.i(0x08, [0, 1, 3, 2])
    c.fc(0xDC, [-1.0, 0.0, 0.0, 0.5, 3.0, 1000.0])
    c.i(0xE0, [0, 1, 7, 100, 1000, 0xFFFFFFF0, c.rng.randrange(-5000, 5000) & 0xFFFFFFFF])
    c.i(0xE8, [0, 1, 5, c.rng.randrange(0, 2 ** 32)])
    c.b(0xEC, [0, 0, 1, 255])
    c.ptr(0xF0, 0x200)
    c.ptr(0xF4, 0x200)

def rnd32(c, n): return ((c.salt * 2654435761 + n * 97 + 5) & 0xFFFFFFFF, 0, 0.0)
def rnd8(c, n): return ((c.salt >> (n % 24)) & 0xFF, 0, 0.0)
VCALL0 = 0x81710000                  # primer metodo virtual senuelo (slot 0)

def bigeye_setup(c):
    kind = c.rng.randrange(3)
    if kind == 0:                    # funcion directa (delta, -1, func)
        c.i(0xD4, [0, 4, 8]); c.i(0xD8, [0xFFFFFFFF]); c.w32(c.obj + 0xDC, c.fn(3))
    else:                            # virtual: func = offset del vptr en el objeto, index = offset en la vtable
        d = c.rng.choice([0, 4, 8]); c.w32(c.obj + 0xD4, d); c.w32(c.obj + 0xD8, 8 + 4 * c.rng.randrange(6))
        c.w32(c.obj + 0xDC, 0x20)    # el vptr del objeto adelantado esta en obj + d + 0x20
        c.vptr(d + 0x20)

def damage_setup(c):
    c.i(0x08, [0, 1, 3]); c.i(0x18, [0, 1, 4, 9, 0xFFFFFFFF, c.rng.randrange(0, 12)])
    c.i(0x1C, [0, 1, 5, 10, 0xFFFFFFFF, c.rng.randrange(0, 12)]); c.i(0x2C, [0, 0, 0, 1, 3, 0xFFFFFFFF])
    c.vptr(0x10); c.ptr(0x28, 0x40)

def spawner_setup(c):
    c.vptr(0)
    c.b(0x9C, [0, 0, 1, 255]); c.i(0x8C, [0, 1, 30, c.rng.randrange(0, 200)])
    c.i(0x90, [0, 0, 1, 7, 100, 0xFFFFFFFF]); c.i(0x94, [0, 1, 1, 1, 2, 0xFFFFFFFF, 0xFFFFFFF0, 50])
    sub = c.ptr(0x74, 0x100); c.w32(sub + 8, c.rng.choice([0, 0, 1, 5])); c.w32(sub, 0)
    t = c.mock_vtable(); c.w32(sub, t)

def bat2_setup(c):
    c.ptr(0x10, 0x200)

def dokuro_setup(c):
    c.i(0x08, [0, 1]); body = c.ptr(0xC0, 0x100); c.ptr(0xC4, 0x200)
    c.w8(body + 0x10, c.rng.choice([0, 0, 1])); c.w8(body + 0x45, c.rng.choice([0, 1]))
    c.i(0xCC, [0, 1, 1, 2, 2, 3, 4, 0xFFFFFFFF])
    c.fc(0xD8, [119.5, 119.9, -0.5, -119.9, 239.8, 0.0, c.rng.uniform(-300, 300)])
    c.fc(0xDC, [0.2, 1.0, -0.5, -2.0, 0.0, c.rng.uniform(-3, 3)])
    for o in (0x18, 0x1C): c.wf(body + o, c.rng.uniform(-200, 200))

def frame_ret(c, n): return (c.salt % 20, 0, 0.0)
def dur_ret(c, n): return (0, 0, float((c.salt >> 8) % 20))
def alpha_ret(c, n): return ((c.salt >> 16) % 24, 0, 0.0)

def medusa_setup(c):
    c.vptr(0)
    c.i(0x08, [0, 1]); c.i(0xDC, [0, 0, 1, 2, 20, 0xFFFFFFFF])
    c.f(0x78, -300, 300); c.f(0x7C, -300, 300); c.f(0x84, -5, 5); c.f(0x88, -5, 5)
    c.fc(0xE0, [-1.0, 0.0, 1.0, 3.0, 10.0, c.rng.uniform(-5, 20)])
    c.fc(0xE4, [-1.0, 0.0, 0.0, 1.0, 2.0, 3.0, 10.0, c.rng.uniform(0, 12)])
    c.f(0xE8, -20, 20); c.fc(0xEC, [0.0, 0.1, 0.5, 5.0, c.rng.uniform(-2, 8)])
    c.fc(0xF0, [0.0, 1.0, 359.0, 360.0, 355.0, c.rng.uniform(-10, 400)])
    c.ptr(0xF4, 0x200); c.ptr(0xF8, 0x100); c.ptr(0xFC, 0x40)
    c.b(0x100, [0, 0, 1]); c.b(0x101, [0, 1]); c.i(0x104, [0, 7, c.rng.randrange(0, 2 ** 32)])

def houshi_setup(c):
    c.vptr(0)
    c.i(0x08, [0, 1]); c.f(0x78, -300, 300); c.f(0x7C, -300, 300); c.f(0x80, -300, 300)
    c.f(0x9C, -5, 5); c.f(0xA0, -5, 5); c.f(0xA4, -5, 5)
    c.i(0xD4, [0, 0, 1, 2, 3, 3, 5, 5, 4, 0xFFFFFFFF])
    c.fc(0xD8, [-1.0, 0.0, 0.0, 0.0, 1.0, 3.0]); c.i(0xDC, [0, 0, 0, 1, 2, 120])
    c.fc(0xE0, [0.0, 5.0, c.rng.uniform(-3, 9)])
    c.ptr(0xE4, 0x200); c.ptr(0xE8, 0x100)
    body = c.ptr(0xF0, 0x40); c.w8(body + 0x20, c.rng.choice([0, 1, 255]))
    c.b(0xF4, [0, 0, 0, 1]); c.i(0xF8, [0, 7, c.rng.randrange(0, 2 ** 32)])

def alive_ret(c, n): return ((c.salt >> 3) & 1, 0, 0.0)

def fbits(v):
    import struct
    return struct.unpack(">I", struct.pack(">f", float(v)))[0]

def ptmf_setup(c, off, base=None):
    base = base or c.obj
    kind = c.rng.randrange(3)
    if kind == 0:                    # funcion directa
        c.w32(base + off, c.rng.choice([0, 4, 8])); c.w32(base + off + 4, 0xFFFFFFFF); c.w32(base + off + 8, c.fn(3))
    else:                            # virtual: func = offset del vptr dentro del objeto ajustado
        d = c.rng.choice([0, 4]); c.w32(base + off, d); c.w32(base + off + 4, 8 + 4 * c.rng.randrange(6))
        c.w32(base + off + 8, 0x20); c.vptr(d + 0x20, base=base)

def pos_ret(c, n): return (fbits(((c.salt >> (4 * n)) & 15) - 8), fbits(((c.salt >> (4 * n + 2)) & 15) - 8), 0.0)

def yoko_setup(c):
    c.vptr(0)
    c.f(0x78, -300, 300); c.f(0x7C, -300, 300); c.f(0x80, -300, 300); c.f(0x9C, -5, 5); c.f(0xA0, -5, 5); c.f(0xA4, -5, 5)
    ptmf_setup(c, 0xCC)
    c.fc(0xDC, [0.0, 0.0, 0.0, 1.0, -1.0, 5.0]); c.i(0xE0, [0, 0, 1, 2, 20]); c.i(0xE4, [0, 0, 0, 1])
    c.f(0xF4, -50, 50); c.i(0xFC, [0, 0, 1]); c.fc(0x108, [0.0, 10.0, c.rng.uniform(-5, 50)])
    c.ptr(0x12C, 0x100); c.ptr(0x130, 0x200); c.ptr(0x138, 0x40) if c.rng.random() < 0.7 else c.w32(c.obj + 0x138, 0)
    body = c.ptr(0x140, 0x80)
    c.wf(body + 0x2C, c.rng.choice([0.5, 1.0, 3.0, 10.0, c.rng.uniform(-3, 30)])); c.w8(body + 0x10, c.rng.choice([0, 1])); c.w8(body + 0x46, c.rng.choice([0, 1]))
    sub = c.ptr(0x144, 0x100); c.w32(sub + 8, c.rng.choice([0, 0, 1])); c.w32(sub, c.mock_vtable())
    c.b(0x159, [0, 1, 1]); c.i(0x15C, [0, 7, c.rng.randrange(0, 2 ** 32)])
    mgr = c.alloc(0x400); obj2 = c.alloc(0x100); c.w32(mgr + 0x220, obj2); c.w32(obj2, c.mock_vtable()); c.mgr = mgr

def mgr_ret(c, n): return (c.mgr, 0, 0.0)

CASES = [
    dict(name="DrcBatGenerator::Update", orig=0x8004FC10, sym="_ZN15DrcBatGenerator6UpdateEv", setup=bat_setup,
         sigs={0x80128340: ["f"], 0x802162F4: ["f"]}),
    dict(name="DrcBigEyeGenerator::Update", orig=0x80051A80, sym="_ZN18DrcBigEyeGenerator6UpdateEv", setup=bigeye_setup,
         real=[0x80207628]),
    dict(name="DrcDamage::Update", orig=0x800A6C60, sym="_ZN9DrcDamage6UpdateEv", setup=damage_setup,
         rets={VCALL0: rnd8}),
    dict(name="DrcEnemySpawner::Update", orig=0x800AEEB0, sym="_ZN15DrcEnemySpawner6UpdateEv", setup=spawner_setup,
         sigs={0x80139770: ["i"]},
         rets={0x80139770: rnd32, 0x80207650: 0x80900000 + 0x1000,
               0x80017340: lambda c, n: (c.salt & 7 == 0, 0, 0.0),                # casi nunca pausa
               VCALL0 + 16 * 8: lambda c, n: ((c.salt >> 5) & 3 != 0, 0, 0.0)}),    # vcall 0x28: casi siempre activo
    dict(name="DrcEnemyBat2::Update", orig=0x8004FF20, sym="_ZN12DrcEnemyBat26UpdateEv", setup=bat2_setup,
         rets={0x80207650: 0x80900000 + 0x1000}),
    dict(name="DrcDokuroShot::Update", orig=0x80062630, sym="_ZN13DrcDokuroShot6UpdateEv", setup=dokuro_setup,
         rets={0x80133A50: frame_ret, 0x80127EE0: dur_ret, 0x80128330: alpha_ret}),
    dict(name="DrcMedousaGenerator::Update", orig=0x80082FE0, setup=medusa_setup,
         sigs={0x802162F4: ["f"], 0x80128340: ["f"], 0x8001AD70: ["i", "i", "i"]},
         rets={0x80207650: 0x80900000 + 0x1000, 0x8001AD70: lambda c, n: (c.salt & 3 != 0, 0, 0.0)}),
    dict(name="DrcEnemyHoushi::Update", orig=0x80081570, setup=houshi_setup,
         sigs={0x80128340: ["f"]},
         rets={0x80127D90: alive_ret, 0x80139770: rnd32}),
    dict(name="DrcYokokamaEffect::Update", orig=0x8005FA20, setup=yoko_setup,
         sigs={0x802162F4: ["f"], 0x80207628: []}, real=[0x80207628],
         rets={0x80207650: mgr_ret, 0x800B42E0: lambda c, n: ((c.salt >> 9) & 3 != 0, 0, 0.0),
               VCALL0 + 16 * 10: pos_ret}),
]


# ---- funciones triviales traducidas automaticamente (decomp/tools/trivial.py) ----
def _smart_fill(c, addr, size):
    """Rellena un bloque con palabras plausibles: punteros a otros bloques, floats, enteros pequenos y,
    a veces, un vptr de CodeWarrior falso (metodos virtuales senuelo)."""
    pool = c.pool
    for off in range(0, size, 4):
        r = c.rng.random()
        if r < 0.28: c.w32(addr + off, c.rng.choice(pool))
        elif r < 0.40: c.wf(addr + off, c.rng.choice([0.0, 1.0, -1.0, c.rng.uniform(-300, 300)]))
        elif r < 0.62: c.w32(addr + off, c.rng.choice([0, 0, 1, 2, 3, 5, 0x14, 0xFF, 0xFFFFFFFF]))
    if c.rng.random() < 0.6: c.w32(addr, c.shared_vtable())

def _auto_setup(kinds, chains, ptmfs=()):
    def setup(c):
        if not hasattr(c, "_smart"):
            c._smart = True
            for b in c.pool: _smart_fill(c, b, 0x400)
        ia, fa = [], []
        for k in kinds:
            if k == "p": a_ = c.alloc(0x200); _smart_fill(c, a_, 0x200); ia.append(a_)
            elif k == "q":
                if c.rng.random() < 0.7: a_ = c.alloc(0x200); _smart_fill(c, a_, 0x200); ia.append(a_)
                else: ia.append(c.rng.choice([0, 1, 2, 7, 0xFFFFFFFF]))
            elif k == "i": ia.append(c.rng.choice([0, 1, 2, 7, 0xFFFFFFFF, 0x80000000, c.rng.randrange(0, 2 ** 32)]))
            else: fa.append(c.rng.choice([0.0, 1.0, -1.0, c.rng.uniform(-100, 100)]))
        for k, off in chains:                      # palabras que el original usa luego como puntero
            if k < len(ia) and kinds[k] == "p": c.w32(ia[k] + off, c.alloc(0x200))
        for k, off in ptmfs:                          # punteros a metodo miembro que invoca la funcion (__ptmf_scall)
            if k < len(ia) and 0x80900000 <= ia[k] < 0x80907000: ptmf_setup(c, off, base=ia[k])
        c.iargs, c.fargs = ia, fa
        c.obj = ia[0] if ia and kinds[0] in "pq" and ia[0] > 0x1000 else c.obj
    return setup

def _auto_case(name, addr, kinds, kind, tail, chains=()):
    d = dict(name="auto:" + name, orig=addr, sym="dc_" + name, setup=_auto_setup(kinds, chains), discover=6,
             ret={"int": "int", "float": "float", "void": None, "tail": "both"}[kind])
    if kind == "tail": d["sigs"] = {tail: ["i"] * 8}
    return d

try:
    from auto_cases import AUTO
    CASES += [_auto_case(*t) for t in AUTO]
except ImportError:
    pass


# ---- funciones elevadas automaticamente (decomp/tools/lift.py) ----
def _lift_cases():
    try:
        from lift_cases import LIFT
    except ImportError:
        return []
    hand = {c["orig"]: c for c in CASES if not c["name"].startswith("auto:")}
    out = []
    for item in LIFT:
        name, addr, ret, ps = item[:4]
        pt = item[4] if len(item) > 4 else ()
        if addr in hand:                      # misma preparacion que el caso escrito a mano
            h = dict(hand[addr]); h["name"] = "lift:" + name; h["sym"] = "dc_" + name
            h["ret"] = {"i": "int", "f": "float", "v": None, "l": "i64"}[ret]
            h["real"] = list(set(h.get("real", [])) | {0x80207628})
            out.append(h)
        else:
            kinds = ps if isinstance(ps, str) and ps else "pq"
            out.append(dict(name="lift:" + name, orig=addr, sym="dc_" + name, setup=_auto_setup(kinds, (), pt), discover=10, real=[0x80207628],
                            ret={"i": "int", "f": "float", "v": None, "l": "i64"}[ret]))
    return out

CASES += _lift_cases()
