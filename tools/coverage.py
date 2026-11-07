"""First-call coverage of the original code in a game run (src/hook/coverage.h): writes the site list the host arms,
and reads what a run recorded, joined with db/ (status, library, subsystem, class, milestone H's direct-call set).
The mechanism and how to run it: docs/coverage.md; the metric it feeds: docs/roadmap.md (Metrics 1, Campaign order).

    tools/py -I tools/coverage.py sites [--out local/coverage/sites.txt] [--all]
    tools/py -I tools/coverage.py report RUN.txt [--round N] [--top 25] [--json] [--todo]
    tools/py -I tools/coverage.py diff A.txt B.txt [--round N]
    tools/py -I tools/coverage.py headline RUN... [--json]
    tools/py -I tools/coverage.py queue RUN... [--subsystem a,b [--whole]] [--guesses] [--keep-library] [--top 40]
                                          [--group G] [--functions] [--json]
    tools/py -I tools/coverage.py deps RUN... [--by subsystem|group] [--guesses] [--pair A,B] [--min-bytes 4000] [--json]

sites   one line per function start: "<rva> <first byte>" (hex), from tsdb.functions() (Ghidra's functions, the static
        initialisers and the missed functions). By default only starts something references: a direct call or tail jump
        (db/functions.jsonl callees, db/tail-calls.jsonl), or an absolute reference (a relocated dword equal to the
        start: vtables, function-pointer tables, pushes of callbacks), plus the entry point and the starts db/status
        names that Ghidra's list lacks (their size reads 0 in reports). A start nothing references
        is either dead or misidentified (possibly mid-instruction, where an int3 would corrupt code), so it is left out
        unless --all. Starts whose byte is int3 (padding), and switch tables Ghidra made into functions (an indexed
        table operand names them: data, where an int3 changes a switch target), are always left out.
report  per round (R lines; round 0 is from the entry point on, later rounds start at each host "coverage <label>"
        command): functions and bytes that ran, split by status (verified, replaced, reimplemented, mapped, none),
        library and third-party code; how much of it is outside milestone H's direct-call set; by subsystem and by
        class, the not-done bytes, largest first; by owner (below); and the original functions our code (the host)
        still calls.
        --todo lists every not-done function of the round instead (size, rva, subsystem, class or "~owner", name,
        reached how). --json prints one object per function (all rounds) instead ("class" and "owner" fields).

Class and owner. "class" is what db/ says (tools/status.py best_names: the function's name, else the class whose vtable
slot it is or whose vftable it stores into `this`, tools/subsystems.py "ctor:"); empty when nothing says. A function
merely using an object of a class (a cRZString local) has no class: until 2026-10-19 the ctor rule counted any vftable
store and filed ~3,900 such functions under cRZString. "owner" fills the gaps for packaging work: the class, else
"~Class" of the nearest callers that have one (callers from db/functions.jsonl plus the functions the run's own return
addresses lie in, up to 4 calls up; "~A+N" when they disagree). An inferred owner is a lead, not a fact: a helper
called from one class's method is usually that class's (or a file-local helper of its .cpp), but check before naming.
diff    functions that ran in B's round and not in A's, and the other way round (e.g. a reference run against a
        replacement run, or two scenarios).

The session modes read one or more runs (.txt, or `report --json` output; a .json takes its round labels and O sites from
the .txt beside it) as one session: every function that ran in any round of any run counts once (rounds re-arm the
sites, so per-round sums count a function once per round it ran in); a site ours in every run is "not measured". Given
no runs they read the reference set (DEFAULT_RUNS in the main checkout's local/coverage/, docs/coverage.md), and a run
named that is not found here is looked up in the main checkout and its local/coverage/. "EA code" is what is neither library nor
third-party; "done" is verified, replaced or library (as everywhere); "not done" is the rest.
headline  the progress metric (docs/roadmap.md, Metrics): bytes of EA code that ran and is not done, with what ran, what
        of it is done, by status and by subsystem. Our hooked or trapped sites (O lines) are not measured in that run;
        their count and bytes are printed apart (a -Reference run measures them).
queue   the not-done code that ran, grouped by class, else ~owner, largest first: bytes, functions, how many bytes have
        only an inferred owner, the class's size in db/ and how much of it is done, its subsystems and the rounds it
        ran in. Library instances are left out (counted on one line): functions db/ names std:: (own name or class),
        helpers whose inferred owner is a std:: class, and classless functions BSim matched to the STL probe with high
        confidence (local/bsim/candidates.jsonl); --keep-library keeps them. --subsystem keeps the functions filed
        under those subsystems: class-backed only, since db/subsystems.jsonl files call-graph guesses under "other"
        (--guesses reads each guess as its subsystem). With --whole, whole groups whose campaign is one of them (a
        campaign's scope, helpers filed elsewhere or under other included); a group's campaign is its class's
        class-backed subsystem, else the largest non-other subsystem of its functions. --functions lists each group's
        functions.
deps    call edges between not-done executed functions in different subsystems ("other" is one; --guesses reads
        guesses instead) or, --by group, campaigns (each queue group under its campaign, library instances apart), weighted by the bytes of the distinct callees; per subsystem the bytes it
        calls out and is called in; a bottom-up order (callees first) that keeps the calls into later subsystems
        small, its cycles (strongly connected at --min-bytes) with their lightest edges, and the hard test boundaries
        (Direct3D, file, threads, timers, Win32 UI, registry imports) each subsystem's not-done code calls directly.
        --pair A,B lists B's callees that A calls, by group. Edges are db/functions.jsonl calls and tail jumps plus the
        run's return addresses; COM calls (Direct3D devices, DirectSound) are not imports, so a device call is seen
        only through the HRESULT check after it (LogD3DError 0x18def0).

Reached how (from the return address the host recorded at the function's entry): "direct" (the instruction before it
is a call rel32 to this function or to a jump stub that leads to it), "tail" (a call rel32 to another function: this
one was reached by a jump from there), "indirect" (call through a register or memory: a vtable or a callback), "ours"
(the return address is outside the image: our code or a system DLL calling it), "?" otherwise.
"""
import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


def load_image():
    import pefile
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
    return pe


def relocated_values(pe):
    """RVAs that some relocated dword in the image points at (absolute references)."""
    base = pe.OPTIONAL_HEADER.ImageBase
    out = set()
    for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
        for e in block.entries:
            if e.type == 3:  # HIGHLOW
                out.add(pe.get_dword_at_rva(e.rva) - base)
    return out


