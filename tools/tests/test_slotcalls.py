#!/usr/bin/env python3
"""Tests for tools/slotcalls.py (slot-number calls -> candidate interface methods). Run:

  tools/py -I tools/tests/test_slotcalls.py
"""
import shutil
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import slotcalls  # noqa: E402

failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def fact(iface, slot, name, nparams=0, cls=None, tentative=False):
    f = {"iface": iface, "slot": slot, "name": name, "ret": "u32", "params": [{"type": "u32", "name": f"p{i}"} for i in range(nparams)],
         "_where": f"{iface}.jsonl:{slot}"}
    if cls:
        f["class"] = cls
    if tentative:
        f["tentative"] = True
    return f


FACTS = [fact("cIGZIStream", 9, "GetUint32", 1), fact("NbrInStream", 9, "GetUint32", 1, tentative=True),
         fact("cIGZOStream", 9, "SetUint32", 1), fact("cITSWidget", 23, "Frob", 2, cls="cTSWidget"),
         fact("cITSWidget", 24, "Twiddle", 0, cls="cTSWidget"), fact("Other", 23, "Unrelated", 2),
         fact("Other", 24, "TwoArgs", 2)]

SOURCE = """#include "cTSWidget.h"
namespace { constexpr int kFrobSlot = 23; }
void cTSWidget::Run(cIGZIStream *in, void *thing)
{
    uint32_t v;
    Call<uint8_t>(in, 9, &v);
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), kFrobSlot>(this,
        1u, 2u);
    static_cast<NarrowSlot<24, bool> *>(this)->call();
    VirtualSlot<void(__thiscall *)(void *)>(thing, 24)(thing);
    Call(thing, 77);
    Call<int>(Helper(1, 2), 23, 1, 2);
}
template <class Fn> Fn Generic(void *object, int slot) { return VirtualSlot<Fn>(object, slot); }
"""


HELPER_HEADER = """#pragma once
namespace toolbar {
template <int S, class R = uint32_t, class... A> R V(const void *object, A... a)
{
    return VirtualCall<R(__thiscall *)(void *, A...), S>(object, a...);
}
} // namespace toolbar
"""

HELPER_SOURCE = """#include "ui/Helpers.h"
namespace {
template <int Slot> uint32_t WordAt(void *object) { return VirtualCall<uint32_t(__thiscall *)(void *), Slot>(object); }
template <int Slot> bool KeyFits(void *object, const Key *want)
{
    return SameKey(want, V<Slot, const Key *>(object)) || V<Slot, const Key *>(object) == nullptr;
}
template <int Slot> void ShaderPass(State *state, uint32_t value)
{
    VirtualCall<PassFn, Slot>(state->mShader.Get(), state->mPassIndex, value);
}
constexpr int kWidthSlot = 12;
} // namespace
void cTSWidget::Run(cIGZIStream *in, void *thing, State *state)
{
    V<9, bool>(in, &v);
    V<23>(this, 1u, 2u);
    WordAt<24>(thing);
    KeyFits<29>(thing, nullptr);
    ShaderPass<7>(state, 1u);
    RawSlot<kWidthSlot, float>(V<3, void *>(thing, 0u), 1, 2);
    std::vector<V> list; // a type named V is not a call
}
"""


