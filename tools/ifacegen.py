#!/usr/bin/env python3
"""Generates C++ interface headers for the game's vtables from the method facts in db/methods/*.jsonl.

Our code calls game objects through their vtables. Instead of a slot number
(`PCall<uint8_t>(holder, 9)`), code includes the generated interface and calls a named virtual
method (`As<ifaces::PersonInputHolder>(holder)->IsWaiting()`): an abstract class whose virtual
methods sit at the game's slot positions (placeholders fill the gaps), so MSVC emits the same
`call [eax+N]` the original makes. One fact per method; a name learned once improves every caller,
and the same facts name the vtable slots in the annotated Ghidra project (tools/annotate.py).

A fact (one per line, any file in db/methods/, normally one file per group):
  {"iface": "PersonInputHolder", "slot": 9, "name": "IsWaiting", "ret": "u8",
   "params": [{"type": "u32", "name": "flag"}], "tentative": true,
   "class": "cTSPerson", "vtable": 752, "source": "lane Y: Prim03 reads it before the loop"}
- iface: the interface name (a class from db/classes.jsonl, or a name for an object whose class
  isn't known yet; `class`/`vtable` record where its vtable lives when known).
- types: i8 u8 i16 u16 i32 u32 f32 bool void, `T*` (pointer; T may be another iface, else void*),
  or a C++ spelling in `cpp_ret` / per-parameter `cpp` when a project type fits better.
- inheritance: a line {"iface": "cTSPerson", "extends": "cTSObject", "class": "cTSPerson"} makes the
  derived interface start after the base's last slot; the base needs a `class` (any of its facts)
  so its full vtable size is known and padded.
- tentative: the name is a working guess (shown in the header comment; the name itself stays plain).
- aliases: a line {"iface": "NbrFramework", "alias_of": "cIGZCOM", "source": "..."} keeps an old name for an interface
  whose facts were merged into another: ifaces/NbrFramework.h is `using NbrFramework = cIGZCOM;`, so existing code
  compiles unchanged and `NbrFramework*` in a fact means `cIGZCOM *`. The old name has no facts of its own (move them,
  same slots, into the target first); src/ must not forward-declare `class NbrFramework;` (--check names the places).
  Merging: pick the RTTI name (cI*) where one exists, move each fact (resolve any slot both name differently, against
  the disassembly), add the alias line, run --check. `--check` warns about likely duplicates: unrelated interfaces on one
  class vtable, and interfaces naming three or more slots alike (not both RTTI names).
Calling convention is __thiscall (the game's virtuals); a slot used with another convention can't
be expressed here: keep VirtualSlot for it.

Usage: python -I tools/ifacegen.py [--db db] [--out build/generated/ifaces] [--check]
Writes <out>/<Iface>.h and <out>/ifaces.h (all of them); files are rewritten only when changed.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCALARS = {"i8": "int8_t", "u8": "uint8_t", "i16": "int16_t", "u16": "uint16_t", "i32": "int32_t",
           "u32": "uint32_t", "i64": "int64_t", "u64": "uint64_t", "f32": "float", "f64": "double", "bool": "bool",
           "char": "char", "wchar": "wchar_t", "void": "void"}
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def load(db):
    facts = []
    for path in sorted((db / "methods").glob("*.jsonl")):
        for n, line in enumerate(open(path, encoding="utf-8"), 1):
            if line.strip():
                f = json.loads(line)
                f["_where"] = f"{path.name}:{n}"
                facts.append(f)
    return facts


def cpp_type(t, ifaces):
    """ifaces: the interface names, or {name: name it resolves to} (an alias resolves to its target)."""
    if t.endswith("*"):
        inner = t[:-1].strip()
        if inner in ifaces:
            return f"{ifaces[inner] if isinstance(ifaces, dict) else inner} *"
        if inner in SCALARS and inner != "void":
            return f"{SCALARS[inner]} *"
        return "void *"
    if t not in SCALARS:
        raise ValueError(f"unknown type {t!r} (use cpp/cpp_ret for project types)")
    return SCALARS[t]


def holder(f):
    """Where a fact lives and who recorded it: `x.jsonl:3 (Name, slot 4, lane AD, tentative)`."""
    who = ", ".join(x for x in (f.get("name"), f"slot {f.get('slot')}", f"lane {f['lane']}" if f.get("lane") else "",
                                "tentative" if f.get("tentative") else "confirmed") if x)
    return f"{f['_where']} ({who})"


def aliases_of(facts):
    """{old interface name: the interface it is another name for} from the alias lines."""
    return {f["iface"]: f["alias_of"] for f in facts if "alias_of" in f}


def check_aliases(facts):
    errors = []
    aliases = aliases_of(facts)
    defined = {f["iface"] for f in facts if "alias_of" not in f}
    seen = {}
    for f in facts:
        if "alias_of" not in f:
            continue
        w, old, new = f["_where"], f["iface"], f["alias_of"]
        extra = set(f) - {"iface", "alias_of", "source", "lane", "tentative", "_where"}
        if extra:
            errors.append(f"{w}: an alias line has only iface and alias_of (and source), not {', '.join(sorted(extra))}")
        if old in seen:
            errors.append(f"{w}: {old} is already an alias at {seen[old]}")
        seen[old] = w
        if old in defined:
            errors.append(f"{w}: {old} is an alias of {new} but still has facts: move them to {new} (same slots) first")
        if new in aliases:
            errors.append(f"{w}: {old} is an alias of {new}, itself an alias of {aliases[new]}: name the final interface")
        elif new not in defined:
            errors.append(f"{w}: {old} is an alias of {new}, which has no facts")
    return errors


def forward_declared_aliases(aliases, src):
    """`class Old;` in src/ next to the generated `using Old = New;` is a redefinition: name those places."""
    if not aliases:
        return []
    pat = re.compile(r"\bclass\s+(" + "|".join(map(re.escape, sorted(aliases))) + r")\s*;")
    out = []
    for p in sorted(Path(src).rglob("*")):
        if p.suffix in (".h", ".hpp", ".cpp") and p.is_file():
            for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                m = pat.search(line)
                if m:
                    out.append(f"{p.relative_to(Path(src).parent).as_posix()}:{n}: forward-declares class {m.group(1)}, now an "
                               f"alias of {aliases[m.group(1)]}: declare `class {aliases[m.group(1)]};` instead")
    return out


def ancestry(iface, extends):
    out = [iface]
    while out[-1] in extends and extends[out[-1]] not in out:
        out.append(extends[out[-1]])
    return out


def duplicate_warnings(facts, rtti=()):
    """Interfaces that look like one interface under two names: they share a class and vtable without one extending the
    other, or name three or more of the same slots alike past IUnknown's three (unless both are RTTI names, `rtti`: distinct
    interfaces of the game that share a base). Facts without a `class` are found only by the second test. [warning text]"""
    extends = {f["iface"]: f["extends"] for f in facts if "extends" in f}
    methods = [f for f in facts if "slot" in f and "extends" not in f and "alias_of" not in f]
    by_table, slots = {}, {}
    for f in methods:
        if f.get("class"):
            by_table.setdefault((f["class"], f.get("vtable", 0)), set()).add(f["iface"])
        slots.setdefault(f["iface"], set()).add((f["slot"], f["name"]))
    out, reported = [], set()
    for (cls, vt), ifaces in sorted(by_table.items()):
        # A chain of interfaces (base, derived, more derived) is one vtable by design; anything else on it is a duplicate.
        groups = []
        for i in sorted(ifaces):
            related = [g for g in groups if any(j in ancestry(i, extends) or i in ancestry(j, extends) for j in g)]
            merged = {i}.union(*related)
            groups = [g for g in groups if g not in related] + [merged]
        if len(groups) > 1:
            names = sorted(" < ".join(sorted(g, key=lambda n: -len(ancestry(n, extends)))) for g in groups)
            out.append(f"{cls} vtable +0x{vt:x} has {len(groups)} unrelated interfaces: {', '.join(names)} "
                       "(one object under several names? merge the facts into one interface and alias the others)")
            reported.add(frozenset(ifaces))
    names = sorted(slots)
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            same = sorted(s for s in slots[a] & slots[b] if s[0] >= 3)
            if len(same) >= 3 and not any({a, b} <= r for r in reported) and not (a in rtti and b in rtti) and \
                    b not in ancestry(a, extends) and a not in ancestry(b, extends):
                out.append(f"{a} and {b} name {len(same)} slots alike ({', '.join(f'{s}:{n}' for s, n in same[:4])}"
                           f"{', ...' if len(same) > 4 else ''}): one interface under two names?")
    return out


def check(facts, classes):
    errors = check_aliases(facts)
    facts = [f for f in facts if "extends" not in f and "alias_of" not in f]
    seen, names = {}, {}
    for f in facts:
        w = f["_where"]
        for k in ("iface", "slot", "name", "ret", "params"):
            if k not in f:
                errors.append(f"{w}: missing {k}")
        if errors and errors[-1].startswith(w):
            continue
        if not IDENT.match(f["iface"]) or not IDENT.match(f["name"]):
            errors.append(f"{w}: iface and name must be C++ identifiers")
        key = (f["iface"], f["slot"])
        if key in seen:
            errors.append(f"{w}: {f['iface']} slot {f['slot']} already defined at {holder(seen[key])}")
        seen[key] = f
        nkey = (f["iface"], f["name"])
        if nkey in names:
            errors.append(f"{w}: {f['iface']}::{f['name']} already used at {holder(names[nkey])} (no overloads)")
        names[nkey] = f
        cls = f.get("class")
        if cls:
            tables = classes.get(cls)
            if tables is None:
                errors.append(f"{w}: class {cls} not in db/classes.jsonl")
            else:
                size = tables.get(f.get("vtable", 0))
                if size is None:
                    errors.append(f"{w}: {cls} has no vftable at offset {f.get('vtable', 0)}")
                elif f["slot"] >= size:
                    errors.append(f"{w}: slot {f['slot']} beyond {cls}'s {size}-slot vftable")
    return errors


def vtable_size(methods, classes):
    for m in methods:
        if m.get("class") and m.get("vtable", 0) in classes.get(m["class"], {}):
            return classes[m["class"]][m.get("vtable", 0)]
    return None


def render(iface, methods, ifaces, base=None, first=0, last=None):
    """The header for `iface`: slots first..last (last defaults to the highest fact), named where a
    fact exists, placeholders elsewhere; `base` is the interface it extends (slots below `first`)."""
    by_slot = {m["slot"]: m for m in methods}
    if last is None:
        last = max(by_slot) if by_slot else first - 1
    where = sorted({(m.get("class"), m.get("vtable", 0)) for m in methods if m.get("class")})
    resolve = (lambda n: ifaces[n]) if isinstance(ifaces, dict) else (lambda n: n)  # noqa: E731
    deps = sorted({resolve(t[:-1].strip()) for m in methods for t in [m["ret"]] + [p["type"] for p in m["params"]]
                   if t.endswith("*") and t[:-1].strip() in ifaces and resolve(t[:-1].strip()) != iface})
    # Project types named as `struct X` in cpp/cpp_ret: declared at global scope, used as ::X.
    project = sorted({t for m in methods for c in [m.get("cpp_ret", "")] + [p.get("cpp", "") for p in m["params"]]
                      for t in re.findall(r"struct (\w+)", c)})
    lines = ["// Generated by tools/ifacegen.py from db/methods/*.jsonl. Do not edit; change the facts.",
             "#pragma once", "#include <cstdint>"]
    if base:
        lines.append(f'#include "ifaces/{base}.h"')
    lines += [""] + [f"struct {t};" for t in project]
    lines += ["", "namespace ifaces {", ""]
    lines += [f"class {d};" for d in deps] + ([""] if deps else [])
    if where:
        lines.append("// vtable of " + ", ".join(f"{c} at +0x{o:x}" for c, o in where))
    lines += [f"class {iface}" + (f" : public {base}" if base else "") + " {", "public:"]
    for slot in range(first, last + 1):
        m = by_slot.get(slot)
        if m is None:
            lines.append(f"    virtual void vf{slot}_{iface}() = 0;")
            continue
        glob = lambda c: re.sub(r"struct (\w+)", r"::\1", c)
        ret = glob(m.get("cpp_ret") or cpp_type(m["ret"], ifaces))
        params = ", ".join(f"{glob(p.get('cpp') or cpp_type(p['type'], ifaces))} {p['name']}".replace("* ", "*")
                           for p in m["params"])
        note = f"slot {slot}" + (", tentative" if m.get("tentative") else "")
        if m.get("source"):
            note += ": " + m["source"]
        lines.append(f"    // {note}")
        lines.append(f"    virtual {ret} {m['name']}({params}) = 0;")
    lines += ["};", "", "} // namespace ifaces", ""]
    return "\n".join(lines)


def render_alias(old, new):
    """The header for an old interface name kept as an alias: existing code keeps compiling, new code uses `new`."""
    return "\n".join(["// Generated by tools/ifacegen.py from db/methods/*.jsonl. Do not edit; change the facts.",
                      "#pragma once", f'#include "ifaces/{new}.h"', "", "namespace ifaces {", "",
                      f"// {old} is another name for {new} (an alias_of line in db/methods/): use {new} in new code.",
                      f"using {old} = {new};", "", "} // namespace ifaces", ""])


def build(facts, classes):
    """{iface: header text}. Raises ValueError for facts that can't be laid out."""
    aliases = aliases_of(facts)
    extends = {f["iface"]: aliases.get(f["extends"], f["extends"]) for f in facts if "extends" in f}
    groups = {}
    for f in facts:
        if "alias_of" in f:
            continue
        groups.setdefault(f["iface"], [])
        if "extends" not in f:
            groups[f["iface"]].append(f)
    names = {n: n for n in groups}
    names.update(aliases)  # a type `Old*` in a fact means the target interface
    out = {}
    for old, new in aliases.items():
        out[old] = render_alias(old, new)
    for iface, methods in groups.items():
        base = extends.get(iface)
        first = 0
        if base:
            first = vtable_size(groups.get(base, []), classes) or 0
            if not first:
                raise ValueError(f"{iface} extends {base}, whose vtable size isn't known (give one of its facts a class)")
            low = [m["slot"] for m in methods if m["slot"] < first]
            if low:
                raise ValueError(f"{iface}: slot {low[0]} is below {first}, i.e. {base}'s")
        last = None
        if iface in extends.values():
            # Extended by another interface: padded to the full vtable so the derived slots line up.
            last = (vtable_size(methods, classes) or 0) - 1
        out[iface] = render(iface, methods, names, base, first, last)
    return out


