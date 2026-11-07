"""The fact ledger behind `tools/ghidra.py annotate` and `export` (pure Python, no server needed).

The ledger is the text of db/'s hand-maintained fact files, verbatim: db/annotations.jsonl and db/methods/*.jsonl, as
{"annotations.jsonl": text, "methods/<group>.jsonl": text}. The annotated Ghidra program keeps it in its options (so a
saved project carries the facts it was built from), and tools/ghidra.py export writes it back to db/. Not in the
ledger, because they are derived or belong to other tools: db/names.jsonl, classes.jsonl, functions.jsonl (rtti.py, names.py,
Ghidra analysis), db/status, verification, original_calls and library matches (hand-written records of our own work).

Facts enter the ledger only through `annotate`: each fact is an entry (one JSON line, the format the file already has) plus
`lane` (who recorded it) and `tentative` (true when it is a guess; absent when confirmed). An entry replaces the line with the
same key, or is appended; field additions to an existing structure merge into its entry field by field. Every annotate is also
kept as an operation in the pseudo-file "_ops.jsonl" ({"lane","file","entry"}); `export --lane L` replays lane L's operations
on the checkout's own db/ files, so a branch carries exactly its own change, and a restart replays operations that db/ does not
contain yet. Replaying an operation twice changes nothing.
"""
import json
import re
from pathlib import Path

OPS = "_ops.jsonl"
HAND_FILES = ("annotations.jsonl", "methods/")
# Overlay sections annotate can record, and the db/annotations.jsonl kinds that go with them.
KINDS = ("struct", "fields", "union", "enum", "function", "name", "local", "global", "equate", "callsite", "flags")
REJECTED = ("comments", "inline", "classes", "impls", "compiled", "members", "missed", "resized")
REQUIRED = {"struct": ("name", "size", "fields"), "fields": ("name", "fields"), "union": ("name", "fields"),
            "enum": ("name", "values"), "function": ("rva", "name", "cc", "ret", "params"), "name": ("rva", "name"),
            "local": ("rva", "type", "name"), "global": ("rva",), "equate": ("rva", "value"),
            "callsite": ("rva", "cc", "ret", "params"), "flags": ("rva",)}
ONE_OF = {"local": ("offset", "at", "var"), "equate": ("name", "enum"), "flags": ("noreturn", "thunk_of", "call_fixup")}
LAYOUT_KINDS = ("struct", "fields", "union", "enum")
METHOD_REQUIRED = ("iface", "name", "slot", "ret", "params")


def read_db(root):
    """The hand-maintained fact files of a checkout, verbatim: {"annotations.jsonl": text, "methods/x.jsonl": text}."""
    db = Path(root) / "db"
    files = {}
    if (db / "annotations.jsonl").exists():
        files["annotations.jsonl"] = (db / "annotations.jsonl").read_bytes().decode("utf-8")
    for p in sorted((db / "methods").glob("*.jsonl")):
        files["methods/" + p.name] = p.read_bytes().decode("utf-8")
    return files


def write_files(root, files, names=None):
    """Writes ledger files into root/db/ (LF, UTF-8, as they are); returns the names whose content changed."""
    db = Path(root) / "db"
    changed = []
    for name, text in sorted(files.items()):
        if name == OPS or (names is not None and name not in names):
            continue
        path = db / name
        if not text and not path.exists():
            continue  # a file the ledger emptied that db/ never had (lane QT's "methods/nGZGraphic4::cDraw2D.jsonl")
        old = path.read_bytes().decode("utf-8") if path.exists() else None
        if old != text:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode("utf-8"))
            changed.append(name)
    return changed


def dumps(rec):
    return json.dumps(rec)


def lines_of(text):
    if not text:
        return []
    return text.split("\n")[:-1] if text.endswith("\n") else text.split("\n")


def key(file, rec):
    if file == "annotations.jsonl":
        k = rec["kind"]
        if k in ("struct", "fields"):
            return ("layout", rec["name"])
        if k in ("enum", "union"):
            return (k, rec["name"])
        if k == "local":
            return (k, rec["rva"], rec["offset"] if "offset" in rec else "@" + rec.get("at", rec.get("var", "")))
        if k == "equate":
            return (k, rec["rva"], rec.get("operand", -1), rec["value"])
        return (k, rec["rva"])
    if _LEGACY_KEYS:
        return legacy_method_key(rec)
    # A method fact is one slot of one interface (what ifacegen lays out): base and derived interfaces, or two interfaces
    # of one class, share a vtable but never a key. Until 2026-10-17 the key was class+vtable+slot (legacy_method_key).
    if "extends" in rec:
        return ("extends", rec["iface"])
    if "alias_of" in rec:
        return ("alias", rec["iface"])
    return ("method", rec["iface"], rec["slot"])


