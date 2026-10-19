#!/usr/bin/env python3
"""Traduce automaticamente las funciones triviales de game.dol a C++.

Una funcion es "trivial" si es codigo recto sin ramas que termina en `blr` o en un salto de
cola (`b destino`): getters, setters, constantes, thunks con ajuste de `this`... Se ejecuta
simbolicamente (registros r3-r10 / f1-f4 = argumentos) y se emite C++ equivalente que usa los
accesores de memoria de include/guest_mem.h. La verificacion la hace el fuzzer
(tests/difftest/fuzz.py) con los casos que genera este mismo script.

Uso: trivial.py [--max-bytes N]
Salida: src/auto/trivial.cpp y tests/difftest/auto_cases.py
"""
import re, struct, sys, os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SDA, SDA2 = 0x8031B7A0, 0x8031EF60
GAME_LO, GAME_HI = 0x80006F20, 0x80164800     # rango de codigo del juego (sin SDK)

def load_dol():
    d = open(f"{ROOT}/orig/game.dol", "rb").read()
    h = lambda o: struct.unpack(">I", d[o:o + 4])[0]
    secs = [(h(i * 4), h(0x48 + i * 4), h(0x90 + i * 4)) for i in range(18) if h(0x90 + i * 4)]
    def rd(a, n):
        for off, ad, sz in secs:
            if ad <= a < ad + sz: return d[off + a - ad: off + a - ad + n]
    return rd

def functions():
    out = []
    for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
        m = re.match(r"^(\S+) = \.text:0x([0-9A-F]+); // type:function size:0x([0-9A-F]+)", l)
        if m: out.append((int(m.group(2), 16), int(m.group(3), 16), m.group(1)))
    return out

def sext16(v): return v - 0x10000 if v & 0x8000 else v
def hexs(v): return f"{v & 0xFFFFFFFF:#x}"

class Bail(Exception): pass

class Fn:
    """Estado de la ejecucion simbolica de una funcion recta."""
    def __init__(self):
        self.g = {i: None for i in range(32)}     # expresion C (u32) de cada GPR
        self.f = {i: None for i in range(32)}     # (tipo, expresion) de cada FPR; tipo 'f' f32, 'd' f64
        for i in range(8): self.g[3 + i] = f"a{i}"
        for i in range(4): self.f[1 + i] = ("f", f"f{i}")
        self.g[13], self.g[2] = hexs(SDA), hexs(SDA2)
        self.used_g, self.used_f = set(), set()
        self.written_g, self.written_f = set(), set()
        self.stmts, self.ptr_args = [], set()
        self.tmp = 0
        self.src = {}                              # temporal -> expresion de la direccion de la que se cargo
        self.chains = set()                        # (arg, offset): palabra cargada que luego se usa como direccion

    def cv(self, r):
        """Valor constante del registro (int) o None."""
        v = self.g[r]
        return v if isinstance(v, int) else (int(v, 16) if isinstance(v, str) and re.fullmatch(r"0x[0-9a-f]+", v) else None)

    def rg(self, r, base=False):
        v = self.g[r]
        if v is None: raise Bail(f"r{r} sin definir")
        if isinstance(v, int): return hexs(v)
        if 3 <= r <= 10 and v == f"a{r - 3}": self.used_g.add(r - 3)
        if base: self._mark_ptr(v)
        return v

    def _mark_ptr(self, expr):
        # los argumentos que solo aparecen rotados/desplazados (slwi...) son indices, no punteros
        expr = re.sub(r"\(\(a(\d) << \d+\) \| \(a\1 >> \d+\)\)", "", expr)
        for m in re.finditer(r"\ba(\d)\b", expr): self.ptr_args.add(int(m.group(1)))
        for m in re.finditer(r"\bt\d+\b", expr):
            src = self.src.get(m.group(0), "")
            mm = re.fullmatch(r"a(\d)(?: \+ (0x[0-9a-f]+))?", src)
            if mm: self.chains.add((int(mm.group(1)), int(mm.group(2) or "0", 16)))

    def wg(self, r, expr):
        if r in (1, 2, 13): raise Bail("escribe r1/r2/r13")
        if isinstance(expr, int):
            self.g[r] = expr & 0xFFFFFFFF; self.written_g.add(r); return
        self.g[r] = expr if re.fullmatch(r"[A-Za-z_]\w*|0x[0-9a-f]+", expr) else f"({expr})"
        self.written_g.add(r)

    def rf(self, r):
        v = self.f[r]
        if v is None: raise Bail(f"f{r} sin definir")
        if 1 <= r <= 4 and v == ("f", f"f{r - 1}"): self.used_f.add(r - 1)
        return v

    def wf(self, r, v):
        t, e = v
        if not re.fullmatch(r"[A-Za-z_]\w*|-?[0-9.]+", e): e = f"({e})"
        self.f[r] = (t, e); self.written_f.add(r)

    def temp(self, ty, expr):
        self.tmp += 1; n = f"t{self.tmp}"
        self.stmts.append(f"{ty} {n} = {expr};")
        m = re.fullmatch(r"LD32\((.*)\)", expr)
        if m: self.src[n] = m.group(1)
        return n

