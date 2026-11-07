// Exports facts (no code bytes) from the analysed program into db/:
//   functions.jsonl  one function per line: rva, size, name, namespace, flags, callees
//   (vftables come from tools/rtti.py -> classes.jsonl)
// All addresses are RVAs (hex strings) since the game runs with ASLR.
// Usage: analyzeHeadless <proj> <name> -process -noanalysis -readOnly -postScript ExportDb.java <outDir>
//@category TS2Reimpl

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Set;
import java.util.TreeSet;

public class ExportDb extends GhidraScript {

    private Address base;

    private String rva(Address a) {
        return String.format("0x%x", a.subtract(base));
    }

    private static String q(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "db");
        out.mkdirs();
        base = currentProgram.getImageBase();
        FunctionManager fm = currentProgram.getFunctionManager();

        int n = 0;
        try (PrintWriter w = new PrintWriter(new File(out, "functions.jsonl"), StandardCharsets.UTF_8)) {
            for (Function f : fm.getFunctions(true)) {
                if (monitor.isCancelled()) return;
                Set<String> callees = new TreeSet<>();
                for (Function c : f.getCalledFunctions(monitor)) {
                    // Imports have no RVA; record them as "DLL!name".
                    callees.add(c.isExternal()
                        ? c.getExternalLocation().getLibraryName() + "!" + c.getName()
                        : rva(c.getEntryPoint()));
                }
                w.println("{\"rva\":" + q(rva(f.getEntryPoint()))
                    + ",\"size\":" + f.getBody().getNumAddresses()
                    + ",\"name\":" + q(f.getName())
                    + ",\"namespace\":" + q(f.getParentNamespace().getName(true))
                    + ",\"source\":" + q(f.getSymbol().getSource().toString())
                    + ",\"cc\":" + q(f.getCallingConventionName())
                    + ",\"thunk\":" + f.isThunk()
                    + ",\"callees\":[" + String.join(",", callees.stream().map(ExportDb::q).toList()) + "]}");
                n++;
            }
        }
        println("functions: " + n);

    }
}
