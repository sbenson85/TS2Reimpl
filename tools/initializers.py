"""Lists the game's static initialisers (the functions the CRT runs from its _initterm tables before
WinMain) and the static objects they build, as facts for stage 3 of docs/roadmap.md.

    .venv/Scripts/python -I tools/initializers.py            # writes both files below, prints counts
    .venv/Scripts/python -I tools/initializers.py --kind key-hash   # also lists the XC entries of one kind
    .venv/Scripts/python -I tools/initializers.py --rva 0x13d9ab    # one entry's line, pretty-printed

db/static-initializers.jsonl (XI and XC): {"rva", "size", "table" ("initterm_e" or "initterm"), "index",
"callees" (direct calls and tail jumps), "objects" (addresses loaded into ecx before a call: the static objects
whose constructors run here), "atexit" (functions registered with atexit: their destructors)}. Most initialisers
aren't in db/functions.jsonl (Ghidra didn't find them); tsdb.functions() merges them in so the other tools see
them as functions.

db/initializers.jsonl (XC only, one line per non-null slot, table order; docs/subsystems/startup.md "Stage 3"):
  slot      index in the XC table 0xd9a724..0xd9b3a8 (slot address = 0xd9a724 + 4 * slot)
  rva       the initialiser
  kind      what it does (KINDS below)
  writes    globals it stores to directly, and objects whose constructor it calls (ecx), as RVAs
  reads     globals it loads from (constants it copies, other statics it depends on: order constraints)
  ctors     [rva] functions called with a static object in ecx
  calls     [rva] other direct calls (not atexit, not ctors)
  atexit    [{"thunk", "objects", "calls"}]: the registered destructor thunk, the globals it touches, its calls
  class     the object's class if known (from a vtable it stores, or the constructor's / destructor's name)
  class_slots  for a class known by its vtable: "done/total" of its non-library vtable slots with a status
            other than "mapped" (null otherwise)
  subsystem by the functions that reference what it writes (db/subsystems.jsonl, class-backed), else by its
            callees; when only call-graph guesses vote, "other" with the winning guess in "guess"
  status    {rva: status} for its non-library ctors/calls/destructor calls that have a db/status entry
  ours      true when every non-library function it reaches (ctors, calls, destructor calls) has a status entry
            other than "mapped"; "owned" (the host skips it, our code reproduces it) comes from src, not here.
  after     slots of earlier entries that write a global this one reads (direct loads only: what its callees
            read is not seen); "before": the same for later entries (it reads the constant-initialised value)
Facts only: addresses, sizes, names, kinds. No bytes, no strings' contents.
"""
import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import (X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_EAX, X86_REG_ECX, X86_REG_EDX,
                          X86_REG_INVALID)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

# _initterm_e and _initterm tables (pointer arrays, null entries allowed), docs/startup-map.md.
TABLES = [("initterm_e", 0xD9B3AC, 0xD9B3D8), ("initterm", 0xD9A724, 0xD9B3A8)]
XC_FIRST = 0xD9A724

# Kinds, in the order the classifier tries them (docs/subsystems/startup.md "Stage 3").
KINDS = {
    "crt": "the CRT's or the STL's own state (locks, facet registry, _Init_atexit, new mode, tz, ATL module)",
    "key-hash": "a resource / property key: Crc24 / Crc32 of a string constant, stored in a global",
    "std-string": "a std::string constant (constructed from a literal, or its characters copied in), atexit destructor",
    "class-inline": "an object of a class whose constructor is inlined (stores a known vtable), atexit destructor",
    "container": "an empty STL / pool container (tree head node, hash buckets, pooled list head), atexit destructor",
    "object": "a class static via its constructor (ecx = the global), usually with an atexit destructor",
    "atexit-only": "a constant-initialised object: only registers its destructor",
    "value": "a value computed from other constants (copies, float arithmetic), no calls",
    "computed": "a global set from a function call's result (no object in ecx)",
    "registration": "a call that registers something elsewhere (no global written here)",
    "other": "none of the above",
}

CRT_NAMES = ("_Init_locks", "_Fac_tidy_reg_t", "_Init_atexit", "_set_new_mode", "__tzset", "CAtlBaseModule",
             "ios_base::Init", "_Init_thread")
