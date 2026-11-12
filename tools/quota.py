#!/usr/bin/env python3
"""Weekly and 5-hour subscription usage, the pacing line, and a history of snapshots.

    tools/py -I tools/quota.py              # usage now (cached up to 3 min), the pacing line and the lane verdict
    tools/py -I tools/quota.py --json       # the same as one JSON object
    tools/py -I tools/quota.py --history 20 # the last 20 snapshots (or --since 2026-10-26)

Reads the claude.ai login token Claude Code keeps in ~/.claude/.credentials.json and queries the usage endpoint
behind Claude Code's /usage (api.anthropic.com/api/oauth/usage). The endpoint is undocumented and rate-limited, so:
a snapshot younger than --max-age seconds (default 180) is reused, a failed request is cached the same way, and the
token is never refreshed here (Claude Code refreshes it; refreshing it ourselves would rotate the refresh token
under Claude Code). With no usable token or a failed request the last snapshot is shown with its age.

Every request lands in local/quota.sqlite (main checkout): time, both windows' % and reset time, the raw response
and any error, for pacing and later analysis (join on time with tools/lanecost.py data).

Pacing rule (memory quota-pacing, 2026-11-02): weekly % <= 98 x (elapsed share of the week) + 2. At or over the
line: one lane; under it: two (when there is non-conflicting work); at 97% or more: no new lanes. Near the cutoff a
lane starts only if weekly % + its expected cost (about 2 points for a large Opus lane, 1 for Sonnet) stays <= 98.5,
unless the reset comes before it would finish. The week starts 7 days before the weekly window's reset time.
"""
import argparse
import datetime as dt
import json
import sqlite3
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

URL = "https://api.anthropic.com/api/oauth/usage"
CREDS = Path.home() / ".claude" / ".credentials.json"
WEEK_S = 7 * 24 * 3600

SCHEMA = """
create table if not exists snapshots (
    ts          real not null,     -- unix seconds of the request
    five_hour   real,              -- % used of the 5-hour window
    five_resets text,              -- ISO time the 5-hour window resets
    seven_day   real,              -- % used of the weekly window
    week_resets text,              -- ISO time the weekly window resets
    error       text,              -- set when the request failed (the other fields are then null)
    raw         text               -- the response body
);
create index if not exists snapshots_ts on snapshots(ts);
"""


def db():
    path = tsdb.main_root() / "local" / "quota.sqlite"
    path.parent.mkdir(exist_ok=True)
    con = sqlite3.connect(path)
    con.row_factory = sqlite3.Row
    con.executescript(SCHEMA)
    return con


def token():
    """The access token, or (None, reason)."""
    try:
        oauth = json.loads(CREDS.read_text())["claudeAiOauth"]
    except (OSError, KeyError, ValueError) as e:
        return None, f"no token in {CREDS}: {e.__class__.__name__}"
    if oauth.get("expiresAt", 0) / 1000 < time.time() + 30:
        return None, "token expired (Claude Code refreshes it on its next request)"
    return oauth["accessToken"], None


def fetch(con):
    now = time.time()
    tok, why = token()
    row = dict(ts=now, five_hour=None, five_resets=None, seven_day=None, week_resets=None, error=why, raw=None)
    if tok:
        req = urllib.request.Request(URL, headers={"Authorization": "Bearer " + tok,
                                                   "anthropic-beta": "oauth-2025-04-20",
                                                   "User-Agent": "ts2reimpl-quota/1"})
        try:
            with urllib.request.urlopen(req, timeout=20) as f:
                body = f.read().decode()
            data = json.loads(body)
            five, week = data.get("five_hour") or {}, data.get("seven_day") or {}
            row.update(five_hour=five.get("utilization"), five_resets=five.get("resets_at"),
                       seven_day=week.get("utilization"), week_resets=week.get("resets_at"), raw=body)
            if row["seven_day"] is None:
                row["error"] = "response has no seven_day window"
        except urllib.error.HTTPError as e:
            row["error"] = f"HTTP {e.code} {e.reason}"
        except (urllib.error.URLError, OSError, ValueError) as e:
            row["error"] = f"{e.__class__.__name__}: {e}"
    con.execute("insert into snapshots values (:ts, :five_hour, :five_resets, :seven_day, :week_resets, :error, :raw)",
                row)
    con.commit()
    return row


def latest(con, ok_only):
    q = "select * from snapshots" + (" where error is null" if ok_only else "") + " order by ts desc limit 1"
    r = con.execute(q).fetchone()
    return dict(r) if r else None


