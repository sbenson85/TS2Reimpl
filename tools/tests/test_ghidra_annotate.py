#!/usr/bin/env python3
"""Tests for annotate / export / snapshot / the annotated-project cache (tools/ghidra.py, tools/ghidra_ledger.py,
tools/ghidra/DecompServer.java). Run directly:

  tools/py -I tools/tests/test_ghidra_annotate.py [--offline] [--keep]

--offline runs only the checks that need no server (ledger round trip, replay, conversion, derived facts from a ledger).
Otherwise it also starts a server of its own in local/ghidra-annotate-test/ (GHIDRA_SERVER_DIR; never the shared one; the first
start builds the annotated project, ~3-4 minutes), exercises annotate, export, restart from the cache, snapshots and the
decompile options on it, and removes the directory at the end unless --keep. Skips the server part when local/ghidra is absent.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import ghidra_ledger as ledger  # noqa: E402


def main_local():
    out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"], capture_output=True, text=True)
    return Path(out.stdout.strip()).parent / "local"


LOCAL = main_local()
TEST_DIR = LOCAL / "ghidra-annotate-test"  # not the server test's directory: its cache would make the cold start warm
os.environ["GHIDRA_SERVER_DIR"] = str(TEST_DIR)
os.environ["GHIDRA_BACKUP_DIR"] = str(TEST_DIR / "backups")
import ghidra as client  # noqa: E402

failures = []
RVA = "0x5890c0"        # for the decompile options (db/annotations.jsonl already types it)
PROBE = "0x1330"     # annotated by the test: a small function db/annotations.jsonl does not mention


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def run_cli(*argv):
    """Runs a tools/ghidra.py subcommand in-process; returns (exit code, stdout)."""
    import contextlib
    import io
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["ghidra.py", *argv]
    try:
        with contextlib.redirect_stdout(buf):
            rc = client.main()
    finally:
        sys.argv = old
    client.sys.stdout = sys.__stdout__
    return rc, buf.getvalue()


# ---------------------------------------------------------------- offline

def test_roundtrip_offline():
    files = ledger.read_db(ROOT)
    check("annotations.jsonl" in files and any(n.startswith("methods/") for n in files), "read_db finds annotations and methods")
    tmp = Path(tempfile.mkdtemp(prefix="bz-rt-"))
    changed = ledger.write_files(tmp, files)
    same = all((tmp / "db" / n).read_bytes() == (ROOT / "db" / n).read_bytes() for n in files)
    check(same and len(changed) == len(files), f"export of the ledger read from db/ is byte-identical ({len(files)} files)")
    check(ledger.write_files(ROOT, files) == [], "writing it over db/ changes nothing")
    again = ledger.read_db(tmp)
    check(again == files, "second round trip identical")
    shutil.rmtree(tmp)


def test_method_keys():
    """Method facts are keyed by interface+slot: two interfaces on one vtable keep both sets (lane CI's collision)."""
    files = ledger.read_db(ROOT)
    base = dict(files)
    file = "methods/CMTest.jsonl"
    mk = lambda iface, slot, name: {"iface": iface, "class": "cTSString", "slot": slot, "name": name,  # noqa: E731
                                    "ret": "u32", "params": []}
    ov = {"methods": {"CMTest": [mk("CMIfaceA", 0, "QueryA"), mk("CMIfaceB", 0, "QueryB"), mk("CMIfaceB", 1, "AddRefB")]}}
    entries, problems = ledger.convert_overlay(ov, "CMT", True)
    for f, rec in entries:
        ledger.upsert(files, f, json.loads(json.dumps(rec)))
        ledger.add_op(files, "CMT", f, rec)
    recs = [json.loads(l) for l in ledger.lines_of(files.get(file, ""))]
    check(not problems and [r["name"] for r in recs] == ["QueryA", "QueryB", "AddRefB"], "two interfaces at one class slot keep both facts")
    with ledger.legacy_keys():
        old = dict(base)
        ledger.replay(old, ledger.ops(files))
    check([json.loads(l)["name"] for l in ledger.lines_of(old[file])] == ["QueryB", "AddRefB"],
          "(the old class+slot key replaced one interface's fact with the other's)")
    fresh = dict(base)
    ledger.replay(fresh, ledger.ops(files))
    check(fresh == {k: v for k, v in files.items() if k != ledger.OPS}, "replaying the operations gives the same ledger")
    tmp = Path(tempfile.mkdtemp(prefix="cm-keys-"))
    ledger.write_files(tmp, files)
    check(ledger.read_db(tmp) == {k: v for k, v in files.items() if k != ledger.OPS}, "both sets round-trip through export")
    shutil.rmtree(tmp)
    check(len(ledger.find(files, "CMIfaceA:0")) == 1 and len(ledger.find(files, "CMIfaceB:0")) == 1, "Iface:slot names one fact")
    check({m[1]["iface"] for m in ledger.find(files, "cTSString:0") if m[0] == file} == {"CMIfaceA", "CMIfaceB"},
          "Class:slot names every interface's fact at that slot")
    check(len(ledger.find(files, "CMTest/CMIfaceB:1")) == 1 and ledger.find(files, "cTSString/CMIfaceB:1") == [],
          "group/Iface:slot keeps to one file")
    check(all(ledger.find(files, ledger.entry_key(f, r)) == [(f, r, None)] for f, r in entries), "entry_key round-trips through find")
    # Deleting one interface's fact leaves the other's.
    work = json.loads(json.dumps(files))
    for op in ledger.delete_ops(work, ledger.find(work, "CMIfaceA:0"), "CMT"):
        ledger.replay(work, [op])
    check([json.loads(l)["name"] for l in ledger.lines_of(work[file])] == ["QueryB", "AddRefB"], "deleting Iface:slot removes only it")
    # An old-style delete (class+vtable+slot, recorded before the re-keying) still replays, and migrates to interface keys.
    legacy = {"lane": "CMT", "file": file, "delete": ["method", "cTSString", 0, 0]}
    ops_ = ledger.ops(files) + [legacy]
    migrated = ledger.migrate_ops(base, ops_)
    check([op.get("delete") for op in migrated[-2:]] == [["method", "CMIfaceA", 0], ["method", "CMIfaceB", 0]],
          "migrate_ops rewrites an old delete as one delete per interface it removed")
    a, b = dict(base), dict(base)
    ledger.replay(a, ops_)
    ledger.replay(b, migrated)
    check(a == b and [json.loads(l)["name"] for l in ledger.lines_of(a[file])] == ["AddRefB"], "old and migrated deletes give the same ledger")
    check(ledger.migrate_ops(base, ledger.ops(files)) == ledger.ops(files), "operations without old deletes migrate unchanged")
    # An alias line (ifacegen alias_of) is its own entry, found by alias:Name.
    entries, problems = ledger.convert_overlay({"methods": {"CMTest": [{"iface": "CMOldName", "alias_of": "CMIfaceB"}]}}, "CMT", True)
    check(not problems and len(entries) == 1, "an alias line converts (no slot, name, ret or params needed)")
    ledger.upsert(files, *entries[0])
    ledger.upsert(files, *entries[0])
    hit = ledger.find(files, "alias:CMOldName")
    check(len(hit) == 1 and ledger.entry_key(*hit[0][:2]) == "alias:CMOldName", "alias:Name finds the alias line, once")
    # Every db/ entry can be described and keyed (`recorded --all` crashed on an extends line without a name).
    bad = []
    for f in sorted(base):
        for l in ledger.lines_of(base[f]):
            try:
                ledger.describe_match(f, json.loads(l), None)
                ledger.entry_key(f, json.loads(l))
            except KeyError as e:
                bad.append(f"{f}: {e}")
    check(not bad, f"describe/entry_key handle every db/ entry {bad[:2]}")


def test_class_facts():
    """`ghidra.py facts <Class>`: a class's facts as an annotate file that converts back to the same entries."""
    files = ledger.read_db(ROOT)
    ov = ledger.class_facts(files, "cTSFamilyRelationshipArray")
    ann, meths = ov.get("annotations", []), [m for ms in ov.get("methods", {}).values() for m in ms]
    check(any(a["kind"] == "struct" and a["name"] == "cTSFamilyRelationshipArray" for a in ann) and
          any(m["iface"] == "cITSFamilyRelationshipArray" for m in meths), f"struct and method facts found ({len(ann)}, {len(meths)})")
    text = ledger.dump_overlay(ov)
    back = json.loads(text)
    check(back == ov and text.count("\n") >= len(ann) + len(meths), "dumped one entry per line, parses back")
    entries, problems = ledger.convert_overlay(json.loads(text), "CMT", True)
    work = dict(files)
    for f, rec in entries:
        old = ledger.find_existing(work, f, rec)
        if old is not None and old.get("lane"):
            rec["lane"] = old["lane"]
        ledger.upsert(work, f, rec)
    norm = lambda fs: {f: sorted(json.dumps(json.loads(l), sort_keys=True) for l in ledger.lines_of(t)) for f, t in fs.items()}  # noqa: E731
    check(not problems and norm(work) == norm(files), "applying the dump back changes no entry (key order aside)")


def test_refusals():
    """annotate refuses what `ifacegen --check` refuses, naming the fact that holds the slot (lane CJ's duplicate slots)."""
    import ifacegen
    classes = ifacegen.load_classes(ROOT / "db")
    base = ledger.read_db(ROOT)
    held = next(json.loads(l) for f in sorted(base) if f.startswith("methods/") for l in ledger.lines_of(base[f])
                if '"lane"' in l and '"extends"' not in l)
    held_file = next(f for f in sorted(base) if f.startswith("methods/") and json.dumps(held) in base[f])

    def attempt(file, rec):
        files = dict(base)
        entries, problems = ledger.convert_overlay({"methods": {file[len("methods/"):-len(".jsonl")]: [rec]}}, "CMT", True)
        for f, r in entries:
            ledger.upsert(files, f, json.loads(json.dumps(r)))
        return ifacegen.refusals(base, files, entries, classes)

    dup = dict({k: v for k, v in held.items() if k not in ("lane", "tentative", "source")}, name="CMOther")
    r = attempt("methods/CMOther.jsonl", dup)
    check(len(r) == 1 and "already held by " + held_file[len("methods/"):] in r[0] and f"lane {held['lane']}" in r[0],
          f"a second fact for a held interface slot in another file is refused, holder named ({r[0][:110] if r else r})")
    r = attempt(held_file, dup)
    check(r == [], "the same slot in the holder's own file replaces it (not refused)")
    r = attempt("methods/CMOther.jsonl", dict(dup, slot=held["slot"] + 1000, name=held["name"]))
    check(len(r) == 1 and "no overloads" in r[0], "a second slot with an interface's method name is refused")
    r = attempt("methods/CMOther.jsonl", {"iface": "CMNewIface", "slot": 2, "name": "Fine", "ret": "u32", "params": []})
    check(r == [], "a fact for a new interface is fine")
    r = attempt("methods/CMOther.jsonl", {"iface": "CMNewIface", "class": "NoSuchClass", "slot": 2, "name": "Fine", "ret": "u32",
                                          "params": []})
    check(len(r) == 1 and "not in db/classes.jsonl" in r[0], "other --check problems the change introduces are refused")


def test_symbol_names():
    """Names Ghidra refuses (spaces, control characters) are caught before any server work; punctuation is fine."""
    overlay = {"functions": [{"rva": "0x1330", "name": "A::B(thunk)~x<y>,z&*[1]"}, {"rva": "0x1340", "name": "Bad name"}],
               "annotations": [{"kind": "function", "rva": "0x1350", "name": "F", "cc": "__thiscall", "ret": "void",
                                "params": [{"name": "a\tb", "type": "int"}]},
                               {"kind": "struct", "name": "S", "size": 4, "fields": [{"off": 0, "type": "int", "name": "x"}]}]}
    entries, problems = ledger.convert_overlay(overlay, "CMT", True)
    check(not problems, f"the overlay converts ({problems})")
    out = ledger.check_symbol_names(entries)
    check(len(out) == 2 and "'Bad name'" in out[0] and "'Bad_name'" in out[0] and "params[0]" in out[1],
          f"a space in a name and a tab in a parameter name are refused, with the _ form; punctuation passes ({out})")
    entries, _ = ledger.convert_overlay(overlay, "CMT", True)
    out = ledger.check_symbol_names(entries, fix=True)
    names = [r.get("name") for _, r in entries] + [p["name"] for _, r in entries for p in r.get("params") or []]
    check(len(out) == 2 and "Bad_name" in names and "a_b" in names and "Bad name" not in names,
          f"--fix-names replaces them with _ and notes each ({out})")
    rc = None
    with tempfile.TemporaryDirectory() as d:
        f = Path(d) / "bad.json"
        f.write_text(json.dumps({"functions": [{"rva": "0x1340", "name": "Bad name"}]}), encoding="utf-8")
        before = client.running
        client.running = lambda: (_ for _ in ()).throw(AssertionError("server touched"))
        try:
            rc, _ = run_cli("annotate", "--apply", str(f), "--lane", "CMT", "--dry-run")
        finally:
            client.running = before
    check(rc == 2, f"annotate refuses the file before contacting the server (exit {rc})")


def test_upsert_and_replay():
    files = ledger.read_db(ROOT)
    base = dict(files)
    ov = {"annotations": [
        {"kind": "struct", "name": "BZStruct", "size": 16, "fields": [{"off": 8, "type": "i32", "name": "b"}, {"off": 0, "type": "i32", "name": "a"}]},
        {"kind": "fields", "name": "BZStruct", "fields": [{"off": 4, "type": "i16", "name": "mid"}]},
        {"kind": "function", "rva": "0x123456", "name": "BZFn", "cc": "__thiscall", "ret": "i32", "params": []}],
        "functions": [{"rva": "0x1b5c5b", "name": "BZName"}],
        "methods": {"cTSString": [{"iface": "cITSString", "class": "cTSString", "slot": 40, "name": "BZMethod", "ret": "u32", "params": []}]}}
    entries, problems = ledger.convert_overlay(ov, "BZ", True)
    check(not problems and len(entries) == 5, "overlay converts to 5 entries")
    check(all(r["lane"] == "BZ" and r["tentative"] is True for _, r in entries), "every entry carries lane and tentative")
    check(entries[0][1]["fields"][0]["tentative"] is True, "fields inherit tentative")
    for file, rec in entries:
        ledger.upsert(files, file, json.loads(json.dumps(rec)))
        ledger.add_op(files, "BZ", file, rec)
    ann = [json.loads(l) for l in ledger.lines_of(files["annotations.jsonl"])]
    st = [a for a in ann if a.get("name") == "BZStruct"]
    check(len(st) == 1 and len(st[0]["fields"]) == 3, "fields merged into the structure's entry")
    check(any(f["name"] == "mid" and f["lane"] == "BZ" for f in st[0]["fields"]), "merged field has its lane")
    check(files["annotations.jsonl"].startswith(base["annotations.jsonl"]), "new entries are appended; existing lines untouched")
    # Replay on a fresh db changes the same lines, and a second replay changes nothing.
    fresh = dict(base)
    ledger.replay(fresh, ledger.ops(files))
    expected = {k: v for k, v in files.items() if k != ledger.OPS}
    check(fresh == expected, "replaying the operations on db/ reproduces the ledger")
    ledger.replay(fresh, ledger.ops(files))
    check(fresh == expected, "a second replay changes nothing")
    check(ledger.pending(expected, ledger.ops(files)) == [], "operations already in db/ are not pending")
    check(len(ledger.pending(base, ledger.ops(files))) == 5, "operations missing from db/ are pending")
    # Replace in place: re-annotating the function as confirmed changes the same line.
    entries2, _ = ledger.convert_overlay({"annotations": [ov["annotations"][2]]}, "BZ", False)
    n_before = len(ledger.lines_of(files["annotations.jsonl"]))
    ledger.upsert(files, *entries2[0])
    fn = [json.loads(l) for l in ledger.lines_of(files["annotations.jsonl"]) if '"BZFn"' in l]
    check(len(ledger.lines_of(files["annotations.jsonl"])) == n_before and len(fn) == 1 and "tentative" not in fn[0],
          "confirming replaces the line and drops tentative")
    bad, problems = ledger.convert_overlay({"comments": [], "annotations": [{"kind": "struct", "name": "X"}]}, "BZ", True)
    check(len(problems) == 2 and not bad, "unsupported sections and incomplete entries are rejected")


NEW_KINDS = {"annotations": [
    {"kind": "enum", "name": "CAOp", "size": 2, "values": {"kOpA": 1, "kOpB": 21}},
    {"kind": "union", "name": "CAUnion", "fields": [{"type": "i32", "name": "asInt"}, {"type": "f32", "name": "asFloat"}]},
    {"kind": "struct", "name": "CAStruct", "size": 8, "fields": [{"off": 0, "type": "CAOp", "name": "op"},
                                                               {"off": 4, "type": "CAUnion", "name": "value"}]},
    {"kind": "function", "rva": "0x1330", "name": "CAFn", "cc": "__thiscall", "ret": "void", "params": [], "this": "CAStruct"},
    {"kind": "equate", "rva": "0x1340", "value": 21, "enum": "CAOp"},
    {"kind": "callsite", "rva": "0x1350", "cc": "__thiscall", "ret": "i32", "params": [{"name": "x", "type": "i32"}]},
    {"kind": "flags", "rva": "0x1360", "noreturn": True}],
    "locals": [{"rva": "0x1330", "at": "0x1338", "type": "CAStruct*", "name": "item"},
               {"rva": "0x1330", "offset": -8, "type": "i32", "name": "count"}]}


def test_keys_and_ops():
    """--confirm / --delete / --rename on a ledger: find by key, operations, replay, export drops deleted entries."""
    files = ledger.read_db(ROOT)
    base = dict(files)
    entries, problems = ledger.convert_overlay(NEW_KINDS, "CAT", True)
    check(not problems and len(entries) == 9, f"the new kinds convert ({problems})")
    for file, rec in entries:
        ledger.upsert(files, file, json.loads(json.dumps(rec)))
        ledger.add_op(files, "CAT", file, rec)
    find = lambda k: ledger.find(files, k)  # noqa: E731
    keys_ok = all(any(m[1] == rec for m in find(ledger.entry_key(f, rec))) for f, rec in entries)
    check(keys_ok, "every entry's printed key (`recorded`) finds it again")
    check(len(find("0x1330")) == 1 and find("0x1330")[0][1]["name"] == "CAFn", "key 0x1330: the function entry (not its locals)")
    check(len(find("0x1330@-8")) == 1 and len(find("0x1330@0x1338")) == 1 and len(find("0x1330@item")) == 1, "local keys: offset, first use, name")
    check(len(find("CAStruct")) == 1 and find("CAStruct.value")[0][2]["name"] == "value" and find("CAStruct+4")[0][2]["off"] == 4,
          "structure and field keys (name and offset)")
    check(len(find("CAOp")) == 1 and find("enum:CAOp")[0][1]["kind"] == "enum", "enum key")
    check(len(find("equate:0x1340")) == 1 and len(find("callsite:0x1350")) == 1, "equate and callsite keys")
    check(len(find("cTSObject::Prim00")) == 1, "qualified key finds a db/ signature")
    check(find("NoSuchThing") == [], "an unknown key finds nothing")
    # confirm: the entry loses tentative, nothing else changes
    ops = ledger.confirm_ops(files, find("CAStruct"), "CAT")
    for op in ops:
        ledger.replay(files, [op])
        ledger.add_raw_op(files, op)
    st = find("CAStruct")[0][1]
    check(len(ops) == 1 and "tentative" not in st and not any("tentative" in f for f in st["fields"]), "confirm drops tentative (and the fields')")
    check(ledger.confirm_ops(files, find("CAStruct"), "CAT") == [], "confirming twice is no operation")
    # field confirm alone
    ops = ledger.confirm_ops(files, find("CAUnion.asInt"), "CAT")
    check(ops == [], "a field key on a union matches nothing: unions are confirmed whole")
    # rename a field, a function, a type
    for op in ledger.rename_ops(files, find("CAStruct.op")[0], "opcode", "CAT") + ledger.rename_ops(files, find("0x1330")[0], "CAFn2", "CAT") \
            + ledger.rename_ops(files, find("CAStruct")[0], "CAStruct2", "CAT"):
        ledger.replay(files, [op])
        ledger.add_raw_op(files, op)
    ann = files["annotations.jsonl"]
    mine = "\n".join(l for l in ann.splitlines() if '"CAStruct' in l)  # db/ has its own field named op (lane IL)
    check('"opcode"' in mine and '"op"' not in mine, "field renamed in place")
    check('"CAFn2"' in ann and '"this": "CAStruct2"' in ann and '"type": "CAStruct2*"' in ann and '"CAStruct"' not in ann,
          "type renamed with every use (this, local type)")
    # delete: a field, a whole entry
    for op in ledger.delete_ops(files, find("CAStruct2+4"), "CAT") + ledger.delete_ops(files, find("callsite:0x1350"), "CAT"):
        ledger.replay(files, [op])
        ledger.add_raw_op(files, op)
    ann = test_lines(files["annotations.jsonl"])
    check('"name": "value"' not in ann and '"kind": "callsite"' not in ann, "deleted field and entry are gone")
    # replay on fresh db/ reproduces it; twice is idempotent; the db/ lines untouched
    fresh = dict(base)
    ledger.replay(fresh, ledger.ops(files))
    expected = {k: v for k, v in files.items() if k != ledger.OPS}
    check(fresh == expected, "replaying confirm/rename/delete operations on db/ reproduces the ledger")
    ledger.replay(fresh, ledger.ops(files))
    check(fresh == expected, "a second replay changes nothing")
    check(fresh["annotations.jsonl"].startswith(base["annotations.jsonl"]), "db/'s own lines are untouched")
    # export of the whole thing round-trips through files byte for byte
    tmp = Path(tempfile.mkdtemp(prefix="ca-rt-"))
    ledger.write_files(tmp, files)
    check(ledger.read_db(tmp) == expected, "new kinds round-trip through export and read")
    shutil.rmtree(tmp)
    # deleting everything this lane added leaves db/ as it was (export drops deleted entries)
    for spec in ("CAStruct2", "CAOp", "CAUnion", "0x1330", "0x1330@-8", "0x1330@0x1338", "equate:0x1340", "0x1360"):
        for op in ledger.delete_ops(files, find(spec), "CAT"):
            ledger.replay(files, [op])
            ledger.add_raw_op(files, op)
    check({k: v for k, v in files.items() if k != ledger.OPS} == base, "deleting every added entry restores db/ byte for byte")
    check(ledger.pending(base, ledger.ops(files)) == [], "after the deletes the lane's operations are no longer pending")


def test_facts_changes():
    old = {"functions": [{"rva": "0x10", "name": "A"}, {"rva": "0x20", "name": "B"}], "inline": [], "members": ["0x20"],
           "annotations": [{"kind": "struct", "name": "S", "size": 8, "fields": [{"off": 0, "type": "i32", "name": "a"},
                                                                                {"off": 4, "type": "i32", "name": "b"}]},
                           {"kind": "struct", "name": "Gone", "size": 4, "fields": []},
                           {"kind": "function", "rva": "0x20", "name": "B", "cc": "__thiscall", "ret": "void", "params": []},
                           {"kind": "equate", "rva": "0x30", "value": 1, "name": "ONE"}],
           "locals": [{"rva": "0x10", "offset": -4, "type": "i32", "name": "x"}], "globals": [{"rva": "0x40", "name": "g"}]}
    new = json.loads(json.dumps(old))
    new["functions"] = [{"rva": "0x20", "name": "B"}]                       # name of 0x10 deleted
    new["annotations"][0]["fields"] = [{"off": 0, "type": "i32", "name": "a"}]  # field +4 deleted
    del new["annotations"][1]                                                # struct Gone deleted
    new["annotations"] = [a for a in new["annotations"] if a.get("kind") != "equate"]
    new["globals"] = []
    delta, revert = ledger.facts_changes(old, new)
    check(revert.get("functions") == ["0x10"] and revert.get("fields") == [{"name": "S", "off": 4}] and revert.get("types") == ["Gone"],
          f"removed name, field and type are reverted ({revert})")
    check(revert.get("globals") == ["0x40"] and revert.get("equates") == [{"rva": "0x30", "value": 1}], "removed global and equate are reverted")
    check(any(l["rva"] == "0x10" for l in delta.get("locals", [])), "a reverted function gets its remaining facts again (its local)")
    check(any(a.get("name") == "S" for a in delta.get("annotations", [])), "a structure with a removed field gets its remaining fields again")
    renamed = json.loads(json.dumps(old))
    renamed["annotations"][0]["name"] = "S2"
    delta, revert = ledger.facts_changes(old, renamed, [["S", "S2"]])
    check(revert == {"renames": [["S", "S2"]]} and "annotations" not in delta, f"a type rename is a rename, not a delete and an add ({revert}, {delta})")


def test_smart_diff():
    before = "void f(void)\n{\n  int iVar3;\n  uint uVar2;\n  iVar3 = g(local_1c);\n  uVar2 = h();\n  return;\n}\n"
    after = "void f(void)\n{\n  int iVar4;\n  uint uVar1;\n  iVar4 = g(local_20);\n  uVar1 = CAThing(this->count);\n  return;\n}\n"
    d, dropped = client.smart_diff(before, after, "0x10", 1)
    changed = [l for l in d if l[:1] in "+-" and not l.startswith(("---", "+++"))]
    check(len(changed) == 2 and "CAThing" in changed[1], f"renumbering is ignored, the real change shown ({changed})")
    d, dropped = client.smart_diff(before, after, "0x10", 1, {"Unrelated"})
    check(d == [] and dropped == 1, "a caller's hunk without the changed name is hidden")
    d, dropped = client.smart_diff(before, after, "0x10", 1, {"count"})
    check(len(d) > 0 and dropped == 0, "a caller's hunk naming the changed field is shown")


def test_ledger_derivation():
    files = ledger.read_db(ROOT)
    tmp = Path(tempfile.mkdtemp(prefix="bz-led-"))
    (tmp / "l.json").write_text(json.dumps({"files": files}), encoding="utf-8")
    t0 = time.time()
    client.make_facts(tmp / "l.json", tmp / "from-ledger.json")
    client.make_facts(tmp / "l.json", tmp / "from-ledger2.json")
    default = subprocess.run([sys.executable, "-I", str(TOOLS / "annotate.py"), "--out", str(tmp / "from-db.json")], capture_output=True)
    print(f"     three derivations: {time.time() - t0:.1f} s")
    a, b, c = ((tmp / n).read_bytes() for n in ("from-ledger.json", "from-ledger2.json", "from-db.json"))
    check(default.returncode == 0 and a == c, "facts derived from the ledger equal those derived from db/")
    check(a == b, "derivation is deterministic")
    shutil.rmtree(tmp)


# ---------------------------------------------------------------- with a server

def annotate_file(ov, *flags):
    f = TEST_DIR / "ov-ca.json"
    f.write_text(json.dumps(ov), encoding="utf-8")
    t0 = time.time()
    rc, out = run_cli("annotate", "--apply", str(f), "--lane", "CAT", *flags)
    return rc, out, time.time() - t0


def test_lines(text):
    """The annotation lines the test recorded (lanes CAT, BZT), not the real db/ entries, which name the same kinds."""
    return "\n".join(l for l in text.splitlines() if l.strip() and json.loads(l).get("lane") in ("CAT", "BZT"))


def entries_of(kind=None):
    text = test_lines(client.request("/ledger")["files"]["annotations.jsonl"])
    return [json.loads(l) for l in text.splitlines() if l.strip() and (kind is None or json.loads(l)["kind"] == kind)]


def test_new_kinds_and_keys(pre0):
    """New annotation kinds applied and persisted; --confirm, --rename, --delete through the server; deleting every fact on
    the probe function gives back its pseudo-code from before any annotation."""
    v = client.request("/vars", {"rva": PROBE})["vars"]
    reg = [x for x in v if not x["param"] and "at" in x and "offset" not in x]
    rc, out = run_cli("vars", PROBE)
    check(rc == 0 and len(out.splitlines()) == len(v) + 1, f"vars lists the decompiler's variables ({len(v)})")
    check(len(reg) > 0, "the probe has register locals with a first-use address")
    var = reg[0]["name"]
    before = decompile(PROBE)
    ov = {"annotations": [{"kind": "enum", "name": "CAEnum", "size": 4, "values": {"kCANodeSize": 0x1c, "kCAOther": 2}},
                          {"kind": "equate", "rva": "0x1390", "value": 0x1c, "enum": "CAEnum"},
                          {"kind": "callsite", "rva": "0x144c", "cc": "__cdecl", "ret": "i32",
                           "params": [{"name": "ctx", "type": "u32"}, {"name": "node", "type": "BZProbe*"}, {"name": "n", "type": "i32"}]},
                          {"kind": "flags", "rva": "0x831a54", "noreturn": True},
                          {"kind": "function", "rva": PROBE, "name": "CAProbeFn", "cc": "__thiscall", "ret": "i32", "params": [],
                           "this": "BZProbe"}],
          "locals": [{"rva": PROBE, "var": var, "type": "i32", "name": "caLocal"}]}
    rc, out, secs = annotate_file(ov)
    print(f"     annotate of 6 new-kind entries: {secs:.1f} s, {len(out.splitlines())} output lines")
    print("\n".join(out.splitlines()[:10]))
    text = decompile(PROBE)
    check(rc == 0 and "kCANodeSize" in text, "an equate from an enum shows by name")
    check("t_caLocal" in text, "a local named by its decompiler name is renamed (t_ while tentative)")
    check("BZProbe *this" in text and "t_CAProbeFn" in text, "`this` forced to a structure on a __thiscall function")
    check(text != before, "call-site override and noreturn change the pseudo-code")
    loc = entries_of("local")
    check(len(loc) == 1 and "at" in loc[0] and "var" not in loc[0], f"the local is recorded by first use, not by name ({loc})")
    # confirm without resending
    rc, out = run_cli("annotate", "--confirm", f"{PROBE}@caLocal", PROBE, "--lane", "CAT")
    print("\n".join(out.splitlines()[:4]))
    text = decompile(PROBE)
    check(rc == 0 and "caLocal" in text and "t_caLocal" not in text and "CAProbeFn" in text and "t_CAProbeFn" not in text,
          "--confirm promotes the local and the function (no t_ prefix)")
    check(not any(e.get("tentative") for e in entries_of("local")), "confirmed in the ledger")
    rc, out = run_cli("annotate", "--confirm", "NoSuchKey", "--lane", "CAT")
    check(rc == 2, "an unknown key is refused (nothing recorded)")
    # rename a type: its uses follow
    t0 = time.time()
    rc, out = run_cli("annotate", "--rename", "BZProbe", "CAProbe", "--lane", "CAT")
    print(f"     rename of a structure: {time.time() - t0:.1f} s, {len(out.splitlines())} output lines")
    text = decompile(PROBE)
    ann = client.request("/ledger")["files"]["annotations.jsonl"]
    check(rc == 0 and "CAProbe *this" in text and "BZProbe" not in text, "a renamed structure shows under its new name")
    check('"BZProbe' not in ann and '"this": "CAProbe"' in ann, "the ledger renamed the structure and its uses")
    rc, out = run_cli("annotate", "--rename", "CAProbe.probeField", "caField", "--lane", "CAT")
    check(rc == 0 and '"caField"' in client.request("/ledger")["files"]["annotations.jsonl"], "a field renamed by key")
    # the whole ledger still exports and reads back identically
    cur = client.request("/ledger")
    tmp = Path(tempfile.mkdtemp(prefix="ca-ex-"))
    ledger.write_files(tmp, cur["files"])
    check(ledger.read_db(tmp) == {k: v for k, v in cur["files"].items() if k != ledger.OPS}, "the new kinds round-trip through export")
    shutil.rmtree(tmp)
    # survives a restart (the saved project)
    run_cli("stop")
    time.sleep(2)
    rc, out = run_cli("start")
    check(rc == 0 and client.running()["annotated"] == "reused", "restart reuses the saved project")
    text = decompile(PROBE)
    check("kCANodeSize" in text and "caLocal" in text and "CAProbe *this" in text, "new kinds survive a restart")
    # delete everything on the probe: back to the pseudo-code before any annotation
    t0 = time.time()
    rc, out = run_cli("annotate", "--delete", PROBE, f"{PROBE}@caLocal", "equate:0x1390", "callsite:0x144c", "0x831a54",
                      "CAEnum", "CAProbe", "--lane", "CAT")
    print(f"     delete of 7 facts: {time.time() - t0:.1f} s")
    print("\n".join(out.splitlines()[:12]))
    after = decompile(PROBE)
    check(rc == 0 and after == pre0, "after deleting every fact on it, the function decompiles as before any annotation")
    if after != pre0:
        print("\n".join(client.smart_diff(pre0, after, PROBE)[0][:30]))
    ann = test_lines(client.request("/ledger")["files"]["annotations.jsonl"])
    check(not any(s in ann for s in ("CAEnum", "CAProbe", "caLocal", '"callsite"', '"equate"', '"flags"')),
          "the ledger dropped the deleted entries")
    # export --lane CAT on a copy of db/: the deletes cancel the adds (BZT's entries are another lane's)
    work = Path(tempfile.mkdtemp(prefix="ca-lane-"))
    for n in ledger.read_db(ROOT):
        (work / "db" / n).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "db" / n, work / "db" / n)
    target = ledger.read_db(work)
    ledger.replay(target, [op for op in ledger.ops(client.request("/ledger")["files"]) if op["lane"] in ("BZT", "CAT")])
    led = client.request("/ledger")["files"]
    check(target == {k: v for k, v in led.items() if k != ledger.OPS}, "replaying both lanes' operations on db/ gives the ledger")
    shutil.rmtree(work, ignore_errors=True)


