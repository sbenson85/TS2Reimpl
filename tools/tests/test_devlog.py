#!/usr/bin/env python3
"""Tests for tools/devlog.py (the host's device call log: parsing, summary, compare).

Run: tools/py -I tools/tests/test_devlog.py
"""
import contextlib
import hashlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import devlog as D  # noqa: E402


def h(text):
    return int.from_bytes(hashlib.sha1(text.encode()).digest()[:8], "little")


def frame_lines(n, stream, asyncs, full, draws=0, presents=0):
    """A frame as the recorder writes it (any hash will do: compare only compares digests between files; the
    stream and async-sequence digests depend on order, the async sum does not)."""
    sh, seq, total = 0, 0, 0
    for text in stream:
        sh = h(f"{sh:x}{text}")
    for text in asyncs:
        seq = h(f"{seq:x}{text}")
        total = (total + h(text)) % (1 << 64)
    out = [f"frame {n} stream {len(stream)} {sh:016x} async {len(asyncs)} {total:016x} {seq:016x} "
           f"draws {draws} presents {presents}"]
    if full:
        out += [f"s {n} {i} {t}" for i, t in enumerate(stream)]
        out += [f"a {n} {i} {t}" for i, t in enumerate(asyncs)]
    return out


BASE = {
    1: (["SetRenderState 7 1", "Present"], ["CreateTexture 64 64 -> T:aaaa", "CreateVertexBuffer 96 -> Vd0"]),
    2: (["SetTexture 0 T:aaaa", "DrawPrimitive 4 0 2 s0=Vd0+0/24 #12", "Present"], []),
    3: (["Clear", "DrawIndexedPrimitive 4 0 0 4 0 2 s0=V:bb+0/24 ib=I:cc", "Present"],
        ["Upload T:dd from T:aaaa 0 0 [0,0,64,64] 0 #ab", "CreateIndexBuffer 12 -> I:cc"]),
}