_LEGACY_KEYS = False


def legacy_method_key(rec):
    """The method key before lane CM (class, or iface without a class, + vtable + slot): kept to read old delete operations."""
    if "extends" in rec:
        return ("extends", rec.get("class"), rec["extends"])
    return ("method", rec.get("class") or rec["iface"], rec.get("vtable", 0), rec["slot"])


def is_legacy_key(file, k):
    return file.startswith("methods/") and ((k[0] == "method" and len(k) == 4) or (k[0] == "extends" and len(k) == 3))


class legacy_keys:
    """`with legacy_keys():` keys method facts the old way (for comparing what the re-keying changes)."""
    def __enter__(self):
        global _LEGACY_KEYS
        self.old, _LEGACY_KEYS = _LEGACY_KEYS, True

    def __exit__(self, *exc):
        global _LEGACY_KEYS
        _LEGACY_KEYS = self.old


def merge_fields(entry, new_fields, lane):
    """Adds or replaces fields (by offset) in a structure entry; a field keeps its place, a new one goes in offset order."""
    fields = entry.setdefault("fields", [])
    for nf in new_fields:
        nf = dict(nf, lane=lane)
        for i, f in enumerate(fields):
            if f["off"] == nf["off"]:
                fields[i] = nf
                break
        else:
            offs = [f["off"] for f in fields]
            if offs == sorted(offs):
                at = next((i for i, o in enumerate(offs) if o > nf["off"]), len(fields))
                fields.insert(at, nf)
            else:
                fields.append(nf)


def upsert(files, file, rec):
    """Records rec in files[file]; returns True when the text changed."""
    text = files.get(file, "")
    lines = lines_of(text)
    want = key(file, rec)
    parsed = [json.loads(l) if l.strip() else None for l in lines]
    if want[0] == "layout":
        idx = [i for i, p in enumerate(parsed) if p and p["kind"] in ("struct", "fields") and p["name"] == rec["name"]]
        if rec["kind"] == "fields" and idx:
            at = next((i for i in idx if parsed[i]["kind"] == "struct"), idx[0])
            entry = parsed[at]
            merge_fields(entry, rec["fields"], rec["lane"])
            lines[at] = dumps(entry)
            new = "\n".join(lines) + "\n"
            changed = new != text
            files[file] = new
            return changed
        idx = [i for i in idx if parsed[i]["kind"] == rec["kind"]]
    else:
        idx = [i for i, p in enumerate(parsed) if p and safe_key(file, p) == want]
    if idx:
        lines[idx[0]] = dumps(rec)
    else:
        lines.append(dumps(rec))
    new = "\n".join(lines) + "\n"
    changed = new != text
    files[file] = new
    return changed


def find_existing(files, file, rec):
    """The entry rec would replace (same key, not merely the same structure), or None."""
    want = key(file, rec)
    for line in lines_of(files.get(file, "")):
        if not line.strip():
            continue
        p = json.loads(line)
        if want[0] == "layout":
            if p.get("kind") == rec["kind"] and p.get("name") == rec["name"]:
                return p
        elif safe_key(file, p) == want:
            return p
    return None


def safe_key(file, rec):
    try:
        return key(file, rec)
    except KeyError:
        return None


def add_op(files, lane, file, rec):
    """Appends an operation to _ops.jsonl."""
    files[OPS] = files.get(OPS, "") + json.dumps({"lane": lane, "file": file, "entry": rec}) + "\n"


def ops(files):
    return [json.loads(l) for l in lines_of(files.get(OPS, "")) if l.strip()]


def replay(files, operations):
    """Applies recorded operations to files, in order. Replaying the same operations on the result changes nothing.
    Operations: {"entry"} upserts it; {"delete": key} removes the entries with that key; {"delete_field": {"name", "off"}}
    removes one field of a structure; {"rename_type": [old, new]} renames a structure, union or enum and every use of it."""
    for op in operations:
        if "entry" in op:
            upsert(files, op["file"], json.loads(json.dumps(op["entry"])))
        elif "delete" in op:
            delete(files, op["file"], op["delete"])
        elif "delete_field" in op:
            delete_field(files, op["file"], op["delete_field"]["name"], op["delete_field"]["off"])
        elif "rename_type" in op:
            rename_type(files, *op["rename_type"])