def table_operands(pe):
    """RVAs that a relocated dword names as the base of an indexed table: the disp32 of [index*scale + disp] (a SIB
    with no base: jmp [eax*4 + table], mov r, [ecx*4 + table]) or of movzx r, byte [index + disp] (a switch's byte
    table). Switch tables live in the code section and Ghidra makes some into functions (0xb350, 0x182d04, 0x445bec,
    0x4573fc); they are read as data, so an int3 there changes a switch target (lot 10's load crashed, 2026-11-03)."""
    base = pe.OPTIONAL_HEADER.ImageBase
    out = set()
    for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
        for e in block.entries:
            if e.type != 3 or e.rva < 3:
                continue
            b1, b2, b3 = pe.get_data(e.rva - 3, 3)
            # jmp/call [..] (ff) or mov r, [..] (8b), modrm mod 00 rm 100, SIB base 101: no base register
            sib_table = b1 in (0xFF, 0x8B) and (b2 & 0xC7) == 0x04 and (b3 & 7) == 5
            byte_table = b1 == 0x0F and b2 == 0xB6 and (b3 & 0xC0) == 0x80 and (b3 & 7) != 4
            if sib_table or byte_table:
                out.add(pe.get_dword_at_rva(e.rva) - base)
    return out


def cmd_sites(args):
    pe = load_image()
    funcs = tsdb.functions()
    starts = {int(f["rva"], 16) for f in funcs}
    referenced = {pe.OPTIONAL_HEADER.AddressOfEntryPoint}
    for f in funcs:
        referenced.update(int(c, 16) for c in f["callees"] if c.startswith("0x"))
    for e in tsdb.jsonl("tail-calls.jsonl"):
        referenced.update(int(t, 16) for t in e["targets"])
    referenced |= relocated_values(pe) & starts
    # Function starts established by hand in db/status that Ghidra's list lacks (status.py --check notes them, e.g.
    # GZFrameWork_Shutdown 0x14472b): known starts, listed whether or not something references them.
    known = {int(r, 16) for r in tsdb.load_status()} - starts
    starts |= known
    referenced |= known
    tables = table_operands(pe) & starts
    chosen = sorted((starts if args.all else starts & referenced) - tables)
    lines, padding = [], 0
    for rva in chosen:
        byte = pe.get_data(rva, 1)[0]
        if byte == 0xCC:
            padding += 1
            continue
        lines.append(f"{rva:x} {byte:02x}")
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write(f"# tools/coverage.py sites{' --all' if args.all else ''}: {len(lines)} function starts (rva, first byte)\n")
        f.write("\n".join(lines) + "\n")
    left = len(starts) - len(chosen)
    print(f"{out}: {len(lines)} sites ({left} unreferenced starts left out{'' if not args.all else ' (none: --all)'},"
          f" {padding} int3 starts and {len(tables)} switch tables left out)")


def parse_run(path):
    """{'rounds': [(n, ms, label)], 'hits': {round: [(rva, from, ms, thread)]}, 'ours': set, 'mismatched': {rva: ..},
    'header': [comment lines]}. A .json file (`report --json` output) gives the hits only; its rounds' labels come from
    the .txt of the same name when there is one, and its 'ours' set stays empty unless that .txt has it."""
    if str(path).endswith(".json"):
        return parse_json_run(path)
    run = {"rounds": [], "hits": defaultdict(list), "ours": set(), "mismatched": {}, "header": []}
    current = 0
    with open(path, encoding="ascii", errors="replace") as f:
        lines = f.read().splitlines()
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        kind = parts[0]
        if kind == "#":
            run["header"].append(line.rstrip())
        elif kind == "O":
            run["ours"].add(hex(int(parts[1], 16)))
        elif kind == "M":
            run["mismatched"][hex(int(parts[1], 16))] = (parts[2], parts[3])
        elif kind == "R":
            current = int(parts[1])
            run["rounds"].append((current, int(parts[2]), " ".join(parts[3:])))
        elif kind == "F" and len(parts) >= 5:
            run["hits"][current].append((hex(int(parts[1], 16)), parts[2], int(parts[3]), int(parts[4])))
    return run


def parse_json_run(path):
    """The same shape from `report --json` lines (one object per first call). Status and class in the file are those of
    the day the report ran; queue and headline join db/ afresh, so only round, rva, from, ms and thread are read."""
    path = Path(path)
    txt = path.with_suffix(".txt")
    if txt.exists():
        run = parse_run(txt)
        run["hits"] = defaultdict(list)
    else:
        run = {"rounds": [], "hits": defaultdict(list), "ours": set(), "mismatched": {}, "header": []}
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    for line in lines:
        line = line.strip()
        if not line.startswith("{"):
            continue
        e = json.loads(line)
        run["hits"][e["round"]].append((hex(int(e["rva"], 16)), e["from"], e.get("ms", 0), e.get("thread", 0)))
    if not run["rounds"]:
        run["rounds"] = [(n, 0, "") for n in sorted(run["hits"])]
    return run


def load_runs(paths):
    """Several runs as one session: {'executed': {rva: {'rounds': set of round ids, 'from': first return address}},
    'legend': [(round id, label)], 'ours': the hooked/trapped sites no run measured (ours in every run: a reference
    run measures them), 'files': n}. A round id is the round's
    number for one file, "<file number>:<round>" (from 1) for several."""
    executed, legend, ours = {}, [], None
    for i, path in enumerate(paths, 1):
        run = parse_run(path)
        ours = set(run["ours"]) if ours is None else ours & run["ours"]
        labels = {n: label for n, ms, label in run["rounds"]}
        for n in sorted(run["hits"]):
            rid = str(n) if len(paths) == 1 else f"{i}:{n}"
            legend.append((rid, labels.get(n, "")))
            for rva, frm, ms, thread in run["hits"][n]:
                e = executed.setdefault(rva, {"rounds": set(), "from": frm, "froms": []})
                e["rounds"].add(rid)
                e["froms"].append(frm)
    return {"executed": executed, "legend": legend, "ours": ours or set(), "files": len(paths)}


# The reference set (docs/coverage.md, "Reference runs"): the session modes read it when no runs are given. run-1023 is
# the phases run (start, menu, neighbourhood, lot load, a lot's live play); the lot*-ref runs (2026-11-03) are the same
# phases each ending on a different occupied lot's live play. All are -Reference runs (every site armed, no ours).
DEFAULT_RUNS = ("run-1023.txt", "lot6-ref.txt", "lot10-ref.txt", "lot50-ref.txt", "lot50s-ref.txt", "lot106-ref.txt",
                "lot127-ref.txt")


