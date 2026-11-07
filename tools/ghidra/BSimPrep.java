// Pre-script for tools/bsim.py build: turns off the analyzers a library object doesn't need before BSim
// signatures. "Apply Data Archives" alone took ~7 s of ~9 s per object (it loads the Windows type archive).
//@category TS2Reimpl

import ghidra.app.script.GhidraScript;

public class BSimPrep extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (String a : new String[] { "Apply Data Archives", "ASCII Strings", "Function ID", "Embedded Media" }) {
            setAnalysisOption(currentProgram, a, "false");
        }
    }
}
