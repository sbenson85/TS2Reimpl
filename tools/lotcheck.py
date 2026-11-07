#!/usr/bin/env python3
"""Reference fixtures for the lot-save scenario (milestone B), and checks against them.

  python -I tools/lotcheck.py record SNAPSHOT     store a reference run as a fixture
  python -I tools/lotcheck.py check SNAPSHOT      compare a run with the fixtures for its state
  python -I tools/lotcheck.py recheck             one line per saved replacement run in local/saves
  python -I tools/lotcheck.py list
  python -I tools/lotcheck.py backfill            bind fixtures recorded before run manifests existed

SNAPSHOT is a folder written by scenario.ps1's snap step (local/saves/<stamp>-NAME), with its host
log (<stamp>-NAME.ts2host.log) and run manifest (<stamp>-NAME.run.json) beside it. Runs are
`tools/scenarios/lot-save-frames.txt` with `-ResetUserData -Seed N` (and `-Reference` for fixtures).

Seeded runs from the same baseline end in more than one state, in the original as well as with our
replacements: on lot load a SimAntics BHAV generates a townie whose age is 0x08 in some runs and
0x10 in others (NOTES, "B: the bimodal lot load"), and smaller variants occur within each (the ids two
lot objects get and their order, the lot's OBJM record 4F626A4D/1, which refers to those ids). So
fixtures are a set of end states recorded from reference runs, and a run is compared with each
fixture of its age as a whole: resources are never combined from different fixtures.

Outcomes (exit codes):
  0 MATCH         identical to one fixture (no persistent or unknown differences at all)
  2 MATCH-OPEN    identical to one fixture except inside KNOWN_OPEN byte ranges, held to a relation on
                  both sides (printed): passes, qualified: those bytes vary by nature and are not compared
                  exactly, and the report always says so
  3 UNSEEN        no fixture matches, but every difference is at a place where reference runs
                  differ among themselves (or the run's value is one a reference run produced):
                  plausibly an unrecorded original outcome. Not a pass: record more reference runs
                  or investigate
  1 UNEXPECTED    a difference where every reference run of this age agrees (a resource or byte
                  that never varied, a package or resource added or missing, a package that does
                  not parse). Blocks acceptance. It is a confirmed regression only once traced to
                  our replacements (e.g. the same run with the responsible hook off matches)
  4 NO-FIXTURE    no fixture of this age with the same binding
  5 DIAGNOSTIC    MATCH or MATCH-OPEN, but not acceptance evidence: the matched fixture's binding was
                  backfilled (not captured by its own run), or this run has no run manifest

Every fixture is bound to the run conditions (scenario text, baseline user data, seed, and the host's
"seed control:" lines); only fixtures with the same binding are used. Snapshots from before run
manifests existed have no scenario or baseline digest: their check says so. A fixture's binding is
either captured (written by `record` from the run's own manifest and log: binding_source "run
manifest and host log") or backfilled (reconstructed afterwards by `backfill`): only captured
fixtures, matched by a run with a manifest, give an accepted result.

Fixtures live in local/fixtures/lot-save/age-0xNN/<n>/ (game data: never committed), each with
fixture.json (source run, git revision, binding). Gate policy (NOTES): an accepted MATCH passes, an
accepted MATCH-OPEN passes with its open ranges listed; DIAGNOSTIC is evidence, not acceptance;
UNSEEN needs more reference runs or an explanation; UNEXPECTED blocks.
"""
import argparse
import functools
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dbpf                     # noqa: E402
import savediff                 # noqa: E402
import snapstore                # noqa: E402
from sims2_types import P, U    # noqa: E402

GENERATED_SIM = "Neighborhoods/E001/Characters/E001_User00487.package"
NEIGHBORHOOD = "Neighborhoods/E001/E001_Neighborhood.package"
SCENARIO = "tools/scenarios/lot-save-frames.txt"     # the active suite's (use_suite)

MATCH, UNEXPECTED, MATCH_OPEN, UNSEEN, NO_FIXTURE, DIAGNOSTIC = 0, 1, 2, 3, 4, 5
OUTCOME = {MATCH: "MATCH", UNEXPECTED: "UNEXPECTED", MATCH_OPEN: "MATCH-OPEN (open ranges held to their relation)",
           UNSEEN: "UNSEEN", NO_FIXTURE: "NO-FIXTURE", DIAGNOSTIC: "DIAGNOSTIC"}
CAPTURED = "run manifest and host log"

# (file, resource key) -> (byte ranges [lo, hi), why). A difference there is tolerated only when the
# resource exists on both sides with the same size, every differing byte lies in the ranges, and the
# relation of the resource holds on both sides (camera_session, measured from a baseline, so none: no pass).
CAMERA_KEY = "EC44BDDC/FFFFFFFF/0000262A/00000000"
KNOWN_OPEN = {
    (NEIGHBORHOOD, CAMERA_KEY): (
        [(0x51, 0x55), (0x59, 0x61)],
        "neighbourhood camera yaw at 0x51, focus x, y at 0x59, 0x5d: the idle drift camera, a random walk stepped "
        "on real time; both sides within one session's bounds of the record they loaded"),
}

# The camera record (the 'neighborhood' resource of cTSCameraSmoothSims1Controller) against the one the session
# loaded; docs/save-diff.md, "The neighbourhood camera record". The drift camera folds its orbit into the yaw when
# the next effect starts, so a session's turn is a sum of values of the orbit curve: every session measured lies
# inside one curve range (-0.177..0.255 rad), and the window is two. It carries the focus on a meta-particle's
# random walk, with mean square displacement about 37 per second of drift: the 77 sessions measured lie in
# 3.9..134.5, median 55. 250 is out of reach (under 1e-4 for 170 s of drift); under 1 is a walk that did not run
# (about 3e-4 by chance).
CAMERA_YAW_AT, CAMERA_FOCUS_AT = 0x51, 0x59
CAMERA_YAW_MAX = 4 * math.pi        # restoring normalises to 0..2pi, and a session adds less than a turn
CAMERA_TURN = (-0.36, 0.52)
CAMERA_MOVE = (1.0, 250.0)


def main_checkout():
    # local/ lives in the main checkout (worktrees share it).
    out = subprocess.run(["git", "-C", HERE, "rev-parse", "--path-format=absolute", "--git-common-dir"],
                         capture_output=True, text=True, check=True).stdout.strip()
    return os.path.dirname(out)


ROOT = main_checkout()
FIXTURES = os.path.join(ROOT, "local", "fixtures", "lot-save")
BASELINE = os.path.join(ROOT, "local", "userdata-baseline")

