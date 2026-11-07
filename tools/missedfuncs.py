"""Finds functions Ghidra missed: targets of direct calls (decoded `call rel32` inside known functions)
that aren't function starts. Typically code that follows a record Ghidra sized too long (constructors
starting `push esi`, functions after one ending in a no-return call). Writes
db/missed-functions.jsonl, which tsdb.functions() merges in (and trims the record that swallowed
each one), so hookspec, candidates and xrefs see them.

    .venv/Scripts/python -I tools/missedfuncs.py [--dry-run] [--add 0xRVA "how it is known"]
    .venv/Scripts/python -I tools/missedfuncs.py [--dry-run] --add-file rvas.json "how they are known"
        (a JSON list of RVA strings, or one RVA per line; starts already known are skipped and counted)
    .venv/Scripts/python -I tools/missedfuncs.py --jump-tables [--json]
        the check: functions with a switch (`jmp [reg*4 + table]`) whose cases lie in no function (Ghidra cut the record
        at the jump), each with its full size by recursive descent, the callees past the cut and the status entries
        above mapped it touches (the record's or its tail callees'); also cases that land inside another function
    .venv/Scripts/python -I tools/missedfuncs.py [--dry-run] --switch 0xRVA[,0xRVA...] "how it is known"
        fix one: a manual "resized" entry (written in place: nothing is rescanned) whose size runs to the end of the code reached by descent through the
        jump tables (up to the next function start), and whose callees are the ones past the cut (ApplyDb applies it at
        the next `ghidra.py reload`; tsdb.functions() at once)

A target is accepted when it follows padding or a function end (int3/nop/ret before it) or starts
right after a record; its size runs to the next function start, without trailing int3 padding.

Functions only referenced by address (factory creators stored in a table, inlined into their caller's
record) aren't call targets, so the scan can't find them: `--add` records one as a "manual" entry (the
source text says how it is known) and prints the size it chose and why: the body ends at the first ret or
jmp no earlier branch of it jumps past (packed `new; jmp ctor` factories end at their null tail's ret),
at a ret/jmp followed by padding, or at the next known function start (manual_extent). Check the size
against the disassembly; manual entries are kept when the file is regenerated.
"""
import bisect
import json
import re
import struct
import sys
from pathlib import Path

import capstone
import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


EH_PROLOG = 0x8248B0  # _EH_prolog (db/library-matches.jsonl)


def manual_extent(data, base, target, known):
    """(end, callees, why) of a function starting at `target` that no record has (`--add`): decoded from its start, it
    ends at the first `ret` or unconditional `jmp` that no earlier branch of the body jumps past (a tail jump to a known
    function, the null path's `ret` of a packed `new; jmp ctor` factory), at a `ret`/`jmp` followed by int3 or zero
    padding, or at the next known function start, whichever comes first. Branch targets that are known function starts
    (tail calls) don't count as the body. data: the section's bytes from rva `base`; known: function-start RVAs.
    end None: no end found within 64 KB (why says so)."""
    end_of_data = base + len(data)
    next_start = min((k for k in known if k > target), default=end_of_data)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    callees, furthest = [], 0  # the furthest branch target inside the body seen so far
    for ins in md.disasm(data[target - base:min(target + 0x10000, end_of_data) - base], target):
        if ins.address >= next_start:
            return next_start, sorted(set(callees)), f"ends at the next known function start {hex(next_start)}"
        nxt = ins.address + ins.size
        dest = int(ins.op_str, 16) if re.fullmatch(r"0x[0-9a-f]+", ins.op_str) else None
        if ins.mnemonic == "call" and dest is not None:
            callees.append(hex(dest))
        elif ins.mnemonic.startswith("j") and dest is not None and target <= dest < next_start:
            furthest = max(furthest, dest)  # (a known start is never below next_start: tail jumps don't count)
        if ins.mnemonic in ("ret", "retn", "jmp"):
            padded = nxt < end_of_data and data[nxt - base] in (0xCC, 0x00)
            if furthest < nxt or padded:
                what = f"{ins.mnemonic} {ins.op_str}".strip()
                return nxt, sorted(set(callees)), (f"`{what}` at {hex(ins.address)} ends the body"
                                                   + (" (padding follows)" if padded else " (no branch goes past it)"))
    return None, sorted(set(callees)), f"no end found within 64 KB of {hex(target)}"


