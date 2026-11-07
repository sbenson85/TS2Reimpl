#!/usr/bin/env python3
"""Lists the vtable calls by slot number in our C++ and the method facts that could name them: a retrofit lane's checklist
for turning `VirtualCall<Fn, 23>(p, ...)` / `Call<T>(p, 23, ...)` into `As<ifaces::X>(p)->Name(...)`.

  tools/py -I tools/slotcalls.py src/ts/cTSFamilyManager.cpp [more files] [--all] [--json] [--max N]
  tools/py -I tools/slotcalls.py src --summary     # per file, most nameable first (directories expand to .cpp/.h)

Forms found (the ones tools/guard.py counts, plus VirtualSlot): VirtualCall<Fn, N>(obj, ...), Call<T>(obj, N, ...) /
Call(obj, N, ...) / PCall, VirtualSlot<Fn>(obj, N)(...), NarrowCall<N>(obj, ...), static_cast<NarrowSlot<N, R, A...> *>(obj)
->call(...), and calls of slot-call helpers: any template (in the file, a header it includes, or src/game/vtable.h's
RawSlot) that passes an integer template parameter on as the slot of one of those forms, e.g. a file-local
`template <int S, class R = uint32_t, class... A> R V(const void *object, A... a)`, called `V<29, const SkinKey *>(sim)`
(the form shown is the helper's name). The slot may be a number or a constant defined in the file or a header it
includes (`kSlot = 23`).

Per call: file:line, the form, the slot, the object expression, the argument count, and the candidates: facts in db/methods
at that slot, narrowed by what the receiver is known to be, best first:
  type   the object's declared type (`cIGZIStream *stream`, `ifaces::X *`) is an interface or a class with facts;
  this   `this` (or a member's address) in src/<area>/<Class>.cpp: the facts with that class;
  name   an interface whose name contains the object's identifier (stream -> cIGZIStream, NbrInStream);
  slot   otherwise every interface with a fact at that slot.
Candidates whose parameter count differs from the call's are dropped when some match (marked `arity?` when none do).
Each candidate: Iface::Name (file, tentative|confirmed). UNNAMED: no fact at that slot (name it first: annotate).
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ifacegen  # noqa: E402

FORMS = [
    ("VirtualCall", re.compile(r"\bVirtualCall<")),
    ("NarrowSlot", re.compile(r"\bstatic_cast<\s*NarrowSlot<")),
    ("NarrowCall", re.compile(r"\bNarrowCall<")),
    ("VirtualSlot", re.compile(r"\bVirtualSlot<")),
    ("Call", re.compile(r"\bP?Call(?=[<(])")),
]


def template_args(text, at):
    """(args, end) of the <...> starting at text[at] == '<' (angle brackets nest; parentheses inside are skipped whole)."""
    depth, args, cur, i = 0, [], [], at
    paren = 0
    while i < len(text):
        c = text[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        if paren == 0 and c == "<":
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif paren == 0 and c == ">":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i + 1
        elif paren == 0 and c == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return None, len(text)


def call_args(text, at):
    """(args, end) of the (...) starting at text[at] == '('."""
    depth, args, cur, i = 0, [], [], at
    while i < len(text):
        c = text[i]
        if c == "<" and i > at and (text[i - 1].isalnum() or text[i - 1] == "_"):
            # a template argument list (`V<3, void *>(x)`, `static_cast<A, B>`): its commas don't split arguments
            targs, j = template_args(text, i)
            if targs is not None and re.match(r"\s*(?:\(|::|\*|>|&)", text[j:j + 3]):
                cur.append(text[i:j])
                i = j
                continue
        if c in "([{":
            depth += 1
            if depth == 1:
                i += 1
                continue
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                piece = "".join(cur).strip()
                if piece or args:
                    args.append(piece)
                return args, i + 1
        elif c == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return None, len(text)


def constants(path, text=None, read=None):
    """{name: int} for `kName = N` (constexpr, enum, static const) in the file and the headers it includes."""
    out = {}
    text = read_file(path) if text is None else text
    for t in [text] + included(path, text, read):
        for name, val in re.findall(r"\b(k\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\b", t):
            out.setdefault(name, int(val, 0))
    return out


def slot_value(s, consts):
    s = s.strip()
    m = re.fullmatch(r"(?:static_cast<\w+>\()?\s*(?:(0x[0-9a-fA-F]+|\d+)[uUlL]*|(\w+(?:::\w+)*))\s*\)?", s)
    if not m:
        return None
    if m.group(1):
        return int(m.group(1), 0)
    v = m.group(2)
    return consts.get(v.split("::")[-1])


INT_TPARAM = re.compile(r"(?:const\s+)?(?:auto|int|unsigned(?:\s+int)?|size_t|u?int(?:8|16|32)_t)\s+(\w+)(?:\s*=.*)?", re.S)
BUILTIN = {name for name, _ in FORMS} | {"Call", "PCall"}


def helper_defs(text, known=None):
    """{name: helper} for the templates in `text` that pass one of their integer template parameters on as the slot of a
    VirtualCall / VirtualSlot / NarrowCall, or of another such helper (found first, in `known`): local slot-call helpers
    such as `template <int S, class R = uint32_t, class... A> R V(const void *object, A... a) { return VirtualCall<R(
    __thiscall *)(void *, A...), S>(object, a...); }`, `template <int Slot> uint32_t WordAt(void *object)`. Each call of
    one (`V<29, const SkinKey *>(sim)`) is a slot call like the forms in FORMS. A helper: its template parameter index of
    the slot, the receiver (index of the parameter passed on as the object, else the expression), the arguments passed
    on (`a...` expands the pack parameter), and its line."""
    helpers = dict(known or {})
    for _ in range(4):  # helpers built on helpers: until nothing new
        found = False
        for m in re.finditer(r"\btemplate\s*<", text):
            targs, i = template_args(text, m.end() - 1)
            if not targs:
                continue
            tparams = [(INT_TPARAM.fullmatch(p.strip()) or [None, None])[1] for p in targs]
            if not any(tparams):
                continue
            d = re.compile(r"[^;{}()=]*?\b(\w+)\s*\(").match(text, i)
            if not d or d.group(1) in helpers or d.group(1) in BUILTIN:
                continue
            params, j = call_args(text, d.end() - 1)
            b = re.compile(r"[\s\w]*\{").match(text, j)  # const / noexcept / __forceinline-free tail, then the body
            if params is None or not b:
                continue
            depth, k = 0, b.end() - 1
            while k < len(text):
                depth += {"{": 1, "}": -1}.get(text[k], 0)
                if depth == 0:
                    break
                k += 1
            body = text[b.end() - 1:k + 1]
            names, pack = [], None
            for p in params:
                pm = re.search(r"(\.\.\.)?\s*(\w+)\s*(?:=.*)?$", p.strip(), re.S)
                names.append(pm.group(2) if pm else None)
                if pm and pm.group(1):
                    pack = pm.group(2)
            consts = {t: ("tparam", n) for n, t in enumerate(tparams) if t}
            inner = [c for c in find_calls(body, consts, helpers) if isinstance(c["slot"], tuple)]
            if not inner:
                continue
            c = inner[0]
            obj = c["obj"]
            core = re.sub(r"^(?:\(\s*)?(?:(?:static|reinterpret|const)_cast<[^>]*>\()?\s*&?\s*(\w+)\s*\)*$", r"\1", obj)
            helpers[d.group(1)] = {"tindex": c["slot"][1], "obj_param": names.index(core) if core in names else None,
                                   "obj": obj, "inner": c["args"], "params": names, "pack": pack, "form": c["form"],
                                   "line": text.count("\n", 0, d.start(1)) + 1}
            found = True
        if not found:
            break
    return helpers


def helper_nargs(h, args):
    """The argument count the slot gets from a helper call with these arguments (None: unknown)."""
    if h["inner"] is None:
        return None
    n = 0
    for a in h["inner"]:
        if h["pack"] and re.fullmatch(rf"\s*{h['pack']}\s*\.\.\.\s*", a):
            n += max(len(args) - (len(h["params"]) - 1), 0)
        else:
            n += 1
    return n


_SHARED = None


def shared_helpers():
    """The slot-call helpers every file can use (src/game/vtable.h)."""
    global _SHARED
    if _SHARED is None:
        p = ROOT / "src" / "game" / "vtable.h"
        _SHARED = helper_defs(p.read_text(encoding="utf-8", errors="replace")) if p.exists() else {}
    return _SHARED


def file_helpers(path, text=None, read=None):
    """The slot-call helpers a source file can use: shared ones, those in the headers it includes (see included()), and
    its own."""
    text = read_file(path) if text is None else text
    helpers = dict(shared_helpers())
    for t in included(path, text, read):
        helpers = helper_defs(t, helpers)
    return helper_defs(text, helpers)


def read_file(p):
    p = Path(p)
    return p.read_text(encoding="utf-8", errors="replace") if p.is_file() else None


def included(path, text, read=None):
    """Texts of the `#include "..."` files of `text` found beside `path` or under the src/ it sits in. `read(Path)` gives
    a file's text or None (default: from disk; tools/guard.py reads a branch)."""
    read = read or read_file
    path = Path(path)
    bases = [path.parent] + [p for p in path.parents if p.name == "src"][:1]
    if len(bases) == 1:
        bases += [p for p in path.resolve().parents if p.name == "src"][:1]
    out = []
    for inc in re.findall(r'#include\s+"([^"]+)"', text):
        for base in bases:
            t = read(base / inc)
            if t is not None:
                out.append(t)
                break
    return out


