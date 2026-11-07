"""Checks db/status/*.jsonl and prints progress.

Usage: python -I tools/status.py [--check [--group G]] [--classes] [--metrics] [--reachable-todo [--json] [--live]]
                                 [--rva 0x...[,0x...] [--json]]
  --check    only validate (exit 1 on problems): one entry per RVA across all files, a known status,
             src/test files that exist (test required for verified), RVAs that are known functions,
             and the original-callee rules (tools/origcalls.py).
  --group G  with --check: only the problems (and notes) of db/status/G.jsonl and the src/test files its entries
             name; problems tied to no group's files (no host build, the trap table) become notes. Its last line
             (stdout) is always `group G: clean` or `group G: N problem(s)`, with the note count if any.
  --classes  only the class lists (no validation or totals): nearly complete classes (every virtual done but 1-5;
             --limit N of them, default 40; folded shared_vf_* slots marked), then the complete ones.
  --metrics  only the status totals and the roadmap metrics (no validation, per-file table or RVA lists).
  --reachable-todo  only list what the "reachable" line counts as not done (milestone H), largest first: size,
             status, subsystem (a call-graph guess as "guess?"), class, name, and how many reachable done / not-done
             functions call it; with --json one object per line, with the caller and not-done callee RVAs.
  --rva      for each RVA: status, group, class, original_calls and the reachable line's facts (reached, done and
             why, live, kind, callers); --json for one object per line.
Without --check, also prints the roadmap metrics: verified bytes of game code, the headline "no longer needs decompiling"
(its bands, what it leaves out; decompilation work remaining, not image independence), classes whose
virtuals are all done, and progress on code reachable from the entry point by direct calls (the "reachable" line:
done = verified, `replaced` (descended only through its original_calls), import thunks (`jmp [IAT]`, found in the
image), library code, tiny folded functions (shared_vf_* leaves under 8 bytes: unhookable) whose every direct caller
is verified, and jump stubs to done code).
Each line: {"rva", "name", "status", "src", "test" (optional with "verify": "match"), "notes"}, plus "original_calls" (RVAs of original
functions it still calls through OriginalFunction) when it has any; see CLAUDE.md for the status levels.
A verified entry may instead be verified by byte match (tools/matchcheck.py): "verify": "match" plus
"matchcheck": <checker version>; it then needs no test file, and is counted separately.
A replacement is in effect through its hook by default; "in_effect": "callers" says it is in effect because nothing
reaches the original any more (CLAUDE.md): the entry then names "callers_check" ("callers.py vN date"), its "trap"
("Section.Key=v1,v2": the host switch values under which it is in use) and optionally "callers_handled" ({site:
reason}); --check reruns tools/callers.py on it (no open reference, not hooked, the trap table up to date).
--check also checks every hook row's site CRC as the host computes it (tools/hookcrc.py).
The "callers" check needs a current host build (build/Release/ts2host.exe and its .map, newer than every src/ and host/
source): without one it is skipped with one "skipped:" line naming why and how to build (the other checks, the trap
table's included, still run and set the exit code).
"""
import argparse
import functools
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import callers  # noqa: E402
import hookcrc  # noqa: E402
import origcalls  # noqa: E402
import ourrefs  # noqa: E402
import tsdb  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--group", help="with --check: only the problems of db/status/<GROUP>.jsonl and its src/test files")
    ap.add_argument("--classes", action="store_true",
                    help="only the class lists: complete classes and nearly complete ones (no validation or totals)")
    ap.add_argument("--limit", type=int, default=40, help="with --classes: how many nearly complete classes (default 40)")
    ap.add_argument("--group-missing", action="store_true",
                    help="with --classes: one line per missing-slot set (classes it completes, bytes, names), best "
                         "classes per byte first")
    ap.add_argument("--metrics", action="store_true",
                    help="only the totals and the roadmap metrics: no validation, per-file table or RVA lists")
    ap.add_argument("--reachable-todo", action="store_true",
                    help="only list the reachable game functions the \"reachable\" line doesn't count as done")
    ap.add_argument("--rva", help="comma-separated RVAs: status, group, class and the reachable line's facts for each")
    ap.add_argument("--json", action="store_true", help="with --reachable-todo or --rva: one JSON object per line")
    ap.add_argument("--live", action="store_true",
                    help="with --reachable-todo: only what is still reached under our replacements")
    rec = ap.add_argument_group("--record / --brief (tools/statusrecord.py)")
    rec.add_argument("--record", action="store_true",
                     help="with --group: draft the group's entries (new and changed lines on stdout, report on stderr)")
    rec.add_argument("--test", action="append", help="with --record: a test file of the group (repeatable; default: "
                     "the entries' test files and those with a DIFF_TEST on a hooked function of the group)")
    rec.add_argument("--src", action="append", help="with --record: a src file of the group (repeatable; default: the "
                     "entries' src files and those defining the hooks' replacements)")
    rec.add_argument("--add", action="append", help="with --record: RVAs to draft whatever the scans say (repeatable; comma-separated lists too)")
    rec.add_argument("--mutate-log", action="append", help="with --record: a tools/mutate.py --log file (its last run; "
                     "repeatable)")
    rec.add_argument("--mutants", action="append", help="with --record: the mutants JSON of each --mutate-log, in "
                     "order: attributes each result to the function it edits")
    rec.add_argument("--class", dest="cls", help="with --record: draft only this class's own functions (its vtables' "
                     "slots no class outside its family uses, functions named after it, and what only those reach); "
                     "full RTTI name or its last part. For a group whose tests cover several classes")
    rec.add_argument("--out", help="with --record: write the whole merged file here")
    rec.add_argument("--write", action="store_true", help="with --record: merge into db/status/<group>.jsonl")
    rec.add_argument("--no-binary", action="store_true", help="with --record: skip the scan of our build (callers.py)")
    rec.add_argument("--brief", action="store_true", help="with --group: one line per entry (rva, status, in_effect, "
                     "original calls, test, name); filters --rva, --status, --in-effect, --test-has, --name-has, --no-test")
    rec.add_argument("--status", help="with --brief: comma-separated statuses")
    rec.add_argument("--in-effect", choices=("hook", "callers"), help="with --brief")
    rec.add_argument("--test-has", help="with --brief: test path substring")
    rec.add_argument("--name-has", help="with --brief: name substring (any case)")
    rec.add_argument("--no-test", action="store_true", help="with --brief: entries without a test")
    args = ap.parse_args()
    if args.record or args.brief:
        if not args.group:
            sys.exit("--record and --brief need --group")
        if args.cls and not args.record:
            sys.exit("--class goes with --record")
        import statusrecord
        sys.exit(statusrecord.record(args) if args.record else statusrecord.brief(args))
    if args.reachable_todo:
        reachable_todo(args.json, args.live)
        return
    if args.rva:
        sys.exit(rva_report([r for r in args.rva.replace(" ", ",").split(",") if r], args.json))

    funcs = {f["rva"]: f for f in tsdb.functions()}
    entries = tsdb.status_entries()
    if args.classes:
        status = {e["rva"]: e for _, e in entries}
        library = tsdb.library_rvas()
        verified = {r for r, e in status.items() if e["status"] == "verified"}
        with_tail_calls(funcs)  # as summary() counts done
        class_lists(funcs, status, library, tsdb.resolve_done(verified | library, funcs), args.limit,
                    args.group_missing)
        return
    if args.metrics:
        print_totals(funcs, entries, per_file=False)
        summary(funcs, {e["rva"]: e for _, e in entries}, list_rvas=False)
        return
    if args.group and not any(f == args.group + ".jsonl" for f, _ in entries):
        sys.exit(f"no db/status/{args.group}.jsonl")
    problems, notes, seen = [], [], {}
    by_rva = {e.get("rva"): e for _, e in entries}
    exists = functools.lru_cache(maxsize=None)(lambda path: (tsdb.ROOT / path).exists())
    for f, e in entries:
        where = f"{f}: {e.get('rva')} {e.get('name')}"
        required = ("rva", "name", "status", "src", "notes") + (() if e.get("verify") == "match" else ("test",))
        missing = [k for k in required if k not in e]
        if missing:
            problems.append(f"{where}: missing {missing}")
            continue
        if e["rva"] in seen:
            problems.append(f"{where}: also in {seen[e['rva']]}")
        seen[e["rva"]] = f
        if e["status"] not in tsdb.STATUSES:
            problems.append(f"{where}: unknown status {e['status']!r}")
        if e["status"] != "mapped" and not exists(e["src"]):
            problems.append(f"{where}: src {e['src']} doesn't exist")
        if e.get("verify") not in (None, "match"):
            problems.append(f"{where}: unknown \"verify\" {e['verify']!r} (only \"match\")")
        if e.get("verify") == "match":
            if e["status"] != "verified":
                problems.append(f"{where}: \"verify\": \"match\" on a {e['status']} entry")
            if not isinstance(e.get("matchcheck"), int):
                problems.append(f"{where}: \"verify\": \"match\" without the checker version (\"matchcheck\": N)")
        elif e["status"] == "verified" and not (e.get("test") and exists(e["test"])):
            problems.append(f"{where}: verified without an existing test file ({e['test']!r})")
        if e["status"] == "trace-verified" and not e.get("trace"):
            problems.append(f"{where}: trace-verified without a \"trace\" field (scenarios, traces and call counts)")
        problems += [f"{where}: {p}" for p in absorbed_problems(e, by_rva)]
        if e["rva"] not in funcs:
            # Allowed (Ghidra misses some starts), but worth a look.
            notes.append(f"{where}: not a function start in db/functions.jsonl")
    sources = origcalls.source_files()
    found = origcalls.scan(sources)
    oc_problems, oc_warnings = origcalls.check(found, entries, funcs)
    problems += oc_problems
    notes += oc_warnings
    # A declaration naming an RVA that has a replacement by now, in any group (also when the file's call sites are
    # already warned about above); a note, since the replacement may land after the caller's lane.
    notes += ["stale original_calls: " + s for s in origcalls.stale_lines(entries)]
    # "in_effect": "callers" entries and the trap table. Their verdicts rest on the binary scan of our built host
    # (callers.py); without a current build they would fall back to every hex literal and report hundreds of open
    # references that aren't there, so they are skipped (one line, below) and only the trap table is checked.
    host_skip = host_build_problem()
    if host_skip:
        problems += trap_table_problems()
    else:
        ie_problems, ie_notes = callers.check_status(entries, sources=sources, found=found, funcs=list(funcs.values()))
        problems += ie_problems
        notes += ie_notes
    # Hook rows' site CRCs as the host computes them (a row it would skip: "site at rva 0x... doesn't match").
    problems += hookcrc.check_tables()[1]
    if args.group:
        # Problems tied to no group's files (a missing host build, the trap table) don't fail a group's check.
        problems, others, outside = group_problems(problems, entries, args.group)
        notes, other_notes, _ = group_problems(notes, entries, args.group)
        notes += other_notes + [f"(not tied to a group's files) {p}" for p in others]
        if outside:
            notes.append(f"{outside} problems in other groups' files not shown (--group {args.group})")
    for w in notes:
        print("note: " + w, file=sys.stderr)
    for p in problems:
        print("problem: " + p, file=sys.stderr)
    if host_skip:
        n = sum(1 for _, e in entries if e.get("in_effect") == "callers")
        print(f"skipped: the \"in_effect\": \"callers\" check of {n} entries: {host_skip}; build it (PowerShell: "
              ". .\\tools\\common.ps1; Invoke-Build ts2host) and rerun for that check", file=sys.stderr)
    if not args.check:
        print_totals(funcs, entries)
        summary(funcs, {e["rva"]: e for _, e in entries})
    if args.group:
        # One fixed last line for the group's verdict (lane QD): the notes and problems above can run long.
        sys.stderr.flush()
        print(f"group {args.group}: " + (f"{len(problems)} problem(s)" if problems else "clean")
              + (f" ({len(notes)} note(s) above)" if notes else ""), flush=True)
    sys.exit(1 if problems else 0)


