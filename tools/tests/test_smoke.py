#!/usr/bin/env python3
"""Tests for tools/smoke.py's log analysis: a run whose host skipped a hook fails, naming it.

Run: .venv/Scripts/python -I tools/tests/test_smoke.py
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import smoke as S  # noqa: E402

# The host's lines as src/hook/hooks.cpp writes them (timestamps as in local/saves/*-services.ts2host.log).
SKIP = ("02:57:05.979 hook cTSPersistResKeyList_Create skipped: site at rva 0x1f4dc0 doesn't match "
        "(other version, or patched by another mod)")
TWICE = "02:57:05.980 hook B_Twice skipped: rva 0x1234 is already hooked by A_First"


class Analyse(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def info(self, *lines):
        host = self.dir / "host.log"
        host.write_text("\n".join(lines) + "\n", encoding="latin-1")
        return S.analyse(host, self.dir / "missing-run.log")

    def test_a_skipped_hook_fails_the_run_and_is_named(self):
        info = self.info("02:57:05.900 deployed: abc", SKIP, "02:57:06.000 5463 of 5464 hooks installed")
        self.assertEqual(info["hooks"], "5463/5464")
        self.assertEqual((info["installed"], info["registered"]), (5463, 5464))
        self.assertEqual(info["skipped"], [("cTSPersistResKeyList_Create", "site doesn't match")])
        problem = S.hook_problem(info)
        self.assertIn("cTSPersistResKeyList_Create (site doesn't match)", problem)

    def test_a_hook_already_hooked_is_a_skip_with_its_reason(self):
        info = self.info(TWICE, "5463 of 5464 hooks installed")
        self.assertEqual(info["skipped"], [("B_Twice", "rva 0x1234 is already hooked by A_First")])
        self.assertIn("B_Twice", S.hook_problem(info))

    def test_hooks_disabled_by_the_ini_are_not_a_problem(self):
        # bisect runs (--disable-hooks) turn hooks off on purpose: "1391 of 2778 hooks installed" in local/traces
        lines = [f"hook H{i} disabled by ini" for i in range(3)]
        info = self.info(*lines, "7 of 10 hooks installed")
        self.assertEqual(info["disabled"], ["H0", "H1", "H2"])
        self.assertIsNone(S.hook_problem(info))
        self.assertIsNone(S.hook_problem(self.info("10 of 10 hooks installed")))

    def test_hooks_missing_without_a_skip_line_still_fail(self):
        info = self.info("hook H0 disabled by ini", "7 of 10 hooks installed")
        self.assertIn("2 hook(s) not installed", S.hook_problem(info))

    def test_no_host_log_or_no_install_line_is_left_to_the_runs_result(self):
        self.assertIsNone(S.hook_problem(S.analyse(self.dir / "none.log", self.dir / "none-run.log")))
        self.assertIsNone(S.hook_problem(self.info("02:57:05.900 deployed: abc")))


class Summary(unittest.TestCase):
    def test_the_batch_names_each_skipped_hook(self):
        results = [{"run": "a", "skipped_hooks": ["X", "Y"]}, {"run": "b", "skipped_hooks": ["X"]},
                   {"run": "c", "skipped_hooks": []}, {"run": "old"}]
        line = S.skipped_line(results)
        self.assertIn("HOOKS SKIPPED", line)
        self.assertIn("X in 2 run(s)", line)
        self.assertIn("Y in 1 run(s)", line)
        self.assertIsNone(S.skipped_line([{"run": "c", "skipped_hooks": []}]))


if __name__ == "__main__":
    unittest.main()
