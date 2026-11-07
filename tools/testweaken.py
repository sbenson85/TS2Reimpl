"""Test-weakening candidates: did a branch loosen a difftest that exists on the base?

    tools/py -I tools/testweaken.py <branch> [--base main]       # what guard.py runs on each changed tests/ file
    tools/py -I tools/testweaken.py --history 40 [-v]            # calibration: the last N lane merges on main

For every DIFF_TEST (and wrapper-macro test) of a tests/ file the branch modifies, base against branch:
  removed      the test is gone from the file, its name from the branch's tests/, and its rva (a hex literal or a
               hex constant) too: a test folded into another that names the function in its comment is no warning
  checks       fewer comparison / failure calls: ctx.Fail, Fail*, Compare*, Same*, Check*, Expect*, Dump*, Assert*,
               Verify*, memcmp; counted in the test's block plus every helper it calls in the file or its tests/
               headers (each helper once), so moving checks into a helper is neutral
  loop bound   a `for` loop's numeric bound (or a k-constant naming seeds / iterations / cases / rounds) went down; a
               loop is paired with the base's by its header text with the numbers taken out
  argument     a line of the test's own block that differs from a base line only in its decimal numbers, with one
               of them lower (`ChildCases<X>(ctx, 150)` -> `(ctx, 15)`)
  new skip     a line the branch adds to an existing test, a helper the base has, or a helper an existing test calls:
               `continue;` or `return;` (not on or right after a failure call: `ctx.Fail(...); return;`), a mask
               (`&= ~`, `& ~`), a name starting skip / ignore / mask / exclude (`kSkip`, `m_mask`; not `KindMask`),
               `#if 0`, or a field set to 0 / nullptr / false that a later check in the same block compares; a line
               that kept the line before it from the base (moved into a new helper) is not new
Each is a warning ("test-weakening candidate: <test> <what>"), never an error: rewrites that move checks around are
legitimate. The lane answers each in its report (why it is not a weakening, or what replaced it).
Not seen: a weaker comparison inside an unchanged call (a mask passed by an existing argument), a check made weaker in
place (`!=` -> a looser compare, a wider tolerance), fewer seeds from a changed random generator, a renamed test whose
rva is still named (silent), weakening in a tests/ header's helpers when only the header changed (its includers are
not re-analysed), an expected value changed to match ours. A deleted test file counts as every test in it removed.
Calibration (tools/py -I tools/testweaken.py --history 40, 2026-10-23): 6 warnings over 40 lane merges, 36 with none,
at most 2 per merge; the coordinator's strengthening 92c54cde gives none.
"""
import argparse
import difflib
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cppblocks  # noqa: E402
import gitblob  # noqa: E402
import tsdb  # noqa: E402

CHECK_RE = re.compile(r"\b(ctx\s*\.\s*Fail|Fail\w*|Compare\w*|Same\w*|Check\w*|Expect\w*|Dump\w*|Assert\w*|Verify\w*|"
                      r"memcmp)\s*(?:<[^;{}()]*>)?\s*\(")
CALL_RE = re.compile(r"\b([A-Za-z_]\w*)\s*(?:<[^;{}()]*>)?\s*\(")
FOR_RE = re.compile(r"\bfor\s*\(([^;()]*(?:\([^()]*\))?[^;()]*);([^;]*);")
BOUND_RE = re.compile(r"(<=?|!=)\s*([0-9][0-9'_]*[uU]?|[A-Za-z_]\w*)\s*$")
COUNT_NAME = re.compile(r"(?i)seed|iter|case|round|trial|count|reps|runs|sample|step|loop|times")
CONST_DEF = re.compile(r"\b(k[A-Z]\w*|[A-Z][A-Z0-9_]{2,})\s*(?:=|\{)\s*([0-9]+)\b")
SKIP_RE = re.compile(r"\bcontinue\s*;|\breturn\s*;|&=\s*~|&\s*~|"
                     r"\b(?:[kgs]|m_)?(?i:skip|ignore|exclude|mask)\w*")  # a name that starts so: not KindMask
HEX_RE = re.compile(r"\b0[xX]([0-9a-fA-F]{4,8})\b")
HEX_CONST = re.compile(r"\b([A-Za-z_]\w*)\s*(?:=|\{)\s*0[xX]([0-9a-fA-F]+)\b")
ZERO_RE = re.compile(r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*\w+(?:\s*\[[^\]]*\])?\s*=\s*(?:0[uUlL]*|nullptr|NULL|false|0x0+)\s*;")
DECIMAL = re.compile(r"(?<![\w.])([0-9]+)(?![\w.x])")
NOT_HELPERS = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "reinterpret_cast", "const_cast",
               "decltype", "catch", "DIFF_TEST"}


