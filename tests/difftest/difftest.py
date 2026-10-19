#!/usr/bin/env python3
"""Prueba diferencial: funciones ORIGINALES de game.dol vs codigo decompilado (src/), ambas
ejecutadas en Unicorn PowerPC sobre la misma memoria aleatoria y comparadas.
Cubre: ctor/dtor de ENDamaged, EcoScratch, ENDamager, ENRestricted y CTEngine::Update.
Uso: difftest.py [n_pruebas] [semilla]"""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
from emu import Emu

root = os.path.join(os.path.dirname(__file__), "..", "..")
e = Emu(f"{root}/orig/game.dol", f"{root}/decomp/game/config/symbols.txt", f"{root}/build/tests/decomp.elf")

SCR, SCR_SIZE, SLOT = 0x80900000, 0x10000, 0x80
VEC = 0x802439C0 + 0x18
REGIONS = [(SCR, SCR_SIZE), (0x802845C0, 0x20), (0x80316470, 8), (VEC, 12)]
LOG = e.syms["g_log"], e.syms["g_log_n"]

def sym(*names):
    for n in names:
        if n in e.syms: return e.syms[n]
    raise KeyError(names)

CLASSES = [
    dict(name="ENDamaged", ctor=0x800077A0, dtor=0x800078D0, vt=0x80243A24, grouped=True,
         head=0x802845C0, group=4, prev=0x3C, next=0x40, arrays=(0x1C, 0x28),
         o_ctor=sym("_ZN9ENDamagedC2Ei"), o_dtor=sym("_ZN9ENDamagedD2Ev"), o_vt=sym("_ZTV9ENDamaged") + 8),
    dict(name="EcoScratch", ctor=0x80007A70, dtor=0x80007AC0, vt=0x80243A00, grouped=True,
         head=0x802845D0, group=4, prev=0xC, next=0x10, arrays=(),
         o_ctor=sym("_ZN10EcoScratchC2Ei"), o_dtor=sym("_ZN10EcoScratchD2Ev"), o_vt=sym("_ZTV10EcoScratch") + 8),
    dict(name="ENDamager", ctor=0x800076D0, dtor=0x80007710, vt=0x80243A4C, grouped=False,
         head=0x80316474, prev=0x8, next=0xC, arrays=(),
         o_ctor=sym("_ZN9ENDamagerC2Ev", "_ZN9ENDamagerC1Ev"), o_dtor=sym("_ZN9ENDamagerD2Ev", "_ZN9ENDamagerD1Ev"),
         o_vt=sym("_ZTV9ENDamager") + 8),
    dict(name="ENRestricted", ctor=0x800075B0, dtor=0x80007610, vt=0x80243A78, grouped=False,
         head=0x80316470, prev=0x14, next=0x18, arrays=(),
         o_ctor=sym("_ZN12ENRestrictedC2Ev", "_ZN12ENRestrictedC1Ev"), o_dtor=sym("_ZN12ENRestrictedD2Ev", "_ZN12ENRestrictedD1Ev"),
         o_vt=sym("_ZTV12ENRestricted") + 8),
]

def snapshot(): return [e.read(a, n) for a, n in REGIONS]
def load(st):
    for (a, _), b in zip(REGIONS, st): e.write(a, b)

def diff(a, b, mask=()):
    out = []
    for (base, n), x, y in zip(REGIONS, a, b):
        for o in range(0, n, 4):
            if base + o not in mask and x[o:o + 4] != y[o:o + 4]:
                out.append((hex(base + o), x[o:o + 4].hex(), y[o:o + 4].hex()))
    return out

def w(img, off, v): img[off:off + 4] = struct.pack(">I", v)

def build_state(rng, spec_list_kind=None):
    """Memoria aleatoria con listas bien formadas para TODAS las clases (las que no se prueban
    sirven de ruido). Devuelve (estado, objetos por clase, pool libre)."""
    scr = bytearray(rng.randbytes(SCR_SIZE)); heads = bytearray(rng.randbytes(0x20)); h2 = bytearray(8)
    pool = [SCR + s * SLOT for s in rng.sample(range(0x10, 0x10 + 220), 60)]
    objs = {}
    for c in CLASSES:
        lists = [[] for _ in range(4 if c["grouped"] else 1)]
        for L in lists:
            for _ in range(rng.randint(0, 4)): L.append(pool.pop())
        objs[c["name"]] = lists
        for g, L in enumerate(lists):
            for i, o in enumerate(L):
                off = o - SCR
                if c["grouped"]: w(scr, off + c["group"], g)
                w(scr, off + c["prev"], L[i - 1] if i else 0)
                w(scr, off + c["next"], L[i + 1] if i + 1 < len(L) else 0)
                for a in c["arrays"]:
                    w(scr, off + a, 0 if rng.random() < 0.4 else 0x80B00000 + rng.randrange(0, 0x10000, 4))
            hv = L[0] if L else 0
            if c["head"] >= 0x80316470:
                w(h2, c["head"] - 0x80316470, hv)
            else:
                w(heads, c["head"] - 0x802845C0 + 4 * g, hv)
    return [bytes(scr), bytes(heads), bytes(h2), rng.randbytes(12)], objs, pool

