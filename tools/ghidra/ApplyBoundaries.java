// Applies function-boundary fixes to a Ghidra program: clears a wrong noreturn flag (and un-cuts its callers' flows),
// removes function starts that are pieces of a longer function, creates missing functions and resizes the ones Ghidra
// sized wrongly. Driven by tools/boundaries.py, which writes the actions file from lane HW's boundary list
// (tools/libmatch.py --boundaries --json) and compares the function lists this script writes before and after.
//
// Actions file, one per line, tab-separated, RVAs in hex:
//   returns  RVA            the function at RVA returns after all: clear its noreturn flag, un-cut every call to it (and to
//                           its thunks), disassemble from each fall-through and recompute each caller's body by flow
//   remove   RVA  CONTAINER the function at RVA is part of the one at CONTAINER: delete the function (not its code)
//   create   RVA  SIZE      a function with the body [RVA, RVA+SIZE)
//   resize   RVA  SIZE      set the function's body to [RVA, RVA+SIZE) (created if absent)
//   expect   RVA  SIZE      check only: after the flow repair the function should be SIZE bytes from RVA
// Applied in that order (returns, remove, create/resize by address); expect lines are reported, never forced.
//
// Writes to OUTDIR: before.tsv and after.tsv (every function: rva, size, name) and actions.tsv (one line per action:
// action, rva, result, detail). A body that would overlap another function, or a range that doesn't disassemble cleanly,
// is reported and left alone.
// Usage: analyzeHeadless <proj> <name> -process -noanalysis [-readOnly] -postScript ApplyBoundaries.java ACTIONS OUTDIR
//   -readOnly: nothing is saved (a report on a copy); without it the program is saved with the changes.
//@category TS2Reimpl

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

public class ApplyBoundaries extends GhidraScript {
    private Address base;
    private PrintWriter log;
    private Listing listing;
    private FunctionManager fm;

    private String rva(Address a) {
        return String.format("0x%x", a.subtract(base));
    }

    private Address at(String hex) {
        return base.add(Long.decode(hex));
    }

    private void result(String action, String rva, String result, String detail) {
        log.println(action + "\t" + rva + "\t" + result + "\t" + detail.replace('\t', ' ').replace('\n', ' '));
    }

    private void writeList(Path file) throws Exception {
        try (PrintWriter w = new PrintWriter(file.toFile(), StandardCharsets.UTF_8)) {
            for (Function f : fm.getFunctions(true)) {
                w.println(rva(f.getEntryPoint()) + "\t" + f.getBody().getNumAddresses() + "\t" + f.getName());
            }
        }
    }