def load_classes(db):
    classes = {}
    for c in (json.loads(l) for l in open(Path(db) / "classes.jsonl", encoding="utf-8") if l.strip()):
        classes[c["name"]] = {t["offset"]: len(t["entries"]) for t in c["vftables"]}
    return classes


def ledger_facts(files):
    """The method facts of an annotate ledger ({"methods/x.jsonl": text, ...}), as load() returns them from db/."""
    facts = []
    for name in sorted(files):
        if name.startswith("methods/"):
            for n, line in enumerate(files[name].split("\n"), 1):
                if line.strip():
                    f = json.loads(line)
                    f["_where"] = f"{name[len('methods/'):]}:{n}"
                    facts.append(f)
    return facts


def problems(facts, classes):
    """Everything --check reports, line numbers dropped (so two versions of the files compare)."""
    errors = check(facts, classes)
    if not errors:
        try:
            build(facts, classes)
        except ValueError as e:
            errors.append(str(e))
    return {re.sub(r"(\.jsonl):\d+", r"\1", e) for e in errors}


def refusals(old_files, new_files, entries, classes):
    """What `ghidra.py annotate` refuses before recording: for each new method fact (file, rec) in entries, a fact for the
    same interface slot or name held in another file (naming its holder), then any other --check problem the new ledger has
    and the old one hadn't. [] when the change is fine."""
    facts = ledger_facts(new_files)
    out = []
    for file, rec in entries:
        if not file.startswith("methods/") or "extends" in rec or "alias_of" in rec:
            continue
        group = file[len("methods/"):]
        for f in facts:
            if f["iface"] != rec["iface"] or "extends" in f or "alias_of" in f:
                continue
            here = f["_where"].rsplit(":", 1)[0] == group
            if f.get("slot") == rec["slot"] and not here:
                out.append(f"{rec['iface']} slot {rec['slot']} ({rec['name']}) is already held by {holder(f)}: one fact per "
                           f"interface slot; change that one (--rename/--delete {f['_where'].split('.jsonl')[0]}/{rec['iface']}:"
                           f"{rec['slot']}, or --apply it under methods/{f['_where'].split('.jsonl')[0]})")
            elif f.get("name") == rec["name"] and f.get("slot") != rec["slot"]:
                out.append(f"{rec['iface']}::{rec['name']} (slot {rec['slot']}) is already the name of {holder(f)}: no overloads")
    if out:
        return out
    before = problems(ledger_facts(old_files), classes)
    return sorted(p for p in problems(facts, classes) if p not in before)