def absorbed_problems(e, by_rva):
    """What is wrong with an "absorbed_by" entry's fields (docs/verification.md): the function's work moved into the
    functions listed, each verified by a difftest, this entry's test. callers.py checks the references."""
    if "absorbed_by" not in e:
        return []
    out = []
    if e["status"] != "reimplemented" or e.get("in_effect") != "callers":
        out.append("\"absorbed_by\" goes on a reimplemented entry with \"in_effect\": \"callers\"")
    if not e["absorbed_by"] or not isinstance(e["absorbed_by"], list):
        out.append("\"absorbed_by\" must list the functions that took its work")
        return out
    for rva in e["absorbed_by"]:
        other = by_rva.get(rva, {})
        if other.get("status") != "verified" or other.get("verify") == "match" or other.get("test") != e.get("test"):
            out.append(f"\"absorbed_by\" {rva} must be verified by a difftest, the entry's test {e.get('test')}")
    return out


def host_build_problem():
    """None when our host build (build/Release/ts2host.exe and its .map) exists and is newer than every src/ and host/
    source, as callers.py's binary scan needs; else why not, in a few words. Status files don't count: the build doesn't
    read them, and a rebuild after only a status change leaves the exe as it was."""
    exe = ourrefs.default_exe()
    rel = exe.relative_to(tsdb.ROOT).as_posix() if exe.is_relative_to(tsdb.ROOT) else str(exe)
    if not exe.exists() or not exe.with_suffix(".map").exists():
        return f"no host build ({rel} or its .map is missing)"
    built = exe.stat().st_mtime
    newer = [p for d in ("src", "host") for p in (tsdb.ROOT / d).rglob("*")
             if p.suffix in (".cpp", ".h", ".c") and p.stat().st_mtime > built]
    if newer:
        first = max(newer, key=lambda p: p.stat().st_mtime).relative_to(tsdb.ROOT).as_posix()
        more = f" and {len(newer) - 1} more" if len(newer) > 1 else ""
        return f"{rel} is older than {first}{more} (src/, host/)"
    return None


