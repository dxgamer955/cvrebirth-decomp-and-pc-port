#!/usr/bin/env python3
"""Deduce nombres de clase/metodos de game.dol a partir de vtables.
Uso (desde decomp/game): classes.py <dol> [--apply]
1) Constructores: funciones que guardan una direccion de .data en el offset 0 de un objeto.
2) Nombre de clase: la cadena identificador que precede a esa vtable en .data.
3) Slots de la vtable (desde V+8) -> metodos virtuales (slot0 = destructor)."""
import re, sys, glob, struct, collections, json
dol = sys.argv[1]; apply = "--apply" in sys.argv
d = open(dol, "rb").read()
SEC = {".data": (0x8022DB60, 0x56A50, 0x229C60), ".sdata": (0x803137A0, 0x2CB4, 0x2806C0)}
TEXT_LO, TEXT_HI = 0x80006F20, 0x800BB300      # codigo propio (ver sdk_split.py)
def rd(a, n=4):
    for b, s, o in SEC.values():
        if b <= a < b + s: return d[o + a - b: o + a - b + n]
    return None
def cstr(a):
    s = b""
    while True:
        c = rd(a, 1)
        if not c or c == b"\0" or len(s) > 100: break
        s += c; a += 1
    return s.decode("latin1")
def class_name(v):
    """vtable[0] -> typeinfo+8 ; typeinfo[0] -> nombre."""
    rt = rd(v, 4)
    if not rt: return None
    t = struct.unpack(">I", rt)[0] - 8
    np_ = rd(t, 4)
    if not np_: return None
    n = cstr(struct.unpack(">I", np_)[0])
    return n if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_:<>, ]*", n) else None
def ident_before(v, back=0x140):
    """Cadena identificador (alineada a 4) mas cercana por debajo de v."""
    best = None
    for a in range(v - 4, v - back, -4):
        w = rd(a, 4)
        if w is None: break
        if not re.match(rb"[A-Za-z_]", w[:1]): continue
        s = b""
        x = a
        while True:
            c = rd(x, 1)
            if not c or c == b"\0": break
            s += c; x += 1
            if len(s) > 80: s = b""; break
        if len(s) >= 3 and re.fullmatch(rb"[A-Za-z_][A-Za-z0-9_:<>]*", s) and rd(a - 4, 4) is not None:
            prev = rd(a - 1, 1)
            if prev == b"\0": best = (a, s.decode()); break
    return best

# 1) constructores (todo indexado por DIRECCION de funcion, para poder re-ejecutar tras renombrar)
addr_of = {}
cur = None; pend = None
for f in sorted(glob.glob("build/asm/*.s")):
    for l in open(f):
        m = re.match(r"# \.text:0x[0-9A-F]+ \| 0x([0-9A-F]{8}) \|", l)
        if m: pend = int(m.group(1), 16); continue
        m = re.match(r"\.fn (\S+),", l)
        if m and pend is not None and not m.group(1).startswith("gap_"):
            addr_of[m.group(1)] = pend
calls = collections.defaultdict(set)
ctors = {}
cur = None; regs = {}
for f in sorted(glob.glob("build/asm/*.s")):
    for l in open(f):
        m = re.match(r"\.fn (\S+),", l)
        if m: cur = addr_of.get(m.group(1)); regs = {}; continue
        m = re.search(r"\sbl (\S+)", l)
        if m and cur is not None and m.group(1) in addr_of: calls[cur].add(addr_of[m.group(1)])
        m = re.search(r"addi (r\d+), r\d+, lbl_([0-9A-F]{8})@l", l)
        if m: regs[m.group(1)] = int(m.group(2), 16); continue
        m = re.search(r"stw (r\d+), 0x0\((r\d+)\)", l)
        if m and m.group(1) in regs and rd(regs[m.group(1)], 4) is not None and cur is not None:
            ctors.setdefault(cur, []).append(regs[m.group(1)])
vts = {}   # vtable addr -> (clase, ctors)
skipped = collections.Counter(); dist = collections.Counter()
for fn, vl in ctors.items():
    own = vl[-1]
    n = class_name(own)
    if not n: skipped["sin typeinfo/nombre"] += 1; continue
    vts.setdefault(own, [n, []])[1].append(fn)
print("distancia nombre->vtable (top):", dist.most_common(6)); print("descartadas:", dict(skipped))
print(f"vtables con nombre: {len(vts)} | constructores: {sum(len(v[1]) for v in vts.values())}")

# 2) slots y roles
def clean(c): return re.sub(r"[^A-Za-z0-9_]", "_", c).strip("_")
names = {}  # funcion -> lista de (clase, rol)
slot0 = {}
vtdump = {}
for v, (cls, cs) in sorted(vts.items()):
    slot = 0; a = v + 8
    vtdump[cls + f"@{v:08X}"] = {"ctor_candidates": [f"{c:08X}" for c in cs], "slots": []}
    while True:
        w = rd(a, 4)
        if w is None: break
        p = struct.unpack(">I", w)[0]
        if p != 0 and not (TEXT_LO <= p < 0x8021E028): break   # 0 = virtual puro
        vtdump[cls + f"@{v:08X}"]["slots"].append(f"{p:08X}" if p else "pure")
        if p != 0:
            names.setdefault(p, []).append((cls, "dtor" if slot == 0 else f"vf{slot}"))
            if slot == 0: slot0[cls] = p
        slot += 1; a += 4
        if slot > 64: break
for v, (cls, cs) in sorted(vts.items()):
    inner = calls.get(slot0.get(cls), set())
    for fn in cs:
        if fn in names and any(c == cls for c, _ in names[fn]): continue   # ya es un slot
        role = "dtor_body" if fn in inner else "ctor"
        names.setdefault(fn, []).append((cls, role))
cnt = collections.Counter(len(v) for v in names.values())
print(f"funciones con nombre propuesto: {len(names)} (en 1 clase: {cnt[1]}, compartidas: {sum(c for k,c in cnt.items() if k>1)})")
for k, v in list(names.items())[:6]: print(" ", f"{k:08X}", v[:3])

json.dump(vtdump, open("config/vtables.json", "w"), indent=1)
# 3) nombres finales: solo funciones de una unica clase. 'ctor' solo si la clase tiene <=2 candidatos
# (con mas, son duenos que contienen la clase como miembro con el constructor inline).
ctor_cnt = collections.Counter(c for lst in names.values() for c, r in lst if r == "ctor")
final = {}; used = collections.Counter()
for fn, lst in sorted(names.items()):
    cls = {c for c, _ in lst}
    if len(cls) != 1: continue
    c = next(iter(cls)); role = sorted({r for _, r in lst})[0]
    if role == "ctor" and ctor_cnt[c] > 2: continue
    base = f"{clean(c)}_{role}"
    used[base] += 1
    final[fn] = base if used[base] == 1 else f"{base}_{used[base]}"
print(f"nombres finales: {len(final)}")
json.dump({f"{k:08X}": v for k, v in final.items()}, open("config/auto_names.json", "w"), indent=0)
if apply:
    out, n = [], 0
    for l in open("config/symbols.txt"):
        m = re.match(r"(fn_([0-9A-F]{8})) = ", l)
        if m and int(m.group(2), 16) in final:
            l = final[int(m.group(2), 16)] + l[len(m.group(1)):]; n += 1
        out.append(l)
    open("config/symbols.txt", "w").writelines(out)
    print(f"renombrados en symbols.txt: {n}")