def _rewrite(files, file, fn):
    """Calls fn(record) -> record or None (drop) or the same object (keep the line as it is) on each line of a file;
    returns True when the text changed. Lines that fn leaves alone keep their exact text."""
    text = files.get(file)
    if text is None:
        return False
    out = []
    for line in lines_of(text):
        if not line.strip():
            out.append(line)
            continue
        rec = json.loads(line)
        snapshot = json.dumps(rec)
        new = fn(rec)
        if new is None:
            continue
        out.append(line if json.dumps(new) == snapshot else dumps(new))
    new_text = "\n".join(out) + "\n" if out else ""
    changed = new_text != text
    files[file] = new_text
    return changed


def delete(files, file, k):
    k = list(k)
    if not _LEGACY_KEYS and is_legacy_key(file, k):  # an operation recorded before the re-keying: match it the old way
        return _rewrite(files, file, lambda r: None if list(_safe(legacy_method_key, r) or ()) == k else r)
    return _rewrite(files, file, lambda r: None if list(safe_key(file, r) or ()) == k else r)


def _safe(fn, rec):
    try:
        return fn(rec)
    except KeyError:
        return None


def migrate_ops(files, operations):
    """The operations with every old-style method delete (class+vtable+slot) rewritten as deletes by interface+slot of the
    entries it removed when it ran; `files` is the base the operations replay on (not changed). Other operations as they are."""
    state = json.loads(json.dumps(files))
    out = []
    for op in operations:
        if "delete" in op and is_legacy_key(op["file"], op["delete"]):
            k = list(op["delete"])
            hit = [r for r in (json.loads(l) for l in lines_of(state.get(op["file"], "")) if l.strip())
                   if list(_safe(legacy_method_key, r) or ()) == k]
            out += [dict(op, delete=list(key(op["file"], r))) for r in hit]
        else:
            out.append(op)
        replay(state, [op])
    return out


def delete_field(files, file, name, off):
    def fn(r):
        if r.get("kind") in ("struct", "fields") and r.get("name") == name:
            r["fields"] = [f for f in r.get("fields", []) if f["off"] != off]
            if r["kind"] == "fields" and not r["fields"]:
                return None
        return r
    return _rewrite(files, file, fn)


def rename_type_in(t, old, new):
    """A type string with every use of the type `old` (T, T*, T[n], T**) renamed."""
    import re
    return re.sub(r"(?<![\w:])" + re.escape(old) + r"(?![\w])", new, t) if isinstance(t, str) else t


def rename_type_rec(rec, old, new):
    """A record (db/ entry or derived fact) with the type renamed: its own name if it defines it, and every type it uses."""
    if isinstance(rec, list):
        return [rename_type_rec(x, old, new) for x in rec]
    if not isinstance(rec, dict):
        return rec
    out = {}
    for k, v in rec.items():
        if k in ("type", "ret", "this", "enum"):
            out[k] = rename_type_in(v, old, new)
        elif k in ("class", "cls") and v == old:
            out[k] = new
        elif k == "name" and v == old and rec.get("kind") in LAYOUT_KINDS:
            out[k] = new
        else:
            out[k] = rename_type_rec(v, old, new)
    return out


def rename_type(files, old, new):
    """Renames a type everywhere. A definition of `old` whose kind already has a `new` entry is dropped instead: on a
    replay, the operations before the rename add the old definition again (the renamed one stays the one that counts)."""
    have = {(r.get("kind"), r.get("name")) for r in (json.loads(l) for l in lines_of(files.get("annotations.jsonl", "")) if l.strip())}
    changed = False
    for file in [f for f in files if f != OPS]:
        changed |= _rewrite(files, file, lambda r: None if r.get("kind") in LAYOUT_KINDS and r.get("name") == old
                            and (r["kind"], new) in have else rename_type_rec(r, old, new))
    return changed


# ---------------------------------------------------------------- finding entries by a short key (annotate --confirm, --delete, --rename)