CRC24, CRC32 = 0x9CBBDD, 0x9CBC3E
STRING_CALLS = ("basic_string<char", "_Allocate_for_capacity")
CONTAINER_CALLS = ("_Buyheadnode", "_Hash_vec", "RZBlockPoolAlloc", "_Tree<", "vector<", "_List")
OPERATOR_NEW_RVA = 0x824632
# what an inlined std::string destructor calls: sized operator delete 0x824624, _invalid_parameter_noinfo_noreturn
# 0x82a797 (the allocation-alignment check), _Tidy_deallocate 0x141455
STRING_DTOR_CALLS = {0x824624, 0x82A797, 0x141455}


class Image:
    def __init__(self):
        self.pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.sections = [(s.Name.rstrip(b"\0").decode(), s.VirtualAddress, s.Misc_VirtualSize, s.Characteristics)
                         for s in self.pe.sections]

    def section(self, rva):
        for name, start, size, ch in self.sections:
            if start <= rva < start + size:
                return name, ch
        return None, 0

    def is_code(self, rva):
        return bool(self.section(rva)[1] & 0x20000000)

    def is_data(self, rva):  # writable: .data / .bss
        return bool(self.section(rva)[1] & 0x80000000)

    def dword(self, rva):
        return self.pe.get_dword_at_rva(rva)

    def relocations(self):
        """{value (RVA it points at): [site RVAs]} for every HIGHLOW relocation."""
        d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
        raw = self.pe.get_data(d.VirtualAddress, d.Size)
        out, off = {}, 0
        while off + 8 <= len(raw):
            page, block = struct.unpack_from("<II", raw, off)
            if block < 8:
                break
            for i in range(8, block, 2):
                (e,) = struct.unpack_from("<H", raw, off + i)
                if e >> 12 == 3:
                    site = page + (e & 0xFFF)
                    out.setdefault((self.dword(site) - self.base) & 0xFFFFFFFF, []).append(site)
            off += block
        return out


