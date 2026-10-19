#!/usr/bin/env python3
"""Prueba diferencial de las subarmas arrojadizas (hacha, cruz, reloj, agua bendita, fuego del latigo) contra src/gameplay.
Las funciones auxiliares del juego se sustituyen por senuelos que registran sus argumentos,
de modo que ambas versiones las llaman igual.
Uso: test_thrown.py [n] [semilla]"""
import math, os, random, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
from emu import Emu
from unicorn.ppc_const import UC_PPC_REG_FPR1, UC_PPC_REG_3, UC_PPC_REG_4

root = os.path.join(os.path.dirname(__file__), "..", "..")
# Con OVERLAY=1 se prueba el overlay REAL: se enlaza en 0x817C0000, se inyectan sus palabras y
# los trampolines en las direcciones originales (src/hooks.txt) y se compara llamar a la
# funcion original (sin parchear) con llamarla ya parcheada, como lo haria Dolphin.
OVERLAY = os.environ.get("OVERLAY") == "1"
e = Emu(f"{root}/orig/game.dol", f"{root}/decomp/game/config/symbols.txt",
        f"{root}/build/overlay/overlay.elf" if OVERLAY else f"{root}/build/tests/decomp.elf")
CASES = {"Axe": (0x800A7A80, e.syms["_ZN15DrcSubWeaponAxe6UpdateEv"]),
         "Cross": (0x800A7C90, e.syms["_ZN17DrcSubWeaponCross6UpdateEv"]),
         "Watch": (0x800A7ED0, e.syms["_ZN17DrcSubWeaponWatch6UpdateEv"]),
         "HolyWater": (0x800A84C0, e.syms["_ZN21DrcSubWeaponHolyWater6UpdateEv"]),
         "WhipFire": (0x800A8870, e.syms["_ZN20DrcSubWeaponWhipFire6UpdateEv"]),
         "Base": (0x800A7180, e.syms["_ZN16DrcSubWeaponBase6UpdateEv"])}
SCR, SCR_SIZE = 0x80900000, 0x2000
# globales: (direccion, tipo) tipo p = puntero, f = float
PTRS = [0x8031647C, 0x80316544, 0x80316460]
FLOATS = {0x80318848: 64.0, 0x80318828: 0.0, 0x80318850: 0.3, 0x80318824: 4.0, 0x8031884C: -4.0,
          0x80318830: 2.0, 0x80318854: -8.0, 0x80318858: 16.0, 0x8031885C: 20.0, 0x80318860: 0.1,
          0x80318864: 1.0, 0x80318868: -1.0, 0x80318870: 0.05, 0x80318840: 1.0}
TABLE = [0x802439D8, 0x802439DC, 0x802439E0]   # velocidad al pegarse (3 floats)
COUNTER = 0x80316528
GLOBALS = [(a, 4) for a in PTRS] + [(a, 4) for a in FLOATS] + [(a, 4) for a in TABLE] + [(COUNTER, 4)]
REGIONS = [(SCR, SCR_SIZE)] + GLOBALS
MGR, OBJ2, VT, MOCKFN = SCR + 0x1000, SCR + 0x1400, SCR + 0x1800, 0x81700200
MOCK_SETPOS, MOCK_WPOS = 0x81700210, 0x81700220

log, ret = [], [0]
rand_vals, rand_i, new_ret, new_i, target, alpha = [0.5], [0], [0], [0], [(0, 0)], [0]
def bits(v): return struct.unpack(">Q", struct.pack(">d", v))[0]
def stub_rand(m):
    log.append(("rand", m.reg(3)))
    m.uc.reg_write(UC_PPC_REG_FPR1, bits(rand_vals[rand_i[0] % len(rand_vals)])); rand_i[0] += 1
def stub_getpos(m):
    log.append(("getpos", m.reg(3)))
    m.uc.reg_write(UC_PPC_REG_4, target[0][1]); return target[0][0]
e.stub(0x80009070, lambda m: (log.append(("onscreen", m.fpr(1), m.reg(3), m.reg(4))), ret[0])[1])
e.stub(0x801280B0, lambda m: log.append(("setpos", m.fpr(1), m.fpr(2), m.reg(3))))
e.stub(0x800B6740, lambda m: log.append(("sound", m.reg(3), m.reg(4))))
def stub_new(m):
    log.append(("new", m.reg(3)))
    v = new_ret[new_i[0] % len(new_ret)]; new_i[0] += 1
    return v
