#!/usr/bin/env python3
"""Elevador PowerPC -> C++ para game.dol (funciones con ramas, llamadas y bucles).

Cada funcion se traduce instruccion a instruccion a C++ con variables locales para los
registros (r0-r31 como u32, f0-f31 como f64, cr/ctr/lr/ca), saltos `goto` y etiquetas en los
destinos de rama, llamadas con los 8 enteros + 8 floats de argumento y el marco de pila como un
array local. Usa los ayudantes de include/ppc_lift.h. El resultado es C++ correcto (verificado
con tests/difftest/fuzz.py) pero de bajo nivel: sirve de base para decompilar a mano y para
ejecutar el juego sin el binario original.

Uso: lift.py [--names a,b] [--min N] [--max N] [--out src/auto/lifted.cpp]
"""
import os, re, struct, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SDA, SDA2 = 0x8031B7A0, 0x8031EF60
GAME_LO, GAME_HI = 0x80006F20, 0x80164800

class Bail(Exception): pass

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

def d_(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v

def jumptable_syms():
    """addr -> numero de entradas de cada jumptable_XXXXXXXX de symbols.txt."""
    out = {}
    for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
        m = re.match(r"^jumptable_[0-9A-Fa-f]+ = \.\w+:0x([0-9A-F]+); // type:object size:0x([0-9A-F]+)", l)
        if m: out[int(m.group(1), 16)] = int(m.group(2), 16) // 4
    return out

def sext(v, bits): return v - (1 << bits) if v & (1 << (bits - 1)) else v
def hx(v): return f"{v & 0xFFFFFFFF:#x}"

def ghidra_sigs():
    """addr -> (tipo_retorno, [clases de parametro]) con clase 'f' (float/double), 'p' (puntero), 'i' (entero).
    Read from Ghidra's pseudo-C if present; otherwise from decomp/game/config/sigs.json (an already extracted table, see export_sigs.py)."""
    if not os.path.exists(f"{ROOT}/decomp/game/pseudo_c/all.c"):
        import json
        path = f"{ROOT}/decomp/game/config/sigs.json"
        if not os.path.exists(path): raise SystemExit("decomp/game/pseudo_c/all.c (Ghidra export) and decomp/game/config/sigs.json are both missing")
        return {int(a, 16): (v[0], list(v[1])) for a, v in json.load(open(path)).items()}
    out, cur = {}, None
    for l in open(f"{ROOT}/decomp/game/pseudo_c/all.c", errors="replace"):
        m = re.match(r"^// ==== ([0-9a-f]{8}) ", l)
        if m: cur = int(m.group(1), 16); continue
        if cur is not None and l.strip():
            p = re.match(r"^([\w \*]+?)\(([^)]*)\)", l.strip())
            if p:
                head, args = p.group(1).strip(), p.group(2).strip()
                ret = "f" if re.match(r"(float|double)\b", head) and "*" not in head else ("v" if head.startswith("void") else ("l" if re.match(r"(undefined8|ulonglong|longlong|u?int64)\b", head) else "i"))
                ps = []
                if args not in ("", "void"):
                    for a in args.split(","):
                        a = a.strip()
                        ps.append("f" if re.match(r"(float|double)\b", a) and "*" not in a else ("p" if "*" in a else ("q" if "undefined" in a else "i")))
                out[cur] = (ret, ps)
            cur = None
    return out

def is_call_args_only(ws, i):
    """True si tras la llamada el siguiente evento es otra llamada (r3 y r4 pueden ser solo argumentos)."""
    for j in range(i + 1, min(len(ws), i + 14)):
        w = ws[j]; op = w >> 26
        if (op == 18 and (w & 1)) or w == 0x4E800421: return True
        if w == 0x4E800020 or op in (16, 18, 19): return False
    return False

# (la etiqueta del primer registro de cada tabla de ayudantes se llama __save_gpr/__restore_gpr/__save_fpr/__restore_fpr en symbols.txt)
CALLEE_SAVED_HELPERS = ("_savegpr", "_restgpr", "_savefpr", "_restfpr", "__save_gpr", "__restore_gpr", "__save_fpr", "__restore_fpr")

class Lifter:
    def __init__(self, rd, names, sigs):
        self.rd, self.names, self.sigs = rd, names, sigs
        self.callee_ret = {}        # addr -> 'i'|'f'|'v' (union sobre todos los sitios de llamada)
        self.callee_sites = {}
        self.cur_ret = "i"
        self.native = False
        self.jt = jumptable_syms()

    # ------------------------------------------------------------ lectura/escritura de registros
    @staticmethod
    def fields(w):
        return (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31

    def reads_writes(self, w):
        """(gpr_reads, gpr_writes, fpr_reads, fpr_writes) aproximado, para inferir retornos y argumentos."""
        op = w >> 26
        rd, ra, rb = self.fields(w)
        gr, gw, fr, fw = set(), set(), set(), set()
        if op in (14, 15, 12, 13, 8, 7):          # addi addis addic addic. subfic mulli
            if ra or op in (12, 13, 8, 7): gr.add(ra)
            gw.add(rd)
        elif op in (24, 25, 26, 27, 28, 29): gr.add(rd); gw.add(ra)
        elif op in (32, 34, 40, 42, 33, 35, 41, 43): gr.add(ra) if ra or op in (33, 35, 41, 43) else None; gw.add(rd); (gw.add(ra) if op in (33, 35, 41, 43) else None)
        elif op in (36, 38, 44, 37, 39, 45): gr.add(rd); (gr.add(ra) if ra else None); (gw.add(ra) if op in (37, 39, 45) else None)
        elif op in (48, 50, 49, 51): (gr.add(ra) if ra else None); fw.add(rd)
        elif op in (52, 54, 53, 55): fr.add(rd); (gr.add(ra) if ra else None)
        elif op in (11, 10): gr.add(ra)
        elif op == 21: gr.add(rd); gw.add(ra)
        elif op == 20: gr.add(rd); gr.add(ra); gw.add(ra)
        elif op == 31:
            xo = (w >> 1) & 0x3FF
            if xo in (0, 32): gr.update((ra, rb))
            elif xo in (444, 28, 316, 60, 124, 412, 24, 536, 792): gr.update((rd, rb)); gw.add(ra)
            elif xo in (824, 26, 954, 922): gr.add(rd); gw.add(ra)
            elif xo in (266, 40, 235, 75, 11, 459, 491, 10, 8, 138, 136): gr.update((ra, rb)); gw.add(rd)
            elif xo in (104, 202, 234, 200): gr.add(ra); gw.add(rd)
            elif xo in (23, 87, 279, 343): (gr.add(ra) if ra else None); gr.add(rb); gw.add(rd)
            elif xo in (151, 215, 407): gr.add(rd); (gr.add(ra) if ra else None); gr.add(rb)
            elif xo in (535, 599): (gr.add(ra) if ra else None); gr.add(rb); fw.add(rd)
            elif xo in (567, 631): gr.update((ra, rb)); gw.add(ra); fw.add(rd)
            elif xo in (695, 759): fr.add(rd); gr.update((ra, rb)); gw.add(ra)
            elif xo in (663, 727): fr.add(rd); (gr.add(ra) if ra else None); gr.add(rb)
            elif xo == 339: gw.add(rd)
            elif xo == 467: gr.add(rd)
            elif xo == 19: gw.add(rd)
        elif op in (63, 59):
            xo = (w >> 1) & 0x3FF
            x5 = (w >> 1) & 0x1F
            if op == 63 and xo in (0, 32): fr.update((ra, rb))
            elif op == 63 and xo in (72, 40, 264, 136, 12, 14, 15): fr.add(rb); fw.add(rd)
            elif x5 in (21, 20, 18): fr.update((ra, rb)); fw.add(rd)
            elif x5 == 25: fr.update((ra, (w >> 6) & 31)); fw.add(rd)
            elif x5 in (29, 28, 31, 30, 23): fr.update((ra, rb, (w >> 6) & 31)); fw.add(rd)
            elif x5 in (24, 22, 26): fr.add(rb); fw.add(rd)
        elif op in (46,): gr.add(ra); gw.update(range(rd, 32))
        elif op in (47,): gr.update(range(rd, 32)); gr.add(ra)
        elif op == 18 and (w & 1): gw.update(range(3, 13))
        return gr, gw, fr, fw

    def infer_call_ret(self, ws, i):
        """Mira las instrucciones tras la llamada ws[i] para decidir si el llamador usa r3 (i) o f1 (f)."""
        used_r3 = used_f1 = used_r4 = None
        for j in range(i + 1, min(len(ws), i + 14)):
            w = ws[j]
            op = w >> 26
            gr, gw, fr, fw = self.reads_writes(w)
            is_call = (op == 18 and (w & 1)) or w == 0x4E800421
            is_ret = w == 0x4E800020 or (op == 18 and not (w & 1))
            if is_call:                      # la siguiente llamada puede consumir r3/r4 como argumentos
                gr = set(gr) | {3}
            if is_ret:
                if self.cur_ret == "i": gr = set(gr) | {3}
                elif self.cur_ret == "l": gr = set(gr) | {3, 4}
                elif self.cur_ret == "f": fr = set(fr) | {1}
            if used_r3 is None:
                if 3 in gr: used_r3 = True
                elif 3 in gw: used_r3 = False
            if used_r4 is None:
                if 4 in gr: used_r4 = True
                elif 4 in gw: used_r4 = False
            if used_f1 is None:
                if 1 in fr: used_f1 = True
                elif 1 in fw: used_f1 = False
            if is_call or is_ret or (op in (16,) or (op == 19)): break
            if used_r3 is not None and used_f1 is not None and used_r4 is not None: break
        if used_f1: return "f"
        if used_r3 and used_r4: return "l"
        return "i" if used_r3 else "v"

    def find_jumptable(self, ws, i, addr):
        """Si ws[i] es un `bctr` de un switch (lwzx de una tabla conocida), devuelve sus destinos."""
        base = None
        for j in range(i - 1, max(-1, i - 14), -1):
            w = ws[j]
            if w >> 26 == 31 and ((w >> 1) & 0x3FF) == 23:                       # lwzx rD, rA, rB
                ra = (w >> 16) & 31
                lo = hi = None
                for k in range(j - 1, -1, -1):                                       # addi cerca; el lis puede estar mucho antes
                    wk = ws[k]
                    if lo is None and j - k > 12: break
                    if wk >> 26 == 14 and ((wk >> 21) & 31) == ra and lo is None:    # addi ra, rs, lo
                        lo = d_(wk); rs = (wk >> 16) & 31
                    elif lo is not None and wk >> 26 == 15 and ((wk >> 21) & 31) == rs and ((wk >> 16) & 31) == 0:   # lis rs, hi
                        hi = d_(wk); break
                if lo is not None and hi is not None: base = ((hi << 16) + lo) & 0xFFFFFFFF
                break
        if base is None: return None
        if base in self.jt: n = self.jt[base]
        else:                                    # sin simbolo: el limite sale del cmplwi previo (indice <= N -> N+1 entradas)
            n = None
            for j in range(i - 1, max(-1, i - 24), -1):
                w = ws[j]
                if w >> 26 == 10: n = (w & 0xFFFF) + 1; break
            if n is None or n > 256: return None
            tg = [struct.unpack(">I", self.rd(base + 4 * k, 4))[0] for k in range(n)]
            if not all(addr <= t < addr + 4 * len(ws) for t in tg): return None
            return tg
        raw = self.rd(base, 4 * n)
        return [struct.unpack(">I", raw[4 * k:4 * k + 4])[0] for k in range(n)]

    MEM_D = set(range(32, 56))
    MEM_X = (23, 87, 279, 343, 151, 215, 407, 535, 599, 663, 727, 55, 183)

    def arg_kinds(self, ws, ghidra, addr):
        """Cadena de clases de argumento para el fuzzer: enteros (r3..) y luego floats (f1..).
        'p' = se usa como base de acceso a memoria (siguiendo copias mr/addi), 'i' = leido como entero,
        'q' = desconocido (solo se pasa a otra funcion), 'f' = float leido."""
        alias = {n: n - 3 for n in range(3, 11)}
        aoff = {n: 0 for n in range(3, 11)}          # desplazamiento acumulado sobre el argumento (para localizar __ptmf)
        self.ptmfs = set()
        used_i, ptr, used_f, wf = set(), set(), set(), set()
        for idx, w in enumerate(ws):
            op = w >> 26
            rd, ra, rb = self.fields(w)
            gr, gw, fr, fw = self.reads_writes(w)
            if op == 18 and (w & 1):                       # los ayudantes _savegpr/_restgpr no tocan r3-r10
                li = w & 0x03FFFFFC
                t = (addr + 4 * idx + (li - 0x04000000 if li & 0x02000000 else li)) & 0xFFFFFFFF
                if self.names.get(t, "").startswith(CALLEE_SAVED_HELPERS): gw = set()
                if t == 0x80207628 and 12 in alias: self.ptmfs.add((alias[12], aoff.get(12, 0)))
            bases = []
            if op in self.MEM_D and ra: bases.append(ra)
            if op == 31 and ((w >> 1) & 0x3FF) in self.MEM_X:
                if ra: bases.append(ra)
                bases.append(rb)
            for r in bases:
                if r in alias: ptr.add(alias[r])
            for r in gr:
                if r in alias: used_i.add(alias[r])
            for r in fr:
                if 1 <= r <= 8 and r not in wf: used_f.add(r)
            wf |= fw
            new = None
            if op == 31 and ((w >> 1) & 0x3FF) == 444 and rd == rb and rd in alias: new = (ra, alias[rd], aoff.get(rd, 0))
            elif op == 14 and ra and ra in alias: new = (rd, alias[ra], aoff.get(ra, 0) + d_(w))
            for r in gw: alias.pop(r, None); aoff.pop(r, None)
            if new: alias[new[0]] = new[1]; aoff[new[0]] = new[2]
        gi = [c for c in (ghidra[1] if ghidra else []) if c != "f"]
        n_i = max([len(gi)] + [i + 1 for i in (used_i | ptr)] + [0])
        ints = ""
        for i in range(n_i):
            if i in ptr: ints += "p"
            elif i in used_i: ints += "i"
            elif i < len(gi): ints += {"p": "p", "i": "i", "q": "q"}.get(gi[i], "q")
            else: ints += "q"
        n_f = max(used_f, default=0)
        gf = sum(1 for c in (ghidra[1] if ghidra else []) if c == "f")
        return ints + "f" * max(n_f, gf)

    # ------------------------------------------------------------ elevacion de una funcion
    def lift(self, addr, size, name):
        sg = self.sigs.get(addr)
        self.cur_ret = sg[0] if sg else "i"
        code = self.rd(addr, size)
        ws = [struct.unpack(">I", code[i:i + 4])[0] for i in range(0, size, 4)]
        end = addr + size
        labels = set()
        for i, w in enumerate(ws):
            op = w >> 26
            pc = addr + 4 * i
            if op == 18 and not (w & 1):
                li = w & 0x03FFFFFC
                t = (pc + (li - 0x04000000 if li & 0x02000000 else li)) if not (w & 2) else (li - 0x04000000 if li & 0x02000000 else li)
                if addr <= t < end: labels.add(t)
            elif op == 16 and not (w & 1):
                t = pc + sext(w & 0xFFFC, 16) if not (w & 2) else sext(w & 0xFFFC, 16)
                if addr <= t < end: labels.add(t)
        self.cur_tables = {}
        self.used_q = set()
        self.uses_ps = self.native and any((w >> 26) == 4 or ((w >> 26) in (56, 57, 60, 61) and ((w >> 16) & 31) != 1) for w in ws)
        for i, w in enumerate(ws):
            if w >> 26 == 19 and ((w >> 1) & 0x3FF) == 528 and not (w & 1):
                tg = self.find_jumptable(ws, i, addr)
                if tg:
                    self.cur_tables[i] = tg
                    labels.update(t for t in tg if addr <= t < end)
        out = []
        used_g, used_f = set(), set()
        calls = []          # (indice de linea, direccion destino o None, indice de insn)
        frame = 0
        ret_kind = None

        def R(n): used_g.add(n); return f"r{n}"
        def F(n): used_f.add(n); return f"f{n}"
        def A(ra, d):
            if ra == 0: return hx(d)
            base = R(ra)
            return base if d == 0 else f"({base} + {hx(d)})"

        def CALL(target_expr, i, direct_addr):
            """Linea de llamada; el tipo de retorno se resuelve al final (marcador)."""
            ret = self.infer_call_ret(ws, i)
            args = ", ".join([R(n) for n in range(3, 11)] + [F(n) for n in range(1, 9)])
            for n in range(3, 11): used_g.add(n)
            return ret, args

        for i, w in enumerate(ws):
            pc = addr + 4 * i
            op = w >> 26
            rd, ra, rb = self.fields(w)
            d = sext(w & 0xFFFF, 16)
            lines = []
            if pc in labels: out.append(f"L_{pc:08X}:;")
            try:
                if w == 0x60000000: continue
                # ---- aritmetica inmediata
                if op == 14:
                    lines.append(f"{R(rd)} = {hx(d)};" if ra == 0 else f"{R(rd)} = {R(ra)} + {hx(d)};")
                elif op == 15:
                    lines.append(f"{R(rd)} = {hx(d << 16)};" if ra == 0 else f"{R(rd)} = {R(ra)} + {hx(d << 16)};")
                elif op in (12, 13):                         # addic addic.
                    lines.append(f"{{ u32 t = {R(ra)}; {R(rd)} = ADDC(t, {hx(d)}, &ca); }}")
                    if op == 13: lines.append(f"cr = CRSET(cr, 0, CR0V({R(rd)}));")
                elif op == 8:                                # subfic
                    lines.append(f"{R(rd)} = SUBFC({R(ra)}, {hx(d)}, &ca);")
                elif op == 7:
                    lines.append(f"{R(rd)} = {R(ra)} * {hx(d)};")
                elif op in (24, 25, 26, 27):
                    imm = (w & 0xFFFF) << (16 if op in (25, 27) else 0)
                    o = "|" if op in (24, 25) else "^"
                    lines.append(f"{R(ra)} = {R(rd)} {o} {hx(imm)};")
                elif op in (28, 29):
                    imm = (w & 0xFFFF) << (16 if op == 29 else 0)
                    lines.append(f"{R(ra)} = {R(rd)} & {hx(imm)};")
                    lines.append(f"cr = CRSET(cr, 0, CR0V({R(ra)}));")
                # ---- comparaciones
                elif op in (11, 10):
                    crf = (w >> 23) & 7
                    if op == 11: lines.append(f"cr = CRSET(cr, {crf}, CMPS((s32){R(ra)}, {d}));")
                    else:        lines.append(f"cr = CRSET(cr, {crf}, CMPU({R(ra)}, {hx(w & 0xFFFF)}));")
                # ---- cargas/almacenamientos D-form
                elif op in (32, 34, 40, 42):
                    f = {32: "LD32", 34: "LD8", 40: "LD16", 42: "LD16S"}[op]
                    lines.append(f"{R(rd)} = {f}({A(ra, d)});")
                elif op in (33, 35, 41, 43):
                    f = {33: "LD32", 35: "LD8", 41: "LD16", 43: "LD16S"}[op]
                    lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; {R(rd)} = {f}({R(ra)});")
                elif op in (36, 38, 44):
                    f = {36: "ST32", 38: "ST8", 44: "ST16"}[op]
                    lines.append(f"{f}({A(ra, d)}, {R(rd)});")
                elif op in (37, 39, 45):
                    f = {37: "ST32", 39: "ST8", 45: "ST16"}[op]
                    lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; {f}({R(ra)}, {R(rd)});")
                    if op == 37 and ra == 1 and frame == 0 and d < 0: frame = -d
                elif op == 46:
                    for k in range(rd, 32): lines.append(f"{R(k)} = LD32({A(ra, d + 4 * (k - rd))});")
                elif op == 47:
                    for k in range(rd, 32): lines.append(f"ST32({A(ra, d + 4 * (k - rd))}, {R(k)});")
                elif op == 48:
                    lines.append(f"{F(rd)} = (f64)LDF({A(ra, d)});")
                    if self.uses_ps: self.used_q.add(rd); lines.append(f"q{rd} = f{rd};")
                elif op == 50:  lines.append(f"{F(rd)} = LDD({A(ra, d)});")
                elif op == 49:  lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; {F(rd)} = (f64)LDF({R(ra)});")
                elif op == 51:  lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; {F(rd)} = LDD({R(ra)});")
                elif op == 52:  lines.append(f"STF({A(ra, d)}, (f32){F(rd)});")
                elif op == 54:  lines.append(f"STD({A(ra, d)}, {F(rd)});")
                elif op == 53:  lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; STF({R(ra)}, (f32){F(rd)});")
                elif op == 55:  lines.append(f"{R(ra)} = {R(ra)} + {hx(d)}; STD({R(ra)}, {F(rd)});")
                elif op in (56, 57, 60, 61) and self.native:   # psq_l / psq_lu / psq_st / psq_stu: semantica real con los GQR
                    if ra == 1 and op in (56, 60): pass                                # guardado/restauracion de f31/f30 en el prologo
                    else:
                        wb, gi = (w >> 15) & 1, (w >> 12) & 7
                        d12 = sext(w & 0xFFF, 12)
                        self.used_q.add(rd)
                        ea = A(ra, d12) if op in (56, 60) else None
                        if op in (57, 61): lines.append(f"{R(ra)} = {R(ra)} + {hx(d12)};"); ea = R(ra)
                        if op in (56, 57): lines.append(f"PSQ_L({ea}, {wb}, {gi}, &f{rd}, &q{rd}); " ); F(rd)
                        else: lines.append(f"PSQ_ST({ea}, {wb}, {gi}, f{rd}, q{rd});"); F(rd)
                elif op == 4 and self.native:
                    lines += self.opps(w, rd, ra, rb, R, F)
                elif op in (56, 60):                         # psq_l / psq_st: como en el emulador (nop con r1; lfd/stfd/lfs/stfs)
                    if ((w >> 12) & 7) != 0: raise Bail("psq con indice GQR")
                    wb = (w >> 15) & 1
                    d12 = w & 0xFFF; d16 = sext(d12, 12)
                    if ra == 1: pass
                    elif op == 56: lines.append(f"{F(rd)} = " + (f"(f64)LDF({A(ra, d16)});" if wb else f"LDD({A(ra, d16)});"))
                    else:          lines.append((f"STF({A(ra, d16)}, (f32){F(rd)});" if wb else f"STD({A(ra, d16)}, {F(rd)});"))
                # ---- rotaciones
                elif op == 21:
                    sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
                    lines.append(f"{R(ra)} = ROTL({R(rd)}, {sh}) & {hx(self.mask(mb, me))};")
                    if w & 1: lines.append(f"cr = CRSET(cr, 0, CR0V({R(ra)}));")
                elif op == 20:
                    sh, mb, me = (w >> 11) & 31, (w >> 6) & 31, (w >> 1) & 31
                    m = self.mask(mb, me)
                    lines.append(f"{R(ra)} = (ROTL({R(rd)}, {sh}) & {hx(m)}) | ({R(ra)} & {hx(~m)});")
                    if w & 1: lines.append(f"cr = CRSET(cr, 0, CR0V({R(ra)}));")
                elif op == 23:
                    mb, me = (w >> 6) & 31, (w >> 1) & 31
                    lines.append(f"{R(ra)} = ROTL({R(rd)}, {R(rb)}) & {hx(self.mask(mb, me))};")
                    if w & 1: lines.append(f"cr = CRSET(cr, 0, CR0V({R(ra)}));")
                # ---- ramas
                elif op == 18:
                    li = w & 0x03FFFFFC
                    li = li - 0x04000000 if li & 0x02000000 else li
                    t = (pc + li) & 0xFFFFFFFF if not (w & 2) else li & 0xFFFFFFFF
                    if w & 1:                                 # bl
                        lines += self.emit_call(t, i, ws, addr, end, R, F, used_g, used_f)
                    elif addr <= t < end:
                        lines.append((("RT_POLL(); " if (self.native and t <= pc) else "")) + f"goto L_{t:08X};")
                    else:                                     # salto de cola
                        lines += self.emit_tail(t, i, ws, R, F, used_g, used_f)
                elif op == 16:
                    if w & 1: raise Bail("bc con link")
                    bo, bi = rd, ra
                    t = (pc + sext(w & 0xFFFC, 16)) & 0xFFFFFFFF if not (w & 2) else sext(w & 0xFFFC, 16) & 0xFFFFFFFF
                    cond = self.bc_cond(bo, bi)
                    if addr <= t < end:
                        lines += self.bc_emit(bo, cond, "{ " + ("RT_POLL(); " if (self.native and t <= pc) else "") + f"goto L_{t:08X}; }}")
                    else:
                        tail = self.emit_tail(t, i, ws, R, F, used_g, used_f)
                        lines += self.bc_emit(bo, cond, "{ " + " ".join(tail) + " }")
                elif op == 19:
                    xo = (w >> 1) & 0x3FF
                    bo, bi = rd, ra
                    if xo == 16:                              # bclr
                        if w & 1: raise Bail("blrl")
                        lines += self.bc_emit(bo, self.bc_cond(bo, bi), "RETURN;")
                    elif xo == 528:                           # bcctr
                        if w & 1:                             # bctrl: llamada indirecta
                            lines += self.emit_icall(i, ws, R, F, used_g, used_f)
                        elif i in self.cur_tables:            # switch con tabla de saltos
                            tg = self.cur_tables[i]
                            lines.append("switch (ctr) {")
                            for t in sorted(set(tg)):
                                lines.append(f"    case {hx(t)}: goto L_{t:08X};")
                            lines.append("    default: break; }")
                        else:
                            tail = self.emit_itail(i, ws, R, F, used_g, used_f)
                            lines += self.bc_emit(bo, self.bc_cond(bo, bi), "{ " + " ".join(tail) + " }")
                    elif xo in (449, 193, 33, 129, 225, 257, 289, 417, 0, 150):
                        lines.append(self.cr_op(xo, rd, ra, rb))
                    else: raise Bail(f"op19 xo={xo}")
                elif op == 31:
                    lines += self.op31(w, rd, ra, rb, R, F, A)
                elif op in (63, 59):
                    lines += self.opfloat(w, op, rd, ra, rb, R, F)
                elif op == 17 and self.native:                      # sc: invalida la cache de instrucciones; no hace nada aqui
                    pass
                else:
                    raise Bail(f"op {op}")
            except Bail:
                raise
            out.extend(lines)
        return dict(lines=out, used_g=used_g, used_f=used_f, frame=frame, ws=ws, labels=labels, kinds=self.arg_kinds(ws, sg, addr), ptmfs=set(self.ptmfs), used_q=set(self.used_q))

    @staticmethod
    def mask(mb, me):
        a = 0xFFFFFFFF >> mb
        b = (0xFFFFFFFF << (31 - me)) & 0xFFFFFFFF
        return (a & b) if mb <= me else (a | b)

    # ------------------------------------------------------------ condiciones de rama
    @staticmethod
    def bc_cond(bo, bi):
        parts = []
        if not (bo & 0x04):                                   # decrementa CTR
            parts.append("(--ctr != 0)" if not (bo & 0x02) else "(--ctr == 0)")
        if not (bo & 0x10):
            parts.append(f"(CRBIT(cr, {bi}) == {1 if bo & 0x08 else 0})")
        return " && ".join(parts) if parts else "1"

    @staticmethod
    def bc_emit(bo, cond, stmt):
        return [stmt] if cond == "1" else [f"if ({cond}) {stmt}"]

    @staticmethod
    def cr_op(xo, bt, ba, bb):
        a, b = f"CRBIT(cr, {ba})", f"CRBIT(cr, {bb})"
        if xo == 0: return f"cr = CRSET(cr, {bt >> 2}, (cr >> {28 - 4 * (ba >> 2)}) & 0xF);"       # mcrf
        if xo == 150: return ""                                                                 # isync
        e = {449: f"({a} | {b})", 193: f"({a} ^ {b})", 33: f"(!({a} | {b}))", 129: f"({a} & !{b})",
             225: f"(!({a} & {b}))", 257: f"({a} & {b})", 289: f"(!({a} ^ {b}))", 417: f"({a} | !{b})"}[xo]
        return f"cr = CRBITSET(cr, {bt}, {e});"

    # ------------------------------------------------------------ llamadas
    def call_args(self, R, F):
        return ", ".join([R(n) for n in range(3, 11)] + [F(n) for n in range(1, 9)])

    def emit_call(self, t, i, ws, addr, end, R, F, used_g, used_f):
        if addr <= t < end: raise Bail("bl dentro de la propia funcion")
        nm = self.names.get(t, "")
        if nm.startswith(CALLEE_SAVED_HELPERS): return []
        if t == 0x80207628:                                   # __ptmf_scall
            R(12); R(3)
            ret = self.infer_call_ret(ws, i)
            return [f"{{ u32 self_ = {R(3)}; u32 tgt_ = PTMF_TARGET(&self_, {R(12)}); {R(3)} = self_; " + self.set_ret(ret, f"((RET_{ret.upper()}(*)(U8F8))tgt_)({self.call_args(R, F)})") + " }"]
        ret = self.infer_call_ret(ws, i)
        self.callee_sites.setdefault(t, set()).add(ret)
        return [self.set_ret(ret, f"CALLEE_{t:08X}_{ret}({self.call_args(R, F)})")]

    def set_ret(self, ret, expr):
        if ret == "l": return f"{{ u64 t_ = {expr}; r3 = (u32)(t_ >> 32); r4 = (u32)t_; }}"
        if ret == "f": return f"f1 = {expr};"
        if ret == "i": return f"r3 = {expr};"
        return f"{expr};"

    def emit_icall(self, i, ws, R, F, used_g, used_f):
        ret = self.infer_call_ret(ws, i)
        used_f.add(1)
        return [self.set_ret(ret, f"((RET_{ret.upper()}(*)(U8F8))ctr)({self.call_args(R, F)})")]

    def emit_itail(self, i, ws, R, F, used_g, used_f):
        return [f"TAILRET(((RET_I(*)(U8F8))ctr)({self.call_args(R, F)}));"]

    def emit_tail(self, t, i, ws, R, F, used_g, used_f):
        self.callee_sites.setdefault(t, set()).add("t")
        return [f"TAILRET(CALLEE_{t:08X}_{self.cur_ret}({self.call_args(R, F)}));"]

    # ------------------------------------------------------------ opcode 31
    def op31(self, w, rd, ra, rb, R, F, A):
        xo = (w >> 1) & 0x3FF
        rc = w & 1
        oe = (w >> 10) & 1
        L = []
        def rec(reg):
            if rc: L.append(f"cr = CRSET(cr, 0, CR0V({R(reg)}));")
        def ea(): return f"({R(ra)} + {R(rb)})" if ra else R(rb)
        if oe and xo in (266, 40, 8, 10, 136, 138, 104, 235, 491, 459, 202, 200): raise Bail("OE")
        if xo == 0 or xo == 32:
            crf = (w >> 23) & 7
            L.append(f"cr = CRSET(cr, {crf}, " + (f"CMPS((s32){R(ra)}, (s32){R(rb)})" if xo == 0 else f"CMPU({R(ra)}, {R(rb)})") + ");")
        elif xo == 444:  L.append(f"{R(ra)} = {R(rd)} | {R(rb)};" if rd != rb else f"{R(ra)} = {R(rd)};"); rec(ra)
        elif xo == 28:   L.append(f"{R(ra)} = {R(rd)} & {R(rb)};"); rec(ra)
        elif xo == 60:   L.append(f"{R(ra)} = {R(rd)} & ~{R(rb)};"); rec(ra)
        elif xo == 316:  L.append(f"{R(ra)} = {R(rd)} ^ {R(rb)};"); rec(ra)
        elif xo == 124:  L.append(f"{R(ra)} = ~({R(rd)} | {R(rb)});"); rec(ra)
        elif xo == 412:  L.append(f"{R(ra)} = {R(rd)} | ~{R(rb)};"); rec(ra)
        elif xo == 266:  L.append(f"{R(rd)} = {R(ra)} + {R(rb)};"); rec(rd)
        elif xo == 40:   L.append(f"{R(rd)} = {R(rb)} - {R(ra)};"); rec(rd)
        elif xo == 8:    L.append(f"{R(rd)} = SUBFC({R(ra)}, {R(rb)}, &ca);"); rec(rd)
        elif xo == 10:   L.append(f"{R(rd)} = ADDC({R(ra)}, {R(rb)}, &ca);"); rec(rd)
        elif xo == 138:  L.append(f"{R(rd)} = ADDE({R(ra)}, {R(rb)}, &ca);"); rec(rd)
        elif xo == 136:  L.append(f"{R(rd)} = SUBFE({R(ra)}, {R(rb)}, &ca);"); rec(rd)
        elif xo == 202:  L.append(f"{R(rd)} = ADDE({R(ra)}, 0, &ca);"); rec(rd)
        elif xo == 200:  L.append(f"{R(rd)} = SUBFE({R(ra)}, 0, &ca);"); rec(rd)
        elif xo == 104:  L.append(f"{R(rd)} = 0 - {R(ra)};"); rec(rd)
        elif xo == 235:  L.append(f"{R(rd)} = {R(ra)} * {R(rb)};"); rec(rd)
        elif xo == 75:   L.append(f"{R(rd)} = MULHW({R(ra)}, {R(rb)});"); rec(rd)
        elif xo == 11:   L.append(f"{R(rd)} = MULHWU({R(ra)}, {R(rb)});"); rec(rd)
        elif xo == 491:  L.append(f"{R(rd)} = DIVW({R(ra)}, {R(rb)});"); rec(rd)
        elif xo == 459:  L.append(f"{R(rd)} = DIVWU({R(ra)}, {R(rb)});"); rec(rd)
        elif xo == 24:   L.append(f"{R(ra)} = SLW({R(rd)}, {R(rb)});"); rec(ra)
        elif xo == 536:  L.append(f"{R(ra)} = SRW({R(rd)}, {R(rb)});"); rec(ra)
        elif xo == 792:  L.append(f"{R(ra)} = SRAW({R(rd)}, {R(rb)}, &ca);"); rec(ra)
        elif xo == 824:  L.append(f"{R(ra)} = SRAWI({R(rd)}, {rb}, &ca);"); rec(ra)
        elif xo == 26:   L.append(f"{R(ra)} = CNTLZW({R(rd)});"); rec(ra)
        elif xo == 954:  L.append(f"{R(ra)} = (u32)(s32)(s8){R(rd)};"); rec(ra)
        elif xo == 922:  L.append(f"{R(ra)} = (u32)(s32)(s16){R(rd)};"); rec(ra)
        elif xo == 23:   L.append(f"{R(rd)} = LD32({ea()});")
        elif xo == 87:   L.append(f"{R(rd)} = LD8({ea()});")
        elif xo == 279:  L.append(f"{R(rd)} = LD16({ea()});")
        elif xo == 343:  L.append(f"{R(rd)} = LD16S({ea()});")
        elif xo == 151:  L.append(f"ST32({ea()}, {R(rd)});")
        elif xo == 215:  L.append(f"ST8({ea()}, {R(rd)});")
        elif xo == 407:  L.append(f"ST16({ea()}, {R(rd)});")
        elif xo == 183:  L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; ST32({R(ra)}, {R(rd)});")
        elif xo == 55:   L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; {R(rd)} = LD32({R(ra)});")
        elif xo == 535:  L.append(f"{F(rd)} = (f64)LDF({ea()});")
        elif xo == 599:  L.append(f"{F(rd)} = LDD({ea()});")
        elif xo == 567:  L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; {F(rd)} = (f64)LDF({R(ra)});")        # lfsux
        elif xo == 631:  L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; {F(rd)} = LDD({R(ra)});")             # lfdux
        elif xo == 695:  L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; STF({R(ra)}, (f32){F(rd)});")        # stfsux
        elif xo == 759:  L.append(f"{R(ra)} = {R(ra)} + {R(rb)}; STD({R(ra)}, {F(rd)});")             # stfdux
        elif xo == 663:  L.append(f"STF({ea()}, (f32){F(rd)});")
        elif xo == 727:  L.append(f"STD({ea()}, {F(rd)});")
        elif xo == 339:                                      # mfspr
            spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
            if spr == 8:   L.append(f"{R(rd)} = lr;")
            elif spr == 9: L.append(f"{R(rd)} = ctr;")
            elif spr == 1: L.append(f"{R(rd)} = 0;")          # XER (solo se usa CA aqui)
            elif self.native: L.append(f"{R(rd)} = RT_MFSPR({spr});")
            else: raise Bail(f"mfspr {spr}")
        elif xo == 467:                                      # mtspr
            spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
            if spr == 8:   L.append(f"lr = {R(rd)};")
            elif spr == 9: L.append(f"ctr = {R(rd)};")
            elif spr == 1: pass
            elif self.native: L.append(f"RT_MTSPR({spr}, {R(rd)});")
            else: raise Bail(f"mtspr {spr}")
        elif xo == 371 and self.native:                      # mftb
            tbr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
            L.append(f"{R(rd)} = RT_MFTB({tbr});")
        elif xo == 83 and self.native:   L.append(f"{R(rd)} = RT_MFMSR();")                  # mfmsr
        elif xo == 146 and self.native:  L.append(f"RT_MTMSR({R(rd)});")                     # mtmsr
        elif xo in (210, 242, 306, 566, 598, 854, 982, 470, 1014 + 100) and self.native: pass   # mtsr mtsrin tlbie tlbsync ... sin efecto
        elif xo in (595, 659) and self.native: L.append(f"{R(rd)} = 0;")                      # mfsr mfsrin
        elif xo == 19:   L.append(f"{R(rd)} = cr;")                                     # mfcr
        elif xo == 144:                                                                 # mtcrf
            fxm = (w >> 12) & 0xFF
            m = 0
            for k in range(8):
                if fxm & (0x80 >> k): m |= 0xF0000000 >> (4 * k)
            L.append(f"cr = (cr & {hx(~m)}) | ({R(rd)} & {hx(m)});")
        elif xo in (86, 54, 598, 246, 470, 982, 854, 566): pass  # dcbf dcbst sync dcbtst dcbi icbi eieio tlbsync: sin efecto
        elif xo == 1014: L.append(f"DCBZ({ea()});")
        elif xo == 278 or xo == 342: pass                    # dcbt
        else: raise Bail(f"op31 xo={xo}")
        return L

    # ------------------------------------------------------------ pares simples (Broadway)
    def opps(self, w, rd, ra, rb, R, F):
        rc = (w >> 6) & 31
        x5, x10 = (w >> 1) & 0x1F, (w >> 1) & 0x3FF
        for r in (rd, ra, rb, rc): self.used_q.add(r)
        def P(n): return (F(n), f"q{n}")
        D, A_, B_, C_ = P(rd), P(ra), P(rb), P(rc)
        def lane(expr):                                 # expr con {a},{b},{c} -> (ps0, ps1)
            out = []
            for k in (0, 1):
                out.append(expr.format(a=A_[k], b=B_[k], c=C_[k]))
            return out
        def set2(e0, e1): return [f"{{ f64 n0_ = {e0}; f64 n1_ = {e1}; {D[0]} = n0_; {D[1]} = n1_; }}"]
        if x5 in (21, 20, 18, 25):
            sym = {21: "+", 20: "-", 18: "/", 25: "*"}[x5]
            opnd = (lambda k: (A_[k], C_[k] if x5 == 25 else B_[k]))
            return set2(*[f"FRSP({opnd(k)[0]} {sym} {opnd(k)[1]})" for k in (0, 1)])
        if x5 == 12: return set2(f"FRSP({A_[0]} * {C_[0]})", f"FRSP({A_[1]} * {C_[0]})")              # ps_muls0
        if x5 == 13: return set2(f"FRSP({A_[0]} * {C_[1]})", f"FRSP({A_[1]} * {C_[1]})")              # ps_muls1
        if x5 == 14: return set2(f"FRSP(__builtin_fma({A_[0]}, {C_[0]}, {B_[0]}))", f"FRSP(__builtin_fma({A_[1]}, {C_[0]}, {B_[1]}))")   # ps_madds0
        if x5 == 15: return set2(f"FRSP(__builtin_fma({A_[0]}, {C_[1]}, {B_[0]}))", f"FRSP(__builtin_fma({A_[1]}, {C_[1]}, {B_[1]}))")   # ps_madds1
        if x5 == 10: return set2(f"FRSP({A_[0]} + {B_[1]})", f"{C_[1]}")                              # ps_sum0
        if x5 == 11: return set2(f"{C_[0]}", f"FRSP({A_[0]} + {B_[1]})")                              # ps_sum1
        if x5 == 29: return set2(*[f"FRSP(__builtin_fma({A_[k]}, {C_[k]}, {B_[k]}))" for k in (0, 1)])      # ps_madd
        if x5 == 28: return set2(*[f"FRSP(__builtin_fma({A_[k]}, {C_[k]}, -{B_[k]}))" for k in (0, 1)])     # ps_msub
        if x5 == 31: return set2(*[f"FRSP(-__builtin_fma({A_[k]}, {C_[k]}, {B_[k]}))" for k in (0, 1)])     # ps_nmadd
        if x5 == 30: return set2(*[f"FRSP(-__builtin_fma({A_[k]}, {C_[k]}, -{B_[k]}))" for k in (0, 1)])    # ps_nmsub
        if x5 == 23: return set2(*[f"FSEL({A_[k]}, {C_[k]}, {B_[k]})" for k in (0, 1)])                      # ps_sel
        if x5 == 24: return set2(*[f"FRSP(1.0 / {B_[k]})" for k in (0, 1)])                                    # ps_res (aprox.)
        if x5 == 26: return set2(*[f"FRSP(1.0 / __builtin_sqrt({B_[k]}))" for k in (0, 1)])                    # ps_rsqrte (aprox.)
        if x10 == 528: return set2(A_[0], B_[0])                 # ps_merge00
        if x10 == 560: return set2(A_[0], B_[1])                 # ps_merge01
        if x10 == 592: return set2(A_[1], B_[0])                 # ps_merge10
        if x10 == 624: return set2(A_[1], B_[1])                 # ps_merge11
        if x10 == 72:  return set2(B_[0], B_[1])                 # ps_mr
        if x10 == 40:  return set2(f"-{B_[0]}", f"-{B_[1]}")     # ps_neg
        if x10 == 264: return set2(f"F64FROMBITS(F64BITS({B_[0]}) & 0x7FFFFFFFFFFFFFFFull)", f"F64FROMBITS(F64BITS({B_[1]}) & 0x7FFFFFFFFFFFFFFFull)")   # ps_abs
        if x10 == 136: return set2(f"F64FROMBITS(F64BITS({B_[0]}) | 0x8000000000000000ull)", f"F64FROMBITS(F64BITS({B_[1]}) | 0x8000000000000000ull)")   # ps_nabs
        if x10 in (0, 32): return [f"cr = CRSET(cr, {(w >> 23) & 7}, FCMP({A_[0]}, {B_[0]}));"]               # ps_cmpu0 / ps_cmpo0
        if x10 in (64, 96): return [f"cr = CRSET(cr, {(w >> 23) & 7}, FCMP({A_[1]}, {B_[1]}));"]              # ps_cmpu1 / ps_cmpo1
        if x10 in (6, 38, 7, 39):                                                                              # psq_lx / psq_lux / psq_stx / psq_stux
            wb, gi = (w >> 10) & 1, (w >> 7) & 7
            ea = f"({R(ra)} + {R(rb)})" if ra else R(rb)
            if x10 in (38, 39): return [f"{R(ra)} = {R(ra)} + {R(rb)};", (f"PSQ_L({R(ra)}, {wb}, {gi}, &{D[0]}, &{D[1]});" if x10 == 38 else f"PSQ_ST({R(ra)}, {wb}, {gi}, {D[0]}, {D[1]});")]
            return [f"PSQ_L({ea}, {wb}, {gi}, &{D[0]}, &{D[1]});" if x10 == 6 else f"PSQ_ST({ea}, {wb}, {gi}, {D[0]}, {D[1]});"]
        if x10 == 1014: return [f"DCBZ(({R(ra)} + {R(rb)}));" if ra else f"DCBZ({R(rb)});"]
        raise Bail(f"ps op xo={x10}")

    # ------------------------------------------------------------ coma flotante
    def opfloat(self, w, op, rd, ra, rb, R, F):
        rc = (w >> 6) & 31
        xo = (w >> 1) & 0x3FF
        x5 = (w >> 1) & 0x1F
        if w & 1: raise Bail("fp con Rc")
        single = op == 59
        def rnd(e): return f"FRSP({e})" if single else e
        if op == 63 and xo == 0:   return [f"cr = CRSET(cr, {(w >> 23) & 7}, FCMP({F(ra)}, {F(rb)}));"]
        if op == 63 and xo == 32:  return [f"cr = CRSET(cr, {(w >> 23) & 7}, FCMP({F(ra)}, {F(rb)}));"]
        if op == 63 and xo == 72:  return [f"{F(rd)} = {F(rb)};"]
        if op == 63 and xo == 40:  return [f"{F(rd)} = -{F(rb)};"]
        if op == 63 and xo == 264: return [f"{F(rd)} = F64FROMBITS(F64BITS({F(rb)}) & 0x7FFFFFFFFFFFFFFFull);"]
        if op == 63 and xo == 136: return [f"{F(rd)} = F64FROMBITS(F64BITS({F(rb)}) | 0x8000000000000000ull);"]
        if op == 63 and xo == 12:  return [f"{F(rd)} = FRSP({F(rb)});"]
        if op == 63 and xo == 15:  return [f"{F(rd)} = FCTIWZ({F(rb)});"]
        if op == 63 and xo == 14:  return [f"{F(rd)} = FCTIWZ({F(rb)});"]                              # fctiw (redondeo por defecto ~ truncar aqui)
        if op == 63 and x5 == 26:  return [f"{F(rd)} = 1.0 / __builtin_sqrt({F(rb)});"] if self.native else None   # frsqrte (aproximacion exacta)
        if op == 59 and x5 == 24:  return [f"{F(rd)} = FRSP(1.0 / {F(rb)});"] if self.native else None            # fres
        if op == 63 and x5 == 22:  return [f"{F(rd)} = __builtin_sqrt({F(rb)});"] if self.native else None         # fsqrt
        if op == 59 and x5 == 22:  return [f"{F(rd)} = FRSP(__builtin_sqrt({F(rb)}));"] if self.native else None   # fsqrts
        if x5 == 21:  return [f"{F(rd)} = {rnd(f'{F(ra)} + {F(rb)}')};"]
        if x5 == 20:  return [f"{F(rd)} = {rnd(f'{F(ra)} - {F(rb)}')};"]
        if x5 == 18:  return [f"{F(rd)} = {rnd(f'{F(ra)} / {F(rb)}')};"]
        if x5 == 25:  return [f"{F(rd)} = {rnd(f'{F(ra)} * {F(rc)}')};"]
        if x5 == 29:  return [f"{F(rd)} = {rnd(f'__builtin_fma({F(ra)}, {F(rc)}, {F(rb)})')};"]
        if x5 == 28:  return [f"{F(rd)} = {rnd(f'__builtin_fma({F(ra)}, {F(rc)}, -{F(rb)})')};"]
        if x5 == 31:  return [f"{F(rd)} = {rnd(f'-__builtin_fma({F(ra)}, {F(rc)}, {F(rb)})')};"]
        if x5 == 30:  return [f"{F(rd)} = {rnd(f'-__builtin_fma({F(ra)}, {F(rc)}, -{F(rb)})')};"]
        if op == 63 and x5 == 23: return [f"{F(rd)} = FSEL({F(ra)}, {F(rc)}, {F(rb)});"]
        raise Bail(f"fp op{op} xo={xo}")

# ---------------------------------------------------------------- generacion del fichero
def decls_and_text(info, name, addr, size, lifter, sig, ret_kind):
    """Texto C++ de una funcion elevada."""
    used_g, used_f = info["used_g"], info["used_f"]
    body = ["    " + l for l in info["lines"] if l]
    frame = info["frame"]
    fsz = frame + 32
    head = [f'extern "C" {ret_c(ret_kind)} dc_{name}(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, '
            f'f64 fa0, f64 fa1, f64 fa2, f64 fa3, f64 fa4, f64 fa5, f64 fa6, f64 fa7) {{']
    decl = []
    for n in sorted(used_g | {1}):
        if 3 <= n <= 10: decl.append(f"    u32 r{n} = a{n - 3};")
        elif n == 1: decl.append(f"    u8 frame_[{fsz}] __attribute__((aligned(16)));\n    u32 r1 = (u32)(frame_ + {frame + 16});")
        elif n == 13: decl.append(f"    u32 r13 = {SDA:#x};")
        elif n == 2:  decl.append(f"    u32 r2 = {SDA2:#x};")
        else: decl.append(f"    u32 r{n} = 0xC0DE{n:04X};")
    for n in range(3, 11):
        if n not in used_g: decl.append(f"    u32 r{n} = a{n - 3};")
    for n in sorted(used_f | {1}):
        decl.append(f"    f64 f{n} = fa{n - 1};" if 1 <= n <= 8 else f"    f64 f{n} = F64FROMBITS(0xFEED000000000000ull | {n});")
    for n in range(2, 9):
        if n not in used_f: decl.append(f"    f64 f{n} = fa{n - 1};")
    decl.append("    u32 cr = 0, ctr = 0, lr = 0xDEAD0000, ca = 0;")
    return "\n".join(head + decl + body) + "\n}\n"

def ret_c(kind): return {"i": "u32", "f": "f64", "v": "void", "l": "u64"}[kind]

def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--names", default="")
    ap.add_argument("--min", type=int, default=0)
    ap.add_argument("--max", type=int, default=0x400)
    ap.add_argument("--out", default=f"{ROOT}/src/auto/lifted.cpp")
    ap.add_argument("--cases", default=f"{ROOT}/tests/difftest/lift_cases.py")
    ap.add_argument("--shard", type=int, default=0, help="funciones por fichero (0 = un solo fichero)")
    ap.add_argument("--skip-trivial", action="store_true", help="omite las ya cubiertas por trivial.py")
    args = ap.parse_args()
    rd = load_dol()
    funcs = functions()
    names = {a: n for a, s, n in funcs}
    for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):           # etiquetas sin tamano (_savegpr_N...)
        m = re.match(r"^(\S+) = \.text:0x([0-9A-F]+); // type:label", l)
        if m: names.setdefault(int(m.group(2), 16), m.group(1))
    sigs = ghidra_sigs()
    lf = Lifter(rd, names, sigs)
    want = set(x for x in args.names.split(",") if x)
    trivial = set()
    if args.skip_trivial:
        for l in open(f"{ROOT}/tests/difftest/auto_cases.py"):
            m = re.match(r"\s+\('([^']+)'", l)
            if m: trivial.add(m.group(1))
    done, failed = [], {}
    for a, s, n in funcs:
        if not GAME_LO <= a < GAME_HI or s > args.max or s < args.min: continue
        if want and n not in want: continue
        if n in trivial: continue
        try:
            info = lf.lift(a, s, n)
            done.append((a, s, n, info))
        except Bail as e:
            failed[n] = str(e)
        except Exception as e:                      # error del elevador: se informa
            failed[n] = f"EXC {e!r}"
    # tipo de retorno de cada destino: firma de Ghidra o union de usos
    callee_ret = {}
    for t, kinds in lf.callee_sites.items():
        kinds = kinds - {"t"}
        if "f" in kinds: callee_ret[t] = "f"
        elif "l" in kinds: callee_ret[t] = "l"
        elif "i" in kinds: callee_ret[t] = "i"
        elif kinds: callee_ret[t] = "v"
        else: callee_ret[t] = "i"
    # tipo de retorno de cada funcion
    texts, cases = [], []
    used_targets = set()
    for a, s, n, info in done:
        sg = sigs.get(a)
        if sg: ret = sg[0]
        else:
            ret = "i"
        text = decls_and_text(info, n, a, s, lf, sg, ret)
        pass
        # RETURN / TAILRET segun el tipo de la funcion
        text = text.replace("RETURN;", {"i": "return r3;", "f": "return f1;", "v": "return;", "l": "return ((u64)r3 << 32) | r4;"}[ret])
        def tail(m):
            inner = m.group(1)
            return f"{{ (void)({inner}); return; }}" if ret == "v" else f"return {inner};"
        text = re.sub(r"TAILRET\((.*)\);", tail, text)
        texts.append(text)
        cases.append((n, a, ret, info['kinds'], sorted(info['ptmfs'])))
    # un fichero por tanda de funciones: cada uno con sus propias declaraciones de destinos
    outdir = args.out if args.out.endswith("/") else None
    shards = []
    if outdir is None and args.shard == 0:
        shards = [(args.out, list(range(len(texts))))]
    else:
        base = outdir or os.path.splitext(args.out)[0] + "_"
        os.makedirs(os.path.dirname(base + "x") or ".", exist_ok=True)
        n = max(args.shard, 1)
        for k in range(0, len(texts), n):
            shards.append((f"{base}{k // n:03d}.cpp", list(range(k, min(k + n, len(texts))))))
    for fn, idxs in shards:
        tg = set()
        for i in idxs: tg |= set(re.findall(r"CALLEE_([0-9A-F]{8})_([ifvl])", texts[i]))
        decl_lines = []
        for t, kind in sorted(tg):
            ti = int(t, 16)
            sym = names.get(ti)
            if sym is None:
                decl_lines.append(f'asm(".globl fn_{ti:08X}\\n.set fn_{ti:08X}, {ti:#x}");')
                sym = f"fn_{ti:08X}"
            decl_lines.append(f'extern "C" {ret_c(kind)} CALLEE_{t}_{kind}(u32, u32, u32, u32, u32, u32, u32, u32, f64, f64, f64, f64, f64, f64, f64, f64) __asm__("{sym}");')
        with open(fn, "w") as f:
            f.write("// GENERADO por decomp/tools/lift.py: funciones de game.dol elevadas de PowerPC a C++ (no editar a mano).\n")
            f.write('#include "ppc_lift.h"\n\n')
            f.write("#define RET_I u32\n#define RET_L u64\n#define RET_F f64\n#define RET_V void\n#define U8F8 u32, u32, u32, u32, u32, u32, u32, u32, f64, f64, f64, f64, f64, f64, f64, f64\n\n")
            f.write("\n".join(decl_lines) + "\n\n")
            f.write("\n".join(texts[i] for i in idxs))
    with open(args.cases, "w") as f:
        f.write("# GENERADO por decomp/tools/lift.py\nLIFT = [\n")
        for n, a, ret, ps, pt in cases:
            f.write(f"    ({n!r}, {a:#x}, {ret!r}, {ps!r}, {pt!r}),\n")
        f.write("]\n")
        f.write("# tipos de retorno que usan los llamadores de cada destino (los senuelos solo escriben lo que se lee)\n")
        f.write("CALLEE_KINDS = {\n")
        for t, ks in sorted(lf.callee_sites.items()):
            f.write(f"    {t:#x}: {''.join(sorted(k for k in ks if k != 't'))!r},\n")
        f.write("}\n")
    print(f"{len(done)} funciones elevadas; {len(failed)} no elevables")
    from collections import Counter
    print(Counter(v.split(' ')[0] + ' ' + (v.split(' ')[1] if ' ' in v else '') for v in failed.values()).most_common(15))

if __name__ == "__main__":
    main()