def trap_table_problems():
    """The trap table check of callers.check_status alone (it needs no host build)."""
    image = callers.Image()
    rows = callers.trap_rows(callers.Db(binary=False, image=image), image)
    path = tsdb.ROOT / callers.TRAP_TABLE
    if not path.exists() or path.read_text(encoding="utf-8") != callers.trap_table_text(rows):
        return [f"{callers.TRAP_TABLE} is out of date: tools/py -I tools/callers.py --write-traps"]
    return []


def group_problems(problems, entries, group):
    """(the group's problems, problems tied to no group's files, how many belong to other groups). A problem belongs
    to a group when it starts with its status file's name or with one of the src/test files its entries name."""
    def paths(want):
        out = set()
        for f, e in entries:
            if want(f):
                out |= {e[k].replace("\\", "/") for k in ("src", "test") if isinstance(e.get(k), str) and e[k]}
        return out

    mine_file = group + ".jsonl"
    mine = paths(lambda f: f == mine_file)
    theirs = paths(lambda f: f != mine_file) - mine
    files = {f for f, _ in entries}

    def owner(p):
        head = p.split(":", 1)[0].replace("\\", "/")
        if head == mine_file or head in mine or head == f"src/hook/tables/{group}.cpp":
            return "mine"
        if head in files or head in theirs or head.startswith("src/hook/tables/"):
            return "other"
        return "global"

    kinds = [owner(p) for p in problems]
    return ([p for p, k in zip(problems, kinds) if k == "mine"], [p for p, k in zip(problems, kinds) if k == "global"],
            kinds.count("other"))