# ---- switches whose case bodies Ghidra left out (--jump-tables, --switch) ----
# Ghidra cuts some functions at a switch's `jmp dword ptr [reg*4 + table]` when it can't recover the table (no bounds
# check before the jump, e.g. a loop over cases: cDeviceStateDX::Validate 0x182220, 55 of 1826 bytes): the case bodies
# after it belong to no function, so their calls look like original code nobody owns. Recursive descent through the
# tables finds the whole body.

TABLE_SIB = {0x85, 0x8D, 0x95, 0x9D, 0xAD, 0xB5, 0xBD}  # SIB of [reg*4 + disp32] without a base register (index != esp)
TABLE_JMP = re.compile(r"dword ptr \[(\w+)\*4 ([+-]) (0x[0-9a-f]+|\d+)\]$")
MAX_CASES = 1024


def table_targets(data, base, imagebase, table_va, lo, hi, count=None):
    """Case RVAs of the jump table at table_va (a VA): consecutive dwords pointing into [lo, hi), at most `count`
    (from a bounds check) or MAX_CASES; the table also ends where it would run into code one of its entries targets."""
    out = []
    at = table_va - imagebase
    while len(out) < (count or MAX_CASES):
        off = at + 4 * len(out) - base
        if off < 0 or off + 4 > len(data) or at + 4 * len(out) in out:
            break
        t = struct.unpack_from("<I", data, off)[0] - imagebase
        if not lo <= t < hi:
            break
        out.append(t)
    return out