KEY_HELP = """keys: 0x5890c0 (the entries for that function or global: name, signature, flags), function:0x5890c0 (one kind),
  0x5890c0@-8 (a stack local by offset), 0x5890c0@0x5890d3 or 0x5890c0@iVar3 (a local by first use or decompiler name),
  SimDataRef (a structure, union or enum, with its fields), SimDataRef.index or SimDataRef+2 (one field),
  cTSObject::Prim00 (a function or method by qualified name), cITSString:40 (one interface's method fact: interface:slot),
  cTSString:40 or cTSString@8:3 (every interface's fact at that slot of the class's vtable: across files and lanes),
  cTSPerson/cITSPerson:8 (only the fact in db/methods/cTSPerson.jsonl; slot keys are otherwise cross-file),
  alias:NbrFramework, extends:cITSPerson (an interface's alias_of or extends line),
  equate:0x5890d3, callsite:0x5890d3"""


def _int(x):
    if isinstance(x, int):
        return x
    try:
        return int(str(x), 0)
    except ValueError:
        return None


def _bare(name):
    return name[2:] if isinstance(name, str) and name.startswith("t_") else name


bare_name = _bare


def find(files, spec):
    """The entries a key names: [(file, record, field)] (field: the field dict when the key names one field, else None)."""
    import re
    kind = None
    m = re.fullmatch(r"([a-z]+):(.+)", spec)
    if m and m.group(1) in KINDS:
        kind, spec = m.group(1), m.group(2)
    out = []

    def each(prefix):
        for file in sorted(files):
            if file == OPS or not file.startswith(prefix):
                continue
            for line in lines_of(files[file]):
                if line.strip():
                    yield file, json.loads(line)

    m = re.fullmatch(r"(alias|extends):(\w+)", spec)
    if m:  # an interface's alias or extends line
        field = "alias_of" if m.group(1) == "alias" else "extends"
        return [(file, r, None) for file, r in each("methods/") if field in r and r.get("iface") == m.group(2)]
    m = re.fullmatch(r"(0x[0-9a-fA-F]+)(?:@(.+))?", spec)
    if m:
        rva, sub = int(m.group(1), 16), m.group(2)
        for file, r in each("annotations.jsonl"):
            if _int(r.get("rva")) != rva or (kind and r["kind"] != kind) or r["kind"] in LAYOUT_KINDS:
                continue
            if sub is None:
                if r["kind"] != "local" or kind == "local":
                    out.append((file, r, None))
            elif r["kind"] == "local":
                n = _int(sub)
                if (n is not None and _int(r.get("offset")) == n) or (n is not None and _int(r.get("at")) == n) or \
                        sub in (r.get("var"), r.get("name"), _bare(r.get("name"))):
                    out.append((file, r, None))
        return out
    m = re.fullmatch(r"(?:(\w+)/)?([A-Za-z_]\w*)(?:@(\w+))?:(\d+)", spec)
    if m:
        # Iface:slot names one fact; Class:slot / Class@vt:slot every interface's fact at that slot of the class's vtable
        # (when no interface has that name). A group/ prefix keeps to one file: cTSPerson/cITSPerson:8.
        group, cls, vt, slot = m.group(1), m.group(2), m.group(3), int(m.group(4))
        facts = [(file, r) for file, r in each("methods/" + (group + ".jsonl" if group else ""))
                 if "extends" not in r and r.get("slot") == slot]
        by_iface = vt is None and any(r.get("iface") == cls for _, r in each("methods/"))
        for file, r in facts:
            if (r.get("iface") == cls) if by_iface else (r.get("class") == cls and r.get("vtable", 0) == _int(vt or 0)):
                out.append((file, r, None))
        return out
    m = re.fullmatch(r"([A-Za-z_][\w<>,]*)(?:\.(\w+)|\+(0x[0-9a-fA-F]+|\d+))", spec)
    if m:
        name, field, off = m.group(1), m.group(2), _int(m.group(3)) if m.group(3) else None
        for file, r in each("annotations.jsonl"):
            if r.get("kind") in ("struct", "fields") and r.get("name") == name:  # a union is confirmed, renamed, deleted whole
                for f in r.get("fields", []):
                    if (field and _bare(f["name"]) == _bare(field)) or (off is not None and _int(f.get("off")) == off):
                        out.append((file, r, f))
        return out
    if "::" in spec:
        owner, leaf = spec.rsplit("::", 1)
        for file, r in each(""):
            if file.startswith("methods/"):
                if (r.get("class") == owner or r.get("iface") == owner) and _bare(r.get("name")) == leaf:
                    out.append((file, r, None))
            elif r.get("kind") == "function" and r.get("class") == owner and _bare(r["name"]) == leaf:
                out.append((file, r, None))
            elif r.get("kind") == "name" and r["name"] == spec:
                out.append((file, r, None))
        return [o for o in out if not kind or o[1].get("kind") == kind]
    for file, r in each("annotations.jsonl"):
        if r.get("kind") in LAYOUT_KINDS and r.get("name") == spec and (not kind or r["kind"] == kind):
            out.append((file, r, None))
    return out


