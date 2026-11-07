#!/usr/bin/env python3
"""Tests for .gitattributes' union merges and tools/annotations_check.py.

Run: .venv/Scripts/python -I tools/tests/test_annotations_check.py
The merge tests build throwaway git repositories in a temp folder (the repo's own .gitattributes copied in).
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import annotations_check as A  # noqa: E402

REPO = Path(__file__).resolve().parent.parent.parent


def name(rva, n, **extra):
    return json.dumps({"kind": "name", "rva": rva, "name": n, "source": "test", **extra})


class Problems(unittest.TestCase):
    def kinds(self, text, path="db/annotations.jsonl"):
        return [(s, i[0]) for s, i, _ in A.problems(path, text)]

    def test_clean(self):
        self.assertEqual(self.kinds(name("0x10", "A") + "\n" + name("0x20", "B") + "\n"), [])

    def test_bad_json_marker_duplicate_newline(self):
        text = "\n".join([name("0x10", "A"), "{not json", "<<<<<<< HEAD", "=======", ">>>>>>> lane", name("0x10", "A"),
                          name("0x30", "C")])
        self.assertEqual(self.kinds(text), [("error", "no final newline"), ("error", "json"), ("error", "marker"),
                                            ("error", "marker"), ("error", "marker"), ("error", "duplicate")])

    def test_same_key_disagreeing(self):
        text = name("0x10", "A") + "\n" + name("0x10", "A", tentative=True) + "\n"
        self.assertEqual(self.kinds(text), [("error", "key")])

    def test_known_duplicates_are_warnings(self):
        for rva in ("0x1e3c70", "0x1c48da"):
            text = name(rva, "A") + "\n" + name(rva, "B") + "\n"
            self.assertEqual(self.kinds(text), [("warning", "key")])

    def test_method_keys(self):
        m = lambda n: json.dumps({"iface": "cIGZFoo", "slot": 3, "name": n, "ret": "void", "params": []})  # noqa: E731
        self.assertEqual(self.kinds(m("Get") + "\n" + m("Fetch") + "\n", "db/methods/cFoo.jsonl"), [("error", "key")])

    def test_lane_records_disagreeing_is_a_warning(self):
        r = lambda d: json.dumps({"lane": "LJ", "date": d})  # noqa: E731
        self.assertEqual(self.kinds(r("1") + "\n" + r("2") + "\n", "notes/lane-records.jsonl"), [("warning", "key")])

    def test_real_files_have_only_the_known_duplicates(self):
        problems = []
        for pat in A.union_patterns((REPO / ".gitattributes").read_text(encoding="utf-8")):
            for p in REPO.glob(pat):
                problems += [(s, msg) for s, _, msg in A.problems(p.relative_to(REPO).as_posix(), p.read_text(encoding="utf-8"))]
        self.assertTrue(all(s == "warning" for s, _ in problems), problems)


def git(cwd, *a):
    r = subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", *a], cwd=cwd,
                       capture_output=True, text=True)
    return r


class UnionMerge(unittest.TestCase):
    """Two branches that each append to db/annotations.jsonl merge clean with the repo's .gitattributes, and conflict
    without it (the control)."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.repo = Path(self.dir.name)
        git(self.repo, "init", "-q", "-b", "main")
        (self.repo / "db").mkdir()
        self.ann = self.repo / "db/annotations.jsonl"
        self.ann.write_text(name("0x10", "A") + "\n" + name("0x20", "B") + "\n", encoding="utf-8", newline="\n")

    def tearDown(self):
        self.dir.cleanup()

    def commit_all(self, msg):
        git(self.repo, "add", "-A")
        self.assertEqual(git(self.repo, "commit", "-q", "-m", msg).returncode, 0)

    def append(self, branch, line, start="main"):
        git(self.repo, "checkout", "-q", "-b", branch, start)
        with open(self.ann, "a", encoding="utf-8", newline="\n") as f:
            f.write(line + "\n")
        self.commit_all(branch)

    def two_lanes(self, with_attributes):
        if with_attributes:
            (self.repo / ".gitattributes").write_bytes((REPO / ".gitattributes").read_bytes())
        self.commit_all("base")
        self.append("lane1", name("0x30", "C", lane="L1"))
        self.append("lane2", name("0x40", "D", lane="L2"))
        git(self.repo, "checkout", "-q", "main")
        self.assertEqual(git(self.repo, "merge", "-q", "--no-edit", "lane1").returncode, 0)
        return git(self.repo, "merge", "--no-edit", "lane2")

    def test_appends_merge_clean_with_union(self):
        r = self.two_lanes(True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        lines = self.ann.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(lines), 4)
        self.assertIn(name("0x30", "C", lane="L1"), lines)
        self.assertIn(name("0x40", "D", lane="L2"), lines)
        with mock.patch.object(A, "ROOT", self.repo):
            self.assertEqual([p for p in A.problems("db/annotations.jsonl", self.ann.read_text(encoding="utf-8"))], [])

    def test_control_without_attributes_conflicts(self):
        r = self.two_lanes(False)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("CONFLICT", r.stdout)
        git(self.repo, "merge", "--abort")

    def test_check_merge_reports_an_entry_both_sides_changed(self):
        (self.repo / ".gitattributes").write_bytes((REPO / ".gitattributes").read_bytes())
        self.commit_all("base")
        # main confirms 0x20 in place and appends; the lane appends a different name for 0x20
        git(self.repo, "checkout", "-q", "-b", "lane", "main")
        with open(self.ann, "a", encoding="utf-8", newline="\n") as f:
            f.write(name("0x20", "Bee", lane="L") + "\n")
        self.commit_all("lane")
        git(self.repo, "checkout", "-q", "main")
        self.ann.write_text(name("0x10", "A") + "\n" + name("0x20", "B") + "\n" + name("0x50", "E") + "\n",
                            encoding="utf-8", newline="\n")
        self.commit_all("main moves on")
        with mock.patch.object(A, "ROOT", self.repo):
            errors, warnings = A.check_merge("lane", "main")
            self.assertTrue(any("share the key ('name', '0x20')" in e for e in errors), (errors, warnings))
        # git agrees the merge is clean (that's why the check is needed)
        self.assertEqual(git(self.repo, "merge", "--no-edit", "lane").returncode, 0)

    def test_check_merge_warns_when_union_keeps_a_deleted_line(self):
        (self.repo / ".gitattributes").write_bytes((REPO / ".gitattributes").read_bytes())
        self.commit_all("base")
        self.append("lane", name("0x30", "C", lane="L"))
        git(self.repo, "checkout", "-q", "main")
        self.ann.write_text(name("0x10", "A") + "\n", encoding="utf-8", newline="\n")  # main deletes 0x20
        self.commit_all("delete 0x20")
        with mock.patch.object(A, "ROOT", self.repo):
            errors, warnings = A.check_merge("lane", "main")
        self.assertEqual(errors, [])
        self.assertTrue(any("main deleted this line" in w and '"0x20"' in w for w in warnings), warnings)
        git(self.repo, "merge", "--no-edit", "lane")
        self.assertIn('"0x20"', self.ann.read_text(encoding="utf-8"))  # what the warning is about


if __name__ == "__main__":
    unittest.main()
