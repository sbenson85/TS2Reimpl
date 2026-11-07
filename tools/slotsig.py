#!/usr/bin/env python3
"""Per-slot signatures of an interface, from every class that implements it: stack argument words (`ret N`) and whether
the slot returns in ST0 (a float), agreed across the implementations. The facts recording fakes need (tests/drawfx.h).

  tools/py -I tools/slotsig.py cIGZDrawContext            # table: slot, words, kind, agreement, implementers' bodies
  tools/py -I tools/slotsig.py cIGZWin --slots 0-60        # a range of slots
  tools/py -I tools/slotsig.py cIGZDrawContext --cpp kDrawContextSlots   # a C++ initialiser for drawfx::SlotSig[]
  tools/py -I tools/slotsig.py cGZWin --class              # one class's own vtable(s) only (no implementers)
  tools/py -I tools/slotsig.py cIGZDrawContext --field 0x18=cIGZDrawContextEngine   # resolve tail calls on this+0x18
  tools/py -I tools/slotsig.py --header tests/fx_slots.h cIGZDrawContext@0x18=cIGZDrawContextEngine cIGZWin ...
      # writes the drawfx slot tables (names and argument kinds from db/methods; docs/ui-testing.md)
  tools/py -I tools/slotsig.py --header tests/fx_slots.h ... cIGZString:src/gz/cIGZString.h
      # names and kinds from our own declaration instead, for a hand-written interface (no db/methods facts)

Implementers: every class in db/classes.jsonl with the interface among its RTTI bases; the vtable at the interface's
subobject offset gives the slots. Slots past the shortest implementer's vtable are a derived class's, not the
interface's: the table stops at the shortest (`--all` shows every slot any implementer has).

Per body: `ret N` (all the returns in the body; a thunk `jmp target` is followed; `jmp [reg+N]` forwards elsewhere and
says so), `_purecall` bodies are skipped, and a body whose last instruction before a `ret` loads the x87 stack
(fld/fild/fldz/fld1) returns a float. Kinds: `int` (eax or nothing), `float` (ST0). A slot whose bodies disagree is
printed with every variant and its count: read them before trusting the slot (a folded `ret` from an unrelated body,
or a body that never returns).
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PURECALL = "0x8261ba"
SKIP_BEFORE_RET = ("pop", "leave", "add", "mov", "fstp")  # epilogue instructions between the result and the ret

_cache = {}
TAIL = re.compile(r"^dword ptr \[e[a-d]x(?: \+ (0x[0-9a-f]+|\d+))?\]$")
FIELD = re.compile(r"^ecx, dword ptr \[e(?:cx|si|di|bx|ax|dx)(?: \+ (0x[0-9a-f]+|\d+))?\]$")


def tail_of(ins, prev):
    """A tail call through a vtable (`jmp [eax + N]`, or `mov eax, [eax + N]; jmp eax`): (object, slot), the object
    being 'this' or the field offset ecx was last loaded from."""
    slot = None
    m = TAIL.match(ins.op_str)
    if m:
        slot = int(m.group(1) or "0", 0) // 4
    elif ins.op_str in ("eax", "edx") and prev:
        p = prev[-1]
        m2 = re.match(r"^e[a-d]x, dword ptr \[e[a-d]x(?: \+ (0x[0-9a-f]+|\d+))?\]$", p.op_str)
        if p.mnemonic == "mov" and m2:
            slot = int(m2.group(1) or "0", 0) // 4
    if slot is None:
        return None
    obj = "this"
    for p in reversed(prev):
        if p.mnemonic == "mov" and p.op_str.startswith("ecx, "):
            f = FIELD.match(p.op_str)
            obj = f"+{int(f.group(1) or '0', 0):#x}" if f else ("this" if p.op_str == "ecx, esi" else "?")
            break
    return obj, slot


def body_sig(rva, funcs, pe, md, depth=0):
    """(words or None, kind, note, tail) for one function body; tail = (object, slot) for a body that ends in a tail
    call through a vtable without a ret of its own (its arguments are the callee's)."""
    if rva in _cache:
        return _cache[rva]
    start = int(rva, 16)
    size = funcs.get(rva, {}).get("size", 0) or 64
    rets, floats, note, tail, direct = set(), False, "", None, None
    prev = []
    count = 0
    adjust = 0
    for ins in md.disasm(pe.get_data(start, size + 4096), start):
        count += 1
        # An adjustor thunk: `add ecx, -8` / `sub ecx, 8` / `lea ecx, [ecx - 8]` before a jump.
        if count == 1 + adjust and ins.mnemonic in ("add", "sub", "lea") and ins.op_str.startswith("ecx, "):
            adjust += 1
            prev.append(ins)
            continue
        if count == 1 + adjust and ins.mnemonic == "jmp" and ins.op_str.startswith("0x"):
            if depth < 4:
                res = body_sig(hex(int(ins.op_str, 16)), funcs, pe, md, depth + 1)
                _cache[rva] = (res[0], res[1], "thunk " + hex(int(ins.op_str, 16)) + (" " + res[2] if res[2] else ""),
                               res[3])
                return _cache[rva]
        if ins.mnemonic == "jmp" and not ins.op_str.startswith("0x"):
            t = tail_of(ins, prev)
            if t and not tail:
                tail = t
        elif ins.mnemonic == "jmp" and not (start <= int(ins.op_str, 16) < start + size) and depth < 4 and not direct:
            direct = hex(int(ins.op_str, 16))  # a tail call to another function: its arguments are this one's
        if ins.mnemonic == "ret":
            rets.add(int(ins.op_str, 16) // 4 if ins.op_str else 0)
            for p in reversed(prev):
                if p.mnemonic.startswith(("fld", "fild")):
                    floats = True
                    break
                if p.mnemonic.startswith(SKIP_BEFORE_RET) and not p.mnemonic.startswith("fstp"):
                    continue
                break
        prev.append(ins)
        prev = prev[-6:]
        end = ins.address + ins.size - start
        if end >= size and ins.mnemonic in ("ret", "jmp", "int3"):
            break
    if not rets and direct:
        r = body_sig(direct, funcs, pe, md, depth + 1)
        if r[0] is not None:
            rets.add(r[0])
            floats = r[1] == "float"
            note = f"tail call {direct}"
    if rets:
        tail = None
    if tail:
        res = (None, "?", f"tail call: {tail[0]} slot {tail[1]}", tail)
    elif not rets:
        res = (None, "?", "no ret found", None)
    elif len(rets) > 1:
        res = (max(rets), "float" if floats else "int", "rets " + "/".join(map(str, sorted(rets))), None)
    else:
        res = (rets.pop(), "float" if floats else "int", note, None)
    _cache[rva] = res
    return res


def resolve(entries, slot, funcs, pe, md, fields, seen=()):
    """A slot's (words, kind, note) in one vtable, following tail calls on `this` within the same vtable and tail calls
    on a field through the --field interface's consensus."""
    words, kind, note, tail = body_sig(entries[slot], funcs, pe, md)
    if not tail or slot in seen:
        return words, kind, note
    obj, target = tail
    if obj == "this" and target < len(entries):
        w, k, n = resolve(entries, target, funcs, pe, md, fields, seen + (slot,))
        return w, k, f"tail -> this slot {target}" + (f" ({n})" if n else "")
    if obj in fields:
        sig = fields[obj].get(target)
        if sig:
            return sig[0], sig[1], f"tail -> {obj} slot {target}"
    return None, "?", note




def implementers(name, classes, own=False):
    """(class, vtable rva, entries) for every vtable implementing `name` at its subobject offset."""
    tables = []
    if own:
        c = classes.get(name) or sys.exit(f"no class {name}")
        return [(c["name"], vt["rva"], vt["entries"]) for vt in c["vftables"]]
    for c in classes.values():
        if name not in c["bases"] and c["name"] != name:
            continue
        off = 0 if c["name"] == name else c["base_offsets"][c["bases"].index(name)]
        for vt in c["vftables"]:
            if vt["offset"] == off and any(e != PURECALL for e in vt["entries"]):
                tables.append((c["name"], vt["rva"], vt["entries"]))
    return tables


def consensus(tables, lo, hi, funcs, pe, md, fields):
    """Per slot in lo..hi: (slot, Counter of (words, kind), Counter of bodies, a note of the commonest body)."""
    rows = []
    for s in range(lo, hi + 1):
        variants, bodies, notes = Counter(), Counter(), {}
        for _, _, entries in tables:
            if s >= len(entries) or entries[s] == PURECALL:
                continue
            words, kind, note = resolve(entries, s, funcs, pe, md, fields)
            variants[(words, kind)] += 1
            bodies[entries[s]] += 1
            notes.setdefault(entries[s], note)
        # Unknown answers (no ret, unresolved tail) don't disagree with a known one.
        known = Counter({k: v for k, v in variants.items() if k[0] is not None})
        rows.append((s, known or variants, bodies, notes))
    return rows


FX_BY_TYPE = {"f32": "f", "bool": "b", "u8": "b", "i8": "b", "char*": "s", "wchar*": "w", "cIGZString*": "S",
              "u32": "u", "i32": "u", "u16": "u", "i16": "u"}
# Where drawfx's undeclared-argument error sends the person declaring a kind (drawfx::Iface::declare).
FX_DECLARE = ("in the db/methods fact's params (a param's fx key, or its type; tools/ghidra.py annotate, then "
              "tools/slotsig.py --header regenerates tests/fx_slots.h)")


def method_facts():
    """{(iface, slot): fact} from db/methods/*.jsonl."""
    facts = {}
    for path in sorted((ROOT / "db/methods").glob("*.jsonl")):
        for line in open(path, encoding="utf-8"):
            if line.strip():
                f = json.loads(line)
                if "slot" in f:
                    facts[(f["iface"], f["slot"])] = f
    return facts


SRC_TYPES = {"uint32_t": "u32", "int32_t": "i32", "uint16_t": "u16", "int16_t": "i16", "uint8_t": "u8", "int8_t": "i8",
             "bool": "bool", "float": "f32", "char": "char", "wchar_t": "wchar", "int": "i32", "unsigned": "u32",
             "size_t": "u32", "va_list": "void*"}


def src_type(t):
    """A C++ parameter type from our headers as a fact type: `const char *` char*, `const cIGZString &` cIGZString*
    (a reference is a pointer on the stack), integers by width; None for a struct passed by value."""
    t = re.sub(r"\b(const|volatile|struct|class)\b", "", t).replace("&", "*")
    stars = t.count("*")
    base = t.replace("*", "").strip().split("::")[-1]
    if base == "va_list":
        return "void*"
    if base not in SRC_TYPES and not stars:
        return None
    return SRC_TYPES.get(base, base) + "*" * stars


def src_decl_facts(header_path, iface):
    """{slot: fact} from our own declaration of an interface in src/ (`virtual R Name(params) = 0; // slot N`), its
    bases' declarations included: for hand-written interfaces that can't have db/methods facts (ifacegen would generate
    a second class of the same name, notes/feedback/CN.md). A declaration without a slot comment, with varargs or a
    struct by value is left out (its arguments stay undeclared)."""
    src = ROOT / "src"
    text = (ROOT / header_path).read_text(encoding="utf-8")
    m = re.search(r"\b(?:class|struct)\s+" + re.escape(iface) + r"\b\s*(?::\s*public\s+([\w:]+))?\s*\{", text)
    if m is None:
        sys.exit(f"{header_path}: no definition of {iface}")
    out = {}
    if m.group(1):
        base = m.group(1).split("::")[-1]
        where = next((p for p in sorted(src.rglob("*.h"))
                      if re.search(r"\b(?:class|struct)\s+" + base + r"\b[^;{]*\{", p.read_text(encoding="utf-8"))), None)
        if where is None:
            sys.exit(f"{header_path}: {iface}'s base {base} has no definition in src/")
        out.update(src_decl_facts(where.relative_to(ROOT).as_posix(), base))
    depth, i = 1, m.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        i += 1
    decl = re.compile(r"virtual\s+([^;(]*?)\b(operator\s*=|\w+)\s*\(([^()]*)\)[^;]*;[ \t]*//\s*slot\s+(\d+)")
    for d in decl.finditer(text[m.end():i - 1]):
        params, slot = d.group(3).strip(), int(d.group(4))
        if "..." in params:
            continue
        types = []
        for p in ([] if params in ("", "void") else params.split(",")):
            p = p.split("=")[0].strip()
            named = re.match(r"^(.*?[\s*&])(\w+)$", p)
            types.append(src_type(named.group(1) if named else p))
        if None in types:
            continue
        name = "Assign" if d.group(2).startswith("operator") else d.group(2)
        out[slot] = {"iface": iface, "slot": slot, "name": name, "params": [{"type": t} for t in types],
                     "ret": "f32" if re.search(r"\bfloat\s*$", d.group(1)) else "u32"}
    return out


def fx_spec(fact):
    """drawfx's argument kinds for a method fact: a param's own "fx" key, else by type (f32 'f', bool/u8 'b', char* 's',
    wchar* 'w', cIGZString* 'S', other integers 'u' data, any other pointer 'a' a strict pointer), else 'i' (undeclared:
    drawfx fails the test when the argument is not zero). Never by the values a test passes (tests/norm_words.h)."""
    out = []
    for p in fact.get("params", []):
        t = p["type"].replace(" ", "")
        out.append(p.get("fx") or FX_BY_TYPE.get(t) or ("a" if t.endswith("*") else "i"))
    return "".join(out)


def header(ifaces, path, classes, funcs, pe, md):
    """Writes the drawfx slot tables (tests/fx_slots.h) for the named interfaces: arity and return kind from the bodies
    (slotsig), names and argument kinds from the db/methods facts. A fact settles a slot the bodies can't (its param
    count), and a fact that disagrees with the bodies is reported and the bodies win."""
    facts = method_facts()
    names_db = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    out = ["#pragma once",
           "// Generated by tools/slotsig.py --header " + " ".join(ifaces) + ": do not edit. Arity and return kind of",
           "// every slot from the implementing classes' bodies (`ret N`, x87 returns), names and argument kinds from",
           "// db/methods. Regenerate after recording method facts (tools/ghidra.py annotate), see docs/ui-testing.md.",
           '#include "drawfx_core.h"', "", "namespace fx_slots {", "using drawfx::Slot;", "using drawfx::Ret;",
           f'inline constexpr const char *kDeclare = "{FX_DECLARE}";', ""]
    problems = []
    for spec in ifaces:
        spec, _, decl_header = spec.partition(":src/")
        name, _, fieldspec = spec.partition("@")
        decl = src_decl_facts("src/" + decl_header, name) if decl_header else None
        if decl is not None and any(k[0] == name for k in facts):
            sys.exit(f"{name}: declared both in src/{decl_header} and in db/methods; use one")
        declare = "kDeclare"
        if decl is not None:
            declare = "kDeclare" + re.sub(r"\W", "_", name)
            out.append(f'inline constexpr const char *{declare} = "in our declaration, src/{decl_header} (`// slot N` '
                       f'comments; tools/slotsig.py --header regenerates tests/fx_slots.h)";')
            facts.update({(name, s): f for s, f in decl.items()})
        fields = {}
        for fs in filter(None, fieldspec.split(";")):
            off, _, other = fs.partition("=")
            ot = implementers(other, classes)
            rows = consensus(ot, 0, max(len(t[2]) for t in ot) - 1, funcs, pe, md, {})
            fields[f"+{int(off, 0):#x}"] = {s: v.most_common(1)[0][0] for s, v, _, _ in rows
                                             if v and v.most_common(1)[0][0][0] is not None}
        tables = [t for t in implementers(name, classes) if t[0] != name]
        own_pure = None
        for vt in classes.get(name, {}).get("vftables", []):
            run = next((i for i, e in enumerate(vt["entries"]) if e != PURECALL), len(vt["entries"]))
            own_pure = run or own_pure
        n = min([len(t[2]) for t in tables] + ([own_pure] if own_pure else []))
        rows = consensus(tables, 0, n - 1, funcs, pe, md, fields)
        ident = re.sub(r"\W", "_", name.split("::")[-1])
        out.append(f"// {name}: {n} slots from {len(tables)} implementing vtables")
        out.append(f"inline constexpr Slot k{ident}Slots[] = {{")
        for s, variants, bodies, notes in rows:
            fact = facts.get((name, s)) or facts.get((name.split("::")[-1], s))
            known = [(w, k) for (w, k) in variants if w is not None]
            words, kind = (variants.most_common(1)[0][0] if known else (None, "?"))
            settled = len(variants) == 1 and words is not None
            if fact is not None:
                fw = len(fact.get("params", []))
                if words is None:
                    words, settled = fw, True
                    kind = "float" if fact.get("ret") == "f32" else "int"
                elif fw != words:
                    problems.append(f"{name} slot {s}: the fact {fact['name']} has {fw} params, the bodies {words}")
            fx = fx_spec(fact) if fact is not None and len(fact.get("params", [])) == words else ""
            mname = fact["name"] if fact is not None else ""
            top = bodies.most_common(1)[0][0] if bodies else ""
            comment = f"{s} {top} {names_db.get(top, '')[:40]}".rstrip()
            if not settled:
                comment += " UNSETTLED " + (", ".join(f"{w}w/{k} x{c}" for (w, k), c in variants.items()) or "no body")
            out.append(f'    {{{words or 0}, Ret::{"Float" if kind == "float" else "Int"}, {str(settled).lower()}, '
                       f'"{mname}", "{fx}"}}, // {comment}')
        out.append("};")
        out.append(f'inline constexpr drawfx::Iface {ident}{{"{name}", {n}, k{ident}Slots, false, {declare}}};')
        out.append("")
    out.append("} // namespace fx_slots")
    Path(path).write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")
    for p in problems:
        print("warning:", p)
    print(f"wrote {path}: {len(ifaces)} interfaces")


def main():
    if "--header" in sys.argv:
        i = sys.argv.index("--header")
        path, ifaces = sys.argv[i + 1], sys.argv[i + 2:]
        import pefile
        from capstone import CS_ARCH_X86, CS_MODE_32, Cs
        pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
        header(ifaces, path, {c["name"]: c for c in tsdb.jsonl("classes.jsonl")},
               {f["rva"]: f for f in tsdb.functions()}, pe, Cs(CS_ARCH_X86, CS_MODE_32))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("iface")
    ap.add_argument("--slots", help="a range, e.g. 3-48")
    ap.add_argument("--all", action="store_true", help="every slot any implementer has")
    ap.add_argument("--class", dest="own", action="store_true", help="the named class's own vtables only")
    ap.add_argument("--field", action="append", default=[], metavar="OFF=IFACE",
                    help="resolve tail calls on the object at this+OFF through IFACE's slots (e.g. 0x18=cIGZDrawContextEngine)")
    ap.add_argument("--cpp", metavar="NAME", help="print a C++ initialiser for drawfx::SlotSig NAME[]")
    args = ap.parse_args()

    import pefile
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    classes = {c["name"]: c for c in tsdb.jsonl("classes.jsonl")}
    funcs = {f["rva"]: f for f in tsdb.functions()}
    names = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}

    fields = {}
    for spec in args.field:
        off, _, other = spec.partition("=")
        ot = implementers(other, classes)
        rows = consensus(ot, 0, max(len(t[2]) for t in ot) - 1, funcs, pe, md, {})
        fields[f"+{int(off, 0):#x}"] = {s: v.most_common(1)[0][0] for s, v, _, _ in rows
                                         if v and v.most_common(1)[0][0][0] is not None}

    tables = implementers(args.iface, classes, args.own)
    if not tables:
        sys.exit(f"no vtables for {args.iface}")
    # The interface's own (pure) vtable, when the image has one: its run of _purecall entries is the interface's length
    # (the table extracted for it can run on into the next vtable).
    own_pure = None
    if not args.own and args.iface in classes:
        for vt in classes[args.iface]["vftables"]:
            run = next((i for i, e in enumerate(vt["entries"]) if e != PURECALL), len(vt["entries"]))
            if run:
                own_pure = run
        tables = [t for t in tables if t[0] != args.iface]
    if not tables:
        sys.exit(f"no implementing vtables for {args.iface}")
    shortest = min(len(t[2]) for t in tables)
    if own_pure:
        shortest = min(shortest, own_pure)
    longest = max(len(t[2]) for t in tables)
    n = longest if args.all or args.own else shortest
    lo, hi = 0, n - 1
    if args.slots:
        a, _, b = args.slots.partition("-")
        lo, hi = int(a), min(int(b or a), n - 1)
    rows = consensus(tables, lo, hi, funcs, pe, md, fields)

    if args.cpp:
        print(f"// {args.iface}: {len(tables)} implementing vtables, {n} slots (tools/slotsig.py {args.iface} "
              f"{' '.join('--field ' + f for f in args.field)} --cpp {args.cpp})")
        print(f"inline constexpr SlotSig {args.cpp}[] = {{")
        for s, variants, bodies, notes in rows:
            if not variants:
                print(f"    {{0, kInt, false}}, // {s}: pure only")
                continue
            (words, kind), cnt = variants.most_common(1)[0]
            agreed = len(variants) == 1 and words is not None
            top = bodies.most_common(1)[0][0]
            print(f"    {{{words if words is not None else 0}, {'kFloat' if kind == 'float' else 'kInt'}, "
                  f"{str(agreed).lower()}}}, // {s} {top} {names.get(top, '')[:50]}"
                  + ("" if agreed else " UNSETTLED " + ", ".join(f"{w}w/{k} x{c}" for (w, k), c in variants.items())))
        print("};")
        return

    print(f"{args.iface}: {len(tables)} vtables ({', '.join(sorted({t[0] for t in tables})[:6])}"
          f"{', ...' if len(tables) > 6 else ''}); slots {shortest}..{longest}, shown {lo}-{hi}")
    for s, variants, bodies, notes in rows:
        if not variants:
            print(f"  [{s:3}] pure only")
            continue
        (words, kind), cnt = variants.most_common(1)[0]
        flag = "" if len(variants) == 1 else "  DISAGREE " + ", ".join(f"{w}w/{k} x{c}" for (w, k), c in variants.items())
        top = bodies.most_common(1)[0][0]
        note = notes[top]
        print(f"  [{s:3}] {str(words) + 'w':>5} {kind:5} {len(bodies):3} bodies  e.g. {top} {names.get(top, '')[:50]}"
              f"{' (' + note + ')' if note else ''}{flag}")


if __name__ == "__main__":
    main()