def find_calls(text, consts, helpers=None):
    calls = []
    taken = set()
    forms = list(FORMS) + [(name, re.compile(rf"\b{re.escape(name)}\s*<")) for name in (helpers or {})]
    for form, pat in forms:
        for m in pat.finditer(text):
            if m.start() in taken:
                continue
            i = m.end()
            slot = obj = None
            nargs = rest = None
            if helpers and form in helpers:
                h = helpers[form]
                targs, i = template_args(text, text.index("<", m.start()))
                if not targs or len(targs) <= h["tindex"] or i >= len(text) or text[i] != "(":
                    continue
                args, _ = call_args(text, i)
                if args is None:
                    continue
                slot = slot_value(targs[h["tindex"]], consts)
                if h["obj_param"] is None:
                    obj = h["obj"]
                elif h["obj_param"] < len(args):
                    obj = args[h["obj_param"]]
                else:
                    continue
                nargs, rest = helper_nargs(h, args), [a for n, a in enumerate(args) if n != h["obj_param"]]
            elif form == "VirtualCall":
                targs, i = template_args(text, i - 1)
                if not targs or len(targs) < 2 or i >= len(text) or text[i] != "(":
                    continue
                args, _ = call_args(text, i)
                if not args:
                    continue
                slot, obj, nargs = slot_value(targs[-1], consts), args[0], len(args) - 1
                rest = args[1:]
            elif form == "NarrowSlot":
                targs, i = template_args(text, i - 1)
                if not targs:
                    continue
                k = text.find("(", i)
                args, j = call_args(text, k)
                if not args or not re.match(r"\s*->\s*call\s*\(", text[j:j + 20]):
                    continue
                slot, obj, nargs = slot_value(targs[0], consts), args[0], max(len(targs) - 2, 0)
            elif form == "NarrowCall":
                targs, i = template_args(text, i - 1)
                if not targs or i >= len(text) or text[i] != "(":
                    continue
                args, _ = call_args(text, i)
                if not args:
                    continue
                slot, obj, nargs = slot_value(targs[0], consts), args[0], len(args) - 1
                rest = args[1:]
            elif form == "VirtualSlot":
                targs, i = template_args(text, i - 1)
                if not targs or i >= len(text) or text[i] != "(":
                    continue
                args, j = call_args(text, i)
                if not args or len(args) != 2:
                    continue
                slot, obj = slot_value(args[1], consts), args[0]
                if j < len(text) and text[j] == "(":
                    more, _ = call_args(text, j)
                    nargs = len(more or []) - 1 if more else None
                    rest = (more or [])[1:] if more else None
            else:  # Call / PCall
                if text[i] == "<":
                    _, i = template_args(text, i)
                if i >= len(text) or text[i] != "(":
                    continue
                args, _ = call_args(text, i)
                if not args or len(args) < 2:
                    continue
                slot = slot_value(args[1], consts)
                if slot is None and not re.fullmatch(r"\s*\w+\s*", args[1]):
                    continue  # not a slot call (a template or helper named Call with other arguments)
                obj, nargs = args[0], len(args) - 2
                rest = args[2:]
            if slot is None:
                continue  # a helper's own definition (slot as a parameter), or a constant this tool can't resolve
            taken.add(m.start())
            calls.append({"pos": m.start(), "form": form, "slot": slot, "obj": " ".join(obj.split()), "nargs": nargs,
                          "args": rest})
    calls.sort(key=lambda c: c["pos"])
    return calls


