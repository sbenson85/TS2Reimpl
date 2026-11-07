#!/usr/bin/env python3
"""Tests for tools/dbpf.py, tools/savediff.py, tools/sims2_types.py.

Run: .venv/Scripts/python -I tools/tests/test_savediff.py
Tests that need real saves look for local/userdata-baseline in the main checkout (or
$TS2_USERDATA) and are skipped if it is missing.
"""
import glob
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import dbpf                     # noqa: E402
import savediff                 # noqa: E402
import sims2_types as st        # noqa: E402


def _find_hood():
    cands = [os.environ.get("TS2_USERDATA", "")]
    d = HERE
    for _ in range(8):
        cands.append(os.path.join(d, "local", "userdata-baseline"))
        d = os.path.dirname(d)
    for c in cands:
        h = os.path.join(c, "Neighborhoods", "E001")
        if c and os.path.isdir(h):
            return h
    return None


HOOD = _find_hood()


class QfsTests(unittest.TestCase):
    def _blk(self, size, stream):
        return struct.pack("<I", 9 + len(stream)) + b"\x10\xfb" + size.to_bytes(3, "big") + stream

    def test_literal_and_copy(self):
        blk = self._blk(6, bytes([0x03, 0x02]) + b"ABC" + bytes([0xFC]))
        self.assertEqual(dbpf.qfs_decompress(blk), b"ABCABC")

    def test_overlapping_copy(self):
        # command: 1 literal byte, copy length 5 (code 2) from offset 1 -> "A" + "AAAAA"
        stream = bytes([0x01 | (2 << 2), 0x00]) + b"A" + bytes([0xFC])
        self.assertEqual(dbpf.qfs_decompress(self._blk(6, stream)), b"AAAAAA")

    def test_size_mismatch_rejected(self):
        blk = self._blk(7, bytes([0x03, 0x02]) + b"ABC" + bytes([0xFC]))
        with self.assertRaises(dbpf.DbpfError):
            dbpf.qfs_decompress(blk)


class WriterTests(unittest.TestCase):
    def test_roundtrip_minors(self):
        for minor in (1, 2):
            with tempfile.TemporaryDirectory() as td:
                p = os.path.join(td, "x.package")
                items = [((0x1234, 7, 9, 5 if minor == 2 else 0), b"hello"),
                         ((0x1235, 0xFFFFFFFF, 1, 0), b"")]
                dbpf.write_package(p, items, index_minor=minor)
                pk = dbpf.Package(p)
                self.assertEqual(pk.index_minor, minor)
                self.assertEqual([e.key for e in pk.entries], [k for k, _ in items])
                self.assertEqual(pk.get(items[0][0]), b"hello")


class SavediffSyntheticTests(unittest.TestCase):
    def _mk(self, td, name, items):
        os.makedirs(os.path.join(td, "Lots"), exist_ok=True)
        dbpf.write_package(os.path.join(td, "Lots", "a.package"), items)
        return td

    def test_detects_add_remove_change(self):
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            k1, k2, k3 = (0xFA1C39F7, 0xFFFFFFFF, 1, 0), (0x424D505F, 0, 2, 0), (0x99999999, 0, 3, 0)
            self._mk(ta, "a", [(k1, b"\x00" * 100), (k2, b"thumb"), (k3, b"gone")])
            self._mk(tb, "b", [(k1, b"\x00" * 50 + b"\x01" + b"\x00" * 49), (k2, b"THUMB"),
                               ((0x88888888, 0, 4, 0), b"new")])
            rep = savediff.compare(ta, tb)
            res = {r["key"]: r for r in rep["resources"]}
            self.assertEqual(res["FA1C39F7/FFFFFFFF/00000001/00000000"]["ranges"], [[50, 51]])
            self.assertEqual(res["FA1C39F7/FFFFFFFF/00000001/00000000"]["class"], "persistent")
            self.assertEqual(res["424D505F/00000000/00000002/00000000"]["class"], "transient")
            self.assertEqual(res["99999999/00000000/00000003/00000000"]["status"], "removed")
            self.assertEqual(res["88888888/00000000/00000004/00000000"]["status"], "added")
            self.assertEqual(rep["summary"]["persistent"]["changed"], 1)
            self.assertEqual(savediff.main([ta, tb]), 1)

    def test_identical_is_clean(self):
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            items = [((0x1, 2, 3, 0), b"abc")]
            self._mk(ta, "a", items)
            self._mk(tb, "b", items)
            self.assertEqual(savediff.main([ta, tb]), 0)

    def test_transient_only_exit_zero(self):
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            k = (0x424D505F, 0, 2, 0)
            self._mk(ta, "a", [(k, b"a")])
            self._mk(tb, "b", [(k, b"b")])
            self.assertEqual(savediff.main([ta, tb]), 0)

    def test_field_exclusion(self):
        # cProxyManager map: only the uninitialised dword after type id + version differs -> excluded;
        # a difference elsewhere is still reported, with the excluded bytes masked.
        k = (0xACE46235, 0xFFFFFFFF, 0, 0)
        head = b"\x00" * 0x40 + b"\x35\x62\xe4\xac\x01\x00\x00\x00"
        a = head + b"\x0e\x00\x00\x00" + b"\x0d\x00\x00\x00rest"
        b = head + b"flag" + b"\x0d\x00\x00\x00rest"
        c = head + b"flag" + b"\x0d\x00\x00\x00REST"
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            self._mk(ta, "a", [(k, a)])
            self._mk(tb, "b", [(k, b)])
            rep = savediff.compare(ta, tb)
            self.assertEqual(rep["resources"][0]["class"], "excluded")
            self.assertIn("uninitialised", rep["resources"][0]["reason"])
            self.assertEqual(savediff.main([ta, tb]), 0)
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            self._mk(ta, "a", [(k, a)])
            self._mk(tb, "b", [(k, c)])
            r = savediff.compare(ta, tb)["resources"][0]
            self.assertEqual(r["class"], "unknown")
            self.assertEqual(r["ranges"], [[0x50, 0x54]])
            self.assertEqual(savediff.main([ta, tb]), 1)

    def test_diff_ranges(self):
        a = bytes(1000)
        b = bytearray(a)
        b[3] = 1
        b[4] = 1
        b[900] = 9
        r, n = savediff.diff_ranges(a, bytes(b) + b"xx")
        self.assertEqual(r, [(3, 5), (900, 901), (1000, 1002)])
        self.assertEqual(n, 3)