def class_facts(files, cls):
    """A class's recorded facts as an annotate file (the --apply format, lane and tentative kept): its structures (and
    `Class_*` faces), functions and names in `Class::`, their locals, flags, call sites and equates, and the method facts of
    every interface with a fact on the class's vtables. Applying it back records nothing new."""
    def mine(name):
        return isinstance(name, str) and (name == cls or name.startswith((cls + "_", cls + "::", "t_" + cls + "::")))
    ann, methods = [], {}
    recs = [json.loads(l) for l in lines_of(files.get("annotations.jsonl", "")) if l.strip()]
    rvas = {r["rva"] for r in recs if r["kind"] in ("function", "name") and (r.get("class") == cls or mine(r.get("name")))}
    for r in recs:
        if (r["kind"] in LAYOUT_KINDS and mine(r["name"])) or r.get("rva") in rvas:
            ann.append(r)
    ifaces = set()
    for file in sorted(f for f in files if f.startswith("methods/")):
        for l in lines_of(files[file]):
            if l.strip():
                r = json.loads(l)
                if r.get("class") == cls:
                    ifaces.add(r["iface"])
    for file in sorted(f for f in files if f.startswith("methods/")):
        for l in lines_of(files[file]):
            if l.strip():
                r = json.loads(l)
                if r.get("iface") in ifaces or r.get("class") == cls:
                    methods.setdefault(file[len("methods/"):-len(".jsonl")], []).append(r)
    # "tentative": false spelled out, so --apply (tentative by default) keeps confirmed facts confirmed.
    explicit = lambda r: dict(r, tentative=bool(r.get("tentative")))  # noqa: E731
    out = {}
    if ann:
        out["annotations"] = [explicit(r) for r in ann]
    if methods:
        out["methods"] = {g: [explicit(r) for r in rs] for g, rs in methods.items()}
    return out


def dump_overlay(overlay):
    """An annotate file with one entry per line (diffable, editable)."""
    parts = []
    for section, items in overlay.items():
        if isinstance(items, dict):
            groups = [f'    {json.dumps(g)}: [\n' + ",\n".join("      " + json.dumps(x) for x in xs) + "\n    ]" for g, xs in items.items()]
            parts.append(f'  {json.dumps(section)}: {{\n' + ",\n".join(groups) + "\n  }")
        else:
            parts.append(f'  {json.dumps(section)}: [\n' + ",\n".join("    " + json.dumps(x) for x in items) + "\n  ]")
    return "{\n" + ",\n".join(parts) + "\n}\n"


def entry_key(file, rec):
    """The command-line key that names this entry (find() accepts it)."""
    if file != "annotations.jsonl":
        if "extends" in rec or "alias_of" in rec:
            return ("extends:" if "extends" in rec else "alias:") + rec["iface"]
        return f"{rec['iface']}:{rec.get('slot', '?')}"
    k = rec["kind"]
    if k in LAYOUT_KINDS:
        return rec["name"] if k != "enum" else f"enum:{rec['name']}"
    if k == "local":
        return f"{rec['rva']}@{rec['offset'] if 'offset' in rec else rec.get('at', rec.get('var'))}"
    if k in ("equate", "callsite"):
        return f"{k}:{rec['rva']}"
    return f"{k}:{rec['rva']}"


def describe_match(file, rec, field):
    who = f" [lane {rec['lane']}]" if rec.get("lane") else " [db/]"
    t = " (tentative)" if (field or rec).get("tentative") else ""
    if field is not None:
        return f"field {rec['name']}+{field['off']} {field['name']}: {field['type']}{t}{who}"
    return describe(file, rec) + t + who


def confirm_ops(files, matches, lane):
    """Operations that drop `tentative` from the matched entries (and a structure's fields); [] when all are confirmed."""
    ops_ = []
    for file, rec, field in matches:
        if field is not None:
            if field.get("tentative"):
                f = {k: v for k, v in field.items() if k not in ("tentative", "lane")}
                ops_.append({"lane": lane, "file": file, "entry": {"kind": "fields", "name": rec["name"], "fields": [f],
                                                                    "source": rec.get("source", ""), "lane": field.get("lane", lane)}})
            continue
        new = json.loads(json.dumps(rec))
        new.pop("tentative", None)
        for f in new.get("fields", []):
            f.pop("tentative", None)
        if new != rec:
            ops_.append({"lane": lane, "file": file, "entry": new})
    return ops_