def receiver(obj, text, pos, path, facts_by_iface, classes_with_facts):
    """(how, [iface names]) the object is known to be."""
    o = obj.strip()
    o = re.sub(r"^\(?\s*(?:static_cast|reinterpret_cast)<[^>]*>\((.*)\)\)?$", r"\1", o).strip()
    cls = path.stem.split(".")[0]
    if o == "this" or o == "&mVTable":
        hit = sorted(classes_with_facts.get(cls, ()))
        if hit:
            return "this", hit
    ident = re.sub(r"^[&*]+", "", o.split("->")[-1].split(".")[-1]).strip()
    ident = re.sub(r"\(\)$", "", ident)
    if re.fullmatch(r"\w+", ident):
        # the identifier's declared type, nearest declaration before the call
        decl = None
        for m in re.finditer(r"([A-Za-z_][\w:]*)\s*(?:const\s*)?\*\s*(?:const\s*)?" + re.escape(ident) + r"\b", text[:pos]):
            decl = m.group(1).split("::")[-1]
        if decl:
            if decl in facts_by_iface:
                return "type", [decl]
            if decl in classes_with_facts:
                return "type", sorted(classes_with_facts[decl])
        if len(ident) >= 4:
            key = ident.lower().lstrip("m")
            named = sorted(i for i in facts_by_iface if key and key in i.lower())
            if named:
                return "name", named
    return "slot", None


