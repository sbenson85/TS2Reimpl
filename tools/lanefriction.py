"""Where lanes lose tool calls: failed commands, tool crashes, build errors, refusals, long waits, retries.

Reads the lane (subagent) transcripts that tools/lanecost.py reads, so it measures friction from what happened
rather than from what a lane remembers to report (lanes also write notes/feedback/<lane>.md; docs/lane/brief.md).

    .venv/Scripts/python -I tools/lanefriction.py [--since 2026-10-15] [--lane BX] [--top 15]

Per lane: tool calls, and how many ended in each kind of trouble; then the most common trouble across lanes,
with a short normalised snippet of each (paths, numbers and hex shortened so repeats group together).
"""
import argparse
import collections
import datetime
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lanecost  # noqa: E402

WAIT_SECONDS = 240  # a tool call that took longer than this (CLAUDE.md asks for waits under 4 minutes)


def text_of(content):
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(b.get("text", "") for b in content if isinstance(b, dict))
    return ""


def norm(s):
    s = re.sub(r"[A-Za-z]:[\\/][^\s'\"]*|/c/[^\s'\"]*", "<path>", s)
    s = re.sub(r"0x[0-9a-fA-F]+", "0x…", s)
    s = re.sub(r"\d+", "N", s)
    return re.sub(r"\s+", " ", s).strip()[:110]


def tool_of(name, inp):
    """A short label for a call: the project tool it ran if any, else the harness tool."""
    cmd = (inp or {}).get("command") or ""
    m = re.search(r"tools[\\/]([\w-]+\.(?:py|ps1))", cmd)
    if m:
        return m.group(1)
    if re.search(r"Invoke-Build|cmake --build|msbuild", cmd, re.I):
        return "build"
    if "difftest.exe" in cmd:
        return "difftest.exe"
    return name


def classify(tool, out, is_error):
    """-> (kind, snippet) or None."""
    low = out.lower()
    if "traceback (most recent call last)" in low:
        last = [l for l in out.splitlines() if l.strip()][-1] if out.strip() else ""
        return "tool crash (Python traceback)", f"{tool}: {last}"
    if re.search(r"\berror (C|LNK)\d{4}", out):
        line = next(l for l in out.splitlines() if re.search(r"\berror (C|LNK)\d{4}", l))
        return "build error", re.sub(r".*\berror ", "error ", line)
    if re.search(r"\b[1-9]\d* failed\b", out) and "run," in out:
        return "difftest failures", next((l for l in out.splitlines() if "[FAIL" in l), "failed")
    if is_error and re.search(r"denied|not allowed|refus|blocked|permission", low):
        return "refused (permission / guard / sandbox)", out.splitlines()[0] if out else ""
    if is_error:
        first = next((l for l in out.splitlines() if l.strip() and not l.startswith("Exit code")), out[:100])
        return "failed command", f"{tool}: {first}"
    return None


def lane_friction(path):
    uses, starts = {}, {}
    for r in lanecost.records(path):
        ts = lanecost.timestamp(r)
        for b in (r.get("message") or {}).get("content") or []:
            if not isinstance(b, dict):
                continue
            if b.get("type") == "tool_use":
                uses[b["id"]] = (b.get("name"), b.get("input") or {})
                starts[b["id"]] = ts
            elif b.get("type") == "tool_result":
                yield b.get("tool_use_id"), uses.get(b.get("tool_use_id"), ("?", {})), text_of(b.get("content")), \
                    bool(b.get("is_error")), starts.get(b.get("tool_use_id")), ts


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--since", help="only lanes started on or after this date (YYYY-MM-DD)")
    ap.add_argument("--lane", help="only this lane (letter in the lane's description)")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()
    since = datetime.datetime.fromisoformat(args.since).replace(tzinfo=datetime.timezone.utc) if args.since else None
    overall = collections.Counter()
    by_tool = collections.Counter()
    print(f"{'lane':42} {'calls':>5} {'trouble':>7} {'crash':>5} {'build':>5} {'fail':>5} {'refused':>7} "
          f"{'wait>4m':>7} {'retry':>5}")
    for session in sorted(lanecost.transcript_dir().glob("*.jsonl")):
        sub = session.with_suffix("") / "subagents"
        for path in sorted(sub.glob("*.jsonl")) if sub.is_dir() else []:
            meta_path = path.with_suffix(".meta.json")
            meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
            desc = meta.get("description", path.stem)
            letter = lanecost.lane_letter(desc)
            if args.lane and letter != args.lane.upper():
                continue
            rows = list(lane_friction(path))
            if not rows or (since and rows[0][4] and rows[0][4] < since):
                continue
            kinds = collections.Counter()
            seen = collections.Counter()
            waits = retries = 0
            for _, (name, inp), out, err, t0, t1 in rows:
                tool = tool_of(name, inp)
                key = json.dumps(inp, sort_keys=True)[:400]
                seen[key] += 1
                if seen[key] == 2 and name in ("Bash", "PowerShell"):
                    retries += 1
                if t0 and t1 and (t1 - t0).total_seconds() > WAIT_SECONDS:
                    waits += 1
                c = classify(tool, out, err)
                if c:
                    kinds[c[0]] += 1
                    overall[(c[0], norm(c[1]))] += 1
                    by_tool[(tool, c[0])] += 1
            short = {"tool crash (Python traceback)": "crash", "build error": "build", "difftest failures": "fail",
                     "refused (permission / guard / sandbox)": "refused", "failed command": "other"}
            print(f"{desc[:42]:42} {len(rows):5} {sum(kinds.values()):7} {kinds[next(k for k, v in short.items() if v == 'crash')]:5} "
                  f"{kinds['build error']:5} {kinds['difftest failures']:5} "
                  f"{kinds['refused (permission / guard / sandbox)']:7} {waits:7} {retries:5}")
    print("\nby tool (calls that ended in trouble):")
    for (tool, kind), n in by_tool.most_common(args.top):
        print(f"  {n:4}  {tool:24} {kind}")
    print("\nmost common trouble:")
    for (kind, snip), n in overall.most_common(args.top):
        print(f"  {n:4}  {kind}: {snip}")
    print("\nBuild errors and failing difftests are partly normal work (writing code); tool crashes, refusals, waits "
          "and retries are the friction to look at.")


if __name__ == "__main__":
    main()
