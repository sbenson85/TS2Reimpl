#!/usr/bin/env python3
"""Tests for tools/mutate.py (clean run first, per-mutant timeout from it, sources restored).

Run: .venv/Scripts/python -I tools/tests/test_mutate.py
Builds and test runs are replaced by fakes: nothing is compiled or run.
"""
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import mutate as M  # noqa: E402


class Timeout(unittest.TestCase):
    def test_factor_floor_and_upper_bound(self):
        self.assertEqual(M.mutant_timeout(10, 180), 50)    # 5x the clean run
        self.assertEqual(M.mutant_timeout(1, 180), 20)     # floored
        self.assertEqual(M.mutant_timeout(100, 180), 180)  # --timeout is the upper bound
        self.assertEqual(M.mutant_timeout(1, 5), 5)        # also over the floor


class Main(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        d = Path(self.dir.name)
        self.src = d / "a.cpp"
        self.src.write_bytes(b"int f() { return 1; }\r\n")
        self.mutants = d / "m.json"
        self.mutants.write_text(json.dumps([{"file": str(self.src), "old": "return 1", "new": "return 2"},
                                            {"file": str(self.src), "old": "int f", "new": "int g", "name": "hangs"}]))
        self.backup = d / "backup.json"
        self.calls = []

    def tearDown(self):
        self.dir.cleanup()

    def run_main(self, results, *extra, reach=True):
        """main() with run_tests answering from `results` in turn (clean run first); returns (exit code, output).
        `reach`: what tools/testselect.py's check says about the filter."""
        answers = iter(results)
        self.builds = 0

        def fake_build():
            self.builds += 1
            return []

        def fake_check(filt, targets, root):
            self.targets = targets
            return reach, ["(selection lines)"], {"selected": []}

        def fake_run(filt, timeout, jobs=1):
            self.calls.append((filt, timeout, self.src.read_bytes()))
            if self.backup.exists():  # while a mutant is applied, the backup holds the original source
                self.assertEqual(json.loads(self.backup.read_text())[str(self.src)], "int f() { return 1; }\r\n")
            outcome, seconds, *rest = next(answers)
            tests = rest[0] if rest else {}
            fake_time[0] += seconds
            return outcome, "", tests

        fake_time = [1000.0]
        out = io.StringIO()
        argv = ["mutate.py", str(self.mutants), "--filter", "simantics_prims_aa70", *extra]
        with mock.patch.object(M, "build", fake_build), mock.patch.object(M, "run_tests", fake_run), \
                mock.patch.object(M.testselect, "check", fake_check), \
                mock.patch.object(M, "BACKUP", self.backup), mock.patch.object(M.time, "time", lambda: fake_time[0]), \
                mock.patch.object(sys, "argv", argv), redirect_stdout(out):
            try:
                M.main()
                code = 0
            except SystemExit as e:
                code = e.code
        return code, out.getvalue()

    def test_timeout_derived_from_clean_run(self):
        code, out = self.run_main([("passed", 8), ("failed", 1), ("timeout", 40)])
        self.assertEqual(code, 0, out)
        self.assertEqual([c[1] for c in self.calls], [180, 40, 40])  # clean run bounded by --timeout, mutants by 5x8s
        self.assertEqual([c[0] for c in self.calls], ["simantics_prims_aa70"] * 3)  # the filter passes through as is
        self.assertEqual(self.calls[1][2], b"int f() { return 2; }\r\n")
        self.assertIn("times out after 40s", out)
        self.assertIn("2 killed, 0 survived", out)
        self.assertEqual(self.src.read_bytes(), b"int f() { return 1; }\r\n")
        self.assertFalse(self.backup.exists())

    def test_upper_bound(self):
        code, out = self.run_main([("passed", 100), ("passed", 1), ("failed", 1)], "--timeout", "120")
        self.assertEqual(code, 1, out)
        self.assertEqual([c[1] for c in self.calls], [120, 120, 120])
        self.assertIn("SURVIVED", out)

    def test_failing_clean_run_stops_before_any_mutant(self):
        code, out = self.run_main([("failed", 3)])
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertIn("clean run failed", out)
        self.assertTrue(out.rstrip().endswith("MUTATE DONE"))
        self.assertEqual(self.src.read_bytes(), b"int f() { return 1; }\r\n")

    def test_filter_reaching_nothing_is_refused_before_building(self):
        code, out = self.run_main([("passed", 8)], reach=False)
        self.assertEqual(code, 2, out)
        self.assertEqual(self.calls, [])
        self.assertEqual(self.builds, 0)
        self.assertIn("(selection lines)", out)
        self.assertIn("refused", out)
        self.assertTrue(out.rstrip().endswith("MUTATE DONE"))
        self.assertEqual(self.targets, [(self.src.as_posix(), "f")])  # both mutants edit f (its body, its header)

    def test_force_filter_runs_and_says_so(self):
        code, out = self.run_main([("passed", 8), ("failed", 1), ("failed", 1)], "--force-filter", reach=False)
        self.assertEqual(code, 0, out)
        self.assertEqual(len(self.calls), 3)
        self.assertIn("FORCED (--force-filter)", out)
        self.assertIn("FORCED filter", out.splitlines()[-2])  # also on the totals line

    def test_kills_name_their_tests(self):
        code, out = self.run_main([("passed", 8, {"ran": ["A", "B"]}),
                                   ("failed", 1, {"ran": ["A", "B"], "failed": ["B"], "died": None, "last": "B"}),
                                   ("timeout", 40, {"ran": ["A"], "failed": [], "died": None, "last": "A"})])
        self.assertEqual(code, 0, out)
        self.assertIn("(2 tests ran)", out)
        self.assertRegex(out, r" 1\. \[json 0\] killed .*<- B\n")
        self.assertRegex(out, r" 2\. \[json 1\] killed .*\) hangs <- hung after A\n")  # its "name" as the label

    def test_several_filters_joined_for_check_and_runs(self):
        code, out = self.run_main([("passed", 8), ("failed", 1), ("failed", 1)], "--filter", "B, C", "--filter", "D")
        self.assertEqual(code, 0, out)
        want = "simantics_prims_aa70,B,C,D"
        self.assertEqual([c[0] for c in self.calls], [want] * 3)

    def test_several_filters_reach_check_gets_all(self):
        got = []
        with mock.patch.object(M.testselect, "check", lambda f, t, r: (got.append(f), (False, [], {}))[1]), \
                mock.patch.object(sys, "argv", ["mutate.py", str(self.mutants), "--filter", "A", "--filter", "B"]), \
                mock.patch.object(M, "BACKUP", self.backup), redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit):
                M.main()
        self.assertEqual(got, ["A,B"])

    def test_at_file_filter_only_alone(self):
        with mock.patch.object(sys, "argv", ["mutate.py", str(self.mutants), "--filter", "@names.txt", "--filter", "B"]), \
                redirect_stdout(io.StringIO()), mock.patch.object(sys, "stderr", io.StringIO()):
            with self.assertRaises(SystemExit) as e:
                M.main()
        self.assertEqual(e.exception.code, 2)
        self.assertEqual(self.src.read_bytes(), b"int f() { return 1; }\r\n")

    def test_which_tests_reads_the_runner_output(self):
        out = ("[pass] A_One (rva 0x10, 3 cases)\n[fail] A_Two (rva 0x20, 5 cases)\n    mismatch\n"
               "[error] A_Three (rva 0x30, 1 cases)\ndifftest.exe died in test A_Four at ctx.cases 2: exit code 0xc0000005\n")
        t = M.which_tests(out)
        self.assertEqual(t["ran"], ["A_One", "A_Two", "A_Three"])
        self.assertEqual(t["failed"], ["A_Two", "A_Three"])
        self.assertEqual(t["died"], "A_Four")
        self.assertEqual(M.killed_by("failed", t), "A_Two, A_Three, A_Four (runner died)")
        self.assertEqual(M.killed_by("failed", {"failed": []}), "no test named (exit code only)")

    def test_backup_restored_after_a_killed_run(self):
        self.backup.write_text(json.dumps({str(self.src): "int f() { return 1; }\r\n"}), encoding="utf-8")
        self.src.write_bytes(b"int f() { return 2; }\r\n")  # a run killed while its mutant was applied
        with mock.patch.object(M, "BACKUP", self.backup), redirect_stdout(io.StringIO()):
            M.restore_backup()
        self.assertEqual(self.src.read_bytes(), b"int f() { return 1; }\r\n")
        self.assertFalse(self.backup.exists())


if __name__ == "__main__":
    unittest.main()
