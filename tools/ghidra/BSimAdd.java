// Adds the BSim signatures of every function in the current program (a library object) to a local
// H2 BSim database. Run by tools/bsim.py build, headless, once per imported object:
//   analyzeHeadless <proj> <name> -import <x.lib|dir> -recursive -readOnly -postScript BSimAdd.java <db.mv.db> <library>
// The executable is recorded as "<library>/<object name>" so a match names its library.
// Objects with no function bodies (data-only members) and duplicates (same MD5) are skipped.
//@category TS2Reimpl

import ghidra.app.script.GhidraScript;
import ghidra.features.bsim.query.BSimClientFactory;
import ghidra.features.bsim.query.BSimServerInfo;
import ghidra.features.bsim.query.FunctionDatabase;
import ghidra.features.bsim.query.FunctionDatabase.BSimError;
import ghidra.features.bsim.query.GenSignatures;
import ghidra.features.bsim.query.description.DatabaseInformation;
import ghidra.features.bsim.query.description.DescriptionManager;
import ghidra.features.bsim.query.protocol.InsertRequest;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.util.ArrayList;
import java.util.List;

public class BSimAdd extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String db = args[0];
        String library = args.length > 1 ? args[1] : "lib";
        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();

        // Name each function by its mangled symbol when the demangler gave it a short name, so a
        // match carries the decorated name libmatch and db/library-matches.jsonl use.
        List<Function> funcs = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) {
            if (f.isThunk() || f.isExternal() || f.getBody().getNumAddresses() < 2) continue;
            for (Symbol s : st.getSymbols(f.getEntryPoint())) {
                if (s.getName().startsWith("?") && !s.getName().equals(f.getName())) {
                    try {
                        f.setName(s.getName(), ghidra.program.model.symbol.SourceType.IMPORTED);
                    } catch (Exception e) {
                        // keep Ghidra's name
                    }
                    break;
                }
            }
            funcs.add(f);
        }
        if (funcs.isEmpty()) {
            println("BSimAdd: " + currentProgram.getName() + ": no functions");
            return;
        }

        BSimServerInfo info = new BSimServerInfo(db);
        try (FunctionDatabase h2 = BSimClientFactory.buildClient(info, false)) {
            if (!h2.initialize()) {
                throw new Exception("BSimAdd: " + h2.getLastError().message);
            }
            DatabaseInformation dbInfo = h2.getInfo();
            GenSignatures gensig = new GenSignatures(dbInfo.trackcallgraph);
            try {
                gensig.setVectorFactory(h2.getLSHVectorFactory());
                gensig.addExecutableCategories(dbInfo.execats);
                gensig.addFunctionTags(dbInfo.functionTags);
                gensig.addDateColumnName(dbInfo.dateColumnName);
                String exe = library + "/" + currentProgram.getName();
                gensig.openProgram(currentProgram, exe, null, null, "ghidra://local/" + library, "/" + library);
                gensig.scanFunctions(funcs.iterator(), funcs.size(), monitor);
                DescriptionManager manager = gensig.getDescriptionManager();
                if (manager.numFunctions() == 0) {
                    println("BSimAdd: " + exe + ": no signatures");
                    return;
                }
                manager.listAllFunctions().forEachRemaining(fd -> fd.sortCallgraph());
                InsertRequest req = new InsertRequest();
                req.manage = manager;
                if (req.execute(h2) == null) {
                    BSimError err = h2.getLastError();
                    println("BSimAdd: " + exe + ": skipped: " + err.message);
                    return;
                }
                println("BSimAdd: " + exe + ": " + manager.numFunctions() + " functions");
            } finally {
                gensig.dispose();
            }
        }
    }
}