def coverage_dir():
    return tsdb.main_root() / "local/coverage"


def resolve_run(path):
    """A run path as given, else under the main checkout (so worktrees can name local/coverage/x.txt or x.txt)."""
    if Path(path).exists():
        return str(path)
    for cand in (tsdb.main_root() / path, coverage_dir() / path):
        if cand.exists():
            return str(cand)
    return str(path)


def resolve_runs(paths, quiet=False):
    """The runs to read: those given (each through resolve_run), else the reference set that exists."""
    if paths:
        return [resolve_run(x) for x in paths]
    found = [coverage_dir() / n for n in DEFAULT_RUNS if (coverage_dir() / n).exists()]
    if not found:
        sys.exit(f"coverage: no runs given and no reference set in {coverage_dir()}")
    if not quiet:
        missing = [n for n in DEFAULT_RUNS if not (coverage_dir() / n).exists()]
        print(f"(runs: the reference set, {len(found)} runs as one session: {', '.join(f.stem for f in found)}"
              + (f"; missing {', '.join(missing)}" if missing else "") + ")", file=sys.stderr)
    return [str(f) for f in found]


def round_key(rid):
    return tuple(int(x) for x in rid.split(":"))


def STD_NAME(s):
    """A name of the standard library: std::... (demangled or a status entry's name), or a mangled class in std."""
    s = (s or "").lstrip("~")
    return s.startswith("std::") or (s.startswith(".?A") and s.endswith("@std@@"))


def bsim_stl_leads(path=None):
    """RVAs BSim ranks as high-confidence stlprobe (STL) matches (local/bsim/candidates.jsonl in the main checkout, from
    tools/bsim.py candidates); empty when the file is absent."""
    path = path or tsdb.main_root() / "local/bsim/candidates.jsonl"
    try:
        with open(path, encoding="utf-8") as fh:
            rows = [json.loads(line) for line in fh if line.strip()]
    except OSError:
        return set()
    return {r["rva"] for r in rows if r.get("lib") == "stlprobe" and r.get("confidence") == "high"}


class Facts:
    """db/ joined per function: size, name, class, subsystem, status, library, H set, and the image for call kinds."""

    def __init__(self):
        import status as status_tool
        funcs = {f["rva"]: f for f in tsdb.functions()}
        self.funcs = status_tool.with_tail_calls(dict(funcs))
        self.status = tsdb.load_status()
        self.library = tsdb.library_rvas()
        sub_rows = tsdb.jsonl("subsystems.jsonl")
        self.subsystem = {e["rva"]: e["subsystem"] for e in sub_rows}  # class-backed, else "other"
        self.guess = {e["rva"]: e["guess"] for e in sub_rows if e.get("guess")}  # other's call-graph guesses
        self.names = status_tool.best_names(self.funcs, self.status)
        self.stl_leads = bsim_stl_leads()
        _, roots = status_tool.startup_roots()
        replaced = {r: e for r, e in self.status.items() if e["status"] == "replaced"}
        self.h_set = status_tool.walk(self.funcs, roots, replaced)
        self.thunks = tsdb.thunk_targets(self.funcs)
        self.pe = load_image()
        self.callers = defaultdict(set)
        for r, f in self.funcs.items():
            for c in f.get("callees", []):
                self.callers[c].add(r)
        self.starts = sorted(int(r, 16) for r in self.funcs)

    def containing(self, rva):
        import bisect
        k = bisect.bisect_right(self.starts, rva) - 1
        if k < 0:
            return None
        r = hex(self.starts[k])
        return r if rva < self.starts[k] + self.funcs[r]["size"] else None

    def add_run_callers(self, hits):
        """The functions the run's return addresses lie in count as callers too (calls the static list lacks)."""
        for rva, frm, ms, thread in hits:
            if frm.startswith("i"):
                c = self.containing(int(frm[1:], 16))
                if c and c != rva:
                    self.callers[rva].add(c)

    def owner(self, rva, depth=4):
        """The function's class, else "~Class": the class of its nearest callers that have one (breadth first, up to
        `depth` calls up; "~A+N" when they disagree: A the most common, N other classes). "" when none is found."""
        cls = self.names.get(rva, ("", ""))[1]
        if cls:
            return cls
        seen, frontier = {rva}, [rva]
        for _ in range(depth):
            found, nxt = defaultdict(int), []
            for x in frontier:
                for c in self.callers.get(x, ()):
                    if c in seen:
                        continue
                    seen.add(c)
                    k = self.names.get(c, ("", ""))[1]
                    if k:
                        found[k] += 1
                    else:
                        nxt.append(c)
            if found:
                ranked = sorted(found, key=lambda k: (-found[k], k))
                return "~" + ranked[0] + (f"+{len(ranked) - 1}" if len(ranked) > 1 else "")
            frontier = nxt
        return ""

    def size(self, rva):
        return self.funcs.get(rva, {}).get("size", 0)

    def sub(self, rva, guesses=False):
        """db/subsystems.jsonl's subsystem ("other" for a call-graph guess); with guesses, the guess in its place."""
        s = self.subsystem.get(rva, "") or "?"
        return self.guess.get(rva, s) if guesses and s == "other" else s

    def library_instance(self, rva, owner=None):
        """Why a not-done function is library code the toolchain provides (an STL instance EA's older compiler made,
        which libmatch could not match byte for byte), or "": its name or class is std::... (status entry, Ghidra
        annotation, names), or it has no class and its nearest classed callers are std:: classes (an STL internal
        helper), or BSim ranks it a high-confidence stlprobe match (local/bsim/candidates.jsonl: a lead, not a fact)."""
        name, cls = self.names.get(rva, ("", ""))
        if STD_NAME(name) or STD_NAME(cls):
            return "std name"
        owner = self.owner(rva) if owner is None else owner
        if owner.startswith("~") and STD_NAME(owner[1:]):
            return "std owner"
        if not cls and rva in self.stl_leads:
            return "bsim stlprobe lead"
        return ""

    def kind(self, rva):
        if rva in self.library:
            return "library"
        if self.subsystem.get(rva) == "third-party":
            return "third-party"
        return self.status.get(rva, {}).get("status", "none")

    def done(self, rva):
        return self.kind(rva) in ("library", "verified", "replaced")

    def final_target(self, rva):
        seen = set()
        while rva in self.thunks and rva not in seen:
            seen.add(rva)
            rva = self.thunks[rva]
        return rva

    def reached(self, rva, frm):
        """direct / tail / indirect / ours / ? (see the module doc)."""
        if not frm.startswith("i"):
            return "ours"
        ret = int(frm[1:], 16)
        try:
            before = self.pe.get_data(ret - 7, 7)  # before[7 - n] is the byte n before the return address
        except Exception:
            return "?"
        if before[2] == 0xE8:  # call rel32
            rel = int.from_bytes(before[3:7], "little", signed=True)
            target = hex((ret + rel) & 0xFFFFFFFF)
            return "direct" if target == rva or self.final_target(target) == rva else "tail"
        # call r/m32 (FF /2), 2, 3, 4, 6 or 7 bytes long: FF, then a ModRM byte whose reg field is 2.
        if any(before[7 - n] == 0xFF and (before[8 - n] & 0x38) == 0x10 for n in (2, 3, 4, 6, 7)):
            return "indirect"
        return "?"