class ParserSyntheticTests(unittest.TestCase):
    def test_fami(self):
        w = [0x46414D49, 0x55, 0, 5, 5, 0, 9, 1234, 0, 0, 2, 0xA, 0xB, 0xC, 1, 1234]
        d = struct.pack("<%dI" % len(w), *w)
        f = savediff.parse_fami(d)
        self.assertEqual(f["funds"], 1234)
        self.assertEqual(f["funds_tail"], 1234)
        self.assertEqual(f["member_count"], 2)
        diff = savediff.field_diff(st_FAMI, d, d.replace(struct.pack("<I", 1234) * 1,
                                                          struct.pack("<I", 99), 1))
        self.assertEqual(diff[0][0], "funds")


st_FAMI = 0x46414D49


@unittest.skipUnless(HOOD, "real saves not available")
class RealDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.neigh = dbpf.Package(os.path.join(HOOD, "E001_Neighborhood.package"))

    def test_decompress_everything_in_samples(self):
        files = [os.path.join(HOOD, "E001_Neighborhood.package"),
                 os.path.join(HOOD, "E001_Suburb001.package")]
        files += sorted(glob.glob(os.path.join(HOOD, "Characters", "*.package")))[:3]
        files += sorted(glob.glob(os.path.join(HOOD, "Lots", "*.package")))[:3]
        total = 0
        for f in files:
            pk = dbpf.Package(f)
            for e in pk.entries:
                data = pk.read(e)
                self.assertEqual(len(data), e.uncompressed_size)
                total += e.compressed
        self.assertGreater(total, 100)

    def test_fami_funds_consistent(self):
        n = 0
        for e in self.neigh.entries:
            if e.type == st_FAMI:
                f = savediff.parse_fami(self.neigh.read(e))
                self.assertEqual(f["funds"], f["funds_tail"])
                n += 1
        self.assertGreater(n, 10)

    def test_sdsc_guid(self):
        n = 0
        for e in self.neigh.entries:
            if e.type == 0xAACE2EFB and e.uncompressed_size >= 0x1E0:
                savediff.parse_sdsc(self.neigh.read(e))
                n += 1
        self.assertGreater(n, 100)

    def test_lot_objects_parse(self):
        f = sorted(glob.glob(os.path.join(HOOD, "Lots", "*.package")))[0]
        pk = dbpf.Package(f)
        n = bad = 0
        for e in pk.entries:
            if e.type == 0xFA1C39F7:
                n += 1
                try:
                    r = savediff.parse_lot_object(pk.read(e))
                    for v in r["position"]:
                        self.assertTrue(abs(v) < 2000)
                except Exception:           # noqa: BLE001
                    bad += 1
        self.assertGreater(n, 0)
        self.assertLessEqual(bad, n // 10)

    def test_class_table_covers_real_types(self):
        seen = set()
        for e in self.neigh.entries:
            seen.add(e.type)
        for t in seen:
            self.assertIn(st.type_class(t), (st.P, st.T, st.U))


@unittest.skipUnless(HOOD, "real saves not available")
class SelfCheckTests(unittest.TestCase):
    """Copy one neighbourhood package twice; identical -> clean; rewrite with a one-byte
    change in one resource (stored uncompressed) -> only that resource differs."""

    def test_one_byte_change_detected(self):
        src = os.path.join(HOOD, "E001_Neighborhood.package")
        with tempfile.TemporaryDirectory() as ta, tempfile.TemporaryDirectory() as tb:
            shutil.copy(src, os.path.join(ta, "n.package"))
            shutil.copy(src, os.path.join(tb, "n.package"))
            self.assertEqual(savediff.main([ta, tb]), 0)
            pk = dbpf.Package(src)
            items = []
            target = None
            for e in pk.entries:
                if e.type == dbpf.DIR_TYPE:
                    continue
                d = pk.read(e)
                if target is None and e.type == st_FAMI and e.compressed and len(d) > 40:
                    target = e.key
                    d = bytearray(d)
                    d[0x1C] ^= 1          # funds low byte
                    d = bytes(d)
                items.append((e.key, d))
            self.assertIsNotNone(target)
            dbpf.write_package(os.path.join(tb, "n.package"), items, pk.index_minor)
            rep = savediff.compare(ta, tb)
            self.assertEqual(len(rep["resources"]), 1)
            r = rep["resources"][0]
            self.assertEqual(r["status"], "changed")
            self.assertEqual(r["ranges"], [[0x1C, 0x1D]])
            self.assertEqual(r["fields"][0][0], "funds")
            self.assertEqual(savediff.main([ta, tb]), 1)


if __name__ == "__main__":
    unittest.main()