# Fixture suites, chosen by the scenario a run's manifest names (`suite_of`): the scenario file, where
# its fixtures live and how a run's state (the fixture group it is compared within) is found.
#   lot-save-frames  lot 6 from the standard baseline; state = the generated townie's age
#   lot-resave       lot 10 re-saved from a prepared baseline (local/baselines/); one state, "all"
# plus how a run shows it saved (a host log line, or a test on the snapshot).
SUITES = {
    "lot-save-frames": ("lot-save", "age", "control: post done"),
    "lot-resave": ("lot-resave", None, lambda snap: lot_resaved(snap)),
}
LOT10 = "Neighborhoods/E001/Lots/E001_Lot10.package"
SUITE = "lot-save-frames"
NORMALISE = True        # --bytes turns the normalised classes off (plain byte comparison, as before)
LOG_OVERRIDE = None     # --log: the run's host log when it is not beside the snapshot
MANIFEST_OVERRIDE = None
FIRST_ACCEPTABLE = False   # crosscheck: stop at the first MATCH-OPEN instead of looking on for a MATCH
EXCLUDE = set()        # fixtures left out of a comparison (crosscheck: the fixture under test)


def use_suite(name):
    global SUITE, SCENARIO, FIXTURES
    if name not in SUITES:
        sys.exit("no fixture suite for scenario %r (known: %s)" % (name, ", ".join(SUITES)))
    SUITE = name
    SCENARIO = "tools/scenarios/%s.txt" % name
    FIXTURES = os.path.join(ROOT, "local", "fixtures", SUITES[name][0])


def suite_of(snapshot):
    """The suite named by the run manifest's scenario ('@name'); lot-save-frames when no scenario is
    recorded."""
    m = manifest(snapshot)
    if m is not None:
        scen = m.get("scenario")
        if scen and scen.startswith("@"):
            return scen[1:]
        if scen:
            sys.exit("run %s used an inline scenario: no fixture suite" % snapshot)
    return "lot-save-frames"


def manifest(snapshot):
    """The run manifest (<snapshot>.run.json) as a dict, None without one; MANIFEST_OVERRIDE stands in for it
    (crosscheck, where the run is a fixture)."""
    if MANIFEST_OVERRIDE is not None:
        return MANIFEST_OVERRIDE
    man = snapshot.rstrip("/\\") + ".run.json"
    if not os.path.isfile(man):
        return None
    with open(man, encoding="utf-8-sig") as f:
        return json.load(f)


def norm(path):
    return path.replace("\\", "/")


# ---- run conditions ------------------------------------------------------------------------------

def tree_digest(root):
    """sha256 over sorted "relpath<TAB>sha256" lines (lower-case, forward slashes); scenario.ps1
    computes the same for the baseline it restores."""
    lines = []
    for d, _, files in os.walk(root):
        for n in files:
            p = os.path.join(d, n)
            rel = norm(os.path.relpath(p, root)).lower()
            with open(p, "rb") as f:
                lines.append("%s\t%s" % (rel, hashlib.sha256(f.read()).hexdigest()))
    return hashlib.sha256("".join(l + "\n" for l in sorted(lines)).encode("utf-8")).hexdigest()


def read_log(snapshot):
    log = LOG_OVERRIDE or snapshot.rstrip("/\\") + ".ts2host.log"
    if not os.path.isfile(log):
        sys.exit("no host log beside the snapshot: %s" % log)
    with open(log, encoding="latin-1") as f:
        text = f.read()
    if "seed control:" not in text:
        sys.exit("not a seeded run (no 'seed control:' line in %s)" % log)
    saved = SUITES[suite_of(snapshot)][2]
    if FIRST_ACCEPTABLE:        # crosscheck: the fixture's run was checked when it was recorded (copies lose mtimes)
        return log, text
    if isinstance(saved, str):
        if saved not in text:
            sys.exit("the run did not save (no %r in %s)" % (saved, log))
    elif not saved(snapshot):
        sys.exit("the run did not save (%s is not newer than in its baseline)" % LOT10)
    return log, text


def lot_resaved(snapshot):
    """A UI save leaves no host log line: the lot package must be newer than the baseline's copy
    (robocopy keeps modification times, so an unsaved run keeps the baseline's)."""
    base = (manifest(snapshot) or {}).get("baseline")
    a, b = os.path.join(snapshot, LOT10), os.path.join(base or "", LOT10)
    return os.path.isfile(a) and os.path.isfile(b) and os.path.getmtime(a) > os.path.getmtime(b) + 1


def log_binding(text):
    controls = sorted({re.sub(r"^\S+ ", "", l) for l in text.splitlines() if "seed control:" in l})
    m = re.search(r"seed control: clock seeds replaced, starting at (0x[0-9a-fA-F]+)", text)
    return {"seed": int(m.group(1), 16) if m else None, "controls": controls,
            "draws": bool(re.search(r"\bdraws: ", text))}


def binding_of(snapshot, text):
    b = log_binding(text)
    b.update(scenario_sha256=None, baseline_sha256=None)
    m = manifest(snapshot)
    if m is not None:
        b["scenario_sha256"] = m.get("scenario_sha256")
        b["baseline_sha256"] = m.get("baseline_sha256")
        chain = chain_of(snapshot)
        if len(chain) > 1:
            # A reload run binds to the chain's root (the prepared baseline), as its reference chain does;
            # its own baseline (an earlier save) is checked by its digest when it is used.
            b["immediate_sha256"] = b["baseline_sha256"]
            b["baseline_sha256"] = (_read_manifest(chain[-2]) or {}).get("baseline_sha256")
        if m.get("seed"):
            b["seed_manifest"] = int(str(m["seed"]), 0)
    return b


def mismatches(fix, run):
    """Fields where a fixture's binding and a run's differ (a field missing on either side is not
    compared; check() reports missing run fields separately)."""
    out = []
    for k in ("seed", "controls", "draws", "scenario_sha256", "baseline_sha256"):
        a, b = (fix or {}).get(k), run.get(k)
        if a is not None and b is not None and a != b:
            out.append(k)
    if not fix or fix.get("seed") is None:
        out.append("fixture unbound")
    return out


# ---- fixtures ------------------------------------------------------------------------------------

def chain_of(snapshot):
    """The baselines behind a run, nearest first: [its baseline, that baseline's baseline, ...]. A reload run
    starts from an earlier run's snapshot (which has its own run manifest); a first run from a prepared
    baseline, which has none."""
    out, cur = [], snapshot
    while True:
        m = manifest(cur) if cur == snapshot else _read_manifest(cur)
        base = (m or {}).get("baseline")
        if not base or base in out or len(out) > 8:
            return out
        out.append(base)
        cur = base


def _read_manifest(snapshot):
    man = snapshot.rstrip("/\\") + ".run.json"
    if not os.path.isfile(man):
        return None
    with open(man, encoding="utf-8-sig") as f:
        return json.load(f)


def state_of(snapshot):
    if SUITES[SUITE][1] is None:
        depth = len(chain_of(snapshot))
        return "all" if depth <= 1 else "reload-%d" % (depth - 1)
    path = os.path.join(snapshot, GENERATED_SIM)
    if not os.path.isfile(path):
        sys.exit("no generated Sim in %s" % snapshot)
    pk = dbpf.Package(path)
    for e in pk.entries:
        if e.type == 0xAC598EAC:
            data = pk.read(e)
            i = data.find(b"\x03\x00\x00\x00age")
            if i >= 0:
                return "age-0x%02x" % struct.unpack_from("<I", data, i + 7)[0]
    sys.exit("no age property in the generated Sim's appearance record")