def check_helpers(tmp, facts):
    (tmp / "src" / "ui").mkdir(parents=True)
    (tmp / "src" / "ui" / "Helpers.h").write_text(HELPER_HEADER, encoding="utf-8")
    src = tmp / "src" / "ui" / "cTSWidget.cpp"
    src.write_text(HELPER_SOURCE, encoding="utf-8")
    helpers = slotcalls.file_helpers(src)
    check({"V", "WordAt", "KeyFits", "ShaderPass", "RawSlot"} <= set(helpers),
          "helpers found: V (included header), WordAt, KeyFits (built on V), ShaderPass, RawSlot (src/game/vtable.h)")
    check(helpers["V"]["tindex"] == 0 and helpers["V"]["obj_param"] == 0 and helpers["V"]["pack"] == "a",
          "V: slot is template parameter 0, the object parameter 0, `a` the forwarded pack")
    check(helpers["ShaderPass"]["obj_param"] is None and helpers["ShaderPass"]["obj"] == "state->mShader.Get()",
          "a helper whose receiver is not a parameter keeps the expression")
    calls = slotcalls.analyse(src, facts)
    got = [(c["line"], c["form"], c["slot"], c["obj"], c["nargs"]) for c in calls]
    print("     " + repr(got))
    check(len(calls) == 7, "seven helper calls; the helpers' own bodies (slot a template parameter) and `vector<V>` are none")
    byform = {(c["form"], c["slot"]): c for c in calls}
    c = byform.get(("V", 9))
    check(c is not None and c["obj"] == "in" and c["nargs"] == 1 and c["by"] == "type"
          and [f["name"] for f in c["candidates"]] == ["GetUint32"], "V<9, bool>(in, &v): one argument, typed receiver narrows")
    c = byform.get(("V", 23))
    check(c is not None and c["by"] == "this" and c["nargs"] == 2, "V<23>(this, 1u, 2u): `this` in cTSWidget.cpp, two arguments")
    c = byform.get(("WordAt", 24))
    check(c is not None and c["obj"] == "thing" and c["nargs"] == 0, "WordAt<24>(thing): no arguments")
    c = byform.get(("KeyFits", 29))
    check(c is not None and c["obj"] == "thing" and c["nargs"] == 0, "KeyFits<29>(thing, want): nested helper, V's argument count")
    c = byform.get(("ShaderPass", 7))
    check(c is not None and c["obj"] == "state->mShader.Get()" and c["nargs"] == 2,
          "ShaderPass<7>(state, 1u): receiver from the body, two arguments")
    c = byform.get(("RawSlot", 12))
    check(c is not None and c["obj"] == "V<3, void *>(thing, 0u)" and c["nargs"] == 2,
          "RawSlot<kWidthSlot, float>(V<3, void *>(...), 1, 2): constant slot; a template call as receiver stays whole")
    check(("V", 3) in byform and byform[("V", 3)]["nargs"] == 1, "the nested V<3, void *>(thing, 0u) is a call of its own")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="cm-slotcalls-"))
    try:
        (tmp / "cTSWidget.h").write_text("struct cTSWidget { static constexpr int kUnused = 5; };\n", encoding="utf-8")
        src = tmp / "cTSWidget.cpp"
        src.write_text(SOURCE, encoding="utf-8")
        by_slot, by_iface, classes = {}, {}, {}
        for f in FACTS:
            by_slot.setdefault(f["slot"], []).append(f)
            by_iface.setdefault(f["iface"], []).append(f)
            if f.get("class"):
                classes.setdefault(f["class"], set()).add(f["iface"])
        calls = slotcalls.analyse(src, (by_slot, by_iface, classes))
        got = [(c["line"], c["form"], c["slot"], c["obj"], c["nargs"]) for c in calls]
        print("     " + repr(got))
        check(len(calls) == 6, "six slot calls found; the helper definition with a variable slot is not one")
        names = lambda c: sorted(f"{f['iface']}::{f['name']}" for f in c["candidates"])  # noqa: E731
        c = calls[0]
        check(c["form"] == "Call" and c["line"] == 6 and c["by"] == "type" and names(c) == ["cIGZIStream::GetUint32"],
              "Call<T>(obj, N, ...) on a declared cIGZIStream * narrows to that interface")
        c = calls[1]
        check(c["form"] == "VirtualCall" and c["slot"] == 23 and c["line"] == 7 and c["by"] == "this" and names(c) == ["cITSWidget::Frob"],
              "VirtualCall with a named slot constant on `this` in cTSWidget.cpp narrows to the class's interfaces")
        c = calls[2]
        check(c["form"] == "NarrowSlot" and c["slot"] == 24 and c["nargs"] == 0 and names(c) == ["cITSWidget::Twiddle"],
              "static_cast<NarrowSlot<N, R, A...> *>(obj)->call()")
        c = calls[3]
        check(c["form"] == "VirtualSlot" and c["slot"] == 24 and c["by"] == "slot" and names(c) == ["cITSWidget::Twiddle"],
              "VirtualSlot<Fn>(obj, N)(obj): unknown receiver, every interface at the slot, filtered by arity")
        c = calls[4]
        check(c["slot"] == 77 and c["candidates"] == [], "a slot no fact names is UNNAMED")
        c = calls[5]
        check(c["obj"] == "Helper(1, 2)" and c["nargs"] == 2 and names(c) == ["Other::Unrelated", "cITSWidget::Frob"],
              "a call expression as the object; nested commas don't split arguments")
        groups = slotcalls.by_method(FACTS[:3])
        check(groups[0] == ("GetUint32", ["cIGZIStream", "NbrInStream~"]), "candidates grouped by method, RTTI and confirmed first")
        n = slotcalls.file_counts(calls)
        check(n == {"calls": 6, "nameable": 3, "several": 2, "unnamed": 1},
              f"--summary counts: typed / this receivers with one method are nameable, the rest several or unnamed ({n})")
        check([p.name for p in slotcalls.expand([str(tmp)])] == ["cTSWidget.cpp", "cTSWidget.h"],
              "a directory expands to its .cpp / .h files")
        check_helpers(tmp, (by_slot, by_iface, classes))
        check(slotcalls.slot_value("1u", {}) == 1 and slotcalls.slot_value("0x10UL", {}) == 16
              and slotcalls.slot_value("static_cast<int>(3u)", {}) == 3 and slotcalls.slot_value("kSlot", {"kSlot": 7}) == 7,
              "slot numbers with integer suffixes")
    finally:
        shutil.rmtree(tmp)
    print("FAILED: " + "; ".join(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
