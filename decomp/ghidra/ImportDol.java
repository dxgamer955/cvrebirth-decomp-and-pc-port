// Carga un DOL de GameCube/Wii (Ghidra no trae cargador), aplica symbols.txt de dtk
// y exporta el pseudo-C del decompilador para un rango de direcciones.
// Args: <dol> <symbols.txt> <outDir> <lo> <hi> <sda> <sda2>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

import java.io.*;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class ImportDol extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String dolPath = a[0], symPath = a[1], outDir = a[2];
        long lo = Long.parseLong(a[3], 16), hi = Long.parseLong(a[4], 16);
        long sda = Long.parseLong(a[5], 16), sda2 = Long.parseLong(a[6], 16);

        byte[] dol = Files.readAllBytes(Paths.get(dolPath));
        ByteBuffer h = ByteBuffer.wrap(dol); // big endian por defecto
        Memory mem = currentProgram.getMemory();
        if (mem.getBlocks().length > 0 && mem.getBlock("ram") != null) {
            mem.removeBlock(mem.getBlock("ram"), monitor);
        }
        long minA = Long.MAX_VALUE, maxA = 0;
        for (int i = 0; i < 18; i++) {
            long off = h.getInt(i * 4) & 0xFFFFFFFFL;
            long addr = h.getInt(0x48 + i * 4) & 0xFFFFFFFFL;
            long size = h.getInt(0x90 + i * 4) & 0xFFFFFFFFL;
            if (size == 0) continue;
            String name = (i < 7 ? ".text" + i : ".data" + (i - 7));
            Address s = toAddr(addr);
            MemoryBlock b = mem.createInitializedBlock(name, s,
                new ByteArrayInputStream(dol, (int) off, (int) size), size, monitor, false);
            b.setRead(true); b.setWrite(i >= 7); b.setExecute(i < 7);
            minA = Math.min(minA, addr); maxA = Math.max(maxA, addr + size);
        }
        long bssAddr = h.getInt(0xD8) & 0xFFFFFFFFL, bssSize = h.getInt(0xDC) & 0xFFFFFFFFL;
        // .bss de la cabecera solapa con .sdata/.sdata2: crear solo en los huecos libres
        long cur = bssAddr, end = bssAddr + bssSize;
        int n = 0;
        while (cur < end) {
            MemoryBlock ex = mem.getBlock(toAddr(cur));
            if (ex != null) { cur = ex.getEnd().getOffset() + 1; continue; }
            long gapEnd = end;
            for (MemoryBlock b : mem.getBlocks()) {
                long bs = b.getStart().getOffset();
                if (bs > cur && bs < gapEnd) gapEnd = bs;
            }
            MemoryBlock b = mem.createUninitializedBlock(".bss" + n++, toAddr(cur), gapEnd - cur, false);
            b.setRead(true); b.setWrite(true);
            minA = Math.min(minA, cur); maxA = Math.max(maxA, gapEnd);
            cur = gapEnd;
        }
        // Registros base de datos pequenos (EABI): r13 = _SDA_BASE_, r2 = _SDA2_BASE_
        Register r13 = currentProgram.getLanguage().getRegister("r13");
        Register r2 = currentProgram.getLanguage().getRegister("r2");
        Address amin = toAddr(minA), amax = toAddr(maxA - 1);
        currentProgram.getProgramContext().setValue(r13, amin, amax, BigInteger.valueOf(sda));
        currentProgram.getProgramContext().setValue(r2, amin, amax, BigInteger.valueOf(sda2));

        // Simbolos de dtk (nombre, direccion, tamano)
        Pattern p = Pattern.compile("^(\\S+) = \\.text:0x([0-9A-Fa-f]+);.*type:function size:0x([0-9A-Fa-f]+)");
        List<Object[]> syms = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(symPath))) {
            Matcher m = p.matcher(line);
            if (m.find()) syms.add(new Object[] { m.group(1), Long.parseLong(m.group(2), 16), Long.parseLong(m.group(3), 16) });
        }
        // Ghidra no entiende los paired-singles de Broadway. En este juego solo hay psq_l/psq_st:
        //  - relativos a r1: guardado/restauracion de la mitad alta de f30/f31 -> nop
        //  - resto (qr0): copia de par de floats -> lfd/stfd (W=0) o lfs/stfs (W=1)
        int patched = 0, kept = 0;
        for (Object[] sy : syms) {
            long base = (Long) sy[1], size = (Long) sy[2];
            for (long o = 0; o + 4 <= size; o += 4) {
                Address ad = toAddr(base + o);
                int w = mem.getInt(ad);
                int op = (w >>> 26) & 0x3F;
                if (op != 56 && op != 60) continue;       // psq_l / psq_st
                int rA = (w >>> 16) & 0x1F, frD = (w >>> 21) & 0x1F;
                int wbit = (w >>> 15) & 1, ib = (w >>> 12) & 7;
                if (ib != 0) { kept++; continue; }
                if (rA == 1) { mem.setInt(ad, 0x60000000); patched++; continue; }
                int d12 = w & 0xFFF;
                int d16 = ((d12 & 0x800) != 0) ? (d12 | 0xF000) : d12;
                int nop = (op == 56 ? (wbit == 0 ? 50 : 48) : (wbit == 0 ? 54 : 52));
                mem.setInt(ad, (nop << 26) | (frD << 21) | (rA << 16) | (d16 & 0xFFFF));
                patched++;
            }
        }
        println("psq parcheadas: " + patched + " | sin tocar (gqr!=0): " + kept);
        int made = 0;
        for (Object[] sy : syms) {
            Address ad = toAddr((Long) sy[1]);
            Function f = getFunctionAt(ad);
            if (f == null) { disassemble(ad); f = createFunction(ad, (String) sy[0]); }
            else f.setName((String) sy[0], SourceType.USER_DEFINED);
            if (f != null) made++;
        }
        println("funciones creadas/nombradas: " + made);

        // Decompilar el rango
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        new File(outDir).mkdirs();
        int ok = 0, bad = 0;
        try (PrintWriter all = new PrintWriter(new FileWriter(new File(outDir, "all.c")))) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(lo), true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.getEntryPoint().getOffset() >= hi) break;
                DecompileResults r = ifc.decompileFunction(f, 60, monitor);
                String c;
                if (r.decompileCompleted()) { c = r.getDecompiledFunction().getC(); ok++; }
                else { c = "// FALLO: " + r.getErrorMessage() + "\n"; bad++; }
                String hdr = String.format("// ==== %08x %s ====\n", f.getEntryPoint().getOffset(), f.getName());
                all.print(hdr); all.print(c); all.println();
            }
        }
        println("decompiladas: " + ok + " | fallidas: " + bad);
    }
}