def print_totals(funcs, entries, per_file=True):
    """The table of statuses: one row per status file (per_file), then the totals in entries and in bytes."""
    shown = [(f, e) for f, e in entries if "absorbed_by" not in e]  # their own line, in summary()
    total = Counter(e["status"] for _, e in shown)
    size = Counter()
    for _, e in shown:
        size[e["status"]] += funcs.get(e["rva"], {}).get("size", 0)
    print(f"{'file':28} " + " ".join(f"{s:>13}" for s in tsdb.STATUSES))
    if per_file:
        for f in sorted({f for f, _ in shown}):
            c = Counter(e["status"] for g, e in shown if g == f)
            print(f"{f:28} " + " ".join(f"{c[s]:13}" for s in tsdb.STATUSES))
    print(f"{'total':28} " + " ".join(f"{total[s]:13}" for s in tsdb.STATUSES))
    print(f"{'bytes of code':28} " + " ".join(f"{size[s]:13}" for s in tsdb.STATUSES))


def tainted(funcs, status, library):
    """Our functions (reimplemented, verified, replaced) that still reach original code: they declare
    original_calls (the only way src/ may call original code, enforced by origcalls.py), or a direct
    callee in the binary is one of ours that does. Callees we don't own are not followed: a replacement
    reproduces those itself (STL, inlined helpers) unless it declares them. Virtual calls aren't call
    edges, so this is a lower bound."""
    ours = {r for r, e in status.items() if e["status"] in tsdb.OURS}
    bad = {r for r in ours if status[r].get("original_calls")}
    changed = True
    while changed:
        changed = False
        for r in ours - bad:
            if any(c in bad for c in funcs.get(r, {}).get("callees", []) if c != r):
                bad.add(r)
                changed = True
    return bad


def class_lists(funcs, status, library, done, limit, group_missing=False):
    """Classes with every virtual (all slots of all vftables, inherited ones included) done: the count line, and with a
    limit (--classes) the complete classes and up to `limit` nearly complete ones. A missing slot that is a folded
    shared_vf_* body (unrelated classes share it) is marked "(folded)": it is verified once by its defining class, and
    hooked only when one class family references it (docs/verification.md)."""
    import json
    rows = []
    for c in map(json.loads, open(tsdb.ROOT / "db/classes.jsonl", encoding="utf-8")):
        slots = {s for vt in c.get("vftables", []) for s in vt.get("entries", []) if s in funcs}
        if not slots - library:
            continue  # pure interfaces (_purecall) and library classes
        todo = slots - done
        rows.append((c["name"], len(slots), todo))
    complete = [r for r in rows if not r[2]]
    print(f"  classes with every virtual done: {len(complete):,} of {len(rows):,} with game code in"
          f" their vtables (constructors aren't in vtables, so not counted yet)")
    if limit is None:
        return
    shared = {n["rva"] for n in tsdb.jsonl("names.jsonl") if n["name"].startswith("shared_vf_")}
    near = sorted((r for r in rows if 0 < len(r[2]) <= 5), key=lambda r: (len(r[2]), -r[1]))
    print(f"  nearly complete (missing slots / all slots; {min(limit, len(near))} of {len(near)} with 1-5 missing,"
          f" --limit N for more; '(folded)': a shared_vf_* body):")
    if group_missing:
        # One line per missing set: classes it would complete, its bytes, the class names (best classes per byte first).
        groups = {}
        for name, n, todo in near:
            groups.setdefault(frozenset(todo), []).append(name)
        nbytes = {k: sum(funcs[r]["size"] for r in k) for k in groups}
        order = sorted(groups, key=lambda k: (-len(groups[k]) / max(nbytes[k], 1), nbytes[k]))
        print(f"  grouped by missing set ({min(limit, len(order))} of {len(order)} sets; classes, bytes):")
        for k in order[:limit]:
            miss = [r + (" (folded)" if r in shared else "") for r in by_address(k)]
            print(f"    {len(groups[k]):>3} cls {nbytes[k]:>5} B  {', '.join(miss)}  :: {'; '.join(groups[k])[:160]}")
        return
    for name, n, todo in near[:limit]:
        miss = [r + (" (folded)" if r in shared else "") for r in by_address(todo)]
        print(f"    {len(todo)}/{n:<4} {name[:70]}  missing {', '.join(miss)}")
    print("  complete: " + ", ".join(sorted(r[0] for r in complete)))


