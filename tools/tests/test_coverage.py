#!/usr/bin/env python3
"""Tests for tools/coverage.py (the host's first-call coverage: site list, run parsing, reports).

Run: tools/py -I tools/tests/test_coverage.py
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import coverage as C  # noqa: E402

# cRZDate::IsValid 0x1b98a4 calls cRZDate::IsLeapYear 0x1b995c at 0x1b98c4 (return 0x1b98c9);
# cCTGFramework::MainLoop 0xc2d9c5 calls slot 17 through `call [eax+0x44]` at 0xc2d9d5 (return 0xc2d9d8).
RUN = """# ts2coverage 1: test
# sites 4 from x: armed 2, ours 1, mismatched 1
O c2d427
M 1b969a 8b 9b
R 0 0 start
F 1b98a4 x00401234 5 100
F 1b995c i1b98c9 5 100
R 1 900 menu-idle frame 900
F 1b995c i1b98c9 905 100
F 142dbb ic2d9d8 905 100
F 1b98a4 i1b98c9 906 100
"""


def write(text):
    f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="ascii")
    f.write(text)
    f.close()
    return f.name


class ParseRun(unittest.TestCase):
    def test_rounds_hits_and_skipped_sites(self):
        run = C.parse_run(write(RUN))
        self.assertEqual(run["rounds"], [(0, 0, "start"), (1, 900, "menu-idle frame 900")])
        self.assertEqual([h[0] for h in run["hits"][0]], ["0x1b98a4", "0x1b995c"])
        self.assertEqual(run["hits"][0][0], ("0x1b98a4", "x00401234", 5, 100))
        self.assertEqual(len(run["hits"][1]), 3)
        self.assertEqual(run["ours"], {"0xc2d427"})
        self.assertEqual(run["mismatched"], {"0x1b969a": ("8b", "9b")})
        self.assertEqual(len(run["header"]), 2)


class WithFacts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.facts = C.Facts()

    def test_reached(self):
        f = self.facts
        self.assertEqual(f.reached("0x1b995c", "i1b98c9"), "direct")
        self.assertEqual(f.reached("0x1b98a4", "i1b98c9"), "tail")       # the call there targets another function
        self.assertEqual(f.reached("0x142dbb", "ic2d9d8"), "indirect")   # call dword ptr [eax + 0x44]
        self.assertEqual(f.reached("0x1b98a4", "x00401234"), "ours")

    def test_kind_and_done(self):
        f = self.facts
        self.assertEqual(f.kind("0x1b995c"), "verified")
        self.assertTrue(f.done("0x1b995c"))
        self.assertEqual(f.size("0x1b995c"), 46)

    def test_report_prints_each_round(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_report(type("A", (), {"run": write(RUN), "round": None, "top": 5, "json": False, "todo": False})())
        text = out.getvalue()
        self.assertIn("round 0 (start, at 0 ms): 2 functions", text)
        self.assertIn("round 1 (menu-idle frame 900, at 900 ms): 3 functions", text)
        self.assertIn("originals first reached from outside the image", text)

    def test_headline_counts_each_function_once(self):
        f = self.facts
        s = C.session(f, [write(RUN)])
        h = C.headline_numbers(f, s)
        ea = {r for r in ("0x1b98a4", "0x1b995c", "0x142dbb") if f.kind(r) not in ("library", "third-party")}
        todo = {r for r in ea if not f.done(r)}
        self.assertEqual(s["ea"], ea)
        self.assertEqual(s["todo"], todo)
        self.assertEqual(h["ea_functions"], len(ea))              # 0x1b98a4 and 0x1b995c ran in both rounds
        self.assertEqual(h["ea_bytes"], sum(f.size(r) for r in ea))
        self.assertEqual(h["not_done_bytes"], sum(f.size(r) for r in todo))
        self.assertEqual(h["done_bytes"] + h["not_done_bytes"], h["ea_bytes"])
        self.assertIn("0x1b995c", ea - todo)                      # IsLeapYear is verified
        self.assertEqual(sum(h["not_done_by_subsystem"].values()), h["not_done_bytes"])
        self.assertEqual(sum(h["not_done_by_status"].values()), h["not_done_bytes"])
        self.assertEqual(h["ours_sites"], 1 if f.kind("0xc2d427") not in ("library", "third-party") else 0)
        self.assertEqual((h["rounds"], h["runs"]), (2, 1))

    def test_headline_command(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_headline(type("A", (), {"runs": [write(RUN)], "json": False})())
        text = out.getvalue()
        self.assertTrue(text.startswith("headline: "))
        self.assertIn("1 run(s), 2 rounds, each function counted once", text)

    def test_queue_groups_cover_the_not_done_code(self):
        f = self.facts
        s = C.session(f, [write(RUN)])
        groups = C.queue_groups(f, s, keep_library=True)
        self.assertEqual(sorted(r for g in groups for r in g["rvas"]), sorted(s["todo"]))
        self.assertEqual(sum(g["bytes"] for g in groups), C.headline_numbers(f, s)["not_done_bytes"])
        dropped = {}
        kept = C.queue_groups(f, s, dropped=dropped)
        self.assertEqual(sorted([r for g in kept for r in g["rvas"]] + list(dropped)), sorted(s["todo"]))
        self.assertTrue(all(f.library_instance(r) for r in dropped))
        for g in groups:
            self.assertEqual(g["rounds"], sorted(set().union(*(s["executed"][r]["rounds"] for r in g["rvas"])),
                                                 key=C.round_key))
            self.assertLessEqual(g["class_done_bytes"], g["class_bytes"])
        subs = {f.sub(r) for r in s["todo"]}
        for sub in subs:
            only = C.queue_groups(f, s, [sub])
            self.assertTrue(all(f.sub(r) == sub for g in only for r in g["rvas"]))
            with_guesses = C.queue_groups(f, s, [sub], guesses=True)
            self.assertTrue(all(f.sub(r, True) == sub for g in with_guesses for r in g["rvas"]))
            if sub != "other":    # a subsystem with its guesses keeps everything filed under it
                self.assertLessEqual({r for g in only for r in g["rvas"]}, {r for g in with_guesses for r in g["rvas"]})
        self.assertEqual(C.queue_groups(f, s, ["no-such-subsystem"]), [])
        self.assertEqual(C.queue_groups(f, s, ["no-such-subsystem"], whole=True), [])

    def test_queue_command(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_queue(type("A", (), {"runs": [write(RUN)], "subsystem": None, "whole": False, "top": 0,
                                       "group": None, "json": False, "functions": True})())
        text = out.getvalue()
        self.assertIn("rounds: 0 start; 1 menu-idle frame 900", text)
        self.assertIn("queue: ", text)

    def test_deps_command(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_deps(type("A", (), {"runs": [write(RUN)], "top": 10, "min_bytes": 1, "pair": None, "json": False})())
        text = out.getvalue()
        self.assertIn("bottom-up order", text)
        self.assertIn("hard test boundaries", text)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_deps(type("A", (), {"runs": [write(RUN)], "top": 10, "min_bytes": 1, "pair": None, "json": False,
                                      "by": "group"})())
        self.assertIn("by campaign", out.getvalue())

    def test_campaign_map_files_each_function_under_its_group(self):
        f = self.facts
        s = C.session(f, [write(RUN)])
        m = C.campaign_map(f, s)
        self.assertEqual(set(m), s["todo"])
        for g in C.queue_groups(f, s):
            self.assertEqual({m[r] for r in g["rvas"]}, {g["campaign"]})
            self.assertNotEqual(g["campaign"], "")

    def test_json(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            C.cmd_report(type("A", (), {"run": write(RUN), "round": 1, "top": 5, "json": True, "todo": False})())
        lines = out.getvalue().splitlines()
        self.assertEqual(len(lines), 3)
        self.assertIn('"reached": "indirect"', lines[1])


def write_pair(text, json_lines):
    """A .txt run and its `report --json` twin with the same stem."""
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "run.txt"), "w", encoding="ascii") as f:
        f.write(text)
    with open(os.path.join(d, "run.json"), "w", encoding="utf-8") as f:
        f.write("\n".join(json_lines) + "\n")
    return os.path.join(d, "run.json")


JSON_LINES = [
    '{"round": 0, "rva": "0x1b98a4", "from": "x00401234", "ms": 5, "thread": 100, "status": "stale"}',
    '{"round": 1, "rva": "0x142dbb", "from": "ic2d9d8", "ms": 905, "thread": 100}',
]


class JsonAndSessions(unittest.TestCase):
    def test_json_run_takes_labels_and_ours_from_its_txt(self):
        run = C.parse_run(write_pair(RUN, JSON_LINES))
        self.assertEqual(run["rounds"], [(0, 0, "start"), (1, 900, "menu-idle frame 900")])
        self.assertEqual(run["hits"][0], [("0x1b98a4", "x00401234", 5, 100)])   # the .txt's hits are not mixed in
        self.assertEqual(run["hits"][1], [("0x142dbb", "ic2d9d8", 905, 100)])
        self.assertEqual(run["ours"], {"0xc2d427"})

    def test_json_run_alone_numbers_its_rounds(self):
        path = os.path.join(tempfile.mkdtemp(), "alone.json")
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(JSON_LINES) + "\n")
        run = C.parse_run(path)
        self.assertEqual(run["rounds"], [(0, 0, ""), (1, 0, "")])
        self.assertEqual(run["ours"], set())

    def test_one_file_round_ids_and_union(self):
        s = C.load_runs([write(RUN)])
        self.assertEqual(s["legend"], [("0", "start"), ("1", "menu-idle frame 900")])
        self.assertEqual(sorted(s["executed"]), ["0x142dbb", "0x1b98a4", "0x1b995c"])   # each function once
        self.assertEqual(s["executed"]["0x1b98a4"]["rounds"], {"0", "1"})
        self.assertEqual(s["executed"]["0x1b98a4"]["froms"], ["x00401234", "i1b98c9"])
        self.assertEqual(s["files"], 1)

    def test_several_files_prefix_round_ids(self):
        s = C.load_runs([write(RUN), write("R 0 0 other\nF 0x10 x1 0 0\n")])
        self.assertEqual([rid for rid, _ in s["legend"]], ["1:0", "1:1", "2:0"])
        self.assertEqual(s["executed"]["0x10"]["rounds"], {"2:0"})
        self.assertEqual(s["executed"]["0x142dbb"]["rounds"], {"1:1"})
        self.assertEqual(sorted(s["executed"]["0x1b98a4"]["rounds"], key=C.round_key), ["1:0", "1:1"])
        self.assertEqual(s["ours"], set())   # the second run (a reference run) measured 0xc2d427

    def test_unmeasured_sites_are_ours_in_every_run(self):
        s = C.load_runs([write(RUN), write("O c2d427\nO 10\nR 0 0 x\n")])
        self.assertEqual(s["ours"], {"0xc2d427"})

    def test_default_reference_set_and_main_checkout_paths(self):
        d = tempfile.mkdtemp()
        os.makedirs(os.path.join(d, "local", "coverage"))
        for n in ("run-1023.txt", "lot6-ref.txt"):
            with open(os.path.join(d, "local", "coverage", n), "w") as fh:
                fh.write("R 0 0 start\n")
        saved = C.tsdb.main_root
        C.tsdb.main_root = lambda: C.Path(d)
        cwd = os.getcwd()
        os.chdir(tempfile.mkdtemp())  # relative names must not resolve against the checkout the tests run in
        try:
            with contextlib.redirect_stderr(io.StringIO()) as err:
                runs = C.resolve_runs([])
            self.assertEqual([os.path.basename(r) for r in runs], ["run-1023.txt", "lot6-ref.txt"])
            self.assertIn("2 runs", err.getvalue())
            self.assertIn("missing", err.getvalue())
            self.assertEqual(C.resolve_runs(["lot6-ref.txt"]), [os.path.join(d, "local", "coverage", "lot6-ref.txt")])
            self.assertEqual(C.resolve_run("local/coverage/run-1023.txt"),
                             str(C.Path(d) / "local/coverage/run-1023.txt"))
            self.assertEqual(C.resolve_run("nowhere.txt"), "nowhere.txt")
            os.remove(os.path.join(d, "local", "coverage", "run-1023.txt"))
            os.remove(os.path.join(d, "local", "coverage", "lot6-ref.txt"))
            with self.assertRaises(SystemExit):
                C.resolve_runs([])
        finally:
            os.chdir(cwd)
            C.tsdb.main_root = saved


class FakeFacts:
    """The parts of Facts that deps_edges and boundary_kinds read."""

    def __init__(self, calls, subsystem, sizes, callees=None):
        self.callers = {}
        for a, bs in calls.items():
            for b in bs:
                self.callers.setdefault(b, set()).add(a)
        self.subsystem = subsystem
        self.guess = {}
        self.sizes = sizes
        self.funcs = {r: {"callees": c} for r, c in (callees or {}).items()}

    def size(self, rva):
        return self.sizes.get(rva, 0)

    sub = C.Facts.sub


class Deps(unittest.TestCase):
    def test_edges_only_between_not_done_executed_functions(self):
        facts = FakeFacts({"a1": ["b1", "b2", "c1"], "a2": ["b1", "a1"], "b1": ["a2"], "x": ["b1"], "c1": ["c1"]},
                          {"a1": "A", "a2": "A", "b1": "B", "b2": "B", "c1": "C", "x": "A"},
                          {"a1": 10, "a2": 20, "b1": 100, "b2": 200, "c1": 5, "x": 1})
        s = {"todo": {"a1", "a2", "b1", "c1"}}    # b2 done or not executed, x not executed
        pairs = C.deps_edges(facts, s)
        self.assertEqual(pairs[("A", "B")]["edges"], 2)          # a1->b1, a2->b1; not a1->b2, not x->b1
        self.assertEqual(pairs[("A", "B")]["callees"], {"b1"})
        self.assertEqual(pairs[("A", "B")]["callers"], {"a1", "a2"})
        self.assertEqual(pairs[("B", "A")]["callees"], {"a2"})
        self.assertEqual(pairs[("A", "A")]["edges"], 1)          # a2->a1 (internal)
        self.assertEqual(pairs[("A", "C")]["edges"], 1)
        self.assertNotIn(("C", "C"), pairs)                      # self-recursion is not an edge

    def test_edges_by_another_grouping(self):
        facts = FakeFacts({"a": ["h"], "h": ["b"]}, {"a": "A", "h": "util", "b": "B"}, {"a": 1, "h": 2, "b": 3})
        pairs = C.deps_edges(facts, {"todo": {"a", "h", "b"}}, {"a": "A", "h": "A", "b": "B"}.get)
        self.assertEqual(sorted(pairs), [("A", "A"), ("A", "B")])   # the helper goes with its caller's campaign

    def test_guesses_read_in_place_of_other(self):
        facts = FakeFacts({"a": ["h"]}, {"a": "A", "h": "other"}, {"a": 1, "h": 2})
        facts.guess = {"h": "A"}
        self.assertEqual(sorted(C.deps_edges(facts, {"todo": {"a", "h"}})), [("A", "other")])
        sub_of = lambda r: C.subsystem_of(facts, r, True)  # noqa: E731
        self.assertEqual(sorted(C.deps_edges(facts, {"todo": {"a", "h"}}, sub_of)), [("A", "A")])

    def test_std_names(self):
        for n in ("std::vector<int>::_Grow", "~std::basic_string<char>", ".?AV?$basic_ios@DU?$char_traits@D@std@@@std@@"):
            self.assertTrue(C.STD_NAME(n), n)
        for n in ("cRZString::Append", "", None, ".?AVcRZString@@", "stdio_helper"):
            self.assertFalse(C.STD_NAME(n), n)

    def test_unassigned_subsystem_reads_question_mark(self):
        facts = FakeFacts({"a": ["b"]}, {"a": "A"}, {"a": 1, "b": 2})
        self.assertIn(("A", "?"), C.deps_edges(facts, {"todo": {"a", "b"}}))

    def test_order_cost_counts_calls_into_later_subsystems(self):
        out = {("A", "B"): 10, ("B", "A"): 3, ("B", "C"): 7}
        self.assertEqual(C.order_cost(["A", "B", "C"], out), 17)
        self.assertEqual(C.order_cost(["C", "B", "A"], out), 3)
        self.assertEqual(C.order_cost(["C", "A", "B"], out), 10)
        self.assertEqual(C.order_cost(["A", "Z"], out), 0)        # pairs outside the order don't count

    def test_bottom_up_puts_callees_first(self):
        own = {"A": 1, "B": 1, "C": 1, "D": 1}
        out = {("A", "B"): 50, ("B", "C"): 40, ("A", "C"): 5, ("D", "C"): 1}
        order = C.bottom_up_order(own, out)
        names = [x[0] for x in order]
        self.assertEqual(names.index("C"), 0)
        self.assertLess(names.index("B"), names.index("A"))
        self.assertEqual(sum(x[1] for x in order), 0)            # acyclic: nothing calls into a later subsystem

    def test_bottom_up_cuts_a_cycle_at_its_lightest_edge(self):
        own = {"A": 1, "B": 1, "C": 1}
        out = {("A", "B"): 30, ("B", "C"): 20, ("C", "A"): 2}
        order = C.bottom_up_order(own, out)
        self.assertEqual([x[0] for x in order], ["C", "B", "A"])
        self.assertEqual(order[-1], ("A", 0, {}))
        self.assertEqual(order[0], ("C", 2, {"A": 2}))           # the cut: C's calls into A wait for A's campaign
        self.assertEqual(C.order_cost([x[0] for x in order], out), 2)

    def test_strongly_connected(self):
        comps = C.strongly_connected(["a", "b", "c", "d", "e"],
                                     {"a": {"b"}, "b": {"c"}, "c": {"a"}, "d": {"a", "e"}, "e": {"d"}})
        self.assertEqual(comps, [["a", "b", "c"], ["d", "e"]])
        self.assertEqual(C.strongly_connected(["a", "b"], {"a": {"b"}}), [])

    def test_boundary_kinds(self):
        facts = FakeFacts({}, {}, {}, {
            "f": ["KERNEL32.DLL!CreateFileA", "0x123", C.D3D_CHECK, "KERNEL32.DLL!Sleep", "WINMM.DLL!timeGetTime"],
            "g": ["USER32.DLL!SetTimer", "ADVAPI32.DLL!RegOpenKeyExA", "KERNEL32.DLL!GetProcAddress"],
            "h": ["0x456"],
        })
        self.assertEqual(C.boundary_kinds(facts, "f"), {"file", "d3d", "threads/sync", "timers/clock"})
        self.assertEqual(C.boundary_kinds(facts, "g"), {"timers/clock", "registry", "other imports"})
        self.assertEqual(C.boundary_kinds(facts, "h"), set())
        self.assertEqual(C.boundary_kinds(facts, "missing"), set())


class Sites(unittest.TestCase):
    def test_sites_file(self):
        path = os.path.join(tempfile.mkdtemp(), "sites.txt")
        with contextlib.redirect_stdout(io.StringIO()):
            C.cmd_sites(type("A", (), {"out": path, "all": False})())
        lines = open(path, encoding="ascii").read().splitlines()
        self.assertTrue(lines[0].startswith("# tools/coverage.py sites"))
        body = lines[1:]
        rvas = [int(l.split()[0], 16) for l in body]
        self.assertEqual(rvas, sorted(rvas))
        self.assertIn("1b995c f6", body)   # IsLeapYear: test byte ptr [esp+4], 3
        self.assertIn("1b98a4 83", body)   # IsValid: cmp dword ptr [esp+8], 0
        self.assertGreater(len(body), 60000)


if __name__ == "__main__":
    unittest.main()