def decompile(rva, **kw):
    r = client.request("/decompile", {"rvas": [rva], **kw})
    return r["results"][0]["text"]


def test_eh_frames():
    """/O1 functions with an EH frame decompile past their _EH_prolog call: db/annotations.jsonl says 0x8248b0 returns
    (flags noreturn false), and ApplyDb un-cuts the calls the analysis ended there (returnAgain) before it creates the missed
    functions. Before that fix each of these decompiled to the one call (5 statement lines or fewer)."""
    flags = [json.loads(l) for l in (ROOT / "db" / "annotations.jsonl").read_text(encoding="utf-8").splitlines()
             if '"flags"' in l and '"0x8248b0"' in l]
    if not any(f.get("noreturn") is False for f in flags):
        print("SKIP EH-frame check: db/annotations.jsonl has no flags 0x8248b0 noreturn false")
        return
    # RunFramework (a Ghidra function), cTSSaveNeighborhoodController::Step, a controller ApplyDb creates (db/missed-functions.jsonl)
    for rva in ("0xc2d0a8", "0xc489e8", "0xc375ae"):
        text = decompile(rva)
        statements = [l for l in text.splitlines() if l.rstrip().endswith(";") and not l.startswith("//")]
        check(len(statements) > 12, f"EH-framed {rva} decompiles past _EH_prolog ({len(statements)} statement lines)")