def walk(img, rva, limit=400):
    """Linear walk of a small function: {"size", "writes", "reads", "calls": [(target, ecx, pushed, tail)],
    "end"}. Stops at a ret past every forward branch seen, or an unconditional jmp out of the function."""
    base = img.base
    regs, pushed = {}, []
    writes, reads, calls = [], [], []
    furthest = rva
    size = 0
    for n, ins in enumerate(img.md.disasm(img.pe.get_data(rva, 4096), base + rva)):
        here = ins.address - base
        m = ins.mnemonic
        if m == "int3":  # padding after a no-return call (_invalid_parameter_noinfo_noreturn): the end
            return {"size": size, "writes": writes, "reads": reads, "calls": calls, "end": "int3"}
        size = here + ins.size - rva
        ops = ins.operands

        def absmem(op):
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_INVALID and op.mem.index == X86_REG_INVALID:
                a = (op.mem.disp & 0xFFFFFFFF) - base
                return a if 0 <= a < 0x2000000 else None
            return None

        # memory operands: the first is written by stores, the rest read
        for i, op in enumerate(ops):
            a = absmem(op)
            if a is None:
                continue
            if i == 0 and m not in ("cmp", "test", "push", "call", "jmp") and len(ops) >= 1 and not m.startswith("j"):
                writes.append(a)
                if m not in ("mov", "movq", "movups", "movaps", "movss", "movsd", "movd", "movdqu", "movdqa", "movlps"):
                    reads.append(a)  # read-modify-write (and, or, add ...)
            else:
                reads.append(a)
        # immediates into registers / the stack
        if m == "mov" and len(ops) == 2 and ops[0].type == X86_OP_MEM and ins.reg_name(ops[0].mem.base) == "esp" \
                and ops[0].mem.disp == 0 and ops[0].mem.index == X86_REG_INVALID:
            # `mov dword ptr [esp], imm`: the compiler reuses the previous call's argument slot for the next one
            value = (ops[1].imm & 0xFFFFFFFF) - base if ops[1].type == X86_OP_IMM else None
            pushed = (pushed[:-1] if pushed else []) + [value]
        elif m == "mov" and len(ops) == 2 and ops[0].type == X86_OP_REG:
            if ops[1].type == X86_OP_IMM:
                regs[ops[0].reg] = (ops[1].imm & 0xFFFFFFFF) - base
            else:
                regs.pop(ops[0].reg, None)
        elif m == "push" and ops:
            if ops[0].type == X86_OP_IMM:
                pushed.append((ops[0].imm & 0xFFFFFFFF) - base)
            elif ops[0].type == X86_OP_REG:
                pushed.append(regs.get(ops[0].reg))
            else:
                pushed.append(None)
        elif m in ("call", "jmp") and ops and ops[0].type == X86_OP_IMM:
            t = (ops[0].imm & 0xFFFFFFFF) - base
            if m == "call":
                calls.append((t, regs.get(X86_REG_ECX), list(pushed), False))
                pushed = []
                for r in (X86_REG_EAX, X86_REG_ECX, X86_REG_EDX):  # caller-saved registers are gone after a call
                    regs.pop(r, None)
            elif rva <= t < rva + 4096 and t > here:
                furthest = max(furthest, t)
            elif rva <= t <= here:
                pass  # backward jump: a loop
            elif here >= furthest:
                calls.append((t, regs.get(X86_REG_ECX), list(pushed), True))
                return {"size": size, "writes": writes, "reads": reads, "calls": calls, "end": "tail"}
        elif m.startswith("j") and ops and ops[0].type == X86_OP_IMM:
            t = (ops[0].imm & 0xFFFFFFFF) - base
            if rva <= t < rva + 4096:
                furthest = max(furthest, t)
        elif ops and ops[0].type == X86_OP_REG and m not in ("push", "cmp", "test"):
            regs.pop(ops[0].reg, None)
        if m == "ret" and here >= furthest:
            return {"size": size, "writes": writes, "reads": reads, "calls": calls, "end": "ret"}
        if n >= limit:
            break
    return {"size": size, "writes": writes, "reads": reads, "calls": calls, "end": "limit"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", help="list the XC entries of this kind")
    ap.add_argument("--rva", help="print one XC entry's line")
    args = ap.parse_args()

    img = Image()
    base = img.base
    library = tsdb.library_rvas()
    names = {n["rva"]: n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
    atexit_rvas = {int(r, 16) for r in library if "atexit" in names.get(r, "")}
    status = tsdb.load_status()
    vtables = {}
    class_slots = {}  # class name -> its vtables' slot RVAs (library slots left out)
    for c in tsdb.jsonl("classes.jsonl"):
        for v in c.get("vftables", []):
            vtables[int(v["rva"], 16)] = c["name"]
            class_slots.setdefault(c["name"], set()).update(s for s in v["entries"] if s not in library)

    for r, e in status.items():  # status names are often better than Ghidra's (FUN_...)
        if e.get("name") and (not names.get(r) or names[r].startswith(("FUN_", "shared_vf_"))):
            names[r] = e["name"].split(" (")[0]

    def name(rva):
        return names.get(hex(rva), "")

    def readable(cls):
        """'.?AV?$cRZFastFixedAllocator2@$0BM@@@' -> 'cRZFastFixedAllocator2<...>', '.?AVcFoo@ns@@' -> 'ns::cFoo'."""
        if not cls.startswith(".?A"):
            return cls
        body = cls[4:]
        if body.startswith("?$"):
            head, _, rest = body[2:].partition("@")
            arg = rest[1:].split("@")[0] if rest.startswith("V") else "..."
            return f"{head}<{arg}>"
        parts = [p for p in body.split("@") if p]
        return "::".join(reversed(parts))

    def is_lib(rva):
        return hex(rva) in library

    # ---- db/static-initializers.jsonl (unchanged format) ----
    legacy = []
    for table, start, end in TABLES:
        for i, addr in enumerate(range(start, end, 4)):
            target = img.dword(addr)
            if not target:
                continue
            rva = target - base
            callees, objects, atexits = [], [], []
            ecx = None
            pushed = None
            size = 0
            for ins in img.md.disasm(img.pe.get_data(rva, 4096), base + rva):
                size = ins.address + ins.size - (base + rva)
                op = ins.op_str
                if ins.mnemonic == "mov" and op.startswith("ecx, 0x"):
                    ecx = int(op.split(", ")[1], 16) - base
                elif ins.mnemonic == "push" and op.startswith("0x"):
                    pushed = int(op, 16) - base
                elif ins.mnemonic in ("call", "jmp") and op.startswith("0x"):
                    t = int(op, 16) - base
                    callees.append(hex(t))
                    if ins.mnemonic == "call" and ecx is not None:
                        objects.append(hex(ecx))
                    if t in atexit_rvas and pushed is not None:
                        atexits.append(hex(pushed))
                    ecx = None
                if ins.mnemonic == "ret" or ins.mnemonic == "jmp":
                    break
            legacy.append({"rva": hex(rva), "size": size, "table": table, "index": i,
                           "callees": sorted(set(callees)), "objects": sorted(set(objects)),
                           "atexit": sorted(set(atexits))})
    with open(tsdb.ROOT / "db/static-initializers.jsonl", "w", encoding="utf-8", newline="\n") as fh:
        for e in legacy:
            fh.write(json.dumps(e) + "\n")

    # ---- db/initializers.jsonl: the XC entries, classified ----
    funcs = sorted((int(f["rva"], 16), f.get("size") or 1) for f in tsdb.functions())
    starts = [f[0] for f in funcs]
    import bisect

    def containing(site):
        i = bisect.bisect_right(starts, site) - 1
        if i >= 0 and funcs[i][0] <= site < funcs[i][0] + funcs[i][1]:
            return funcs[i][0]
        return None

    # class-backed subsystems only; a call-graph guess (subsystem "other", "guess") votes only when nothing class-backed
    # does, and then the entry is "other" with that guess
    submap = tsdb.jsonl("subsystems.jsonl")
    subsystem_of = {int(e["rva"], 16): e["subsystem"] for e in submap if e["subsystem"] != "other"}
    guess_of = {int(e["rva"], 16): e["guess"] for e in submap if e.get("guess")}
    relocs = img.relocations()

    entries = []
    for slot, addr in enumerate(range(XC_FIRST, TABLES[1][2], 4)):
        target = img.dword(addr)
        if not target:
            continue
        rva = target - base
        w = walk(img, rva)
        ctors, calls, thunks = [], [], []
        writes = [a for a in w["writes"] if img.is_data(a)]
        reads = [a for a in w["reads"] if a not in writes]
        for t, ecx, pushed, tail in w["calls"]:
            if t in atexit_rvas:
                if pushed and pushed[-1] is not None:
                    thunks.append(pushed[-1])
            elif ecx is not None and img.is_data(ecx):
                ctors.append(t)
                writes.append(ecx)
            else:
                calls.append(t)
        atexit = []
        for th in thunks:
            tw = walk(img, th)
            objs = sorted({a for a in tw["writes"] + tw["reads"] if img.is_data(a)}
                          | {e for _, e, _, _ in tw["calls"] if e is not None and img.is_data(e)})
            atexit.append({"thunk": hex(th), "objects": [hex(o) for o in objs],
                           "calls": sorted({hex(t) for t, _, _, _ in tw["calls"]})})
        dtor_calls = sorted({int(c, 16) for a in atexit for c in a["calls"]})
        all_calls = ctors + calls + dtor_calls

        # vtables stored by the entry or its thunks: `mov dword ptr [G], imm` where imm is a known vtable
        stored_vt = set()
        for fn in [rva] + thunks:
            for ins in img.md.disasm(img.pe.get_data(fn, min(4096, max(16, walk(img, fn)["size"]))), base + fn):
                if ins.mnemonic == "mov" and len(ins.operands) == 2 and ins.operands[1].type == X86_OP_IMM:
                    v = (ins.operands[1].imm & 0xFFFFFFFF) - base
                    if v in vtables:
                        stored_vt.add(vtables[v])

        cls = None
        class_status = None
        if stored_vt:
            cls = readable(sorted(stored_vt)[0])
            slots_ = class_slots.get(sorted(stored_vt)[0], set())
            done = sum(1 for s in slots_ if s in status and status[s]["status"] != "mapped")
            class_status = f"{done}/{len(slots_)}"
        else:
            for c in ctors + dtor_calls:
                n = name(c)
                if "::" in n and not is_lib(c):
                    cls = n.split("(")[0].rsplit("::", 1)[0]
                    break

        names_called = [name(c) for c in all_calls] + [name(rva)]
        lib_calls = [c for c in all_calls if is_lib(c)]
        game_calls = [c for c in all_calls if not is_lib(c) and c not in atexit_rvas]
        own_calls = set(calls) - {CRC24, CRC32}

        if is_lib(rva) or any(any(k in n for k in CRT_NAMES) for n in names_called):
            kind = "crt"
        elif calls and set(calls) <= {CRC24, CRC32} and not ctors and writes:
            kind = "key-hash"
        elif stored_vt:
            kind = "class-inline"
        elif any(any(k in name(c) for k in STRING_CALLS) for c in ctors + calls) or (
                thunks and any(0x141455 in [int(c, 16) for c in a["calls"]] for a in atexit) and not game_calls):
            kind = "std-string"
        elif any(any(k in name(c) for k in CONTAINER_CALLS) for c in ctors + calls + dtor_calls):
            kind = "container"
        elif ctors:
            kind = "object"
        elif not calls and thunks and not writes:
            kind = "atexit-only"
        elif not calls and writes:
            # operator new + string bytes copied in: a std::string longer than the small buffer, inlined
            kind = "value"
        elif calls and writes:
            kind = "computed"
        elif calls:
            kind = "registration"
        else:
            kind = "other"
        # A std::string whose constructor is inlined: the characters copied in (operator new first when longer than
        # the small buffer; size and capacity are constant-initialised in .data), destroyed by a thunk that only
        # frees the buffer (_Tidy_deallocate inlined: sized operator delete, _invalid_parameter_noinfo_noreturn).
        if kind in ("value", "computed") and thunks and set(calls) <= {OPERATOR_NEW_RVA} and \
                all(set(int(c, 16) for c in a["calls"]) <= STRING_DTOR_CALLS for a in atexit):
            kind = "std-string"

        # subsystem: who references what it writes (outside this entry and its thunks)
        mine = {rva} | set(thunks)
        refs = [f for g in set(writes) | {int(o, 16) for a in atexit for o in a["objects"]} for site in relocs.get(g, [])
                for f in [containing(site)] if f is not None and f not in mine]
        guess = None
        for of in (subsystem_of, guess_of):
            votes = Counter(s for s in map(of.get, refs) if s and s != "static-init")
            if not votes:
                votes = Counter(s for s in map(of.get, game_calls) if s)
            if votes:
                break
        subsystem = votes.most_common(1)[0][0] if votes else ("crt" if kind == "crt" else "unknown")
        if votes and of is guess_of:
            subsystem, guess = "other", subsystem

        st = {hex(c): status[hex(c)]["status"] for c in game_calls if hex(c) in status}
        ours = bool(game_calls) and all(hex(c) in status and status[hex(c)]["status"] != "mapped" for c in game_calls)
        entries.append({"slot": slot, "rva": hex(rva), "kind": kind,
                        "writes": [hex(a) for a in sorted(set(writes))], "reads": [hex(a) for a in sorted(set(reads))],
                        "ctors": [hex(c) for c in ctors], "calls": [hex(c) for c in calls], "atexit": atexit,
                        "class": cls, "class_slots": class_status, "subsystem": subsystem, "status": st,
                        "ours": ours})
        if guess:
            entries[-1]["guess"] = guess

    # Order: an entry that loads a global another entry stores to (directly, or as its constructor's object)
    # depends on it. "after" lists the writers' slots; a writer in a later slot means the entry reads the
    # constant-initialised value (or a latent order bug): "before" lists those.
    writers = {}
    for e in entries:
        for g in e["writes"]:
            writers.setdefault(g, []).append(e["slot"])
    for e in entries:
        dep = {s for g in e["reads"] for s in writers.get(g, []) if s != e["slot"]}
        e["after"] = sorted(s for s in dep if s < e["slot"])
        e["before"] = sorted(s for s in dep if s > e["slot"])

    with open(tsdb.ROOT / "db/initializers.jsonl", "w", encoding="utf-8", newline="\n") as fh:
        for e in entries:
            fh.write(json.dumps(e) + "\n")

    known = {f["rva"] for f in tsdb.jsonl("functions.jsonl")}
    print(f"{len(legacy)} initialisers ({sum(e['rva'] not in known for e in legacy)} not in functions.jsonl), "
          f"{sum(len(e['objects']) for e in legacy)} object constructions, {sum(len(e['atexit']) for e in legacy)} atexit registrations")
    kinds = Counter(e["kind"] for e in entries)
    print(f"XC entries: {len(entries)}; by kind:")
    for k in KINDS:
        if kinds[k]:
            print(f"  {k:13} {kinds[k]:4}  ours {sum(e['ours'] for e in entries if e['kind'] == k):3}  {KINDS[k]}")
    subs = Counter(e["subsystem"] for e in entries)
    print("by subsystem: " + ", ".join(f"{s} {n}" for s, n in subs.most_common()))
    print(f"order: {sum(bool(e['after']) for e in entries)} entries read a global an earlier entry writes, "
          f"{sum(bool(e['before']) for e in entries)} one a later entry writes "
          f"({', '.join(str(e['slot']) for e in entries if e['before'])})")
    if args.kind:
        for e in entries:
            if e["kind"] == args.kind:
                print(json.dumps(e))
    if args.rva:
        for e in entries:
            if int(e["rva"], 16) == int(args.rva, 16):
                print(json.dumps(e, indent=1))


if __name__ == "__main__":
    main()