def mask_vptr(st, addr, ours, orig):
    st = [bytearray(x) for x in st]; o = addr - SCR
    if st[0][o:o + 4] == struct.pack(">I", ours): st[0][o:o + 4] = struct.pack(">I", orig)
    return [bytes(x) for x in st]

def test_classes(rng, n, report):
    bad = {}
    for t in range(n):
        st, objs, pool = build_state(rng)
        for c in CLASSES:
            # constructor
            this = pool[-1]; g = rng.randrange(4); args = (this, g) if c["grouped"] else (this,)
            load(st); e.call(c["ctor"], args); s1 = snapshot(); d1 = list(e.deletes)
            load(st); e.call(c["o_ctor"], args); s2 = mask_vptr(snapshot(), this, c["o_vt"], c["vt"]); d2 = list(e.deletes)
            dd = diff(s1, s2)
            if dd or d1 != d2:
                bad[c["name"] + "_ctor"] = bad.get(c["name"] + "_ctor", 0) + 1
                report(f"[{c['name']}_ctor #{t}] grupo={g} difs={dd[:4]}")
            # destructor (con y sin delete)
            alive = [o for L in objs[c["name"]] for o in L]
            if not alive: continue
            v = rng.choice(alive); flag = rng.choice([0, 1])
            load(st); e.call(c["dtor"], (v, flag)); s1 = snapshot(); d1 = list(e.deletes)
            load(st); e.call(c["o_dtor"], (v,)); s2 = snapshot(); d2 = list(e.deletes) + ([v] if flag else [])
            dd = diff(s1, s2, mask=(v,))
            if dd or d1 != d2:
                bad[c["name"] + "_dtor"] = bad.get(c["name"] + "_dtor", 0) + 1
                report(f"[{c['name']}_dtor #{t}] flag={flag} difs={dd[:4]} deletes={d1}/{d2}")
    return bad

# ---- CTEngine::Update con tablas virtuales senuelo ----
VT_D, VT_S = SCR + 0xE000, SCR + 0xE100         # tablas senuelo (CW: slots +0xC/+0x14; Itanium: +0x8/+0x10)
def mock_vtables():
    for vt, last in ((VT_D, "stub_on_hit"), (VT_S, "stub_hit")):
        words = [0, 0, sym("stub_exec"), sym("stub_exec"), sym(last), sym(last)]
        e.write(vt, struct.pack(">6I", *words))

def test_update(rng, n, report):
    bad = 0
    for t in range(n):
        st, objs, pool = build_state(rng)
        scr = bytearray(st[0]); engine = pool.pop()
        for L in objs["ENDamaged"]:
            for o in L: w(scr, o - SCR, VT_D); scr[o - SCR + 8] = rng.choice([0, 1, 1, 255])
        for L in objs["EcoScratch"]:
            for o in L:
                w(scr, o - SCR, VT_S); scr[o - SCR + 8] = rng.choice([0, 1, 1, 7]); scr[o - SCR + 0x18] = rng.choice([0, 1])
        st[0] = bytes(scr)
        res = []
        for fn in (0x800086F0, sym("_ZN8CTEngine6UpdateEv")):
            load(st); mock_vtables(); e.write(LOG[1], b"\0\0\0\0"); e.write(LOG[0], bytes(4 * 4096))
            e.call(fn, (engine,))
            n_ = e.r32(LOG[1]); res.append((snapshot()[:3], [e.r32(LOG[0] + 4 * i) for i in range(n_)]))
        if res[0] != res[1]:
            bad += 1
            a, b = res[0][1], res[1][1]
            k = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
            report(f"[CTEngine::Update #{t}] llamadas orig={len(a)//3} nuestro={len(b)//3}, primera diferencia en indice {k}")
    return {"CTEngine_Update": bad} if bad else {}

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
    shown = [0]
    def report(msg):
        if shown[0] < 8: shown[0] += 1; print(msg)
    bad = test_classes(rng, n, report); bad.update(test_update(rng, n, report))
    print(f"pruebas por funcion: {n} | fallos: {bad if bad else 'ninguno'}")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