def summary(funcs, status, list_rvas=True):
    """The roadmap's metrics (docs/roadmap.md): bytes, whole classes, the startup path."""
    library = tsdb.library_rvas()
    verified = {r for r, e in status.items() if e["status"] == "verified"}
    with_tail_calls(funcs)
    # Done = verified or library, or a jump stub to one of those.
    done = tsdb.resolve_done(verified | library, funcs)

    def size(rvas):
        return sum(funcs[r]["size"] for r in rvas if r in funcs)

    total, lib = size(funcs), size(library)
    game = total - lib
    print(f"\ngame code (excluding library): {game:,} bytes in {len(funcs) - len(library & set(funcs)):,} functions")
    print(f"  verified: {size(verified - library):,} bytes ({100 * size(verified - library) / game:.2f}%)"
          f"   library: {lib:,} bytes ({len(library):,} functions)")
    third = set()
    sub = tsdb.ROOT / "db/subsystems.jsonl"
    if sub.exists():
        third = {e["rva"] for e in tsdb.jsonl("subsystems.jsonl") if e["subsystem"] == "third-party"} - library
        own = game - size(third)
        print(f"  of which third-party code (db/subsystems.jsonl: open-source builds or dropped, not reimplemented; OpenSSL, the"
              f" HLSL compiler, crashpad...): {size(third):,} bytes; EA's own code: {own:,} bytes,"
              f" verified {size(verified - library - third):,} ({100 * size(verified - library - third) / own:.2f}%)")
    matched = {r for r in verified if status[r].get("verify") == "match"}
    print(f"  verified by byte match (tools/matchcheck.py): {size(matched - library):,} bytes ({len(matched - library):,} functions);"
          f" by differential test: {size(verified - matched - library):,} bytes")
    absorbed = {r for r, e in status.items() if "absorbed_by" in e}
    via_callers = {r for r, e in status.items() if e.get("in_effect") == "callers" and e["status"] in tsdb.OURS} - absorbed
    print(f"  in effect through their callers (not hooked; originals trapped): {len(via_callers)} functions,"
          f" {size(via_callers):,} bytes, of which verified {size(via_callers & verified):,}"
          + (f" ({', '.join(sorted(via_callers))})" if via_callers and list_rvas else ""))
    tests = sorted({status[r]["test"] for r in absorbed})
    print(f"  absorbed by a template of ours (original routines only a replaced reader's data table reaches; done, but not"
          f" verified bytes): {len(absorbed)} functions, {size(absorbed):,} bytes" + (f" ({', '.join(tests)})" if tests else ""))
    print_headline(funcs, status, library, third)
    standalone = verified - tainted(funcs, status, library)
    print(f"  verified, no known original calls (none declared in anything it reaches by direct calls;"
          f" indirect calls and image-data dependencies not fully checked):"
          f" {size(standalone - library):,} bytes ({len(standalone - library):,} functions)")

    class_lists(funcs, status, library, done, limit=None)

    entry, roots = startup_roots()
    line = reachable_line(funcs, status, library, roots, tsdb.import_thunks(funcs), folded=tiny_folded(funcs))
    reach, ldone = line["reach"], line["done"]
    print(f"  reachable from the entry point {entry} and the static initialisers by direct calls: {len(reach):,} game functions,"
          f" {size(reach):,} bytes; done {size(ldone):,} bytes ({100 * size(ldone) / max(1, size(reach)):.2f}%):"
          f" verified {size(line['verified']):,}, replaced {size(line['replaced']):,} (followed only through their"
          f" original_calls), import thunks {size(line['thunks']):,} ({len(line['thunks'])}), tiny folded functions"
          f" called only from verified code {size(line['folded']):,} ({len(line['folded'])}"
          + (f": {', '.join(by_address(line['folded']))}" if line["folded"] and list_rvas else "")
          + f"), jump stubs and tiny leaves {size(line['other']):,}")


def headline(funcs, status, library, third):
    """"No longer needs decompiling" over EA's own code (docs/roadmap.md, Metrics): bytes per band
    (tsdb.HEADLINE_BANDS), and what the figure leaves out."""
    own = set(funcs) - set(library) - set(third)
    bands = dict.fromkeys(tsdb.HEADLINE_BANDS, 0)
    bare = 0
    for r, e in status.items():
        if r in own:
            band = tsdb.headline_band(e)
            if band:
                bands[band] += funcs[r]["size"]
            elif e["status"] == "reimplemented":
                bare += funcs[r]["size"]
    total = sum(funcs[r]["size"] for r in own)
    done = sum(bands.values())
    return {"bands": bands, "done": done, "total": total, "bare_reimplemented": bare, "rest": total - done - bare,
            "library": sum(funcs[r]["size"] for r in funcs if r in library),
            "third_party": sum(funcs[r]["size"] for r in third if r in funcs and r not in library)}


def print_headline(funcs, status, library, third):
    h = headline(funcs, status, library, third)
    b = h["bands"]
    print(f"  no longer needs decompiling (decompilation work remaining, not image independence): {h['done']:,} bytes of"
          f" EA's own {h['total']:,} ({100 * h['done'] / max(1, h['total']):.2f}%): verified {b['verified']:,},"
          f" absorbed {b['absorbed']:,}, replaced {b['replaced']:,}, dead proofs {b['dead']:,} (none recorded in status"
          f" data), trace-verified {b['trace-verified']:,}")
    print(f"    not counted: bare reimplemented {h['bare_reimplemented']:,}; everything else {h['rest']:,} (mapped, no entry,"
          f" deferred, handed on, unreached). Outside the denominator: library {h['library']:,},"
          f" third-party {h['third_party']:,}")