class Writer:
    def __init__(self, test):
        self.dir = tempfile.TemporaryDirectory()
        test.addCleanup(self.dir.cleanup)
        self.count = 0

    def log(self, frames=BASE, full=(1, 2, 3), extra=(), shift=0, header="devlog 1 full=1-3 frameclock=1"):
        lines = [header] + list(extra)
        for n in sorted(frames):
            stream, asyncs = frames[n]
            lines += frame_lines(n + shift, stream, asyncs, n in full,
                                 draws=sum("Draw" in t for t in stream), presents=stream.count("Present"))
        self.count += 1
        path = os.path.join(self.dir.name, f"log{self.count}.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        return D.Log(path)


def changed(frames, n, stream=None, asyncs=None):
    out = dict(frames)
    s, a = out[n]
    out[n] = (stream if stream is not None else s, asyncs if asyncs is not None else a)
    return out


def run_compare(*args, **kwargs):
    buf = io.StringIO()
    status = D.compare(*args, out=buf, **kwargs)
    return status, buf.getvalue()


class ParseTest(unittest.TestCase):
    def test_frames_and_records(self):
        log = Writer(self).log(extra=["full menu 1 3", "mark 2 lot_loaded"])
        self.assertEqual(log.header, "devlog 1 full=1-3 frameclock=1")
        self.assertEqual(sorted(log.frames), [1, 2, 3])
        f = log.frames[2]
        self.assertEqual((f.stream_count, f.async_count, f.draws, f.presents), (3, 0, 1, 1))
        self.assertTrue(f.full)
        self.assertEqual(f.s[1], "DrawPrimitive 4 0 2 s0=Vd0+0/24 #12")
        self.assertEqual(log.frames[1].a, BASE[1][1])
        self.assertEqual(log.anchor("menu"), 1)
        self.assertEqual(log.anchor("lot_loaded"), 2)
        self.assertIsNone(log.anchor("nothing"))

    def test_digest_only_frames(self):
        log = Writer(self).log(full=())
        self.assertFalse(log.frames[3].full)
        self.assertEqual(log.frames[3].stream_count, 3)
        self.assertEqual(log.frames[3].s, [])

    def test_summary_and_show(self):
        log = Writer(self).log(full=(2,), extra=["full menu 2 1"])
        buf = io.StringIO()
        D.summary(log, out=buf)
        text = buf.getvalue()
        self.assertIn("frames 1..3 (3 with calls): 12 calls, 2 draws, 3 presents", text)
        self.assertIn("full lists: 1 frames (2)", text)
        self.assertIn("full request 'menu': frames 2..2", text)
        buf = io.StringIO()
        self.assertEqual(D.show(log, 2, out=buf), 0)
        self.assertIn("s 1: DrawPrimitive", buf.getvalue())
        self.assertEqual(D.show(log, 9, out=io.StringIO()), 1)


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.w = Writer(self)

    def test_identical(self):
        status, text = run_compare(self.w.log(), self.w.log())
        self.assertEqual(status, 0)
        self.assertIn("devlog compare: 3 frames compared, 0 differ (0 stream, 0 async)", text)

    def test_first_stream_difference(self):
        stream = list(BASE[3][0])
        stream[1] = stream[1].replace("ib=I:cc", "ib=I:ce")
        status, text = run_compare(self.w.log(), self.w.log(changed(BASE, 3, stream=stream)), context=1)
        self.assertEqual(status, 1)
        self.assertIn("frame 3:\n  stream call 1 (of 3 / 3):\n      0: Clear\n    A 1: DrawIndexed", text)
        self.assertIn("    B 1: DrawIndexedPrimitive 4 0 0 4 0 2 s0=V:bb+0/24 ib=I:ce", text)
        self.assertIn("1 differ (1 stream, 0 async)", text)

    def test_stream_missing_call(self):
        status, text = run_compare(self.w.log(), self.w.log(changed(BASE, 2, stream=BASE[2][0][:2])))
        self.assertEqual(status, 1)
        self.assertIn("A 2: Present", text)
        self.assertIn("B 2: (end)", text)

    def test_digest_only_difference(self):
        stream = ["SetRenderState 7 0", "Present"]
        status, text = run_compare(self.w.log(full=()), self.w.log(changed(BASE, 1, stream=stream), full=()))
        self.assertEqual(status, 1)
        self.assertIn("stream digest differs (2 calls", text)
        self.assertIn("no full list in both", text)

    def test_async_only_frame(self):  # a loading frame: creation and uploads, no stream calls
        frames = dict(BASE)
        frames[4] = ([], ["CreateTexture 8 8 -> T:01"])
        other = changed(frames, 4, asyncs=["CreateTexture 8 8 -> T:02"])
        status, text = run_compare(self.w.log(frames, full=(4,)), self.w.log(other, full=(4,)))
        self.assertEqual(status, 1)
        self.assertIn("frame 4:\n  async set differs (1 / 1 calls)\n    only A: CreateTexture 8 8 -> T:01", text)

    def test_async_reordered_is_a_set(self):
        status, text = run_compare(self.w.log(), self.w.log(changed(BASE, 3, asyncs=BASE[3][1][::-1])))
        self.assertEqual(status, 0)
        self.assertIn("1 frames with async calls in another order (compared as a set)", text)

    def test_async_set_difference(self):
        asyncs = [BASE[3][1][0].replace("#ab", "#ac"), BASE[3][1][1]]
        status, text = run_compare(self.w.log(), self.w.log(changed(BASE, 3, asyncs=asyncs)))
        self.assertEqual(status, 1)
        self.assertIn("async set differs (2 / 2 calls)", text)
        self.assertIn("only A: Upload T:dd from T:aaaa 0 0 [0,0,64,64] 0 #ab", text)
        self.assertIn("only B: Upload T:dd from T:aaaa 0 0 [0,0,64,64] 0 #ac", text)
        self.assertIn("(0 stream, 1 async)", text)

    def test_async_order_counts_where_references_agree(self):
        a, c = self.w.log(), self.w.log()
        b = self.w.log(changed(BASE, 3, asyncs=BASE[3][1][::-1]))
        status, text = run_compare(a, b, c)
        self.assertEqual(status, 1)
        self.assertIn("async order (the reference runs agree on it) differs", text)
        self.assertIn("A 0: Upload", text)
        self.assertIn("B 0: CreateIndexBuffer", text)

    def test_async_order_free_where_references_reorder(self):
        a = self.w.log()
        c = self.w.log(changed(BASE, 3, asyncs=BASE[3][1][::-1]))
        b = self.w.log(changed(BASE, 3, asyncs=BASE[3][1][::-1]))
        self.assertEqual(run_compare(a, b, c)[0], 0)
        b2 = self.w.log(changed(BASE, 1, asyncs=BASE[1][1][::-1]))
        status, text = run_compare(a, b2, c)  # frame 1: the references agree on the order, B does not
        self.assertEqual(status, 1)
        self.assertIn("frame 1:", text)

    def test_reference_noise(self):
        noisy = ["SetRenderState 7 2", "Present"]
        a, c = self.w.log(), self.w.log(changed(BASE, 1, stream=noisy))
        b = self.w.log(changed(BASE, 1, stream=["SetRenderState 7 3", "Present"]))
        status, text = run_compare(a, b, c)
        self.assertEqual(status, 0)
        self.assertIn("0 differ (0 stream, 0 async); 1 reference-noise parts", text)
        # The same change where the references agree is a difference.
        self.assertEqual(run_compare(a, b, self.w.log())[0], 1)

    def test_align(self):
        a = self.w.log(extra=["full menu 1 3"])
        b = self.w.log(extra=["full menu 101 3"], shift=100)
        self.assertEqual(run_compare(a, b, align="menu")[0], 0)
        status, text = run_compare(a, b)
        self.assertEqual(status, 0)
        self.assertIn("0 frames compared", text)
        self.assertIn("frames only in A: 3, only in B: 3", text)
        b2 = self.w.log(changed(BASE, 2, stream=["Present"]), extra=["mark 101 menu"], shift=100)
        status, text = run_compare(a, b2, align="menu")
        self.assertEqual(status, 1)
        self.assertIn("frame 2 (B 102):", text)
        status, text = run_compare(a, self.w.log(), align="menu")
        self.assertEqual(status, 2)
        self.assertIn("no 'menu' in", text)

    def test_frames_filter_and_max(self):
        b = self.w.log({n: (s + ["Clear"], a) for n, (s, a) in BASE.items()})
        status, text = run_compare(self.w.log(), b, frames="2-3")
        self.assertEqual(status, 1)
        self.assertIn("2 frames compared, 2 differ", text)
        self.assertNotIn("frame 1:", text)
        status, text = run_compare(self.w.log(), b, max_frames=1)
        self.assertIn("frame 1:", text)
        self.assertNotIn("frame 2:", text)
        self.assertIn("(further 2 differing frames not shown; --max)", text)

    def test_main_exit_status(self):
        a, b = self.w.log(), self.w.log(changed(BASE, 2, stream=["Present"]))
        with contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(D.main(["compare", a.path, b.path]), 1)
            self.assertEqual(D.main(["compare", a.path, a.path, "--ref2", a.path]), 0)
            self.assertEqual(D.main(["summary", a.path]), 0)
            self.assertEqual(D.main(["show", a.path, "2", "--kind", "s"]), 0)
        self.assertIn("s 2: Present", out.getvalue())


class FormatTwoTest(unittest.TestCase):
    """Lane UC: frames written in parts, clock lines, piecewise alignment, shifted frames."""

    def setUp(self):
        self.w = Writer(self)

    def raw(self, lines):
        self.w.count += 1
        path = os.path.join(self.w.dir.name, f"raw{self.w.count}.txt")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(["devlog 2 full=none frameclock=1"] + lines) + "\n")
        return D.Log(path)

    def test_frame_parts_merge(self):
        # Frame 0 in three parts (three devices), as the game writes it; the same split in both files compares equal,
        # and a call more in any part is a difference.
        p1 = frame_lines(0, ["Direct3DCreate9 32"], [], False)
        p2 = frame_lines(0, ["Direct3DCreate9 32", "CreateDevice 0 1"], ["CreateTexture -> T:aa"], False)
        p3 = frame_lines(0, [], ["CreateTexture -> T:bb"], False)
        a = self.raw(p1 + p2 + p3 + frame_lines(1, ["Present"], [], False))
        fr = a.frames[0]
        self.assertEqual((fr.stream_count, fr.async_count, fr.parts), (3, 2, 3))
        self.assertEqual(run_compare(a, self.raw(p1 + p2 + p3 + frame_lines(1, ["Present"], [], False)))[0], 0)
        p2b = frame_lines(0, ["Direct3DCreate9 32", "CreateDevice 0 2"], ["CreateTexture -> T:aa"], False)
        status, text = run_compare(a, self.raw(p1 + p2b + p3 + frame_lines(1, ["Present"], [], False)))
        self.assertEqual(status, 1)
        self.assertIn("frame 0:", text)
        # The async parts add up as a set: the same calls split differently between parts are equal.
        p2c = frame_lines(0, ["Direct3DCreate9 32", "CreateDevice 0 1"], ["CreateTexture -> T:bb"], False)
        p3c = frame_lines(0, [], ["CreateTexture -> T:aa"], False)
        self.assertEqual(run_compare(a, self.raw(p1 + p2c + p3c + frame_lines(1, ["Present"], [], False)))[0], 0)
        with contextlib.redirect_stdout(io.StringIO()) as out:
            D.summary(a)
        self.assertIn("frames written in parts (merged): 0", out.getvalue())

    def test_clock_lines(self):
        body = frame_lines(1, ["Present"], [], False) + frame_lines(2, ["Present"], [], False)
        a = self.raw(["clk 0 t=0 main=3:aa | polls=1", "clk 1 t=33333 main=5:bb | polls=9"] + body)
        self.assertEqual(a.clocks[1], ("t=33333 main=5:bb", "polls=9"))
        # The informational part may differ.
        status, text = run_compare(a, self.raw(["clk 0 t=0 main=3:aa | polls=4", "clk 1 t=33333 main=5:bb | polls=0"] + body))
        self.assertEqual(status, 0)
        self.assertIn("clock: 2 ticks agree", text)
        # The deterministic part names the first tick, without changing the exit status.
        status, text = run_compare(a, self.raw(["clk 0 t=0 main=3:aa | polls=1", "clk 1 t=33333 main=6:bc | polls=9"] + body))
        self.assertEqual(status, 0)
        self.assertIn("clock: first difference at tick 1:", text)
        self.assertIn("B: t=33333 main=6:bc | polls=9", text)
        # No clock lines: nothing said.
        self.assertNotIn("clock:", run_compare(self.w.log(), self.w.log())[1])
        with contextlib.redirect_stdout(io.StringIO()) as out:
            D.show(a, 1)
        self.assertIn("clk t=33333 main=5:bb | polls=9", out.getvalue())

    def test_loose_rule(self):
        # From the neighbourhood's load (auto: the first frame from 1000 on with 1000+ async calls) frames compare by
        # draws, presents and async call counts: another order or other buffer contents pass; another draw count, present
        # count or async count doesn't. Frames before it stay exact.
        load = [f"CreateVertexBuffer -> V:{i:04x}" for i in range(1000)]
        load_b = [f"CreateVertexBuffer -> V:{i:04x} #other" for i in range(1000)]

        def log(menu, hood, extra_async, hood_draws=2):
            return self.raw(frame_lines(900, menu, [], False, 1, 1) + frame_lines(999, ["Present"], ["x"] * 1000, False, 0, 1)
                            + frame_lines(1239, ["Draw A", "Draw B", "Present"], load, False, 2, 1)
                            + frame_lines(1240, hood, ["Upload"] * extra_async, False, hood_draws, 1))

        a = log(["Draw", "Present"], ["Draw A", "Draw B", "Present"], 1)
        b = self.raw(frame_lines(900, ["Draw", "Present"], [], False, 1, 1) + frame_lines(999, ["Present"], ["x"] * 1000, False, 0, 1)
                     + frame_lines(1239, ["Draw B", "SetTexture", "Draw A", "Present"], load_b, False, 2, 1)
                     + frame_lines(1240, ["Draw B", "Draw A", "Present"], ["Upload #b"], False, 2, 1))
        status, text = run_compare(a, b)
        self.assertEqual(status, 0, text)
        self.assertIn("loose from frame 1239 (auto", text)  # 999 has 1000 async calls but is before 1000
        self.assertIn("2 frames from 1239 on equal only by the loose rule", text)
        # Exact with 'none', or from a later frame.
        self.assertEqual(run_compare(a, b, loose_from="none")[0], 1)
        status, text = run_compare(a, b, loose_from="1240")
        self.assertEqual(status, 1)
        self.assertIn("frame 1239:", text)
        self.assertNotIn("\nframe 1240:", text)
        # Before the start: exact.
        status, text = run_compare(log(["Draw", "SetTexture", "Present"], ["Draw A", "Draw B", "Present"], 1), a)
        self.assertEqual(status, 1)
        self.assertIn("frame 900:", text)
        # A frame from 1000 on without a big load (the click into the neighbourhood; 500 async calls, under the 1000 needed)
        # is not the start: exact.
        x = self.raw(frame_lines(1100, ["Draw", "SetTexture", "Present"], ["Upload"] * 500, False, 1, 1)
                     + frame_lines(1239, ["Draw A", "Present"], load, False, 1, 1))
        y = self.raw(frame_lines(1100, ["SetTexture", "Draw", "Present"], ["Upload"] * 500, False, 1, 1)
                     + frame_lines(1239, ["Draw A", "Present"], load, False, 1, 1))
        status, text = run_compare(x, y)
        self.assertEqual(status, 1)
        self.assertIn("loose from frame 1239", text)
        self.assertIn("\nframe 1100:", text)
        # Draws, async calls and presents count.
        status, text = run_compare(a, log(["Draw", "Present"], ["Draw A", "Draw B", "Present"], 1, hood_draws=3))
        self.assertEqual(status, 1)
        self.assertIn("loose: draws 2 / 3, presents 1 / 1, async calls 1 / 1", text)
        status, text = run_compare(a, log(["Draw", "Present"], ["Draw A", "Draw B", "Present"], 2))
        self.assertEqual(status, 1)
        self.assertIn("async calls 1 / 2", text)
        self.assertIn("(0 stream, 1 async)", text)
        c = self.raw(frame_lines(900, ["Draw", "Present"], [], False, 1, 1) + frame_lines(999, ["Present"], ["x"] * 1000, False, 0, 1)
                     + frame_lines(1239, ["Draw A", "Draw B", "Present"], load, False, 2, 1)
                     + frame_lines(1240, ["Draw A", "Draw B", "Present"], ["Upload"], False, 2, 2))
        status, text = run_compare(a, c)
        self.assertIn("presents 1 / 2", text)
        self.assertIn("(1 stream, 0 async)", text)
        # No big load after the menu: every frame exact.
        self.assertNotIn("loose from", run_compare(self.raw(frame_lines(900, ["Present"], [], False)),
                                                   self.raw(frame_lines(900, ["Present"], [], False)))[1])

    def test_input_fields(self):
        # msgs= and in= (messages taken, real input read) are informational: the first tick where they differ is named
        # on its own line, and the exit status doesn't change.
        body = frame_lines(1, ["Present"], [], False) + frame_lines(2, ["Present"], [], False)
        a = self.raw(["clk 0 t=0 main=3:aa | polls=1 msgs=2:0000abcd in=0:00000000 by=0:3",
                      "clk 1 t=33333 main=5:bb | polls=9 msgs=1:00001111 in=3:00002222 by=0:5"] + body)
        # ctl= (the control's own command messages, real-time by design) is not input.
        same = self.raw(["clk 0 t=0 main=3:aa | polls=7 msgs=2:0000abcd in=0:00000000 ctl=16 by=0:3",
                         "clk 1 t=33333 main=5:bb | polls=2 msgs=1:00001111 in=3:00002222 ctl=0 by=0:5"] + body)
        status, text = run_compare(a, same)
        self.assertEqual(status, 0)
        self.assertIn("input: the same in every tick compared", text)
        moved = self.raw(["clk 0 t=0 main=3:aa | polls=1 msgs=2:0000abcd in=0:00000000 by=0:3",
                          "clk 1 t=33333 main=5:bb | polls=9 msgs=1:00001111 in=3:00009999 by=0:5"] + body)
        status, text = run_compare(a, moved)
        self.assertEqual(status, 0)
        self.assertIn("input: first difference at tick 1 (A/B): in=3:00002222/3:00009999", text)
        self.assertNotIn("msgs=", text.split("input:")[1].splitlines()[0])
        # Logs without the fields (format 2 before them): nothing said.
        self.assertNotIn("input:", run_compare(self.raw(["clk 0 t=0 main=3:aa | polls=1"] + body),
                                               self.raw(["clk 0 t=0 main=3:aa | polls=1"] + body))[1])

    def test_align_auto_piecewise(self):
        # A: menu at 1, lot at 10; B: menu at 101, lot at 205 (the lot came 4 frames later relative to the menu).
        fa = {1: (["M1"], []), 2: (["M2"], []), 10: (["L1"], []), 11: (["L2"], [])}
        fb = {101: (["M1"], []), 102: (["M2"], []), 205: (["L1"], []), 206: (["L2"], [])}
        a = self.w.log(fa, full=(), extra=["mark 1 menu", "mark 10 lot"])
        b = self.w.log(fb, full=(), extra=["mark 101 menu", "mark 205 lot"])
        status, text = run_compare(a, b, align="auto")
        self.assertEqual(status, 0, text)
        self.assertIn("4 frames compared, 0 differ", text)
        self.assertIn("aligned (B): from A 1: +100, from A 10: +195", text)
        # One label alone leaves the lot frames unmatched.
        status, text = run_compare(a, b, align="menu")
        self.assertIn("2 frames compared", text)
        # Frames before the first shared label match by number.
        a2 = self.w.log({0: (["S"], []), **fa}, full=(), extra=["mark 1 menu", "mark 10 lot"])
        b2 = self.w.log({0: (["S"], []), **fb}, full=(), extra=["mark 101 menu", "mark 205 lot"])
        self.assertIn("5 frames compared, 0 differ", run_compare(a2, b2, align="auto")[1])

    def test_shifted_frames(self):
        # B's menu animation runs a frame late: frame n of A is B's frame n+1.
        fa = {1: (["A0"], []), 2: (["A1"], []), 3: (["A2"], []), 4: (["A3"], [])}
        fb = {1: (["A0"], []), 2: (["A0"], []), 3: (["A1"], []), 4: (["A2"], [])}
        a, b = self.w.log(fa, full=()), self.w.log(fb, full=())
        status, text = run_compare(a, b)
        self.assertEqual(status, 1)
        self.assertIn("B's frame 3 has it: shifted +1", text)
        self.assertIn("3 differ", text)
        status, text = run_compare(a, b, shift=1)
        self.assertIn("2 frames shifted (within 1)", text)
        # Frame 4's content (A3) is in no frame of B: still a difference.
        self.assertEqual(status, 1)
        self.assertIn("1 differ (1 stream, 0 async)", text)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(D.main(["compare", a.path, b.path, "--shift", "1", "--align", "auto"]), 1)


