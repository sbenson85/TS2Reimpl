#!/usr/bin/env python3
"""Tests for the lane priority rule: a worktree's builds and test runs at below normal, the main checkout's untouched
(tsdb.lane_priority_flag, tsdb.lower_lane_priority, common.ps1's Set-LanePriority).

Run: tools/py -I tools/tests/test_lane_priority.py
"""
import ctypes
import ctypes.wintypes
import os
import subprocess
import sys
import textwrap
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import tsdb  # noqa: E402

TOOLS = Path(__file__).resolve().parent.parent
PROBE_PY = textwrap.dedent("""
    import ctypes, ctypes.wintypes, subprocess, sys
    sys.path.insert(0, sys.argv[1])
    import tsdb
    tsdb.in_worktree = lambda: sys.argv[2] == "lane"
    tsdb.lower_lane_priority()
    print(subprocess.run(["powershell", "-NoProfile", "-Command", "(Get-Process -Id $PID).PriorityClass"],
                         capture_output=True, text=True).stdout.strip())
""")


def own_class_in_child(script_in_worktree):
    return subprocess.run([sys.executable, "-I", "-c", PROBE_PY, str(TOOLS), "lane" if script_in_worktree else "main"],
                          capture_output=True, text=True, check=True).stdout.strip()


def powershell_class(is_worktree):
    """The class a child of Set-LanePriority gets. common.ps1 is run as it is; only its $isWorktree is set."""
    script = ". '{}'; $isWorktree = ${}; Set-LanePriority; (Start-Process powershell -NoNewWindow -Wait -PassThru " \
             "-ArgumentList '-NoProfile','-Command','(Get-Process -Id $PID).PriorityClass' | Out-Null)".format(
                 TOOLS / "common.ps1", "true" if is_worktree else "false")
    return subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True,
                          check=True).stdout.strip()


class Flags(unittest.TestCase):
    def test_a_worktree_compiles_below_normal(self):
        with mock.patch.object(tsdb, "main_root", return_value=Path("C:/elsewhere")):
            self.assertTrue(tsdb.in_worktree())
            self.assertEqual(tsdb.lane_priority_flag(), 0x4000)

    def test_the_main_checkout_keeps_its_class(self):
        with mock.patch.object(tsdb, "main_root", return_value=tsdb.ROOT):
            self.assertFalse(tsdb.in_worktree())
            self.assertEqual(tsdb.lane_priority_flag(), 0)

    def test_paths_compare_as_windows_paths(self):
        with mock.patch.object(tsdb, "main_root", return_value=Path(str(tsdb.ROOT).upper())):
            self.assertFalse(tsdb.in_worktree())


class Processes(unittest.TestCase):
    def setUp(self):
        # A test run started below normal can't show main's rule: the class would be inherited.
        kernel32 = ctypes.windll.kernel32
        kernel32.GetPriorityClass.argtypes = (ctypes.wintypes.HANDLE,)
        if kernel32.GetPriorityClass(ctypes.wintypes.HANDLE(-1)) != 0x20:
            self.skipTest("this process isn't at normal priority")

    def test_python_lane_lowers_itself_and_its_children(self):
        self.assertEqual(own_class_in_child(True), "BelowNormal")

    def test_python_main_changes_nothing(self):
        self.assertEqual(own_class_in_child(False), "Normal")

    def test_powershell_lane_lowers_itself_and_its_children(self):
        self.assertEqual(powershell_class(True), "BelowNormal")

    def test_powershell_main_changes_nothing(self):
        self.assertEqual(powershell_class(False), "Normal")


if __name__ == "__main__":
    unittest.main()