def summarize(facts, hits, label, top):
    seen = {}
    for rva, frm, ms, thread in hits:
        seen.setdefault(rva, (frm, ms, thread))
    total = sum(facts.size(r) for r in seen)
    print(f"round {label}: {len(seen):,} functions, {total:,} bytes ran")
    by_kind = defaultdict(lambda: [0, 0])
    for r in seen:
        k = by_kind[facts.kind(r)]
        k[0] += 1
        k[1] += facts.size(r)
    print("  by status: " + "; ".join(f"{k} {n:,} ({b:,} B)" for k, (n, b) in sorted(by_kind.items(), key=lambda x: -x[1][1])))
    todo = {r for r in seen if not facts.done(r) and facts.kind(r) != "third-party"}
    third = {r for r in seen if facts.kind(r) == "third-party"}
    outside_h = {r for r in seen if r not in facts.h_set}
    todo_outside = todo & outside_h

    def bytes_of(rvas):
        return sum(facts.size(r) for r in rvas)
    print(f"  EA code not done: {len(todo):,} functions, {bytes_of(todo):,} B; third-party: {len(third):,}, {bytes_of(third):,} B")
    print(f"  outside milestone H's direct-call set: {len(outside_h):,} functions, {bytes_of(outside_h):,} B;"
          f" not done {len(todo_outside):,}, {bytes_of(todo_outside):,} B")
    how = defaultdict(lambda: [0, 0])
    for r in todo:
        h = how[facts.reached(r, seen[r][0])]
        h[0] += 1
        h[1] += facts.size(r)
    print("  not done, reached by (first call): " + "; ".join(f"{k} {n:,} ({b:,} B)" for k, (n, b) in sorted(how.items(), key=lambda x: -x[1][1])))
    for title, key in (("subsystem", lambda r: facts.subsystem.get(r, "?")), ("class", lambda r: facts.names[r][1] or "(no class)")):
        groups = defaultdict(lambda: [0, 0, 0, 0])  # not-done n, not-done bytes, ran n, ran bytes
        for r in seen:
            if r not in facts.funcs or facts.kind(r) in ("library", "third-party"):
                continue
            g = groups[key(r)]
            g[2] += 1
            g[3] += facts.size(r)
            if r in todo:
                g[0] += 1
                g[1] += facts.size(r)
        rows = sorted(groups.items(), key=lambda x: -x[1][1])[:top]
        print(f"  not done by {title} (top {top}: not-done functions / bytes of what ran there):")
        for name, (n, b, rn, rb) in rows:
            if b:
                print(f"    {b:>9,} B {n:>5} of {rn:<5} {name}")
    owners = defaultdict(lambda: [0, 0, 0, 0])  # not-done n, bytes; of them inferred from callers n, bytes
    for r in todo:
        o = facts.owner(r)
        g = owners[o.lstrip("~").split("+")[0] or "(none found)"]
        g[0] += 1
        g[1] += facts.size(r)
        if o.startswith("~"):
            g[2] += 1
            g[3] += facts.size(r)
    print(f"  not done by owner (top {top}: the class, else the nearest callers' class; not-done functions / bytes,"
          f" and how much of that is inferred from callers):")
    for name, (n, b, i_n, i_b) in sorted(owners.items(), key=lambda x: -x[1][1])[:top]:
        print(f"    {b:>9,} B {n:>5}  inferred {i_n:>4} ({i_b:>7,} B)  {name}")
    ours =sorted((r for r in seen if not seen[r][0].startswith("i")), key=lambda r: -facts.size(r))
    if ours:
        print(f"  originals first reached from outside the image (our code, or a system DLL calling back): {len(ours)},"
              f" {bytes_of(ours):,} B; largest: " + ", ".join(f"{r} {facts.names.get(r, ('?',))[0][:40]}" for r in ours[:8]))
    return seen


def cmd_report(args):
    run = parse_run(args.run)
    facts = Facts()
    labels = {n: f"{n} ({label}, at {ms} ms)" for n, ms, label in run["rounds"]}
    rounds = sorted(run["hits"]) if args.round is None else [args.round]
    for hits in run["hits"].values():
        facts.add_run_callers(hits)
    if not args.json:
        for line in run["header"]:
            print(line)
        print(f"sites left alone: ours {len(run['ours']):,} (hooked/trapped), mismatched {len(run['mismatched']):,}")
    if args.json:
        for n in rounds:
            for rva, frm, ms, thread in run["hits"][n]:
                name, cls = facts.names.get(rva, ("", ""))
                print(json.dumps({"round": n, "rva": rva, "from": frm, "ms": ms, "thread": thread, "size": facts.size(rva),
                                  "status": facts.kind(rva), "in_h": rva in facts.h_set, "reached": facts.reached(rva, frm),
                                  "subsystem": facts.subsystem.get(rva, ""), "class": cls, "owner": facts.owner(rva),
                                  "name": name}))
        return
    if args.todo:
        for n in rounds:
            print(f"round {labels.get(n, n)}: not done, largest first")
            seen = {}
            for rva, frm, ms, thread in run["hits"][n]:
                seen.setdefault(rva, frm)
            rows = [r for r in seen if not facts.done(r) and facts.kind(r) != "third-party"]
            for r in sorted(rows, key=lambda r: -facts.size(r)):
                name, cls = facts.names.get(r, ("", ""))
                print(f"  {facts.size(r):>6} {r:>9} {facts.kind(r):<13} {facts.reached(r, seen[r]):<8} {'H' if r in facts.h_set else '-'}"
                      f" {facts.subsystem.get(r, '?'):<14} {(cls or facts.owner(r))[:40]:<40} {name[:70]}")
        return
    for n in rounds:
        summarize(facts, run["hits"][n], labels.get(n, str(n)), args.top)