def with_tail_calls(funcs):
    """funcs with the tail-call jump targets (db/tail-calls.jsonl) added to their callees, as the metrics count them."""
    for e in tsdb.jsonl("tail-calls.jsonl"):
        if e["rva"] in funcs:
            funcs[e["rva"]] = dict(funcs[e["rva"]], callees=list(funcs[e["rva"]]["callees"]) + e["targets"])
    return funcs


def startup_roots():
    """(entry, roots): the entry point and the static initialisers (the CRT calls those through its _initterm
    tables, not by direct calls)."""
    import pefile
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    entry = hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    return entry, [entry] + [e["rva"] for e in tsdb.jsonl("static-initializers.jsonl")]


def walk(funcs, roots, declared_only):
    """Every function reachable from roots by direct and tail calls (virtual calls aren't known), library code
    included and followed. funcs must already carry the tail calls (with_tail_calls). From a function in declared_only
    (rva -> status entry) only its declared original_calls are followed: its other original callees no longer run."""
    seen, stack = set(), list(roots)
    while stack:
        r = stack.pop()
        if r in seen or r not in funcs:
            continue
        seen.add(r)
        if r in declared_only:
            stack.extend(hex(int(c, 16)) for c in declared_only[r].get("original_calls", []))
        else:
            stack.extend(c for c in funcs[r]["callees"] if "!" not in c)
    return seen


FOLDED_MAX = 8  # "under 8 bytes" (notes/decisions.md 2026-10-19)


def tiny_folded(funcs, names=None):
    """Folded leaves under FOLDED_MAX bytes: shared_vf_* bodies (db/names.jsonl: one body behind slots of unrelated
    classes) with no callees. They can be neither hooked nor trapped (the other classes still use them), so the
    reachable line counts one as done when every direct call to it comes from verified code (folded_done)."""
    names = tsdb.jsonl("names.jsonl") if names is None else names
    shared = {n["rva"] for n in names if n["name"].startswith("shared_vf_")}
    return {r for r in shared if r in funcs and funcs[r]["size"] < FOLDED_MAX
            and not funcs[r]["callees"] and not funcs[r]["thunk"]}


def folded_done(funcs, folded, verified):
    """The tiny folded functions (tiny_folded) that every direct caller in the image (call or tail call; funcs carries
    the tail calls) is verified code, with at least one caller: a replacement reproduces their effect inline, so once
    all their callers are ours nothing of ours calls them. Calls through vtables aren't edges: those come from the
    other classes' slots, which the line doesn't follow either."""
    callers = {}
    for r, f in funcs.items():
        for c in f["callees"]:
            if c in folded and c != r:
                callers.setdefault(c, set()).add(r)
    return {r for r in folded if callers.get(r) and callers[r] <= verified}


def reachable_line(funcs, status, library, roots, thunks, targets=None, folded=frozenset()):
    """The roadmap's "reachable" line (milestone H): the game functions (library left out) reachable from roots,
    descending from a `replaced` function only through its original_calls (its contract replacement runs instead),
    and which of them are done: verified, replaced, import thunks (`jmp [IAT]`: our exe makes the same DLL calls),
    tiny folded functions whose direct callers are all verified (folded: tiny_folded's set; folded_done), or jump
    stubs to done code and tiny leaves (tsdb.resolve_done). Returns {reach, done, verified, replaced, thunks, folded,
    other} (sets of RVAs; the last five split done)."""
    replaced = {r: e for r, e in status.items() if e["status"] == "replaced"}
    verified = {r for r, e in status.items() if e["status"] == "verified"}
    reach = walk(funcs, roots, replaced) - library
    folded = folded_done(funcs, set(folded), verified)
    done = tsdb.resolve_done(verified | set(replaced) | library | thunks | folded, funcs, targets) & reach
    parts = {"verified": verified & done, "replaced": set(replaced) & done - verified,
             "thunks": thunks & done - verified - set(replaced)}
    parts["folded"] = folded & done - set().union(*parts.values())
    parts["other"] = done - set().union(*parts.values())
    return dict(parts, reach=reach, done=done)


def todo_kind(rva, f, sub, statics, near_library):
    """A rough first cut for grouping (docs/subsystems/startup.md "Milestone H: work packages")."""
    if rva in statics:
        return "static-init"
    if f["namespace"].upper().endswith(".DLL"):
        return "import-thunk"
    if sub.get("subsystem") == "third-party":
        return "third-party"
    if rva in near_library:
        return "near-library"
    return "game"