def addr(fn, ra, d):
    if ra == 0: return hexs(d)
    c = fn.cv(ra)
    if c is not None: return hexs(c + d)
    base = fn.rg(ra, base=True)
    return base if d == 0 else f"{base} + {hexs(d)}"

def step(fn, w):
    op = w >> 26
    rd, ra, rb = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    d = sext16(w & 0xFFFF)
    if op == 14:
        c = 0 if ra == 0 else fn.cv(ra)
        fn.wg(rd, (c + d) if c is not None else f"{fn.rg(ra)} + {hexs(d)}"); return
    if op == 15:
        c = 0 if ra == 0 else fn.cv(ra)
        fn.wg(rd, (c + (d << 16)) if c is not None else f"{fn.rg(ra)} + {hexs(d << 16)}"); return
    if op in (24, 25, 26, 27):                      # ori oris xori xoris (rd = rS, ra = destino)
        imm = (w & 0xFFFF) << (16 if op in (25, 27) else 0)
        o = "|" if op in (24, 25) else "^"
        c = fn.cv(rd)
        if c is not None: fn.wg(ra, (c | imm) if o == "|" else (c ^ imm)); return
        fn.wg(ra, f"{fn.rg(rd)} {o} {hexs(imm)}"); return
    if op == 28 or op == 29:                        # andi. andis.
        imm = (w & 0xFFFF) << (16 if op == 29 else 0)
        c = fn.cv(rd)
        if c is not None: fn.wg(ra, c & imm); return
        fn.wg(ra, f"{fn.rg(rd)} & {hexs(imm)}"); return
    if op in (32, 34, 40, 42):                      # lwz lbz lhz lha
        ty, fnm = {32: ("u32", "LD32"), 34: ("u32", "LD8"), 40: ("u32", "LD16"), 42: ("u32", "LD16S")}[op]
        fn.wg(rd, fn.temp("u32", f"{fnm}({addr(fn, ra, d)})")); return
    if op in (48, 50):                              # lfs lfd
        if op == 48: fn.wf(rd, ("f", fn.temp("f32", f"LDF({addr(fn, ra, d)})")))
        else:        fn.wf(rd, ("d", fn.temp("f64", f"LDD({addr(fn, ra, d)})")))
        return
    if op in (36, 38, 44):                          # stw stb sth
        fnm = {36: "ST32", 38: "ST8", 44: "ST16"}[op]
        fn.stmts.append(f"{fnm}({addr(fn, ra, d)}, {fn.rg(rd)});"); return
    if op in (52, 54):                              # stfs stfd
        t, e = fn.rf(rd)
        if (op == 52) != (t == "f"): raise Bail("tipo float/double")
        fn.stmts.append(f"{'STF' if op == 52 else 'STD'}({addr(fn, ra, d)}, {e});"); return
    if op == 21:                                    # rlwinm
        sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
        mask = 0
        i = mb
        while True:
            mask |= 1 << (31 - i)
            if i == me: break
            i = (i + 1) & 31
        c = fn.cv(rd)
        if c is not None:
            r_ = ((c << sh) | (c >> (32 - sh))) & 0xFFFFFFFF if sh else c
            fn.wg(ra, r_ & mask); return
        src = fn.rg(rd)
        rot = src if sh == 0 else f"(({src} << {sh}) | ({src} >> {32 - sh}))"
        fn.wg(ra, f"{rot} & {hexs(mask)}"); return
    if op == 31:
        xo = (w >> 1) & 0x3FF
        ca, cb, cd = fn.cv(ra), fn.cv(rb), fn.cv(rd)
        if xo == 444:                                                                         # or / mr
            if rd == rb: fn.wg(ra, cd if cd is not None else fn.rg(rd))
            else: fn.wg(ra, (cd | cb) if cd is not None and cb is not None else f"{fn.rg(rd)} | {fn.rg(rb)}")
            return
        if xo == 266:   fn.wg(rd, (ca + cb) if ca is not None and cb is not None else f"{fn.rg(ra)} + {fn.rg(rb)}"); return
        if xo == 40:    fn.wg(rd, (cb - ca) if ca is not None and cb is not None else f"{fn.rg(rb)} - {fn.rg(ra)}"); return
        if xo == 28:    fn.wg(ra, (cd & cb) if cd is not None and cb is not None else f"{fn.rg(rd)} & {fn.rg(rb)}"); return
        if xo == 316:   fn.wg(ra, f"{fn.rg(rd)} ^ {fn.rg(rb)}"); return
        if xo == 104:   fn.wg(rd, (-ca) if ca is not None else f"0 - {fn.rg(ra)}"); return
        if xo == 235:   fn.wg(rd, f"{fn.rg(ra)} * {fn.rg(rb)}"); return
        if xo == 954:   fn.wg(ra, f"(u32)(s32)(s8){fn.rg(rd)}"); return
        if xo == 922:   fn.wg(ra, f"(u32)(s32)(s16){fn.rg(rd)}"); return
        if xo == 26:    fn.wg(ra, f"CNTLZW({fn.rg(rd)})"); return
        if xo == 824:   fn.wg(ra, f"(u32)((s32){fn.rg(rd)} >> {rb})"); return        # srawi (el shift va en el campo rb)
        if xo == 24:    fn.wg(ra, f"SLW({fn.rg(rd)}, {fn.rg(rb)})"); return
        if xo == 536:   fn.wg(ra, f"SRW({fn.rg(rd)}, {fn.rg(rb)})"); return
        if xo == 23:    fn.wg(rd, fn.temp("u32", f"LD32({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)})")); return
        if xo == 87:    fn.wg(rd, fn.temp("u32", f"LD8({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)})")); return
        if xo == 279:   fn.wg(rd, fn.temp("u32", f"LD16({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)})")); return
        if xo == 343:   fn.wg(rd, fn.temp("u32", f"LD16S({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)})")); return
        if xo == 151:   fn.stmts.append(f"ST32({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)}, {fn.rg(rd)});"); return
        if xo == 215:   fn.stmts.append(f"ST8({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)}, {fn.rg(rd)});"); return
        if xo == 407:   fn.stmts.append(f"ST16({fn.rg(ra, True) if ra else '0'} + {fn.rg(rb, True)}, {fn.rg(rd)});"); return
        raise Bail(f"op31 xo={xo}")
    if op == 63:
        xo = (w >> 1) & 0x3FF
        if xo == 72:   fn.wf(rd, fn.rf(rb)); return                                    # fmr
        if xo == 40:   fn.wf(rd, ("f", f"-{fn.rf(rb)[1]}")) if fn.rf(rb)[0] == "f" else (_ for _ in ()).throw(Bail("fneg d")); return
        if xo == 12:   t, e = fn.rf(rb); fn.wf(rd, ("f", f"(f32){e}")); return        # frsp
        raise Bail(f"op63 xo={xo}")
    if op == 59:
        xo = (w >> 1) & 0x1F
        a_, b_, c_ = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
        sym = {21: "+", 20: "-", 18: "/"}.get(xo)
        if sym:
            x, y = fn.rf(a_), fn.rf(b_)
            if x[0] != "f" or y[0] != "f": raise Bail("fp tipo")
            fn.wf(rd, ("f", fn.temp("f32", f"{x[1]} {sym} {y[1]}")))
            return
        if xo == 25:
            x, y = fn.rf(a_), fn.rf(c_)
            if x[0] != "f" or y[0] != "f": raise Bail("fp tipo")
            fn.wf(rd, ("f", fn.temp("f32", f"{x[1]} * {y[1]}")))
            return
        raise Bail(f"op59 xo={xo}")
    raise Bail(f"op {op}")