e.stub(0x8015CB60, stub_new)
e.stub(0x801398F0, stub_rand)
e.stub(0x800AE870, lambda m: log.append(("spawn", m.fpr(1), m.reg(3), m.reg(4), m.reg(5), m.read(m.reg(6), 8))))
e.stub(0x80207650, lambda m: (log.append(("find", m.reg(3), m.reg(4), m.reg(5), m.reg(6), m.reg(7))), MGR)[1])
e.stub(MOCKFN, stub_getpos)
e.stub(0x800B68B0, lambda m: log.append(("snd2", m.reg(3), m.fpr(1))))
e.stub(0x8001CB20, lambda m: log.append(("setobj", m.reg(3), m.reg(4))))
e.stub(0x80128330, lambda m: (log.append(("getalpha", m.reg(3))), alpha[0])[1])
e.stub(0x80128320, lambda m: log.append(("setalpha", m.reg(3), m.reg(4))))
# --- fuego del latigo ---
wpos, wpos_i, trace_ret, trace_i, trace_out, wh = [], [0], [], [0], [0.0] * 4, {"alive": 1, "frame": 0, "dur": 0.0}
def stub_wpos(m):
    log.append(("wpos", m.reg(3)))
    v = wpos[wpos_i[0] % len(wpos)]; wpos_i[0] += 1
    m.uc.reg_write(UC_PPC_REG_4, v[1]); return v[0]
def stub_trace(m):
    log.append(("trace", m.reg(3), m.read(m.reg(4), 8), m.read(m.reg(5), 8)))
    m.write(m.reg(6), struct.pack(">4f", *trace_out))
    r = trace_ret[trace_i[0] % len(trace_ret)]; trace_i[0] += 1
    return r
def stub_sqrt(m):
    x = struct.unpack(">d", struct.pack(">Q", m.fpr(1)))[0]
    log.append(("sqrt", m.fpr(1)))
    try: r = math.sqrt(x)
    except ValueError: r = float("nan")
    m.uc.reg_write(UC_PPC_REG_FPR1, bits(r))
e.stub(MOCK_WPOS, stub_wpos)
e.stub(MOCK_SETPOS, lambda m: log.append(("setpos28", m.reg(3), m.read(m.reg(4), 8))))
e.stub(0x800084C0, stub_trace)
e.stub(0x800A4D00, lambda m: log.append(("hitreset", m.reg(3))))
e.stub(0x8002EE40, lambda m: log.append(("spritename", m.reg(3), m.reg(4), m.reg(5))))
e.stub(0x80127D90, lambda m: (log.append(("alive", m.reg(3))), wh["alive"])[1])
e.stub(0x80133A50, lambda m: (log.append(("frame", m.reg(3))), wh["frame"] & 0xFFFFFFFF)[1])
e.stub(0x80127EE0, lambda m: (log.append(("dur", m.reg(3))), m.uc.reg_write(UC_PPC_REG_FPR1, bits(wh["dur"])))[0])
e.stub(0x80216698, stub_sqrt)
MOCK_VF20 = 0x81700230
e.stub(MOCK_VF20, lambda m: (log.append(("vf20", m.reg(3), m.reg(4))), 0xC0DE0000 | (m.reg(4) & 0xFFFF))[1])
e.stub(0x800A6160, lambda m: (log.append(("valanime", m.reg(3))), m.reg(3) + 0x40)[1])
e.stub(0x8013A4D0, lambda m: (log.append(("animetask", m.reg(3))), (m.reg(3) * 3 + 1) & 0xFFFFFFFF)[1])
e.stub(0x800A6410, lambda m: (log.append(("fadetask", m.reg(3), m.reg(4), m.reg(5), m.reg(6))), (m.reg(3) ^ 0x5555) & 0xFFFFFFFF)[1])
e.stub(0x8018E910, lambda m: log.append(("subinit", m.reg(3))))
e.stub(0x8013A800, lambda m: log.append(("addchild", m.reg(3), m.reg(4))))
e.stub(0x80009C00, lambda m: (log.append(("terrain", m.reg(3), m.reg(4))), m.reg(3))[1])

if OVERLAY:
    sys.path.insert(0, f"{root}/decomp/tools")
    import overlay as ov
    HOOKS = ov.parse_hooks(f"{root}/src/hooks.txt")
    PATCH = {}
    for _w_addr, _w in ov.patches(f"{root}/build/overlay/overlay.elf", HOOKS):
        if _w_addr == ov.ARENA_END_ADDR: continue                   # reserva del arena: no es codigo
        if _w_addr >= ov.BASE: e.w32(_w_addr, _w)                  # imagen del overlay
        else: PATCH.setdefault(max(h[0] for h in HOOKS if h[0] <= _w_addr), []).append(_w)   # trampolin de cada hook
    in_overlay = [0]
    from unicorn import UC_HOOK_CODE
    e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, sz, ud: in_overlay.__setitem__(0, in_overlay[0] + 1), begin=ov.BASE, end=ov.BASE + ov.SIZE_LIMIT - 1)
    def install(addr):
        saved = e.read(addr, 16)
        for k, w in enumerate(PATCH[addr]): e.w32(addr + 4 * k, w)
        e.uc.ctl_flush_tb()      # Unicorn no invalida los bloques traducidos al escribir codigo
        return saved