def by_method(cands):
    """Candidates grouped by method name, best first: [(name, [iface (~ = tentative)])]."""
    groups = {}
    for f in cands:
        groups.setdefault(f["name"], []).append(f)

    def rank(item):
        fs = item[1]
        return (not any(not f.get("tentative") for f in fs), not any(f["iface"].startswith("cI") for f in fs), -len(fs), item[0])
    out = []
    for name, fs in sorted(groups.items(), key=rank):
        fs = sorted(fs, key=lambda f: (not f["iface"].startswith("cI"), bool(f.get("tentative")), f["iface"]))
        out.append((name, [f["iface"] + ("~" if f.get("tentative") else "") for f in fs]))
    return out


def analyse(path, facts, text=None, read=None):
    """[call dict with line, by, candidates] for one file."""
    by_slot, facts_by_iface, classes_with_facts = facts
    text = read_file(path) if text is None else text
    out = []
    for c in find_calls(text, constants(path, text, read), file_helpers(path, text, read)):
        c["line"] = text.count("\n", 0, c["pos"]) + 1
        how, ifaces = receiver(c["obj"], text, c["pos"], path, facts_by_iface, classes_with_facts)
        pool = by_slot.get(c["slot"], [])
        cands = [f for f in pool if ifaces is None or f["iface"] in ifaces]
        if not cands and ifaces is not None:
            how, cands = "slot", pool
        if c["nargs"] is not None and cands:
            fit = [f for f in cands if len(f["params"]) == c["nargs"]]
            if fit:
                cands = fit
            else:
                how += ",arity?"
        c["by"], c["candidates"] = how, cands
        out.append(c)
    return out


def load_facts(db):
    """(by_slot, facts_by_iface, classes_with_facts) from db/methods, as analyse() takes them."""
    facts = [f for f in ifacegen.load(Path(db)) if "slot" in f and "alias_of" not in f and "extends" not in f]
    by_slot, facts_by_iface, classes_with_facts = {}, {}, {}
    for f in facts:
        by_slot.setdefault(f["slot"], []).append(f)
        facts_by_iface.setdefault(f["iface"], []).append(f)
        if f.get("class"):
            classes_with_facts.setdefault(f["class"], set()).add(f["iface"])
    return by_slot, facts_by_iface, classes_with_facts


def summary(c):
    """`Iface::Name` (or `Name: Iface, Iface`) of a call's candidates when the receiver is known and one method fits, else
    None: the name a raw slot call could take."""
    groups = by_method(c["candidates"])
    if len(groups) != 1 or c["by"].startswith("slot"):
        return None
    name, ifaces = groups[0]
    return (f"{ifaces[0].rstrip('~')}::{name}" + (" (tentative)" if all(i.endswith("~") for i in ifaces) else "")
            + (" (receiver guessed from its name: check its class)" if c["by"].startswith("name") else ""))


