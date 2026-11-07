// Applies tools/annotate.py's facts (from db/) to the annotated copy of the project (pilot; run by
// tools/ghidra-annotate.ps1, never on local/ghidra itself): class and vtable structures, the structures
// and signatures in db/annotations.jsonl, function names and class membership, inline accessors; and the other
// db/annotations.jsonl kinds: enums, unions, equates, call-site signatures, noreturn/thunk flags, locals (stack offset,
// or a decompiler variable by first use / name, renamed through the decompiler's symbol).
// Tentative fields and names get a t_ prefix, so a guess never reads as a fact.
// Usage: analyzeHeadless <proj> <name> -process -noanalysis -postScript ApplyDb.java <facts.json>
//@category TS2Reimpl

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.lang.InjectPayload;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.listing.ParameterImpl;
import ghidra.program.model.listing.ReturnParameterImpl;
import ghidra.program.model.listing.VariableStorage;
import ghidra.program.model.lang.Register;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.listing.GhidraClass;
import ghidra.app.util.NamespaceUtils;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class ApplyDb extends GhidraScript {
    private DataTypeManager dtm;
    private final Map<String, Structure> structs = new HashMap<>();
    // Facts that could not be applied (no function at the address, a field that didn't fit, a bad signature): printed at
    // the end as "ApplyDb: unapplied ..." lines, so nothing in db/ is dropped silently.
    private final List<String> unapplied = new ArrayList<>();

    private DataType type(String t) {
        if (t.endsWith("]")) {
            int open = t.lastIndexOf('[');
            DataType element = type(t.substring(0, open));
            int n = Integer.parseInt(t.substring(open + 1, t.length() - 1));
            return new ArrayDataType(element, n, element.getLength(), dtm);
        }
        if (t.endsWith("*")) {
            DataType inner = type(t.substring(0, t.length() - 1));
            return new PointerDataType(inner, dtm);
        }
        switch (t) {
            case "void": return VoidDataType.dataType;
            case "i16": return ShortDataType.dataType;
            case "u16": return UnsignedShortDataType.dataType;
            case "u8": return ByteDataType.dataType;
            case "i8": return SignedByteDataType.dataType;
            case "u32": return UnsignedIntegerDataType.dataType;
            case "i32": return IntegerDataType.dataType;
            case "f32": return FloatDataType.dataType;
            case "f64": return DoubleDataType.dataType;
            case "i64": return LongLongDataType.dataType;
            case "u64": return UnsignedLongLongDataType.dataType;
            case "bool": return BooleanDataType.dataType;
            case "char": return CharDataType.dataType;
            case "wchar": return WideChar16DataType.dataType;
            default:
                Structure s = structs.get(t);
                if (s != null) return s;
                DataType existing = dtm.getDataType(CategoryPath.ROOT, t); // a structure, union or enum the program has
                if (existing instanceof Structure || existing instanceof Union || existing instanceof ghidra.program.model.data.Enum)
                    return existing;
                throw new IllegalArgumentException("unknown type " + t);
        }
    }

    // An enum (annotations kind "enum": {"name", "size"?, "values": {"NAME": n}}), replacing one of the same name.
    private void applyEnum(JsonObject a) {
        String name = a.get("name").getAsString();
        int size = a.has("size") ? a.get("size").getAsInt() : 4;
        EnumDataType e = new EnumDataType(CategoryPath.ROOT, name, size, dtm);
        for (Map.Entry<String, JsonElement> v : a.getAsJsonObject("values").entrySet()) e.add(v.getKey(), v.getValue().getAsLong());
        dtm.addDataType(e, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    // A union (annotations kind "union": {"name", "fields": [{"type", "name"}]}): created empty first (other types may
    // name it), filled after the structures; its members are rewritten whole.
    private Union union(String name) {
        DataType existing = dtm.getDataType(CategoryPath.ROOT, name);
        if (existing instanceof Union u) return u;
        return (Union) dtm.addDataType(new UnionDataType(CategoryPath.ROOT, name, dtm), DataTypeConflictHandler.REPLACE_HANDLER);
    }

    private void fillUnion(JsonObject a) {
        Union u = union(a.get("name").getAsString());
        while (u.getNumComponents() > 0) u.delete(0);
        for (JsonElement fe : a.getAsJsonArray("fields")) {
            JsonObject f = fe.getAsJsonObject();
            try {
                DataType dt = type(f.get("type").getAsString());
                u.add(dt, dt.getLength(), fieldName(f), null);
            } catch (Exception e) {
                unapplied.add("union member " + u.getName() + "." + fieldName(f) + ": " + e.getMessage());
            }
        }
    }

    // __thiscall with `this` (in ECX) typed as the given pointer type and the other parameters on the stack (custom storage).
    private void setThiscall(Function fn, String self, DataType ret, JsonArray ps) throws Exception {
        List<Parameter> params = new ArrayList<>();
        params.add(new ParameterImpl("this", type(self), new VariableStorage(currentProgram, currentProgram.getRegister("ECX")), currentProgram));
        int stack = 4;
        for (JsonElement pe : ps) {
            JsonObject p = pe.getAsJsonObject();
            DataType dt = type(p.get("type").getAsString());
            int size = Math.max(4, (dt.getLength() + 3) & ~3); // a small argument still takes a whole stack slot
            params.add(new ParameterImpl(p.get("name").getAsString(), dt, new VariableStorage(currentProgram, stack, dt.getLength()), currentProgram));
            stack += size;
        }
        fn.setCustomVariableStorage(true);
        fn.updateFunction("__thiscall", new ReturnParameterImpl(ret, returnStorage(ret), currentProgram), params,
                          FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
    }

    private FunctionDefinitionDataType signature(String name, JsonObject a) {
        FunctionDefinitionDataType fd = new FunctionDefinitionDataType(CategoryPath.ROOT, name, dtm);
        try {
            fd.setCallingConvention(a.get("cc").getAsString());
        } catch (Exception e) {
            throw new IllegalArgumentException("calling convention " + a.get("cc").getAsString() + ": " + e.getMessage());
        }
        fd.setReturnType(type(a.get("ret").getAsString()));
        JsonArray ps = a.getAsJsonArray("params");
        ParameterDefinition[] defs = new ParameterDefinition[ps.size()];
        for (int k = 0; k < ps.size(); k++) {
            JsonObject p = ps.get(k).getAsJsonObject();
            defs[k] = new ParameterDefinitionImpl(p.get("name").getAsString(), type(p.get("type").getAsString()), null);
        }
        fd.setArguments(defs);
        if (a.has("varargs") && a.get("varargs").getAsBoolean()) fd.setVarArgs(true);
        return fd;
    }

    // An equate on a scalar operand (annotations kind "equate": {"rva" of the instruction, "value", "name" or "enum",
    // "operand"?}): the decompiler shows the name instead of the number. With "enum", the name is the member with that value.
    private void applyEquate(JsonObject a, Address base) {
        String rva = a.get("rva").getAsString();
        long value = a.get("value").getAsLong();
        Address at = base.add(Long.decode(rva));
        ghidra.program.model.listing.Instruction ins = currentProgram.getListing().getInstructionAt(at);
        if (ins == null) { unapplied.add("equate " + rva + ": no instruction there"); return; }
        String name;
        if (a.has("name")) name = a.get("name").getAsString();
        else {
            DataType dt = dtm.getDataType(CategoryPath.ROOT, a.get("enum").getAsString());
            if (!(dt instanceof ghidra.program.model.data.Enum en)) { unapplied.add("equate " + rva + ": unknown enum " + a.get("enum").getAsString()); return; }
            name = en.getName(value);
            if (name == null) { unapplied.add("equate " + rva + ": enum " + en.getName() + " has no value " + value); return; }
        }
        int op = -1;
        if (a.has("operand")) op = a.get("operand").getAsInt();
        else {
            for (int i = 0; i < ins.getNumOperands() && op < 0; i++) {
                for (Object o : ins.getOpObjects(i)) {
                    if (o instanceof ghidra.program.model.scalar.Scalar s && (s.getValue() == value || s.getUnsignedValue() == value)) { op = i; break; }
                }
            }
            if (op < 0) { unapplied.add("equate " + rva + ": no operand with the value " + value + " in " + ins); return; }
        }
        try {
            ghidra.program.model.symbol.EquateTable et = currentProgram.getEquateTable();
            ghidra.program.model.symbol.Equate eq = et.getEquate(name);
            if (eq == null) eq = et.createEquate(name, value);
            else if (eq.getValue() != value) { unapplied.add("equate " + rva + ": " + name + " already means " + eq.getValue()); return; }
            ghidra.program.model.symbol.Equate old = et.getEquate(at, op, value);
            if (old != null && !old.equals(eq)) old.removeReference(at, op);
            eq.addReference(at, op);
            equates++;
        } catch (Exception e) {
            unapplied.add("equate " + rva + ": " + e.getMessage());
        }
    }

    private int equates, callsites, flagged, namedLocals;
    private ghidra.app.decompiler.DecompInterface localsDecomp;

    // A local the decompiler made (a register or temporary), by its first-use address ("at") or its current name ("var"):
    // renamed and retyped through the decompiler's own symbol (the way the decompiler window's rename does it).
    private void applyHighLocal(Function fn, JsonObject l, Address base) throws Exception {
        if (localsDecomp == null) {
            localsDecomp = new ghidra.app.decompiler.DecompInterface();
            localsDecomp.openProgram(currentProgram);
        }
        localsDecomp.flushCache();
        ghidra.app.decompiler.DecompileResults res = localsDecomp.decompileFunction(fn, 120, monitor);
        if (!res.decompileCompleted()) throw new IllegalStateException("decompile failed: " + res.getErrorMessage());
        Long at = l.has("at") ? Long.decode(l.get("at").getAsString()) : null;
        String var = l.has("var") ? l.get("var").getAsString() : null;
        java.util.Iterator<ghidra.program.model.pcode.HighSymbol> it = res.getHighFunction().getLocalSymbolMap().getSymbols();
        ghidra.program.model.pcode.HighSymbol hit = null;
        while (it.hasNext()) {
            ghidra.program.model.pcode.HighSymbol s = it.next();
            if (s.isParameter()) continue;
            Address pc = s.getPCAddress();
            if ((at != null && pc != null && pc.isMemoryAddress() && pc.subtract(base) == at) || (at == null && s.getName().equals(var))) {
                if (l.has("storage") && !s.getStorage().toString().contains(l.get("storage").getAsString())) continue;
                if (hit != null) throw new IllegalStateException("more than one variable matches; give \"storage\"");
                hit = s;
            }
        }
        if (hit == null) throw new IllegalStateException("no local " + (at != null ? "first used at 0x" + Long.toHexString(at) : var) + " (tools/ghidra.py vars)");
        ghidra.program.model.pcode.HighFunctionDBUtil.updateDBVariable(hit, l.get("name").getAsString(), type(l.get("type").getAsString()), SourceType.USER_DEFINED);
        namedLocals++;
    }

    private Structure struct(String name, int size) {
        Structure s = structs.get(name);
        if (s == null && dtm.getDataType(CategoryPath.ROOT, name) instanceof Structure existing) {
            // Already in the program (an overlay on top of db/'s facts, tools/ghidra/DecompServer.java): extend it.
            s = existing;
            structs.put(name, s);
        }
        if (s == null) {
            s = new StructureDataType(CategoryPath.ROOT, name, size, dtm);
            s = (Structure) dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
            structs.put(name, s);
        }
        if (s.getLength() < size) s.growStructure(size - s.getLength());
        return s;
    }

    private void field(Structure s, int off, DataType dt, String name) {
        int end = off + dt.getLength();
        if (s.getLength() < end) s.growStructure(end - s.getLength());
        s.replaceAtOffset(off, dt, dt.getLength(), name, null);
    }

    private int typedSlots, typedImpls, typedCompiled;

    // A section the facts may leave out (an overlay file usually has only a few).
    private static JsonArray arr(JsonObject facts, String key) {
        return facts.has(key) ? facts.getAsJsonArray(key) : new JsonArray();
    }

    // A slot without a confirmed signature: variadic with a 4-byte result (not the default byte).
    private FunctionDefinitionDataType genericSlot() {
        FunctionDefinitionDataType fd = new FunctionDefinitionDataType(CategoryPath.ROOT, "vfn", dtm);
        fd.setVarArgs(true);
        fd.setReturnType(Undefined4DataType.dataType);
        return fd;
    }

    // The function behind a typed slot gets the slot's signature. Primary vtable: a __thiscall member of the class (`this`
    // is Class*). Secondary vtable at +N: `this` points N bytes into the object, so it is a shifted pointer (Class + N) in
    // ECX, with the other parameters on the stack (custom storage, as Ghidra's thiscall would place them).
    private void applyImpl(JsonObject im, Address base) throws Exception {
        String rva = im.get("rva").getAsString();
        Function fn = getFunctionAt(base.add(Long.decode(rva)));
        String cls = im.get("cls").getAsString();
        int off = im.get("offset").getAsInt();
        if (fn == null) {
            unapplied.add("implementation " + rva + " of " + cls + ": no function there");
            return;
        }
        DataType ret = type(im.get("ret").getAsString());
        JsonArray ps = im.getAsJsonArray("params");
        Structure cs = structs.get(cls);
        if (off == 0) {
            List<Parameter> params = new ArrayList<>();
            for (JsonElement pe : ps) {
                JsonObject p = pe.getAsJsonObject();
                params.add(new ParameterImpl(p.get("name").getAsString(), type(p.get("type").getAsString()), currentProgram));
            }
            fn.setParentNamespace(namespaceFor(cls + "::x", true));
            fn.updateFunction("__thiscall", new ReturnParameterImpl(ret, currentProgram), params,
                              FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
        } else {
            Register ecx = currentProgram.getRegister("ECX");
            List<Parameter> params = new ArrayList<>();
            DataType self = new PointerTypedef(cls + "_at_" + off, cs, 4, dtm, off);
            params.add(new ParameterImpl("this", self, new VariableStorage(currentProgram, ecx), currentProgram));
            int stack = 4;
            for (JsonElement pe : ps) {
                JsonObject p = pe.getAsJsonObject();
                DataType dt = type(p.get("type").getAsString());
                int size = Math.max(4, (dt.getLength() + 3) & ~3); // a small argument still takes a whole stack slot
                params.add(new ParameterImpl(p.get("name").getAsString(), dt, new VariableStorage(currentProgram, stack, dt.getLength()), currentProgram));
                stack += size;
            }
            VariableStorage retStorage = returnStorage(ret);
            fn.setCustomVariableStorage(true);
            fn.updateFunction("__thiscall", new ReturnParameterImpl(ret, retStorage, currentProgram), params,
                              FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
        }
        typedImpls++;
    }

    private VariableStorage returnStorage(DataType ret) throws Exception {
        if (ret instanceof VoidDataType) return VariableStorage.VOID_STORAGE;
        if (ret instanceof FloatDataType || ret instanceof DoubleDataType)
            return new VariableStorage(currentProgram, currentProgram.getRegister("ST0"));
        return new VariableStorage(currentProgram, currentProgram.getRegister(ret.getLength() == 1 ? "AL" : ret.getLength() == 2 ? "AX" : "EAX"));
    }

    // A hooked function typed from our build's debug information (tools/annotate.py "compiled"): our signature and parameter
    // names; for __thiscall, `this` is our class (what the function receives in ECX), the name and namespace are left alone.
    private void applyCompiled(JsonObject im, Address base) throws Exception {
        String rva = im.get("rva").getAsString();
        Function fn = getFunctionAt(base.add(Long.decode(rva)));
        if (fn == null) {
            unapplied.add("compiled signature " + rva + " (" + im.get("fn").getAsString() + "): no function there");
            return;
        }
        DataType ret = type(im.get("ret").getAsString());
        String cc = im.get("cc").getAsString();
        List<Parameter> params = new ArrayList<>();
        if (cc.equals("thiscall")) {
            String self = im.has("this") ? im.get("this").getAsString() : "void*";
            params.add(new ParameterImpl("this", type(self), new VariableStorage(currentProgram, currentProgram.getRegister("ECX")), currentProgram));
            int stack = 4;
            for (JsonElement pe : im.getAsJsonArray("params")) {
                JsonObject p = pe.getAsJsonObject();
                DataType dt = type(p.get("type").getAsString());
                int size = Math.max(4, (dt.getLength() + 3) & ~3); // a small argument still takes a whole stack slot
                params.add(new ParameterImpl(p.get("name").getAsString(), dt, new VariableStorage(currentProgram, stack, dt.getLength()), currentProgram));
                stack += size;
            }
            fn.setCustomVariableStorage(true);
            fn.updateFunction("__thiscall", new ReturnParameterImpl(ret, returnStorage(ret), currentProgram), params,
                              FunctionUpdateType.CUSTOM_STORAGE, true, SourceType.USER_DEFINED);
        } else {
            for (JsonElement pe : im.getAsJsonArray("params")) {
                JsonObject p = pe.getAsJsonObject();
                params.add(new ParameterImpl(p.get("name").getAsString(), type(p.get("type").getAsString()), currentProgram));
            }
            fn.setCustomVariableStorage(false);
            fn.updateFunction("__" + cc, new ReturnParameterImpl(ret, currentProgram), params,
                              FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
        }
        typedCompiled++;
    }

    private static String fieldName(JsonObject f) {
        boolean tentative = f.has("tentative") && f.get("tentative").getAsBoolean();
        return (tentative ? "t_" : "") + f.get("name").getAsString();
    }

    private Namespace namespaceFor(String qualified, boolean asClass) throws Exception {
        String[] parts = qualified.split("::");
        Namespace ns = currentProgram.getGlobalNamespace();
        SymbolTable st = currentProgram.getSymbolTable();
        for (int i = 0; i < parts.length - 1; i++) {
            Namespace next = st.getNamespace(parts[i], ns);
            if (next == null) next = st.createNameSpace(ns, parts[i], SourceType.USER_DEFINED);
            ns = next;
        }
        if (asClass && !(ns instanceof GhidraClass) && ns != currentProgram.getGlobalNamespace()) {
            ns = NamespaceUtils.convertNamespaceToClass(ns);
        }
        return ns;
    }

    // A function the analysis took for noreturn (_EH_prolog 0x8248b0: ~4,500 /O1 callers decompiled to the one call) that a
    // fact says returns: the analysis cut each call to it (a CALL_RETURN flow override, or a cleared fall-through), left the
    // code after it undisassembled, and ended the caller's body at the call. Un-cut those calls (to it and its thunks), disassemble
    // from each fall-through, and recompute each caller's body by flow. Only call sites that were cut are touched, so this is
    // cheap when nothing needs it (an overlay, or a fact that was already applied).
    private int repairedCalls, repairedCallers, grownCallers;

    private void returnAgain(Function fn) throws Exception {
        Listing listing = currentProgram.getListing();
        List<Address> targets = new ArrayList<>();
        targets.add(fn.getEntryPoint());
        Address[] thunks = fn.getFunctionThunkAddresses(true);
        if (thunks != null) targets.addAll(List.of(thunks));
        AddressSet starts = new AddressSet();
        Set<Function> callers = new LinkedHashSet<>();
        for (Address t : targets) {
            for (Reference r : currentProgram.getReferenceManager().getReferencesTo(t)) {
                if (!r.getReferenceType().isCall()) continue;
                Instruction in = listing.getInstructionAt(r.getFromAddress());
                if (in == null) continue;
                boolean cut = false;
                if (in.getFlowOverride() == FlowOverride.CALL_RETURN) { in.setFlowOverride(FlowOverride.NONE); cut = true; }
                if (in.isFallThroughOverridden() && in.getFallThrough() == null) { in.clearFallThroughOverride(); cut = true; }
                if (!cut) continue;
                repairedCalls++;
                Address ft = in.getFallThrough();
                if (ft != null && listing.getInstructionAt(ft) == null) starts.add(ft);
                Function c = getFunctionContaining(in.getAddress());
                if (c != null) callers.add(c); // none: a function created below (db/missed-functions.jsonl) flows past it
            }
        }
        if (!starts.isEmpty()) new DisassembleCommand(starts, null, true).applyTo(currentProgram, monitor);
        for (Function c : callers) {
            long before = c.getBody().getNumAddresses();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, c, monitor);
            repairedCallers++;
            if (c.getBody().getNumAddresses() > before) grownCallers++;
        }
    }

    @Override
    protected void run() throws Exception {
        long t0 = System.currentTimeMillis();
        JsonObject facts = JsonParser.parseString(Files.readString(Path.of(getScriptArgs()[0]), StandardCharsets.UTF_8))
                .getAsJsonObject();
        dtm = currentProgram.getDataTypeManager();
        Address base = currentProgram.getImageBase();

        // noreturn flags first, so the functions created and resized below flow past calls to a function that returns after
        // all; and a function that returns after all gets its callers' bodies back (returnAgain).
        long tr = System.currentTimeMillis();
        for (JsonElement ae : arr(facts, "annotations")) {
            JsonObject a = ae.getAsJsonObject();
            if (!a.get("kind").getAsString().equals("flags") || !a.has("noreturn")) continue;
            Function fn = getFunctionAt(base.add(Long.decode(a.get("rva").getAsString())));
            if (fn == null) continue; // reported by the flags pass below
            boolean noReturn = a.get("noreturn").getAsBoolean();
            fn.setNoReturn(noReturn);
            if (!noReturn) returnAgain(fn);
        }
        if (repairedCalls > 0)
            println("ApplyDb: returning again: " + repairedCalls + " call sites un-cut, " + grownCallers + " of " + repairedCallers
                    + " calling functions grew (" + (System.currentTimeMillis() - tr) / 100 / 10.0 + " s)");

        // Functions the analysis missed (db/missed-functions.jsonl), so names, signatures and decompiles reach them.
        int created = 0;
        if (facts.has("missed")) {
            for (JsonElement me : facts.getAsJsonArray("missed")) {
                Address a = base.add(Long.decode(me.getAsString()));
                if (getFunctionAt(a) != null) continue;
                if (createFunction(a, null) != null) created++;
                else unapplied.add("missed function " + me.getAsString() + ": Ghidra could not create it");
            }
        }

        // Functions the analysis cut short (db/missed-functions.jsonl kind "resized"): disassemble the rest and give each its
        // full body, so the decompiler sees the whole function (GenericSimCall stopped at its switch jump, 367 of ~52K bytes).
        int resized = 0, around = 0;
        if (facts.has("resized")) {
            for (JsonElement re : facts.getAsJsonArray("resized")) {
                JsonArray r = re.getAsJsonArray();
                String rva = r.get(0).getAsString();
                Address a = base.add(Long.decode(rva));
                Address last = a.add(r.get(1).getAsLong() - 1);
                Function f = getFunctionAt(a);
                if (f == null) f = createFunction(a, null);
                if (f == null) { unapplied.add("resized function " + rva + ": no function"); continue; }
                AddressSet body = new AddressSet(a, last);
                new DisassembleCommand(body, null, true).applyTo(currentProgram, monitor);
                body.add(f.getBody());
                // The size runs to the next function db/ knew of; functions found since inside that range (call targets in
                // code returnAgain disassembled) keep their bodies, and this one gets the rest.
                var others = currentProgram.getFunctionManager().getFunctionsOverlapping(body);
                while (others.hasNext()) {
                    Function o = others.next();
                    if (!o.equals(f)) { body = body.subtract(o.getBody()); around++; }
                }
                try {
                    f.setBody(body);
                    resized++;
                } catch (Exception e) {
                    unapplied.add("resized function " + rva + ": " + e.getMessage());
                }
            }
        }

        // Every class structure exists before any vtable is typed (a slot's parameter may name another class).
        for (JsonElement ce : arr(facts, "classes")) struct(ce.getAsJsonObject().get("name").getAsString(), 4);

        // Classes: a structure per class with a typed vtable pointer at each vftable offset.
        for (JsonElement ce : arr(facts, "classes")) {
            JsonObject c = ce.getAsJsonObject();
            String cname = c.get("name").getAsString();
            Structure cs = struct(cname, 4);
            for (JsonElement te : c.getAsJsonArray("vftables")) {
                JsonObject t = te.getAsJsonObject();
                int off = t.get("offset").getAsInt();
                JsonArray slots = t.getAsJsonArray("slots");
                JsonArray sigs = t.has("sigs") ? t.getAsJsonArray("sigs") : null;
                Structure vt = struct(cname + "_vtbl" + (off == 0 ? "" : "_" + off), slots.size() * 4);
                for (int i = 0; i < slots.size(); i++) {
                    if (slots.get(i).isJsonNull()) continue; // an overlay naming only some slots leaves the others as they are
                    JsonElement sig = sigs == null ? null : sigs.get(i);
                    FunctionDefinitionDataType fd;
                    if (sig != null && !sig.isJsonNull()) {
                        // A confirmed db/methods/ fact: __thiscall with its return and parameter types. A function definition
                        // has no implicit `this`: Ghidra gives ECX to the first declared parameter, so `this` is declared
                        // explicitly (without it every typed virtual call shifted the stack model by 4 bytes; lane CG).
                        fd = new FunctionDefinitionDataType(CategoryPath.ROOT, cname + "_vtbl" + (off == 0 ? "" : "_" + off) + "_s" + i, dtm);
                        try {
                            fd.setCallingConvention("__thiscall");
                            fd.setReturnType(type(sig.getAsJsonObject().get("ret").getAsString()));
                            JsonArray ps = sig.getAsJsonObject().getAsJsonArray("params");
                            ParameterDefinition[] defs = new ParameterDefinition[ps.size() + 1];
                            defs[0] = new ParameterDefinitionImpl("this",
                                new PointerDataType(off == 0 ? cs : VoidDataType.dataType, dtm), null);
                            for (int k = 0; k < ps.size(); k++) {
                                JsonObject p = ps.get(k).getAsJsonObject();
                                defs[k + 1] = new ParameterDefinitionImpl(p.get("name").getAsString(), type(p.get("type").getAsString()), null);
                            }
                            fd.setArguments(defs);
                            typedSlots++;
                        } catch (Exception e) {
                            unapplied.add("slot " + cname + "+" + off + " s" + i + ": " + e.getMessage());
                            fd = genericSlot();
                        }
                    } else {
                        fd = genericSlot();
                    }
                    field(vt, i * 4, new PointerDataType(fd, dtm), slots.get(i).getAsString());
                }
                field(cs, off, new PointerDataType(vt, dtm), off == 0 ? "vt" : "vt_" + off);
            }
        }

        // db/annotations.jsonl: enums and structures first (signatures refer to them), unions, then extra fields, then functions.
        JsonArray ann = arr(facts, "annotations");
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            String kind = a.get("kind").getAsString();
            try {
                if (kind.equals("enum")) applyEnum(a);
                else if (kind.equals("union")) union(a.get("name").getAsString());
            } catch (Exception e) {
                unapplied.add(kind + " " + a.get("name").getAsString() + ": " + e.getMessage());
            }
        }
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            if (!a.get("kind").getAsString().equals("struct")) continue;
            struct(a.get("name").getAsString(), a.get("size").getAsInt());
        }
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            if (a.get("kind").getAsString().equals("union")) fillUnion(a);
        }
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            String kind = a.get("kind").getAsString();
            if (!kind.equals("struct") && !kind.equals("fields")) continue;
            Structure s = struct(a.get("name").getAsString(), a.has("size") ? a.get("size").getAsInt() : 4);
            for (JsonElement fe : a.getAsJsonArray("fields")) {
                JsonObject f = fe.getAsJsonObject();
                try {
                    field(s, f.get("off").getAsInt(), type(f.get("type").getAsString()), fieldName(f));
                } catch (Exception e) {
                    unapplied.add("field " + a.get("name").getAsString() + "." + fieldName(f) + ": " + e.getMessage());
                }
            }
        }

        // Names (and class membership for the listed classes' functions).
        java.util.Set<String> members = new java.util.HashSet<>();
        for (JsonElement m : arr(facts, "members")) members.add(m.getAsString());
        int named = 0, failed = 0;
        for (JsonElement fe : arr(facts, "functions")) {
            JsonObject f = fe.getAsJsonObject();
            String rva = f.get("rva").getAsString();
            Function fn = getFunctionAt(base.add(Long.decode(rva)));
            String name = f.get("name").getAsString();
            if (fn == null) {
                unapplied.add("name " + rva + " " + name + ": no function there");
                continue;
            }
            try {
                boolean member = members.contains(rva);
                Namespace ns = namespaceFor(name, member);
                String leaf = name.substring(name.lastIndexOf("::") + 2 > 1 ? name.lastIndexOf("::") + 2 : 0);
                fn.setName(leaf, SourceType.USER_DEFINED);
                fn.setParentNamespace(ns);
                if (member) fn.setCallingConvention("__thiscall");
                named++;
            } catch (Exception e) {
                failed++;
                unapplied.add("name " + rva + " " + name + ": " + e.getMessage());
            }
        }

        // Signatures from db/annotations.jsonl.
        int typed = 0;
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            if (!a.get("kind").getAsString().equals("function")) continue;
            Function fn = getFunctionAt(base.add(Long.decode(a.get("rva").getAsString())));
            if (fn == null) {
                unapplied.add("signature " + a.get("rva").getAsString() + " " + a.get("name").getAsString() + ": no function there");
                continue;
            }
            try {
                fn.setName((a.has("tentative") && a.get("tentative").getAsBoolean() ? "t_" : "") + a.get("name").getAsString(), SourceType.USER_DEFINED);
                if (a.has("class")) fn.setParentNamespace(namespaceFor(a.get("class").getAsString() + "::x", true)); // else a free function
                if (a.has("this")) {
                    // `this` typed explicitly (a __thiscall function outside its class's namespace would get void *this)
                    String self = a.get("this").getAsString();
                    setThiscall(fn, self.endsWith("*") ? self : self + "*", type(a.get("ret").getAsString()), a.getAsJsonArray("params"));
                    typed++;
                    continue;
                }
                List<Parameter> params = new ArrayList<>();
                for (JsonElement pe : a.getAsJsonArray("params")) {
                    JsonObject p = pe.getAsJsonObject();
                    params.add(new ParameterImpl(p.get("name").getAsString(), type(p.get("type").getAsString()), currentProgram));
                }
                fn.updateFunction(a.get("cc").getAsString(), new ReturnParameterImpl(type(a.get("ret").getAsString()), currentProgram),
                                  params, FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
                typed++;
            } catch (Exception e) {
                unapplied.add("signature " + a.get("rva").getAsString() + " " + a.get("name").getAsString() + ": " + e.getMessage());
            }
        }

        // Flags (annotations kind "flags": {"rva", "noreturn"?, "thunk_of"?, "call_fixup"?}), call-site signatures (kind "callsite": {"rva" of
        // the call, "cc", "ret", "params", "varargs"?}) and equates.
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            String kind = a.get("kind").getAsString();
            String rva = a.has("rva") ? a.get("rva").getAsString() : "";
            try {
                if (kind.equals("flags")) {
                    Function fn = getFunctionAt(base.add(Long.decode(rva)));
                    if (fn == null) { unapplied.add("flags " + rva + ": no function there"); continue; }
                    if (a.has("noreturn")) fn.setNoReturn(a.get("noreturn").getAsBoolean()); // and returnAgain at the start
                    if (a.has("call_fixup")) { // the compiler spec's p-code for calls to it (x86win.cspec: EH_prolog, alloca_probe, ...)
                        String fix = a.get("call_fixup").getAsString();
                        if (!fix.isEmpty() && currentProgram.getCompilerSpec().getPcodeInjectLibrary()
                                .getPayload(InjectPayload.CALLFIXUP_TYPE, fix) == null) {
                            unapplied.add("flags " + rva + ": no call fixup named " + fix + " in the compiler spec");
                            continue;
                        }
                        fn.setCallFixup(fix.isEmpty() ? null : fix);
                    }
                    if (a.has("thunk_of")) {
                        Function target = getFunctionAt(base.add(Long.decode(a.get("thunk_of").getAsString())));
                        if (target == null) { unapplied.add("flags " + rva + ": no function at thunk_of " + a.get("thunk_of").getAsString()); continue; }
                        fn.setThunkedFunction(target);
                    }
                    flagged++;
                } else if (kind.equals("callsite")) {
                    Address at = base.add(Long.decode(rva));
                    Function fn = getFunctionContaining(at);
                    if (fn == null || currentProgram.getListing().getInstructionAt(at) == null) {
                        unapplied.add("callsite " + rva + ": no instruction in a function there");
                        continue;
                    }
                    for (ghidra.program.model.symbol.Symbol s : currentProgram.getSymbolTable().getSymbols(at)) {
                        if (s.getParentNamespace().getName().equals("override")) s.delete(); // the previous override
                    }
                    ghidra.program.model.pcode.HighFunctionDBUtil.writeOverride(fn, at, signature("callsite_" + rva, a));
                    callsites++;
                } else if (kind.equals("equate")) {
                    applyEquate(a, base);
                }
            } catch (Exception e) {
                unapplied.add(kind + " " + rva + ": " + e.getMessage());
            }
        }

        // Implementations behind typed slots (after the class structures exist and the names are set).
        if (facts.has("impls")) {
            for (JsonElement ie : facts.getAsJsonArray("impls")) {
                JsonObject im = ie.getAsJsonObject();
                try {
                    applyImpl(im, base);
                } catch (Exception e) {
                    unapplied.add("implementation " + im.get("rva").getAsString() + ": " + e.getMessage());
                }
            }
        }

        // Hooked functions typed from our build (after everything else, which wins where it speaks).
        if (facts.has("compiled")) {
            for (JsonElement ce : facts.getAsJsonArray("compiled")) {
                JsonObject im = ce.getAsJsonObject();
                try {
                    applyCompiled(im, base);
                } catch (Exception e) {
                    unapplied.add("compiled signature " + im.get("rva").getAsString() + " (" + im.get("fn").getAsString() + "): " + e.getMessage());
                }
            }
        }

        // Globals our code reads (typed where our code says what it holds, named after our constant or variable).
        int globals = 0;
        if (facts.has("globals")) {
            for (JsonElement ge : facts.getAsJsonArray("globals")) {
                JsonObject g = ge.getAsJsonObject();
                Address a = base.add(Long.decode(g.get("rva").getAsString()));
                try {
                    if (g.has("type")) {
                        DataType dt = type(g.get("type").getAsString());
                        ghidra.program.model.listing.Data existing = getDataAt(a);
                        if (existing == null || !existing.isDefined() || existing.getLength() == dt.getLength()) {
                            clearListing(a, a.add(dt.getLength() - 1));
                            createData(a, dt);
                        } else {
                            unapplied.add("global " + g.get("rva").getAsString() + ": already " + existing.getDataType().getName());
                        }
                    }
                    if (g.has("name")) createLabel(a, g.get("name").getAsString(), true, SourceType.USER_DEFINED);
                    globals++;
                } catch (Exception e) {
                    unapplied.add("global " + g.get("rva").getAsString() + ": " + e.getMessage());
                }
            }
        }

        int comments = 0;
        if (facts.has("comments")) {
            for (JsonElement ce : facts.getAsJsonArray("comments")) {
                JsonObject c = ce.getAsJsonObject();
                Function fn = getFunctionAt(base.add(Long.decode(c.get("rva").getAsString())));
                if (fn == null) continue; // the name pass reports missing functions
                fn.setComment(c.get("text").getAsString());
                comments++;
            }
        }

        int inlined = 0;
        for (JsonElement ie : arr(facts, "inline")) {
            Function fn = getFunctionAt(base.add(Long.decode(ie.getAsString())));
            if (fn != null) {
                fn.setInline(true);
                inlined++;
            }
        }
        // Call fixups again, last: a signature applied above (compiled signatures of library code such as _EH_prolog) resets
        // the function's call fixup, which the flags pass set earlier.
        for (JsonElement ae : ann) {
            JsonObject a = ae.getAsJsonObject();
            if (!a.get("kind").getAsString().equals("flags") || !a.has("call_fixup")) continue;
            Function fn = getFunctionAt(base.add(Long.decode(a.get("rva").getAsString())));
            String fix = a.get("call_fixup").getAsString();
            if (fn != null && currentProgram.getCompilerSpec().getPcodeInjectLibrary()
                    .getPayload(InjectPayload.CALLFIXUP_TYPE, fix) != null) fn.setCallFixup(fix.isEmpty() ? null : fix);
        }
        // Stack locals (overlays): {"rva", "offset" (stack offset, negative for locals), "type", "name"}.
        int locals = 0;
        for (JsonElement le : arr(facts, "locals")) {
            JsonObject l = le.getAsJsonObject();
            String rva = l.get("rva").getAsString();
            Function fn = getFunctionAt(base.add(Long.decode(rva)));
            if (fn == null) { unapplied.add("local " + rva + ": no function there"); continue; }
            if (!l.has("offset")) {
                try {
                    monitor.checkCancelled();
                    applyHighLocal(fn, l, base);
                } catch (ghidra.util.exception.CancelledException e) {
                    throw e;
                } catch (Exception e) {
                    unapplied.add("local " + rva + "@" + (l.has("at") ? l.get("at").getAsString() : l.has("var") ? l.get("var").getAsString() : "?") + ": " + e.getMessage());
                }
                continue;
            }
            try {
                int off = l.get("offset").getAsInt();
                DataType dt = type(l.get("type").getAsString());
                String lname = l.has("name") ? l.get("name").getAsString() : null;
                ghidra.program.model.listing.StackFrame frame = fn.getStackFrame();
                ghidra.program.model.listing.Variable v = frame.getVariableContaining(off);
                if (v != null && v.getStackOffset() == off && v.getLength() == dt.getLength()) {
                    v.setDataType(dt, SourceType.USER_DEFINED);
                    if (lname != null) v.setName(lname, SourceType.USER_DEFINED);
                } else {
                    frame.createVariable(lname, off, dt, SourceType.USER_DEFINED);
                }
                locals++;
            } catch (Exception e) {
                unapplied.add("local " + rva + "@" + l.get("offset").getAsString() + ": " + e.getMessage());
            }
        }
        if (localsDecomp != null) localsDecomp.dispose();
        if (locals > 0 || namedLocals > 0) println("ApplyDb: " + locals + " stack locals, " + namedLocals + " decompiler locals");
        if (equates + callsites + flagged > 0) println("ApplyDb: " + equates + " equates, " + callsites + " call-site signatures, " + flagged + " flagged functions");

        for (String u : unapplied) println("ApplyDb: unapplied " + u);
        println("ApplyDb: elapsed " + (System.currentTimeMillis() - t0) / 100 / 10.0 + " s");
        println("ApplyDb: " + unapplied.size() + " unapplied; " + created + " missed functions created, " + resized + " resized (" + around + " around a function inside), " + structs.size() + " structures, " + named + " names (" + failed + " failed), " + typed
                + " signatures, " + typedSlots + " typed slots, " + typedImpls + " typed implementations, " + typedCompiled + " compiled signatures, " + globals + " globals, " + comments + " comments, " + inlined + " inline");
    }
}
