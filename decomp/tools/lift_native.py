#!/usr/bin/env python3
"""Genera el codigo del runtime nativo: todas las funciones de game.dol elevadas a C++ para x86-64.

Reutiliza el elevador (lift.py) con otro convenio: cada funcion es `RetRegs N_<direccion>(r3-r10,
f1-f8)` y devuelve (r3, r4, f1); las llamadas van a `N_<destino>`; las indirectas a
`rt_call_indirect`; el marco de pila vive en la memoria del invitado (g_sp). Las funciones que el
runtime sustituye a mano (`HLE_FN(nombre)` en runtime/src) no se elevan; las que el elevador no
puede traducir quedan como "no implementadas" y abortan con un mensaje al llamarlas.

Salida en runtime/gen/ (gitignored): decls.h, glue.cpp, functable.cpp, lifted_NNN.cpp
Uso: lift_native.py [--shard 120]
"""
import os, re, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lift
from lift import Lifter, Bail, load_dol, ghidra_sigs, ROOT, SDA, SDA2

RT = f"{ROOT}/runtime"

class NativeLifter(Lifter):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.native = True
        self.targets = set()

    def emit_call(self, t, i, ws, addr, end, R, F, used_g, used_f):
        if addr < t < end: raise Bail("bl dentro de la propia funcion")     # (bl a su propio inicio = recursion: valido)
        nm = self.names.get(t, "")
        if nm.startswith(lift.CALLEE_SAVED_HELPERS): return []
        if t == 0x80207628:                                   # __ptmf_scall
            R(12); R(3)
            return ["{ u32 self_ = r3; u32 tgt_ = PTMF_TARGET(&self_, r12); r3 = self_; CALLI(tgt_); }"]
        if t == 0x801893B0:                                   # OSSaveContext: setjmp en el marco del llamador
            return ["RT_SAVECTX(r3, r3);"]
        self.targets.add(t)
        return [f"CALLD(N_{t:08X});"]

    def emit_icall(self, i, ws, R, F, used_g, used_f):
        return ["CALLI(ctr);"]

    def emit_itail(self, i, ws, R, F, used_g, used_f):
        return ["TAILI(ctr);"]

    def emit_tail(self, t, i, ws, R, F, used_g, used_f):
        self.targets.add(t)
        return [f"TAILD(N_{t:08X});"]

def trace_names():
    p = f"{RT}/trace.txt"
    return set(l.split("#")[0].strip() for l in open(p)) - {""} if os.path.exists(p) else set()

def native_text(info, addr, name):
    used_g, used_f = info["used_g"], info["used_f"]
    body = ["    " + l for l in info["lines"] if l]
    head = [f"// {name}", f"RetRegs N_{addr:08X}(NATIVE_PARAMS) {{", f"    RtFrame rtf_({addr:#x});", "    u32 r1 = g_sp;"]
    if name in TRACE: head.append(f"    rt_trace({addr:#x}, a0, a1, a2, a3);")
    decl = []
    for n in sorted(used_g - {1}):
        if 3 <= n <= 10: continue
        elif n == 13: decl.append(f"    u32 r13 = {SDA:#x};")
        elif n == 2:  decl.append(f"    u32 r2 = {SDA2:#x};")
        else: decl.append(f"    u32 r{n} = 0xC0DE{n:04X};")
    for n in range(3, 11): decl.append(f"    u32 r{n} = a{n - 3};")
    for n in range(1, 9): decl.append(f"    f64 f{n} = fa{n - 1};")
    for n in sorted(used_f):
        if n == 0 or n > 8: decl.append(f"    f64 f{n} = F64FROMBITS(0xFEED000000000000ull | {n});")
    for n in sorted(info.get("used_q", ())): decl.append(f"    f64 q{n} = 0.0;")
    decl.append("    u32 cr = 0, ctr = 0, lr = 0xDEAD0000, ca = 0;")
    text = "\n".join(head + decl + body + ["    return MKRET(r3, r4, f1);", "}", ""])
    text = text.replace("RETURN;", "return MKRET(r3, r4, f1);")
    if name in TRACE:                                     # las funciones de trace.txt tambien imprimen su valor de retorno (r3)
        text = text.replace("return MKRET(r3, r4, f1);", f"{{ rt_trace_ret({addr:#x}, r3); return MKRET(r3, r4, f1); }}")
    return text

def all_functions():
    out = []
    for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
        m = re.match(r"^(\S+) = \.(?:text|init):0x([0-9A-F]+); // type:function size:0x([0-9A-F]+)", l)
        if m: out.append((int(m.group(2), 16), int(m.group(3), 16), m.group(1)))
    return out

def hle_names():
    names = set()
    for f in glob.glob(f"{RT}/src/*.cpp"):
        names |= set(re.findall(r"HLE_FN\((\w+)\)", open(f).read()))
    return names

