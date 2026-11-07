#!/usr/bin/env python3
"""Tests for tools/ifacegen.py: aliases (alias_of), duplicate-interface warnings, refusals. Run:

  tools/py -I tools/tests/test_ifacegen.py
"""
import shutil
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import ifacegen  # noqa: E402

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def fact(iface, slot, name, ret="u32", params=(), **kw):
    return dict({"iface": iface, "slot": slot, "name": name, "ret": ret, "params": list(params), "_where": f"t.jsonl:{slot}"}, **kw)


CLASSES = {"cGZCOM": {0: 8}, "cTSThing": {0: 10}, "cITSBase": {0: 6}}


def test_aliases():
    facts = [fact("cIGZCOM", 3, "GetClassObject", class_="x"),
             fact("cIGZCOM", 4, "CreateInstance"),
             {"iface": "NbrFramework", "alias_of": "cIGZCOM", "source": "merged", "_where": "t.jsonl:9"},
             fact("User", 0, "GetFramework", ret="NbrFramework*"),
             fact("User", 1, "Take", ret="void", params=[{"type": "NbrFramework*", "name": "fw"}])]
    for f in facts:
        f.pop("class_", None)
    check(ifacegen.check(facts, CLASSES) == [], "an alias line passes --check")
    out = ifacegen.build(facts, CLASSES)
    check("using NbrFramework = cIGZCOM;" in out["NbrFramework"] and '#include "ifaces/cIGZCOM.h"' in out["NbrFramework"],
          "the alias header is a using declaration over the target's header")
    check("virtual cIGZCOM * GetFramework() = 0;" in out["User"] and "virtual void Take(cIGZCOM *fw) = 0;" in out["User"],
          "a type naming the alias means the target")
    check("class cIGZCOM;" in out["User"] and "class NbrFramework;" not in out["User"], "forward declarations name the target")
    bad = facts + [fact("NbrFramework", 5, "Leftover")]
    check(any("still has facts" in e for e in ifacegen.check(bad, CLASSES)), "an alias with facts of its own is an error")
    chain = facts + [{"iface": "ComFramework", "alias_of": "NbrFramework", "_where": "t.jsonl:10"}]
    check(any("itself an alias" in e for e in ifacegen.check(chain, CLASSES)), "an alias of an alias is an error")
    dangling = [{"iface": "Old", "alias_of": "Nowhere", "_where": "t.jsonl:1"}]
    check(any("has no facts" in e for e in ifacegen.check(dangling, CLASSES)), "an alias of an unknown interface is an error")
    extra = facts + [{"iface": "Other", "alias_of": "cIGZCOM", "slot": 3, "_where": "t.jsonl:11"}]
    check(any("only iface and alias_of" in e for e in ifacegen.check(extra, CLASSES)), "an alias line with a slot is an error")
    # extends through an alias
    ext = [fact("cITSBase", 0, "Q", **{"class": "cITSBase"}), {"iface": "BaseOld", "alias_of": "cITSBase", "_where": "t.jsonl:2"},
           {"iface": "Derived", "extends": "BaseOld", "_where": "t.jsonl:3"}, fact("Derived", 7, "More")]
    out = ifacegen.build(ext, CLASSES)
    check("class Derived : public cITSBase" in out["Derived"], "extends of an alias derives from the target")
    tmp = Path(tempfile.mkdtemp(prefix="cm-ifacegen-"))
    (tmp / "src").mkdir()
    (tmp / "src" / "a.h").write_text("namespace ifaces {\nclass NbrFramework;\nclass cIGZCOM;\n}\n", encoding="utf-8")
    fd = ifacegen.forward_declared_aliases(ifacegen.aliases_of(facts), tmp / "src")
    check(len(fd) == 1 and fd[0].startswith("src/a.h:2:") and "class cIGZCOM;" in fd[0], "a forward declaration of an alias is named")
    shutil.rmtree(tmp)


def test_duplicate_warnings():
    t = {"class": "cTSThing"}
    facts = [fact("GscContext", 3, "A", **t), fact("TokenSingleton", 4, "B", **t)]
    w = ifacegen.duplicate_warnings(facts)
    check(len(w) == 1 and "GscContext" in w[0] and "TokenSingleton" in w[0], "two unrelated interfaces on one vtable are reported")
    chain = [fact("cITSBase", 3, "A", **t), fact("cITSDerived", 7, "B", **t), {"iface": "cITSDerived", "extends": "cITSBase"}]
    check(ifacegen.duplicate_warnings(chain) == [], "base and derived interfaces on one vtable are not")
    same = [fact("NbrInStream", s, n) for s, n in ((5, "GetUint8"), (8, "GetSint32"), (9, "GetUint32"))] + \
           [fact("FamilyInStream", s, n) for s, n in ((5, "GetUint8"), (8, "GetSint32"), (9, "GetUint32"))]
    w = ifacegen.duplicate_warnings(same)
    check(len(w) == 1 and "3 slots alike" in w[0], "interfaces naming three slots alike are reported")
    check(ifacegen.duplicate_warnings(same, {"NbrInStream", "FamilyInStream"}) == [], "unless both are RTTI names")
    iunk = [fact(i, s, n) for i in ("X", "Y") for s, n in ((0, "QueryInterface"), (1, "AddRef"), (2, "Release"))]
    check(ifacegen.duplicate_warnings(iunk) == [], "IUnknown's three slots don't count")


def test_real_db():
    db = TOOLS.parent / "db"
    facts = ifacegen.load(db)
    classes = ifacegen.load_classes(db)
    check(ifacegen.check(facts, classes) == [], "db/methods passes --check")
    check(len(ifacegen.build(facts, classes)) > 100, "db/methods builds")


def main():
    test_aliases()
    test_duplicate_warnings()
    test_real_db()
    print("FAILED: " + "; ".join(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