def translate(rd, addr_, size, name, funcs):
    """-> dict con el cuerpo C++ y los datos del caso, o None si no es trivial."""
    code = rd(addr_, size)
    ws = [struct.unpack(">I", code[i:i + 4])[0] for i in range(0, size, 4)]
    fn = Fn()
    tail = None
    try:
        for i, w in enumerate(ws):
            if w == 0x4E800020:                          # blr
                if i != len(ws) - 1: raise Bail("blr en medio")
                break
            if w >> 26 == 18 and not (w & 3) and i == len(ws) - 1:     # b destino (salto de cola)
                li = w & 0x03FFFFFC
                if li & 0x02000000: li -= 0x04000000
                tail = (addr_ + 4 * i + li) & 0xFFFFFFFF
                break
            if w == 0x60000000: continue                 # nop
            step(fn, w)
        else:
            raise Bail("sin final")
    except Bail as e:
        return None, str(e)
    nargs = max(fn.used_g, default=-1) + 1
    nflt = max(fn.used_f, default=-1) + 1
    return dict(fn=fn, tail=tail, nargs=nargs, nflt=nflt), None

def emit(name, addr_, info, symname):
    fn, tail = info["fn"], info["tail"]
    sig_i = ", ".join(f"u32 a{i}" for i in range(8 if tail else info["nargs"]))
    sig_f = ", ".join(f"f32 f{i}" for i in range(4 if tail else info["nflt"]))
    sig = ", ".join(x for x in (sig_i, sig_f) if x)
    body = "\n".join("    " + s for s in fn.stmts)
    if tail is not None:
        args = []
        for i in range(8):
            e = fn.g[3 + i]
            args.append(hexs(e) if isinstance(e, int) else (e if e is not None else f"a{i}"))
        for i in range(4):
            v = fn.f[1 + i]
            if v is None: args.append(f"f{i}")
            elif v[0] != "f": raise Bail("float double en salto de cola")
            else: args.append(v[1])
        tname = symname.get(tail, f"fn_{tail:08X}")
        decl = f'extern "C" u32 {tname}(u32, u32, u32, u32, u32, u32, u32, u32, f32, f32, f32, f32);'
        ret = f"    return {tname}({', '.join(args)});"
        return decl, f'extern "C" u32 dc_{name}({sig}) {{\n{body}\n{ret}\n}}\n', "tail"
    if 1 in fn.written_f and 3 in fn.written_g: raise Bail("devuelve r3 y f1")
    if 1 in fn.written_f:
        t, e = fn.f[1]
        if t != "f": raise Bail("devuelve double")
        return None, f'extern "C" f32 dc_{name}({sig}) {{\n{body}\n    return {e};\n}}\n', "float"
    if 3 in fn.written_g:
        return None, f'extern "C" u32 dc_{name}({sig}) {{\n{body}\n    return {hexs(fn.g[3]) if isinstance(fn.g[3], int) else fn.g[3]};\n}}\n', "int"
    return None, f'extern "C" void dc_{name}({sig}) {{\n{body}\n}}\n', "void"