def cmd_diff(args):
    a, b = parse_run(args.a), parse_run(args.b)
    facts = Facts()
    ra = {h[0] for h in a["hits"].get(args.round, [])}
    rb = {h[0] for h in b["hits"].get(args.round, [])}
    for title, rvas in ((f"only in {args.b}", rb - ra), (f"only in {args.a}", ra - rb)):
        print(f"{title}: {len(rvas):,} functions, {sum(facts.size(r) for r in rvas):,} B")
        for r in sorted(rvas, key=lambda r: -facts.size(r))[:args.top]:
            name, cls = facts.names.get(r, ("", ""))
            print(f"  {facts.size(r):>6} {r:>9} {facts.kind(r):<13} {name[:80]}")


def session(facts, paths):
    """load_runs plus the run callers fed to `facts` (owners), and the EA split of what executed."""
    s = load_runs(paths)
    for rva, e in s["executed"].items():
        facts.add_run_callers([(rva, frm, 0, 0) for frm in e["froms"]])
    ea = {r for r in s["executed"] if facts.kind(r) not in ("library", "third-party")}
    s["ea"] = ea
    s["todo"] = {r for r in ea if not facts.done(r)}
    return s


def headline_numbers(facts, s):
    size = facts.size
    ea, todo = s["ea"], s["todo"]
    by_status, by_sub = defaultdict(int), defaultdict(int)
    for r in todo:
        by_status[facts.kind(r)] += size(r)
        by_sub[facts.subsystem.get(r, "") or "?"] += size(r)
    ours = {r for r in s["ours"] if facts.kind(r) not in ("library", "third-party")}
    ours_todo = {r for r in ours if not facts.done(r)}
    lib = [r for r in s["executed"] if facts.kind(r) == "library"]
    third = [r for r in s["executed"] if facts.kind(r) == "third-party"]
    return {
        "rounds": len(s["legend"]), "runs": s["files"],
        "ea_functions": len(ea), "ea_bytes": sum(size(r) for r in ea),
        "done_bytes": sum(size(r) for r in ea - todo),
        "not_done_functions": len(todo), "not_done_bytes": sum(size(r) for r in todo),
        "not_done_by_status": dict(sorted(by_status.items(), key=lambda x: -x[1])),
        "not_done_by_subsystem": dict(sorted(by_sub.items(), key=lambda x: -x[1])),
        "ours_sites": len(ours), "ours_bytes": sum(size(r) for r in ours),
        "ours_not_done_sites": len(ours_todo), "ours_not_done_bytes": sum(size(r) for r in ours_todo),
        "library_bytes": sum(size(r) for r in lib), "third_party_bytes": sum(size(r) for r in third),
    }


def cmd_headline(args):
    facts = Facts()
    h = headline_numbers(facts, session(facts, args.runs))
    if args.json:
        print(json.dumps(h))
        return
    st = ", ".join(f"{k} {b:,}" for k, b in h["not_done_by_status"].items())
    print(f"headline: {h['not_done_bytes']:,} B of EA code executed and not done ({h['not_done_functions']:,} functions:"
          f" {st}); EA code executed {h['ea_bytes']:,} B in {h['ea_functions']:,} functions, of which done"
          f" {h['done_bytes']:,} B ({h['runs']} run(s), {h['rounds']} rounds, each function counted once)")
    print(f"  not measured: our hooked/trapped sites {h['ours_sites']:,} ({h['ours_bytes']:,} B; of them not done"
          f" {h['ours_not_done_sites']:,}, {h['ours_not_done_bytes']:,} B): whether they ran needs a -Reference run;"
          f" also executed: library {h['library_bytes']:,} B, third-party {h['third_party_bytes']:,} B")
    print("  not done by subsystem: " + ", ".join(f"{k} {b:,}" for k, b in h["not_done_by_subsystem"].items()))


def queue_groups(facts, s, subsystems=None, whole=False, guesses=False, keep_library=False, dropped=None):
    """Not-done executed EA code grouped by class, else owner (the nearest callers' class), largest first.

    Each group's "campaign" is its class's subsystem in db/subsystems.jsonl (where most of the class's class-backed
    bytes are filed), else the largest class-backed subsystem among its functions, else "other". With subsystems: only
    functions filed (class-backed) under those, or with `whole`, the whole groups whose campaign is one of them (a
    campaign's scope: a class's helpers filed under "other" come with it). `guesses` reads each call-graph guess as its
    subsystem instead of "other". Library instances (Facts.library_instance) are left out unless keep_library; a
    `dropped` dict receives them (rva -> reason)."""
    subsystems = set(subsystems or [])
    keep = subsystems
    subsystems = set() if whole else subsystems
    cls_total, cls_done = defaultdict(int), defaultdict(int)
    cls_subs = defaultdict(lambda: defaultdict(int))
    for r in facts.funcs:
        cls = facts.names.get(r, ("", ""))[1]
        if cls and facts.kind(r) != "third-party":
            cls_total[cls] += facts.size(r)
            if facts.done(r):
                cls_done[cls] += facts.size(r)
            sub = facts.sub(r, guesses)
            if sub not in ("other", "?"):
                cls_subs[cls][sub] += facts.size(r)
    groups = {}
    for r in s["todo"]:
        sub = facts.sub(r, guesses)
        if subsystems and sub not in subsystems:
            continue
        o = facts.owner(r)
        if not keep_library:
            why = facts.library_instance(r, o)
            if why:
                if dropped is not None:
                    dropped[r] = why
                continue
        name = o.lstrip("~").split("+")[0] or "(none found)"
        g = groups.setdefault(name, {"group": name, "bytes": 0, "functions": 0, "inferred_bytes": 0,
                                     "subsystems": defaultdict(int), "rounds": set(), "rvas": []})
        n = facts.size(r)
        g["bytes"] += n
        g["functions"] += 1
        if o.startswith("~") or not o:
            g["inferred_bytes"] += n
        g["subsystems"][sub] += n
        g["rounds"] |= s["executed"][r]["rounds"]
        g["rvas"].append(r)
    out = []
    for g in groups.values():
        subs = sorted(g["subsystems"].items(), key=lambda x: (-x[1], x[0]))
        own = sorted(cls_subs.get(g["group"], {}).items(), key=lambda x: (-x[1], x[0]))
        backed = [k for k, _ in subs if k not in ("other", "?")]
        g["campaign"] = own[0][0] if own else backed[0] if backed else "other"
        extra = sum(1 for k, _ in subs if k != g["campaign"])
        g["subsystem"] = g["campaign"] + (f"+{extra}" if extra else "")
        g["subsystems"] = dict(subs)
        g["rounds"] = sorted(g["rounds"], key=round_key)
        g["class_bytes"] = cls_total.get(g["group"], 0)
        g["class_done_bytes"] = cls_done.get(g["group"], 0)
        g["rvas"].sort(key=lambda r: -facts.size(r))
        if whole and keep and g["campaign"] not in keep:
            continue
        out.append(g)
    return sorted(out, key=lambda g: -g["bytes"])