def delete_ops(files, matches, lane):
    ops_ = []
    for file, rec, field in matches:
        if field is not None:
            ops_.append({"lane": lane, "file": file, "delete_field": {"name": rec["name"], "off": field["off"]}})
        else:
            ops_.append({"lane": lane, "file": file, "delete": list(key(file, rec))})
    return ops_


def rename_ops(files, match, new_name, lane):
    """The operation renaming one matched entry (a type rename rewrites every use of the type)."""
    file, rec, field = match
    if field is not None:
        f = dict(field, name=new_name)
        f.pop("lane", None)
        return [{"lane": lane, "file": file, "entry": {"kind": "fields", "name": rec["name"], "fields": [f],
                                                        "source": rec.get("source", ""), "lane": field.get("lane", lane)}}]
    if file == "annotations.jsonl" and rec["kind"] in LAYOUT_KINDS:
        return [{"lane": lane, "rename_type": [rec["name"], new_name]}]
    new = json.loads(json.dumps(rec))
    if rec.get("kind") == "name" and "::" in rec["name"] and "::" not in new_name:
        new_name = rec["name"].rsplit("::", 1)[0] + "::" + new_name  # keep the class
    new["name"] = new_name
    return [{"lane": lane, "file": file, "entry": new}]


def pending(base, operations):
    """The operations not in db/ yet (base: the files as db/ has them). A lane's operations are done when replaying them
    on base changes nothing (they were exported and merged); the others are kept whole."""
    done = set()
    for lane in {op["lane"] for op in operations}:
        probe = dict(base)
        replay(probe, [op for op in operations if op["lane"] == lane])
        if probe == base:
            done.add(lane)
    return [op for op in operations if op["lane"] not in done]


# Kinds whose name becomes a Ghidra symbol (function/label/variable). Data type, field and equate names follow other
# rules and are left to the server.
SYMBOL_NAME_KINDS = ("function", "name", "local", "global")
ILLEGAL_SYMBOL_CHAR = re.compile(r"[\x00-\x20\x7f]")  # Ghidra refuses whitespace and control characters in a symbol


def symbol_name_slots(file, rec):
    """[(where, container, key)] of the record's values Ghidra turns into symbol names (types are not names: "char *"
    is fine)."""
    out = []
    if file != "annotations.jsonl":
        return [("method name", rec, "name")] if isinstance(rec.get("name"), str) else []
    if rec.get("kind") in SYMBOL_NAME_KINDS and isinstance(rec.get("name"), str):
        out.append(("name", rec, "name"))
    for list_key in ("params",) if rec.get("kind") in ("function", "callsite") else ():
        for i, f in enumerate(rec.get(list_key) or []):
            if isinstance(f, dict) and isinstance(f.get("name"), str):
                out.append((f"{list_key}[{i}] name", f, "name"))
    return out


def legal_symbol(name):
    """`name` with each run of whitespace or control characters replaced by `_` (`~X (deleting)` -> `~X_(deleting)`)."""
    return re.sub(r"[\x00-\x20\x7f]+", "_", name.strip())


def check_symbol_names(entries, fix=False):
    """Problems (or, with fix, notes after renaming in place) for names Ghidra would refuse, before any server work."""
    out = []
    for file, rec in entries:
        for where, holder, key in symbol_name_slots(file, rec):
            name = holder[key]
            if ILLEGAL_SYMBOL_CHAR.search(name) or not name.strip():
                if fix and name.strip():
                    holder[key] = legal_symbol(name)
                    out.append(f"{where} {name!r} becomes {holder[key]!r} (--fix-names)")
                else:
                    out.append(f"{where} {name!r} ({rec.get('rva') or rec.get('kind') or file}): Ghidra symbol names "
                               f"can't hold spaces or control characters" + (f"; use {legal_symbol(name)!r} or "
                               "--fix-names" if name.strip() else ""))
    return out


