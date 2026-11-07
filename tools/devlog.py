"""Reads and compares the host's device call logs (ts2devlog.txt; TS2HOST_DEVLOG, docs/ui-testing.md).

    tools/py -I tools/devlog.py summary LOG
    tools/py -I tools/devlog.py show LOG FRAME [--kind s|a]
    tools/py -I tools/devlog.py compare A B [--ref2 C] [--align LABEL|auto] [--frames 900,4000-4002] [--shift N]
                                            [--max 20] [--context 2] [--loose-from auto|none|N]
    tools/py -I tools/devlog.py trace A B [--ticks 0-10] [--max 5] [--context 3] [--loose]

trace: the frame clock's trace lines (TS2HOST_FRAMECLOCK_OPTS=trace=<from>-<to>): "ct <tick> <thread> <clock> <caller>
<time>" per clock read and "cs <tick> <thread> <import>(<arguments>) <caller> -> <result>" per scheduler call, at most
200 per thread and tick. Threads are t<index> (the scheduler's) or x<id> (outside the lockstep). Per tick, the game
threads' lines are compared in order and the outside threads' as a set (ids dropped); the first difference is printed.

compare: A is a reference run, B the run under test, C (optional) a second reference run. Frames are matched by
number, or with --align LABEL relative to the first frame of the `full LABEL` request (the "devlog <n> LABEL" control
command; a `mark` with that label also works) in each file. --align auto aligns piecewise: every label both files have
(full requests and marks) starts a stretch matched relative to it, until the next one; frames before the first are
matched by number. For each frame both files have:
  - stream (state, draws, dynamic uploads, Present; ordered): the digests are compared; where both files have the
    frame in full, the first differing call is printed (A's and B's text, with --context calls before it);
  - async (resource creation, static uploads, calls from other threads): compared as a set (the calls only one side
    made are printed); their order counts only with --ref2, in frames where A and C made them in the same order.
With --ref2, a frame where A and C (two reference runs) differ in the same part is reference noise, not a difference:
B is accepted there as long as it is no worse (the trace suite's rule, docs/verification.md). A differing frame that
equals B's frame a few frames off (within 2, or --shift N) says so ("B's frame n+1 has it: shifted"); with --shift N
such frames count as shifted, not as differences. Frames only one file has are counted.

The loose rule: from the frame --loose-from names (default 'auto': the first frame from 1000 on with 1000+ async calls,
the neighbourhood's resource load; 'none': never), frames compare by draw, present and async call counts only, since
two reference runs differ there in draw order and static upload contents (docs/ui-testing.md, "What the frame clock
guarantees"). Frames before it are compared as above.

A frame number written more than once (frame 0: the game creates its Direct3D object three times, and each device's
release ends the frame) is one frame: its parts are merged in file order. The frame clock's "clk" lines (TS2HOST_
FRAMECLOCK with the device log) are compared too: the first tick whose time or schedule differs is named (a
diagnostic: it doesn't change the exit status). Exit status 1 when any frame differs, else 0. Last line:
"devlog compare: ...".
"""
import argparse
import bisect
import collections
import hashlib
import re
import sys

MASK = (1 << 64) - 1


def chain(a, b):
    """Two digests (hex strings) in order, as one (for merged frame parts)."""
    if not a:
        return b
    return hashlib.sha1(f"{a}:{b}".encode()).hexdigest()[:16]


class Frame:
    __slots__ = ("n", "stream_count", "stream_hash", "async_count", "async_sum", "async_seq", "draws", "presents", "s", "a",
                 "full", "parts")

    def __init__(self, n):
        self.n = n
        self.stream_count = self.async_count = self.draws = self.presents = 0
        self.stream_hash = self.async_sum = self.async_seq = ""
        self.s, self.a = [], []
        self.full = False
        self.parts = 0  # digest lines merged into this frame

    def add_digest(self, stream_count, stream_hash, async_count, async_sum, async_seq, draws, presents):
        if not self.parts:
            self.stream_hash, self.async_sum, self.async_seq = stream_hash, async_sum, async_seq
        else:
            # A later part: the stream digests chain in order, the async sums add up (a part without calls of a kind
            # changes nothing of it).
            if stream_count:
                self.stream_hash = chain(self.stream_hash, stream_hash) if self.stream_count else stream_hash
            if async_count:
                self.async_sum = f"{(int(self.async_sum, 16) + int(async_sum, 16)) & MASK:016x}"
                self.async_seq = chain(self.async_seq, async_seq) if self.async_count else async_seq
        self.stream_count += stream_count
        self.async_count += async_count
        self.draws += draws
        self.presents += presents
        self.parts += 1


