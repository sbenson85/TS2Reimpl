// A persistent Ghidra server (tools/ghidra.py starts and talks to it): keeps Ghidra and the decompiler warm so a
// decompile costs well under a second instead of a ~15 s headless start, and lanes share one Ghidra instead of
// fighting over the project lock and memory. Runs under analyzeHeadless -readOnly on its own copy of the analysed
// project (local/ghidra-server/proj-*; local/ghidra is never opened or written).
//
// Two program instances:
//   raw        the one headless opened, never changed (the same as tools/decompile.ps1 without -Annotated);
//   annotated  a writable program in its own project, local/ghidra-server/ann/: the raw program with db/'s facts applied
//              by ApplyDb.java (the slow part, ~170 s, done once and cached: the project is saved, and a restart with
//              the same facts opens it in seconds), plus every fact committed since through /annotate. Each commit saves
//              the project. The fact text itself (db/annotations.jsonl and db/methods/*.jsonl, with the lane and
//              tentative flag of each entry) is kept in the program's options ("ledger"), mirrored to ann/ledger.json:
//              tools/ghidra.py export writes it back to db/.
// An overlay (a facts file in tools/annotate.py's format, usually a few entries) is applied to the annotated instance
// inside a transaction, decompiled, then rolled back; the decompiler's cache is flushed before every decompile.
//
// HTTP on 127.0.0.1 only. Work requests run one at a time, in arrival order (a fair lock: the rest queue, which also
// serialises writers); /status, /cancel and /shutdown are answered at once, even while a long decompile runs. Each work
// request has its own task monitor: /cancel cancels it, which kills the native decompiler process (DecompInterface stops
// it on cancel; the next decompile starts a new one), so the request fails with "cancelled" and the queue moves on.
// POST, JSON bodies:
//   /decompile {"rvas", "raw", "overlay": "path.json", "out": "dir under local/", "addr", "callees"} -> {"results": [{rva, file, text, callees}], "log"}
//   /annotate  {"overlay": delta path, "revert", "ledger": {"version", "files"}, "base_version", "commit", "watch", "callers", "max"}
//              undoes `revert` (facts deleted or replaced: functions back to the raw program, fields cleared, types removed or
//              renamed, locals, globals, equates, call-site overrides), then applies the delta; all or nothing (any unapplied
//              fact rolls the transaction back); on commit stores the ledger and saves; returns the affected functions'
//              pseudo-code before and after, and "roles" (named / caller / user of a changed type)
//   /ledger    -> {"version", "files"}
//   /snapshot {"zip": "path under local/"}  saves and zips the annotated project
//   /reload {"facts", "ledger"}  rebuild the annotated project from facts (drops the cache)
//   /vars {"rva"}  the decompiler's local variables of a function: name, type, storage, first use (for locals by name)
//   /cancel    cancels the running work request (not /reload or /snapshot); waits up to 30 s for it to stop
//   /status (with the running request and the queue length), /shutdown (cancels the running request)
// Usage: analyzeHeadless <proj> Sims2EP9 -process -noanalysis -readOnly -postScript DecompServer.java <facts.json> <stateFile>
//          <rawOut> <annotatedOut> <overlayOut> <annDir> <open|build> <ledger.json> <localRoot>
//@category TS2Reimpl

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import ghidra.app.decompiler.ClangLine;
import ghidra.app.decompiler.ClangToken;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.component.DecompilerUtils;
import ghidra.app.script.GhidraScript;
import ghidra.app.script.GhidraState;
import ghidra.base.project.GhidraProject;
import ghidra.framework.options.Options;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.Program;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.pcode.HighSymbol;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.FileInputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.ReentrantLock;
import ghidra.util.exception.CancelledException;
import ghidra.util.task.TaskMonitor;
import ghidra.util.task.TaskMonitorAdapter;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public class DecompServer extends GhidraScript {
    private static final String LEDGER_OPTIONS = "TS2Reimpl facts";
    private Program raw, annotated;
    private GhidraProject annProject;
    private DecompInterface rawDecomp, annDecomp;
    private String factsPath;
    private File rawOut, annOut, overlayOut, annDir, localRoot;
    private long started = System.currentTimeMillis();
    private int requests;
    private String lastApply = "";
    private long applyMs;
    private String cache = "";
    private long ledgerVersion;
    private JsonObject ledgerFiles = new JsonObject();
    private final CountDownLatch done = new CountDownLatch(1);

    // Work requests, one at a time in arrival order; the running one and how many wait.
    private final ReentrantLock work = new ReentrantLock(true);
    private final AtomicInteger queued = new AtomicInteger();
    private final AtomicInteger requestIds = new AtomicInteger();

    private static final class Running {
        final int id;
        final String path, what;
        final boolean cancellable;
        final TaskMonitorAdapter mon;
        final long t0 = System.currentTimeMillis();

        Running(int id, String path, String what, boolean cancellable) {
            this.id = id;
            this.path = path;
            this.what = what;
            this.cancellable = cancellable;
            this.mon = new TaskMonitorAdapter(cancellable);
        }

        JsonObject json() {
            JsonObject o = new JsonObject();
            o.addProperty("id", id);
            o.addProperty("path", path);
            o.addProperty("what", what);
            o.addProperty("running_s", (System.currentTimeMillis() - t0) / 1000);
            o.addProperty("cancellable", cancellable);
            return o;
        }
    }

    private volatile Running current;
    private final Map<Function, String> roles = new LinkedHashMap<>(); // affected(): why each function is looked at
    private TaskMonitor reqMon = TaskMonitor.DUMMY; // the running work request's monitor (only the worker holding `work` uses it)

    private void checkCancelled() throws CancelledException {
        if (reqMon.isCancelled()) throw new CancelledException("cancelled (tools/ghidra.py cancel)");
    }

    private DecompInterface open(Program p) {
        DecompInterface d = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.setMaxPayloadMBytes(512); // as Decompile.java
        d.setOptions(options);
        d.openProgram(p);
        return d;
    }

    // Runs ApplyDb.java's logic on the annotated instance (the caller holds a transaction); returns its output.
    private String apply(String facts) throws Exception {
        StringWriter sw = new StringWriter();
        ApplyDb a = new ApplyDb();
        a.setScriptArgs(new String[] { facts });
        GhidraState st = new GhidraState(null, state.getProject(), annotated, null, null, null);
        try (PrintWriter w = new PrintWriter(sw)) {
            a.execute(st, reqMon, w);
        }
        checkCancelled();
        return sw.toString();
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    // Output goes under local/ only (a request could otherwise name any folder).
    private File underLocal(File f) throws Exception {
        File c = f.getCanonicalFile();
        if (!c.toPath().startsWith(localRoot.getCanonicalFile().toPath()))
            throw new IllegalArgumentException("path outside " + localRoot + ": " + f);
        return c;
    }

    private void closeAnnotated() throws Exception {
        if (annDecomp != null) annDecomp.dispose();
        annDecomp = null;
        if (annotated != null) annotated.release(this);
        annotated = null;
        if (annProject != null) annProject.close();
        annProject = null;
    }

    // Opened as a plain domain object (GhidraProject.openProgram would keep a "Batch Processing" transaction open for the
    // life of the program, which blocks save and makes an inner rollback undo everything).
    private Program openAnnotated() throws Exception {
        ghidra.framework.model.DomainFile df = annProject.getProjectData().getRootFolder().getFile("Sims2EP9");
        return (Program) df.getDomainObject(this, true, false, monitor);
    }

    private void readLedgerFromOptions() {
        Options o = annotated.getOptions(LEDGER_OPTIONS);
        ledgerVersion = o.getLong("version", 0);
        ledgerFiles = new JsonObject();
        JsonArray names = JsonParser.parseString(o.getString("names", "[]")).getAsJsonArray();
        for (int i = 0; i < names.size(); i++) ledgerFiles.addProperty(names.get(i).getAsString(), o.getString("f" + i, ""));
    }

    // Inside a transaction.
    private void storeLedgerInOptions() {
        Options o = annotated.getOptions(LEDGER_OPTIONS);
        o.setLong("version", ledgerVersion);
        JsonArray names = new JsonArray();
        int i = 0;
        for (Map.Entry<String, JsonElement> e : ledgerFiles.entrySet()) {
            names.add(e.getKey());
            o.setString("f" + i++, e.getValue().getAsString());
        }
        o.setString("names", names.toString());
    }

    private void writeMirror() throws Exception {
        JsonObject m = new JsonObject();
        m.addProperty("version", ledgerVersion);
        m.add("files", ledgerFiles);
        Files.writeString(new File(annDir, "ledger.json").toPath(), m.toString(), StandardCharsets.UTF_8);
    }

    private void setLedger(JsonObject l) {
        ledgerVersion = l.get("version").getAsLong();
        ledgerFiles = l.getAsJsonObject("files");
    }

    // Applies the facts to a read-only copy of the raw program, saves it as the annotated project, reopens it writable.
    private void build(String facts, String ledgerFile) throws Exception {
        closeAnnotated();
        underLocal(annDir);
        deleteTree(annDir);
        annDir.mkdirs();
        long t0 = System.currentTimeMillis();
        Program ro = (Program) currentProgram.getDomainFile().getReadOnlyDomainObject(this, -1, monitor);
        annotated = ro;
        int tx = ro.startTransaction("db facts");
        try {
            lastApply = apply(facts);
        } finally {
            ro.endTransaction(tx, true);
        }
        applyMs = System.currentTimeMillis() - t0;
        println("DecompServer: facts applied in " + applyMs / 100 / 10.0 + " s; " + lastApply.lines().filter(l -> l.contains("ApplyDb: ") && l.contains("structures,")).findFirst().orElse(""));
        long t1 = System.currentTimeMillis();
        annProject = GhidraProject.createProject(annDir.getPath(), "ann", false);
        annProject.saveAs(ro, "/", "Sims2EP9", true);
        // saveAs lists ro among the project's open programs, whose close() later releases it as the project's own
        // consumer, which it never was: a reload then failed in closeAnnotated. Hand it over and close it here instead.
        ro.addConsumer(annProject);
        annProject.close(ro);
        ro.release(this);
        annotated = openAnnotated();
        println("DecompServer: annotated project saved in " + (System.currentTimeMillis() - t1) / 100 / 10.0 + " s");
        JsonObject l = JsonParser.parseString(Files.readString(new File(ledgerFile).toPath(), StandardCharsets.UTF_8)).getAsJsonObject();
        setLedger(l);
        tx = annotated.startTransaction("ledger");
        try {
            storeLedgerInOptions();
        } finally {
            annotated.endTransaction(tx, true);
        }
        annotated.getDomainFile().save(monitor);
        annotated.clearUndo();
        writeMirror();
        annDecomp = open(annotated);
        cache = "built";
    }

    private void openCached() throws Exception {
        closeAnnotated();
        long t0 = System.currentTimeMillis();
        annProject = GhidraProject.openProject(annDir.getPath(), "ann");
        annotated = openAnnotated();
        readLedgerFromOptions();
        writeMirror();
        annDecomp = open(annotated);
        cache = "reused";
        println("DecompServer: annotated project opened in " + (System.currentTimeMillis() - t0) / 100 / 10.0 + " s (ledger version " + ledgerVersion + ")");
    }

    private String lineText(ClangLine line) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < line.getIndent(); i++) sb.append(' ');
        for (ClangToken t : line.getAllTokens()) sb.append(t.getText());
        return sb.toString();
    }

    private String body(Program p, DecompileResults res, boolean addr) {
        if (!res.decompileCompleted()) return "// failed: " + res.getErrorMessage() + "\n";
        if (!addr) return res.getDecompiledFunction().getC();
        // One line of the pseudo-C per line, prefixed with the lowest RVA of the instructions it came from (blank where none).
        StringBuilder sb = new StringBuilder();
        Address base = p.getImageBase();
        for (ClangLine line : DecompilerUtils.toLines(res.getCCodeMarkup())) {
            Address min = null;
            for (ClangToken t : line.getAllTokens()) {
                Address a = t.getMinAddress();
                if (a != null && (min == null || a.compareTo(min) < 0)) min = a;
            }
            sb.append(min == null ? "       " : String.format("%07x", min.subtract(base))).append("| ").append(lineText(line)).append('\n');
        }
        return sb.toString();
    }

    private String decompileText(Program p, DecompInterface d, Function f, boolean addr) throws CancelledException {
        return decompileText(p, d, f, addr, (int) Math.max(120, f.getBody().getNumAddresses() / 70));
    }

    private String decompileText(Program p, DecompInterface d, Function f, boolean addr, int timeout) throws CancelledException {
        checkCancelled();
        d.flushCache(); // the program may have changed (an overlay, a rollback, a commit) since the last decompile
        DecompileResults res = d.decompileFunction(f, timeout, reqMon); // a cancel kills the decompiler process
        checkCancelled();
        String head = String.format("// %s at RVA 0x%x, %d bytes (Ghidra decompilation; local only, never commit)%n", f.getName(true),
                                    f.getEntryPoint().subtract(p.getImageBase()), f.getBody().getNumAddresses());
        return head + body(p, res, addr);
    }

    private JsonObject decompile(Program p, DecompInterface d, JsonObject q, File out) throws Exception {
        out.mkdirs();
        boolean addr = q.has("addr") && q.get("addr").getAsBoolean();
        boolean callees = q.has("callees") && q.get("callees").getAsBoolean();
        JsonArray results = new JsonArray();
        Address base = p.getImageBase();
        for (JsonElement re : q.getAsJsonArray("rvas")) {
            long rva = Long.decode(re.getAsString());
            JsonObject r = new JsonObject();
            r.addProperty("rva", String.format("0x%x", rva));
            Function f = p.getFunctionManager().getFunctionContaining(base.add(rva));
            if (f == null) {
                r.addProperty("error", "no function");
                results.add(r);
                continue;
            }
            String text = decompileText(p, d, f, addr);
            long entry = f.getEntryPoint().subtract(base);
            File file = new File(out, String.format("0x%x.c", entry));
            try (PrintWriter w = new PrintWriter(file, StandardCharsets.UTF_8)) {
                w.print(text);
            }
            r.addProperty("file", file.getPath());
            r.addProperty("text", text);
            if (callees) {
                JsonArray cs = new JsonArray();
                java.util.TreeMap<Long, String> sorted = new java.util.TreeMap<>();
                for (Function c : f.getCalledFunctions(monitor)) {
                    if (!c.isExternal()) sorted.put(c.getEntryPoint().subtract(base), c.getName(true));
                }
                for (Map.Entry<Long, String> e : sorted.entrySet()) {
                    JsonObject c = new JsonObject();
                    c.addProperty("rva", String.format("0x%x", e.getKey()));
                    c.addProperty("name", e.getValue());
                    cs.add(c);
                }
                r.add("callees", cs);
            }
            results.add(r);
        }
        JsonObject o = new JsonObject();
        o.add("results", results);
        return o;
    }

    private Function functionAt(String rva) {
        return annotated.getFunctionManager().getFunctionContaining(annotated.getImageBase().add(Long.decode(rva)));
    }

    private static void collect(JsonObject delta, String section, List<String> into) {
        if (!delta.has(section)) return;
        for (JsonElement e : delta.getAsJsonArray(section)) {
            JsonObject o = e.getAsJsonObject();
            if (o.has("rva") && !o.has("kind")) into.add(o.get("rva").getAsString());
            if (o.has("rva") && o.has("kind") && o.get("kind").getAsString().equals("function")) into.add(o.get("rva").getAsString());
        }
    }

    private static DataType unwrap(DataType dt) {
        while (true) {
            if (dt instanceof Pointer p && p.getDataType() != null) dt = p.getDataType();
            else if (dt instanceof TypeDef t) dt = t.getBaseDataType();
            else return dt;
        }
    }

    // The functions whose pseudo-code a delta can change: the ones it names, then (up to `callersPer` each) their callers
    // when a signature or name changed, then (up to `maxUsers`) functions with a parameter of a changed structure.
    private List<Function> affected(JsonObject delta, JsonObject revert, JsonArray watch, int callersPer, int max) {
        List<String> direct = new ArrayList<>();
        if (watch != null) for (JsonElement w : watch) direct.add(w.getAsString());
        collect(delta, "functions", direct);
        collect(delta, "annotations", direct);
        collect(delta, "impls", direct);
        collect(delta, "compiled", direct);
        collect(delta, "locals", direct);
        for (JsonElement e : arr(revert, "functions")) direct.add(e.getAsString());
        for (JsonElement e : arr(revert, "locals")) direct.add(e.getAsJsonObject().get("rva").getAsString());
        for (JsonElement e : arr(revert, "callsites")) direct.add(e.getAsString());
        for (JsonElement e : arr(revert, "equates")) direct.add(e.getAsJsonObject().get("rva").getAsString());
        if (delta.has("annotations")) { // call-site overrides and equates sit inside a function: that one changes
            for (JsonElement e : delta.getAsJsonArray("annotations")) {
                JsonObject o = e.getAsJsonObject();
                String k = o.get("kind").getAsString();
                if ((k.equals("callsite") || k.equals("equate") || k.equals("local") || k.equals("flags")) && o.has("rva")) direct.add(o.get("rva").getAsString());
            }
        }
        Set<Function> out = new LinkedHashSet<>();
        roles.clear();
        for (String rva : direct) {
            Function f = functionAt(rva);
            if (f != null && out.add(f)) roles.put(f, "named");
        }
        List<Function> seeds = new ArrayList<>(out);
        for (Function f : seeds) {
            int n = 0;
            for (Reference ref : annotated.getReferenceManager().getReferencesTo(f.getEntryPoint())) {
                if (n >= callersPer || out.size() >= max) break;
                if (!ref.getReferenceType().isCall()) continue;
                Function c = annotated.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                if (c != null && !c.equals(f) && out.add(c)) {
                    n++;
                    roles.put(c, "caller");
                }
            }
        }
        Set<String> structs = new LinkedHashSet<>();
        if (delta.has("annotations")) {
            for (JsonElement e : delta.getAsJsonArray("annotations")) {
                JsonObject o = e.getAsJsonObject();
                String kind = o.get("kind").getAsString();
                if (kind.equals("struct") || kind.equals("fields") || kind.equals("union") || kind.equals("enum")) structs.add(o.get("name").getAsString());
            }
        }
        for (JsonElement e : arr(revert, "fields")) structs.add(e.getAsJsonObject().get("name").getAsString());
        for (JsonElement e : arr(revert, "renames")) structs.add(e.getAsJsonArray().get(0).getAsString()); // looked up before the rename
        if (!structs.isEmpty()) {
            int found = 0;
            for (Function f : annotated.getFunctionManager().getFunctions(true)) {
                if (found >= 3 || out.size() >= max) break;
                boolean uses = false;
                for (Parameter p : f.getParameters()) {
                    DataType dt = unwrap(p.getDataType());
                    if (dt != null && structs.contains(dt.getName())) { uses = true; break; }
                }
                if (uses && out.add(f)) {
                    found++;
                    roles.put(f, "user");
                }
            }
        }
        List<Function> list = new ArrayList<>(out);
        return list.size() > max ? list.subList(0, max) : list;
    }

    private JsonObject texts(List<Function> fs) throws CancelledException {
        JsonObject o = new JsonObject();
        for (Function f : fs) {
            // A short limit: the functions looked at after a change are context, and a huge one (GenericSimCall, 52 KB) can
            // take many minutes (a decompiler that loops cannot be interrupted, so the limit also bounds the damage).
            o.addProperty(String.format("0x%x", f.getEntryPoint().subtract(annotated.getImageBase())),
                          decompileText(annotated, annDecomp, f, false, 45));
        }
        return o;
    }

    // ---------------------------------------------------------------- revert: undo facts that were deleted or replaced
    // (tools/ghidra_ledger.py facts_changes). A function goes back to what the analysed (raw) program has: name, namespace,
    // signature, flags, comment, local variables; the facts that still hold for it are then applied again on top.

    private static JsonArray arr(JsonObject o, String k) {
        return o != null && o.has(k) ? o.getAsJsonArray(k) : new JsonArray();
    }

    private DataType typeNamed(String name) {
        return annotated.getDataTypeManager().getDataType(ghidra.program.model.data.CategoryPath.ROOT, name);
    }

    private ghidra.program.model.symbol.Namespace namespaceLike(ghidra.program.model.symbol.Namespace rawNs) throws Exception {
        if (rawNs == null || rawNs.isGlobal()) return annotated.getGlobalNamespace();
        ghidra.program.model.symbol.Namespace parent = namespaceLike(rawNs.getParentNamespace());
        ghidra.program.model.symbol.SymbolTable st = annotated.getSymbolTable();
        ghidra.program.model.symbol.Namespace ns = st.getNamespace(rawNs.getName(), parent);
        return ns != null ? ns : st.createNameSpace(parent, rawNs.getName(), ghidra.program.model.symbol.SourceType.ANALYSIS);
    }

    private DataType resolve(DataType dt) {
        return annotated.getDataTypeManager().resolve(dt, null);
    }

    private VariableStorage storageLike(VariableStorage s) throws Exception {
        if (s.isUnassignedStorage() || s.isVoidStorage()) return s.isVoidStorage() ? VariableStorage.VOID_STORAGE : VariableStorage.UNASSIGNED_STORAGE;
        return new VariableStorage(annotated, s.getVarnodes());
    }

    private void revertFunction(long rva, List<String> log) throws Exception {
        Address base = annotated.getImageBase();
        Function fn = annotated.getFunctionManager().getFunctionAt(base.add(rva));
        if (fn == null) return;
        Function r = raw.getFunctionManager().getFunctionAt(raw.getImageBase().add(rva));
        for (ghidra.program.model.listing.Variable v : fn.getLocalVariables()) fn.removeVariable(v);
        if (r == null) { // a function ApplyDb created (db/missed-functions.jsonl): back to a default name and signature
            fn.getSymbol().setNameAndNamespace("", annotated.getGlobalNamespace(), ghidra.program.model.symbol.SourceType.DEFAULT);
            fn.setCustomVariableStorage(false);
            fn.updateFunction(null, new ghidra.program.model.listing.ReturnParameterImpl(ghidra.program.model.data.Undefined4DataType.dataType, annotated),
                              new ArrayList<Parameter>(), Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
                              ghidra.program.model.symbol.SourceType.DEFAULT);
            fn.setComment(null);
            fn.setInline(false);
            fn.setNoReturn(false);
            return;
        }
        ghidra.program.model.symbol.SourceType src = r.getSymbol().getSource();
        fn.getSymbol().setNameAndNamespace(r.getName(), namespaceLike(r.getParentNamespace()), src);
        fn.setCustomVariableStorage(r.hasCustomVariableStorage());
        List<Parameter> ps = new ArrayList<>();
        for (Parameter p : r.getParameters()) {
            ps.add(r.hasCustomVariableStorage()
                   ? new ghidra.program.model.listing.ParameterImpl(p.getName(), resolve(p.getDataType()), storageLike(p.getVariableStorage()), annotated, p.getSource())
                   : new ghidra.program.model.listing.ParameterImpl(p.getName(), resolve(p.getDataType()), annotated, p.getSource()));
        }
        Parameter ret = r.getReturn();
        fn.updateFunction(r.getCallingConventionName(),
                          r.hasCustomVariableStorage()
                              ? new ghidra.program.model.listing.ReturnParameterImpl(resolve(ret.getDataType()), storageLike(ret.getVariableStorage()), annotated)
                              : new ghidra.program.model.listing.ReturnParameterImpl(resolve(ret.getDataType()), annotated),
                          ps, r.hasCustomVariableStorage() ? Function.FunctionUpdateType.CUSTOM_STORAGE : Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                          true, r.getSignatureSource());
        fn.setVarArgs(r.hasVarArgs());
        fn.setNoReturn(r.hasNoReturn()); // (calls un-cut by ApplyDb.returnAgain stay un-cut until the next build)
        fn.setCallFixup(r.getCallFixup());
        fn.setInline(r.isInline());
        fn.setComment(r.getComment());
        Function thunked = r.getThunkedFunction(false);
        fn.setThunkedFunction(thunked == null ? null : annotated.getFunctionManager().getFunctionAt(thunked.getEntryPoint()));
        for (ghidra.program.model.listing.Variable v : r.getLocalVariables()) {
            try {
                fn.addLocalVariable(new ghidra.program.model.listing.LocalVariableImpl(v.getName(), v.getFirstUseOffset(), resolve(v.getDataType()),
                                    storageLike(v.getVariableStorage()), annotated), v.getSource());
            } catch (Exception e) {
                log.add("revert: local " + v.getName() + " of 0x" + Long.toHexString(rva) + ": " + e.getMessage());
            }
        }
    }

    private void revertLocal(JsonObject l, List<String> log) throws Exception {
        Address base = annotated.getImageBase();
        Function fn = annotated.getFunctionManager().getFunctionContaining(base.add(Long.decode(l.get("rva").getAsString())));
        if (fn == null) return;
        for (ghidra.program.model.listing.Variable v : fn.getLocalVariables()) {
            boolean hit = l.has("offset") ? v.isStackVariable() && v.getStackOffset() == l.get("offset").getAsInt()
                        : l.has("at") && v.getFirstUseOffset() != 0
                          && fn.getEntryPoint().add(v.getFirstUseOffset()).subtract(base) == Long.decode(l.get("at").getAsString());
            if (hit) fn.removeVariable(v);
        }
    }

    private void revertGlobal(long rva) throws Exception {
        Address a = raw.getImageBase().add(rva);
        Address b = annotated.getImageBase().add(rva);
        ghidra.program.model.symbol.SymbolTable st = annotated.getSymbolTable();
        for (ghidra.program.model.symbol.Symbol s : st.getSymbols(b)) {
            if (s.getSource() == ghidra.program.model.symbol.SourceType.USER_DEFINED && s.getSymbolType() == ghidra.program.model.symbol.SymbolType.LABEL) s.delete();
        }
        ghidra.program.model.listing.Data was = annotated.getListing().getDefinedDataAt(b);
        ghidra.program.model.listing.Data orig = raw.getListing().getDefinedDataAt(a);
        if (was != null) annotated.getListing().clearCodeUnits(b, was.getMaxAddress(), false);
        if (orig != null) annotated.getListing().createData(b, resolve(orig.getDataType()), orig.getLength());
    }

    private String revert(JsonObject rv) throws Exception {
        List<String> log = new ArrayList<>();
        if (rv == null) return "";
        ghidra.program.model.data.DataTypeManager dtm = annotated.getDataTypeManager();
        for (JsonElement e : arr(rv, "renames")) {
            JsonArray r = e.getAsJsonArray();
            DataType dt = typeNamed(r.get(0).getAsString());
            if (dt != null) dt.setName(r.get(1).getAsString());
            else log.add("revert: no type " + r.get(0).getAsString() + " to rename");
        }
        for (JsonElement e : arr(rv, "functions")) revertFunction(Long.decode(e.getAsString()), log);
        for (JsonElement e : arr(rv, "locals")) revertLocal(e.getAsJsonObject(), log);
        for (JsonElement e : arr(rv, "globals")) revertGlobal(Long.decode(e.getAsString()));
        Address base = annotated.getImageBase();
        for (JsonElement e : arr(rv, "callsites")) {
            Address a = base.add(Long.decode(e.getAsString()));
            for (ghidra.program.model.symbol.Symbol s : annotated.getSymbolTable().getSymbols(a)) {
                if (s.getParentNamespace().getName().equals("override")) s.delete(); // HighFunctionDBUtil.writeOverride's symbol
            }
        }
        for (JsonElement e : arr(rv, "equates")) {
            JsonObject q = e.getAsJsonObject();
            Address a = base.add(Long.decode(q.get("rva").getAsString()));
            long value = q.get("value").getAsLong();
            for (ghidra.program.model.symbol.Equate eq : annotated.getEquateTable().getEquates(a)) {
                if (eq.getValue() != value) continue;
                if (q.has("operand")) eq.removeReference(a, q.get("operand").getAsInt());
                else for (ghidra.program.model.symbol.EquateReference ref : eq.getReferences(a)) eq.removeReference(a, ref.getOpIndex());
                if (eq.getReferenceCount() == 0) annotated.getEquateTable().removeEquate(eq.getName());
            }
        }
        for (JsonElement e : arr(rv, "fields")) {
            JsonObject f = e.getAsJsonObject();
            if (typeNamed(f.get("name").getAsString()) instanceof ghidra.program.model.data.Structure s) s.clearAtOffset(f.get("off").getAsInt());
        }
        for (JsonElement e : arr(rv, "types")) {
            DataType dt = typeNamed(e.getAsString());
            if (dt != null) dtm.remove(dt);
        }
        StringBuilder sb = new StringBuilder();
        for (String l : log) sb.append("DecompServer: ").append(l).append('\n');
        return sb.toString();
    }

    private JsonObject annotate(JsonObject q) throws Exception {
        JsonObject o = new JsonObject();
        String overlay = q.get("overlay").getAsString();
        boolean commit = !q.has("commit") || q.get("commit").getAsBoolean();
        JsonObject newLedger = q.has("ledger") ? q.getAsJsonObject("ledger") : null;
        if (commit && newLedger != null && q.has("base_version") && q.get("base_version").getAsLong() != ledgerVersion) {
            throw new IllegalStateException("the ledger changed since you read it (version " + ledgerVersion + ", you had "
                                            + q.get("base_version").getAsLong() + "); run the command again");
        }
        JsonObject delta = JsonParser.parseString(Files.readString(new File(overlay).toPath(), StandardCharsets.UTF_8)).getAsJsonObject();
        JsonObject rv = q.has("revert") ? q.getAsJsonObject("revert") : null;
        List<Function> targets = affected(delta, rv, q.has("watch") ? q.getAsJsonArray("watch") : null,
                                          q.has("callers") ? q.get("callers").getAsInt() : 3, q.has("max") ? q.get("max").getAsInt() : 8);
        JsonObject before = texts(targets);
        boolean ok = false;
        String log = "";
        int tx = annotated.startTransaction("annotate");
        try {
            long t0 = System.currentTimeMillis();
            log = revert(rv) + apply(overlay);
            ok = !log.contains("ApplyDb: unapplied ");
            if (ok && commit && newLedger != null) {
                setLedger(newLedger);
                storeLedgerInOptions();
            }
            o.addProperty("apply_ms", System.currentTimeMillis() - t0);
        } finally {
            annotated.endTransaction(tx, ok && commit); // all or nothing: any unapplied fact rolls the whole request back
        }
        if (ok && commit) { // saved before the (possibly slow) decompiles below, so a failure there cannot lose the commit
            long t0 = System.currentTimeMillis();
            annotated.getDomainFile().save(monitor);
            annotated.clearUndo();
            writeMirror();
            o.addProperty("save_ms", System.currentTimeMillis() - t0);
        } else {
            annDecomp.flushCache();
        }
        JsonObject after = new JsonObject();
        if (ok) {
            try {
                after = texts(targets);
            } catch (Exception e) {
                o.addProperty("after_error", String.valueOf(e));
            }
        }
        o.addProperty("applied", ok);
        o.addProperty("committed", ok && commit);
        o.addProperty("version", ledgerVersion);
        o.addProperty("log", log);
        o.add("before", before);
        o.add("after", after);
        JsonObject r = new JsonObject(); // why each function is shown: named by the change, a caller, or a user of a changed type
        for (Map.Entry<Function, String> e : roles.entrySet())
            r.addProperty(String.format("0x%x", e.getKey().getEntryPoint().subtract(annotated.getImageBase())), e.getValue());
        o.add("roles", r);
        return o;
    }

    // The decompiler's view of a function's variables, so a lane can name a local by what the pseudo-code calls it.
    private JsonObject vars(String rva) throws Exception {
        Function f = functionAt(rva);
        if (f == null) throw new IllegalArgumentException("no function at " + rva);
        checkCancelled();
        annDecomp.flushCache();
        DecompileResults res = annDecomp.decompileFunction(f, (int) Math.max(120, f.getBody().getNumAddresses() / 70), reqMon);
        checkCancelled();
        if (!res.decompileCompleted()) throw new IllegalStateException("decompile failed: " + res.getErrorMessage());
        Address base = annotated.getImageBase();
        JsonArray out = new JsonArray();
        java.util.Iterator<HighSymbol> it = res.getHighFunction().getLocalSymbolMap().getSymbols();
        while (it.hasNext()) {
            HighSymbol s = it.next();
            JsonObject o = new JsonObject();
            o.addProperty("name", s.getName());
            o.addProperty("type", s.getDataType() == null ? "?" : s.getDataType().getDisplayName());
            o.addProperty("size", s.getSize());
            o.addProperty("param", s.isParameter());
            VariableStorage st = s.getStorage();
            o.addProperty("storage", st.toString());
            if (st.isStackStorage()) o.addProperty("offset", st.getStackOffset());
            Address pc = s.getPCAddress();
            if (pc != null && pc.isMemoryAddress()) o.addProperty("at", String.format("0x%x", pc.subtract(base)));
            out.add(o);
        }
        JsonObject o = new JsonObject();
        o.addProperty("rva", String.format("0x%x", f.getEntryPoint().subtract(base)));
        o.addProperty("function", f.getName(true));
        o.add("vars", out);
        return o;
    }

    private JsonObject snapshot(File zip) throws Exception {
        underLocal(zip.getParentFile());
        annotated.getDomainFile().save(monitor);
        zip.getParentFile().mkdirs();
        int files = 0;
        try (ZipOutputStream zos = new ZipOutputStream(Files.newOutputStream(zip.toPath()))) {
            zos.setLevel(Deflater.BEST_SPEED);
            java.util.ArrayDeque<File> stack = new java.util.ArrayDeque<>();
            stack.push(annDir);
            byte[] buf = new byte[1 << 16];
            while (!stack.isEmpty()) {
                File d = stack.pop();
                File[] kids = d.listFiles();
                if (kids == null) continue;
                for (File k : kids) {
                    if (k.isDirectory()) { stack.push(k); continue; }
                    if (k.getName().contains(".lock")) continue; // Ghidra's lock files
                    zos.putNextEntry(new ZipEntry(annDir.toPath().relativize(k.toPath()).toString().replace('\\', '/')));
                    try (FileInputStream in = new FileInputStream(k)) {
                        int n;
                        while ((n = in.read(buf)) > 0) zos.write(buf, 0, n);
                    }
                    zos.closeEntry();
                    files++;
                }
            }
        }
        JsonObject o = new JsonObject();
        o.addProperty("zip", zip.getPath());
        o.addProperty("files", files);
        o.addProperty("bytes", zip.length());
        return o;
    }

    private JsonObject handle(String path, JsonObject q) throws Exception {
        switch (path) {
            case "/decompile": {
                File custom = q.has("out") ? underLocal(new File(q.get("out").getAsString())) : null;
                if (q.has("raw") && q.get("raw").getAsBoolean()) {
                    return decompile(raw, rawDecomp, q, custom != null ? custom : rawOut);
                }
                if (!q.has("overlay")) {
                    return decompile(annotated, annDecomp, q, custom != null ? custom : annOut);
                }
                int tx = annotated.startTransaction("overlay");
                try {
                    String log = apply(q.get("overlay").getAsString());
                    JsonObject o = decompile(annotated, annDecomp, q, custom != null ? custom : overlayOut);
                    o.addProperty("log", log);
                    return o;
                } finally {
                    annotated.endTransaction(tx, false); // roll back: nothing of the overlay persists
                    annDecomp.flushCache();
                }
            }
            case "/annotate":
                return annotate(q);
            case "/ledger": {
                JsonObject o = new JsonObject();
                o.addProperty("version", ledgerVersion);
                o.add("files", ledgerFiles);
                return o;
            }
            case "/snapshot":
                return snapshot(new File(q.get("zip").getAsString()));
            case "/reload": {
                if (q.has("facts")) factsPath = q.get("facts").getAsString();
                build(factsPath, q.get("ledger").getAsString());
                JsonObject o = new JsonObject();
                o.addProperty("log", lastApply);
                o.addProperty("apply_ms", applyMs);
                return o;
            }
            case "/vars":
                return vars(q.get("rva").getAsString());
            default:
                throw new IllegalArgumentException("unknown request " + path);
        }
    }

    private static boolean isControl(String path) {
        return path.equals("/status") || path.equals("/cancel") || path.equals("/shutdown");
    }

    // Answered at once on the HTTP thread, without waiting for the running work request.
    private JsonObject control(String path, JsonObject q) throws Exception {
        switch (path) {
            case "/status": {
                JsonObject o = new JsonObject();
                Runtime rt = Runtime.getRuntime();
                o.addProperty("pid", ProcessHandle.current().pid());
                o.addProperty("uptime_s", (System.currentTimeMillis() - started) / 1000);
                o.addProperty("requests", requests);
                o.addProperty("facts", factsPath);
                o.addProperty("annotated", cache);
                o.addProperty("apply_ms", applyMs);
                o.addProperty("ledger_version", ledgerVersion);
                o.addProperty("heap_used_mb", (rt.totalMemory() - rt.freeMemory()) >> 20);
                o.addProperty("heap_max_mb", rt.maxMemory() >> 20);
                Running r = current;
                if (r != null) o.add("current", r.json());
                o.addProperty("queued", queued.get());
                return o;
            }
            case "/cancel": {
                JsonObject o = new JsonObject();
                Running r = current;
                o.addProperty("queued", queued.get());
                if (r == null) {
                    o.addProperty("cancelled", false);
                    return o;
                }
                o.add("request", r.json());
                if (q.has("id") && q.get("id").getAsInt() != r.id) {
                    o.addProperty("cancelled", false);
                    o.addProperty("reason", "the running request is " + r.id + ", not " + q.get("id").getAsInt() + " (that one has finished)");
                    return o;
                }
                if (!r.cancellable) {
                    o.addProperty("cancelled", false);
                    o.addProperty("reason", r.path + " cannot be cancelled (it would leave the program half-built or half-saved)");
                    return o;
                }
                long t0 = System.nanoTime();
                r.mon.cancel(); // DecompInterface's listener kills the decompiler process
                while (current == r && System.nanoTime() - t0 < 30_000_000_000L) Thread.sleep(20);
                o.addProperty("cancelled", true);
                o.addProperty("stopped", current != r);
                o.addProperty("stop_ms", (System.nanoTime() - t0) / 1_000_000);
                return o;
            }
            default: { // /shutdown: stop the running request too, so the server can close
                Running r = current;
                if (r != null && r.cancellable) r.mon.cancel();
                done.countDown();
                return new JsonObject();
            }
        }
    }

    private static String what(JsonObject q) {
        if (q.has("rvas")) return q.get("rvas").toString();
        if (q.has("rva")) return q.get("rva").getAsString();
        if (q.has("watch")) return "annotate, watch " + q.get("watch").toString();
        return "";
    }

    private JsonObject work(String path, JsonObject q) throws Exception {
        queued.incrementAndGet();
        work.lock();
        queued.decrementAndGet();
        // A reload or snapshot is never cancelled midway: it would leave the annotated program half-built or half-saved.
        Running r = new Running(requestIds.incrementAndGet(), path, what(q), !path.equals("/reload") && !path.equals("/snapshot"));
        try {
            current = r;
            reqMon = r.mon;
            requests++;
            return handle(path, q);
        } catch (Exception e) {
            if (r.mon.isCancelled() && !(e instanceof CancelledException)) throw new CancelledException(); // however it surfaced
            throw e;
        } finally {
            reqMon = TaskMonitor.DUMMY;
            current = null;
            work.unlock();
        }
    }

    private void serve(HttpExchange ex) {
        JsonObject reply;
        int code = 200;
        try {
            String body = new String(ex.getRequestBody().readAllBytes(), StandardCharsets.UTF_8);
            JsonObject q = body.isBlank() ? new JsonObject() : JsonParser.parseString(body).getAsJsonObject();
            long t0 = System.nanoTime();
            String path = ex.getRequestURI().getPath();
            reply = isControl(path) ? control(path, q) : work(path, q);
            reply.addProperty("ms", (System.nanoTime() - t0) / 1_000_000);
        } catch (CancelledException e) {
            code = 500;
            reply = new JsonObject();
            reply.addProperty("error", "cancelled: the request was stopped by tools/ghidra.py cancel; nothing it was doing was kept");
            reply.addProperty("cancelled", true);
        } catch (Throwable e) {
            code = 500;
            reply = new JsonObject();
            StringWriter sw = new StringWriter();
            e.printStackTrace(new PrintWriter(sw));
            reply.addProperty("error", sw.toString());
        }
        try {
            byte[] out = reply.toString().getBytes(StandardCharsets.UTF_8);
            ex.getResponseHeaders().add("Content-Type", "application/json");
            ex.sendResponseHeaders(code, out.length);
            ex.getResponseBody().write(out);
        } catch (Exception e) {
            // the client went away
        } finally {
            ex.close();
        }
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        factsPath = args[0];
        File stateFile = new File(args[1]);
        rawOut = new File(args[2]);
        annOut = new File(args[3]);
        overlayOut = new File(args[4]);
        annDir = new File(args[5]);
        localRoot = new File(args[8]);
        raw = currentProgram;
        rawDecomp = open(raw);
        if (args[6].equals("open")) openCached();
        else build(factsPath, args[7]);

        HttpServer http = HttpServer.create(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0), 16);
        http.createContext("/", this::serve);
        // A thread per connection: control requests are answered at once; work requests queue on `work` (see work()).
        http.setExecutor(Executors.newCachedThreadPool(r -> {
            Thread t = new Thread(r, "DecompServer-http");
            t.setDaemon(true);
            return t;
        }));
        http.start();
        JsonObject st = new JsonObject();
        st.addProperty("pid", ProcessHandle.current().pid());
        st.addProperty("port", http.getAddress().getPort());
        Files.writeString(stateFile.toPath(), st.toString(), StandardCharsets.UTF_8);
        println("DecompServer: listening on 127.0.0.1:" + http.getAddress().getPort());

        done.await();
        http.stop(1);
        if (!work.tryLock(60, TimeUnit.SECONDS)) println("DecompServer: a request is still running after 60 s; closing anyway");
        rawDecomp.dispose();
        closeAnnotated();
        stateFile.delete();
        println("DecompServer: stopped");
    }
}