def git(*a):
    return subprocess.run(["git", *a], cwd=tsdb.ROOT, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def show(rev, path):
    try:
        return gitblob.show(rev, path, cwd=tsdb.ROOT, errors="replace")
    except subprocess.CalledProcessError:
        return None


class Unit:
    """A test or helper: its text span (offsets into the file) and body (blanked)."""

    def __init__(self, name, kind, start, end, body_start):
        self.name, self.kind, self.start, self.end, self.body_start = name, kind, start, end, body_start


class Model:
    """One version of a test file: tests, helpers (this file's and its tests/ headers'), constants."""

    def __init__(self, text, headers=()):
        self.text = text
        self.blank = cppblocks.blank(text)
        defs = "\n".join(headers)
        self.tests = {}
        for t in cppblocks.test_blocks(text, defs):
            self.tests.setdefault(t.name, Unit(t.name, "test", t.start, t.end, t.start))
        self.helpers = {}  # name -> [blanked bodies]
        for src in (text,) + tuple(headers):
            b = cppblocks.blank(src)
            for blk in cppblocks.blocks(src):
                if blk.kind == "function":
                    self.helpers.setdefault(blk.qual.split("::")[-1], []).append(b[blk.open:blk.close + 1])
        self.helper_units = [Unit(blk.qual.split("::")[-1], "helper", blk.start, blk.close + 1, blk.open)
                             for blk in cppblocks.blocks(text) if blk.kind == "function"]
        self.consts = {}
        for src in (text,) + tuple(headers):
            for m in CONST_DEF.finditer(cppblocks.blank(src)):
                self.consts.setdefault(m.group(1), int(m.group(2)))
            for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(k[A-Z]\w*|[A-Z][A-Z0-9_]{2,})[ \t]+([0-9]+)\b", src, re.M):
                self.consts.setdefault(m.group(1), int(m.group(2)))
        self._memo = {}

    def body(self, unit):
        return self.blank[unit.body_start:unit.end]

    def reach(self, body, seen=None):
        """Bodies of the helpers `body` calls, transitively (each once): [(name, body)]."""
        seen = set() if seen is None else seen
        out = []
        for m in CALL_RE.finditer(body):
            name = m.group(1)
            if name in seen or name in NOT_HELPERS or name not in self.helpers:
                continue
            seen.add(name)
            for hb in self.helpers[name]:
                out.append((name, hb))
                out += self.reach(hb, seen)
        return out

    def stats(self, unit):
        """(checks, [(loop key, bound)], helper names) over the test and the helpers it reaches."""
        if unit.name in self._memo:
            return self._memo[unit.name]
        body = self.body(unit)
        parts = [(None, body)] + self.reach(body, {unit.name})
        checks = sum(len(CHECK_RE.findall(b)) for _, b in parts)
        loops = []
        for where, b in parts:
            for m in FOR_RE.finditer(b):
                cond = m.group(2).strip()
                bm = BOUND_RE.search(cond)
                if not bm:
                    continue
                tok = bm.group(2).replace("'", "").replace("_", "").rstrip("uU")
                value = int(tok) if tok.isdigit() else self.consts.get(bm.group(2)) if COUNT_NAME.search(bm.group(2)) \
                    or bm.group(2).startswith("k") else None
                if value is None:
                    continue
                key = (where, re.sub(r"\s+", " ", DECIMAL.sub("#", m.group(1) + ";" + cond[:bm.start()])))
                loops.append((key, value, f"for ({m.group(1).strip()}; {cond})"))
        self._memo[unit.name] = (checks, loops, [n for n, _ in parts[1:]])
        return self._memo[unit.name]


def _pair_loops(old, new):
    """Loops of the base and the branch paired by key, in order: [(old, new)]."""
    pairs, pool = [], {}
    for item in new:
        pool.setdefault(item[0], []).append(item)
    for item in old:
        if pool.get(item[0]):
            pairs.append((item, pool[item[0]].pop(0)))
    return pairs


def added_lines(old_text, new_text):
    """[(offset in new_text, line)] of the lines the new text adds (difflib on stripped lines)."""
    a = [l.strip() for l in old_text.splitlines()]
    blines = new_text.splitlines(keepends=True)
    b = [l.strip() for l in blines]
    offsets, at = [], 0
    for l in blines:
        offsets.append(at)
        at += len(l)
    out = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag in ("insert", "replace"):
            out += [(offsets[j], blines[j].rstrip("\r\n")) for j in range(j1, j2)]
    return out


def prev_line(text, at):
    """The stripped non-blank line before offset `at` (a line start) in `text`, or ""."""
    for line in reversed(text[:at].splitlines()):
        if line.strip():
            return line.strip()
    return ""


def line_pairs(text):
    """{(previous non-blank line, line)} of `text`, stripped: a line the branch moved keeps its predecessor."""
    out, prev = set(), ""
    for line in text.splitlines():
        if line.strip():
            out.add((prev, line.strip()))
            prev = line.strip()
    return out


def changed_numbers(old_body, new_body):
    """(old line, new line, old number, new number) for lines of new_body that differ from a line of old_body only in
    decimal numbers, with a number that went down."""
    def norm(l):
        return re.sub(r"\s+", " ", DECIMAL.sub("#", l.strip()))
    old_by = {}
    for l in old_body.splitlines():
        if DECIMAL.search(l):
            old_by.setdefault(norm(l), []).append(l.strip())
    old_set = {l.strip() for l in old_body.splitlines()}
    out = []
    for l in new_body.splitlines():
        s = l.strip()
        if s in old_set or not DECIMAL.search(s):
            continue
        for o in old_by.get(norm(s), []):
            on = [int(x) for x in DECIMAL.findall(o)]
            nn = [int(x) for x in DECIMAL.findall(s)]
            down = [(x, y) for x, y in zip(on, nn) if y < x]
            if len(on) == len(nn) and down and all(y <= x for x, y in zip(on, nn)):
                out.append((o, s, down[0][0], down[0][1]))
                break
    return out


def rva_of(expr, texts):
    """The rva a test's rva expression gives (a hex literal, or a constant defined in one of `texts`), or None."""
    m = re.fullmatch(r"\s*0[xX]([0-9a-fA-F]+)[uUlL]*\s*", expr or "")
    if m:
        return int(m.group(1), 16)
    for t in texts:
        for c in HEX_CONST.finditer(t):
            if c.group(1) == (expr or "").strip():
                return int(c.group(2), 16)
    return None


def rva_pattern(rva):
    return rf"(?i)\b0x0*{rva:x}\b"


def analyse(path, old_text, new_text, old_headers=(), new_headers=(), test_exists=None, rva_named=None):
    """Warnings for one tests/ file: base text against branch text. `test_exists(name)` says whether a test name is
    still registered somewhere on the branch, `rva_named(rva)` whether the branch's tests/ still name the rva
    (defaults: only this file is looked at)."""
    old, new = Model(old_text, old_headers), Model(new_text, new_headers)
    out = []
    old_rvas = {t.name: t.rva for t in cppblocks.test_blocks(old_text, "\n".join(old_headers))}
    for name, ou in old.tests.items():
        nu = new.tests.get(name)
        if nu is None:
            if test_exists and test_exists(name):
                continue  # moved to another file
            rva = rva_of(old_rvas.get(name), (old_text,) + tuple(old_headers))
            if rva is not None and (re.search(rva_pattern(rva), new_text) or (rva_named and rva_named(rva))):
                continue  # folded into another test that still names the function
            out.append(f"{name}: removed (not registered anywhere in the branch's tests/"
                       + (f", rva 0x{rva:x} named nowhere there)" if rva is not None else ")"))
            continue
        oc, ol, _ = old.stats(ou)
        nc, nl, _ = new.stats(nu)
        if nc < oc:
            out.append(f"{name}: {oc} -> {nc} comparison/failure calls (Fail, Compare*, Same*, Check*, Expect*, Dump*, "
                       "memcmp; in the test and the helpers it calls)")
        for (ok_, ov, otext), (nk, nv, ntext) in _pair_loops(ol, nl):
            if nv < ov:
                out.append(f"{name}: loop bound {ov} -> {nv}: `{ntext[:80]}`")
        for o, n, ov, nv in changed_numbers(old.body(ou), new.body(nu)):
            if "for" in n and FOR_RE.search(n):
                continue  # reported as a loop bound
            out.append(f"{name}: argument {ov} -> {nv}: `{n[:90]}`")
    # constants that count seeds / iterations
    for k, v in old.consts.items():
        if k in new.consts and new.consts[k] < v and COUNT_NAME.search(k):
            users = [n for n, u in new.tests.items() if re.search(rf"\b{k}\b", new.text[u.start:u.end])] or ["(file)"]
            out.append(f"{', '.join(users[:3])}: constant {k} {v} -> {new.consts[k]}")
    # lines added to tests that exist on the base, and to helpers that exist on the base or that such a test calls
    old_helpers = {u.name for u in old.helper_units}
    users_of = {}
    for n, u in new.tests.items():
        if n in old.tests:
            for h in new.stats(u)[2]:
                if n not in users_of.setdefault(h, []):
                    users_of[h].append(n)
    units = [u for n, u in new.tests.items() if n in old.tests] + \
        [u for u in new.helper_units if u.name in old_helpers or u.name in users_of]
    old_pairs = line_pairs(old_text)
    for at, line in added_lines(old_text, new_text):
        unit = next((u for u in units if u.start <= at < u.end), None)
        if unit is None:
            continue
        prev = prev_line(new_text, at)
        if (prev, line.strip()) in old_pairs:
            continue  # moved here from elsewhere in the file (a helper split), with the line before it
        code = new.blank[at:at + len(line)]
        what = None
        if re.match(r"\s*#\s*if\s+0\b", line):
            what = "#if 0"
        else:
            m = SKIP_RE.search(code)
            if m and m.group(0).startswith(("return", "continue")) and (CHECK_RE.search(code) or CHECK_RE.search(
                    cppblocks.blank(prev))):
                m = None  # `if (!Compare(...)) return;`, `ctx.Fail(...); return;`: stops after a failure
            if m:
                what = m.group(0).strip()
            else:
                z = ZERO_RE.search(code)
                if z and re.search(rf"\b{re.escape(z.group(1))}\b", "\n".join(
                        l for l in new.blank[at + len(line):unit.end].splitlines() if CHECK_RE.search(l))):
                    what = f"{z.group(0).strip()} before a check on {z.group(1)}"
        if what:
            who = unit.name if unit.kind == "test" else \
                f"{', '.join(users_of.get(unit.name, ['(no test of the base calls it)'])[:3])} (via {unit.name})"
            out.append(f"{who}: new `{what}`: {line.strip()[:90]}")
    return out


def headers_of(rev, path, text):
    out = []
    for inc in re.findall(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', text or "", re.M):
        p = (Path(path).parent / inc).as_posix()
        if p.startswith("tests/"):
            h = show(rev, p)
            if h is not None:
                out.append(h)
    return out


def branch_check(branch, base="main", mb=None):
    """Warnings for every tests/ file the branch modifies (base: the merge base)."""
    mb = mb or git("merge-base", base, branch).strip()
    out = []
    for line in git("diff", "--name-status", mb, branch, "--", "tests/").splitlines():
        st, *paths = line.split("\t")
        path = paths[-1]
        if not st.startswith(("M", "R", "D")) or not path.endswith((".cpp", ".h")):
            continue
        old_path = paths[0]
        old_text, new_text = show(mb, old_path), "" if st.startswith("D") else show(branch, path)
        if old_text is None or new_text is None:
            continue

        def exists(name, _branch=branch):
            r = subprocess.run(["git", "grep", "-q", "-w", name, _branch, "--", "tests/"], cwd=tsdb.ROOT)
            return r.returncode == 0

        def named(rva, _branch=branch):
            r = subprocess.run(["git", "grep", "-q", "-i", "-E", rf"\b0x0*{rva:x}\b", _branch, "--", "tests/"],
                               cwd=tsdb.ROOT)
            return r.returncode == 0
        for w in analyse(path, old_text, new_text, headers_of(mb, old_path, old_text),
                         headers_of(branch, path, new_text), exists, named):
            out.append(f"test-weakening candidate: {path}: {w}")
    return out


def history(n, verbose):
    merges = git("log", "--merges", "--first-parent", "main", f"-{n}", "--format=%H %s").splitlines()
    total = 0
    per = []
    for line in merges:
        sha, subject = line.split(" ", 1)
        p1, p2 = git("rev-parse", f"{sha}^1", f"{sha}^2").split()
        mb = git("merge-base", p1, p2).strip()
        ws = branch_check(p2, mb=mb)
        per.append(len(ws))
        total += len(ws)
        print(f"{sha[:8]} {len(ws):2} {subject[:80]}")
        if verbose:
            for w in ws:
                print("    " + w)
    print(f"{len(merges)} merges, {total} warnings; per merge: {sorted(per)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("branch", nargs="?")
    ap.add_argument("--base", default="main")
    ap.add_argument("--history", type=int, help="run over the last N merges on main (calibration)")
    ap.add_argument("-v", action="store_true", help="with --history: print each warning")
    args = ap.parse_args()
    if args.history:
        history(args.history, args.v)
        return
    if not args.branch:
        ap.error("a branch, or --history N")
    ws = branch_check(args.branch, args.base)
    for w in ws:
        print("warning: " + w)
    print(f"{len(ws)} test-weakening candidates")


if __name__ == "__main__":
    main()