def convert_overlay(overlay, lane, tentative):
    """(entries, problems): an overlay file's sections as ledger entries [(file, record)], in the db/ formats."""
    entries, problems = [], []
    for section, items in overlay.items():
        if section in REJECTED:
            problems.append(f"section {section!r}: no home in db/; use it with `decompile --overlay` for a private try")
            continue
        if section not in ("annotations", "functions", "locals", "globals", "methods"):
            problems.append(f"unknown section {section!r}")
            continue
        if section == "methods":
            for group, facts in items.items():
                if not re.fullmatch(r"[A-Za-z0-9_.-]+", group):  # a file name: "nGZGraphic4::cDraw2D" broke exports (lane QT)
                    problems.append(f"methods group {group!r}: not a file name (letters, digits, _ . -); use the class's bare name")
                    continue
                for m in facts:
                    entries.append(("methods/" + group + ".jsonl", dict(m)))
            continue
        for a in items:
            a = json.loads(json.dumps(a))
            if section == "functions":
                a = {"kind": "name", "rva": a["rva"], "name": a["name"], **({"source": a["source"]} if "source" in a else {})}
            elif section == "locals":
                a = dict({"kind": "local"}, **a)
            elif section == "globals":
                a = dict({"kind": "global"}, **a)
            entries.append(("annotations.jsonl", a))
    out = []
    for file, rec in entries:
        if file == "annotations.jsonl":
            if rec.get("kind") not in KINDS:
                problems.append(f"annotation kind {rec.get('kind')!r} is not one of {', '.join(KINDS)}")
                continue
            missing = [k for k in REQUIRED[rec["kind"]] if k not in rec]
            alts = ONE_OF.get(rec["kind"])
            if alts and not any(k in rec for k in alts):
                missing.append(" or ".join(alts))
        else:
            missing = [k for k in METHOD_REQUIRED if k not in rec] if "extends" not in rec and "alias_of" not in rec else []
        if missing:
            problems.append(f"{file} entry {json.dumps(rec)[:80]}: missing {', '.join(missing)}")
            continue
        t = rec.pop("tentative", None)
        t = tentative if t is None else bool(t)
        rec.setdefault("source", f"lane {lane} (annotate)")
        rec.pop("lane", None)
        if t:
            rec["tentative"] = True
            for f in rec.get("fields", []):
                f.setdefault("tentative", True)
        rec["lane"] = lane
        out.append((file, rec))
    return out, problems


def describe(file, rec):
    if file != "annotations.jsonl":
        if "extends" in rec:
            return f"interface {rec['iface']} extends {rec['extends']}"
        if "alias_of" in rec:
            return f"interface {rec['iface']} = {rec['alias_of']} (alias) [{file}]"
        where = f" ({rec['class']}" + (f"@{rec['vtable']}" if rec.get("vtable") else "") + ")" if rec.get("class") else ""
        return f"method {rec['iface']}::{rec['name']} slot {rec['slot']}{where} [{file}]"
    k = rec["kind"]
    if k in ("struct", "fields", "union"):
        return f"{k} {rec['name']} ({len(rec['fields'])} field(s))"
    if k == "enum":
        return f"enum {rec['name']} ({len(rec['values'])} value(s))"
    if k == "local":
        where = rec["offset"] if "offset" in rec else rec.get("at", rec.get("var"))
        return f"local {rec['rva']}@{where} {rec['name']}"
    if k == "equate":
        return f"equate {rec['rva']} {rec['value']} = {rec.get('name') or rec.get('enum')}"
    if k == "flags":
        return f"flags {rec['rva']} " + ", ".join(f for f in ("noreturn", "thunk_of", "call_fixup") if f in rec)
    return f"{k} {rec['rva']}" + (f" {rec['name']}" if "name" in rec else "")


def add_raw_op(files, op):
    files[OPS] = files.get(OPS, "") + json.dumps(op) + "\n"


def facts_delta(old, new):
    """The sections of a derived facts file (tools/annotate.py --out) that gained or changed entries, as an overlay."""
    delta = {}
    for section, items in new.items():
        if section in ("comments", "inline", "missed", "resized") or not isinstance(items, list):
            continue
        have = {json.dumps(i, sort_keys=True) for i in old.get(section, [])}
        add = [i for i in items if json.dumps(i, sort_keys=True) not in have]
        if add:
            delta[section] = add
    return delta


# Sections of a derived facts file whose entries belong to one function (by "rva"), and the annotation kinds that do.
FUNCTION_SECTIONS = ("functions", "impls", "compiled", "comments", "locals")
FUNCTION_KINDS = ("function", "name", "local", "flags")


def _rva(x):
    return int(x, 16) if isinstance(x, str) else None