def fixtures(state):
    root = os.path.join(FIXTURES, state)
    if not os.path.isdir(root):
        return []
    return [os.path.join(root, n) for n in sorted(os.listdir(root), key=int)]


def all_fixtures():
    if not os.path.isdir(FIXTURES):
        return []
    return [(s, f) for s in sorted(os.listdir(FIXTURES)) for f in fixtures(s)]


def fixture_meta(f):
    p = os.path.join(f, "fixture.json")
    if not os.path.isfile(p):
        return {}
    with open(p) as f:
        return json.load(f)


def git_rev():
    return subprocess.run(["git", "-C", HERE, "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()


# ---- differences ---------------------------------------------------------------------------------

@functools.lru_cache(maxsize=512)
def _package(path):
    try:
        return dbpf.Package(path)
    except Exception:           # noqa: BLE001  (a package that does not parse)
        return None


def resource(root, file, key):
    """Bytes of a resource, None if the package or the resource is missing or unreadable."""
    path = os.path.join(root, file)
    if not os.path.isfile(path):
        return None
    pk = _package(os.path.abspath(path))
    if pk is None:
        return None
    return pk.get(tuple(int(x, 16) for x in key.split("/")))


def file_bytes(root, file):
    path = os.path.join(root, file)
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        return f.read()


def differences(fixture, snapshot):
    """Persistent and unknown differences between a fixture and a run, resource- and file-level:
    (file, key or None, status, class, error)."""
    rep = savediff.compare(os.path.join(fixture, "userdata"), snapshot)
    out = []
    for r in rep["resources"]:
        if r["class"] in (P, U):
            out.append((norm(r["file"]), r["key"], r["status"], r["class"], None))
    for r in rep["files"]:
        cls = r["class"] or U       # packages added or removed carry no class of their own
        if cls in (P, U) or r.get("error"):
            out.append((norm(r["file"]), None, r["status"], cls, r.get("error")))
    return out


# ---- normalisation: object ids and the scheduler's countdowns (docs/save-diff.md, "Normalised classes") ----
#
# Two value classes in a lot package, each with its meaning established from the code; everything else in the
# records they touch is still compared byte for byte.
#  1. Object ids of objects created during the run ("fresh": not live in the baseline with the same type word).
#     cEdithObjectModule's AddObject (0x422910) gives an object with no requested id the lowest free id; which
#     of the objects created on every lot load asks first varies between runs, so the same objects get permuted
#     ids (and a different set of the free ids). Fresh objects are matched by their type word (unique per type,
#     else no match), the ids are compared as a permutation, and inside the records an id may differ only where
#     the run has the run id and the fixture the matched fixture id (16-bit words).
#  2. Countdowns in the OBJM schedule table (module +0x1a30, one word per object id: flags << 16 | countdown).
#     The simulator's object pass (cTSSimulator vf4_11, 0x483190) decrements the countdown of each scheduled
#     object (flag 1) once per pass while it is >= 1 and runs the object's thread when it is < 1; in the
#     simulator's mode where vf4_26 (0x480fe0) is true only objects with flag 2 take part. In the lot-resave runs
#     only entries with flags 1 and 2 move: every one of them by the same number of passes t (t depends on how
#     many passes ran between load and save), and entries whose countdown reached 0 hold whatever their thread
#     set when it ran. Per side the check finds t from the baseline, and accepts an entry that is unchanged,
#     counted down by exactly t (t - 1 above one id: a pass interrupted by a thread that returns false resumes
#     at that id), or could have run (baseline countdown <= t); flags must be unchanged, the two sides must agree
#     on which entries are unchanged, and entries without both flags must not move at all.
#  Records of objects that are not live (not in the side's OBJM list) are kept from the input package: the save
#  writes the live objects' records (vf112 0x42ebf0) and deletes no others, so they must equal the baseline's.

XOBJ_TYPE, LOT_OBJECT_TYPE = 0x584F424A, 0xFA1C39F7
OBJECT_RECORDS = (XOBJ_TYPE, LOT_OBJECT_TYPE)
OBJM_KEY = "4F626A4D/FFFFFFFF/00000001/00000000"
COUNTING = 0x3       # schedule flags 1 (scheduled) and 2 (takes part in the simulator's mode)


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


@functools.lru_cache(maxsize=8)
def _tree_digest(path):
    return tree_digest(path)


def baseline_of(snapshot, run_b):
    """The baseline user data the run started from, verified against the run's own digest: (path, None) or
    (None, why)."""
    want = run_b.get("baseline_sha256")
    if want is None:
        return None, "the run records no baseline digest"
    chain = chain_of(snapshot) or [BASELINE]
    checks = [(chain[-1], want)]
    if len(chain) > 1:
        checks.append((chain[0], run_b.get("immediate_sha256")))
    for path, digest in checks:
        if not os.path.isdir(path):
            return None, "baseline %s is not on disk" % path
        if _tree_digest(os.path.abspath(path)) != digest:
            return None, "baseline %s no longer has the run's digest" % path
    return (chain[-1], chain[0]), None


def objm_of(root, file):
    data = resource(root, file, OBJM_KEY)
    return (data, savediff.objm_layout(data)) if data is not None else (None, None)


def per_load_types(links):
    """The type words of the objects a load deletes and creates again, from (save, its baseline) pairs of one
    reference chain: every object that is not at the same id with the same type on both sides of a link. Only
    the reference chain names them, so a run cannot widen the set by moving an object of another type."""
    out = set()
    for save, base in links:
        s, b = dict(save["objects"]), dict(base["objects"])
        out |= {t for i, t in s.items() if b.get(i) != t} | {t for i, t in b.items() if s.get(i) != t}
    return out


def fresh(lay, types):
    """The ids of the per-load objects in a save (whatever id they landed on, even their old one)."""
    return {i for i, t in lay["objects"] if t in types}


def id_mapping(F, R, B, types=None):
    """Run id -> fixture id for every id whose value differs (a permutation of ids), or (None, why). types: the
    per-load object types (default: those of the fixture against the baseline B)."""
    f, r = dict(F["objects"]), dict(R["objects"])
    if len(f) != len(F["objects"]) or len(r) != len(R["objects"]):
        return None, "an object id is listed twice"
    if types is None:
        types = per_load_types([(F, B)])
    ff, fr = fresh(F, types), fresh(R, types)
    if {i: t for i, t in f.items() if i not in ff} != {i: t for i, t in r.items() if i not in fr}:
        return None, "objects that were live in the baseline differ"
    common = {i for i in ff & fr if f[i] == r[i]}
    uf, ur = ff - common, fr - common
    pairs = {}
    for t in sorted({f[i] for i in uf} | {r[i] for i in ur}):
        lf, lr = sorted(i for i in uf if f[i] == t), sorted(i for i in ur if r[i] == t)
        if len(lf) != len(lr):
            return None, "fresh objects of type 0x%x: %d in the fixture, %d in the run" % (t, len(lf), len(lr))
        if len(lf) > 1:
            return None, "fresh objects of type 0x%x are not unique (%d)" % (t, len(lf))
        pairs[lr[0]] = lf[0]
    dom, img = set(pairs), set(pairs.values())
    for a, b in zip(sorted(img - dom), sorted(dom - img)):      # complete the permutation over the free ids
        pairs[a] = b
    return {a: b for a, b in pairs.items() if a != b}, None


def id_words_only(run, fix, pairs, prior=None):
    """Offsets where run and fix differ other than by an id word (run id at the same offset as its fixture
    id, 16-bit little-endian); [] if they are equal up to the id mapping, [-1] if the sizes differ. prior: the
    mapping between the two sides' own baselines' per-load objects (a reload; see normalise_lot)."""
    if len(run) != len(fix):
        return [-1]
    if prior:
        allowed = set(pairs.items()) | set(prior.items())
        pairs = _PairSet(allowed)
    bad = []
    for i in range(len(run)):
        if run[i] == fix[i]:
            continue
        ok = False
        for o in (i - 1, i):
            if 0 <= o and o + 2 <= len(run):
                a, b = struct.unpack_from("<H", run, o)[0], struct.unpack_from("<H", fix, o)[0]
                if pairs.get(a) == b if not isinstance(pairs, _PairSet) else (a, b) in pairs:
                    ok = True
                    break
        if not ok:
            bad.append(i)
    return bad


class _PairSet(frozenset):
    """(run id, fixture id) pairs from two mappings, which may map one run id differently."""


def countdowns(base, side, ids):
    """The side's pass count t against the baseline (the shift most counting entries share), the cut (ids at
    or above it saw t - 1 passes), and per id (1-based): (flags as in the baseline, counting flags, passes the
    id saw, could have run: its baseline countdown ran out within those passes)."""
    d, out = {}, {}
    for i in ids:
        b, s = base[i - 1], side[i - 1]
        if (b >> 16) & COUNTING == COUNTING and b >> 16 == s >> 16:
            d[i] = s16(b) - s16(s)
    pos = [v for v in d.values() if v > 0]
    t = 0
    if pos:
        score = {c: sum(1 for v in pos if v in (c, c - 1)) for c in set(pos) | {v + 1 for v in pos}}
        t = max(score, key=lambda c: (score[c], sum(1 for v in pos if v == c), c))
    at_t = [i for i, v in d.items() if v == t and t > 0]
    cut = max(at_t) + 1 if at_t else 1
    for i in ids:
        b, s = base[i - 1], side[i - 1]
        teff = max(t if i < cut else t - 1, 0)
        out[i] = (b >> 16 == s >> 16, (b >> 16) & COUNTING == COUNTING, teff, teff > 0 and s16(b) <= teff)
    return out, t, cut


def normalise_lot(file, fixture_root, snapshot, base_root, prevs=None):
    """Normalised comparison of one lot package: (explained keys, bad keys, notes, {open key: why}).
    base_root is the chain's root (fresh objects and pass counts are measured from it); prevs = (fixture's,
    run's) own baseline, which non-live records must equal (the root itself for a first save)."""
    prevs = prevs or (base_root, base_root)
    notes, bad, opened, cf, cr = [], set(), {}, {}, {}
    (fd, F), (rd, R), (_, B) = (objm_of(fixture_root, file), objm_of(snapshot, file), objm_of(base_root, file))
    if F is None or R is None or B is None:
        return set(), set(), ["%s: no parsable OBJM on every side: not normalised" % file], {}
    # Each side's own baseline: the schedule it loaded and, in a reload, the per-load objects it deleted.
    PF, PR = (B, B) if tuple(prevs) == (base_root, base_root) else (objm_of(p, file)[1] for p in prevs)
    if PF is None or PR is None:
        return set(), {OBJM_KEY}, ["%s: no parsable OBJM in a side's own baseline" % file], {}
    # Per-load objects, named by the reference chain (each link: a save against the save it loaded). In a reload
    # both chains' first saves hold their own per-load objects at their own ids, and the load deletes those and
    # creates them again at whichever ids are free (sometimes the root's own ids): they are matched by type too.
    types = per_load_types([(F, PF)] + ([(PF, B)] if PF is not B else []))
    pairs, why = id_mapping(F, R, B, types)
    if pairs is None:
        return set(), {OBJM_KEY}, ["%s: objects not matched (%s)" % (file, why)], {}
    inv = {b: a for a, b in pairs.items()}
    pi = lambda i: pairs.get(i, i)
    ff, fr = fresh(F, types), fresh(R, types)
    if PF is not B and (len(fresh(PF, types)) != len(ff) or len(fresh(PR, types)) != len(fr)):
        return set(), {OBJM_KEY}, ["%s: per-load objects not the same in a baseline" % file], {}
    # A per-load object's record keeps the id its predecessor (the baseline's object of its type, which the load
    # deleted) had: in a reload those are the two first saves' ids, matched by type as well.
    prior = {}
    if PF is not B:
        pf_ids, pr_ids = ({t: i for i, t in P["objects"] if t in types} for P in (PF, PR))
        if set(pf_ids) != set(pr_ids):
            return set(), {OBJM_KEY}, ["%s: per-load object types differ between the baselines" % file], {}
        prior = {pr_ids[t]: pf_ids[t] for t in types if t in pr_ids and pr_ids[t] != pf_ids[t]}
        if prior:
            notes.append("%s: baselines' per-load objects matched by type: %s" % (
                file, ", ".join("run 0x%x = fixture 0x%x" % kv for kv in sorted(prior.items()))))
    # OBJM: header, object list (fresh objects in any order), schedule, the rest.
    objm_bad = []
    if rd[:0x50] != fd[:0x50]:
        objm_bad.append("header")
    mapped = [(pi(i), t) for i, t in R["objects"]]
    if sorted(mapped) != sorted(F["objects"]) or \
            [x for x in mapped if x[0] not in ff] != [x for x in F["objects"] if x[0] not in ff]:
        objm_bad.append("object list")
    # Countdowns run on from the schedule each side loaded (vf113 0x42e610 reads it verbatim): its own baseline.
    sf, sr = F["schedule"], R["schedule"]
    sbf, sbr = (PF or {}).get("schedule", []), (PR or {}).get("schedule", [])
    t_f = t_r = None
    if not (len(sf) == len(sr) == len(B["schedule"]) == len(sbf) == len(sbr)):
        objm_bad.append("schedule length")
    else:
        kept = [i for i, _ in F["objects"] if i not in ff]        # live in the baseline, same id on both sides
        cf, t_f, cut_f = countdowns(sbf, sf, kept)
        cr, t_r, cut_r = countdowns(sbr, sr, kept)
        moved = ran = 0
        keptset = set(kept)
        for i in range(1, len(sf) + 1):
            r = inv.get(i, i)
            if sr[r - 1] == sf[i - 1]:
                continue
            if i not in keptset:
                objm_bad.append("schedule id 0x%x" % i)
                continue
            (okf, cntf, tf, ranf), (okr, cntr, tr, ranr) = cf[i], cr[i]
            if not (okf and okr and cntf and cntr):
                objm_bad.append("schedule id 0x%x (%s)" % (i, "flags changed" if not (okf and okr) else
                                                           "moved without counting flags"))
            elif s16(sr[r - 1]) + tr == s16(sf[i - 1]) + tf:
                moved += 1          # same deadline: set at the same pass, counted down by each side's passes
            elif s16(sr[r - 1]) + tr == s16(sbr[r - 1]) and s16(sf[i - 1]) + tf == s16(sbf[i - 1]):
                moved += 1          # a reload: counted down on each side from the countdown that side loaded
            elif ranf and ranr:
                moved += 1          # its thread ran in both runs and set its own countdown
                ran += 1
            else:
                objm_bad.append("schedule id 0x%x (countdown %d + %d passes vs %d + %d)"
                                % (i, s16(sr[r - 1]), tr, s16(sf[i - 1]), tf))
        notes.append("%s: schedule countdowns: %d passes in the fixture (cut 0x%x), %d in the run (cut 0x%x); "
                     "%d entries differ: %d with the same deadline, %d whose threads ran on both sides"
                     % (file, t_f, cut_f, t_r, cut_r, moved, moved - ran, ran))
    rest = id_words_only(rd[R["rest_at"]:], fd[F["rest_at"]:], pairs)
    if rest:
        objm_bad.append("rest of the record (%s)" % ("size" if rest == [-1] else "0x%x" % (F["rest_at"] + rest[0])))
    if objm_bad:
        bad.add(OBJM_KEY)
        notes.append("%s OBJM: not explained: %s" % (file, "; ".join(objm_bad[:6])))
    # Object records.
    pks = [_package(os.path.abspath(os.path.join(root, file)))
           for root in (fixture_root, snapshot, base_root) + tuple(prevs)]
    if any(p is None for p in pks):
        return set(), bad | {OBJM_KEY}, notes + ["%s: a baseline copy of the package is missing" % file], {}
    live_f, live_r = {i for i, _ in F["objects"]}, {i for i, _ in R["objects"]}
    explained = set()
    for typ in OBJECT_RECORDS:
        recs = [{e.instance: e for e in p.entries if e.type == typ and e.group == 0xFFFFFFFF and e.instance_hi == 0}
                for p in pks]
        key = lambda i: "%08X/FFFFFFFF/%08X/00000000" % (typ, i)
        get = lambda k, i: pks[k].read(recs[k][i]) if i in recs[k] else None
        for side in (1, 0):
            for i in recs[side]:
                data = get(side, i)
                if i in (live_r if side == 1 else live_f):
                    j = pi(i) if side == 1 else inv.get(i, i)
                    if side == 0:
                        if get(1, j) is None:
                            bad.update({key(i), key(j)})
                            notes.append("%s %s: live in the fixture, no record for run object 0x%x" % (file, key(i), j))
                        continue
                    ref = get(0, j)
                    off = [-1] if ref is None else id_words_only(data, ref, pairs, prior if i in fr else None)
                    why = pass_counter(file, key(i), off, data, ref, get(4, i), get(3, j), cr.get(i), cf.get(j)) \
                        if off and i == j else None
                    if why:
                        opened[key(i)] = why
                        notes.append("%s %s: open: %s" % (file, key(i), why))
                    elif off:
                        bad.update({key(i), key(j)})
                        notes.append("%s %s: run object 0x%x vs fixture object 0x%x: %s" % (
                            file, key(i), i, j, "no fixture record" if ref is None else
                            "size" if off == [-1] else "differs at 0x%x" % off[0]))
                elif get(4 if side == 1 else 3, i) != data:     # kept from that side's own baseline
                    bad.add(key(i))
                    notes.append("%s %s: not a live object in the %s and not its baseline's record"
                                 % (file, key(i), "run" if side == 1 else "fixture"))
        for side in (0, 1):
            explained |= {key(i) for i in recs[side]}
    explained.add(OBJM_KEY)
    if pairs:
        notes.append("%s: object ids matched by type: %s" % (
            file, ", ".join("run 0x%x = fixture 0x%x" % (a, b) for a, b in sorted(pairs.items()))))
    if bad:
        # The mapping and countdowns explain the package only as a whole: a record that contradicts them (even
        # one byte-identical to the fixture's, so not itself a difference) leaves OBJM and every record unexplained.
        notes.append("%s: not normalised (OBJM or a record not explained, above)" % file)
        return set(), bad | {OBJM_KEY}, notes, {}
    return explained - set(opened), bad, notes, opened


# A 16-bit word that advances by one per simulator pass, modulo M, in one object's saved state: (file, key) ->
# (offset, M, why). Open, not normalised: which field it is has not been found (it lies in cTSObject::Write's
# (0x53ebf0) output after the inventory block, before the 0xff9b marker), so a match with it differing is
# MATCH-OPEN at best, and only when the word equals the baseline's plus the side's own pass count, modulo M.
PASS_COUNTERS = {
    (LOT10, "584F424A/FFFFFFFF/00000173/00000000"): (
        0x1BA, 30, "XOBJ 0x173 word 0x1ba = (baseline + passes) mod 30 on both sides (exact in all 26 reference "
                   "and 6 replacement re-saves and the 12 reloads of 2026-10-18); which field it is is not established"),
}


def pass_counter(file, key, off, run, fix, base_run, base_fix, run_obj, fix_obj):
    """base_run/base_fix: the record each side loaded (its own baseline's)."""
    spec = PASS_COUNTERS.get((file, key))
    if spec is None or off == [-1] or None in (base_run, base_fix, run_obj, fix_obj):
        return None
    at, m, why = spec
    if any(not at <= o < at + 2 for o in off) or not len(base_run) == len(base_fix) == len(run):
        return None
    word = lambda d: struct.unpack_from("<H", d, at)[0]
    if (word(base_run) + run_obj[2]) % m != word(run) or (word(base_fix) + fix_obj[2]) % m != word(fix):
        return None
    return why


def normalise(diffs, fixture, snapshot, base_root):
    """Split diffs into (remaining, normalised, open [(diff, why)], notes) using normalise_lot on each lot
    package that differs in OBJM or object records."""
    if base_root is None:
        return diffs, [], [], []
    root, immediate = base_root
    fixture_root = os.path.join(fixture, "userdata")
    prev_f = os.path.join(fixture, "previous")        # a reload fixture's own baseline lots (record copies them)
    if root != immediate and not os.path.isdir(prev_f):
        return diffs, [], [], ["fixture %s has no copy of its own baseline's lots: not normalised" % fixture]
    prevs = (prev_f if root != immediate else root, immediate)
    files = sorted({d[0] for d in diffs if d[1] and "/Lots/" in d[0] and not d[4] and
                    (d[1] == OBJM_KEY or int(d[1].split("/")[0], 16) in OBJECT_RECORDS)})
    remaining, done, opened, notes = list(diffs), [], [], []
    for file in files:
        explained, _, n, op = normalise_lot(file, fixture_root, snapshot, root, prevs)
        notes += n
        keep = []
        for d in remaining:
            if d[0] == file and not d[4] and d[1] in explained:
                done.append(d)
            elif d[0] == file and not d[4] and d[1] in op and d[2] == "changed":
                opened.append((d, op[d[1]]))
            else:
                keep.append(d)
        remaining = keep
    return remaining, done, opened, notes


def camera_session(saved, loaded):
    """Why a saved camera record cannot be one session's work on the record it loaded; None when it can."""
    if len(saved) != len(loaded):
        return "size %d, loaded %d" % (len(saved), len(loaded))
    ranges = KNOWN_OPEN[(NEIGHBORHOOD, CAMERA_KEY)][0]
    moved = [i for i in range(len(saved)) if saved[i] != loaded[i] and not any(lo <= i < hi for lo, hi in ranges)]
    if moved:
        return "byte 0x%x differs from the record it loaded" % moved[0]
    yaw, was = (struct.unpack_from("<f", d, CAMERA_YAW_AT)[0] for d in (saved, loaded))
    x, y = struct.unpack_from("<2f", saved, CAMERA_FOCUS_AT)
    x0, y0 = struct.unpack_from("<2f", loaded, CAMERA_FOCUS_AT)
    if not all(map(math.isfinite, (yaw, was, x, y, x0, y0))):
        return "a value is not finite"
    if abs(yaw) > CAMERA_YAW_MAX:       # a huge angle wraps to anything
        return "yaw %g is not an angle of this camera" % yaw
    turn, move = math.remainder(yaw - was, 2 * math.pi), math.hypot(x - x0, y - y0)
    if not CAMERA_TURN[0] <= turn <= CAMERA_TURN[1]:
        return "yaw turned %.3f rad from the record it loaded (bound %g..%g)" % ((turn,) + CAMERA_TURN)
    if not CAMERA_MOVE[0] <= move <= CAMERA_MOVE[1]:
        return "focus moved %.1f from the record it loaded (bound %g..%g)" % ((move,) + CAMERA_MOVE)
    return None


def fixture_camera_base(fixture, root, immediate):
    """The camera record a fixture's run loaded, None if no longer on disk. A first save loaded the root's; a reload
    its first save's, which record() keeps in previous/; older fixtures find it by their source run's manifest."""
    if root == immediate:
        return resource(root, NEIGHBORHOOD, CAMERA_KEY)
    if os.path.isfile(os.path.join(fixture, "previous", NEIGHBORHOOD)):
        return resource(os.path.join(fixture, "previous"), NEIGHBORHOOD, CAMERA_KEY)
    source = fixture_meta(fixture).get("source")
    base = (_read_manifest(source) or {}).get("baseline") if source else None
    return resource(base, NEIGHBORHOOD, CAMERA_KEY) if base and os.path.isdir(base) else None


def camera_relation(fixture, snapshot, base_root):
    """(why the relation fails on a side, or None; a note on what could not be measured)."""
    if base_root is None:
        return "no baseline to measure the session from", ""
    root, immediate = base_root
    loaded = resource(immediate, NEIGHBORHOOD, CAMERA_KEY)
    if loaded is None:
        return "the run's baseline has no camera record", ""
    why = camera_session(resource(snapshot, NEIGHBORHOOD, CAMERA_KEY), loaded)
    if why:
        return "this run: " + why, ""
    loaded = fixture_camera_base(fixture, root, immediate)
    if loaded is None:
        return None, "; the fixture's own baseline is gone, so only this run's side was measured"
    why = camera_session(resource(os.path.join(fixture, "userdata"), NEIGHBORHOOD, CAMERA_KEY), loaded)
    return ("the fixture: " + why if why else None), ""


def open_reason(d, fixture, snapshot, base_root):
    file, key, status = d[0], d[1], d[2]
    spec = KNOWN_OPEN.get((file, key))
    if spec is None or status != "changed":
        return None
    a, b = resource(os.path.join(fixture, "userdata"), file, key), resource(snapshot, file, key)
    if a is None or b is None or len(a) != len(b):
        return None
    ranges, why = spec
    if any(a[i] != b[i] and not any(lo <= i < hi for lo, hi in ranges) for i in range(len(a))):
        return None
    refused, note = camera_relation(fixture, snapshot, base_root)
    return None if refused else why + note


def variation(d, best, same, every, snapshot):
    """Whether the original varies where the run differs from its closest fixture: (varies, text)."""
    file, key, status, _, error = d
    if error:
        return False, "does not parse: %s" % error
    get = (lambda root: resource(root, file, key)) if key else (lambda root: file_bytes(root, file))
    run = get(snapshot)
    refs_same = [get(os.path.join(f, "userdata")) for f in same]
    refs_all = [get(os.path.join(f, "userdata")) for f in every]
    if run is not None and any(run == r for r in refs_all):
        return True, "a value a reference run produced (in another combination)"
    present = [r is not None for r in refs_same]
    if run is None:
        return (False, "missing; present in all %d references" % len(refs_same)) if all(present) else \
            (True, "missing; also missing in some references")
    if not any(present):
        return False, "added; absent in all %d references" % len(refs_same)
    vals = [r for r in refs_same if r is not None]
    if len(set(vals)) == 1 and all(present):
        return False, "constant in all %d references" % len(refs_same)
    lens = {len(v) for v in vals}
    if len(lens) == 1 and len(run) not in lens:
        return False, "size %d; %d in all references" % (len(run), lens.pop())
    if len(lens) == 1 and all(present):
        var = {i for i in range(len(run)) if len({v[i] for v in vals}) > 1}
        ref = get(os.path.join(best, "userdata"))
        diff = [i for i in range(len(run)) if ref is None or run[i] != ref[i]]
        fixed = [i for i in diff if i not in var]
        if fixed:
            return False, "%d bytes differ where all references agree (first at 0x%x)" % (len(fixed), fixed[0])
        return True, "new value, only in bytes where references differ (%d of %d)" % (len(diff), len(var))
    return True, "references differ in size or presence"


# ---- commands ------------------------------------------------------------------------------------

def label(d):
    return "%s %s%s" % (d[0], d[1] or "(file)", " " + d[2] if d[2] else "")


def captured(fixture):
    return fixture_meta(fixture).get("binding_source") == CAPTURED


def accepted(fixture, run_b, say):
    """Whether a match against this fixture is acceptance evidence (see DIAGNOSTIC)."""
    why = []
    if not captured(fixture):
        why.append("the fixture's binding was backfilled, not captured by its run")
    if run_b.get("scenario_sha256") is None or run_b.get("baseline_sha256") is None:
        why.append("this run has no run manifest")
    if why:
        say("diagnostic only: " + "; ".join(why))
    return not why


def check(snapshot, quiet=False):
    """Returns (outcome, state, accepted): accepted is True only for a MATCH or MATCH-OPEN against a
    captured fixture by a run with a manifest."""
    use_suite(suite_of(snapshot))
    say = (lambda *a: None) if quiet else print
    _, text = read_log(snapshot)
    state = state_of(snapshot)
    run_b = binding_of(snapshot, text)
    missing = [k for k in ("scenario_sha256", "baseline_sha256") if run_b.get(k) is None]
    if missing:
        say("binding: this run records no %s (no .run.json); compared on seed and controls only"
            % " or ".join(k.split("_")[0] for k in missing))
    compat, skipped = [], []
    for s, f in all_fixtures():
        if os.path.normcase(os.path.abspath(f)) in EXCLUDE:
            continue
        mm = mismatches(fixture_meta(f).get("binding"), run_b)
        (skipped if mm else compat).append((s, f, mm))
    for s, f, mm in skipped:
        say("skip  %s: binding differs (%s)" % (f, ", ".join(mm)))
    same = [f for s, f, _ in compat if s == state]
    every = [f for _, f, _ in compat]
    if not same:
        say("%s: NO-FIXTURE (no compatible fixture of this age)" % state)
        return NO_FIXTURE, state, False
    results = {}
    base_root, why_not = baseline_of(snapshot, run_b) if NORMALISE else (None, "normalisation off")
    if base_root is None:
        say("not normalised: %s (object ids and countdowns compared as bytes)" % why_not)

    def show_normalised(f):
        normed, notes = results[f][2], results[f][3]
        if normed:
            say("  normalised %d resource differences (docs/save-diff.md, \"Normalised classes\"):" % len(normed))
            for n in notes:
                say("    " + n)

    # Captured fixtures first, so an identical captured fixture wins over an identical backfilled one.
    same.sort(key=lambda f: not captured(f))
    for f in same:
        rest, normed, opened, notes = normalise(differences(f, snapshot), f, snapshot, base_root)
        hard = []
        for d in rest:
            why = open_reason(d, f, snapshot, base_root)
            (opened if why else hard).append((d, why))
        results[f] = (hard, opened, normed, notes)
        if not hard and opened and FIRST_ACCEPTABLE:
            same = [f]
            break
        if not hard and not opened:
            show_normalised(f)
            say("%s: MATCH (fixture %s%s)" % (state, f, "; normalised" if normed else ""))
            return MATCH, state, accepted(f, run_b, say)
    ok = [f for f in same if not results[f][0]]
    if ok:
        f = min(ok, key=lambda x: (len(results[x][1]), not captured(x)))
        show_normalised(f)
        for d, why in results[f][1]:
            say("  open  %s (%s)" % (label(d), why))
        say("%s: MATCH-OPEN (fixture %s; %d open differences, within their relation)" % (state, f, len(results[f][1])))
        return MATCH_OPEN, state, accepted(f, run_b, say)
    best = min(same, key=lambda x: len(results[x][0]))
    say("closest fixture %s (%d differences)" % (best, len(results[best][0])))
    show_normalised(best)
    if not results[best][2]:
        for n in results[best][3]:
            say("    " + n)
    regress = 0
    for d, _ in results[best][0]:
        # A broken relation is a difference, however much the references vary at those bytes.
        refused = camera_relation(best, snapshot, base_root)[0] if (d[0], d[1]) in KNOWN_OPEN else None
        varies, why = (False, refused) if refused else variation(d, best, same, every, snapshot)
        regress += not varies
        say("  %s  %s: %s" % ("vary" if varies else "DIFF", label(d), why))
    for d, why in results[best][1]:
        say("  open  %s (%s)" % (label(d), why))
    out = UNEXPECTED if regress else UNSEEN
    say("%s: %s" % (state, OUTCOME[out]))
    if out == UNEXPECTED:
        say("(blocks acceptance; a confirmed regression only once traced to our replacements)")
    return out, state, False


def record(snapshot):
    _, text = read_log(snapshot)
    if "TS2HOST_NOHOOKS" not in text:
        sys.exit("not a reference run (TS2HOST_NOHOOKS): fixtures come from the original")
    b = binding_of(snapshot, text)
    if b.get("scenario_sha256") is None or b.get("baseline_sha256") is None:
        sys.exit("no run manifest (.run.json) with scenario and baseline digests: rerun with scenario.ps1")
    out, state, _ = check(snapshot, quiet=True)     # selects the run's suite
    if out == MATCH:
        print("%s: matches a fixture already; not recorded" % state)
        return 0
    dest = os.path.join(FIXTURES, state, str(len(fixtures(state)) + 1))
    # Links into local/snapstore (tools/snapstore.py), as the snapshot's own files: a fixture costs nothing on disk
    # beyond its snapshot, and reads as a copy. Never edit a fixture's files in place.
    store = snapstore.Store()
    store.snap(snapshot, os.path.join(dest, "userdata"))
    shutil.copy(snapshot.rstrip("/\\") + ".ts2host.log", os.path.join(dest, "ts2host.log"))
    chain = chain_of(snapshot)
    if len(chain) > 1:
        # A reload fixture keeps its own baseline's lot packages: records of objects deleted before that save
        # stay in the package as they were, and normalise() compares them with this copy. And its neighbourhood,
        # for the camera record the reload loaded.
        for d, _, fs in os.walk(chain[0]):
            for n in fs:
                rel = os.path.relpath(os.path.join(d, n), chain[0])
                if norm(rel) == NEIGHBORHOOD or ("Lots" in norm(rel).split("/")[:-1] and n.lower().endswith(".package")):
                    os.makedirs(os.path.join(dest, "previous", os.path.dirname(rel)), exist_ok=True)
                    store.link(os.path.join(d, n), os.path.join(dest, "previous", rel))
    keys = ("seed", "controls", "draws", "scenario_sha256", "baseline_sha256") + \
        (("immediate_sha256",) if "immediate_sha256" in b else ())
    meta = {"state": state, "source": os.path.abspath(snapshot), "git": git_rev(), "scenario": SCENARIO,
            "binding": {k: b[k] for k in keys},
            "binding_source": CAPTURED}
    json.dump(meta, open(os.path.join(dest, "fixture.json"), "w"), indent=1)
    print("recorded %s fixture %s from %s (was %s)" % (state, dest, snapshot, OUTCOME[out]))
    return 0


def backfill():
    """Bind lot-save-frames fixtures recorded before run manifests existed: the scenario as of the
    fixture's git revision, the baseline as it is now (refused if any baseline file is newer than the
    fixture), seed and controls from the fixture's own log."""
    newest = max(os.path.getmtime(os.path.join(d, n)) for d, _, fs in os.walk(BASELINE) for n in fs)
    base = tree_digest(BASELINE)
    for s, f in all_fixtures():
        meta = fixture_meta(f)
        if meta.get("binding"):
            continue
        made = os.path.getmtime(os.path.join(f, "ts2host.log"))
        if newest > made:
            print("skip %s: the baseline changed after it was recorded" % f)
            continue
        scen = subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (meta["git"], SCENARIO)],
                              capture_output=True, check=True).stdout
        b = log_binding(open(os.path.join(f, "ts2host.log"), encoding="latin-1").read())
        b.update(scenario_sha256=hashlib.sha256(scen).hexdigest(), baseline_sha256=base)
        meta["binding"] = b
        meta["binding_source"] = ("backfilled 2026-10-12: scenario at git %s, baseline unchanged since "
                                  "before the run, seed and controls from the run's log" % meta["git"])
        json.dump(meta, open(os.path.join(f, "fixture.json"), "w"), indent=1)
        print("bound %s" % f)
    return 0