def cmd_queue(args):
    facts = Facts()
    s = session(facts, args.runs)
    subs = [x for x in (args.subsystem or "").split(",") if x]
    dropped = {}
    groups = queue_groups(facts, s, subs, args.whole, getattr(args, "guesses", False),
                          getattr(args, "keep_library", False), dropped)
    if args.group:
        groups = [g for g in groups if g["group"] == args.group]
    if args.json:
        for g in groups[:args.top] if args.top else groups:
            print(json.dumps({k: v for k, v in g.items() if k != "rvas"} | {"rvas": g["rvas"]}))
        return
    h = headline_numbers(facts, s)
    total = sum(g["bytes"] for g in groups)
    print(f"headline: {h['not_done_bytes']:,} B executed and not done, of {h['ea_bytes']:,} B EA code executed"
          f" ({h['done_bytes']:,} B done)")
    print("rounds: " + "; ".join(f"{rid} {label}" for rid, label in s["legend"]))
    print(f"queue{' (' + ','.join(subs) + (', whole groups' if args.whole else '') + ')' if subs else ''}: {total:,} B in"
          f" {sum(g['functions'] for g in groups):,} functions, {len(groups):,} groups (class, else ~owner: inferred"
          f" from callers); class B / done = the class's size in db/ and how much of it is done")
    if dropped:
        why = defaultdict(lambda: [0, 0])
        for r, w in dropped.items():
            why[w][0] += 1
            why[w][1] += facts.size(r)
        print(f"  left out as library instances (--keep-library lists them): {len(dropped):,} functions,"
              f" {sum(facts.size(r) for r in dropped):,} B (" + "; ".join(f"{w} {n} fns {b:,} B" for w, (n, b) in
                                                            sorted(why.items(), key=lambda x: -x[1][1])) + ")")
    shown = groups[:args.top] if args.top else groups
    print(f"  {'bytes':>9} {'fns':>5} {'inferred':>9} {'class B':>9} {'done':>5}  {'subsystem':<16} {'rounds':<14} group")
    for g in shown:
        done = f"{100 * g['class_done_bytes'] // g['class_bytes']}%" if g["class_bytes"] else "-"
        name = g["group"] if g["inferred_bytes"] < g["bytes"] or g["group"].startswith("(") else "~" + g["group"]
        print(f"  {g['bytes']:>9,} {g['functions']:>5} {g['inferred_bytes']:>9,} {g['class_bytes']:>9,} {done:>5}"
              f"  {g['subsystem'][:16]:<16} {','.join(g['rounds'])[:14]:<14} {name}")
        if args.functions:
            for r in g["rvas"]:
                name, cls = facts.names.get(r, ("", ""))
                rounds = ",".join(sorted(s["executed"][r]["rounds"], key=round_key))
                sub = facts.sub(r) + (f"({facts.guess[r]})" if r in facts.guess else "")
                lib = facts.library_instance(r) if getattr(args, "keep_library", False) else ""
                print(f"      {facts.size(r):>6} {r:>9} {'library?' if lib else facts.kind(r):<13} {sub:<14}"
                      f" {rounds:<12} {'' if cls else '~'}{name[:80]}")
    if len(shown) < len(groups):
        rest = groups[len(shown):]
        print(f"  ... {len(rest):,} more groups, {sum(g['bytes'] for g in rest):,} B")


def subsystem_of(facts, rva, guesses=False):
    return facts.sub(rva, guesses) or "?"


def campaign_map(facts, s):
    """rva -> the campaign of its queue group (class, else ~owner; queue_groups): the campaign that would take it. A
    helper db/subsystems.jsonl files under "other" goes with the class that calls it; library instances are "library"."""
    dropped = {}
    out = {}
    for g in queue_groups(facts, s, dropped=dropped):
        for r in g["rvas"]:
            out[r] = g["campaign"]
    for r in dropped:
        out[r] = "library"
    return out


def deps_edges(facts, s, sub_of=None):
    """Call edges between executed not-done EA functions, keyed (caller subsystem, callee subsystem):
    {'edges': n caller->callee pairs, 'callees': set, 'callers': set}. Edges are db/functions.jsonl's direct calls and
    tail jumps plus the run's own return addresses (which add the virtual and other indirect calls that ran).
    sub_of(rva) replaces the subsystem (e.g. campaign_map's)."""
    todo = s["todo"]
    sub_of = sub_of or (lambda r: subsystem_of(facts, r))
    pairs = defaultdict(lambda: {"edges": 0, "callees": set(), "callers": set()})
    for callee in todo:
        b = sub_of(callee)
        for caller in facts.callers.get(callee, ()):
            if caller == callee or caller not in todo:
                continue
            p = pairs[(sub_of(caller), b)]
            p["edges"] += 1
            p["callees"].add(callee)
            p["callers"].add(caller)
    return pairs


def order_cost(order, out_bytes):
    """Bytes of not-done callees a subsystem calls in subsystems ordered after it: the calls whose callee is still
    original (or a test stand-in) when the caller's campaign runs."""
    pos = {s: i for i, s in enumerate(order)}
    return sum(n for (a, b), n in out_bytes.items() if a in pos and b in pos and pos[a] < pos[b])


def bottom_up_order(own, out_bytes):
    """An order of the subsystems that keeps the calls into later subsystems small (a minimum feedback arc set
    ordering, heuristically): Eades-Lin-Smyth (take sinks first; else the node whose callers outweigh its callees most),
    then single-node moves while any lowers order_cost. out_bytes[(a, b)] = bytes of b's not-done callees that a calls.
    Returns [(subsystem, bytes into later subsystems, {later subsystem: bytes})]."""
    left, order = set(own), []

    def w(a, b):
        return out_bytes.get((a, b), 0)
    while left:
        def score(a):
            return sum(w(b, a) for b in left if b != a) - sum(w(a, b) for b in left if b != a)
        pick = max(sorted(left), key=lambda a: (score(a), -own[a]))
        order.append(pick)
        left.remove(pick)
    best = order_cost(order, out_bytes)
    improved = True
    while improved:
        improved = False
        for i in range(len(order)):
            for j in range(len(order)):
                if i == j:
                    continue
                trial = order[:i] + order[i + 1:]
                trial.insert(j, order[i])
                c = order_cost(trial, out_bytes)
                if c < best:
                    order, best, improved = trial, c, True
                    break
            if improved:
                break
    out = []
    for i, a in enumerate(order):
        later = {b: w(a, b) for b in order[i + 1:] if w(a, b)}
        out.append((a, sum(later.values()), dict(sorted(later.items(), key=lambda x: -x[1]))))
    return out


