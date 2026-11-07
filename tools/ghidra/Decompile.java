// Writes Ghidra's decompilation of the given functions to <outDir>/<rva>.c, for reading while writing
// a replacement (tools/decompile.ps1). The output is decompiler output: it stays in local/ (never
// db/, src/ or any commit), and replacements are written from an understanding of it, not copied.
// Usage: analyzeHeadless <proj> <name> -process -noanalysis -readOnly -postScript Decompile.java <outDir> <rva>...
//@category TS2Reimpl

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class Decompile extends GhidraScript {

    // Seconds: two minutes, longer for big bodies (about a minute per 4 KB).
    private static int timeout(Function f) {
        return (int) Math.max(120, f.getBody().getNumAddresses() / 70);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        Address base = currentProgram.getImageBase();
        DecompInterface decomp = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.setMaxPayloadMBytes(512); // GenericSimCall's 52 KB body overflows the 50 MB default
        decomp.setOptions(options);
        decomp.openProgram(currentProgram);
        for (int i = 1; i < args.length; i++) {
            long rva = Long.decode(args[i]);
            Address at = base.add(rva);
            Function f = getFunctionContaining(at);
            String name = String.format("0x%x", rva);
            if (f == null) {
                println("Decompile: no function at " + name);
                continue;
            }
            DecompileResults r = decomp.decompileFunction(f, timeout(f), monitor);
            String text = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage() + "\n";
            File file = new File(out, String.format("0x%x.c", f.getEntryPoint().subtract(base)));
            try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
                w.printf("// %s at RVA 0x%x, %d bytes (Ghidra decompilation; local only, never commit)%n", f.getName(true),
                         f.getEntryPoint().subtract(base), f.getBody().getNumAddresses());
                w.print(text);
            }
            println("Decompile: " + name + " -> " + file);
        }
        decomp.dispose();
    }
}