def descend(data, base, imagebase, start, limit):
    """Recursive descent from `start` inside [start, limit), through jump tables: (end, tables, callees). end: the end of
    the furthest instruction reached; tables: {jmp site RVA: [case RVAs]}; callees: direct call targets (RVAs, sorted).
    Stops a path at ret, int3, an unconditional jump, a jump out of the range or bytes that don't decode."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    todo, seen, end, tables, callees = [start], set(), start, {}, set()
    while todo:
        a = todo.pop()
        recent = []  # the path's last few instructions: a bounds check before a table jump
        while start <= a < limit and a not in seen:
            ins = next(md.disasm(data[a - base:min(a + 16, limit) - base], a, 1), None)
            if ins is None or ins.mnemonic == "int3":
                break
            seen.add(a)
            nxt = a + ins.size
            end = max(end, nxt)
            m, op = ins.mnemonic, ins.op_str
            dest = int(op, 16) if re.fullmatch(r"0x[0-9a-f]+", op) else None
            if m == "jmp" and (t := TABLE_JMP.fullmatch(op)):
                disp = int(t.group(3), 0) * (-1 if t.group(2) == "-" else 1) & 0xFFFFFFFF
                count = None
                for p in reversed(recent):
                    c = re.fullmatch(rf"{t.group(1)}, (0x[0-9a-f]+|\d+)", p.op_str)
                    if p.mnemonic == "cmp" and c:
                        count = int(c.group(1), 0) + 1
                        break
                cases = table_targets(data, base, imagebase, disp, start, limit, count)
                tables[a] = cases
                todo.extend(cases)
                break
            if m in ("ret", "retn") or (m == "jmp" and dest is None):
                break
            if m == "call":
                if dest is not None:
                    callees.add(dest)
            elif m.startswith(("j", "loop")) and dest is not None:
                if start <= dest < limit:
                    todo.append(dest)
                if m == "jmp":
                    break
            recent = (recent + [ins])[-4:]
            a = nxt
    return end, tables, sorted(callees)


def switch_cuts(data, base, imagebase, funcs, status=None):
    """Functions with a jump table some of whose cases lie in no function (see above). funcs: tsdb.functions()-shaped
    records; status: rva -> status entry. One dict per function: rva, name, size, full (its size by descent up to the
    next function start), cases (case RVAs in no function), elsewhere (case RVAs inside another function), tail_callees
    (callees the record lacks), status, touches ({rva: status} of it and its tail callees above mapped)."""
    status = status or {}
    starts = sorted({int(f["rva"], 16): f for f in funcs}.items())
    keys = [k for k, _ in starts]

    def owner(rva):
        i = bisect.bisect_right(keys, rva) - 1
        return keys[i] if i >= 0 and rva < keys[i] + starts[i][1]["size"] else None

    holders = set()
    for i in range(len(data) - 6):  # raw scan for FF 24 SIB disp32, the descent confirms each
        if data[i] == 0xFF and data[i + 1] == 0x24 and data[i + 2] in TABLE_SIB:
            k = owner(base + i)
            if k is not None:
                holders.add(k)
    out = []
    for k in sorted(holders):
        f = starts[keys.index(k)][1]
        nxt = keys[bisect.bisect_right(keys, k)] if k != keys[-1] else base + len(data)
        end, tables, callees = descend(data, base, imagebase, k, nxt)
        cases = sorted({c for cs in tables.values() for c in cs})
        orphans = [c for c in cases if owner(c) is None]
        elsewhere = [c for c in cases if owner(c) not in (None, k)]
        if not orphans and not elsewhere:
            continue
        tail = sorted(set(hex(c) for c in callees) - set(f.get("callees", [])))
        above = ("reimplemented", "verified", "trace-verified", "replaced")
        touches = {r: status[r]["status"] for r in [f["rva"]] + tail if r in status and status[r]["status"] in above}
        out.append({"rva": f["rva"], "name": status.get(f["rva"], {}).get("name") or f.get("name", ""),
                    "size": f["size"], "full": end - k, "cases": [hex(c) for c in orphans],
                    "elsewhere": [hex(c) for c in elsewhere], "tail_callees": tail,
                    "status": status.get(f["rva"], {}).get("status", ""), "touches": touches})
    return out


def load_text():
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    return text.get_data(), text.VirtualAddress, pe.OPTIONAL_HEADER.ImageBase


def jump_tables_report(as_json):
    data, base, imagebase = load_text()
    rows = switch_cuts(data, base, imagebase, tsdb.functions(), tsdb.load_status())
    if as_json:
        print(json.dumps(rows, indent=1))
        return
    cut = [r for r in rows if r["cases"]]
    for r in cut:
        touch = ", ".join(f"{k} {v}" for k, v in r["touches"].items())
        print(f"{r['rva']} {r['name'][:60]}: {r['size']} -> {r['full']} B, {len(r['cases'])} case(s) in no function, "
              f"{len(r['tail_callees'])} tail callee(s)" + (f"; TOUCHES {touch}" if touch else ""))
    other = [r for r in rows if r["elsewhere"]]
    for r in other:
        print(f"{r['rva']} {r['name'][:60]}: case(s) inside another function: {', '.join(r['elsewhere'][:6])}"
              + (" ..." if len(r["elsewhere"]) > 6 else ""))
    print(f"{len(cut)} function(s) with jump-table cases in no function ({sum(len(r['cases']) for r in cut)} cases, "
          f"{sum(r['full'] - r['size'] for r in cut)} bytes past their records), "
          f"{sum(1 for r in cut if r['touches'])} touching an entry above mapped; "
          f"{len(other)} with cases inside another function. Fix: missedfuncs.py --switch 0xRVA \"how it is known\"")


def switch_entry(data, base, imagebase, record, next_start, how):
    """The manual "resized" entry for a Ghidra record cut at a jump table (record: its functions.jsonl line), or None
    when descent finds nothing past it. Its callees are the ones the record lacks (tsdb.functions() merges them)."""
    k, size = int(record["rva"], 16), record["size"]
    stop, tables, callees = descend(data, base, imagebase, k, next_start)
    if stop <= k + size:
        return None
    ncases = len({c for cs in tables.values() for c in cs})
    return {"rva": hex(k), "size": stop - k, "ghidra_size": size, "kind": "resized",
            "callees": sorted(set(hex(c) for c in callees) - set(record.get("callees", []))),
            "source": f"manual: switch: {len(tables)} jump table(s), {ncases} cases, body by descent to {hex(stop)}; "
                      + re.sub(r"^\s*manual:\s*", "", how)}


def add_switches(rvas, how, dry_run):
    """--switch: a manual resized entry per RVA, written into db/missed-functions.jsonl in place (an entry at the same
    RVA is replaced; nothing else is rescanned or rewritten)."""
    data, base, imagebase = load_text()
    records = {int(f["rva"], 16): f for f in tsdb.jsonl("functions.jsonl")}
    known = sorted({int(f["rva"], 16) for f in tsdb.functions()})
    path = tsdb.ROOT / "db/missed-functions.jsonl"
    lines = path.read_text(encoding="utf-8").splitlines()
    for k in rvas:
        if k not in records:
            sys.exit(f"missedfuncs: {hex(k)} is not the start of a Ghidra record")
        nxt = known[bisect.bisect_right(known, k)] if k < known[-1] else base + len(data)
        e = switch_entry(data, base, imagebase, records[k], nxt, how)
        if e is None:
            print(f"{hex(k)}: descent finds nothing past its record ({records[k]['size']} bytes)")
            continue
        lines = [l for l in lines if json.loads(l)["rva"] != e["rva"]] + [json.dumps(e)]
        print(f"resized {e['rva']}: {e['ghidra_size']} -> {e['size']} bytes (to {hex(k + e['size'])}, next start "
              f"{hex(nxt)}), {len(e['callees'])} callee(s) past the cut")
    if not dry_run:
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    if any(a in ("-h", "--help") for a in sys.argv[1:]):
        print(__doc__)
        return
    args = sys.argv[1:]
    dry_run = "--dry-run" in args
    args = [a for a in args if a != "--dry-run"]
    if args[:1] == ["--jump-tables"]:
        return jump_tables_report("--json" in args[1:])
    if args[:1] == ["--switch"] and len(args) == 3:
        return add_switches([int(r, 16) for r in args[1].split(",")], args[2], dry_run)
    add = None
    if args[:1] == ["--add"] and len(args) == 3:
        add = [(int(args[1], 16), args[2])]
        args = []
    elif args[:1] == ["--add-file"] and len(args) == 3:
        text = open(args[1], encoding="utf-8").read()
        rvas = json.loads(text) if text.lstrip().startswith("[") else text.split()
        add = [(int(r, 16), args[2]) for r in rvas]
        args = []
    if args:
        sys.exit(f"missedfuncs: unknown argument(s) {' '.join(args)}\n\n{__doc__}")
    path = tsdb.ROOT / "db/missed-functions.jsonl"
    manual = [e for e in (tsdb.jsonl("missed-functions.jsonl") if path.exists() else [])
              if str(e.get("source", "")).startswith("manual")]
    funcs = tsdb.jsonl("functions.jsonl")  # Ghidra's records only, not earlier merges
    starts = {int(f["rva"], 16): f for f in funcs}
    keys = sorted(starts)
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    base, data = text.VirtualAddress, text.get_data()
    end = base + len(data)

    def byte(rva):
        return data[rva - base]

    def container(rva):
        i = bisect.bisect_right(keys, rva) - 1
        if i >= 0 and rva < keys[i] + starts[keys[i]]["size"]:
            return keys[i]
        return None

    # Raw scan for E8 rel32, then confirm each site decodes as a call within its function.
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    candidates = {}
    for i in range(len(data) - 5):
        if data[i] != 0xE8:
            continue
        site = base + i
        target = site + 5 + struct.unpack_from("<i", data, i + 1)[0]
        if not (base <= target < end) or target in starts:
            continue
        candidates.setdefault(target, []).append(site)

    def region(site):
        """Start of the code containing `site`: its record, or the end of the record before the gap."""
        i = bisect.bisect_right(keys, site) - 1
        if i < 0:
            return None
        k = keys[i]
        return k if site < k + starts[k]["size"] else k + starts[k]["size"]

    decoded = {}

    def boundaries(start, upto):
        got = decoded.get(start)
        if got is None or got[0] < upto:
            stop = min(upto + 16, end)
            got = (stop, {ins.address for ins in md.disasm(data[start - base:stop - base], start)})
            decoded[start] = got
        return got[1]

    confirmed = {}
    for target, sites in candidates.items():
        for site in sites:
            start = region(site)
            if start is not None and site - start < 0x10000 and site in boundaries(start, site):
                confirmed.setdefault(target, []).append(site)

    accepted = []
    for target in sorted(confirmed):
        prev = byte(target - 1)
        if prev in (0xCC, 0x90, 0xC3) or byte(target - 3) == 0xC2 or region(target) == target:
            accepted.append((target, None))

    # Records that fall through into a gap (Ghidra sized `_EH_prolog` functions at their 10-byte
    # prologue, and a few it cut short elsewhere): extend them to the next start, without trailing int3 padding.
    # Manual entries are starts too: a scanned record stops at one (rerun after an --add to resize it).
    new_starts = sorted(set(keys) | {t for t, _ in accepted} | {int(e["rva"], 16) for e in manual})
    resized = []
    for k in keys:
        size = starts[k]["size"]
        last = None
        for ins in md.disasm(data[k - base:k - base + size], k):
            last = ins
        if last is None or last.address + last.size != k + size:
            continue
        if last.mnemonic in ("ret", "retn", "jmp", "int3") or byte(k + size) == 0xCC:
            continue
        # A record ending in a call falls through only after _EH_prolog; other calls there are
        # no-return (throws, _invalid_parameter_noinfo_noreturn) and the gap is other code.
        if last.mnemonic == "call" and last.op_str != hex(EH_PROLOG):
            continue
        nxt = new_starts[bisect.bisect_right(new_starts, k + size - 1)] if k + size <= new_starts[-1] else end
        stop = nxt
        while stop > k + size and byte(stop - 1) == 0xCC:
            stop -= 1
        if stop > k + size:
            callees = {hex(int(i.op_str, 16)) for i in md.disasm(data[k + size - base:stop - base], k + size)
                       if i.mnemonic == "call" and i.op_str.startswith("0x")}
            resized.append({"rva": hex(k), "size": stop - k, "ghidra_size": size, "kind": "resized",
                            "callees": sorted(callees)})

    out = []
    for target, holder in accepted:
        nxt = new_starts[bisect.bisect_right(new_starts, target)] if target != new_starts[-1] else end
        stop = nxt
        while stop > target and byte(stop - 1) == 0xCC:
            stop -= 1
        callees = []
        for ins in md.disasm(data[target - base:stop - base], target):
            if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                callees.append(hex(int(ins.op_str, 16)))
        out.append({"rva": hex(target), "size": stop - target, "kind": "missed",
                    "callers": [hex(s) for s in confirmed[target]], "callees": sorted(set(callees))})

    if add:
        scanned = {int(e["rva"], 16) for e in out}
        fresh = [(t, src) for t, src in add if t not in starts and t not in scanned]
        if len(add) == 1 and not fresh:
            sys.exit(f"missedfuncs: {hex(add[0][0])} is already a function start")
        if len(fresh) < len(add):
            print(f"skipped {len(add) - len(fresh)} already known function start(s)")
        # every new start bounds the others (a batch entry ends at the next one at the latest)
        known = set(keys) | scanned | {int(e["rva"], 16) for e in manual} | {t for t, _ in fresh}
        added = 0
        for target, source in fresh:
            stop, callees, why = manual_extent(data, base, target, known - {target})
            if stop is None:
                print(f"missedfuncs: not added: {why}")
                continue
            manual = [e for e in manual if int(e["rva"], 16) != target]
            manual.append({"rva": hex(target), "size": stop - target, "kind": "missed", "callers": [],
                           "callees": callees, "source": "manual: " + re.sub(r"^\s*manual:\s*", "", source)})
            added += 1
            if len(add) == 1:
                print(f"added {hex(target)}: size {stop - target} bytes ({hex(target)}..{hex(stop)}): {why}; callees {callees}")
        if len(add) > 1:
            print(f"added {added} manual entries")
    print(f"{len(confirmed)} call targets aren't function starts; {len(out)} accepted; "
          f"{len(resized)} records extended over the gap they fall through into; {len(manual)} manual entries kept")
    if not dry_run:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            mine = {e["rva"] for e in manual}  # a manual entry wins over what the scan found there
            for e in [e for e in out + resized if e["rva"] not in mine] + sorted(manual, key=lambda e: int(e["rva"], 16)):
                f.write(json.dumps(e) + "\n")


if __name__ == "__main__":
    main()
