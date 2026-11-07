#!/usr/bin/env python3
"""Read and compare SimAntics primitive traces (simtrace.txt, written by the host's "simtrace" control
command, host/control_simtrace.cpp; scenario tools/scenarios/lot-simtrace.txt).

Usage:
  python -I tools/simtrace.py summary TRACE
  python -I tools/simtrace.py diff A B [--context 5]
  python -I tools/simtrace.py verify --ref R1 R2 [R3...] --repl P1 [P2...]

A trace line is one primitive run (or, marked "call", one subroutine call: the called BHAV and the
calling node): tick, object (pointer and vtable RVA), opcode, result, BHAV
(stack frame +8: low word the BHAV id, high word not yet known), node index, true/false targets and
16 operand bytes. Objects are keyed by the ids in their stack frame (the "ids" field) or, in traces
without it, numbered by first appearance (pointers differ between runs); ticks
are dropped (runs started by wall-clock clicks don't line up in ticks).

diff compares the two runs' primitive streams in order (the simulator runs objects one after another
on the main thread, so the order is part of the behaviour) and reports the first difference with
context, then the same per object, matched by first-appearance number, so later objects can be
compared past an earlier difference.

verify is the trace check behind the `trace-verified` status (CLAUDE.md): the objects that differ
between any two reference runs are the noise; each replacement run is compared with the reference
run it matches best, and passes when every object that differs from it is noise. Call lines are
compared without operands when only one side recorded them (older traces).
"""
import argparse
import collections
import sys
from pathlib import Path


def parse(path):
    """Yields (tick, object pointer, object vtable, record) per primitive; record holds the rest.
    The operand pointer at node +0xc points at the node's parameter object, so the first operand
    dword is that object's vtable: it is rewritten as an RVA (the trace's "image" line)."""
    base = 0
    with open(path, encoding="ascii", errors="replace") as f:
        for line in f:
            if line.startswith("image "):
                base = int(line.split()[1], 16)
            if not line[:1].isdigit():
                continue
            p = line.split()
            # 0 tick, 1 obj, 2 ptr, 3 vt, 4 vtable, 5 op, 6 opcode, 7 ->, 8 result, then bhav ... ops;
            # subroutine calls: 5 call, 6 called BHAV, 7 from, then the caller's bhav, node, t/f.
            if len(p) < 9 or p[1] != "obj" or p[5] not in ("op", "call"):
                continue
            rest = p[9:] if p[5] == "op" else p[7:]
            head = (p[6], p[8]) if p[5] == "op" else ("call", p[6])
            # BHAV id and node index are 16-bit: the high words differ between identical runs.
            for key in ("bhav", "node"):
                if key in rest:
                    i = rest.index(key) + 1
                    rest[i] = f"{int(rest[i], 16 if key == 'bhav' else 10) & 0xffff:x}"
            if "ops" in rest:
                i = rest.index("ops") + 1
                if len(rest) >= i + 2:
                    raw = bytes.fromhex(rest[i] + rest[i + 1])
                    rest[i:i + 2] = [f"pv:{int.from_bytes(raw, 'little') - base:x}"]
            obj = p[2]
            if "ids" in rest:  # the object ids at stack frame +0x18: stable between runs
                i = rest.index("ids")
                # Two 16-bit ids; the low word is the object itself (the high word is the stack
                # object, which changes with what the object is doing).
                obj = "id:" + rest[i + 1][-4:]
                del rest[i:i + 2]
            rest = tuple(rest)
            yield int(p[0]), obj, p[4], head + rest


def normalised(path):
    ids = {}
    out = []
    for tick, obj, vt, rec in parse(path):
        n = obj[3:] if obj.startswith("id:") else ids.setdefault(obj, len(ids))
        out.append((n, vt, rec, tick))
    return out, len({e[0] for e in out})


