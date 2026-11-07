#!/usr/bin/env python3
"""Tests for tools/snapstore.py, tools/diskuse.py and the free-space floor of tools/smoke.py, on synthetic folders
in a temporary directory (store and snapshots on the same volume).

Run: tools/py -I tools/tests/test_snapstore.py
"""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import diskuse                  # noqa: E402
import lotcheck                 # noqa: E402
import savediff                 # noqa: E402
import smoke                    # noqa: E402
import snapstore                # noqa: E402

T0 = 1_700_000_000_000_000_000   # a fixed modification time (ns), as robocopy keeps the baseline's


def write(path, data, mtime_ns=T0):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    os.utime(path, ns=(mtime_ns, mtime_ns))


def tree(root):
    """{relpath: (bytes, mtime_ns)} for files and {relpath/: None} for folders."""
    out = {}
    for d, dirs, files in os.walk(root):
        for x in dirs:
            out[os.path.relpath(os.path.join(d, x), root) + "/"] = None
        for f in files:
            p = os.path.join(d, f)
            with open(p, "rb") as fh:
                out[os.path.relpath(p, root)] = (fh.read(), os.stat(p).st_mtime_ns)
    return out


def read(path):
    with open(path, "rb") as f:
        return f.read()


def fid(path):
    st = os.stat(path)
    return st.st_dev, st.st_ino


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="snapstore-test-")
        self.store = snapstore.Store(os.path.join(self.tmp, "snapstore"))
        self.live = os.path.join(self.tmp, "live")
        write(os.path.join(self.live, "Neighborhoods", "E001", "Lots", "E001_Lot10.package"), b"lot10" * 100)
        write(os.path.join(self.live, "Neighborhoods", "E001", "E001_Neighborhood.package"), b"hood" * 50)
        write(os.path.join(self.live, "Thumbnails", "CASThumbnails.package"), b"thumbs" * 200)
        write(os.path.join(self.live, "Config", "userProps.xml"), b"<props/>")
        write(os.path.join(self.live, "Copy", "same.package"), b"thumbs" * 200)   # same bytes and time as another
        os.makedirs(os.path.join(self.live, "Empty", "Deeper"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def p(self, *parts):
        return os.path.join(self.tmp, *parts)

    def objects(self):
        return sorted(o.name for o in self.store.iter_objects())


class SnapTests(Base):
    def test_snapshot_reads_as_a_copy(self):
        self.store.snap(self.live, self.p("s1"))
        self.assertEqual(tree(self.p("s1")), tree(self.live))           # contents, times, empty folders
        self.assertEqual(lotcheck._tree_digest(self.p("s1")), lotcheck._tree_digest(self.live))
        rep = savediff.compare(self.live, self.p("s1"))
        self.assertEqual((rep["same_files"], rep["files"], rep["resources"]), (5, [], []))

    def test_unchanged_files_are_stored_once(self):
        self.store.snap(self.live, self.p("s1"))
        self.assertEqual(len(self.objects()), 4)                         # the two identical files share one
        lot = os.path.join("Neighborhoods", "E001", "Lots", "E001_Lot10.package")
        write(os.path.join(self.live, lot), b"resaved" * 100, T0 + 10**9)   # the run saves the lot
        self.store.snap(self.live, self.p("s2"))
        self.assertEqual(len(self.objects()), 5)                         # only the changed file is new
        thumbs = os.path.join("Thumbnails", "CASThumbnails.package")
        self.assertEqual(fid(self.p("s1", thumbs)), fid(self.p("s2", thumbs)))
        self.assertNotEqual(fid(self.p("s1", lot)), fid(self.p("s2", lot)))
        self.assertEqual(tree(self.p("s2")), tree(self.live))
        self.assertEqual(read(self.p("s1", lot)), b"lot10" * 100)   # the old snapshot keeps its bytes
        self.assertEqual((self.store.linked, self.store.copied), (5, 5))   # s1: 4 new + 1 twin; s2: 4 + 1 new

    def test_same_bytes_new_time_is_its_own_object(self):
        """A file the game rewrote with the same bytes keeps its new time (lotcheck.lot_resaved compares times)."""
        self.store.snap(self.live, self.p("s1"))
        cfg = os.path.join("Config", "userProps.xml")
        os.utime(os.path.join(self.live, cfg), ns=(T0 + 500, T0 + 500))   # NTFS keeps 100 ns steps
        self.store.snap(self.live, self.p("s2"))
        self.assertEqual(os.stat(self.p("s2", cfg)).st_mtime_ns, T0 + 500)
        self.assertEqual(os.stat(self.p("s1", cfg)).st_mtime_ns, T0)

    def test_snapshot_is_written_once(self):
        self.store.snap(self.live, self.p("s1"))
        with self.assertRaises(FileExistsError):
            self.store.snap(self.live, self.p("s1"))

    def test_link_limit_starts_a_second_object(self):
        old = snapstore.MAX_LINKS
        snapstore.MAX_LINKS = 3                                         # object + 2 links
        try:
            for i in range(4):
                self.store.snap(self.live, self.p(f"s{i}"))
        finally:
            snapstore.MAX_LINKS = old
        cfg = os.path.join("Config", "userProps.xml")
        self.assertEqual(fid(self.p("s0", cfg)), fid(self.p("s1", cfg)))
        self.assertNotEqual(fid(self.p("s1", cfg)), fid(self.p("s2", cfg)))
        self.assertTrue(any(n.endswith(".1") for n in self.objects()))
        for i in range(4):
            self.assertEqual(tree(self.p(f"s{i}")), tree(self.live))

    def test_gc_keeps_linked_objects_only(self):
        self.store.snap(self.live, self.p("s1"))
        lot = os.path.join(self.live, "Neighborhoods", "E001", "Lots", "E001_Lot10.package")
        write(lot, b"resaved", T0 + 1)
        self.store.snap(self.live, self.p("s2"))
        self.assertEqual(self.store.gc(), (0, 0))
        shutil.rmtree(self.p("s1"))                                      # smoke.py prune
        self.assertEqual(self.store.gc(dry_run=True), (1, 500))
        self.assertEqual(len(self.objects()), 5)
        self.assertEqual(self.store.gc(), (1, 500))                      # the old lot only
        self.assertEqual(len(self.objects()), 4)
        self.assertEqual(tree(self.p("s2")), tree(self.live))

    def test_verify_finds_a_write_through_a_link(self):
        self.store.snap(self.live, self.p("s1"))
        self.assertEqual(self.store.verify(), [])
        with open(self.p("s1", "Config", "userProps.xml"), "r+b") as f:   # what must never happen
            f.write(b"X")
        bad = self.store.verify()
        self.assertEqual(len(bad), 1)
        self.assertIn("content changed", bad[0][1])

    def test_break_link_keeps_the_edit_private(self):
        self.store.snap(self.live, self.p("s1"))
        self.store.snap(self.live, self.p("s2"))
        hood = self.p("s1", "Neighborhoods", "E001", "E001_Neighborhood.package")
        self.assertTrue(snapstore.break_link(hood))
        with open(hood, "r+b") as f:                                     # setfunds.py's in-place edit
            f.write(b"EDIT")
        self.assertEqual(read(self.p("s2", "Neighborhoods", "E001", "E001_Neighborhood.package")),
                         b"hood" * 50)
        self.assertEqual(self.store.verify(), [])
        self.assertFalse(snapstore.break_link(hood))                     # its own file now

    def test_adopt_turns_copies_into_links(self):
        shutil.copytree(self.live, self.p("old1"))                       # snapshots made by robocopy
        shutil.copytree(self.live, self.p("old2"))
        before = tree(self.p("old1"))
        self.store.adopt(self.p("old1"))
        self.store.adopt(self.p("old2"))
        self.assertEqual(tree(self.p("old1")), before)
        self.assertEqual(tree(self.p("old2")), before)
        thumbs = os.path.join("Thumbnails", "CASThumbnails.package")
        self.assertEqual(fid(self.p("old1", thumbs)), fid(self.p("old2", thumbs)))
        self.assertEqual(len(self.objects()), 4)
        self.store.adopt(self.p("old1"))                                 # again: nothing changes
        self.assertEqual(len(self.objects()), 4)
        self.assertEqual(tree(self.p("old1")), before)
        self.assertEqual(self.store.verify(), [])
        self.assertEqual([n for n in os.listdir(self.p("old1", "Config"))], ["userProps.xml"])

    def test_adopt_leaves_outside_links_out_of_the_store(self):
        """A file also linked from elsewhere (something that may be written in place) is not made an object:
        the store gets its own copy."""
        shutil.copytree(self.live, self.p("old"))
        cfg = os.path.join("Config", "userProps.xml")
        os.unlink(self.p("old", cfg))
        os.link(os.path.join(self.live, cfg), self.p("old", cfg))       # e.g. a link to live user data
        self.store.adopt(self.p("old"))
        with open(os.path.join(self.live, cfg), "r+b") as f:            # the game writes its file
            f.write(b"GAME")
        self.assertEqual(read(self.p("old", cfg)), b"<props/>")
        self.assertEqual(self.store.verify(), [])

    def test_adopt_refuses_folders_written_in_place(self):
        local = str(snapstore.tsdb.main_root() / "local")
        self.assertIsNotNone(snapstore.adopt_refusal(os.path.join(local, "baselines", "x"), self.store.root))
        self.assertIsNotNone(snapstore.adopt_refusal(os.path.join(local, "userdata-3"), self.store.root))
        self.assertIsNotNone(snapstore.adopt_refusal(os.path.join(local, "game-2", "EP9"), self.store.root))
        self.assertIsNotNone(snapstore.adopt_refusal(str(self.store.root / "objects"), self.store.root))
        self.assertIsNone(snapstore.adopt_refusal(os.path.join(local, "traces", "b", "r-1"), self.store.root))
        self.assertIsNone(snapstore.adopt_refusal(os.path.join(local, "fixtures", "lot-resave"), self.store.root))


class LotcheckRecordTests(Base):
    """lotcheck record links a fixture's user data (and a reload fixture's previous lots) into the store."""

    def test_record_links_into_the_store(self):
        snap = self.p("traces", "20261019-000000-saved")
        self.store.snap(self.live, snap)
        with open(snap + ".ts2host.log", "w") as f:
            f.write("TS2HOST_NOHOOKS\n")
        base = self.p("baseline")
        shutil.copytree(self.live, base)
        stubs = {"read_log": lambda s: (None, "TS2HOST_NOHOOKS"),
                 "binding_of": lambda s, t: {"seed": 1, "controls": [], "draws": False, "scenario_sha256": "a",
                                             "baseline_sha256": "b"},
                 "check": lambda s, quiet=False: (lotcheck.UNSEEN, "all", False),
                 "fixtures": lambda state: [], "chain_of": lambda s: [base, s], "git_rev": lambda: "test",
                 "FIXTURES": self.p("fixtures")}
        saved = {k: getattr(lotcheck, k) for k in stubs}
        old_store = snapstore.STORE
        try:
            for k, v in stubs.items():
                setattr(lotcheck, k, v)
            snapstore.STORE = self.store.root
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(lotcheck.record(snap), 0)
        finally:
            for k, v in saved.items():
                setattr(lotcheck, k, v)
            snapstore.STORE = old_store
        fixture = self.p("fixtures", "all", "1")
        self.assertEqual(tree(os.path.join(fixture, "userdata")), tree(self.live))
        lot = os.path.join("Neighborhoods", "E001", "Lots", "E001_Lot10.package")
        self.assertEqual(fid(os.path.join(fixture, "userdata", lot)), fid(os.path.join(snap, lot)))
        self.assertEqual(read(os.path.join(fixture, "previous", lot)), read(os.path.join(base, lot)))
        self.assertEqual(fid(os.path.join(fixture, "previous", lot)), fid(os.path.join(snap, lot)))
        self.assertEqual(len(self.objects()), 4)                         # nothing new stored


class DiskuseTests(Base):
    def test_hardlinks_counted_once(self):
        self.store.snap(self.live, self.p("s1"))
        self.store.snap(self.live, self.p("s2"))
        u = diskuse.Usage(depth=1)
        u.add_root("t", self.tmp)
        rows = {r["item"]: r for r in u.report()}
        size = sum(len(v[0]) for v in tree(self.live).values() if v)
        distinct = size - 1200                                           # Copy/same.package shares an object
        self.assertEqual(rows["t/live"]["size"], size)
        self.assertEqual(rows["t/live"]["own"], size)
        self.assertEqual(rows["t/s1"]["size"], distinct)
        self.assertEqual(rows["t/s1"]["logical"], size)
        self.assertEqual(rows["t/s1"]["own"], 0)                         # s2 links the same objects
        self.assertEqual(rows["t/snapstore"]["own"], 0)                  # gc would free nothing
        self.assertEqual(sum(s for s, _ in u.files.values()), size + distinct)
        shutil.rmtree(self.p("s2"))
        u = diskuse.Usage(depth=1)
        u.add_root("t", self.tmp)
        rows = {r["item"]: r for r in u.report()}
        self.assertEqual(rows["t/s1"]["own"], distinct)                  # the store's links don't count

    def test_report_runs(self):
        out = os.path.join(self.tmp, "du.json")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(diskuse.main([self.tmp, "--depth", "1", "--files", "2", "--json", out]), 0)
        self.assertTrue(os.path.exists(out))


class FloorTests(unittest.TestCase):
    def test_free_space_floor(self):
        self.assertIsNone(smoke.free_space_problem(0, HERE))
        msg = smoke.free_space_problem(10**9, HERE)
        self.assertIn("GB free", msg)
        self.assertIn("--min-free-gb", msg)


if __name__ == "__main__":
    unittest.main()
