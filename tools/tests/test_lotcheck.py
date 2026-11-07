#!/usr/bin/env python3
"""Negative tests for the lot-save gate (tools/lotcheck.py): each case copies a real reference
fixture, changes one thing, and checks the outcome. Needs the fixtures in local/ (skipped otherwise).

Run: .venv/Scripts/python -I tools/tests/test_lotcheck.py      (~3-5 min: each check reads ~10 fixtures)
"""
import json
import math
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import dbpf                     # noqa: E402
import lotcheck as L            # noqa: E402

LOT = "Neighborhoods/E001/Lots/E001_Lot6.package"
CAMERA = bytes(64) + bytes([4, 7, 1]) + bytes(4) + b"\1" + struct.pack("<f", 650.0) + struct.pack("<I", 2) + b"\0" + \
    struct.pack("<5f", 1.0, 0.515, 2000.0, 1500.0, 312.5) + bytes(8) + bytes(6)
OBJM = (0x4F626A4D, 0xFFFFFFFF, 1, 0)
OPEN_KEY = (0xEC44BDDC, 0xFFFFFFFF, 0x262A, 0)
BASE = os.path.join(L.FIXTURES, "age-0x08", "1")      # lot objects 0x198/0x199/0x19B
OTHER = os.path.join(L.FIXTURES, "age-0x08", "3")     # lot objects 0x199/0x19A/0x19B


def rewrite(path, change):
    """Rewrite a package with change(dict key -> bytes) applied (stored uncompressed, no DIR)."""
    pk = dbpf.Package(path)
    items = {e.key: pk.read(e) for e in pk.entries if e.type != dbpf.DIR_TYPE}
    change(items)
    dbpf.write_package(path, list(items.items()))


def camera_record(loaded, turn, dx, dy=0.0):
    yaw = struct.unpack_from("<f", loaded, L.CAMERA_YAW_AT)[0]
    x, y = struct.unpack_from("<2f", loaded, L.CAMERA_FOCUS_AT)
    return patched(patched(loaded, L.CAMERA_YAW_AT, struct.pack("<f", yaw + turn)),
                   L.CAMERA_FOCUS_AT, struct.pack("<2f", x + dx, y + dy))


def patched(data, off, new):
    b = bytearray(data)
    b[off:off + len(new)] = new
    return bytes(b)