def write(path, text):
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--db", default=str(ROOT / "db"))
    ap.add_argument("--out", default=str(ROOT / "build" / "generated" / "ifaces"))
    ap.add_argument("--check", action="store_true", help="validate the facts only")
    args = ap.parse_args()
    db = Path(args.db)
    facts = load(db)
    classes = load_classes(db)
    errors = check(facts, classes) + forward_declared_aliases(aliases_of(facts), ROOT / "src")
    rendered = {}
    if not errors:
        try:
            rendered = build(facts, classes)
        except ValueError as e:
            errors.append(str(e))
    for e in errors:
        print("error:", e)
    if errors:
        return 1
    if args.check:
        warnings = duplicate_warnings(facts, set(classes))
        for w in warnings:
            print("warning:", w)
        n_alias = len(aliases_of(facts))
        print(f"{len(facts)} facts in {len(rendered) - n_alias} interfaces" + (f" (+{n_alias} aliases)" if n_alias else "") +
              f": ok" + (f", {len(warnings)} warning(s)" if warnings else ""))
        return 0
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    changed = sum(write(out / f"{i}.h", t) for i, t in rendered.items())
    all_h = "// Generated by tools/ifacegen.py. Do not edit.\n#pragma once\n" + \
            "".join(f'#include "ifaces/{i}.h"\n' for i in sorted(rendered))
    changed += write(out / "ifaces.h", all_h)
    for stale in out.glob("*.h"):
        if stale.stem not in rendered and stale.name != "ifaces.h":
            stale.unlink()
            changed += 1
    (out / "ifaces.stamp").write_text(f"{len(facts)}\n", encoding="utf-8")
    print(f"{len(facts)} facts in {len(rendered)} interfaces -> {out} ({changed} files changed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