def best_names(funcs, status):
    """rva -> (name, class) from, in order: the status entry, a Ghidra annotation (export), db/names.jsonl (vtable
    and interface names; shared_vf_* skipped), db/subsystems.jsonl's vtable/ctor basis (class only), Ghidra's name."""
    import re
    names, classes = {}, {}
    for r, f in funcs.items():
        names[r] = f["name"] if f["namespace"] == "Global" else f"{f['namespace']}::{f['name']}"
    for e in tsdb.jsonl("subsystems.jsonl"):
        m = re.match(r"(vtable|ctor):(.+)", e["basis"])
        if m:
            classes[e["rva"]] = m.group(2)
    for n in tsdb.jsonl("names.jsonl"):
        if not n["name"].startswith("shared_vf_"):
            names[n["rva"]] = n["name"]
    for a in tsdb.jsonl("annotations.jsonl"):
        if a.get("kind") in ("function", "name") and a.get("rva") and a.get("name"):
            names[a["rva"]] = f"{a['class']}::{a['name']}" if a.get("class") else a["name"]
    for r, e in status.items():
        names[r] = e["name"]
    for i in tsdb.jsonl("initializers.jsonl"):
        if i.get("class") and i["rva"] not in classes:
            classes[i["rva"]] = i["class"]
    out = {}
    for r in funcs:
        name = names.get(r, "")
        head = re.split(r"[ (]", name, maxsplit=1)[0]  # status names carry descriptions: "A::B (ret 4: x::y ...)"
        cls = head.rsplit("::", 1)[0] if "::" in head else classes.get(r, "")
        out[r] = (name, cls)
    return out


def by_address(rvas):
    return sorted(rvas, key=lambda x: int(x, 16))


class LineContext:
    """Everything the "reachable" line knows about each function, loaded once (--reachable-todo, --rva)."""

    def __init__(self):
        self.funcs = funcs = with_tail_calls({f["rva"]: f for f in tsdb.functions()})
        self.files = {e["rva"]: f for f, e in tsdb.status_entries()}
        self.status = status = {e["rva"]: e for _, e in tsdb.status_entries()}
        self.library = library = tsdb.library_rvas()
        self.thunks = tsdb.import_thunks(funcs)
        self.entry, roots = startup_roots()
        self.line = reachable_line(funcs, status, library, roots, self.thunks, folded=tiny_folded(funcs))
        self.reach, self.done = self.line["reach"], self.line["done"]
        replaced = {r: e for r, e in status.items() if e["status"] == "replaced"}
        self.from_entry = walk(funcs, [self.entry], replaced)
        self.live = walk(funcs, roots, {r: e for r, e in status.items() if e["status"] in tsdb.OURS}) - library
        self.todo = self.reach - self.done
        self.sub = {e["rva"]: e for e in tsdb.jsonl("subsystems.jsonl")}
        self.statics = {e["rva"] for e in tsdb.jsonl("static-initializers.jsonl")}
        # Unmatched code linked among library code (EA's CRT, STL instantiations): at least 3 of the 5 functions on
        # each side by address are library.
        self.order = order = by_address(funcs)
        self.pos = pos = {r: i for i, r in enumerate(order)}
        self.near_library = {r for r in self.todo
                             if sum(o in library for o in order[max(0, pos[r] - 5):pos[r]]) >= 3
                             and sum(o in library for o in order[pos[r] + 1:pos[r] + 6]) >= 3}
        # Callers among everything the line reaches, library code included (CRT callbacks); a replaced function calls
        # only its original_calls.
        self.callers, self.all_callers = {}, {}
        for r in walk(funcs, roots, replaced):
            out = replaced[r].get("original_calls", []) if r in replaced else funcs[r]["callees"]
            for c in set(out):
                if c != r:
                    self.callers.setdefault(hex(int(c, 16)) if "!" not in c else c, set()).add(r)
        for r, f in funcs.items():
            for c in set(f["callees"]):
                if c != r:
                    self.all_callers.setdefault(c, set()).add(r)
        self.named = best_names(funcs, status)

    def row(self, r):
        """The facts for one function start (the --reachable-todo --json row)."""
        cs, s, f = self.callers.get(r, set()), self.sub.get(r, {}), self.funcs[r]
        return {
            "rva": r, "size": f["size"], "name": self.named[r][0], "class": self.named[r][1],
            "subsystem": s.get("subsystem", ""), "basis": s.get("basis", ""), "guess": s.get("guess", ""),
            "status": self.status[r]["status"] if r in self.status else "",
            "kind": todo_kind(r, f, s, self.statics, self.near_library),
            "root": "entry" if r == self.entry else "static-init" if r in self.statics else "",
            "from_entry": r in self.from_entry, "live": r in self.live,
            "callers_done": by_address(cs & self.done),
            "callers_todo": by_address(cs & self.todo),
            "callers_library": by_address(cs & self.library),
            "callees_todo": by_address({c for c in f["callees"] if c in self.todo and c != r}),
        }

    def containing(self, rva):
        """The function start at or before rva whose body covers it, or None."""
        import bisect
        i = bisect.bisect_right([int(r, 16) for r in self.order], int(rva, 16)) - 1
        if i >= 0:
            r = self.order[i]
            if int(rva, 16) < int(r, 16) + self.funcs[r]["size"]:
                return r
        return None