def fmt(entry):
    n, vt, rec, tick = entry
    op, result = rec[0], rec[1]
    extra = " ".join(rec[2:])
    return f"obj#{n} (vt {vt}) op {op} -> {result} {extra}  [tick {tick}]"


def object_differences(ea, eb):
    """The objects whose own streams differ (over their common length) or appear in one run only."""
    return set(first_differences(ea, eb))


def first_differences(ea, eb):
    """{object: tick of its first difference} for the objects object_differences reports. An object
    in one run only counts from its first primitive there."""
    per_a, per_b = collections.defaultdict(list), collections.defaultdict(list)
    for e in ea:
        per_a[e[0]].append(e[1:4])
    for e in eb:
        per_b[e[0]].append(e[1:4])
    out = {}
    for obj in set(per_a) ^ set(per_b):
        out[obj] = (per_a.get(obj) or per_b[obj])[0][2]
    for obj in set(per_a) & set(per_b):
        sa, sb = per_a[obj], per_b[obj]
        k = min(len(sa), len(sb))
        j = next((i for i in range(k) if sa[i][:2] != sb[i][:2]), None)
        if j is not None:
            out[obj] = min(sa[j][2], sb[j][2])
    return out


def _strip_call_operands(entries):
    out = []
    for n, vt, rec, tick in entries:
        if rec[0] == "call" and "ops" in rec:
            rec = rec[:rec.index("ops")]
        out.append((n, vt, rec, tick))
    return out


def _strip_values(entries):
    return [(n, vt, rec[:rec.index("vals")] if "vals" in rec else rec, tick) for n, vt, rec, tick in entries]


def _has_values(entries):
    return any("vals" in rec for _, _, rec, _ in entries)


def comparable(traces):
    """Drops what only some of the traces recorded (call operands, operand values), so older traces
    compare with newer ones on what both have."""
    if len({_has_call_operands(t) for t in traces.values()}) > 1:
        traces = {p: _strip_call_operands(t) for p, t in traces.items()}
    if len({_has_values(t) for t in traces.values()}) > 1:
        traces = {p: _strip_values(t) for p, t in traces.items()}
    return traces


def _has_call_operands(entries):
    return any(rec[0] == "call" and "ops" in rec for _, _, rec, _ in entries)


def regime(path):
    """The global values (data owner 6) the trace reads on its first tick, before any primitive could have written
    them: the state the engine started the lot in. Runs whose clocks started a tick apart read different values
    (lot 108: global 3 is 131 or 133) and then go different ways for reasons that have nothing to do with the code
    under test."""
    first, seen = None, {}
    for tick, _, _, rec in parse(path):
        if first is None:
            first = tick
        if tick != first:
            break
        for item in rec:
            if item.startswith("6:") and "=" in item:
                seen.setdefault(item.split("=")[0], item.split("=")[1])
    return tuple(sorted(seen.items()))


def verify(refs, repls, regimes=False):
    """With `regimes`, each replacement run is checked against the references that started in its regime (same
    first-tick globals) when at least two did: their noise is smaller, so the check goes further."""
    if regimes:
        keys = {p: regime(p) for p in refs + repls}
        groups = collections.defaultdict(list)
        for r in refs:
            groups[keys[r]].append(r)
        status, passed = 0, 0
        for key, members in groups.items():
            print(f"regime {dict(key)}: {len(members)} references")
        for key, members in groups.items():
            mine = [p for p in repls if keys[p] == key]
            if not mine:
                continue
            if len(members) < 2:
                print(f"regime {dict(key)}: {len(mine)} runs but only {len(members)} reference; checked against all references")
                members = refs
            print(f"== regime {dict(key)}")
            st, ps = verify_group(members, mine)
            status |= st
            passed += ps
        unmatched = [p for p in repls if keys[p] not in groups]
        if unmatched:
            print(f"== {len(unmatched)} runs in no reference regime; checked against all references")
            st, ps = verify_group(refs, unmatched)
            status |= st
            passed += ps
    else:
        status, passed = verify_group(refs, repls)
    if status == 0 and passed < 2:
        print(f"only {passed} passing runs; need 2")
        status = 1
    print("PASS" if status == 0 else "FAIL")
    return status


