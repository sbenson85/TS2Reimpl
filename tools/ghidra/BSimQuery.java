// Queries local H2 BSim databases with the current program's functions (the game) and writes the
// nearest library functions as JSON lines. Run by tools/bsim.py query, headless and read-only:
//   analyzeHeadless <proj> <name> -process -noanalysis -readOnly -postScript BSimQuery.java
//       <out.jsonl> <minSize> <maxMatches> <threshold> <db1.mv.db> [<db2.mv.db> ...] [RVAS <file>]
// One line per game function with at least one match:
//   {"rva": "0x...", "size": N, "self": selfSignificance,
//    "matches": [{"db": "ucrt", "exe": "<library>/<object>", "name": "...", "sim": 0.97, "sig": 41.2}, ...]}
// Functions are signed and queried in batches, so progress shows in the log and a failure loses one batch.
//@category TS2Reimpl

import ghidra.app.script.GhidraScript;
import ghidra.features.bsim.query.BSimClientFactory;
import ghidra.features.bsim.query.BSimServerInfo;
import ghidra.features.bsim.query.FunctionDatabase;
import ghidra.features.bsim.query.GenSignatures;
import ghidra.features.bsim.query.description.DescriptionManager;
import ghidra.features.bsim.query.description.FunctionDescription;
import ghidra.features.bsim.query.description.SignatureRecord;
import ghidra.features.bsim.query.protocol.QueryNearest;
import ghidra.features.bsim.query.protocol.ResponseNearest;
import ghidra.features.bsim.query.protocol.SimilarityNote;
import ghidra.features.bsim.query.protocol.SimilarityResult;
import ghidra.program.model.listing.Function;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class BSimQuery extends GhidraScript {

    private static final int BATCH = 1000;

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
        File out = new File(args[0]);
        long minSize = Long.parseLong(args[1]);
        int maxMatches = Integer.parseInt(args[2]);
        double thresh = Double.parseDouble(args[3]);
        List<String> dbs = new ArrayList<>();
        // "RVAS <file>": query just these (hex RVAs, one per line). (Headless mangles "key=value" script arguments.)
        java.util.Set<Long> only = null;
        for (int i = 4; i < args.length; i++) {
            if (args[i].equals("RVAS") && i + 1 < args.length) {
                only = new java.util.HashSet<>();
                for (String l : java.nio.file.Files.readAllLines(new File(args[++i]).toPath())) {
                    if (!l.isBlank()) only.add(Long.decode(l.trim()));
                }
            } else {
                dbs.add(args[i]);
            }
        }
        long base = currentProgram.getImageBase().getOffset();

        List<Function> all = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isThunk() || f.isExternal()) continue;
            if (f.getBody().getNumAddresses() < minSize) continue;
            if (only != null && !only.contains(f.getEntryPoint().getOffset() - base)) continue;
            all.add(f);
        }
        println("BSimQuery: " + all.size() + " functions of at least " + minSize + " bytes");

        Map<String, FunctionDatabase> clients = new LinkedHashMap<>();
        try (PrintWriter w = new PrintWriter(out, StandardCharsets.UTF_8)) {
            for (String db : dbs) {
                FunctionDatabase c = BSimClientFactory.buildClient(new BSimServerInfo(db), false);
                if (!c.initialize()) throw new Exception("BSimQuery: " + db + ": " + c.getLastError().message);
                String label = new File(db).getName().replace(".mv.db", "");
                clients.put(label, c);
            }
            // All databases share a template (tools/bsim.py creates them alike), so one vector factory signs for all.
            FunctionDatabase first = clients.values().iterator().next();
            for (int start = 0; start < all.size(); start += BATCH) {
                if (monitor.isCancelled()) break;
                List<Function> batch = all.subList(start, Math.min(all.size(), start + BATCH));
                GenSignatures gensig = new GenSignatures(false);
                try {
                    gensig.setVectorFactory(first.getLSHVectorFactory());
                    gensig.openProgram(currentProgram, null, null, null, null, null);
                    gensig.scanFunctions(batch.iterator(), batch.size(), monitor);
                    DescriptionManager manager = gensig.getDescriptionManager();
                    // rva -> json fragments, per database
                    Map<Long, List<String>> hits = new TreeMap<>();
                    Map<Long, Double> self = new TreeMap<>();
                    for (Map.Entry<String, FunctionDatabase> e : clients.entrySet()) {
                        QueryNearest query = new QueryNearest();
                        query.manage = manager;
                        query.max = maxMatches;
                        query.thresh = thresh;
                        query.signifthresh = 0.0;
                        ResponseNearest resp = query.execute(e.getValue());
                        if (resp == null) {
                            println("BSimQuery: " + e.getKey() + ": " + e.getValue().getLastError().message);
                            continue;
                        }
                        for (SimilarityResult sim : resp.result) {
                            FunctionDescription qf = sim.getBase();
                            long rva = qf.getAddress() - base;
                            SignatureRecord sr = qf.getSignatureRecord();
                            if (sr != null) {
                                self.put(rva, e.getValue().getLSHVectorFactory().getSelfSignificance(sr.getLSHVector()));
                            }
                            Iterator<SimilarityNote> it = sim.iterator();
                            while (it.hasNext()) {
                                SimilarityNote n = it.next();
                                FunctionDescription lf = n.getFunctionDescription();
                                hits.computeIfAbsent(rva, k -> new ArrayList<>()).add(String.format(
                                    "{\"db\":%s,\"exe\":%s,\"name\":%s,\"sim\":%.4f,\"sig\":%.2f}",
                                    q(e.getKey()), q(lf.getExecutableRecord().getNameExec()),
                                    q(lf.getFunctionName()), n.getSimilarity(), n.getSignificance()));
                            }
                        }
                    }
                    Map<Long, Long> sizes = new TreeMap<>();
                    for (Function f : batch) {
                        sizes.put(f.getEntryPoint().getOffset() - base, f.getBody().getNumAddresses());
                    }
                    for (Map.Entry<Long, List<String>> h : hits.entrySet()) {
                        w.println(String.format("{\"rva\":\"0x%x\",\"size\":%d,\"self\":%.2f,\"matches\":[%s]}",
                            h.getKey(), sizes.getOrDefault(h.getKey(), 0L), self.getOrDefault(h.getKey(), 0.0),
                            String.join(",", h.getValue())));
                    }
                    w.flush();
                    println("BSimQuery: " + Math.min(all.size(), start + BATCH) + "/" + all.size() + " done, "
                        + hits.size() + " with matches in this batch");
                } finally {
                    gensig.dispose();
                }
            }
        } finally {
            for (FunctionDatabase c : clients.values()) c.close();
        }
    }
}