class TraceTest(unittest.TestCase):
    """Lane UE: the frame clock's trace lines (ct, cs), compared tick by tick."""

    def setUp(self):
        self.w = Writer(self)

    def raw(self, lines):
        return FormatTwoTest.raw(self, lines)

    def run_trace(self, a, b, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            status = D.trace(a, b, **kwargs)
        return status, out.getvalue()

    BASE = ["ct 4 t0 q image 0x1c9244 400000.0", "cs 4 t3 Sleep(10) image 0x1b9f8a -> 0", "ct 4 x812 m dsound.dll+0x10 400000.0",
            "ct 5 t0 q image 0x1c9244 433333.3", "ct 5 x812 m dsound.dll+0x10 433333.3", "ct 5 t3 t image 0xea100 433333.3"]

    def test_parse(self):
        a = self.raw(self.BASE + frame_lines(4, ["Present"], [], False))
        self.assertEqual(sorted(a.traces), [4, 5])
        self.assertEqual(a.traces[4][1], "cs t3 Sleep(10) image 0x1b9f8a -> 0")
        self.assertEqual(a.frames[4].stream_count, 1)  # trace lines are not frame records

    def test_identical_and_outside_threads(self):
        a = self.raw(self.BASE)
        # Outside threads: another id and another place among the game threads' lines: equal.
        moved = [self.BASE[2].replace("x812", "x999"), self.BASE[0], self.BASE[1]] + self.BASE[3:]
        status, text = self.run_trace(a, self.raw(moved))
        self.assertEqual(status, 0, text)
        self.assertIn("devlog trace: 2 ticks compared, 0 differ", text)
        # An outside thread's read more: a set difference.
        status, text = self.run_trace(a, self.raw(self.BASE + ["ct 5 x812 m dsound.dll+0x10 433333.3"]))
        self.assertEqual(status, 1)
        self.assertIn("tick 5: game threads 2 / 2 lines, outside threads 1 / 2", text)
        self.assertIn("only B (outside): ct x m dsound.dll+0x10 433333.3", text)

    def test_game_thread_order(self):
        a = self.raw(self.BASE)
        swapped = [self.BASE[1], self.BASE[0]] + self.BASE[2:]
        status, text = self.run_trace(a, self.raw(swapped), context=1)
        self.assertEqual(status, 1)
        self.assertIn("tick 4: game threads 2 / 2 lines", text)
        self.assertIn("    A ct t0 q image 0x1c9244 400000.0", text)
        self.assertIn("    B cs t3 Sleep(10) image 0x1b9f8a -> 0", text)
        self.assertIn("1 differ", text)
        # --ticks leaves tick 4 out.
        self.assertEqual(self.run_trace(a, self.raw(swapped), ticks="5")[0], 0)
        # A tick only one side traced is a difference ((end) on the other).
        status, text = self.run_trace(a, self.raw(self.BASE[:3]))
        self.assertIn("    B (end)", text)

    def test_loose_pointers(self):
        a = self.raw(["cs 2 t1 WaitForSingleObject(0000012C, 100) image 0x1b9df1 -> 0x102"])
        b = self.raw(["cs 2 t1 WaitForSingleObject(00000130, 100) image 0x1b9df1 -> 0x102"])
        self.assertEqual(self.run_trace(a, b)[0], 1)
        self.assertEqual(self.run_trace(a, b, loose=True)[0], 0)
        # No trace lines: said so, status 2.
        status, text = self.run_trace(self.raw([]), b)
        self.assertEqual(status, 2)
        self.assertIn("no trace lines", text)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(D.main(["trace", a.path, b.path, "--loose", "--ticks", "0-9"]), 0)


if __name__ == "__main__":
    unittest.main()
