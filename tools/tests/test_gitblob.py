#!/usr/bin/env python3
"""Tests for tools/gitblob.py: show() answers as `git show rev:path` read as UTF-8 text does. A throwaway repository.

Run: tools/py -I -m pytest tools/tests/test_gitblob.py
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gitblob  # noqa: E402


def git(cwd, *a):
    return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True, encoding="utf-8", check=True).stdout


def real(cwd, name):
    return git(cwd, "show", name)


class Show(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.email", "t@example.invalid")
        git(self.repo, "config", "user.name", "t")
        git(self.repo, "config", "core.autocrlf", "false")
        (self.repo / "sub").mkdir()
        (self.repo / "plain.txt").write_bytes(b"one\ntwo\n")
        (self.repo / "crlf.txt").write_bytes(b"a\r\nb\rc\n")
        (self.repo / "empty.txt").write_bytes(b"")
        (self.repo / "sub/utf8 name.txt").write_bytes("café\n".encode("utf-8"))
        (self.repo / "bad.bin").write_bytes(b"\xff\xfe\n")
        self.commit("first")

    def tearDown(self):
        gitblob._close_all()  # it holds the directory, which Windows then won't delete
        self.tmp.cleanup()

    def commit(self, message):
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", message)
        return git(self.repo, "rev-parse", "HEAD").strip()

    def show(self, rev, path, **kw):
        return gitblob.show(rev, path, cwd=self.repo, **kw)

    def test_same_text_as_git_show(self):
        for path in ("plain.txt", "crlf.txt", "empty.txt", "sub/utf8 name.txt"):
            self.assertEqual(self.show("main", path), real(self.repo, f"main:{path}"), path)

    def test_a_tree_lists_as_git_show_does(self):
        self.assertEqual(self.show("main", "sub"), real(self.repo, "main:sub"))

    def test_tidied_paths_are_left_to_git_show(self):
        for path in ("sub/../plain.txt", "./plain.txt"):
            self.assertEqual(self.show("main", path), real(self.repo, f"main:{path}"), path)

    def test_missing_raises_what_git_show_raises(self):
        for rev, path in (("main", "nothing.txt"), ("no-such-branch", "plain.txt"), ("main", "sub/nothing"),
                          ("main", "sub//utf8 name.txt")):
            with self.assertRaises(subprocess.CalledProcessError) as caught:
                self.show(rev, path)
            self.assertEqual(caught.exception.returncode, 128)

    def test_bad_utf8_raises_or_is_replaced_as_asked(self):
        with self.assertRaises(UnicodeDecodeError):
            self.show("main", "bad.bin")
        self.assertEqual(self.show("main", "bad.bin", errors="replace"), "��\n")

    def test_a_branch_that_moves_is_read_again(self):
        self.assertEqual(self.show("main", "plain.txt"), "one\ntwo\n")
        (self.repo / "plain.txt").write_bytes(b"moved\n")
        (self.repo / "new.txt").write_bytes(b"new\n")
        self.commit("second")
        self.assertEqual(self.show("main", "plain.txt"), "moved\n")
        self.assertEqual(self.show("main", "new.txt"), "new\n")

    def test_a_dead_batch_process_changes_nothing(self):
        self.assertEqual(self.show("main", "plain.txt"), "one\ntwo\n")
        gitblob._batches[str(self.repo)].kill()
        gitblob._batches[str(self.repo)].wait()
        self.assertEqual(self.show("main", "crlf.txt"), real(self.repo, "main:crlf.txt"))

    def test_a_full_id_is_answered_from_memory(self):
        sha = git(self.repo, "rev-parse", "HEAD").strip()
        self.assertEqual(self.show(sha, "plain.txt"), "one\ntwo\n")
        gitblob._batches[str(self.repo)].kill()
        gitblob._batches[str(self.repo)].wait()
        self.assertIn((str(self.repo), sha, "plain.txt"), gitblob._read)
        self.assertEqual(self.show(sha, "plain.txt"), "one\ntwo\n")

    def test_nothing_is_kept_for_a_name_that_can_move(self):
        self.show("main", "plain.txt")
        self.assertNotIn((str(self.repo), "main", "plain.txt"), gitblob._read)


if __name__ == "__main__":
    unittest.main()
