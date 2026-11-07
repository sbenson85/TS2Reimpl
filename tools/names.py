"""Builds db/names.jsonl: best-known name for each function, with where the name came from.

Sources, highest priority first:
  community  TS2-Extender's named lookups that match a function entry (db/community-hooks.jsonl)
  library    CRT/vcruntime/STL functions identified byte-for-byte (db/library-matches.jsonl),
             demangled with dbghelp's UnDecorateSymbolName
  ghidra-fid library functions named by Ghidra's function ID during analysis (mostly CRT code
             from an older UCRT than ours, which libmatch can't match)
  vtable+interface  as vtable, with the method name from db/interface-slots.jsonl when the slot
             belongs to a known cIGZ* interface at that subobject offset
  vtable     virtual functions: Class::vf<slot>, where Class is the most-base class whose vtable
             holds the function at that slot (if the classes holding it don't share one base, the
             function is COMDAT-folded/shared and named shared_vf_<rva> instead)
Each line: {"rva", "name", "source", "detail"}. Facts only.

Usage: python -I tools/names.py
"""
import ctypes
import json
from collections import defaultdict
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def undecorate(name, name_only=True):
    """Demangles an MSVC decorated name with dbghelp (UNDNAME_NAME_ONLY gives just the qualified name)."""
    if not name.startswith("?"):
        # C names: cdecl/stdcall add one leading underscore; stdcall also appends @<arg bytes>.
        name = name[1:] if name.startswith("_") else name
        return name.split("@")[0] if "@" in name else name
    buf = ctypes.create_string_buffer(2048)
    flags = 0x1000 if name_only else 0
    n = ctypes.windll.dbghelp.UnDecorateSymbolName(name.encode("latin-1"), buf, len(buf), flags)
    return buf.value.decode("latin-1") if n else name


def main():
    classes = [json.loads(l) for l in open(ROOT / "db/classes.jsonl")]
    bases = {c["name"]: set(c["bases"]) for c in classes}
    base_at = {c["name"]: list(zip(c["bases"], c.get("base_offsets", []))) for c in classes}
    slots_of = {}
    path = ROOT / "db/interface-slots.jsonl"
    if path.exists():
        slots_of = {r["interface"]: r["slots"] for r in map(json.loads, open(path))}

    def method_name(cls, off, slot):
        """Method name for a vtable slot, from the largest known interface at that subobject offset."""
        known = [cls] if cls in slots_of and off == 0 else []
        known += [b for b, o in base_at.get(cls, []) if o == off and b in slots_of]
        known = [k for k in known if slot < len(slots_of[k])]
        if not known:
            return None, None
        best = max(known, key=lambda k: len(slots_of[k]))
        return slots_of[best][slot], best

    # function rva -> set of (class, subobject offset, slot)
    holders = defaultdict(set)
    for c in classes:
        for vt in c["vftables"]:
            for slot, e in enumerate(vt["entries"]):
                holders[e].add((c["name"], vt["offset"], slot))

    names = {}
    for f in map(json.loads, open(ROOT / "db/functions.jsonl")):
        if f["source"] == "ANALYSIS" and not f["name"].startswith(("FUN_", "thunk_")):
            names[f["rva"]] = {"name": f["name"], "source": "ghidra-fid", "detail": f["namespace"]}

    for rva, hs in holders.items():
        owners = {cls for cls, _, _ in hs}
        # The defining class is a holder that every other holder derives from.
        root = [c for c in owners if all(o == c or c in bases.get(o, ()) for o in owners)]
        if len(root) == 1:
            cls = root[0]
            slots = sorted({(off, slot) for c, off, slot in hs if c == cls})
            off, slot = slots[0]
            method, iface = method_name(cls, off, slot)
            if method:
                names[rva] = {"name": f"{cls}::{method}", "source": "vtable+interface",
                              "detail": f"{iface} slot {slot}; {len(owners)} classes share it"}
            else:
                label = f"vf{slot}" if off == 0 else f"vf{off}_{slot}"
                names[rva] = {"name": f"{cls}::{label}", "source": "vtable",
                              "detail": f"{len(owners)} classes share it"}
        else:
            names[rva] = {"name": f"shared_vf_{rva[2:]}", "source": "vtable",
                          "detail": f"in {len(owners)} unrelated classes (likely a folded trivial function)"}

    path = ROOT / "db/library-matches.jsonl"
    if path.exists():
        rows = [json.loads(l) for l in open(path)]
        chosen_at = {}
        for m in rows:
            if m.get("kind") == "inside" or tsdb.lookalike(m):
                continue  # pieces are named below; lookalikes are game code (tsdb.lookalike)
            # Byte-identical bodies (atoi/_wtoi wrappers) list several names; a hand-written "name" says which one the
            # game's callers use (its "note" says why); otherwise the names whose relocations fit the game's targets
            # (libmatch's names_by_relocs, e.g. wcstol rather than strtol). Then prefer a real instantiation over one
            # of tools/stlprobe/probe_sizes.cpp's stand-in element types (Blob4<N>), which never occur in the game.
            source = m.get("names_by_relocs") or m["names"]
            pretty = sorted({undecorate(n) for n in source})
            chosen = undecorate(m["name"]) if "name" in m else min(pretty, key=lambda n: ("Blob4<" in n, n))
            chosen_at[m["rva"]] = chosen
            detail = f"{', '.join(m['lib'])}; " + (f"identical to {len(pretty) - 1} more" if len(pretty) > 1 else undecorate(source[0], False))
            if m.get("family") or m.get("family_likely"):
                # The bytes match but the relocation targets are this instantiation's own (libmatch "calls").
                detail += ("; family label" if m.get("family") else "; likely a family label") + ", calls " + \
                    ", ".join(c["target"] for c in m.get("calls", ())[:4])
            if m.get("kind") == "unlisted":
                continue  # no function starts there (the db's record holds it); its pieces are named below
            names[m["rva"]] = {"name": chosen, "source": "library", "detail": detail}
        for m in rows:
            if m.get("kind") == "inside" and m["in"] in chosen_at:
                names[m["rva"]] = {"name": f"{chosen_at[m['in']]}+{m['offset']:#x}", "source": "library",
                                   "detail": f"piece of the library function at {m['in']} (a funclet or a tail the db "
                                             "split off)"}

    for r in map(json.loads, open(ROOT / "db/community-hooks.jsonl")):
        if r["at_start"]:
            names[r["function"]] = {"name": r["name"], "source": "community:" + r["mod"], "detail": r["note"]}

    with open(ROOT / "db/names.jsonl", "w", newline="\n") as f:
        for rva in sorted(names, key=lambda x: int(x, 16)):
            f.write(json.dumps({"rva": rva, **names[rva]}) + "\n")

    by_source = defaultdict(int)
    for n in names.values():
        key = n["source"] + (":shared" if n["name"].startswith("shared_vf_") else "")
        by_source[key] += 1
    print(len(names), "named:", dict(by_source))


if __name__ == "__main__":
    main()
