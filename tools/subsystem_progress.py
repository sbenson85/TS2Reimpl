"""Generate the byte-weighted subsystem history page from committed data.

Usage: .venv/Scripts/python -I tools/subsystem_progress.py
       ... --ref HEAD --dates 2026-10-12 2026-10-19 2026-10-26 2026-11-02

Writes HTML and a JSON sidecar under local/ by default. No game, builds, checkout
changes or network calls. Historical statuses use the selected ref's map, sizes
and library exclusions. This is recorded verification, not an acceptance audit. Each snapshot also
carries the "no longer needs decompiling" bands (docs/roadmap.md, Metrics) over the same EA-code denominator; the
verified series doesn't depend on them.

Series: each subsystem's bytes, class-backed (db/subsystems.jsonl evidence: vtable, ctor, third-party
ranges, strings, static-init, status names; docs/subsystems.md, Method) plus its call-graph guesses, then
"other": unassigned code. With --evidence-only, guesses count as "other" and each subsystem's guessed bytes are
listed beside its size (that view runs high: read code leaves "other" first). A ref whose map predates the
"other" bucket (lane FT, 2026-11-04) counts its guesses under their subsystems in both views.
"""
import argparse
from collections import Counter
from datetime import date, datetime, timedelta, timezone
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb

ROOT = Path(__file__).resolve().parent.parent
OTHER = 'other'
FACTS = ('functions.jsonl', 'static-initializers.jsonl', 'missed-functions.jsonl',
         'library-matches.jsonl', 'names.jsonl', 'crt-equivalents.jsonl', 'subsystems.jsonl')


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def text(*args):
    return git(*args).decode('utf-8').strip()


def stamp(commit):
    return text('show', '-s', '--format=%cI', commit)


def utc_date(value):
    return datetime.fromisoformat(value).astimezone(timezone.utc).date()


GUESSES = True  # main() sets it from --evidence-only

# Rows in the intended campaign order (docs/roadmap.md, "Campaign order"; rough, update when the plan changes): the
# foundations, rendering then the scenegraph, world-lot, the middle campaigns, then the leaves. Subsystems not listed
# (order undecided) follow by size, then "other".
CAMPAIGN_ORDER = (
    'framework-gz',                                         # 1. foundations (shared-utility is mostly untriaged
                                                            #    guesses, notes/backlog.md: listed with the undecided)
    'rendering-gfx', 'scenegraph',                          # 2.
    'world-lot',                                            # 3.
    'neighbourhood', 'animation', 'objects', 'sim-core',    # 4. by the same dependency measure
    'persist-dbpf', 'lua-scripting', 'ui-windowing', 'ui-game',
    'audio-video', 'cas', 'sim-person', 'command-parser',   # 5. leaves (any time, in parallel)
)


def series(entry):
    """The series a map row counts under. By default a call-graph guess counts under its guessed subsystem (lanes'
    status names move only read code out of "other", so an evidence-only view inflates each subsystem's verified share:
    read code is placed, unread code stays guessed); "other" is then unassigned code. With --evidence-only, guesses
    count as "other" too. A calls-into guess (the function calls the subsystem) stays "other" in both views: nearly
    everything calls the framework, so it files callers with their callees, against bottom-up order."""
    entry = entry or {}
    sub = entry.get('subsystem') or OTHER
    if GUESSES and sub == OTHER and entry.get('guess') and not entry.get('basis', '').startswith('calls-into'):
        return entry['guess']
    return OTHER if sub == 'unassigned' or (sub == 'shared-utility' and not GUESSES) else sub


def load_universe(commit):
    # Use the canonical tsdb size corrections and library rules, but only files
    # from the pinned commit, even while another session edits the working tree.
    with tempfile.TemporaryDirectory(prefix='ts2-progress-') as directory:
        db = Path(directory) / 'db'
        db.mkdir()
        for name in FACTS:
            path = 'db/' + name
            exists = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', f'{commit}:{path}'],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            if exists:
                (db / name).write_bytes(git('show', f'{commit}:{path}'))
            elif name not in ('static-initializers.jsonl', 'missed-functions.jsonl'):
                raise ValueError(f'{commit[:8]} lacks required {path}')
        previous_root = tsdb.ROOT
        try:
            tsdb.ROOT = Path(directory)
            funcs = {f['rva']: f for f in tsdb.functions()}
            library = tsdb.library_rvas()
            subs = {s['rva']: s for s in tsdb.jsonl('subsystems.jsonl')}
        finally:
            tsdb.ROOT = previous_root
    universe = {r: f for r, f in funcs.items()
                if r not in library and subs.get(r, {}).get('subsystem') != 'third-party'}
    totals, basis, guessed = Counter(), Counter(), Counter()
    for r, f in universe.items():
        sub = subs.get(r, {})
        totals[series(sub)] += f['size']
        basis[sub.get('basis', 'unassigned').split(':')[0]] += f['size']
        if sub.get('guess'):
            guessed[sub['guess']] += f['size']
    planned = [k for k in CAMPAIGN_ORDER if k in totals]
    rest = sorted((k for k in totals if k != OTHER and k not in CAMPAIGN_ORDER), key=lambda k: -totals[k])
    order = planned + rest + [k for k in (OTHER,) if k in totals]
    totals = {k: totals[k] for k in order}
    return universe, subs, totals, basis, guessed