    // The noreturn flag: un-cut the calls the analysis cut (a CALL_RETURN flow override or a cleared fall-through),
    // disassemble from each fall-through and recompute the callers' bodies by flow. Same steps as ApplyDb.returnAgain.
    private void returns(String hex) throws Exception {
        Function fn = getFunctionAt(at(hex));
        if (fn == null) {
            result("returns", hex, "absent", "no function there");
            return;
        }
        boolean was = fn.hasNoReturn();
        fn.setNoReturn(false);
        List<Address> targets = new ArrayList<>();
        targets.add(fn.getEntryPoint());
        Address[] thunks = fn.getFunctionThunkAddresses(true);
        if (thunks != null) targets.addAll(List.of(thunks));
        AddressSet starts = new AddressSet();
        Set<Function> callers = new LinkedHashSet<>();
        int cutCalls = 0, orphanCalls = 0;
        for (Address t : targets) {
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(t)) {
                if (!r.getReferenceType().isCall()) continue;
                Instruction in = listing.getInstructionAt(r.getFromAddress());
                if (in == null) continue;
                boolean cut = false;
                if (in.getFlowOverride() == FlowOverride.CALL_RETURN) { in.setFlowOverride(FlowOverride.NONE); cut = true; }
                if (in.isFallThroughOverridden() && in.getFallThrough() == null) { in.clearFallThroughOverride(); cut = true; }
                Address ft = in.getFallThrough();
                if (ft != null && listing.getInstructionAt(ft) == null) { starts.add(ft); cut = true; }
                if (!cut) continue;
                cutCalls++;
                Function c = getFunctionContaining(in.getAddress());
                if (c != null) callers.add(c); else orphanCalls++;
            }
        }
        if (!starts.isEmpty()) new DisassembleCommand(starts, null, true).applyTo(currentProgram, monitor);
        int grown = 0;
        for (Function c : callers) {
            long before = c.getBody().getNumAddresses();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, c, monitor);
            if (c.getBody().getNumAddresses() != before) grown++;
        }
        result("returns", hex, "done", "noreturn was " + was + "; calls un-cut " + cutCalls + "; disassembled from "
                + starts.getNumAddressRanges() + " fall-throughs; callers " + callers.size() + " (" + grown
                + " changed size); calls outside any function " + orphanCalls);
    }

    private void remove(String hex, String container) {
        Address a = at(hex);
        Function f = getFunctionAt(a);
        if (f == null) {
            result("remove", hex, "absent", "no function there (part of " + container + ")");
            return;
        }
        long size = f.getBody().getNumAddresses();
        fm.removeFunction(a);
        result("remove", hex, "done", "size " + size + ", part of " + container);
    }

    // Bytes of [a, a+size) that are no instruction, after disassembling from the start (flow restricted to the range) when
    // nothing is there yet. Gaps are left as they are: in a library body they are __except handlers reached only through a
    // scope table, alignment padding after a ret, or a jump table; disassembling them blindly would turn tables into junk.
    private long gaps;

    // Null when [a, a+size) can be the body: an instruction at the start and none running past the end; else what is wrong.
    private String checkRange(Address a, long size) throws Exception {
        Address end = a.add(size - 1);
        AddressSet range = new AddressSet(a, end);
        if (listing.getInstructionAt(a) == null && listing.getDefinedDataContaining(a) == null) {
            new DisassembleCommand(new AddressSet(a), range, true).applyTo(currentProgram, monitor);
        }
        if (listing.getInstructionAt(a) == null) return "no instruction at the start";
        gaps = 0;
        for (Address x = a; x.compareTo(end) <= 0; ) {
            Instruction in = listing.getInstructionAt(x);
            if (in == null) {
                if (listing.getInstructionContaining(x) != null) return "an instruction straddles " + rva(x);
                gaps++;
                x = x.add(1);
                continue;
            }
            if (in.getMaxAddress().compareTo(end) > 0) return "the instruction at " + rva(x) + " runs past the end";
            x = in.getMaxAddress().add(1);
        }
        return null;
    }

    private String overlaps(AddressSetView body, Function self) {
        StringBuilder b = new StringBuilder();
        Iterator<Function> it = fm.getFunctionsOverlapping(body);
        while (it.hasNext()) {
            Function o = it.next();
            if (o.equals(self)) continue;
            b.append(b.length() > 0 ? ", " : "").append(rva(o.getEntryPoint()));
        }
        return b.length() > 0 ? b.toString() : null;
    }

    private void body(String action, String hex, long size) throws Exception {
        Address a = at(hex);
        AddressSet want = new AddressSet(a, a.add(size - 1));
        Function f = getFunctionAt(a);
        if (f != null && f.getBody().equals(want)) {
            result(action, hex, "already", "size " + size);
            return;
        }
        String other = overlaps(want, f);
        if (other != null) {
            result(action, hex, "conflict", "the body would overlap " + other);
            return;
        }
        String bad = checkRange(a, size);
        if (bad != null) {
            result(action, hex, "not-code", bad);
            return;
        }
        if (f == null) {
            fm.createFunction(null, a, want, SourceType.DEFAULT);
            result(action, hex, "created", "size " + size + (gaps > 0 ? "; " + gaps + " bytes no instruction" : ""));
            return;
        }
        AddressSetView old = f.getBody();
        f.setBody(want);
        AddressSet left = new AddressSet(old).subtract(want);
        if (!left.isEmpty()) dropped.add(new Object[] {hex, left});
        result(action, hex, "resized", "size " + old.getNumAddresses() + " -> " + size
                + (gaps > 0 ? "; " + gaps + " bytes no instruction" : ""));
    }

    // Code a resized function had that no function has once every body is set (a shrink can leave a function Ghidra had
    // merged, or a chunk elsewhere it had attached): reported as "orphan" lines.
    private final List<Object[]> dropped = new ArrayList<>();

    private void orphans() {
        for (Object[] d : dropped) {
            StringBuilder b = new StringBuilder();
            for (AddressRange r : ((AddressSet) d[1]).getAddressRanges()) {
                long code = 0;
                for (Address x = r.getMinAddress(); x.compareTo(r.getMaxAddress()) <= 0; x = x.add(1)) {
                    if (fm.getFunctionContaining(x) == null && listing.getInstructionContaining(x) != null) code++;
                }
                if (code > 0) b.append(" ").append(rva(r.getMinAddress())).append("+").append(r.getLength()).append(" (")
                        .append(code).append(" B code)");
            }
            if (b.length() > 0) result("orphan", (String) d[0], "left", "code in no function:" + b);
        }
    }

    private void expect(String hex, long size) {
        Function f = getFunctionAt(at(hex));
        if (f == null) {
            result("expect", hex, "absent", "expected size " + size);
            return;
        }
        AddressSetView body = f.getBody();
        long n = body.getNumAddresses();
        Address a = f.getEntryPoint(), end = a.add(size - 1);
        if (n == size && body.getNumAddressRanges() == 1 && body.getMaxAddress().equals(end)) {
            result("expect", hex, "agree", "size " + size);
            return;
        }
        // What the expected range has that the body lacks: code no function has (a catch block or __except handler
        // reached only through the EH tables, code after another call the analysis cut), another function, or no code.
        long free = 0, other = 0, none = 0;
        String firstOther = null;
        for (Address x = a; x.compareTo(end) <= 0; x = x.add(1)) {
            if (body.contains(x)) continue;
            Function g = fm.getFunctionContaining(x);
            if (g != null) { other++; if (firstOther == null) firstOther = rva(g.getEntryPoint()); }
            else if (listing.getInstructionContaining(x) != null) free++;
            else none++;
        }
        long beyond = n - body.intersect(new AddressSet(a, end)).getNumAddresses();
        StringBuilder ranges = new StringBuilder();
        for (AddressRange r : body.getAddressRanges()) ranges.append(" ").append(rva(r.getMinAddress())).append("+").append(r.getLength());
        result("expect", hex, "differs", "size " + n + " (" + ranges.toString().trim() + "), expected " + size
                + "; missing from the body: " + free + " B code in no function, " + other + " B in other functions"
                + (firstOther != null ? " (" + firstOther + ")" : "") + ", " + none + " B no code; body beyond: " + beyond + " B");
    }

    private void probe(String hex) {
        Address a = at(hex);
        Instruction in = listing.getInstructionContaining(a);
        Data d = listing.getDataContaining(a);
        Function f = fm.getFunctionContaining(a);
        result("probe", hex, "info", "instruction " + (in == null ? "none" : rva(in.getAddress()) + " " + in)
                + "; data " + (d == null ? "none" : rva(d.getAddress()) + " " + d.getDataType().getName() + " len " + d.getLength())
                + "; function " + (f == null ? "none" : rva(f.getEntryPoint()))
                + "; bookmarks " + currentProgram.getBookmarkManager().getBookmarks(a).length);
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Path actions = Path.of(args[0]);
        Path out = Path.of(args[1]);
        Files.createDirectories(out);
        base = currentProgram.getImageBase();
        listing = currentProgram.getListing();
        fm = currentProgram.getFunctionManager();
        writeList(out.resolve("before.tsv"));
        List<String[]> lines = new ArrayList<>();
        for (String l : Files.readAllLines(actions, StandardCharsets.UTF_8)) {
            if (!l.isBlank()) lines.add(l.trim().split("\t"));
        }
        long t0 = System.currentTimeMillis();
        try (PrintWriter w = new PrintWriter(out.resolve("actions.tsv").toFile(), StandardCharsets.UTF_8)) {
            log = w;
            // The order matters: flows first (bodies grow by flow), then pieces go, then explicit bodies.
            for (String kind : new String[] {"probe", "returns", "remove", "body", "expect"}) {
                for (String[] a : lines) {
                    if (monitor.isCancelled()) return;
                    try {
                        switch (kind) {
                            case "probe": if (a[0].equals("probe")) probe(a[1]); break;
                            case "returns": if (a[0].equals("returns")) returns(a[1]); break;
                            case "remove": if (a[0].equals("remove")) remove(a[1], a.length > 2 ? a[2] : "?"); break;
                            case "body":
                                if (a[0].equals("create") || a[0].equals("resize")) body(a[0], a[1], Long.decode(a[2]));
                                break;
                            default: if (a[0].equals("expect")) expect(a[1], Long.decode(a[2]));
                        }
                    } catch (Exception e) {
                        result(a[0], a[1], "error", e.toString());
                    }
                }
                if (kind.equals("body")) orphans();
                println("ApplyBoundaries: " + kind + " pass done at " + (System.currentTimeMillis() - t0) / 1000 + " s");
            }
        }
        writeList(out.resolve("after.tsv"));
        println("ApplyBoundaries: done in " + (System.currentTimeMillis() - t0) / 1000 + " s; lists in " + out);
    }
}
