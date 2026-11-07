"""What each helper lane cost, and what it delivered, from Claude Code's session transcripts.

Cost is at API list prices (the closest public proxy for plan quota): cache reads, cache writes,
uncached input and output, per call. Model and effort come from the coordinator's Agent call
(effort "default" when it set none). Outcome comes from the merge commit on main whose message
names the lane's branch (`Lane-branch: worktree-agent-...`, docs/orchestrator.md): functions whose
status became verified / trace-verified / reimplemented, and their bytes. Older merges are matched by
lane letter. Issues come from the coordinator's records (notes/lane-records.jsonl, format in
docs/archive/lane-experiment.md), joined by branch.

Usage: python -I tools/lanecost.py [--since 2026-10-15] [--lanes]   per-arm summary (and per-lane rows)
       python -I tools/lanecost.py --pick LABEL                     the next lane's model/effort arm
Picks come from shuffled blocks of four (each arm once per block, in random order), logged in
docs/archive/lane-picks.jsonl so the next pick knows where the block stands.
"""
import argparse
import collections
import datetime
import json
import random
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

# The arms of the model/effort experiment (docs/orchestrator.md).
ARMS = [("sonnet", "high"), ("sonnet", "medium"), ("opus", "low"), ("opus", "medium")]

# $/MTok (input, output, cache read), API list prices 2026-10. Cache writes: 1.25x input (5 min
# TTL) or 2x (1 h). Haiku 5.5 charges more above 100k-token prompts (0.5 / 2.5, handled in cost()).
PRICES = {"fable": (10.0, 50.0, 0.25), "opus": (4.0, 20.0, 0.20), "sonnet": (2.0, 10.0, 0.20),
          "haiku": (0.10, 0.50, 0.01)}
DONE = ("verified", "trace-verified", "replaced")


def family(model):
    return next((k for k in PRICES if k in (model or "")), None)


def context(u):
    return sum(u.get(k, 0) or 0 for k in ("input_tokens", "cache_creation_input_tokens", "cache_read_input_tokens"))


def cost(model, u):
    """(read, write, input, output) dollars for one call."""
    fam = family(model)
    if not fam:
        return (0, 0, 0, 0)
    pi, po, pr = PRICES[fam]
    if fam == "haiku" and context(u) > 100_000:
        pi, po, pr = 0.5, 2.5, 0.05
    cc = u.get("cache_creation") or {}
    w1h, w5m = cc.get("ephemeral_1h_input_tokens", 0) or 0, cc.get("ephemeral_5m_input_tokens", 0) or 0
    if w1h + w5m != (u.get("cache_creation_input_tokens", 0) or 0):
        w1h, w5m = u.get("cache_creation_input_tokens", 0) or 0, 0
    return ((u.get("cache_read_input_tokens", 0) or 0) * pr / 1e6, (w1h * 2 + w5m * 1.25) * pi / 1e6,
            (u.get("input_tokens", 0) or 0) * pi / 1e6, (u.get("output_tokens", 0) or 0) * po / 1e6)