def verify_group(refs, repls):
    traces = comparable({p: normalised(p)[0] for p in refs + repls})
    noise = set()
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            d = object_differences(traces[a], traces[b])
            print(f"ref {a} vs {b}: {len(d)} objects differ")
            noise |= d
    print(f"noise: {len(noise)} objects ({', '.join(sorted(map(str, noise))[:20])})")
    # The noise objects share the game's random generator with everything else, so once a run's noise
    # objects go another way than its reference's, later draws (and whatever depends on them) can differ
    # anywhere. With the closest reference, a difference beyond the noise fails the run if it starts
    # before the run's first noise difference; later ones are reported as not attributable.
    status, passed = 0, 0
    for p in repls:
        best = None
        for r in refs:
            d = first_differences(traces[r], traces[p])
            fork = min((t for o, t in d.items() if o in noise), default=None)
            early = {o for o, t in d.items() if o not in noise and (fork is None or t < fork)}
            late = {o for o, t in d.items() if o not in noise} - early
            key = (len(early), -(fork if fork is not None else 1 << 62), len(late))
            if best is None or key < best[0]:
                best = (key, r, early, late, fork)
        _, r, early, late, fork = best
        checked = "the whole run" if fork is None else f"up to tick {fork}"
        print(f"repl {p}: closest {r}, checked {checked}; {len(early)} objects differ beyond the noise"
              + (f": {', '.join(sorted(map(str, early))[:20])}" if early else "")
              + (f"; {len(late)} more after the noise fork ({', '.join(sorted(map(str, late))[:20])})" if late else ""))
        if early:
            status = 1
        else:
            passed += 1
    return status, passed


def summary(path):
    entries, nobj = normalised(path)
    if not entries:
        print("no primitives")
        return 1
    ops = collections.Counter(e[2][0] for e in entries if e[2][0] != "call")
    results = collections.Counter(e[2][1] for e in entries)
    vts = collections.Counter()
    seen = set()
    for n, vt, _, _ in entries:
        if n not in seen:
            seen.add(n)
            vts[vt] += 1
    ticks = (entries[0][3], entries[-1][3])
    print(f"{len(entries)} primitives, {nobj} objects, ticks {ticks[0]}..{ticks[1]}")
    print("objects by vtable: " + ", ".join(f"{vt}: {c}" for vt, c in vts.most_common()))
    print("results: " + ", ".join(f"{r}: {c}" for r, c in sorted(results.items())))
    print("opcodes: " + ", ".join(f"{o}: {c}" for o, c in ops.most_common()))
    return 0


def diff(a, b, context):
    ea, na = normalised(a)
    eb, nb = normalised(b)
    both = comparable({0: ea, 1: eb})
    ea, eb = both[0], both[1]
    print(f"A: {len(ea)} primitives, {na} objects; B: {len(eb)} primitives, {nb} objects")
    n = min(len(ea), len(eb))
    first = next((i for i in range(n) if ea[i][:3] != eb[i][:3]), None)
    if first is None:
        print(f"the first {n} primitives match in order" + ("" if len(ea) == len(eb) else " (one trace is longer)"))
        status = 0
    else:
        print(f"first difference in order at primitive {first}:")
        for i in range(max(0, first - context), first):
            print("    " + fmt(ea[i]))
        print("  A " + fmt(ea[first]))
        print("  B " + fmt(eb[first]))
        status = 1
    # Per object: each object's own stream, up to the shorter one's length.
    per_a, per_b = collections.defaultdict(list), collections.defaultdict(list)
    for e in ea:
        per_a[e[0]].append(e)
    for e in eb:
        per_b[e[0]].append(e)
    same, diffs = 0, []
    for obj in sorted(set(per_a) & set(per_b), key=str):
        sa, sb = per_a[obj], per_b[obj]
        k = min(len(sa), len(sb))
        j = next((i for i in range(k) if sa[i][1:3] != sb[i][1:3]), None)
        if j is None:
            same += 1
        else:
            diffs.append((min(sa[j][3], sb[j][3]), obj, j, k, sa, sb))
    # Earliest first: later differences are often consequences of earlier ones.
    for _, obj, j, k, sa, sb in sorted(diffs, key=lambda d: (d[0], str(d[1])))[:10]:
        print(f"obj#{obj}: differs at its primitive {j} of {k}:")
        for i in range(max(0, j - context), j):
            print("    " + fmt(sa[i]))
        print("  A " + fmt(sa[j]))
        print("  B " + fmt(sb[j]))
    only = (set(per_a) ^ set(per_b))
    print(f"per object: {same} match over their common length, {len(diffs)} differ, {len(only)} in one trace only")
    differ = len(diffs)
    return 1 if differ else status