class Log:
    def __init__(self, path):
        self.path = path
        self.header = ""
        self.frames = {}
        self.fulls = []  # (label, first, count)
        self.marks = []  # (frame, label)
        self.clocks = {}  # tick -> (deterministic part, the rest)
        self.traces = {}  # tick -> trace lines ("ct"/"cs", TS2HOST_FRAMECLOCK_OPTS trace=), in file order
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                self._line(line.rstrip("\r\n"))

    def _line(self, line):
        kind, _, rest = line.partition(" ")
        if kind == "frame":
            # "<n> stream <count> <hash> async <count> <sum> <seq> draws <d> presents <p>"
            parts = rest.split()
            fr = self.frames.setdefault(int(parts[0]), Frame(int(parts[0])))
            fr.add_digest(int(parts[2]), parts[3], int(parts[5]), parts[6], parts[7], int(parts[9]), int(parts[11]))
        elif kind in ("s", "a"):
            n, _, text = (rest.split(" ", 2) + ["", ""])[:3]
            fr = self.frames.setdefault(int(n), Frame(int(n)))
            fr.full = True
            (fr.s if kind == "s" else fr.a).append(text)
        elif kind == "full":
            label, first, count = rest.split()
            self.fulls.append((label, int(first), int(count)))
        elif kind == "mark":
            n, label = rest.split(" ", 1)
            self.marks.append((int(n), label))
        elif kind == "clk":
            n, _, fields = rest.partition(" ")
            det, _, info = fields.partition(" |")
            self.clocks[int(n)] = (det.strip(), info.strip())
        elif kind in ("ct", "cs"):
            n, _, text = rest.partition(" ")
            self.traces.setdefault(int(n), []).append(f"{kind} {text}")
        elif kind == "devlog":
            self.header = line

    def anchor(self, label):
        for name, first, _ in self.fulls:
            if name == label:
                return first
        for n, name in self.marks:
            if name == label:
                return n
        return None

    def labels(self):
        """Every label's first frame (full requests first, then marks)."""
        out = {}
        for name, first, _ in self.fulls:
            out.setdefault(name, first)
        for n, name in self.marks:
            out.setdefault(name, n)
        return out


class Alignment:
    """Maps a frame of A to the other file's frame: one offset, or piecewise from shared labels (--align auto)."""

    def __init__(self, starts=None, offsets=None, offset=0):
        self.starts = starts or []  # A frames where a stretch begins (sorted)
        self.offsets = offsets or []
        self.offset = offset  # before the first stretch (or everywhere)

    def __call__(self, n):
        i = bisect.bisect_right(self.starts, n) - 1
        return n + (self.offsets[i] if i >= 0 else self.offset)

    def describe(self):
        return ", ".join(f"from A {s}: {o:+d}" for s, o in zip(self.starts, self.offsets))


def make_alignment(a, other, align):
    """None (with a message) when a label is missing."""
    if not align:
        return Alignment(), None
    if align == "auto":
        la, lo = a.labels(), other.labels()
        pairs = sorted((la[k], lo[k] - la[k]) for k in la if k in lo)
        return Alignment([p[0] for p in pairs], [p[1] for p in pairs]), None
    fa, fx = a.anchor(align), other.anchor(align)
    if fa is None or fx is None:
        return None, f"devlog compare: no '{align}' in {a.path if fa is None else other.path}"
    return Alignment(offset=fx - fa), None


def parse_frames(spec):
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.append((int(a), int(b)))
        elif part:
            out.append((int(part), int(part)))
    return out


def in_frames(n, ranges):
    return not ranges or any(a <= n <= b for a, b in ranges)


def stream_equal(x, y):
    return x.stream_count == y.stream_count and x.stream_hash == y.stream_hash


def async_set_equal(x, y):
    return x.async_count == y.async_count and x.async_sum == y.async_sum