def file_counts(calls):
    """{"calls", "nameable" (summary() gives a name: receiver known, one method), "several", "unnamed"} for a file."""
    out = {"calls": len(calls), "nameable": 0, "several": 0, "unnamed": 0}
    for c in calls:
        groups = by_method(c["candidates"])
        if not groups:
            out["unnamed"] += 1
        elif summary(c):
            out["nameable"] += 1
        else:
            out["several"] += 1
    return out


def expand(names):
    """Files, with directories expanded to their .cpp / .h files (sorted)."""
    out = []
    for n in names:
        p = Path(n)
        out += sorted(q for q in p.rglob("*") if q.suffix in (".cpp", ".h")) if p.is_dir() else [p]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+", help="source files or directories (their .cpp / .h files)")
    ap.add_argument("--summary", action="store_true",
                    help="one line per file with slot calls, most nameable first: calls, nameable now (receiver known, "
                         "one method fact), several candidates, unnamed")
    ap.add_argument("--json", action="store_true", help="one JSON object per call")
    ap.add_argument("--max", type=int, default=3, help="method names shown per call (default 3; --all for every one)")
    ap.add_argument("--all", action="store_true", help="show every candidate")
    ap.add_argument("--group", action="store_true", help="one line per (object, slot) with its lines, instead of per call")
    ap.add_argument("--db", default=str(ROOT / "db"))
    a = ap.parse_args()
    facts = load_facts(Path(a.db))
    if a.summary:
        rows = [(p.as_posix(), file_counts(analyse(p, facts))) for p in expand(a.files)]
        rows = sorted((r for r in rows if r[1]["calls"]), key=lambda r: (-r[1]["nameable"], -r[1]["calls"], r[0]))
        for path, n in rows:
            print(f"{path}: {n['calls']} slot calls, {n['nameable']} nameable now, {n['several']} several candidates, "
                  f"{n['unnamed']} unnamed")
        total = {k: sum(n[k] for _, n in rows) for k in ("calls", "nameable", "several", "unnamed")}
        print(f"{len(rows)} files: {total['calls']} slot calls, {total['nameable']} nameable now (tools/slotcalls.py "
              f"<file> lists them), {total['several']} several candidates, {total['unnamed']} unnamed")
        return 0
    print("legend: s<slot> <object> (<args>) [how the receiver was narrowed] Method: Iface, Iface~ (~ tentative) | next method"
          if not a.json else "", end="\n" if not a.json else "")
    for path in expand(a.files):
        calls = analyse(path, facts)
        rel = path.as_posix()
        counts = {"one": 0, "several": 0, "unnamed": 0}
        rows = []
        for c in calls:
            groups = by_method(c["candidates"])
            counts["unnamed" if not groups else "one" if len(groups) == 1 else "several"] += 1
            if a.json:
                print(json.dumps({"file": rel, "line": c["line"], "form": c["form"], "slot": c["slot"], "object": c["obj"],
                                  "args": c["nargs"], "by": c["by"], "candidates": [
                                      {"iface": f["iface"], "name": f["name"], "file": f["_where"].split(":")[0],
                                       "tentative": bool(f.get("tentative"))} for f in c["candidates"]]}))
                continue
            shown = groups if a.all else groups[:a.max]
            text = " | ".join(f"{n}: {', '.join(i if a.all else i[:4])}{'' if a.all or len(i) <= 4 else f' +{len(i) - 4}'}"
                              for n, i in shown)
            if len(groups) > len(shown):
                text += f" | +{len(groups) - len(shown)} more"
            args = "" if c["nargs"] is None else f" ({c['nargs']})"
            what = f"[{c['by']}] {text}" if groups else "UNNAMED"
            rows.append((c, f"s{'?' if c['slot'] is None else c['slot']} {c['obj'][:36]}{args}", what))
        if a.json:
            continue
        if a.group:
            seen = {}
            for c, head, what in rows:
                seen.setdefault((head, what), []).append(c["line"])
            for (head, what), lines in seen.items():
                print(f"{rel}:{','.join(map(str, lines))}  {head}  -> {what}")
        else:
            for c, head, what in rows:
                print(f"{rel}:{c['line']}  {head}  -> {what}")
        print(f"{rel}: {len(calls)} slot call(s): {counts['one']} with one method name, {counts['several']} with several, "
              f"{counts['unnamed']} unnamed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