def recheck():
    """Lot-save-frames runs only (the older runs have no manifest naming their scenario)."""
    saves = os.path.join(ROOT, "local", "saves")
    for n in sorted(os.listdir(saves)):
        snap = os.path.join(saves, n)
        log = snap + ".ts2host.log"
        if not os.path.isdir(snap) or not os.path.isfile(log):
            continue
        with open(log, encoding="latin-1") as f:
            text = f.read()
        if "TS2HOST_NOHOOKS" in text or "seed control:" not in text or "control: post done" not in text:
            continue
        if not os.path.isfile(os.path.join(snap, GENERATED_SIM)):
            continue
        out, state, ok = check(snap, quiet=True)
        tag = "" if out not in (MATCH, MATCH_OPEN) else ("accepted" if ok else "diagnostic only")
        print("%-38s %-8s %-15s %s" % (OUTCOME[out], state, tag, n))
    return 0


def crosscheck(suite, verbose=False, only=None, jobs=1):
    """Each captured fixture of a suite checked as a run against the others (itself left out): how the
    references compare among themselves under the current rules. The fixture's binding stands in for its run
    manifest, the baseline is the one its source run's manifest names (checked against the binding's digest)."""
    global MANIFEST_OVERRIDE, LOG_OVERRIDE, EXCLUDE, FIRST_ACCEPTABLE
    use_suite(suite)
    FIRST_ACCEPTABLE = True
    todo = all_fixtures()
    if only:        # "N" (any state) or "state/N", e.g. reload-1/3, or "state/*"
        todo = [(s, f) for s, f in todo if os.path.basename(f) in only or "%s/%s" % (s, os.path.basename(f)) in only
                or "%s/*" % s in only]
    if jobs > 1:
        # One below-normal-priority child per group of fixtures; their lines are printed as they finish.
        names = ["%s/%s" % (s, os.path.basename(f)) for s, f in todo]
        procs = [subprocess.Popen([sys.executable, "-I", os.path.abspath(__file__), "crosscheck", suite,
                                   "--only", ",".join(names[k::jobs])] + (["-v"] if verbose else []),
                                  stdout=subprocess.PIPE, text=True, creationflags=getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0))
                 for k in range(jobs)]
        for p in procs:
            print(p.communicate()[0], end="")
        return 0
    counts = {}
    for s, f in todo:
        meta = fixture_meta(f)
        b = meta.get("binding")
        if not b or not captured(f):
            print("%-38s %-8s %s" % ("(skipped: binding not captured)", s, f))
            continue
        src = (meta.get("source") or "").rstrip("/\\") + ".run.json"
        base = None
        if os.path.isfile(src):
            with open(src, encoding="utf-8-sig") as fh:
                base = json.load(fh).get("baseline")
        if base is None:        # the source run's manifest is gone: a prepared baseline with the bound digest
            here = os.path.join(ROOT, "local", "baselines")
            base = next((os.path.join(here, n) for n in sorted(os.listdir(here)) if os.path.isdir(os.path.join(here, n))
                         and _tree_digest(os.path.join(here, n)) == b["baseline_sha256"]), None) \
                if os.path.isdir(here) else None
        # A reload fixture's binding names the chain's root; its manifest named its own (immediate) baseline.
        MANIFEST_OVERRIDE = {"scenario": "@" + suite, "seed": hex(b["seed"]), "baseline": base,
                             "scenario_sha256": b["scenario_sha256"],
                             "baseline_sha256": b.get("immediate_sha256", b["baseline_sha256"])}
        LOG_OVERRIDE = os.path.join(f, "ts2host.log")
        EXCLUDE = {os.path.normcase(os.path.abspath(f))}
        try:
            out, state, _ = check(os.path.join(f, "userdata"), quiet=not verbose)
        finally:
            MANIFEST_OVERRIDE, LOG_OVERRIDE, EXCLUDE = None, None, set()
        counts[OUTCOME[out]] = counts.get(OUTCOME[out], 0) + 1
        print("%-38s %-8s %s" % (OUTCOME[out], state, f))
    print("; ".join("%s %d" % kv for kv in sorted(counts.items())))
    return 0


