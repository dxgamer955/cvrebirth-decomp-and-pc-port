"""Classifies tiny unnamed functions (<= 0x20 bytes: empty, return a constant, read/write a field of r3, jump to another function) and
proposes names for renames.txt, the executed ones first. Usage: classify_small.py [profile.log [count]]
(the log is the output of RT_PROFILE=1 RT_PROFILE_N=100000)."""
import re,glob,collections,sys,os
root=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')+'/'
prof=sys.argv[1] if len(sys.argv)>1 else 'profile.log'
counts={}
for l in (open(prof,errors='replace') if os.path.exists(prof) else []):
    m=re.match(r'\s+(\d+) (fn_[0-9A-F]{8})$',l.rstrip())
    if m: counts[m.group(2)]=int(m.group(1))
sizes={}; names={}
for l in open(root+'decomp/game/config/symbols.txt'):
    m=re.match(r'(fn_[0-9A-F]{8}) = .*size:0x([0-9A-Fa-f]+)',l)
    if m: sizes[m.group(1)]=int(m.group(2),16)
    m=re.match(r'(\S+) = \.text:0x([0-9A-Fa-f]{8});',l)
    if m: names[m.group(2).upper()]=m.group(1)
want={k for k in sizes if sizes[k]<=0x20}
bodies={}
for f in glob.glob(root+'runtime/gen/lifted_*.cpp'):
    txt=open(f).read()
    for m in re.finditer(r'^RetRegs N_([0-9A-F]{8})\(NATIVE_PARAMS\) \{\n(.*?)^\}',txt,re.S|re.M):
        k='fn_'+m.group(1)
        if k in want: bodies[k]=m.group(2)
skip=re.compile(r'^\s*(u32|f64|f32) [rf]\d+ = (0xC0DE|a\d|fa\d|g_sp|0x8031|0\.0|F64)|RtFrame|u32 cr = |^\s*return MKRET|^\s*f64 f\d+ = F64FROMBITS')
out=[]
for k,b in bodies.items():
    lines=[l.strip() for l in b.split('\n') if l.strip() and not skip.search(l)]
    name=None; a=k[3:]
    if not lines: name=f'Nop_{a}'
    elif len(lines)==1:
        m=re.fullmatch(r'r3 = LD32\(\(r3 \+ 0x([0-9a-f]+)\)\);',lines[0]); 
        if m: name=f'GetU32_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'r3 = LD32\(r3\);',lines[0])
        if m: name=f'GetU32_00_{a}'
        m=re.fullmatch(r'r3 = LD8\(\(r3 \+ 0x([0-9a-f]+)\)\);',lines[0]); 
        if m: name=f'GetU8_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'r3 = LD16\(\(r3 \+ 0x([0-9a-f]+)\)\);',lines[0]); 
        if m: name=f'GetU16_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'f1 = \(f64\)LDF\(\(r3 \+ 0x([0-9a-f]+)\)\);',lines[0])
        if m: name=f'GetF32_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'f1 = \(f64\)LDF\(r3\);',lines[0])
        if m: name=f'GetF32_00_{a}'
        m=re.fullmatch(r'ST32\(\(r3 \+ 0x([0-9a-f]+)\), r4\);',lines[0])
        if m: name=f'SetU32_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'ST32\(r3, r4\);',lines[0])
        if m: name=f'SetU32_00_{a}'
        m=re.fullmatch(r'STF\(\(r3 \+ 0x([0-9a-f]+)\), \(f32\)f1\);',lines[0])
        if m: name=f'SetF32_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'ST8\(\(r3 \+ 0x([0-9a-f]+)\), r4\);',lines[0])
        if m: name=f'SetU8_{int(m.group(1),16):02X}_{a}'
        m=re.fullmatch(r'r3 = (0x[0-9a-f]+|0);',lines[0])
        if m: name=f'ReturnConst_{int(m.group(1),16):X}_{a}'
    if not name:
        tgt=lambda t: names.get(t.upper(),'fn_'+t.upper())
        sdh=lambda v: ('m%X'%(0x10000-(v&0xFFFF)) if v&0x8000 else '%X'%(v&0xFFFF))
        L=lines
        m=re.fullmatch(r'TAILD\(N_([0-9A-F]{8})\);',L[0]) if len(L)==1 else None
        if m: name=f'JumpTo_{tgt(m.group(1))}_{a}'
        m=re.fullmatch(r'r3 = r3 \+ 0x([0-9a-f]+);',L[0]) if len(L)==1 else None
        if m: name=f'FieldAddr_{int(m.group(1),16):02X}_{a}'
        if len(L)==2:
            m1=re.fullmatch(r'r3 = r3 \+ 0x([0-9a-f]+);',L[0]); m2=re.fullmatch(r'TAILD\(N_([0-9A-F]{8})\);',L[1])
            if m1 and m2: name=f'JumpToField_{int(m1.group(1),16):02X}_{tgt(m2.group(1))}_{a}'
            m1=re.fullmatch(r'r0 = (0x[0-9a-f]+|0);',L[0]); m2=re.fullmatch(r'ST(32|8)\(\(?r3(?: \+ 0x([0-9a-f]+))?\)?, r0\);',L[1])
            if m1 and m2: name=f'SetU{m2.group(1)}_{int(m2.group(2) or "0",16):02X}_To{int(m1.group(1),16):X}_{a}'
            m1=re.fullmatch(r'r3 = LD32\(\(r3 \+ 0x([0-9a-f]+)\)\);',L[0]); m2=re.fullmatch(r'r3 = LD32\(\(r3 \+ 0x([0-9a-f]+)\)\);',L[1])
            if m1 and m2: name=f'GetU32_{int(m1.group(1),16):02X}_{int(m2.group(1),16):02X}_{a}'
            m1=re.fullmatch(r'r3 = LD32\(\(r3 \+ 0x([0-9a-f]+)\)\);',L[0]); m2=re.fullmatch(r'TAILD\(N_([0-9A-F]{8})\);',L[1])
            if m1 and m2: name=f'JumpWithField_{int(m1.group(1),16):02X}_{tgt(m2.group(1))}_{a}'
        m=re.fullmatch(r'r3 = LD32\(\(r13 \+ 0x([0-9a-f]+)\)\);',L[0]) if len(L)==1 else None
        if m: name=f'GetSDA13_{sdh(int(m.group(1),16))}_{a}'
    if name: out.append((counts.get(k,0),k,name,lines))
out.sort(reverse=True)
print(len(bodies),'tiny functions;',len(out),'classified',file=sys.stderr)
for c,k,n,l in out[:int(sys.argv[2]) if len(sys.argv)>2 else 100]: print(f'{k} {n}  # tiny function, {c} calls in the test run')