def make_state(rng, name):
    mem = bytearray(rng.randbytes(SCR_SIZE))
    obj, hit, sprite = SCR + 0x100, SCR + 0x400, SCR + 0x800
    def w(off, v): mem[off - SCR:off - SCR + 4] = struct.pack(">I", v)
    def wf(off, v): mem[off - SCR:off - SCR + 4] = struct.pack(">f", v)
    w(obj + 0xD4, sprite); w(obj + 0xD8, hit)
    mem[hit - SCR + 0x9C] = rng.choice([0, 0, 1, 255])
    for o in (0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C):
        wf(obj + o, rng.uniform(-300, 300))
    w(obj + 8, rng.choice([0, 1, 3, 0x7FFFFFFF]))
    fl = dict(FLOATS)
    if rng.random() < 0.4:                       # constantes alteradas: no deben importar los valores concretos
        for a in fl:
            if a != 0x80318828: fl[a] = rng.choice([fl[a], rng.uniform(0, 30), -rng.uniform(0, 30)])
    thr = fl[0x80318828] = rng.choice([0.0, rng.uniform(-5, 5)])
    if rng.random() < 0.25: wf(obj + 0x88, thr)          # vel.y exactamente en el umbral
    # --- solo el reloj ---
    wf(obj + 0x90, rng.choice([-2.0, 0.0, 2.0, rng.uniform(-3, 3)]))        # m_dir
    w(obj + 0xE4, rng.choice([0, 0, 0, 1, 2, 5])); w(obj + 0xE8, rng.choice([-2, 0, 1, 1, 2, 5]) & 0xFFFFFFFF)
    if rng.random() < 0.4: wf(obj + 0x84, rng.choice([fl[0x8031884C], fl[0x80318824], 0.0]))   # vel.x ya en el objetivo
    # --- solo el agua bendita ---
    w(obj + 0xE0, rng.choice([0, 1, 1, 1, 2, 2, 100, 100, 3, 99, 0xFFFFFFFF]))
    w(obj + 0xE8, rng.choice([0, 59, 119, 179, 239, 240, 0xFFFFFFFF, 0xFFFFFFC4, rng.randrange(0, 600), rng.randrange(0, 2**32)]))
    mem[obj - SCR + 0xF0] = rng.choice([0, 1, 255])
    alpha[0] = rng.choice([0, 1, 7, 8, 9, 10, 255, 0xFFFFFFFF, rng.randrange(0, 2**32)])
    if name == "WhipFire":
        w(obj, VT); w(VT + 0x28, MOCK_SETPOS)
        mem[obj - SCR + 0xF0] = rng.choice([0, 0, 1, 255])
        w(obj + 0xE0, rng.choice([0, 0, 0, 1, 1, 2]))
        if rng.random() < 0.4:                    # valores binarios exactos: la diferencia puede empatar con el paso
            fl[0x80318870] = rng.choice([0.5, 0.25, 1.0]); fl[0x80318824] = rng.choice([0.0, 1.0, 4.0, -3.0])
        r = rng.random()
        if r < 0.15: wf(obj + 0x84, fl[0x80318828]); wf(obj + 0x88, fl[0x80318828])
        elif r < 0.5:
            c, st = fl[0x80318824], fl[0x80318870]
            wf(obj + 0x88, rng.choice([c, c + st, c - st, c + st * 0.5, c - st * 0.5, c + st * 2, c - st * 2, 0.0]))
        wpos[:] = [(struct.unpack(">I", struct.pack(">f", rng.choice([0.0, rng.uniform(-300, 300)])))[0],
                    struct.unpack(">I", struct.pack(">f", rng.choice([0.0, rng.uniform(-300, 300)])))[0]) for _ in range(3)]
        trace_ret[:] = [rng.choice([0, 0, 1]), rng.choice([0, 1])]
        trace_out[:] = [rng.uniform(-5, 5), rng.choice([-1.0, 0.0, 1.0, rng.uniform(-3, 3)]), rng.uniform(-5, 5), rng.uniform(-30, 30)]
        wh["alive"] = rng.choice([0, 1, 1, 1]); wh["frame"] = rng.choice([0, 1, 5, 10, 100, -3, rng.randrange(-2**31, 2**31)])
        wh["dur"] = struct.unpack(">f", struct.pack(">f", rng.choice([0.0, 5.0, 10.0, rng.uniform(-20, 120)])))[0]
        if rng.random() < 0.3: wh["dur"] = struct.unpack(">f", struct.pack(">f", float(wh["frame"] - fl[0x8031885C])))[0]   # empate exacto
    if name == "Base":
        mem[obj - SCR + 0x10C] = rng.choice([0, 1, 255]); mem[obj - SCR + 0x10D] = rng.choice([0, 1, 255])
        w(MGR, VT); w(VT + 0x20, MOCK_VF20)
        new_ret[:] = [rng.choice([0, SCR + 0x1C00, SCR + 0x1E00]), rng.choice([0, SCR + 0x1D00, SCR + 0x1F00])]
    w(MGR + 0x220 - 0, OBJ2); w(OBJ2, VT); w(VT + 0x30, MOCK_WPOS if name == "WhipFire" else MOCKFN)
    px, py = (struct.unpack(">f", mem[obj - SCR + 0x78:obj - SCR + 0x7C])[0], struct.unpack(">f", mem[obj - SCR + 0x7C:obj - SCR + 0x80])[0])
    dx = rng.choice([0.0, 15.9, 16.0, 17.0, -15.9, -16.0, rng.uniform(-60, 60)])
    dy = rng.choice([0.0, 19.9, 20.0, 21.0, -19.9, rng.uniform(-60, 60)])
    tx, ty = struct.pack(">f", px + dx), struct.pack(">f", py + fl[0x8031885C] + dy)
    target[0] = (struct.unpack(">I", tx)[0], struct.unpack(">I", ty)[0])
    rand_vals[:] = [struct.unpack('>f', struct.pack('>f', rng.random()))[0] for _ in range(6)]   # fn_801398F0 devuelve un float
    if name != "Base": new_ret[:] = [SCR + 0x1C00 if name == "WhipFire" else rng.choice([0, SCR + 0x1C00])]
    glob = [struct.pack(">I", rng.randrange(0x80B00000, 0x80C00000, 4)) for _ in PTRS] + [struct.pack(">f", fl[a]) for a in FLOATS]
    glob += [struct.pack(">f", rng.choice([0.0, rng.uniform(-9, 9)])) for _ in TABLE] + [struct.pack(">i", rng.randrange(-3, 6))]
    return bytes(mem), glob, obj