def snapshot(commit, label, universe, subs):
    values, seen = Counter(), set()
    bands, series_done = Counter(), Counter()
    with tarfile.open(fileobj=io.BytesIO(git('archive', commit, 'db/status'))) as archive:
        for member in archive.getmembers():
            if not member.isfile() or not member.name.endswith('.jsonl'):
                continue
            for line in archive.extractfile(member):
                if not line.strip():
                    continue
                entry = json.loads(line)
                rva = entry['rva']
                if rva in seen:
                    raise ValueError(f'Duplicate status for {rva} at {commit[:8]}')
                seen.add(rva)
                if rva not in universe:
                    continue
                if entry['status'] == 'verified':
                    values[series(subs.get(rva))] += universe[rva]['size']
                band = tsdb.headline_band(entry)
                if band:
                    bands[band] += universe[rva]['size']
                    series_done[series(subs.get(rva))] += universe[rva]['size']
    return dict(date=label, commit=commit, timestamp=stamp(commit), verified=dict(values), bands=dict(bands),
                no_decompile=dict(series_done))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--ref', default='HEAD', help='Pin map/sizes and newest sample to this commit')
    parser.add_argument('--dates', nargs='+', type=date.fromisoformat, help='Historical UTC end-of-day snapshots; selected ref is always appended')
    parser.add_argument('--out', type=Path, default=ROOT / 'local/subsystem-progress.html')
    parser.add_argument('--evidence-only', action='store_true',
                        help='Count call-graph guesses as "other" (class-backed subsystems only; inflates their share)')
    args = parser.parse_args()
    global GUESSES
    GUESSES = not args.evidence_only
    head = text('rev-parse', '--verify', args.ref + '^{commit}')
    latest = utc_date(stamp(head))
    dates = sorted(set(args.dates or [latest - timedelta(days=n) for n in (28, 21, 14, 7)]))
    if any(day >= latest for day in dates):
        parser.error('--dates must precede the selected commit date; that commit is appended automatically')
    universe, subs, totals, basis, guessed = load_universe(head)
    samples = []
    skipped = []
    for day in dates:
        commit = text('rev-list', '-1', '--first-parent', f'--before={day}T23:59:59Z', head)
        if not commit:
            skipped.append(str(day))
            continue
        has_status = text('ls-tree', '--name-only', commit, 'db/status')
        if not has_status:
            skipped.append(str(day))
            continue
        samples.append(snapshot(commit, str(day), universe, subs))
    samples.append(snapshot(head, str(latest), universe, subs))
    data = dict(head=head, total=totals, guessed={} if GUESSES else dict(guessed.most_common()), basis=dict(basis),
                samples=samples, guesses=GUESSES, headline_bands=list(tsdb.HEADLINE_BANDS),
                skipped_dates=skipped, method='Historical verified status; fixed selected-ref function sizes, subsystem assignments and EA-code denominator. Subsystems are class-backed plus call-graph guesses (default) or class-backed only (--evidence-only, guesses in "other"). Library and third-party excluded. Commit records, not independent acceptance audit.')
    template = (ROOT / 'tools/templates/subsystem_progress.html').read_text(encoding='utf-8')
    payload = json.dumps(data, ensure_ascii=True).replace('<', '\\u003c')
    scope = ('Subsystems count class-backed code plus call-graph guesses (about 93% of guessed bytes that lanes later '
             'read kept their guess; calls-into guesses excluded); other is unassigned code and calls-into guesses.' if GUESSES else
             'Subsystems count class-backed code only (vtable, constructor, string, static-init, status-name evidence); '
             'call-graph guesses and unassigned code are the other row. Read code leaves other first, so shares run high.')
    html = (template.replace('DATA_PLACEHOLDER', payload).replace('REF_PLACEHOLDER', head[:8])
            .replace('SCOPE_PLACEHOLDER', scope))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(html, encoding='utf-8')
    args.out.with_suffix('.json').write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {args.out.resolve()} and JSON sidecar; {len(samples)} snapshots, ref {head[:8]}')
    if skipped:
        print('Skipped dates before available status history: ' + ', '.join(skipped))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