def records(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            try:
                yield json.loads(line)
            except ValueError:
                pass


def timestamp(r):
    t = r.get("timestamp")
    return datetime.datetime.fromisoformat(t.replace("Z", "+00:00")) if t else None


def transcript_dir():
    return Path.home() / ".claude" / "projects" / re.sub(r"[:\\/]", "-", str(tsdb.main_root()))


def lane_stats(path):
    """Cost and shape of one subagent transcript (calls deduplicated by message id)."""
    calls, first, last, compactions = {}, None, None, 0
    order = []
    for r in records(path):
        ts = timestamp(r)
        if ts:
            first, last = first or ts, ts
        if r.get("type") == "system" and r.get("subtype") == "compact_boundary":
            compactions += 1
        m = r.get("message") or {}
        if r.get("type") != "assistant" or not m.get("usage") or m.get("model") == "<synthetic>":
            continue
        if m["id"] not in calls:
            order.append(m["id"])
        calls[m["id"]] = (m.get("model"), m["usage"], ts)
    total = [0.0] * 4
    models = collections.Counter()
    peak = rewrites = rewrite_cost = 0
    prev_ts = None
    for mid in order:
        model, u, ts = calls[mid]
        c = cost(model, u)
        total = [a + b for a, b in zip(total, c)]
        models[family(model)] += 1
        size = context(u)
        peak = max(peak, size)
        written = u.get("cache_creation_input_tokens", 0) or 0
        # A full rewrite after a pause: the cache expired while the lane waited.
        if prev_ts and ts and written > 50_000 and written > size / 2 and (ts - prev_ts).total_seconds() > 300:
            rewrites += 1
            rewrite_cost += c[1]
        prev_ts = ts
    return {"calls": len(order), "read": total[0], "write": total[1], "input": total[2], "output": total[3],
            "cost": sum(total), "model": models.most_common(1)[0][0] if models else None, "peak": peak,
            "rewrites": rewrites, "rewrite_cost": rewrite_cost, "compactions": compactions,
            "start": first, "hours": (last - first).total_seconds() / 3600 if first and last else 0}


def agent_calls(session):
    """The Agent tool inputs in a coordinator transcript, by tool-use id."""
    out = {}
    for r in records(session):
        if r.get("type") != "assistant":
            continue
        for b in (r.get("message") or {}).get("content") or []:
            if isinstance(b, dict) and b.get("type") == "tool_use" and b.get("name") == "Agent":
                out[b["id"]] = b.get("input") or {}
    return out


def git(*args):
    return subprocess.run(["git", "-C", str(tsdb.main_root()), *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace").stdout


def status_at(rev, names):
    """{rva: status} from the named db/status/*.jsonl files at a revision."""
    out = {}
    for name in names:
        for line in git("show", f"{rev}:{name}").splitlines():
            try:
                e = json.loads(line)
                out[e["rva"]] = e["status"]
            except (ValueError, KeyError):
                pass
    return out


def lane_letter(text):
    m = re.match(r"(?:Merge )?lane ([A-Z]+\d*)\b", text, re.I)
    return m.group(1).upper() if m else None


def outcomes(sizes):
    """Outcomes of merges made on main: ({branch: outcome}, {lane letter: outcome}). Merges name
    the lane's branch (`Lane-branch:` line); older ones only say "Merge lane X: ...", so those are
    also keyed by the lane letter (several merges of one letter add up)."""
    by_branch, by_letter = {}, {}
    log = git("log", "main", "--first-parent", "--merges", "--format=%H%x00%B%x01")
    for entry in log.split("\x01"):
        if "\x00" not in entry:
            continue
        sha, body = entry.strip().split("\x00", 1)
        branches = re.findall(r"worktree-agent-[0-9a-f]+", body)
        letter = lane_letter(body)
        if not branches and not letter:
            continue
        changed = git("diff", "--name-only", f"{sha}^1", sha, "--", "db/status/").split()
        before, after = status_at(f"{sha}^1", changed), status_at(sha, changed)
        o = {"merges": 1, "done": 0, "done_bytes": 0, "reimplemented": 0, "reimplemented_bytes": 0}
        for rva, st in after.items():
            if before.get(rva) != st and st in DONE:
                o["done"] += 1
                o["done_bytes"] += sizes.get(rva, 0)
            elif before.get(rva) != st and st == "reimplemented":
                o["reimplemented"] += 1
                o["reimplemented_bytes"] += sizes.get(rva, 0)
        for b in branches:
            by_branch[b] = o
        if letter and not branches:
            prev = by_letter.get(letter)
            by_letter[letter] = {k: o[k] + (prev or {}).get(k, 0) for k in o}
    return by_branch, by_letter


def pick(label):
    """Draws the next arm from the current block and logs it to docs/archive/lane-picks.jsonl."""
    path = tsdb.main_root() / "docs/archive/lane-picks.jsonl"
    picks = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()] \
        if path.exists() else []
    block = picks[len(picks) - len(picks) % len(ARMS):]
    used = {(p["model"], p["effort"]) for p in block}
    model, effort = random.SystemRandom().choice([a for a in ARMS if a not in used])
    entry = {"date": datetime.date.today().isoformat(), "label": label, "model": model, "effort": effort}
    with path.open("a", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(entry) + "\n")
    print(f"model: {model}  effort: {effort}  (block position {len(block) + 1} of {len(ARMS)})")


def lane_records():
    """{branch: [record, ...]} from notes/lane-records.jsonl."""
    out = collections.defaultdict(list)
    path = tsdb.main_root() / "notes/lane-records.jsonl"
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                r = json.loads(line)
                out[r.get("branch")].append(r)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--since", help="only lanes started on or after this date (YYYY-MM-DD)")
    ap.add_argument("--lanes", action="store_true", help="also list each lane")
    ap.add_argument("--pick", metavar="LABEL", help="draw and log the arm for the next lane (LABEL: lane letter or task)")
    args = ap.parse_args()
    if args.pick:
        pick(args.pick)
        return

    since = datetime.datetime.fromisoformat(args.since).replace(tzinfo=datetime.timezone.utc) if args.since else None
    sizes = {f["rva"]: f["size"] for f in tsdb.functions()}
    merged, merged_by_letter = outcomes(sizes)
    recorded = lane_records()
    lanes = []
    for session in sorted(transcript_dir().glob("*.jsonl")):
        sub = session.with_suffix("") / "subagents"
        if not sub.is_dir():
            continue
        spawns = agent_calls(session)
        for path in sorted(sub.glob("*.jsonl")):
            meta_path = path.with_suffix(".meta.json")
            meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
            s = lane_stats(path)
            if not s["calls"] or (since and s["start"] and s["start"] < since):
                continue
            spawn = spawns.get(meta.get("toolUseId"), {})
            s["arm"] = (family(spawn.get("model") or meta.get("model")) or s["model"], spawn.get("effort") or "default")
            s["desc"] = meta.get("description", path.stem)
            s["outcome"] = merged.get(meta.get("worktreeBranch"))
            s["letter"] = lane_letter(s["desc"])
            recs = recorded.get(meta.get("worktreeBranch"), [])
            s["recorded"] = bool(recs)
            s["issues"] = [i for r in recs for i in r.get("issues", [])]
            s["judged"] = " ".join(f"{k}={r[k]}" for r in recs for k in ("difficulty", "quality", "review") if r.get(k))
            lanes.append(s)
    # Older merges name only the lane letter: use them where exactly one lane had that letter.
    letters = collections.Counter(s["letter"] for s in lanes if s["letter"])
    for s in lanes:
        if not s["outcome"] and s["letter"] and letters[s["letter"]] == 1:
            s["outcome"] = merged_by_letter.get(s["letter"])

    if args.lanes:
        print(f"{'lane':40} {'arm':16} {'calls':>5} {'peak':>5} {'$':>6} {'expiry$':>7} {'done':>4} {'KB':>5} {'reimpl':>6} {'rKB':>6} {'issues':>6}")
        for s in sorted(lanes, key=lambda s: s["start"] or datetime.datetime.min.replace(tzinfo=datetime.timezone.utc)):
            o = s["outcome"]
            print(f"{s['desc'][:40]:40} {'/'.join(s['arm']):16} {s['calls']:5} {s['peak'] // 1000:4}k {s['cost']:6.1f} "
                  f"{s['rewrite_cost']:7.1f} {o['done'] if o else '-':>4} {o['done_bytes'] / 1024 if o else 0:5.1f} {o['reimplemented'] if o else '-':>6} {o['reimplemented_bytes'] / 1024 if o else 0:6.1f} {len(s['issues']) if s['recorded'] else '-':>6}")
            if s["judged"]:
                print(f"    {s['judged']}")
            for i in s["issues"]:
                print(f"    {i}")
        print()
    arms = collections.defaultdict(list)
    for s in lanes:
        arms[s["arm"]].append(s)
    print(f"{'arm':16} {'lanes':>5} {'$':>7} {'$/lane':>6} {'expiry$':>7} {'w/outcome':>9} {'done':>5} {'KB':>6} "
          f"{'$/done':>6} {'$/KB':>6} {'rKB':>6} {'$/allKB':>7} {'recorded':>8} {'issues':>6}")
    for arm, ls in sorted(arms.items(), key=lambda kv: -sum(s["cost"] for s in kv[1])):
        total = sum(s["cost"] for s in ls)
        known = [s for s in ls if s["outcome"]]
        kcost = sum(s["cost"] for s in known)
        done = sum(s["outcome"]["done"] for s in known)
        kb = sum(s["outcome"]["done_bytes"] for s in known) / 1024
        rkb = sum(s["outcome"]["reimplemented_bytes"] for s in known) / 1024
        print(f"{'/'.join(arm):16} {len(ls):5} {total:7.0f} {total / len(ls):6.1f} {sum(s['rewrite_cost'] for s in ls):7.0f} "
              f"{len(known):9} {done:5} {kb:6.1f} {kcost / done if done else 0:6.2f} {kcost / kb if kb else 0:6.2f} {rkb:6.1f} {kcost / (kb + rkb) if kb + rkb else 0:7.2f} "
              f"{sum(s['recorded'] for s in ls):8} {sum(len(s['issues']) for s in ls):6}")
    print("\n$ at API list prices; expiry$ = cache rewrites after waits over 5 min; done = verified, "
          "trace-verified or replaced in the lane's merge;\n"
          "reimpl/rKB = left reimplemented, count and KB; $/allKB counts both (simulation lanes are promoted to trace-verified later, outside the merge).\n"
          "Outcomes come from the merge naming the lane's branch (Lane-branch line) or, for older merges, its letter.\n"
          "recorded/issues = lanes with a line in notes/lane-records.jsonl, and the issues listed there.")


if __name__ == "__main__":
    main()