TRACE = set()

def main():
    global TRACE
    TRACE = trace_names()
    shard = 120
    if "--shard" in sys.argv: shard = int(sys.argv[sys.argv.index("--shard") + 1])
    rd = load_dol()
    funcs = all_functions()
    names = {a: n for a, s, n in funcs}
    for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
        m = re.match(r"^(\S+) = \.(?:text|init):0x([0-9A-F]+); // type:label", l)
        if m: names.setdefault(int(m.group(2), 16), m.group(1))
    lf = NativeLifter(rd, names, ghidra_sigs())
    hle = hle_names()
    hle_addr = {}
    lifted, texts, failed = [], [], {}
    for a, s, n in funcs:
        if n in hle:
            hle_addr[a] = n
            continue
        try:
            info = lf.lift(a, s, n)
            texts.append(native_text(info, a, n)); lifted.append(a)
        except Bail as e:
            failed[a] = str(e)
        except Exception as e:
            failed[a] = f"EXC {e!r}"
    all_addrs = sorted(set(a for a, s, n in funcs) | lf.targets)
    os.makedirs(f"{RT}/gen", exist_ok=True)
    def write(path, text):
        if os.path.exists(path) and open(path).read() == text: return
        open(path, "w").write(text)
    old = set(glob.glob(f"{RT}/gen/lifted_*.cpp"))
    write(f"{RT}/gen/decls.h", "// GENERADO por decomp/tools/lift_native.py\n#pragma once\n#include \"native_lift.h\"\n" +
          "".join(f"RetRegs N_{a:08X}(NATIVE_PARAMS);\n" for a in all_addrs))
    blocks = {}
    for a, t in zip(lifted, texts): blocks.setdefault((a - 0x80004000) // 0x4000, []).append(t)      # un fichero por bloque de 16 KB
    written = set()
    for b, ts in sorted(blocks.items()):
        path = f"{RT}/gen/lifted_{b:03d}.cpp"
        written.add(path)
        write(path, "// GENERADO por decomp/tools/lift_native.py: funciones de game.dol elevadas (no editar).\n#include \"decls.h\"\n\n" + "\n".join(ts))
    for path in old - written: os.remove(path)
    lifted_set = set(lifted)
    import io
    f = io.StringIO()
    if True:
        f.write("// GENERADO por decomp/tools/lift_native.py: funciones sustituidas a mano o sin elevar.\n")
        f.write('#include "decls.h"\n#include "hle.h"\n\n')
        for n_ in sorted(set(hle_addr.values())): f.write(f"RetRegs HLE_{n_}(NATIVE_PARAMS);\n")
        f.write("\n")
        for a in all_addrs:
            if a in lifted_set: continue
            if a in hle_addr:
                f.write(f"RetRegs N_{a:08X}(NATIVE_PARAMS) {{ return HLE_{hle_addr[a]}(a0, a1, a2, a3, a4, a5, a6, a7, fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7); }}\n")
            else:
                why = failed.get(a, "sin simbolo")
                f.write(f'RetRegs N_{a:08X}(NATIVE_PARAMS) {{ (void)a0; (void)fa0; return rt_unimplemented_named({a:#x}, "{names.get(a, "?")}", "{why}"); }}\n')
    write(f"{RT}/gen/glue.cpp", f.getvalue())
    f = io.StringIO()
    if True:
        f.write("// GENERADO por decomp/tools/lift_native.py: direcciones de los simbolos de game.dol.\n#pragma once\n")
        seen = set()
        for l in open(f"{ROOT}/decomp/game/config/symbols.txt"):
            m = re.match(r"^([A-Za-z_]\w*) = \.(\w+):0x([0-9A-F]+);", l)
            if m and m.group(1) not in seen:
                seen.add(m.group(1)); f.write(f"#define SYM_{m.group(1)} 0x{m.group(3)}u\n")
    write(f"{RT}/gen/syms.h", f.getvalue())
    f = io.StringIO()
    if True:
        f.write("// GENERADO por decomp/tools/lift_native.py: tabla direccion -> funcion para llamadas indirectas.\n")
        f.write('#include "decls.h"\n#include "hle.h"\n\nconst FuncEntry g_functable[] = {\n')
        for a in all_addrs: f.write(f'    {{ {a:#x}, N_{a:08X}, "{names.get(a, "?")}" }},\n')
        f.write(f"}};\nconst unsigned g_functable_count = {len(all_addrs)};\n")
    write(f"{RT}/gen/functable.cpp", f.getvalue())
    from collections import Counter
    print(f"{len(lifted)} funciones elevadas, {len(hle_addr)} sustituidas a mano, {len(failed)} sin elevar")
    print(Counter(v.split(' ')[0] + ' ' + (v.split(' ')[1] if ' ' in v else '') for v in failed.values()).most_common(10))

if __name__ == "__main__":
    main()