def async_order_equal(x, y):
    return async_set_equal(x, y) and x.async_seq == y.async_seq


def frame_equal(x, y):
    return stream_equal(x, y) and async_set_equal(x, y)


def loose_equal(x, y):
    """The loose rule (--loose-from): the same draws and presents, and as many async calls (what is drawn, not in which
    order, nor what the buffers hold)."""
    return x.draws == y.draws and x.presents == y.presents and x.async_count == y.async_count


LOOSE_AUTO_FIRST, LOOSE_AUTO_ASYNC = 1000, 1000


def loose_start(a, spec):
    """The first frame compared by the loose rule, or None (every frame exact). spec: a frame number, 'none', or 'auto':
    the first frame from LOOSE_AUTO_FIRST on with at least LOOSE_AUTO_ASYNC async calls, the neighbourhood's resource
    load (frame 1239 in the 2026-10-19 reference runs; the menu, frame 900 included, stays exact)."""
    if spec in (None, "none"):
        return None
    if spec != "auto":
        return int(spec)
    for n in sorted(a.frames):
        if n >= LOOSE_AUTO_FIRST and a.frames[n].async_count >= LOOSE_AUTO_ASYNC:
            return n
    return None


def first_difference(xs, ys):
    for i, (x, y) in enumerate(zip(xs, ys)):
        if x != y:
            return i
    return min(len(xs), len(ys)) if len(xs) != len(ys) else None


def set_difference(xs, ys):
    cx, cy = collections.Counter(xs), collections.Counter(ys)
    return list((cx - cy).elements()), list((cy - cx).elements())


def shifted_to(fa, b, nb, reach, equal=None):
    """The offset k (smallest first, -k before +k) within `reach` where B's frame nb+k equals A's frame, else None."""
    equal = equal or frame_equal
    for d in range(1, reach + 1):
        for k in (-d, d):
            fb = b.frames.get(nb + k)
            if fb is not None and equal(fa, fb) and (fa.stream_count or fa.async_count):
                return k
    return None


CLK_INPUT = re.compile(r"\b(msgs|in)=(\S+)")


def compare_inputs(a, b, align, ranges, out):
    """The first tick whose input fields (msgs=, in=: messages taken and real input state read, from outside the game)
    differ: a diagnostic line, informational only. None when the logs have no such fields."""
    found = False
    for n in sorted(a.clocks):
        if not in_frames(n, ranges):
            continue
        other = b.clocks.get(align(n))
        if other is None:
            continue
        fa, fb = dict(CLK_INPUT.findall(a.clocks[n][1])), dict(CLK_INPUT.findall(other[1]))
        if not fa or not fb:
            continue
        found = True
        if fa != fb:
            shown = " ".join(f"{k}={fa.get(k)}/{fb.get(k)}" for k in ("msgs", "in") if fa.get(k) != fb.get(k))
            print(f"input: first difference at tick {n} (A/B): {shown}", file=out)
            return n
    if found:
        print("input: the same in every tick compared", file=out)
    return None


def compare_clocks(a, b, align, ranges, out):
    """The first tick whose deterministic clk fields differ (a diagnostic line), or None when either has no clk lines."""
    if not a.clocks or not b.clocks:
        return None
    compare_inputs(a, b, align, ranges, out)
    compared = 0
    for n in sorted(a.clocks):
        if not in_frames(n, ranges):
            continue
        other = b.clocks.get(align(n))
        if other is None:
            continue
        compared += 1
        if a.clocks[n][0] != other[0]:
            print(f"clock: first difference at tick {n}" + (f" (B {align(n)})" if align(n) != n else "") + ":", file=out)
            print(f"    A: {a.clocks[n][0]} | {a.clocks[n][1]}", file=out)
            print(f"    B: {other[0]} | {other[1]}", file=out)
            return n
    print(f"clock: {compared} ticks agree", file=out)
    return -1