def strongly_connected(nodes, edges):
    """Tarjan's SCCs over {a: set(b)}; only components of more than one node, largest first."""
    index, low, stack, on, out, counter = {}, {}, [], set(), [], [0]

    def visit(v):
        index[v] = low[v] = counter[0]
        counter[0] += 1
        stack.append(v)
        on.add(v)
        for w in edges.get(v, ()):
            if w not in index:
                visit(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1:
                out.append(sorted(comp))
    for v in sorted(nodes):
        if v not in index:
            visit(v)
    return sorted(out, key=len, reverse=True)


# Hard test boundaries: imports by what they need faked in a test, plus the Direct3D device (reached through COM
# vtables, so seen through the HRESULT check every device call is followed by: LogD3DError 0x18def0).
D3D_CHECK = "0x18def0"
BOUNDARIES = [
    ("d3d", lambda c: c.upper().startswith("D3D9.DLL!") or c == D3D_CHECK),
    ("file", lambda c: any(k in c for k in ("CreateFile", "ReadFile", "WriteFile", "SetFilePointer", "FindFirstFile",
                                             "FindNextFile", "GetFileAttributes", "DeleteFile", "CreateDirectory",
                                             "GetFileSize", "FlushFileBuffers", "MoveFile", "CopyFile", "GetFullPathName",
                                             "SHGetFolderPath", "GetCurrentDirectory", "MapViewOfFile",
                                             "CreateFileMapping"))),
    ("threads/sync", lambda c: any(k in c for k in ("CreateThread", "WaitForSingleObject", "WaitForMultipleObjects",
                                                     "CreateEvent", "SetEvent", "ResetEvent", "CreateMutex",
                                                     "ReleaseMutex", "CreateSemaphore", "ReleaseSemaphore", "Sleep",
                                                     "ResumeThread", "SuspendThread", "SetThreadPriority",
                                                     "TerminateThread", "ExitThread"))),
    ("timers/clock", lambda c: any(k in c for k in ("QueryPerformance", "timeGetTime", "GetTickCount", "GetSystemTime",
                                                     "GetLocalTime", "timeBeginPeriod", "timeEndPeriod",
                                                     "SetTimer", "KillTimer"))),
    ("audio", lambda c: c.upper().startswith(("DSOUND.DLL!", "WINMM.DLL!WAVE", "WINMM.DLL!MIXER"))),
    ("network", lambda c: c.upper().startswith(("WS2_32.DLL!", "WSOCK32.DLL!", "WININET.DLL!", "WINHTTP.DLL!"))),
    ("win32 ui", lambda c: c.upper().startswith(("USER32.DLL!", "GDI32.DLL!", "IMM32.DLL!", "COMCTL32.DLL!",
                                                 "SHELL32.DLL!"))),
    ("registry", lambda c: "!Reg" in c),
    ("other imports", lambda c: "!" in c),
]


def boundary_kinds(facts, rva):
    kinds = set()
    for c in facts.funcs.get(rva, {}).get("callees", ()):
        if c != D3D_CHECK and "!" not in c:
            continue
        kinds.add(next(kind for kind, test in BOUNDARIES if test(c)))
    return kinds


def cmd_deps(args):
    facts = Facts()
    s = session(facts, args.runs)
    if getattr(args, "by", "subsystem") == "group":
        campaign = campaign_map(facts, s)
        sub_of = lambda r: campaign.get(r, "?")  # noqa: E731
    else:
        guesses = getattr(args, "guesses", False)
        sub_of = lambda r: subsystem_of(facts, r, guesses)  # noqa: E731
    pairs = deps_edges(facts, s, sub_of)
    own = defaultdict(int)
    for r in s["todo"]:
        own[sub_of(r)] += facts.size(r)
    own = {k: v for k, v in own.items() if v}
    nbytes = lambda rvas: sum(facts.size(r) for r in rvas)
    if args.pair:
        a, b = args.pair.split(",")
        p = pairs.get((a, b))
        if not p:
            print(f"{a} -> {b}: no edges")
            return
        print(f"{a} -> {b}: {p['edges']:,} edges, {len(p['callers']):,} callers ({nbytes(p['callers']):,} B),"
              f" {len(p['callees']):,} callees ({nbytes(p['callees']):,} B); callees by group (class, else ~owner):")
        by = defaultdict(lambda: [0, 0, set()])
        for c in p["callees"]:
            g = by[facts.owner(c) or "(none found)"]
            g[0] += 1
            g[1] += facts.size(c)
            for caller in facts.callers.get(c, ()):
                if caller in p["callers"]:
                    g[2].add(facts.owner(caller) or "(none found)")
        for name, (n, b_, frm) in sorted(by.items(), key=lambda x: -x[1][1])[:args.top]:
            print(f"  {b_:>8,} B {n:>4}  {name}  <- {', '.join(sorted(frm)[:4])}{' ...' if len(frm) > 4 else ''}")
        return
    out_bytes = {k: nbytes(v["callees"]) for k, v in pairs.items() if k[0] != k[1]}
    rows = sorted(((k, v) for k, v in pairs.items() if k[0] != k[1]), key=lambda x: -out_bytes[x[0]])
    if args.json:
        for (a, b), v in rows:
            print(json.dumps({"from": a, "to": b, "edges": v["edges"], "callees": len(v["callees"]),
                              "callee_bytes": out_bytes[(a, b)], "callers": len(v["callers"]),
                              "caller_bytes": nbytes(v["callers"])}))
        return
    h = headline_numbers(facts, s)
    by = ("campaign (each class or ~owner group under its class's subsystem; library instances apart)"
          if getattr(args, "by", "") == "group" else "subsystem (db/subsystems.jsonl" + (
              "; 'other' read as its call-graph guess)" if getattr(args, "guesses", False) else "; guesses are 'other')"))
    print(f"deps: call edges between executed not-done EA functions ({h['not_done_bytes']:,} B in"
          f" {h['not_done_functions']:,} functions), by {by}; edges = db/functions.jsonl calls and tail jumps plus"
          f" the run's return addresses (indirect calls that ran); bytes = distinct not-done callees in the target")
    print(f"pairs (top {args.top} of {len(rows)}, callee bytes >= {args.min_bytes:,} marked *):")
    for (a, b), v in rows[:args.top]:
        mark = "*" if out_bytes[(a, b)] >= args.min_bytes else " "
        print(f"  {mark}{a:>16} -> {b:<16} {out_bytes[(a, b)]:>9,} B {len(v['callees']):>5} callees"
              f" {v['edges']:>6} edges  (back: {out_bytes.get((b, a), 0):,} B)")
    print("per subsystem: own not-done bytes; bytes it calls in other subsystems' not-done code; bytes of its own"
          " not-done code other subsystems call; internal edges")
    for sub in sorted(own, key=lambda x: -own[x]):
        outs = sum(v for (a, b), v in out_bytes.items() if a == sub)
        ins = nbytes(set().union(*([v["callees"] for (a, b), v in pairs.items() if b == sub and a != sub] or [set()])))
        internal = pairs.get((sub, sub), {}).get("edges", 0)
        print(f"  {sub:<16} own {own[sub]:>9,}  calls out {outs:>9,}  called in {ins:>9,}  internal edges {internal:,}")
    order = bottom_up_order(own, out_bytes)
    print(f"bottom-up order (callees first; {sum(x[1] for x in order):,} B of calls go into a later subsystem, of"
          f" {sum(out_bytes.values()):,} B between subsystems):")
    for i, (sub, later, into) in enumerate(order, 1):
        top = ", ".join(f"{b} {n:,}" for b, n in list(into.items())[:4])
        print(f"  {i:>2}. {sub:<16} own {own[sub]:>9,}  into later {later:>8,}{'  (' + top + ')' if top else ''}")
    graph = defaultdict(set)
    for (a, b), n in out_bytes.items():
        if n >= args.min_bytes:
            graph[a].add(b)
    cycles = strongly_connected(own, graph)
    print(f"cycles (edges >= {args.min_bytes:,} B): " + ("none" if not cycles else ""))
    for comp in cycles:
        inner = sorted(((a, b, out_bytes[(a, b)]) for a in comp for b in comp if a != b and (a, b) in out_bytes
                        and out_bytes[(a, b)] >= args.min_bytes), key=lambda x: x[2])
        print(f"  {{{', '.join(comp)}}}: lightest edges " + "; ".join(f"{a}->{b} {n:,}" for a, b, n in inner[:6]))
    kinds = defaultdict(lambda: defaultdict(lambda: [0, 0]))
    for r in s["todo"]:
        for k in boundary_kinds(facts, r):
            g = kinds[sub_of(r)][k]
            g[0] += 1
            g[1] += facts.size(r)
    print("hard test boundaries: not-done executed functions calling them directly (functions / bytes); d3d = the"
          " Direct3D import or a device call checked by LogD3DError 0x18def0")
    for sub in sorted(kinds, key=lambda x: -own.get(x, 0)):
        print(f"  {sub:<16} " + "; ".join(f"{k} {n} ({b:,} B)" for k, (n, b) in sorted(kinds[sub].items(),
                                                                                    key=lambda x: -x[1][1])))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sites")
    s.add_argument("--out", default=str(tsdb.main_root() / "local/coverage/sites.txt"))
    s.add_argument("--all", action="store_true")
    r = sub.add_parser("report")
    r.add_argument("run")
    r.add_argument("--round", type=int)
    r.add_argument("--top", type=int, default=25)
    r.add_argument("--json", action="store_true")
    r.add_argument("--todo", action="store_true")
    d = sub.add_parser("diff")
    d.add_argument("a")
    d.add_argument("b")
    d.add_argument("--round", type=int, default=0)
    d.add_argument("--top", type=int, default=40)
    q = sub.add_parser("queue")
    q.add_argument("runs", nargs="*", help="runs read as one session (default: the reference set)")
    q.add_argument("--subsystem", help="comma-separated: only functions filed under these subsystems (class-backed;"
                   " call-graph guesses are 'other')")
    q.add_argument("--whole", action="store_true", help="with --subsystem: whole groups whose campaign (the class's"
                   " subsystem) is one of them, helpers filed under other subsystems or 'other' included")
    q.add_argument("--guesses", action="store_true", help="read each 'other' function's call-graph guess as its"
                   " subsystem (--subsystem X then also keeps X's guesses)")
    q.add_argument("--keep-library", action="store_true", help="keep library instances (std:: names, std-owned"
                   " helpers, BSim stlprobe leads), marked library? under --functions")
    q.add_argument("--top", type=int, default=40, help="groups shown (0: all)")
    q.add_argument("--group", help="only this group (a class name, or the owner without '~')")
    q.add_argument("--functions", action="store_true", help="list each group's functions under it")
    q.add_argument("--json", action="store_true")
    dp = sub.add_parser("deps")
    dp.add_argument("runs", nargs="*", help="runs read as one session (default: the reference set)")
    dp.add_argument("--top", type=int, default=40, help="pairs (or, with --pair, callee groups) shown")
    dp.add_argument("--min-bytes", type=int, default=4000, help="edges at least this heavy count for cycles")
    dp.add_argument("--pair", help="A,B: the callees in B that A's not-done code calls, by group")
    dp.add_argument("--by", choices=("subsystem", "group"), default="subsystem",
                    help="group: file each function under its class or ~owner group's largest subsystem (campaign scope)")
    dp.add_argument("--guesses", action="store_true", help="--by subsystem: read each 'other' function's call-graph"
                    " guess as its subsystem")
    dp.add_argument("--json", action="store_true")
    hd = sub.add_parser("headline")
    hd.add_argument("runs", nargs="*", help="runs read as one session (default: the reference set)")
    hd.add_argument("--json", action="store_true")
    args = p.parse_args()
    if args.cmd in ("queue", "deps", "headline"):
        args.runs = resolve_runs(args.runs)
    elif args.cmd == "report":
        args.run = resolve_run(args.run)
    elif args.cmd == "diff":
        args.a, args.b = resolve_run(args.a), resolve_run(args.b)
    {"sites": cmd_sites, "report": cmd_report, "diff": cmd_diff, "queue": cmd_queue,
     "headline": cmd_headline, "deps": cmd_deps}[args.cmd](args)


if __name__ == "__main__":
    main()