@unittest.skipUnless(os.path.isdir(BASE) and os.path.isdir(OTHER), "lot-save fixtures not available")
class Gate(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.snap = os.path.join(self.tmp, "run-saved")
        shutil.copytree(os.path.join(BASE, "userdata"), self.snap)
        shutil.copy(os.path.join(BASE, "ts2host.log"), self.snap + ".ts2host.log")
        b = L.fixture_meta(BASE)["binding"]
        self.manifest = {"seed": hex(b["seed"]), "scenario_sha256": b["scenario_sha256"],
                         "baseline_sha256": b["baseline_sha256"]}
        self.write_manifest()
        L._package.cache_clear()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write_manifest(self):
        with open(self.snap + ".run.json", "w") as f:
            json.dump(self.manifest, f)

    def path(self, rel):
        return os.path.join(self.snap, rel)

    def outcome(self):
        L._package.cache_clear()
        return L.check(self.snap, quiet=True)[0]

    def constant_resource(self):
        """A Lot6 resource identical in every compatible fixture."""
        vals = None
        for _, f in L.all_fixtures():
            pk = dbpf.Package(os.path.join(f, "userdata", LOT))
            cur = {e.key: pk.read(e) for e in pk.entries if e.type != dbpf.DIR_TYPE}
            vals = cur if vals is None else {k: v for k, v in vals.items() if cur.get(k) == v}
        return next(k for k, v in sorted(vals.items()) if len(v) > 8)

    # -- passes

    def test_identical_copy_matches(self):
        self.assertEqual(self.outcome(), L.MATCH)

    def test_match_against_backfilled_fixture_is_diagnostic_only(self):
        self.assertFalse(L.check(self.snap, quiet=True)[2])

    def test_match_against_captured_fixture_is_accepted(self):
        meta = dict(L.fixture_meta(BASE), binding_source=L.CAPTURED)
        real = L.fixture_meta
        L.fixture_meta = lambda f: meta if os.path.samefile(f, BASE) else real(f)
        try:
            self.assertEqual(L.check(self.snap, quiet=True)[::2], (L.MATCH, True))
            os.remove(self.snap + ".run.json")      # no run manifest: not acceptance evidence
            self.assertEqual(L.check(self.snap, quiet=True)[::2], (L.MATCH, False))
        finally:
            L.fixture_meta = real

    def camera(self, turn, dx):
        """Give the run a camera record this far from the baseline's (the fixture has its own)."""
        loaded = L.resource(L.BASELINE, L.NEIGHBORHOOD, L.CAMERA_KEY)
        new = camera_record(loaded, turn, dx)
        rewrite(self.path(L.NEIGHBORHOOD), lambda it: it.__setitem__(OPEN_KEY, new))

    def test_known_open_inside_ranges_is_match_open(self):
        self.camera(0.1, 50.0)
        self.assertEqual(self.outcome(), L.MATCH_OPEN)

    def test_known_open_relation_refuses_what_the_mask_allowed(self):
        # The top byte of the yaw: inside the byte ranges, so the mask alone passed it.
        rewrite(self.path(L.NEIGHBORHOOD), lambda it: it.__setitem__(OPEN_KEY, patched(it[OPEN_KEY], 0x54, b"\x7f")))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_known_open_camera_that_never_moved(self):
        self.camera(0.1, 0.5)
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    # -- unexpected differences (block acceptance)

    def test_missing_package(self):
        os.remove(self.path(L.NEIGHBORHOOD))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_added_package(self):
        shutil.copy(self.path(LOT), self.path("Neighborhoods/E001/Lots/E001_Lot99.package"))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_package_that_does_not_parse(self):
        with open(self.path(LOT), "r+b") as f:
            f.write(b"XXXX")
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_constant_resource_changed(self):
        key = self.constant_resource()
        rewrite(self.path(LOT), lambda it: it.__setitem__(key, patched(it[key], 4, bytes([it[key][4] ^ 0xff]))))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_resource_added(self):
        rewrite(self.path(LOT), lambda it: it.__setitem__((0x12345678, 0, 1, 0), b"new"))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_resource_removed(self):
        key = self.constant_resource()
        rewrite(self.path(LOT), lambda it: it.pop(key))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_known_open_outside_ranges(self):
        # 0x55 lies between the open ranges and is constant in every reference.
        rewrite(self.path(L.NEIGHBORHOOD), lambda it: it.__setitem__(OPEN_KEY, patched(it[OPEN_KEY], 0x55, b"\x7f")))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_known_open_size_change(self):
        rewrite(self.path(L.NEIGHBORHOOD), lambda it: it.__setitem__(OPEN_KEY, it[OPEN_KEY] + b"\0"))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_objm_constant_byte(self):
        rewrite(self.path(LOT), lambda it: it.__setitem__(OBJM, patched(it[OBJM], 0x10, bytes([it[OBJM][0x10] ^ 0xff]))))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    # -- not a pass, not a demonstrated regression

    def test_objm_new_value_where_references_vary(self):
        # Word 0x58 holds a lot-object id (0x199 or 0x19A in references); 0x1FF was never seen. Its low
        # byte varies between references, its high byte does not: only the low byte is changed.
        rewrite(self.path(LOT), lambda it: it.__setitem__(OBJM, patched(it[OBJM], 0x58, b"\xff")))
        self.assertEqual(self.outcome(), L.UNSEEN)

    def test_lot_objects_from_another_fixture_without_their_objm(self):
        # Combining recorded components from two fixtures must not pass: OBJM still refers to the old ids.
        other = dbpf.Package(os.path.join(OTHER, "userdata", LOT))
        donor = {e.key: other.read(e) for e in other.entries
                 if e.type in (0x584F424A, 0xFA1C39F7) and 0x198 <= e.instance <= 0x19B}

        def swap(it):
            for k in [k for k in it if k[0] in (0x584F424A, 0xFA1C39F7) and 0x198 <= k[2] <= 0x19B]:
                del it[k]
            it.update(donor)
        rewrite(self.path(LOT), swap)
        self.assertEqual(self.outcome(), L.UNSEEN)

    # -- binding

    def test_other_baseline_has_no_fixture(self):
        self.manifest["baseline_sha256"] = "0" * 64
        self.write_manifest()
        self.assertEqual(self.outcome(), L.NO_FIXTURE)

    def test_other_controls_have_no_fixture(self):
        log = self.snap + ".ts2host.log"
        with open(log, encoding="latin-1") as f:
            text = f.read().replace("42 ms per frame", "40 ms per frame")
        with open(log, "w", encoding="latin-1") as f:
            f.write(text)
        self.assertEqual(self.outcome(), L.NO_FIXTURE)


# ---- normalised classes (object ids, schedule countdowns, the pass counter) --------------------------------

def schedule(objects, words, n=8):
    """A synthetic OBJM instance 1: 64 zero bytes, header, (id, type) pairs, n schedule words, a tail."""
    out = bytearray(0x40) + struct.pack("<4I", 0, 0xD8, 0x4F626A4D, len(objects))
    for i, t in objects:
        out += struct.pack("<II", i, t)
    out += struct.pack("<I", n) + b"".join(struct.pack("<I", words.get(i, 0)) for i in range(1, n + 1))
    return bytes(out + b"tail")


class Pure(unittest.TestCase):
    """The normalisation helpers on synthetic data (no fixtures needed)."""

    def lay(self, objects, words=None):
        import savediff
        return savediff.objm_layout(schedule(objects, words or {}))

    def test_layout(self):
        lay = self.lay([(1, 0x10), (3, 0x20)], {1: 0x30005})
        self.assertEqual(lay["objects"], [(1, 0x10), (3, 0x20)])
        self.assertEqual(lay["schedule"][0], 0x30005)
        self.assertIsNone(__import__("savediff").objm_layout(b"\0" * 0x40 + struct.pack("<4I", 0, 0xD7, 0x4F626A4D, 0)))

    def test_fresh_objects_matched_by_type(self):
        B = self.lay([(1, 0x10)])
        F = self.lay([(4, 0x51), (3, 0x52), (1, 0x10)])
        R = self.lay([(3, 0x51), (2, 0x52), (1, 0x10)])
        pairs, why = L.id_mapping(F, R, B)
        self.assertIsNone(why)
        self.assertEqual(pairs, {3: 4, 2: 3, 4: 2})       # completed to a permutation over the free id 2

    def test_baseline_objects_keep_their_ids(self):
        B = self.lay([(1, 0x10), (2, 0x11)])
        F = self.lay([(1, 0x10), (2, 0x11)])
        R = self.lay([(2, 0x10), (1, 0x11)])               # two baseline objects swapped: not a fresh permutation
        self.assertIsNone(L.id_mapping(F, R, B)[0])

    def test_fresh_type_ambiguous_or_missing(self):
        B = self.lay([])
        self.assertIsNone(L.id_mapping(self.lay([(1, 5), (2, 5)]), self.lay([(3, 5), (4, 5)]), B)[0])
        # Same id and type on both sides pair first; the rest is unique again (records must still agree).
        self.assertEqual(L.id_mapping(self.lay([(1, 5), (2, 5)]), self.lay([(2, 5), (3, 5)]), B)[0], {3: 1, 1: 3})
        self.assertIsNone(L.id_mapping(self.lay([(1, 5)]), self.lay([(1, 6)]), B)[0])

    def test_id_words(self):
        pairs = {0xF5: 0x186, 0x186: 0xF5}
        run = struct.pack("<HHH", 7, 0xF5, 0x186)
        self.assertEqual(L.id_words_only(run, struct.pack("<HHH", 7, 0x186, 0xF5), pairs), [])
        self.assertTrue(L.id_words_only(run, struct.pack("<HHH", 8, 0x186, 0xF5), pairs))      # other field
        self.assertTrue(L.id_words_only(run, struct.pack("<HHH", 7, 0xFE, 0xF5), pairs))       # broken relation
        # Limit: equal bytes are not examined, so a run id equal to an unrelated fixture id is not caught here.
        self.assertEqual(L.id_words_only(run, struct.pack("<HHH", 7, 0xF5, 0xF5), pairs), [])
        self.assertEqual(L.id_words_only(run, run + b"\0", pairs), [-1])

    def test_countdowns(self):
        base = [0x30000 | 500, 0x30000 | 10, 0x10000 | 40, 0x30000 | 600]
        side = [0x30000 | 400, 0x30000 | 77, 0x10000 | 40, 0x30000 | 501]   # t = 100; id 4 one pass short
        out, t, cut = L.countdowns(base, side, [1, 2, 3, 4])
        self.assertEqual((t, cut), (100, 2))
        self.assertEqual(out[1], (True, True, 100, False))
        self.assertEqual(out[2][2:], (99, True))         # countdown 10 ran out within the passes
        self.assertEqual(out[3][:2], (True, False))      # no counting flags
        self.assertEqual(out[4][2], 99)

    def test_pass_counter_from_each_sides_own_baseline(self):
        key = "584F424A/FFFFFFFF/00000173/00000000"
        at = L.PASS_COUNTERS[(LOT10, key)][0]
        rec = lambda w: bytes(at) + struct.pack("<H", w) + bytes(4)
        run_obj, fix_obj = (True, True, 40, True), (True, True, 33, True)
        # A reload: the run loaded word 5, the fixture word 7 (their own first saves).
        self.assertIsNotNone(L.pass_counter(LOT10, key, [at], rec(15), rec(10), rec(5), rec(7), run_obj, fix_obj))
        self.assertIsNone(L.pass_counter(LOT10, key, [at], rec(15), rec(11), rec(5), rec(7), run_obj, fix_obj))
        self.assertIsNone(L.pass_counter(LOT10, key, [at], rec(15), rec(10), rec(7), rec(7), run_obj, fix_obj))
        self.assertIsNone(L.pass_counter(LOT10, key, [at + 4], rec(15), rec(10), rec(5), rec(7), run_obj, fix_obj))


class Camera(unittest.TestCase):
    """The camera record's relation: synthetic records, so these run without fixtures."""

    def test_layout_of_the_synthetic_record(self):
        self.assertEqual(len(CAMERA), 115)
        self.assertEqual(struct.unpack_from("<f", CAMERA, 0x55)[0], 0.5149999856948853)

    def test_one_session(self):
        for turn, dx, dy in ((0.0, 3.9, 0.0), (0.2245, 134.5, 0.0), (-0.128, -30.0, 40.0), (0.5, 0.0, -249.0)):
            self.assertIsNone(L.camera_session(camera_record(CAMERA, turn, dx, dy), CAMERA), (turn, dx, dy))

    def test_yaw_wraps(self):
        loaded = camera_record(CAMERA, 6.2 - 1.0, 0.0)
        self.assertIsNone(L.camera_session(camera_record(loaded, 0.2 - 6.2831855, 50.0), loaded))

    def test_refused(self):
        nan = struct.pack("<f", float("nan"))
        for name, saved in (
                ("turned past the window", camera_record(CAMERA, 0.53, 50.0)),
                ("turned back past the window", camera_record(CAMERA, -0.37, 50.0)),
                ("half way round", camera_record(CAMERA, 3.14, 50.0)),
                ("focus out of reach", camera_record(CAMERA, 0.1, 177.0, 177.0)),
                ("the walk did not run", camera_record(CAMERA, 0.1, 0.5, 0.5)),
                ("focus as loaded", camera_record(CAMERA, 0.1, 0.0)),
                ("yaw that wraps into the window", camera_record(CAMERA, 0.1 + 2000 * math.pi, 50.0)),
                ("yaw infinite", patched(camera_record(CAMERA, 0.1, 50.0), L.CAMERA_YAW_AT, struct.pack("<f", math.inf))),
                ("yaw not a number", patched(camera_record(CAMERA, 0.1, 50.0), L.CAMERA_YAW_AT, nan)),
                ("focus not a number", patched(camera_record(CAMERA, 0.1, 50.0), L.CAMERA_FOCUS_AT, nan)),
                ("height moved", patched(camera_record(CAMERA, 0.1, 50.0), 0x61, struct.pack("<f", 300.0))),
                ("pitch moved", patched(camera_record(CAMERA, 0.1, 50.0), 0x55, struct.pack("<f", 0.6))),
                ("another record", CAMERA[:-1])):
            self.assertIsNotNone(L.camera_session(saved, CAMERA), name)
        broken = patched(CAMERA, L.CAMERA_YAW_AT, struct.pack("<f", math.inf))
        self.assertIsNotNone(L.camera_session(CAMERA, broken), "the loaded record broken")

    def tree(self, *parts, record):
        root = os.path.join(self.tmp, *parts)
        os.makedirs(os.path.dirname(os.path.join(root, L.NEIGHBORHOOD)))
        dbpf.write_package(os.path.join(root, L.NEIGHBORHOOD), [(OPEN_KEY, record)])
        return root

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        L._package.cache_clear()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)
        L._package.cache_clear()

    def test_both_sides_each_from_what_it_loaded(self):
        first, other = camera_record(CAMERA, 0.1, 100.0), camera_record(CAMERA, 0.1, -100.0)
        root = self.tree("root", record=CAMERA)
        run_first = self.tree("run-first", record=first)
        run = self.tree("run", record=camera_record(first, 0.1, 100.0))             # 200 from the root, 100 from first
        fixture = os.path.join(self.tmp, "fixture")
        self.tree("fixture", "previous", record=other)
        self.tree("fixture", "userdata", record=camera_record(other, 0.1, -60.0))
        self.assertEqual(L.camera_relation(fixture, run, (root, run_first)), (None, ""))

        # A baseline too far from the save refuses the run's side.
        far = self.tree("far", record=camera_record(first, 0.1, 600.0))
        self.assertIn("this run", L.camera_relation(fixture, run, (root, far))[0])

        # The fixture's side has its own bounds, whatever the run does.
        self.tree("broken", "previous", record=other)
        self.tree("broken", "userdata", record=camera_record(other, 0.1, 0.0))
        self.assertIn("the fixture", L.camera_relation(os.path.join(self.tmp, "broken"), run, (root, run_first))[0])

    def test_first_save_fixture_loaded_the_root(self):
        root = self.tree("root", record=CAMERA)
        run = self.tree("run", record=camera_record(CAMERA, 0.1, 100.0))
        self.tree("fixture", "userdata", record=camera_record(CAMERA, 0.1, -60.0))
        fixture = os.path.join(self.tmp, "fixture")
        self.assertEqual(L.camera_relation(fixture, run, (root, root)), (None, ""))
        self.tree("broken", "userdata", record=camera_record(CAMERA, 2.0, -60.0))
        self.assertIn("the fixture", L.camera_relation(os.path.join(self.tmp, "broken"), run, (root, root))[0])

    def test_no_baseline_no_pass(self):
        self.assertIsNotNone(L.camera_relation("fixture", "run", None)[0])

    def test_fixture_baseline_gone_is_said(self):
        root = self.tree("root", record=CAMERA)
        first = self.tree("first", record=camera_record(CAMERA, 0.1, 100.0))
        run = self.tree("run", record=camera_record(CAMERA, 0.2, 200.0))
        self.tree("fixture", "userdata", record=CAMERA)
        fixture = os.path.join(self.tmp, "fixture")
        with open(os.path.join(fixture, "fixture.json"), "w") as f:
            json.dump({"source": os.path.join(self.tmp, "gone")}, f)
        refused, note = L.camera_relation(fixture, run, (root, first))
        self.assertIsNone(refused)
        self.assertIn("only this run's side", note)