def test_server():
    TEST_DIR.mkdir(exist_ok=True)
    t0 = time.time()
    rc, out = run_cli("start")
    cold = time.time() - t0
    print(out)
    st = client.running()
    check(rc == 0 and st is not None and st["annotated"] == "built", f"cold start builds the annotated project ({cold:.0f} s)")
    print(f"     ApplyDb {st['apply_ms'] / 1000:.1f} s (server's own measure)")
    test_eh_frames()
    pre0 = decompile(PROBE)  # before any annotation: what deleting them all must give back
    # --- ledger through Ghidra == db/, byte for byte
    cur = client.request("/ledger")
    tmp = Path(tempfile.mkdtemp(prefix="bz-ex-"))
    ledger.write_files(tmp, cur["files"])
    names = ledger.read_db(ROOT)
    same = all((tmp / "db" / n).read_bytes() == (ROOT / "db" / n).read_bytes() for n in names)
    check(same and len(list((tmp / "db").rglob("*.jsonl"))) == len(names), "export from the annotated program is byte-identical to db/")
    # --- annotate: struct + signature
    ov = {"annotations": [
        {"kind": "struct", "name": "BZProbe", "size": 16, "fields": [{"off": 4, "type": "i32", "name": "probeField"}]},
        {"kind": "function", "rva": PROBE, "name": "BZProbeFn", "cc": "__thiscall", "ret": "i32",
         "params": [{"name": "this", "type": "BZProbe*"}]}]}
    f = TEST_DIR / "ov.json"
    f.write_text(json.dumps(ov), encoding="utf-8")
    t0 = time.time()
    rc, out = run_cli("annotate", "--apply", str(f), "--lane", "BZT")
    print(f"     annotate round trip: {time.time() - t0:.1f} s")
    print("\n".join(out.splitlines()[:12]))
    check(rc == 0 and "applied" in out and "BZProbeFn" in decompile(PROBE), "annotate applies and the plain decompile shows it")
    check("--- " in out and "+++ " in out, "annotate prints a diff of the affected function")
    check(len(out.splitlines()) < 150, "annotate output is short")
    led = client.request("/ledger")
    check(led["version"] == cur["version"] + 1, "ledger version advanced")
    # --- export --lane into a copy of db/ shows exactly the change
    work = Path(tempfile.mkdtemp(prefix="bz-lane-"))
    for n in ledger.read_db(ROOT):
        (work / "db" / n).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "db" / n, work / "db" / n)
    target = ledger.read_db(work)
    mine = [op for op in ledger.ops(led["files"]) if op["lane"] == "BZT"]
    ledger.replay(target, mine)
    changed = ledger.write_files(work, target)
    check(changed == ["annotations.jsonl"], "export --lane touches only annotations.jsonl")
    old = (ROOT / "db" / "annotations.jsonl").read_text(encoding="utf-8").splitlines()
    new = (work / "db" / "annotations.jsonl").read_text(encoding="utf-8").splitlines()
    added = [l for l in new if l not in old]
    check(new[:len(old)] == old and len(added) == 2 and all('"lane": "BZT"' in l and '"tentative": true' in l for l in added),
          "db diff is exactly the two annotated entries, with lane and tentative")
    # whole-ledger export differs from db/ only by those lines
    full = Path(tempfile.mkdtemp(prefix="bz-full-"))
    ledger.write_files(full, led["files"])
    diff_files = [n for n in names if (full / "db" / n).read_bytes() != (ROOT / "db" / n).read_bytes()]
    check(diff_files == ["annotations.jsonl"], "whole export differs from db/ in annotations.jsonl only")
    # --- confirm: same entries, replaced
    rc, out = run_cli("annotate", "--apply", str(f), "--lane", "BZT", "--confirmed")
    led2 = client.request("/ledger")
    probe = [l for l in led2["files"]["annotations.jsonl"].splitlines() if "BZProbeFn" in l]
    check(rc == 0 and len(probe) == 1 and "tentative" not in probe[0], "re-annotating with --confirmed replaces the entry")
    # --- unapplied: all or nothing
    bad = {"annotations": [{"kind": "function", "rva": PROBE, "name": "BZBad", "cc": "__thiscall", "ret": "i32",
                            "params": [{"name": "this", "type": "NoSuchType*"}]},
                           {"kind": "struct", "name": "BZGood", "size": 8, "fields": []}]}
    f.write_text(json.dumps(bad), encoding="utf-8")
    rc, out = run_cli("annotate", "--apply", str(f), "--lane", "BZT")
    check(rc == 1 and "NOTHING recorded" in out and "NoSuchType" in out, "an unapplicable fact rolls the whole request back")
    check(client.request("/ledger")["version"] == led2["version"] and "BZBad" not in decompile(PROBE), "nothing recorded, nothing changed")
    # --- method fact for a class not in the default list
    mv = {"methods": {"cTSString": [{"iface": "cITSString", "class": "cTSString", "slot": 1, "name": "BZSlot", "ret": "u32", "params": [],
                                      "source": "test"}]}}
    f.write_text(json.dumps(mv), encoding="utf-8")
    rc, out = run_cli("annotate", "--apply", str(f), "--lane", "BZT")
    check(rc == 0 and "BZSlot" in client.request("/ledger")["files"]["methods/cTSString.jsonl"], "a method fact is recorded in its methods file")
    # --- decompile options
    text = decompile(RVA, addr=True)
    lines = text.replace("\r\n", "\n").splitlines()[1:]
    prefixed = [l for l in lines if l[7:9] == "| "]
    check(len(prefixed) == len(lines) and any(l[:7].strip() for l in lines), "--addr prefixes every line, with addresses on code lines")
    rc, out = run_cli("decompile", "--callees", "--max-lines", "30", RVA)
    check("// callees:" in out and "more lines" in out, "--callees and --max-lines")
    f.write_text(json.dumps({"functions": [{"rva": PROBE, "name": "t_BZDiffName"}]}), encoding="utf-8")
    rc, out = run_cli("decompile", "--diff", "--overlay", str(f), "--context", "1", PROBE)
    print(f"     overlay diff output: {out.count(chr(10))} lines")
    check("t_BZDiffName" in out and out.count("\n") < 60 and "@@" in out, "--diff --overlay prints a short unified diff")
    check("t_BZDiffName" not in decompile(PROBE), "the overlay left no trace")
    try:
        client.request("/decompile", {"rvas": [RVA], "out": "C:/Windows/Temp/bz-out"})
        check(False, "output directory outside local/ is refused")
    except RuntimeError as e:
        check("outside" in str(e), "output directory outside local/ is refused")
    # --- snapshot with rotation
    bk = TEST_DIR / "backups"
    bk.mkdir(exist_ok=True)
    for i in range(11):
        (bk / f"2000010{i % 10}-0000{i:02d}.zip").write_bytes(b"x")
    t0 = time.time()
    rc, out = run_cli("snapshot")
    print(f"     snapshot: {time.time() - t0:.1f} s: {out.strip()}")
    zips = sorted(bk.glob("*.zip"))
    check(rc == 0 and len(zips) == 10 and zips[-1].stat().st_size > 1_000_000, "snapshot written, last 10 kept")
    # --- restart from the cache: the annotation survives
    pid = client.running()["pid"]
    run_cli("stop")
    time.sleep(2)
    check(not client.alive(pid), "stop leaves no server process")
    t0 = time.time()
    rc, out = run_cli("start")
    warm = time.time() - t0
    st = client.running()
    print(out)
    check(rc == 0 and st["annotated"] == "reused", f"restart reuses the saved project ({warm:.0f} s)")
    check("BZProbeFn" in decompile(PROBE), "an annotation committed before the restart is still there")
    check("BZProbe" in client.request("/ledger")["files"]["annotations.jsonl"], "the ledger survived the restart")
    test_new_kinds_and_keys(pre0)
    pid = client.running()["pid"]
    run_cli("stop")
    time.sleep(2)
    check(not client.alive(pid), "final stop leaves no server process")
    for d in (tmp, work, full):
        shutil.rmtree(d, ignore_errors=True)


def main():
    keep = "--keep" in sys.argv
    test_roundtrip_offline()
    test_method_keys()
    test_refusals()
    test_class_facts()
    test_symbol_names()
    test_upsert_and_replay()
    test_keys_and_ops()
    test_facts_changes()
    test_smart_diff()
    test_ledger_derivation()
    if "--offline" not in sys.argv:
        if not (client.LOCAL / "ghidra" / "Sims2EP9.gpr").exists() or not list(client.LOCAL.joinpath("tools").glob("ghidra_*")):
            print("SKIP server part: local/ghidra or Ghidra not present")
        else:
            try:
                test_server()
            finally:
                if client.running():
                    client.cmd_stop(None)
                if not keep:
                    shutil.rmtree(TEST_DIR, ignore_errors=True)
    print("FAILED: " + "; ".join(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