def list_fixtures():
    for name in SUITES:
        use_suite(name)
        list_suite()
    return 0


def list_suite():
    for s, f in all_fixtures():
        meta = fixture_meta(f)
        how = ("UNBOUND" if not meta.get("binding") else
               "captured" if captured(f) else "backfilled (diagnostic only)")
        print(f, meta.get("git"), how)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("action", choices=["record", "check", "recheck", "list", "backfill", "crosscheck"])
    ap.add_argument("snapshot", nargs="?", help="a snapshot folder (crosscheck: a suite name, default lot-resave)")
    ap.add_argument("--log", help="the run's host log, when it is not <snapshot>.ts2host.log")
    ap.add_argument("--bytes", action="store_true", help="no normalised classes: object ids and countdowns as bytes")
    ap.add_argument("-v", action="store_true", help="crosscheck: each fixture's full report")
    ap.add_argument("--jobs", type=int, default=1, help="crosscheck: parallel children (below-normal priority)")
    ap.add_argument("--only", help="crosscheck: these fixture numbers only (comma-separated)")
    a = ap.parse_args(argv)
    global LOG_OVERRIDE, NORMALISE
    LOG_OVERRIDE, NORMALISE = a.log, not a.bytes
    if a.action == "crosscheck":
        return crosscheck(a.snapshot or "lot-resave", a.v, a.only.split(",") if a.only else None, a.jobs)
    if a.action in ("list", "backfill", "recheck"):
        return {"list": list_fixtures, "backfill": backfill, "recheck": recheck}[a.action]()
    if not a.snapshot:
        ap.error("snapshot required")
    if a.action == "record":
        return record(a.snapshot)
    out, _, ok = check(a.snapshot)
    return DIAGNOSTIC if out in (MATCH, MATCH_OPEN) and not ok else out


if __name__ == "__main__":
    sys.exit(main())