def facts_changes(old, new, renames=()):
    """(delta, revert) between two derived facts files. delta: the overlay ApplyDb applies (entries added or changed, plus
    every new entry of a function, structure or global that revert resets); revert: what the server resets first, for
    entries that were removed or changed: {"functions": [rva] (back to the analysed program's name, signature, locals),
    "fields": [{"name", "off"}], "types": [name] (removed), "locals": [{"rva", "offset"|"at"}], "globals": [rva],
    "equates": [{"rva", "value", "operand"?}], "callsites": [rva], "renames": [[old, new]]}. A type renamed in `renames` is
    renamed in the program first (its uses follow), so the old facts are compared as if it had always had the new name."""
    old = json.loads(json.dumps(old))
    for a, b in renames:
        old = rename_type_rec(old, a, b)
    delta = facts_delta(old, new)
    k = lambda i: json.dumps(i, sort_keys=True)  # noqa: E731
    revert = {"functions": set(), "fields": [], "types": [], "locals": [], "globals": set(), "equates": [], "callsites": set(),
              "renames": [list(r) for r in renames]}

    def removed(section):
        have = {k(i) for i in new.get(section, [])}
        return [i for i in old.get(section, []) if isinstance(i, dict) and k(i) not in have]

    for section in FUNCTION_SECTIONS:
        for i in removed(section):
            if section == "locals":
                revert["locals"].append({x: i[x] for x in ("rva", "offset", "at") if x in i})
            else:
                revert["functions"].add(i["rva"])
    have_inline = set(new.get("inline", []))
    revert["functions"] |= {r for r in old.get("inline", []) if r not in have_inline}
    have_members = set(new.get("members", []))
    revert["functions"] |= {r for r in old.get("members", []) if r not in have_members}
    for i in removed("globals"):
        revert["globals"].add(i["rva"])
    layouts_changed = set()
    for i in removed("annotations"):
        kd = i["kind"]
        if kd in FUNCTION_KINDS:
            if kd == "local":
                revert["locals"].append({x: i[x] for x in ("rva", "offset", "at") if x in i})
            else:
                revert["functions"].add(i["rva"])
        elif kd in LAYOUT_KINDS:
            layouts_changed.add(i["name"])
        elif kd == "global":
            revert["globals"].add(i["rva"])
        elif kd == "equate":
            revert["equates"].append({x: i[x] for x in ("rva", "value", "operand") if x in i})
        elif kd == "callsite":
            revert["callsites"].add(i["rva"])
    # Structures: fields at offsets no new fact describes are cleared; a type no new fact defines is removed.
    for name in sorted(layouts_changed):
        olds = [i for i in old.get("annotations", []) if i.get("kind") in LAYOUT_KINDS and i.get("name") == name]
        news = [i for i in new.get("annotations", []) if i.get("kind") in LAYOUT_KINDS and i.get("name") == name]
        if not news:
            revert["types"].append(name)
            continue
        if any(i["kind"] in ("enum", "union") for i in olds):
            continue  # an enum's values and a union's members are rewritten whole
        new_offs = {f["off"] for i in news for f in i.get("fields", [])}
        for off in sorted({f["off"] for i in olds for f in i.get("fields", [])} - new_offs):
            revert["fields"].append({"name": name, "off": off})
        add = [i for i in news if k(i) not in {k(x) for x in delta.get("annotations", [])}]
        delta.setdefault("annotations", []).extend(add)
    # Everything the new facts say about a reset function or global is applied again on top of the reset.
    fns = {_rva(r) for r in revert["functions"]} | {_rva(l["rva"]) for l in revert["locals"]}
    gls = {_rva(r) for r in revert["globals"]}
    if fns or gls:
        for section in FUNCTION_SECTIONS + ("globals", "annotations"):
            have = {k(x) for x in delta.get(section, [])}
            for i in new.get(section, []):
                if not isinstance(i, dict) or "rva" not in i or k(i) in have:
                    continue
                if section == "globals" or (section == "annotations" and i["kind"] == "global"):
                    hit = _rva(i["rva"]) in gls
                elif section == "annotations":
                    hit = i["kind"] in FUNCTION_KINDS and _rva(i["rva"]) in fns
                else:
                    hit = _rva(i["rva"]) in fns
                if hit:
                    delta.setdefault(section, []).append(i)
        inline = [r for r in new.get("inline", []) if _rva(r) in fns]
        if inline:
            delta["inline"] = inline
    if delta.get("functions") or fns:
        delta["members"] = new.get("members", [])  # ApplyDb's name pass reads it to put a member in its class
    out = {key_: (sorted(v) if isinstance(v, set) else v) for key_, v in revert.items()}
    return delta, {key_: v for key_, v in out.items() if v}