def compare(a, b, c=None, align=None, frames=None, max_frames=20, context=0, shift=0, out=None, loose_from="auto"):
    out = out or sys.stdout
    loose = loose_start(a, loose_from)
    loose_only = 0  # loose frames that differ exactly but pass the loose rule
    if loose is not None:
        print(f"loose from frame {loose}" + (" (auto: the first big resource load after the menu)" if loose_from == "auto"
                                             else "") + ": draws, presents and async call counts only", file=out)
    maps = {}
    for name, log in (("B", b), ("C", c)):
        if log is None:
            continue
        m, error = make_alignment(a, log, align)
        if m is None:
            print(error, file=out)
            return 2
        maps[name] = m
    if align == "auto" and maps["B"].starts:
        print(f"aligned (B): {maps['B'].describe()}", file=out)
    ranges = parse_frames(frames) if frames else []
    compared = differing = noise = order_notes = shifted = 0
    only_a = only_b = 0
    stream_diffs = async_diffs = 0
    reports = []
    matched_b = set()
    for n in sorted(a.frames):
        if not in_frames(n, ranges):
            continue
        fa = a.frames[n]
        nb = maps["B"](n)
        fb = b.frames.get(nb)
        if fb is None:
            only_a += 1
            continue
        matched_b.add(nb)
        fc = c.frames.get(maps["C"](n)) if c is not None else None
        compared += 1
        if loose is not None and n >= loose:
            if loose_equal(fa, fb):
                loose_only += not frame_equal(fa, fb)
                continue
            lines = [f"  loose: draws {fa.draws} / {fb.draws}, presents {fa.presents} / {fb.presents}, async calls "
                     f"{fa.async_count} / {fb.async_count}"]
            k = shifted_to(fa, b, nb, max(shift, 2), loose_equal)
            if k is not None:
                lines.insert(0, f"  B's frame {nb + k} has it: shifted {k:+d}")
                if abs(k) <= shift:
                    shifted += 1
                    continue
            differing += 1
            stream_diffs += fa.draws != fb.draws or fa.presents != fb.presents
            async_diffs += fa.async_count != fb.async_count
            if len(reports) < max_frames:
                reports.append([f"frame {n}" + (f" (B {nb})" if nb != n else "") + ":"] + lines)
            continue
        lines = []
        frame_noise = 0
        s_diff = a_diff = False
        # Stream: ordered.
        if not stream_equal(fa, fb):
            ref_noise = fc is not None and not stream_equal(fa, fc)
            if ref_noise:
                frame_noise += 1
                lines.append(f"  stream differs, but so do the reference runs (noise)")
            else:
                s_diff = True
                if fa.full and fb.full:
                    i = first_difference(fa.s, fb.s)
                    if i is None:  # same text, different digest: a line the log could not hold (not expected)
                        i = len(fa.s)
                    lines.append(f"  stream call {i} (of {fa.stream_count} / {fb.stream_count}):")
                    for k in range(max(0, i - context), i):
                        lines.append(f"      {k}: {fa.s[k]}")
                    lines.append(f"    A {i}: {fa.s[i] if i < len(fa.s) else '(end)'}")
                    lines.append(f"    B {i}: {fb.s[i] if i < len(fb.s) else '(end)'}")
                else:
                    lines.append(f"  stream digest differs ({fa.stream_count} calls {fa.stream_hash} / "
                                 f"{fb.stream_count} calls {fb.stream_hash}; no full list in both)")
        # Async: a set, unless both references made them in the same order.
        set_same = async_set_equal(fa, fb)
        ordered = fc is not None and async_order_equal(fa, fc)
        if not set_same or (ordered and not async_order_equal(fa, fb)):
            ref_noise = fc is not None and not async_set_equal(fa, fc)
            if ref_noise:
                frame_noise += 1
                lines.append("  async calls differ, but so do the reference runs (noise)")
            else:
                a_diff = True
                what = "set" if not set_same else "order (the reference runs agree on it)"
                lines.append(f"  async {what} differs ({fa.async_count} / {fb.async_count} calls)")
                if fa.full and fb.full:
                    if not set_same:
                        only_in_a, only_in_b = set_difference(fa.a, fb.a)
                        for text in only_in_a[:5]:
                            lines.append(f"    only A: {text}")
                        for text in only_in_b[:5]:
                            lines.append(f"    only B: {text}")
                    else:
                        i = first_difference(fa.a, fb.a)
                        if i is not None:
                            lines.append(f"    A {i}: {fa.a[i] if i < len(fa.a) else '(end)'}")
                            lines.append(f"    B {i}: {fb.a[i] if i < len(fb.a) else '(end)'}")
        elif not async_order_equal(fa, fb):
            order_notes += 1
        if s_diff or a_diff:
            k = shifted_to(fa, b, nb, max(shift, 2))
            if k is not None:
                lines.insert(0, f"  B's frame {nb + k} has it: shifted {k:+d}")
            if k is not None and abs(k) <= shift:
                shifted += 1
                continue
        noise += frame_noise
        if s_diff or a_diff:
            stream_diffs += s_diff
            async_diffs += a_diff
            differing += 1
            if len(reports) < max_frames:
                where = f"frame {n}" + (f" (B {nb})" if nb != n else "")
                reports.append([where + ":"] + lines)
    for n in b.frames:
        if n not in matched_b and in_frames(n, ranges):
            only_b += 1
    for r in reports:
        print("\n".join(r), file=out)
    if differing > len(reports):
        print(f"(further {differing - len(reports)} differing frames not shown; --max)", file=out)
    compare_clocks(a, b, maps["B"], ranges, out)
    extra = []
    if c is not None:
        extra.append(f"{noise} reference-noise parts")
    if shifted:
        extra.append(f"{shifted} frames shifted (within {shift})")
    if order_notes:
        extra.append(f"{order_notes} frames with async calls in another order (compared as a set)")
    if loose is not None:
        extra.append(f"{loose_only} frames from {loose} on equal only by the loose rule")
    if only_a or only_b:
        extra.append(f"frames only in A: {only_a}, only in B: {only_b}")
    print(f"devlog compare: {compared} frames compared, {differing} differ ({stream_diffs} stream, {async_diffs} async)"
          + ("; " + "; ".join(extra) if extra else ""), file=out)
    return 1 if differing else 0