def rva_report(rvas, as_json):
    """--rva: for each RVA its status entry (group, status, original_calls, in_effect), name, class, size, and the
    reachable line's view of it (reached, done and why, live, kind, callers). Exit 1 if one isn't a function start."""
    import json
    ctx = LineContext()
    bad = 0
    for raw in rvas:
        try:
            r = hex(int(raw, 16))
        except ValueError:
            print(f"{raw}: not an RVA")
            bad += 1
            continue
        if r not in ctx.funcs:
            inside = ctx.containing(r)
            print(f"{r}: not a function start" + (f" (inside {inside} {ctx.named[inside][0]}, +{int(r, 16) - int(inside, 16):#x})"
                                                   if inside else ""))
            bad += 1
            continue
        row = ctx.row(r)
        e = ctx.status.get(r, {})
        line = ctx.line
        why = ("verified" if r in line["verified"] else "replaced" if r in line["replaced"] else
               "import thunk" if r in line["thunks"] else
               "tiny folded function, every direct caller verified" if r in line["folded"] else
               "jump stub or tiny leaf" if r in line["other"] else "")
        row.update({
            "group": ctx.files.get(r, "").removesuffix(".jsonl"), "verify": e.get("verify", ""),
            "original_calls": e.get("original_calls", []), "in_effect": e.get("in_effect", "hook" if e else ""),
            "library": r in ctx.library, "import_thunk": r in ctx.thunks, "reached": r in ctx.reach,
            "line_done": why, "callers_all": len(ctx.all_callers.get(r, ())),
        })
        if as_json:
            print(json.dumps(row))
            continue
        print(f"{r} {row['name']}  ({row['size']} bytes, class {row['class'] or '-'}, subsystem {row['subsystem'] or '-'}"
              f"{' (guess ' + row['guess'] + ')' if row['guess'] else ''})")
        print(f"  status {row['status'] or '-'}" + (f" ({row['verify']})" if row["verify"] else "")
              + (f" in db/status/{row['group']}.jsonl, in effect: {row['in_effect']}" if row["group"] else "")
              + (", library code" if row["library"] else "") + (", import thunk" if row["import_thunk"] else ""))
        if row["original_calls"]:
            print(f"  original_calls: {', '.join(row['original_calls'])}")
        if row["library"]:
            print("  reachable line: library code (left out of the line)")
        elif not row["reached"]:
            print(f"  reachable line: not reached by direct calls ({row['callers_all']} callers in the image)")
        else:
            print(f"  reachable line: reached{' from the entry point' if row['from_entry'] else ' (static initialisers only)'}"
                  f"{', root ' + row['root'] if row['root'] else ''}; {'done: ' + why if why else 'not done, kind ' + row['kind']}"
                  f"; {'live' if row['live'] else 'not live'} (still reached under our replacements)")
            def some(rvas):
                return f"{len(rvas)}" + (f" ({', '.join(rvas[:8])}{', ...' if len(rvas) > 8 else ''})" if rvas else "")
            print(f"  callers on the line: done {some(row['callers_done'])}, not done {some(row['callers_todo'])},"
                  f" library {len(row['callers_library'])}; not-done callees: {some(row['callees_todo'])}")
    return 1 if bad else 0


def reachable_todo(as_json, live_only=False):
    """The reachable game functions the "reachable" line doesn't count as done, largest first (milestone H,
    docs/subsystems/startup.md "Milestone H: work packages"): size, name, class, subsystem, status, and which
    reachable functions call it, split into done and not done."""
    import json
    ctx = LineContext()
    funcs = ctx.funcs
    rows = [ctx.row(r) for r in sorted(ctx.todo, key=lambda r: (-funcs[r]["size"], int(r, 16)))]
    reach = ctx.reach
    if live_only:
        rows = [row for row in rows if row["live"]]
    if as_json:
        for row in rows:
            print(json.dumps(row))
        return
    print(f"{'rva':>9} {'size':>6} {'status':13} {'kind':12} {'subsystem':15} {'class':24} name"
          f"  [callers done | not done | library] (live: still reached under our replacements)")
    for row in rows:
        print(f"{row['rva']:>9} {row['size']:6} {row['status'] or '-':13} {row['kind']:12} {(row['guess'] or row['subsystem'])[:14] + ('?' if row['guess'] else ''):15}"
              f" {row['class'][:24]:24} {row['name'][:56]}  [{len(row['callers_done'])} | {len(row['callers_todo'])}"
              f" | {len(row['callers_library'])}]{' live' if row['live'] else ''}")
    total = sum(r["size"] for r in rows)
    print(f"{len(rows):,} functions, {total:,} bytes not done{' and still reached' if live_only else ''}"
          f" (of {len(reach):,} reachable game functions,"
          f" {sum(funcs[r]['size'] for r in reach):,} bytes)")
    by_kind = Counter()
    for row in rows:
        by_kind[row["kind"]] += row["size"]
    print("  by kind: " + ", ".join(f"{k} {v:,}" for k, v in by_kind.most_common()))
    lv = [row for row in rows if row["live"]]
    print(f"  still reached under our replacements (only declared original_calls followed out of our functions):"
          f" {len(lv):,} functions, {sum(r['size'] for r in lv):,} bytes")


if __name__ == "__main__":
    main()