def cases(traces, opcode, at, expect):
    """Which selector values a primitive ran with: the 16-bit word at byte `at` of its operand bytes (for
    GenericSimCall, opcode 0x01, the case number is the parameter word at +4). Counts per scenario (the
    trace name up to its last '-'), and with `expect` (lo-hi) the values no trace exercised."""
    counts, groups = {}, []
    for path in traces:
        group = Path(path).stem.rsplit("-", 1)[0]
        if group not in groups:
            groups.append(group)
        with open(path, encoding="ascii", errors="replace") as f:
            for line in f:
                p = line.split()
                if len(p) < 9 or p[5] != "op" or int(p[6], 16) != opcode or "ops" not in p:
                    continue
                i = p.index("ops") + 1
                raw = bytes.fromhex("".join(p[i:i + 8]))
                if len(raw) < at + 2:
                    continue
                value = int.from_bytes(raw[at:at + 2], "little")
                counts.setdefault(value, {}).setdefault(group, 0)
                counts[value][group] += 1
    print("case  " + "  ".join(f"{g:>12}" for g in groups))
    for value in sorted(counts):
        print(f"{value:#5x} " + "  ".join(f"{counts[value].get(g, 0):>12}" for g in groups))
    if expect:
        lo, hi = (int(x, 0) for x in expect.split("-"))
        missing = [v for v in range(lo, hi + 1) if v not in counts]
        print(f"{len(counts)} values exercised; never exercised in {lo:#x}..{hi:#x}: " +
              (" ".join(f"{v:#x}" for v in missing) or "none"))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("summary")
    s.add_argument("trace")
    d = sub.add_parser("diff")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--context", type=int, default=5)
    v = sub.add_parser("verify")
    v.add_argument("--ref", nargs="+", required=True)
    v.add_argument("--repl", nargs="+", required=True)
    v.add_argument("--regimes", action="store_true",
                   help="check each run against the references that started from the same first-tick globals")
    c = sub.add_parser("cases", help="selector values a primitive ran with (e.g. GenericSimCall's cases)")
    c.add_argument("traces", nargs="+")
    c.add_argument("--op", default="0x01", help="opcode (default 0x01, GenericSimCall)")
    c.add_argument("--at", type=int, default=4, help="byte offset of the 16-bit selector in the operands (default 4)")
    c.add_argument("--expect", help="lo-hi: list the values in this range that no trace exercised")
    args = ap.parse_args()
    if args.cmd == "cases":
        return cases(args.traces, int(args.op, 0), args.at, args.expect)
    if args.cmd == "summary":
        return summary(args.trace)
    if args.cmd == "verify":
        if len(args.ref) < 2:
            ap.error("verify needs at least two reference traces (the noise is what they disagree on)")
        return verify(args.ref, args.repl, args.regimes)
    return diff(args.a, args.b, args.context)


if __name__ == "__main__":
    sys.exit(main())