RESAVE = os.path.join(L.ROOT, "local", "fixtures", "lot-resave", "all")
RUN, REFS = "9", ("5", "7")                    # run 9: 0x186/0xfe/0xf5; refs: another order and the 0xf4 variant
LOT10 = L.LOT10
FAMI8 = (0x46414D49, 0xFFFFFFFF, 8, 0)


@unittest.skipUnless(all(os.path.isdir(os.path.join(RESAVE, n)) for n in (RUN,) + REFS), "lot-resave fixtures not available")
class Normalised(unittest.TestCase):
    """A reference re-save (fixture 9) checked as a run against two other references only."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.snap = os.path.join(cls.tmp, "run-saved")
        src = os.path.join(RESAVE, RUN)
        shutil.copytree(os.path.join(src, "userdata"), cls.snap)
        shutil.copy(os.path.join(src, "ts2host.log"), cls.snap + ".ts2host.log")
        meta = L.fixture_meta(src)
        b = meta["binding"]
        base = os.path.join(L.ROOT, "local", "baselines", "lot10-funds")    # the bound digest (source manifest gone)
        with open(cls.snap + ".run.json", "w") as f:
            json.dump({"scenario": "@lot-resave", "seed": hex(b["seed"]), "baseline": base,
                       "scenario_sha256": b["scenario_sha256"], "baseline_sha256": b["baseline_sha256"]}, f)
        cls.backup = {}
        for rel in (LOT10, L.NEIGHBORHOOD):
            with open(os.path.join(cls.snap, rel), "rb") as f:
                cls.backup[rel] = f.read()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.real = L.all_fixtures
        L.all_fixtures = lambda: [("all", os.path.join(RESAVE, n)) for n in REFS]

    def tearDown(self):
        L.all_fixtures = self.real
        for rel, data in self.backup.items():
            with open(os.path.join(self.snap, rel), "wb") as f:
                f.write(data)

    def outcome(self, normalise=True):
        L._package.cache_clear()
        L.NORMALISE = normalise
        try:
            return L.check(self.snap, quiet=True)[0]
        finally:
            L.NORMALISE = True

    def lot(self, change):
        rewrite(os.path.join(self.snap, LOT10), change)

    def objm(self, it):
        return __import__("savediff").objm_layout(it[OBJM])

    # -- passes

    def test_reference_matches_after_normalising(self):
        self.assertNotIn(self.outcome(normalise=False), (L.MATCH, L.MATCH_OPEN))   # as bytes: ids, countdowns
        self.assertEqual(self.outcome(), L.MATCH_OPEN)                # open: the pass counter and EC44BDDC

    # -- must still fail

    def test_changed_funds(self):
        import savediff

        def change(it):
            key = next(k for k in it if k[0] == FAMI8[0] and k[2] == FAMI8[2])
            fami = savediff.parse_fami(it[key])
            self.assertEqual(fami["funds"], 12345)
            at = 4 * (7 + (0 if fami["version"] == 0x55 else -1))
            it[key] = patched(it[key], at, struct.pack("<I", 12346))
            self.assertEqual(savediff.parse_fami(it[key])["funds"], 12346)
        rewrite(os.path.join(self.snap, L.NEIGHBORHOOD), change)
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_changed_field_of_a_fresh_object(self):
        key = (0x584F424A, 0xFFFFFFFF, 0xFE, 0)       # a fresh object's XOBJ: byte 0x50 (a float of its state)
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x50, bytes([it[key][0x50] ^ 0x40]))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_changed_field_of_a_baseline_object(self):
        key = (0x584F424A, 0xFFFFFFFF, 0x10C, 0)
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x50, bytes([it[key][0x50] ^ 0x40]))))
        self.assertEqual(self.outcome(), L.UNEXPECTED)

    def test_broken_id_relation(self):
        # The fresh object 0xfe's XOBJ names itself at 0xb6; pointing it at the other fresh object 0xf5 keeps
        # every value a valid id but breaks the relation to the object list.
        key = (0x584F424A, 0xFFFFFFFF, 0xFE, 0)
        self.assertEqual(struct.unpack_from("<H", self.objm_bytes(key), 0xB6)[0], 0xFE)
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0xB6, struct.pack("<H", 0xF5))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_countdown_one_pass_off(self):
        def change(it):
            lay = self.objm(it)
            at = lay["schedule_at"] + 4 * (0x10C - 1)           # a plain countdown (flags 3)
            word = struct.unpack_from("<I", it[OBJM], at)[0]
            it[OBJM] = patched(it[OBJM], at, struct.pack("<I", word - 1))
        self.lot(change)
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_countdown_flags_changed(self):
        def change(it):
            at = self.objm(it)["schedule_at"] + 4 * (0x10C - 1)
            it[OBJM] = patched(it[OBJM], at + 2, struct.pack("<H", 0x0B))
        self.lot(change)
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_non_counting_entry_moved(self):
        def change(it):
            lay = self.objm(it)
            i = next(i for i, w in enumerate(lay["schedule"], 1) if w >> 16 == 1 and w & 0xFFFF == 0xFFFF)
            at = lay["schedule_at"] + 4 * (i - 1)
            it[OBJM] = patched(it[OBJM], at, struct.pack("<H", 5))
        self.lot(change)
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_stale_record_changed(self):
        key = (0x584F424A, 0xFFFFFFFF, 0x187, 0)       # not live: kept from the baseline
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x50, bytes([it[key][0x50] ^ 0x40]))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_pass_counter_wrong_value(self):
        key = (0x584F424A, 0xFFFFFFFF, 0x173, 0)
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x1BA, struct.pack("<H", (it[key][0x1BA] + 1) % 30))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def objm_bytes(self, key):
        return dbpf.Package(os.path.join(self.snap, LOT10)).get(key)


RELOAD = os.path.join(L.ROOT, "local", "fixtures", "lot-resave", "reload-1")
RL_RUN, RL_REFS = "1", ("3",)       # two reference chains with the same 0x15d deadline


def _reload_source_ok():
    try:
        meta = L.fixture_meta(os.path.join(RELOAD, RL_RUN))
        return all(os.path.isdir(os.path.join(RELOAD, n, "previous")) for n in (RL_RUN,) + RL_REFS) and \
            os.path.isfile(meta["source"] + ".run.json")
    except Exception:
        return False


@unittest.skipUnless(_reload_source_ok(), "reload-1 fixtures or their source manifests not available")
class Reload(unittest.TestCase):
    """A reference reload (chain 1: its own first save, then the reload) checked as a run against another
    reference chain (3). The two first saves hold the per-load objects at different ids."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.snap = os.path.join(cls.tmp, "run-saved")
        src = os.path.join(RELOAD, RL_RUN)
        shutil.copytree(os.path.join(src, "userdata"), cls.snap)
        shutil.copy(os.path.join(src, "ts2host.log"), cls.snap + ".ts2host.log")
        meta = L.fixture_meta(src)
        b = meta["binding"]
        with open(meta["source"] + ".run.json", encoding="utf-8-sig") as f:
            base = json.load(f)["baseline"]            # chain 1's first save (has its own manifest)
        with open(cls.snap + ".run.json", "w") as f:
            json.dump({"scenario": "@lot-resave", "seed": hex(b["seed"]), "baseline": base,
                       "scenario_sha256": b["scenario_sha256"], "baseline_sha256": b["immediate_sha256"]}, f)
        with open(os.path.join(cls.snap, LOT10), "rb") as f:
            cls.backup = f.read()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        self.real = L.all_fixtures
        L.all_fixtures = lambda: [("reload-1", os.path.join(RELOAD, n)) for n in RL_REFS]

    def tearDown(self):
        L.all_fixtures = self.real
        with open(os.path.join(self.snap, LOT10), "wb") as f:
            f.write(self.backup)

    def outcome(self):
        L._package.cache_clear()
        return L.check(self.snap, quiet=True)[0]

    def per_load(self, typ):
        lay = __import__("savediff").objm_layout(dbpf.Package(os.path.join(self.snap, LOT10)).get(OBJM))
        return next(i for i, t in lay["objects"] if t == typ)

    def test_state_and_pass(self):
        self.assertEqual(L.state_of(self.snap), "reload-1")
        self.assertEqual(self.outcome(), L.MATCH_OPEN)

    def test_predecessor_id_pointed_elsewhere(self):
        # The type-0x51 object's XOBJ keeps its predecessor's id (the first save's 0x51 object) at 0x27e. Pointing
        # it at the first save's 0x112 object keeps a valid per-load id but breaks the relation.
        key = (0x584F424A, 0xFFFFFFFF, self.per_load(0x51), 0)
        prev = self.first_save()
        play = __import__("savediff").objm_layout(prev.get(OBJM))
        ids = {t: i for i, t in play["objects"]}
        rec = dbpf.Package(os.path.join(self.snap, LOT10)).get(key)
        self.assertEqual(struct.unpack_from("<H", rec, 0x27E)[0], ids[0x51])
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x27E, struct.pack("<H", ids[0x112]))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def test_stale_record_of_the_first_save_changed(self):
        # The first save's per-load objects are deleted on reload; their records stay as that save wrote them.
        prev = self.first_save()
        play = __import__("savediff").objm_layout(prev.get(OBJM))
        live = {i for i, _ in __import__("savediff").objm_layout(
            dbpf.Package(os.path.join(self.snap, LOT10)).get(OBJM))["objects"]}
        i = next(i for i, t in play["objects"] if t in (0x51, 0x112, 0x118) and i not in live)
        key = (0x584F424A, 0xFFFFFFFF, i, 0)
        self.lot(lambda it: it.__setitem__(key, patched(it[key], 0x50, bytes([it[key][0x50] ^ 0x40]))))
        self.assertNotIn(self.outcome(), (L.MATCH, L.MATCH_OPEN))

    def lot(self, change):
        rewrite(os.path.join(self.snap, LOT10), change)

    def first_save(self):
        with open(self.snap + ".run.json") as f:
            return dbpf.Package(os.path.join(json.load(f)["baseline"], LOT10))


if __name__ == "__main__":
    unittest.main(verbosity=2)
