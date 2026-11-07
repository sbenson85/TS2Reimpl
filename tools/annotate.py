#!/usr/bin/env python3
"""Builds the annotation facts for the annotated Ghidra project (pilot; tools/ghidra/ApplyDb.java applies
them, tools/ghidra-annotate.ps1 runs both). Everything comes from db/ and the unpacked exe, so the
annotated project can be rebuilt from scratch at any time:

  functions   name per function: the status entry's name (text before " (") over db/names.jsonl;
              shared_vf_* names are dropped (folded code: a name from one class misleads in another)
  inline      tiny accessors (lea/mov eax, [ecx+N]; ret, and `mov eax, [esp+4]; ret`): marked inline so
              the decompiler shows the field access instead of a call to a folded helper
  members     functions named Class::... for a class in --classes: put in that class, __thiscall, so
              `this` gets the class's structure
  classes     for each class in --classes: a structure with a typed pointer to a vtable structure at
              each vftable offset; vtable fields named after the slot's function, and typed (__thiscall,
              return and parameter types) where a confirmed db/methods/ fact gives the signature
  impls       the functions behind those typed slots get the same signature, `this` typed as the class
              (a shifted pointer, class + offset, for a slot of a secondary vtable); skipped for shared
              implementations and functions db/annotations.jsonl types itself
  missed      functions db/missed-functions.jsonl lists as missed by Ghidra's analysis: created first
  compiled    from local/pdbfacts.jsonl (tools/pdbfacts.ps1: our build's debug information), when present:
              the layout of each of our classes and structs reachable from a hooked function or named
              like an original class (fields our code names; padding, placeholder names and template
              types left undefined), and each hooked function's signature with parameter names, `this`
              typed as our class (it is what the original receives in ECX). db/annotations.jsonl and
              db/methods/ facts win where they speak; disagreements are reported as problems.
  globals     data our code reads through GameAddress<T>(0x...) (src/): typed from T where it is a scalar or a
              pointer, and named after the constant (kName = 0x...) or the variable it is read into
  annotations db/annotations.jsonl: structures, extra class fields and function signatures (kind
              struct / fields / function); tentative fields get a `t_` prefix in Ghidra

Checks run first (and alone with --check, exit 1 on a problem): fields that overlap or run past their
structure, unknown types, a function given two different names or signatures (db/annotations.jsonl
against db/methods/ facts for the function a class's vtable slot holds), one implementation behind
slots whose facts disagree.

Usage: python -I tools/annotate.py [--classes cTSObject,...] [--out local/ghidra-annotated/facts.json] [--check]
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

DEFAULT_CLASSES = ("cTSObject,cTSPerson,cTSTreeSim,cEdithObject,cTSBehaviorNode,cTSBehaviorTree,cTSTreeStackElem,"
                   "cTSTreeStack,cTSPersistSystem")


def clean(name):
    name = name.split(" (")[0].strip()
    return re.sub(r"[^A-Za-z0-9_:~<>,]", "_", name)


def tiny_accessor(img, start, size):
    """True for `lea|mov eax, [ecx+N]; ret` and `mov eax, [esp+4]; ret` bodies."""
    b = img[start:start + size]
    if size <= 4 and b[:2] in (b"\x8d\x41", b"\x8b\x41") and b[3:4] == b"\xc3":
        return True
    if size <= 7 and b[:2] in (b"\x8d\x81", b"\x8b\x81") and b[6:7] == b"\xc3":
        return True
    if b[:2] in (b"\x8b\x01", b"\x8d\x01") and b[2:3] == b"\xc3":
        return True
    return b[:5] == b"\x8b\x44\x24\x04\xc3"


SCALAR_SIZES = {"i8": 1, "u8": 1, "i16": 2, "u16": 2, "i32": 4, "u32": 4, "f32": 4, "bool": 1, "void": 0,
                "i64": 8, "u64": 8, "f64": 8, "char": 1, "wchar": 2}


def type_size(t, struct_sizes):
    """Bytes of an annotation type, or None when it is unknown."""
    t = t.strip()
    if t.endswith("]"):
        inner, n = t[:t.rindex("[")], int(t[t.rindex("[") + 1:-1], 0)
        size = type_size(inner, struct_sizes)
        return None if size is None else size * n
    if t.endswith("*"):
        return 4
    return SCALAR_SIZES.get(t, struct_sizes.get(t))


def check(layouts, method_facts, classes, shared=frozenset()):
    """Problems in the facts that would make the annotated project wrong or silently drop something. `shared`:
    implementations behind unrelated slots (library code such as _purecall, folded shared_vf_*), never typed."""
    problems = []
    struct_sizes = {a["name"]: a["size"] for a in layouts if a["kind"] == "struct" and "size" in a}
    struct_sizes.update({a["name"]: a.get("size", 4) for a in layouts if a["kind"] == "enum"})
    for a in layouts:
        if a["kind"] == "union":
            sizes = [type_size(f["type"], struct_sizes) for f in a["fields"]]
            if None in sizes:
                problems.append(f"union {a['name']}: unknown member type")
            struct_sizes[a["name"]] = max([s for s in sizes if s] + [0])
    fields = {}
    for a in layouts:
        if a["kind"] in ("struct", "fields"):
            for f in a.get("fields", []):
                fields.setdefault(a["name"], []).append(f)
    for name, fs in fields.items():
        spans = []
        for f in fs:
            off = int(f["off"], 0) if isinstance(f["off"], str) else f["off"]
            size = type_size(f["type"], struct_sizes)
            if size is None:
                problems.append(f"{name}.{f['name']}: unknown type {f['type']!r}")
                continue
            if name in struct_sizes and off + size > struct_sizes[name]:
                problems.append(f"{name}.{f['name']}: +0x{off:x}..+0x{off + size:x} runs past the structure's 0x{struct_sizes[name]:x} bytes")
            spans.append((off, off + size, f["name"], f["type"]))
        spans.sort()
        for (a0, a1, an, at), (b0, b1, bn, bt) in zip(spans, spans[1:]):
            if b0 < a1:
                problems.append(f"{name}: {an} ({at} at +0x{a0:x}..+0x{a1:x}) overlaps {bn} ({bt} at +0x{b0:x})")
    # One name and one signature per function.
    names = {}
    for a in layouts:
        if a["kind"] in ("name", "function"):
            full = a["name"] if a["kind"] == "name" or "class" not in a else f"{a['class']}::{a['name']}"
            prev = names.setdefault(a["rva"], full)
            if prev.split("::")[-1] != full.split("::")[-1]:
                problems.append(f"{a['rva']}: named {prev!r} and {full!r}")
    sigs = {a["rva"]: (a["ret"], [p["type"] for p in a["params"]], "db/annotations.jsonl")
            for a in layouts if a["kind"] == "function"}
    tables = {(c["name"], t["offset"]): t["entries"] for c in classes for t in c["vftables"]}
    for m in method_facts:
        if not m.get("class") or "slot" not in m:
            continue
        entries = tables.get((m["class"], m.get("vtable", 0)))
        if not entries or m["slot"] >= len(entries):
            problems.append(f"{m['_where']}: {m['class']} has no slot {m['slot']} at vtable +{m.get('vtable', 0)}")
            continue
        rva = entries[m["slot"]]
        if rva in shared:
            continue
        sig = (m["ret"], [p["type"] for p in m["params"]], f"{m['_where']} ({m['iface']}::{m['name']})")
        prev = sigs.setdefault(rva, sig)
        if prev is not sig and (len(prev[1]) != len(sig[1]) or
                                type_size(prev[0], struct_sizes) != type_size(sig[0], struct_sizes)):
            problems.append(f"{rva}: {prev[2]} says {prev[0]}({', '.join(prev[1])}), "
                            f"{sig[2]} says {sig[0]}({', '.join(sig[1])})")
    return problems


def fact_type(t, known):
    """A method-fact type in annotation syntax: another interface is a pointer to its structure when the
    annotated project has one (a class in --classes or an annotations structure), else void*."""
    t = t.strip()
    if t.endswith("*"):
        inner = t[:-1].strip()
        if inner in SCALAR_SIZES or inner in known:
            return inner + "*" if inner != "void" else "void*"
        return "void*"
    return t if t in SCALAR_SIZES else "u32"


def shared_implementations():
    """Functions that sit behind unrelated slots: library code (_purecall...) and folded shared_vf_* bodies."""
    shared = {m["rva"] for m in tsdb.jsonl("library-matches.jsonl")}
    shared |= {n["rva"] for n in tsdb.jsonl("names.jsonl") if n["name"].startswith("shared_vf_")}
    return shared


# Field names that say nothing (padding, placeholders): left undefined rather than named.
PLACEHOLDER = re.compile(r"^(m_?)?(pad|unknown|unk|reserved|gap|field_?[0-9a-fx]+|_+formal|__vfptr)", re.I)
HOOK_TARGET = re.compile(r'\{"[^"]*",\s*(0x[0-9a-f]+),\s*\d+,\s*0x[0-9a-f]+,\s*'
                         r'(?:MemberAddress\(|reinterpret_cast<void \*>\()&([A-Za-z_][\w:]*)\)\s*\}')


def hook_targets():
    """rva -> our C++ function (qualified as written) for the hook entries naming a plain function or member."""
    out = {}
    for path in sorted((tsdb.ROOT / "src" / "hook" / "tables").glob("*.cpp")):
        for m in HOOK_TARGET.finditer(path.read_text(encoding="utf-8")):
            out[m.group(1)] = m.group(2)
    return out


def compiled_facts(original_classes, vftable_offsets):
    """(structs, impls) from local/pdbfacts.jsonl: our classes' layouts and our hooked functions' signatures."""
    path = tsdb.main_root() / "local" / "pdbfacts.jsonl"
    if not path.exists():
        return [], []
    udts, fns = {}, {}
    for line in open(path, encoding="utf-8-sig"):
        if not line.strip():
            continue
        f = json.loads(line)
        if "udt" in f:
            udts[f["udt"]] = f
        else:
            fns.setdefault(f["fn"], []).append(f)
    udt_sizes = {k: v["size"] for k, v in udts.items()}

    def usable(name):  # an empty class of ours (an opaque handle, size 1) has no layout to give: void* behind a pointer
        return name in udts and (udts[name]["size"] > 1 or udts[name]["fields"]) and "<" not in name and "`" not in name and not name.startswith("std::")

    wanted, queue = set(), []

    def want(name):
        if usable(name) and name not in wanted:
            wanted.add(name)
            queue.append(name)

    def conv(t):
        """A pdbfacts type in annotation notation; our UDTs are kept (and wanted), others become bytes, or void* behind a pointer."""
        if t.endswith("]"):
            inner, n = t[:t.rindex("[")], int(t[t.rindex("[") + 1:-1])
            e = conv(inner)
            return f"{e}[{n}]" if e and n and type_size(e, udt_sizes) else None
        if t.endswith("*"):
            inner = t[:-1]
            if inner in SCALAR_SIZES:
                return t
            if usable(inner) or inner.endswith(("*", "]")):
                e = conv(inner)
                if e and not e.startswith("u8["):
                    return e + "*"
            return "void*"
        if t == "fnptr":
            return "void*"
        if t in SCALAR_SIZES:
            return t
        if t.startswith("?"):
            n = int(t[1:])
            return {1: "u8", 2: "u16", 4: "u32"}.get(n, f"u8[{n}]" if n else None)
        if usable(t):
            want(t)
            return t
        return f"u8[{udts[t]['size']}]" if t in udts and udts[t]["size"] else None

    # Signatures of hooked functions: the compiled function of that name (skipped when overloads disagree).
    impls = []
    for rva, target in hook_targets().items():
        cands = fns.get(target)
        if not cands:
            matches = [k for k in fns if k.endswith("::" + target)]
            cands = fns[matches[0]] if len(matches) == 1 else []
        if len({(c["cc"], c["this"], c["ret"], tuple(c["params"])) for c in cands}) != 1:
            continue
        best = max(cands, key=lambda c: len([n for n in c["names"] if n and n != "__formal"]))
        if best["cc"] not in ("thiscall", "cdecl", "stdcall", "fastcall"):
            continue
        ret = conv(best["ret"])
        params = [conv(t) for t in best["params"]]
        if ret is None or any(p is None for p in params) or type_size(ret, udt_sizes) not in (0, 1, 2, 4):
            continue
        names = best["names"] if len(best["names"]) == len(params) else []
        impl = {"rva": rva, "cc": best["cc"], "ret": ret, "fn": target,
                "params": [{"type": t, "name": names[i] if names and names[i] and names[i] != "__formal" else f"arg{i + 1}"}
                           for i, t in enumerate(params)]}
        if best["this"]:
            impl["this"] = conv(best["this"]) or "void*"
        impls.append(impl)
    # Our classes named like original classes are wanted too, with what they reach.
    for name in original_classes:
        want(name)
    structs = []
    while queue:
        name = queue.pop()
        u = udts[name]
        skip = set(vftable_offsets.get(name, ())) | ({0} if u.get("vfptr") else set())
        fields = []
        for f in u["fields"]:
            if "bits" in f or PLACEHOLDER.match(f["name"]) or "vtable" in f["name"].lower() or f["off"] in skip:
                continue
            t = conv(f["type"])
            if t:
                fields.append({"off": f["off"], "type": t, "name": f["name"]})
        structs.append({"kind": "struct", "name": name, "size": u["size"], "fields": fields, "source": "compiled"})
    return structs, impls


def merge_compiled(layouts, structs):
    """Compiled layouts under the hand-written ones: a field at an offset db/annotations.jsonl already covers is
    dropped (a problem when it disagrees on size), and a structure db/annotations.jsonl sizes keeps that size."""
    problems, out = [], []
    hand = {}
    for a in layouts:
        if a["kind"] in ("struct", "fields"):
            hand.setdefault(a["name"], []).extend(a.get("fields", []))
    hand_sizes = {a["name"]: a["size"] for a in layouts if a["kind"] == "struct" and "size" in a}
    sizes = dict({s["name"]: s["size"] for s in structs}, **hand_sizes)
    for s in structs:
        if not s["fields"] and s["size"] <= 1:
            continue  # an empty marker class of ours (an opaque handle): no layout to give
        mine = {(int(f["off"], 0) if isinstance(f["off"], str) else f["off"]): f for f in hand.get(s["name"], [])}
        fields = []
        for f in s["fields"]:
            h = mine.get(f["off"])
            if h is not None:
                if type_size(h["type"], sizes) != type_size(f["type"], sizes):
                    problems.append(f"{s['name']}+0x{f['off']:x}: db/annotations.jsonl has {h['name']}: {h['type']}, "
                                    f"our code {f['name']}: {f['type']}")
                continue
            fields.append(f)
        if s["name"] in hand and s["name"] not in hand_sizes:
            out.append({"kind": "fields", "name": s["name"], "fields": fields, "source": "compiled"})
        elif s["name"] in hand_sizes:
            if hand_sizes[s["name"]] != s["size"]:
                problems.append(f"{s['name']}: db/annotations.jsonl size 0x{hand_sizes[s['name']]:x}, our code 0x{s['size']:x}")
            out.append({"kind": "fields", "name": s["name"], "fields": fields, "source": "compiled"})
        else:
            out.append(dict(s, fields=fields))
    return out, problems


GLOBAL_TYPES = {"float": "f32", "double": "f64", "int32_t": "i32", "int": "i32", "uint32_t": "u32", "unsigned": "u32",
                "int16_t": "i16", "uint16_t": "u16", "uint8_t": "u8", "int8_t": "i8", "bool": "bool"}
# Local variable names too generic to name a global after (the read is named by its context, not the data).
WEAK_NAMES = {"arg", "args", "text", "value", "result", "cached", "service", "limit", "scale", "count", "data", "ptr", "out",
              "base", "table", "entry", "name", "flag", "flags", "word", "self", "item", "list", "temp", "tmp", "slot"}
CONSTANT = re.compile(r"\bk([A-Z][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-f]{5,})\b")
READ = re.compile(r"(?:(\w+)\s*=\s*)?\*?GameAddress<\s*([^>]+?)\s*>\(\s*(0x[0-9a-f]+|k[A-Za-z0-9_]+)\s*\)")


def global_facts(pe, funcs):
    """Data our code reads: rva -> {type, name}; code addresses, vtables and import slots are left to Ghidra."""
    constants, found = {}, {}
    sources = [p for p in (tsdb.ROOT / "src").rglob("*") if p.suffix in (".cpp", ".h")]
    texts = [p.read_text(encoding="utf-8", errors="replace") for p in sources]
    for text in texts:
        for m in CONSTANT.finditer(text):
            constants.setdefault("k" + m.group(1), (m.group(1), m.group(2)))
    for text in texts:
        for m in READ.finditer(text):
            var, cpp, addr = m.group(1), m.group(2).replace("const ", "").strip(), m.group(3)
            name = None
            if addr.startswith("k"):
                if addr not in constants:
                    continue
                name, addr = constants[addr]
            g = found.setdefault(hex(int(addr, 16)), {"types": set(), "names": []})
            g["types"].add(cpp)
            if name:
                g["names"].insert(0, name)
            elif var and len(var) > 2:
                g["names"].append(var)
    out = []
    for rva, g in sorted(found.items()):
        sec = pe.get_section_by_rva(int(rva, 16))
        if sec is None or sec.Characteristics & 0x20000000 or rva in funcs:  # executable: code, not data
            continue
        entry = {"rva": rva}
        names = [re.sub(r"(Rva|Global)$", "", n) for n in g["names"]
                 if not re.match(r"(Iat|VTable|.*VTable$|.*Vtbl$)", n) and n.lower() not in WEAK_NAMES]
        if names:
            entry["name"] = "g_" + max(sorted(set(names)), key=names.count)
        types = {GLOBAL_TYPES.get(t, "void*" if t.endswith("*") else None) for t in g["types"]}
        if len(types) == 1 and None not in types:
            entry["type"] = types.pop()
        if len(entry) > 1:
            out.append(entry)
    return out


LEDGER = None  # {"annotations.jsonl": text, "methods/<group>.jsonl": text}: hand facts given with --ledger instead of db/


def hand_records(prefix):
    """(where, record) for the hand-maintained facts: db/annotations.jsonl (prefix "annotations") or db/methods/*.jsonl
    ("methods"), from db/ of this checkout, or from the ledger tools/ghidra.py keeps when --ledger is given."""
    sources = []
    if LEDGER is not None:
        sources = [(name.split("/")[-1], text) for name, text in sorted(LEDGER.items(), key=lambda kv: kv[0].lower())
                   if name.startswith(prefix)]
    elif prefix == "annotations":
        path = tsdb.ROOT / "db" / "annotations.jsonl"
        sources = [(path.name, path.read_text(encoding="utf-8"))] if path.exists() else []
    else:
        sources = [(p.name, p.read_text(encoding="utf-8")) for p in sorted((tsdb.ROOT / "db" / "methods").glob("*.jsonl"))]
    for name, text in sources:
        for n, line in enumerate(text.splitlines(), 1):
            if line.strip():
                yield f"{name}:{n}", json.loads(line)


def load_method_facts():
    facts = []
    for where, m in hand_records("methods"):
        if "extends" not in m and "alias_of" not in m:
            m["_where"] = where
            facts.append(m)
    return facts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--classes", default=DEFAULT_CLASSES)
    ap.add_argument("--out", default=str(tsdb.main_root() / "local" / "ghidra-annotated" / "facts.json"))
    ap.add_argument("--check", action="store_true", help="only check the facts for conflicts")
    ap.add_argument("--ledger", help="hand facts from this JSON file ({file: text}, tools/ghidra.py's ledger) instead of db/")
    ap.add_argument("--no-method-classes", action="store_true",
                    help="type only --classes (default: also every class that has a method fact in db/methods/)")
    args = ap.parse_args()
    global LEDGER
    if args.ledger:
        LEDGER = json.loads(Path(args.ledger).read_text(encoding="utf-8"))
        LEDGER = LEDGER.get("files", LEDGER)
    layouts = [r for _, r in hand_records("annotations")]
    all_classes = list(tsdb.jsonl("classes.jsonl"))
    problems = check(layouts, load_method_facts(), all_classes, shared_implementations())
    vft = {c["name"]: [t["offset"] for t in c["vftables"]] for c in all_classes}
    compiled_structs, compiled_impls = compiled_facts([c["name"] for c in all_classes], vft)
    compiled_layouts, merge_problems = merge_compiled(layouts, compiled_structs)
    problems += merge_problems
    problems += [p for p in check(layouts + compiled_layouts, [], []) if p not in problems]
    for p in problems:
        print("problem:", p)
    if args.check:
        print(f"{len(problems)} problems")
        return 1 if problems else 0
    import pefile

    funcs = {f["rva"]: f for f in tsdb.functions()}
    names = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    status = tsdb.load_status()
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    img = pe.get_memory_mapped_image()

    out_funcs, inline = [], []
    for rva, f in funcs.items():
        start = int(rva, 16)
        if tiny_accessor(img, start, f["size"]):
            inline.append(rva)
            continue
        name = status[rva]["name"] if rva in status else names.get(rva)
        if not name or name.startswith("shared_vf_") or name.startswith("FUN_"):
            continue
        out_funcs.append({"rva": rva, "name": clean(name)})

    named = {a["rva"]: a["name"] for a in layouts if a["kind"] == "name"}
    # Method facts (db/methods/*.jsonl, tools/ifacegen.py) name vtable slots: (class, vtable offset, slot) -> name.
    methods = {}
    for _, m in hand_records("methods"):
        if m.get("class") and "slot" in m:
            methods[(m["class"], m.get("vtable", 0), m["slot"])] = ("t_" if m.get("tentative") else "") + m["name"]

    wanted = set(args.classes.split(","))
    if not args.no_method_classes:
        # A class with a method fact is typed too, so the fact shows in the pseudo-code (annotating a method of a class
        # outside the default list would otherwise change nothing the decompiler shows).
        wanted |= {c for c, _, _ in methods} & {c["name"] for c in all_classes}
    known = wanted | {a["name"] for a in layouts + compiled_layouts if a["kind"] in ("struct", "fields")}
    shared = shared_implementations()
    typed_by_annotation = {a["rva"] for a in layouts if a["kind"] == "function"}
    # Confirmed signatures per (class, vtable offset, slot); tentative facts only name their slot.
    signatures = {}
    for m in load_method_facts():
        if m.get("class") and "slot" in m and not m.get("tentative"):
            signatures[(m["class"], m.get("vtable", 0), m["slot"])] = {
                "ret": fact_type(m["ret"], known),
                "params": [{"type": fact_type(p["type"], known), "name": p["name"]} for p in m["params"]]}
    impls = {}
    classes = []
    for c in tsdb.jsonl("classes.jsonl"):
        if c["name"] not in wanted:
            continue
        tables = []
        for t in c["vftables"]:
            slots, sigs = [], []
            for i, e in enumerate(t["entries"]):
                sig = signatures.get((c["name"], t["offset"], i))
                sigs.append(sig)
                if sig and e not in shared and e not in typed_by_annotation:
                    impl = dict(sig, rva=e, cls=c["name"], offset=t["offset"])
                    prev = impls.setdefault(e, impl)
                    if prev is not impl and (prev["cls"], prev["offset"]) != (c["name"], t["offset"]):
                        prev["conflict"] = True  # one body behind slots of two classes: leave it untyped
                n = methods.get((c["name"], t["offset"], i)) or named.get(e) or                     (status[e]["name"] if e in status else names.get(e, f"vf{i}"))
                n = clean(n).split("::")[-1] if not n.startswith("shared_vf_") else f"vf{i}"
                slots.append(f"s{i}_{n}"[:60])
            tables.append({"offset": t["offset"], "rva": t["rva"], "slots": slots, "sigs": sigs})
        classes.append({"name": c["name"], "vftables": tables})

    # db/annotations.jsonl "name" entries (facts lanes worked out) override; tentative ones get t_.
    byrva = {f["rva"]: f for f in out_funcs}
    for a in layouts:
        if a["kind"] == "name":
            n = ("t_" if a.get("tentative") else "") + a["name"]
            if a["rva"] in byrva:
                byrva[a["rva"]]["name"] = n
            else:
                out_funcs.append({"rva": a["rva"], "name": n})
    # A name that is only a slot number or an address (vf19, FUN_a01e5250) takes our C++ function's name: the hooked
    # function's replacement says what it does (cGZDBSegmentPackedFile::vf19 -> cGZDBSegmentPackedFile::ContainsKey).
    generic = re.compile(r"(^|_|::)(vf\d+(_\d+)*|FUN_[0-9a-f]+)$")
    ours = {c["rva"]: c["fn"].split("::")[-1] for c in compiled_impls}
    byrva = {f["rva"]: f for f in out_funcs}
    renamed = 0
    for rva, leaf in ours.items():
        f = byrva.get(rva)
        if f is None:
            target = next(c["fn"] for c in compiled_impls if c["rva"] == rva)
            out_funcs.append({"rva": rva, "name": clean(target)})
            renamed += 1
        elif generic.search(f["name"]) and not generic.search(leaf):
            m = generic.search(f["name"])
            f["name"] = f["name"][:m.start(2)] + clean(leaf)
            renamed += 1
    # Methods of the listed classes and of the structures in db/annotations.jsonl are __thiscall members.
    owners = wanted | {a["name"] for a in layouts if a["kind"] in ("struct", "fields")}
    members = [f for f in out_funcs if "::" in f["name"] and f["name"].split("::")[0] in owners]
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    impl_list = [i for i in impls.values() if not i.get("conflict")]
    # Compiled signatures for the hooked functions nothing else types; where a method fact also types one, compare.
    compiled_list = []
    for c in compiled_impls:
        other = impls.get(c["rva"])
        if other is not None:
            if len(other["params"]) != len(c["params"]):
                print(f"problem: {c['rva']}: db/methods gives {len(other['params'])} parameters, our {c['fn']} {len(c['params'])}")
            continue
        if c["rva"] in shared or c["rva"] in typed_by_annotation:
            continue
        compiled_list.append(c)
    # Functions Ghidra's analysis missed (db/missed-functions.jsonl kind "missed"): created in the annotated copy.
    missed = [m["rva"] for m in tsdb.jsonl("missed-functions.jsonl") if m.get("kind") == "missed"]
    # Functions Ghidra cut short (kind "resized": it stopped at an unresolved jump or an _EH_prolog call): their full extent.
    resized = [[m["rva"], m["size"]] for m in tsdb.jsonl("missed-functions.jsonl") if m.get("kind") == "resized"]
    globals_list = global_facts(pe, funcs)
    # Hand-made globals and locals (db/annotations.jsonl kinds "global" and "local"): a global wins over one derived from our code.
    by_global = {g["rva"]: g for g in globals_list}
    for a in layouts:
        if a["kind"] == "global":
            g = {"rva": a["rva"]}
            if a.get("type"):
                g["type"] = a["type"]
            if a.get("name"):
                g["name"] = ("t_" if a.get("tentative") else "") + a["name"]
            by_global[a["rva"]] = g
    globals_list = [by_global[k] for k in sorted(by_global)]
    # A local is a stack slot ("offset") or a variable the decompiler made, by first use ("at") or name ("var").
    locals_list = [dict({k: a[k] for k in ("rva", "offset", "at", "var", "storage") if k in a}, type=a["type"],
                        name=("t_" if a.get("tentative") else "") + a["name"]) for a in layouts if a["kind"] == "local"]
    # A comment at the top of each function we track: its status, our source and test, the original code it still calls,
    # the community mods that hook it, and the start of its notes.
    hooks = {}
    for h in tsdb.jsonl("community-hooks.jsonl"):
        if h.get("function"):
            hooks.setdefault(h["function"], []).append(f"{h['mod']} {h['name']}")
    comments = []
    for rva, e in status.items():
        lines = [f"{e['status']}: {e['name']}"]
        for key in ("src", "test"):
            if e.get(key):
                lines.append(f"{key}: {e[key]}")
        if e.get("original_calls"):
            lines.append("calls original: " + ", ".join(e["original_calls"]))
        if e.get("trace"):
            lines.append("trace: " + (e["trace"] if isinstance(e["trace"], str) else json.dumps(e["trace"]))[:200])
        if rva in hooks:
            lines.append("hooked by: " + "; ".join(hooks[rva]))
        if e.get("notes"):
            notes = e["notes"]
            lines.append("notes: " + (notes[:400] + "..." if len(notes) > 400 else notes))
        comments.append({"rva": rva, "text": "\n".join(lines)})
    Path(args.out).write_text(json.dumps({"functions": out_funcs, "globals": globals_list, "comments": comments, "inline": inline, "classes": classes,
                                          "impls": impl_list, "compiled": compiled_list, "missed": missed, "resized": resized,
                                          "members": [m["rva"] for m in members], "annotations": layouts + compiled_layouts,
                                          "locals": locals_list}),
                              encoding="utf-8")
    print(f"{len(out_funcs)} names, {len(inline)} inline accessors, {len(classes)} classes, "
          f"{len(members)} member functions, {len(layouts)} annotations, "
          f"{sum(1 for c in classes for t in c['vftables'] for x in t['sigs'] if x)} typed slots, {len(impl_list)} typed implementations, "
          f"{len(compiled_layouts)} compiled layouts, {len(compiled_list)} compiled signatures, "
          f"{renamed} generic names replaced by ours, "
          f"{len(globals_list)} globals -> {args.out}")


if __name__ == "__main__":
    sys.exit(main())