TRACE_OUTSIDE = re.compile(r"^(c[ts]) x\d+ ")
TRACE_POINTER = re.compile(r"\b[0-9A-F]{8}\b")


def trace_parts(lines, loose):
    """Scheduled threads' lines (in order) and outside threads' lines (a multiset, thread ids dropped)."""
    ours, outside = [], []
    for line in lines:
        if loose:
            line = TRACE_POINTER.sub("P", line)
        m = TRACE_OUTSIDE.match(line)
        if m:
            outside.append(m.group(1) + " x " + line[m.end():])
        else:
            ours.append(line)
    return ours, outside


def trace(a, b, ticks=None, max_ticks=5, context=3, loose=False, out=None):
    """Compares the trace lines (ct: clock reads, cs: scheduler calls) tick by tick: the game threads' lines in order,
    the outside threads' as a set. Prints each differing tick's first difference."""
    out = out or sys.stdout
    if not a.traces or not b.traces:
        print(f"devlog trace: no trace lines in {a.path if not a.traces else b.path} "
              "(TS2HOST_FRAMECLOCK_OPTS=trace=<from>-<to> with the device log on)", file=out)
        return 2
    ranges = parse_frames(ticks) if ticks else []
    compared = differing = 0
    shown = 0
    for n in sorted(set(a.traces) | set(b.traces)):
        if not in_frames(n, ranges):
            continue
        compared += 1
        oa, xa = trace_parts(a.traces.get(n, []), loose)
        ob, xb = trace_parts(b.traces.get(n, []), loose)
        i = first_difference(oa, ob)
        only_a, only_b = set_difference(xa, xb)
        if i is None and not only_a and not only_b:
            continue
        differing += 1
        if shown >= max_ticks:
            continue
        shown += 1
        print(f"tick {n}: game threads {len(oa)} / {len(ob)} lines, outside threads {len(xa)} / {len(xb)}", file=out)
        if i is not None:
            for k in range(max(0, i - context), i):
                print(f"      {oa[k]}", file=out)
            print(f"    A {oa[i] if i < len(oa) else '(end)'}", file=out)
            print(f"    B {ob[i] if i < len(ob) else '(end)'}", file=out)
        for text in only_a[:5]:
            print(f"    only A (outside): {text}", file=out)
        for text in only_b[:5]:
            print(f"    only B (outside): {text}", file=out)
    if differing > shown:
        print(f"(further {differing - shown} differing ticks not shown; --max)", file=out)
    print(f"devlog trace: {compared} ticks compared, {differing} differ", file=out)
    return 1 if differing else 0


