// Applies db/classes.jsonl (from tools/rtti.py) to the program:
//   - creates a function at every vftable entry (many are only reachable through vtables,
//     so Ghidra's auto-analysis never finds them)
//   - labels each vftable as <Class>::vftable (or vftable_<offset> for secondary tables)
// Run as a -preScript so auto-analysis afterwards picks up the new functions.
//@category TS2Reimpl

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class ApplyRtti extends GhidraScript {

    @Override
    protected void run() throws Exception {
        Path in = Path.of(getScriptArgs()[0]);
        Address base = currentProgram.getImageBase();
        SymbolTable st = currentProgram.getSymbolTable();
        int created = 0, labelled = 0;

        for (String line : Files.readAllLines(in, StandardCharsets.UTF_8)) {
            if (monitor.isCancelled()) return;
            JsonObject c = JsonParser.parseString(line).getAsJsonObject();
            JsonArray tables = c.getAsJsonArray("vftables");
            if (tables.isEmpty()) continue;

            // Plain names become nested namespaces (A::B); template names stay mangled as one namespace.
            String name = c.get("name").getAsString();
            Namespace ns = currentProgram.getGlobalNamespace();
            for (String part : name.startsWith(".?") ? new String[] { name } : name.split("::")) {
                ns = st.getOrCreateNameSpace(ns, part, SourceType.ANALYSIS);
            }

            for (JsonElement te : tables) {
                JsonObject t = te.getAsJsonObject();
                Address vt = base.add(Long.decode(t.get("rva").getAsString()));
                int offset = t.get("offset").getAsInt();
                st.createLabel(vt, offset == 0 ? "vftable" : "vftable_" + offset, ns, SourceType.ANALYSIS);
                labelled++;

                for (JsonElement ee : t.getAsJsonArray("entries")) {
                    Address fa = base.add(Long.decode(ee.getAsString()));
                    if (getFunctionContaining(fa) != null && getFunctionAt(fa) == null) {
                        // Entry points into the middle of an existing function: leave it for review.
                        continue;
                    }
                    if (getFunctionAt(fa) == null) {
                        if (getInstructionAt(fa) == null) {
                            new DisassembleCommand(fa, null, true).applyTo(currentProgram, monitor);
                        }
                        if (new CreateFunctionCmd(fa).applyTo(currentProgram, monitor)) created++;
                    }
                }
            }
        }
        println("vftables labelled: " + labelled + ", functions created: " + created);
    }
}