def run(fn, mem, glob, obj, onscreen):
    e.write(SCR, mem)
    for (a, _), g in zip(GLOBALS, glob): e.write(a, g)
    log.clear(); ret[0] = onscreen; rand_i[0] = 0; wpos_i[0] = 0; trace_i[0] = 0; new_i[0] = 0
    e.call(fn, (obj,))
    return [e.read(a, n) for a, n in REGIONS], list(log)

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    total = 0
    for name, (orig, ours) in CASES.items():
        rng = random.Random(seed)
        bad = shown = 0
        for t in range(n):
            mem, glob, obj = make_state(rng, name); onscreen = rng.choice([0, 1])
            a = run(orig, mem, glob, obj, onscreen)
            if OVERLAY:
                saved = install(orig); b = run(orig, mem, glob, obj, onscreen); e.write(orig, saved); e.uc.ctl_flush_tb()
            else:
                b = run(ours, mem, glob, obj, onscreen)
            if a != b:
                bad += 1
                if shown < 4:
                    shown += 1
                    d = [(hex(REGIONS[i][0] + o), x[o:o+4].hex(), y[o:o+4].hex()) for i, (x, y) in enumerate(zip(a[0], b[0]))
                         for o in range(0, len(x), 4) if x[o:o+4] != y[o:o+4]]
                    k = next((i for i in range(min(len(a[1]), len(b[1]))) if a[1][i] != b[1][i]), None)
                    print(f"[{name}::Update #{t}] difs={d[:4]} llamadas orig={len(a[1])} nuestro={len(b[1])} primera dif={k}")
        print(f"{name}: pruebas {n} | fallos {bad}" + (f" | instrucciones en overlay {in_overlay[0]}" if OVERLAY else ""))
        if OVERLAY and in_overlay[0] == 0:
            print(f"{name}: el trampolin no llego al overlay"); bad += 1
        total += bad
    return 1 if total else 0

if __name__ == "__main__":
    sys.exit(main())