def main():
    maxb = 64
    if "--max-bytes" in sys.argv: maxb = int(sys.argv[sys.argv.index("--max-bytes") + 1])
    rd = load_dol()
    funcs = functions()
    skip = set(l.split("#")[0].strip() for l in open(f"{ROOT}/decomp/tools/trivial_skip.txt")) - {""}
    names = {a: n for a, s, n in funcs}
    decls, bodies, cases, stats, why = set(), [], [], {}, {}
    for a, s, n in funcs:
        if not GAME_LO <= a < GAME_HI or s > maxb or n in skip: continue
        info, err = translate(rd, a, s, n, funcs)
        if info is None:
            why[err.split(" ")[0] + " " + (err.split(" ")[1] if " " in err else "")] = why.get(err, 0) + 1
            continue
        try:
            decl, body, kind = emit(n, a, info, names)
        except Bail as e:
            why[str(e)] = why.get(str(e), 0) + 1; continue
        if decl:
            decls.add(decl)
            t = info["tail"]
            if t not in names:                       # destino sin simbolo (seccion de arranque): se define absoluto
                decls.add(f'asm(".globl fn_{t:08X}\\n.set fn_{t:08X}, {t:#x}");')
        bodies.append(body)
        stats[kind] = stats.get(kind, 0) + 1
        fn = info["fn"]
        kinds = ["p" if i in fn.ptr_args else "i" for i in range(8 if kind == "tail" else info["nargs"])] + ["f"] * (4 if kind == "tail" else info["nflt"])
        cases.append((n, a, kinds, kind, info["tail"], sorted(fn.chains)))
    os.makedirs(f"{ROOT}/src/auto", exist_ok=True)
    with open(f"{ROOT}/src/auto/trivial.cpp", "w") as f:
        f.write("// GENERADO por decomp/tools/trivial.py: funciones triviales de game.dol (no editar a mano).\n")
        f.write('#include "guest_mem.h"\n\n')
        f.write("\n".join(sorted(decls)) + "\n\n")
        f.write("\n".join(bodies))
    with open(f"{ROOT}/tests/difftest/auto_cases.py", "w") as f:
        f.write("# GENERADO por decomp/tools/trivial.py: un caso del fuzzer por funcion trivial.\n")
        f.write("AUTO = [\n")
        for n, a, kinds, kind, tail, chains in cases:
            f.write(f"    ({n!r}, {a:#x}, {''.join(kinds)!r}, {kind!r}, {tail if tail else 0:#x}, {chains!r}),\n")
        f.write("]\n")
    print(f"{len(bodies)} funciones traducidas:", stats)
    top = sorted(why.items(), key=lambda kv: -kv[1])[:12]
    print("no traducidas (motivos):", top)

if __name__ == "__main__":
    main()