def summary(log, out=None):
    out = out or sys.stdout
    frames = sorted(log.frames)
    print(log.header or "(no header)", file=out)
    if not frames:
        print("no frames", file=out)
        return 0
    calls = sum(f.stream_count + f.async_count for f in log.frames.values())
    draws = sum(f.draws for f in log.frames.values())
    presents = sum(f.presents for f in log.frames.values())
    full = [n for n in frames if log.frames[n].full]
    print(f"frames {frames[0]}..{frames[-1]} ({len(frames)} with calls): {calls} calls, {draws} draws, {presents} presents",
          file=out)
    merged = [n for n in frames if log.frames[n].parts > 1]
    if merged:
        print(f"frames written in parts (merged): {', '.join(map(str, merged[:20]))}", file=out)
    if full:
        print(f"full lists: {len(full)} frames ({', '.join(map(str, full[:20]))}{' ...' if len(full) > 20 else ''})", file=out)
    for label, first, count in log.fulls:
        print(f"full request '{label}': frames {first}..{first + count - 1}", file=out)
    for n, label in log.marks:
        print(f"mark '{label}' at frame {n}", file=out)
    if log.clocks:
        ticks = sorted(log.clocks)
        print(f"clock lines: ticks {ticks[0]}..{ticks[-1]}", file=out)
    return 0


def show(log, frame, kind=None, out=None):
    out = out or sys.stdout
    fr = log.frames.get(frame)
    if fr is None:
        print(f"no frame {frame}", file=out)
        return 1
    print(f"frame {frame}: stream {fr.stream_count} {fr.stream_hash}, async {fr.async_count} {fr.async_sum}, "
          f"draws {fr.draws}, presents {fr.presents}{'' if fr.full else ' (digest only)'}", file=out)
    if frame in log.clocks:
        det, info = log.clocks[frame]
        print(f"clk {det} | {info}", file=out)
    if kind in (None, "s"):
        for i, text in enumerate(fr.s):
            print(f"s {i}: {text}", file=out)
    if kind in (None, "a"):
        for i, text in enumerate(fr.a):
            print(f"a {i}: {text}", file=out)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("summary")
    p.add_argument("log")
    p = sub.add_parser("show")
    p.add_argument("log")
    p.add_argument("frame", type=int)
    p.add_argument("--kind", choices=["s", "a"])
    p = sub.add_parser("compare")
    p.add_argument("a", help="reference run")
    p.add_argument("b", help="run under test")
    p.add_argument("--ref2", help="a second reference run: frames where the references differ are noise")
    p.add_argument("--align", help="match frames relative to this full-request (or mark) label, or 'auto' (every shared "
                                   "label, piecewise)")
    p.add_argument("--frames", help="only these frames of A, e.g. 900,4000-4002")
    p.add_argument("--shift", type=int, default=0, help="a differing frame that B has this many frames off counts as "
                                                        "shifted, not as a difference (default 0)")
    p.add_argument("--max", type=int, default=20, help="differing frames printed (default 20)")
    p.add_argument("--context", type=int, default=0, help="stream calls printed before the first difference")
    p.add_argument("--loose-from", default="auto", help="from this frame of A on, compare draws, presents and async "
                   "call counts only; 'auto' (default): the first frame from 1000 on with 1000+ async calls (the "
                   "neighbourhood's load); 'none': every frame exact")
    p = sub.add_parser("trace")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--ticks", help="only these ticks, e.g. 0-10")
    p.add_argument("--max", type=int, default=5, help="differing ticks printed (default 5)")
    p.add_argument("--context", type=int, default=3, help="game-thread lines printed before the first difference")
    p.add_argument("--loose", action="store_true", help="pointers and handles (8 hex digits) compare equal")
    args = ap.parse_args(argv)
    if args.cmd == "summary":
        return summary(Log(args.log))
    if args.cmd == "show":
        return show(Log(args.log), args.frame, args.kind)
    if args.cmd == "trace":
        return trace(Log(args.a), Log(args.b), args.ticks, args.max, args.context, args.loose)
    return compare(Log(args.a), Log(args.b), Log(args.ref2) if args.ref2 else None, args.align, args.frames, args.max,
                   args.context, args.shift, loose_from=args.loose_from)


if __name__ == "__main__":
    sys.exit(main())