def iso(s):
    return dt.datetime.fromisoformat(s.replace("Z", "+00:00"))


def pacing(snap, now):
    reset = iso(snap["week_resets"]).timestamp()
    share = min(max((now - (reset - WEEK_S)) / WEEK_S, 0.0), 1.0)
    line = 98 * share + 2
    used = snap["seven_day"]
    lanes = 0 if used >= 97 else (1 if used >= line else 2)
    return dict(elapsed_share=round(share, 4), line=round(line, 1), ahead_by=round(used - line, 1), lanes=lanes,
                headroom=round(98.5 - used, 1), hours_to_reset=round(max(reset - now, 0) / 3600, 1))


def utc(ts):
    return dt.datetime.fromtimestamp(round(ts / 60) * 60, dt.timezone.utc).strftime("%Y-%m-%d %H:%MZ")


def scoped(raw):
    """The model-scoped weekly limits in a raw answer (Fable's since 2026-11-09): [(name, % of its own allowance, % of
    the weekly quota used, its allowance as % of the weekly quota)]. The first has the finest steps the endpoint gives:
    1% of a 50% allowance is half a weekly point."""
    try:
        d = json.loads(raw) if raw else {}
    except ValueError:
        return []
    limits = d.get("limits") or []
    out = []
    for share in d.get("weekly_scoped_shares") or []:
        i = share.get("limit_index")
        lim = limits[i] if isinstance(i, int) and i < len(limits) else {}
        name = (((lim.get("scope") or {}).get("model") or {}).get("display_name")) or "scoped"
        out.append((name, lim.get("percent"), share.get("used_percent_of_weekly"), share.get("allowance_percent_of_weekly")))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--max-age", type=float, default=180, help="reuse a snapshot (or failure) this recent, seconds")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--history", type=int, metavar="N", help="print the last N snapshots and exit")
    ap.add_argument("--since", help="with --history: snapshots from this UTC date/time on")
    a = ap.parse_args()
    con = db()

    if a.history is not None or a.since:
        q, args = "select * from snapshots", []
        if a.since:
            q += " where ts >= ?"
            since = iso(a.since)
            args.append((since if since.tzinfo else since.replace(tzinfo=dt.timezone.utc)).timestamp())
        rows = con.execute(q + " order by ts desc limit ?", args + [a.history or 10 ** 9]).fetchall()
        for r in reversed(rows):
            print(f"{utc(r['ts'])}  week {r['seven_day']!s:>5}%  5h {r['five_hour']!s:>5}%" +
                  "".join(f"  {n} {own}% of its {cap}% (= {used} weekly)" for n, own, used, cap in scoped(r["raw"])) +
                  (f"  error: {r['error']}" if r["error"] else ""))
        return 0

    now = time.time()
    last = latest(con, ok_only=False)
    snap = last if last and now - last["ts"] < a.max_age else fetch(con)
    cached = snap is last
    if snap["error"]:
        err, snap = snap["error"], latest(con, ok_only=True)
    else:
        err = None
    if not snap:
        print(f"quota: no usage data ({err})", file=sys.stderr)
        return 2

    out = dict(time=utc(snap["ts"]), age_s=round(now - snap["ts"]), cached=cached and not err, error=err,
               weekly=snap["seven_day"], weekly_resets=snap["week_resets"],
               five_hour=snap["five_hour"], five_hour_resets=snap["five_resets"], **pacing(snap, now))
    if a.json:
        print(json.dumps(out))
        return 0
    stale = f" (STALE, {out['age_s'] // 60} min old: {err})" if err else (" (cached)" if out["cached"] else "")
    print(f"weekly {out['weekly']:.0f}% (resets {utc(iso(out['weekly_resets']).timestamp())}), "
          f"5-hour {'none (window not started)' if out['five_hour'] is None else format(out['five_hour'], '.0f') + '%'}{stale}")
    for n, own, used, cap in scoped(snap["raw"]):
        print(f"{n}: {own}% of its allowance ({cap}% of the weekly quota), {used} of the weekly points")
    print(f"line {out['line']:.1f}% at {out['elapsed_share'] * 100:.1f}% of the week; "
          f"{'over' if out['ahead_by'] >= 0 else 'under'} by {abs(out['ahead_by']):.1f}; "
          f"new lanes: {['none (97% cutoff)', 'one (at or over the line)', 'two (under the line)'][out['lanes']]}")
    print(f"headroom to 98.5%: {out['headroom']:.1f} points (large Opus lane ~2, Sonnet ~1); "
          f"reset in {out['hours_to_reset']:.1f} h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
