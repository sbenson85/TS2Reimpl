#!/usr/bin/env python3
"""Tests for tools/subsystems.py on synthetic data: the db/status evidence basis (a status entry's class names its
subsystem; the group file's class when the name gives none; shared_vf bodies skipped), the name-rule fallback
(status-rule), the status-group rules (2/3 byte majority, the coordinator's list, the --check-groups merge check), the
STL single-user rule, the class index, and map_rows (guesses filed under other, except third-party which is an
exclusion and stays).

Run: tools/py -I tools/tests/test_subsystems.py
"""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import subsystems as SS  # noqa: E402


class NameClass(unittest.TestCase):
    def test_qualifier(self):
        self.assertEqual(SS.name_class("nGZSceneDBAnim::cChannelUber::QueryInterface"), "nGZSceneDBAnim::cChannelUber")
        self.assertEqual(SS.name_class("cFoo::vf3 (deleting destructor)"), "cFoo")
        self.assertEqual(SS.name_class("cFoo::Release thunk (shared_vf_1)"), "cFoo")
        self.assertEqual(SS.name_class("std::vector<a::b, c::d>::insert"), "std::vector<a::b, c::d>")

    def test_no_qualifier(self):
        self.assertIsNone(SS.name_class("FindFirst (FindFirstFileA)"))
        self.assertIsNone(SS.name_class(""))
        self.assertIsNone(SS.name_class("::Global"))


class ClassIndex(unittest.TestCase):
    def test_classes_and_votes(self):
        full, outer = SS.class_index(
            {"nA::cBase": "framework-gz", "nB::cSame": "cas", "nC::cSame": "objects"},
            [("cVoted::F", "world-lot"), ("cVoted::G", "world-lot"), ("cVoted::H", "cas"),  # 2/3: placed
             ("cSplit::F", "world-lot"), ("cSplit::G", "cas"),                              # 1/2: not placed
             ("nA::cBase::F", "objects"), ("nA::cBase::G", "objects")])                     # db/classes wins
        self.assertEqual(full["cVoted"], "world-lot")
        self.assertNotIn("cSplit", full)
        self.assertEqual(full["nA::cBase"], "framework-gz")
        self.assertEqual(outer["cBase"], "framework-gz")
        self.assertNotIn("cSame", outer)  # two classes of that outer name in different subsystems: ambiguous

    def test_lookup(self):
        index = SS.class_index({"nA::cBase": "framework-gz"}, [])
        self.assertEqual(SS.lookup_class("nA::cBase", index), ("framework-gz", "nA::cBase"))
        self.assertEqual(SS.lookup_class("cBase", index), ("framework-gz", "cBase"))
        self.assertEqual(SS.lookup_class("nX::cBase", index), ("framework-gz", "cBase"))
        self.assertEqual(SS.lookup_class("cOther", index), (None, None))


class StatusSeeds(unittest.TestCase):
    INDEX = SS.class_index({"nA::cBase": "framework-gz", "cGroupClass": "scenegraph"}, [])

    def seeds(self, status, names=None, shared_vf=()):
        return {r: (s, b) for r, s, b in SS.status_seeds(status, names or {}, self.INDEX, set(shared_vf))}

    def test_by_name_class(self):
        got = self.seeds({"0x10": {"name": "nA::cBase::Run", "status": "verified", "_file": "Whatever.jsonl"}})
        self.assertEqual(got, {"0x10": ("framework-gz", "status:nA::cBase")})

    def test_mapped_counts(self):
        got = self.seeds({"0x10": {"name": "cBase::Run", "status": "mapped", "_file": "X.jsonl"}})
        self.assertEqual(got["0x10"][0], "framework-gz")

    def test_group_when_name_has_no_class(self):
        got = self.seeds({"0x20": {"name": "Helper (cdecl)", "status": "verified", "_file": "cGroupClass.jsonl"},
                          "0x21": {"name": "Helper2", "status": "verified", "_file": "NotAClass.jsonl"}})
        self.assertEqual(got, {"0x20": ("scenegraph", "status-group:cGroupClass")})

    def test_stl_qualifier_not_filed_by_group(self):
        got = self.seeds({"0x70": {"name": "std::vector<cRZString>::_Assign", "status": "verified",
                                   "_file": "cGroupClass.jsonl"},
                          "0x71": {"name": "vector<cRZString>::_Assign_counted_range", "status": "verified",
                                   "_file": "cGroupClass.jsonl"},
                          "0x72": {"name": "std::_Tree<x>::_Insert", "status": "verified", "_file": "cGroupClass.jsonl"},
                          "0x73": {"name": "unordered_map<cRZString,cRZString>::operator[] body", "status": "verified",
                                   "_file": "cGroupClass.jsonl"},
                          "0x74": {"name": "nGZSceneGraph::cShapeRefNodeImpl::Hit", "status": "verified",
                                   "_file": "cGroupClass.jsonl"},
                          "0x75": {"name": "nTS::cMapLoader::Load", "status": "verified", "_file": "cGroupClass.jsonl"},
                          "0x76": {"name": "ContentKeyMap find (std::unordered_map<K, V> instantiation)",
                                   "status": "verified", "_file": "cGroupClass.jsonl"},
                          "0x77": {"name": "AttractorListReader (cdecl: stream, vector)", "status": "verified",
                                   "_file": "cGroupClass.jsonl"}})
        self.assertEqual(set(got), {"0x74", "0x75", "0x77"})  # unknown EA qualifiers and bare names use the group
        self.assertTrue(SS.is_stl("std::list<cRZAutoRefCount<cIGZFoo>>"))
        self.assertTrue(SS.is_stl("_List_val<int>"))
        self.assertFalse(SS.is_stl("nTSSG::cNHoodOccupantManager"))
        self.assertFalse(SS.is_stl("cVectorField"))

    def test_known_class_beats_group(self):
        got = self.seeds({"0x30": {"name": "cBase::Run", "status": "verified", "_file": "cGroupClass.jsonl"}})
        self.assertEqual(got["0x30"][0], "framework-gz")

    def test_name_from_names_when_entry_has_none(self):
        got = self.seeds({"0x40": {"status": "verified", "_file": "X.jsonl"}}, names={"0x40": {"name": "cBase::F"}})
        self.assertEqual(got["0x40"][0], "framework-gz")

    def test_shared_vf_skipped(self):
        got = self.seeds({"0x50": {"name": "cBase::Release (shared_vf_50)", "status": "verified", "_file": "X.jsonl"},
                          "0x51": {"name": "cBase::Nop", "status": "verified", "_file": "X.jsonl"}},
                         shared_vf={"0x51"})
        self.assertEqual(got, {})

    def test_unknown_status_level_skipped(self):
        self.assertEqual(self.seeds({"0x60": {"name": "cBase::F", "status": "guessed", "_file": "X.jsonl"}}), {})


class StatusRule(unittest.TestCase):
    INDEX = SS.class_index({"nA::cBase": "framework-gz"}, [])

    def seeds(self, status):
        return {r: (s, b) for r, s, b in SS.status_seeds(status, {}, self.INDEX, set())}

    def test_rule_subsystem(self):
        self.assertEqual(SS.rule_subsystem("nGZSceneGraph::cViewerRefNodeBaseImpl"), "scenegraph")
        self.assertIsNone(SS.rule_subsystem("nTS::cWhatever"))  # other-ts: no campaign
        self.assertIsNone(SS.rule_subsystem("Zzqx"))

    def test_class_by_name_rules(self):
        got = self.seeds({"0x10": {"name": "nGZSceneGraph::cViewerImpl::Hit", "status": "verified",
                                   "_file": "Zzqx.jsonl"}})
        self.assertEqual(got, {"0x10": ("scenegraph", "status-rule:nGZSceneGraph::cViewerImpl")})

    def test_group_by_name_rules(self):
        got = self.seeds({"0x11": {"name": "Helper", "status": "verified", "_file": "nGZSceneGraphBits.jsonl"},
                          "0x12": {"name": "nTS::cZzqx::F", "status": "verified", "_file": "nGZSceneGraphBits.jsonl"}})
        self.assertEqual(got["0x11"], ("scenegraph", "status-rule:nGZSceneGraphBits"))
        self.assertEqual(got["0x12"][0], "scenegraph")  # other-ts class rule says nothing: falls through to the group

    def test_stl_names(self):
        self.assertTrue(SS.stl_named("std::vector<int>::_Grow"))
        self.assertTrue(SS.stl_named("ActionRecordsReallocate (std::vector<ActionRecord>::_Reallocate)"))
        self.assertFalse(SS.stl_named("nGZCommandParser::EvalFunctionTable::EvalFunctionTable (fills a std::map)"))
        got = self.seeds({"0x13": {"name": "nGZCommandParser::cZzqx::cZzqx (fills a std::map)", "status": "verified",
                                   "_file": "Zzqx.jsonl"},
                          "0x14": {"name": "std::vector<nGZSceneGraph::cX>::_Grow", "status": "verified",
                                   "_file": "nGZSceneGraphBits.jsonl"}})
        self.assertEqual(set(got), {"0x13"})
        self.assertEqual(got["0x13"][0], "command-parser")


class GroupSeeds(unittest.TestCase):
    SIZES = {f"0x{i:x}": 100 for i in range(1, 20)}

    def run_groups(self, members, assign, listed=None):
        seeds, report = SS.group_seeds(members, assign, self.SIZES, listed or {})
        return {r: (s, b) for r, s, b in seeds}, report

    def test_majority_two_thirds(self):
        members = {"G": ["0x1", "0x2", "0x3", "0x4"], "H": ["0x5", "0x6", "0x7", "0x8"]}
        assign = {"0x1": ("cas", "vtable:cA"), "0x2": ("cas", "vtable:cA"), "0x3": ("objects", "vtable:cB"),
                  "0x5": ("cas", "vtable:cA"), "0x6": ("objects", "vtable:cB")}
        seeds, report = self.run_groups(members, assign)
        self.assertEqual(seeds, {"0x4": ("cas", "group-majority:G")})  # exactly 2/3: placed
        self.assertEqual(report["G"]["rule"], "majority")
        self.assertIsNone(report["H"]["majority"])  # 1/2: not placed
        self.assertNotIn("0x7", seeds)

    def test_majority_by_bytes_not_count(self):
        sizes = {"0x1": 400, "0x2": 100, "0x3": 100, "0x4": 50}
        seeds, _ = SS.group_seeds({"G": ["0x1", "0x2", "0x3", "0x4"]},
                                  {"0x1": ("cas", "v"), "0x2": ("objects", "v"), "0x3": ("objects", "v")}, sizes, {})
        self.assertEqual([(r, s) for r, s, _ in seeds], [("0x4", "cas")])  # 400/600 bytes, though 1 of 3 functions
        sizes["0x1"] = 350
        seeds, _ = SS.group_seeds({"G": ["0x1", "0x2", "0x3", "0x4"]},
                                  {"0x1": ("cas", "v"), "0x2": ("objects", "v"), "0x3": ("objects", "v")}, sizes, {})
        self.assertEqual(seeds, [])  # 350/550 bytes: under 2/3

    def test_no_campaign_majority(self):
        for sub in ("other-ts", "shared-utility", "third-party"):
            seeds, report = self.run_groups({"G": ["0x1", "0x2"]}, {"0x1": (sub, "vtable:cA")})
            self.assertEqual(seeds, {}, sub)
            self.assertIsNone(report["G"]["rule"])

    def test_no_placed_members(self):
        seeds, report = self.run_groups({"G": ["0x1", "0x2"]}, {})
        self.assertEqual(seeds, {})
        self.assertEqual(report["G"]["unplaced"], ["0x1", "0x2"])

    def test_list_wins(self):
        assign = {"0x1": ("cas", "vtable:cA"), "0x2": ("cas", "vtable:cA")}
        seeds, report = self.run_groups({"G": ["0x1", "0x2", "0x3"], "P": ["0x4"]}, assign,
                                        {"G": "objects", "P": "simantics-vm"})
        self.assertEqual(seeds, {"0x3": ("objects", "group-list:G"), "0x4": ("simantics-vm", "group-list:P")})
        self.assertEqual(assign["0x1"][0], "cas")  # evidence-placed members keep theirs
        self.assertEqual(report["G"]["rule"], "list")

    def test_mixed(self):
        seeds, report = self.run_groups({"G": ["0x1", "0x2"]}, {"0x1": ("cas", "v")}, {"G": SS.MIXED})
        self.assertEqual(seeds, {"0x2": ("cas", "group-majority:G")})  # the majority still applies
        seeds, report = self.run_groups({"G": ["0x1", "0x2", "0x3"]}, {"0x1": ("cas", "v"), "0x2": ("objects", "v")},
                                        {"G": SS.MIXED})
        self.assertEqual(seeds, {})
        self.assertEqual(report["G"]["rule"], "mixed")

    def test_members(self):
        status = {"0x1": {"name": "Helper", "status": "verified", "_file": "G.jsonl"},
                  "0x2": {"name": "std::vector<int>::_Grow", "status": "verified", "_file": "G.jsonl"},
                  "0x3": {"name": "cA::F (shared_vf_3)", "status": "verified", "_file": "G.jsonl"},
                  "0x4": {"name": "cA::G", "status": "verified", "_file": "G.jsonl"},
                  "0x5": {"name": "cA::H", "status": "guessed", "_file": "G.jsonl"}}
        self.assertEqual(dict(SS.group_members(status, {}, {"0x4"})), {"G": ["0x1"]})


class GroupList(unittest.TestCase):
    def load(self, lines):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "db"))
            with open(os.path.join(d, "db", SS.GROUP_LIST), "w", encoding="utf-8") as f:
                f.write("\n".join(json.dumps(e) for e in lines) + "\n")
            old = SS.tsdb.ROOT
            SS.tsdb.ROOT = Path(d)
            try:
                return SS.load_group_list()
            finally:
                SS.tsdb.ROOT = old

    def test_valid(self):
        self.assertEqual(self.load([{"group": "G", "subsystem": "cas", "reason": "r"},
                                    {"group": "H", "subsystem": "mixed", "reason": "r"}]), {"G": "cas", "H": "mixed"})

    def test_held_and_forced(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "db").mkdir()
            (root / "db" / SS.GROUP_LIST).write_text(
                json.dumps({"group": "G", "subsystem": "mixed", "held": True}) + "\n"
                + json.dumps({"group": "H", "subsystem": "cas", "force": True}) + "\n", encoding="utf-8")
            jsonl = lambda name: [json.loads(x) for x in (root / "db" / name).read_text(encoding="utf-8").splitlines()]
            real = SS.tsdb.jsonl
            SS.tsdb.jsonl = jsonl
            try:
                self.assertEqual(SS.load_held_groups(), ["G"])
                self.assertEqual(SS.load_forced_groups(), {"H": "cas"})
            finally:
                SS.tsdb.jsonl = real

    def test_rejects_non_campaign(self):
        for sub in ("third-party", "other-ts", "shared-utility", "other", "nonsense"):
            with self.assertRaises(SystemExit, msg=sub):
                self.load([{"group": "G", "subsystem": sub, "reason": "r"}])


class Bands(unittest.TestCase):
    def load(self, lines):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "db"))
            with open(os.path.join(d, "db", SS.BAND_LIST), "w", encoding="utf-8") as f:
                f.write("\n".join(json.dumps(e) for e in lines) + "\n")
            old = SS.tsdb.ROOT
            SS.tsdb.ROOT = Path(d)
            try:
                return SS.load_bands()
            finally:
                SS.tsdb.ROOT = old

    def entry(self, lo, hi, sub="cas", name="n", reason="r"):
        return {"lo": lo, "hi": hi, "subsystem": sub, "name": name, "reason": reason}

    def test_valid_and_sorted(self):
        self.assertEqual(self.load([self.entry("0x30", "0x40", "objects", "b"), self.entry("0x10", "0x30", "cas", "a")]),
                         [(0x10, 0x30, "cas", "a"), (0x30, 0x40, "objects", "b")])

    def test_rejects(self):
        for bad in (self.entry("0x10", "0x20", "third-party"), self.entry("0x10", "0x20", "shared-utility"),
                    self.entry("0x20", "0x20"), self.entry("0x10", "0x20", reason=""), self.entry("0x10", "0x20", name="")):
            with self.assertRaises(SystemExit, msg=str(bad)):
                self.load([bad])

    def test_rejects_overlap(self):
        with self.assertRaises(SystemExit):
            self.load([self.entry("0x10", "0x21"), self.entry("0x20", "0x30")])

    def test_seeds_half_open(self):
        bands = [(0x10, 0x20, "cas", "a"), (0x30, 0x40, "objects", "b")]
        self.assertEqual(SS.band_seeds(["0xf", "0x10", "0x1f", "0x20", "0x30", "0x40"], bands),
                         [("0x10", "cas", "band:a"), ("0x1f", "cas", "band:a"), ("0x30", "objects", "band:b")])


class StlSingleUser(unittest.TestCase):
    def test_rules(self):
        assign = {"0xa": ("cas", "vtable:cA"), "0xb": ("cas", "vtable:cB"), "0xc": ("objects", "vtable:cC"),
                  "0xd": ("cas", "called-only-from"), "0xe": ("third-party", "third-party:x")}
        callers = {"0x1": ["0xa", "0xb"],   # one subsystem: placed
                   "0x2": ["0xa", "0xc"],   # two: not
                   "0x3": ["0xa", "0xd"],   # a caller placed only by a guess: not
                   "0x4": [],               # no callers: not
                   "0x5": ["0x1", "0x5"],   # called from 0x1 (placed above) and itself: follows it
                   "0x6": ["0xa", "0x9"],   # a caller nothing placed: not
                   "0x7": ["0xe"]}          # third-party only: not (an exclusion)
        out = SS.stl_single_user_seeds({"0x1", "0x2", "0x3", "0x4", "0x5", "0x6", "0x7"}, assign, {"0xd"}, callers)
        self.assertEqual(sorted(out), [("0x1", "cas"), ("0x5", "cas")])
        self.assertEqual(assign["0x5"], ("cas", "stl-single-user"))
        self.assertNotIn("0x2", assign)


class GroupCheck(unittest.TestCase):
    def test_unresolved_and_notes(self):
        members = {"G": ["0x1", "0x2"], "L": ["0x3"], "M": ["0x4", "0x5"], "R": ["0x6", "0x7"], "D": ["0x8", "0x9"],
                   "N": ["0xa"]}
        report = {"G": {"placed": {"cas": 1}, "unplaced": ["0x2"], "majority": None, "listed": None, "rule": None},
                  "L": {"placed": {}, "unplaced": ["0x3"], "majority": None, "listed": "cas", "rule": "list"},
                  "M": {"placed": {"cas": 1}, "unplaced": ["0x5"], "majority": "cas", "listed": "mixed",
                        "rule": "majority"},
                  "R": {"placed": {"cas": 1}, "unplaced": ["0x7"], "majority": "cas", "listed": "cas", "rule": "list"},
                  "D": {"placed": {"cas": 1}, "unplaced": ["0x9"], "majority": "cas", "listed": "objects",
                        "rule": "list"},
                  "N": {"placed": {"cas": 1}, "unplaced": [], "majority": None, "listed": "objects", "rule": "list"}}
        rows = {"0x2": {"subsystem": "other", "guess": "objects"}, "0x1": {"subsystem": "cas"}}
        listed = {"L": "cas", "M": "mixed", "R": "cas", "D": "objects", "N": "objects", "Gone": "cas"}
        unresolved, notes = SS.group_check(members, report, rows, {"0x2": 64}, listed)
        self.assertEqual([(u[0], u[1], dict(u[3])) for u in unresolved], [("G", ["0x2"], {"objects": 64})])
        text = "\n".join(notes)
        self.assertIn("Gone has no status members", text)
        self.assertIn("R -> cas is now the members' majority", text)
        self.assertIn("M is listed mixed", text)
        self.assertIn("D -> objects but its placed members' majority is cas", text)
        self.assertIn("N has no unplaced members", text)
        self.assertNotIn("L", [n.split(": ")[1].split(" ")[0] for n in notes])
        self.assertEqual(len(notes), 5)

    def test_quiet(self):
        report = {"G": {"placed": {}, "unplaced": [], "majority": None, "listed": None, "rule": None}}
        self.assertEqual(SS.group_check({"G": ["0x1"]}, report, {"0x1": {"subsystem": "cas"}}, {}, {}), ([], []))


class EvidenceHelpers(unittest.TestCase):
    def test_forced_group_needs_no_majority_note(self):
        report = {"D": {"placed": {"cas": 2}, "unplaced": [], "majority": "cas", "listed": "objects", "rule": "list"}}
        self.assertEqual(SS.group_check({}, report, {}, {}, {"D": "objects"}, {"D": "objects"}), ([], []))
        self.assertEqual(len(SS.group_check({}, report, {}, {}, {"D": "objects"})[1]), 1)

    def test_shared_vtable(self):
        classes = [{"name": "A", "vftables": [{"entries": ["0x1", "0x2", "0x3", "0x4"]}]},
                   {"name": "B", "vftables": [{"entries": ["0x1", "0x2"]}]},
                   {"name": "C", "vftables": [{"entries": ["0x2", "0x5", "0x6"]}]}]
        cls_sub = {"A": "cas", "B": "cas", "C": "objects"}
        out = SS.shared_vtable_seeds(classes, cls_sub, {"0x1", "0x2", "0x4", "0x5", "0x6"}, {"0x4": ("cas", "vtable:A")})
        self.assertEqual([(r, s) for r, s, _ in out], [("0x1", "cas"), ("0x5", "objects"), ("0x6", "objects")])
        self.assertEqual(SS.shared_vtable_seeds(classes, {"A": "other-ts", "B": "other-ts"}, {"0x1"}, {}), [])
        self.assertEqual(SS.shared_vtable_seeds(classes, {"A": "cas"}, {"0x1"}, {}), [])  # B has no subsystem

    def test_contained(self):
        F = {"0x10": {"size": 0x20}, "0x18": {"size": 4}, "0x28": {"size": 8}, "0x2c": {"size": 8}, "0x40": {"size": 4}}
        self.assertEqual(SS.contained(F), {"0x18": "0x10", "0x28": "0x10"})  # 0x2c overruns 0x10's end, so it stands alone

    def test_pointer_tables(self):
        relocs = [(0x100, 1), (0x104, 2), (0x108, 3), (0x110, 4), (0x200, 5), (0x204, 6), (0x208, 7), (0x20c, 8)]
        data = lambda p: p < 0x204 or p >= 0x208
        self.assertEqual(SS.pointer_tables(relocs, data), [(0x100, 0x10c, [1, 2, 3]), (0x208, 0x210, [7, 8])])
        self.assertEqual(SS.pointer_tables(relocs, data, minimum=3), [(0x100, 0x10c, [1, 2, 3])])
        self.assertEqual(SS.pointer_tables(relocs, data, skip={0x104}), [(0x104, 0x10c, [2, 3]), (0x208, 0x210, [7, 8])])

    def test_table_users_through_nesting(self):
        tables = [(0x100, 0x108, [0x200, 0x204]), (0x200, 0x208, [0x300, 1]), (0x300, 0x308, [1, 2])]
        refs = {0x100: {"top"}, 0x304: {"low"}}
        users = SS.table_users(tables, lambda p: refs.get(p, set()))
        self.assertEqual(users, [{"top"}, {"top"}, {"top", "low"}])


class Inferred(unittest.TestCase):
    A = {"0x100": ("cas", "vtable:c"), "0x180": ("cas", "vtable:c"), "0x200": ("objects", "vtable:d"),
         "0x300": ("objects", "vtable:d"), "0x400": ("third-party", "named"), "0x500": ("third-party", "named")}

    def run_on(self, guesses, order, max_gap=SS.INFER_MAX_GAP):
        assign = dict(self.A)
        assign.update({r: (g, "called-only-from") for r, g in guesses.items() if g})
        return {r: (s, k) for r, s, k in SS.inferred(sorted(set(order) | set(self.A), key=lambda r: int(r, 16)), assign,
                                                     {r for r, g in guesses.items() if g}, max_gap)}

    def test_strata(self):
        got = self.run_on({"0x110": "cas", "0x120": None, "0x130": "shared-utility", "0x140": "ui-game", "0x1c0": "objects",
                           "0x1d0": "cas", "0x1e0": "ui-game"},
                          ["0x110", "0x120", "0x130", "0x140", "0x1c0", "0x1d0", "0x1e0"])
        self.assertEqual(got, {"0x110": ("cas", "both=guess"), "0x120": ("cas", "both"), "0x130": ("cas", "both"),
                               "0x140": ("cas", "both>guess"), "0x1c0": ("objects", "side=guess"),
                               "0x1d0": ("cas", "side=guess")})  # 0x1e0: a guess neither flank supports stays a guess

    def test_never_into_third_party(self):
        got = self.run_on({"0x410": None, "0x420": "cas", "0x430": "third-party", "0x3f0": "third-party"},
                          ["0x410", "0x420", "0x430", "0x3f0"])
        self.assertEqual(got, {})

    def test_outside_the_evidence_or_gap(self):
        self.assertEqual(self.run_on({"0x10": "cas", "0x900": "third-party"}, ["0x10", "0x900"]), {})
        self.assertEqual(self.run_on({"0x210": "objects"}, ["0x210"], 0x100), {})
        self.assertEqual(self.run_on({"0x210": "objects"}, ["0x210"], 0x101), {"0x210": ("objects", "both=guess")})

    def test_neutral_code_is_no_flank(self):
        order = ["0x1", "0x2", "0x3", "0x4"]
        assign = {"0x1": ("cas", "vtable:c"), "0x2": ("crt-glue", "vtable:e"), "0x4": ("cas", "vtable:c")}
        self.assertEqual(SS.flanks(order, assign, set()), {"0x3": ("cas", "cas", 3)})


class SettleByAddress(unittest.TestCase):
    def test_promotes_only_what_is_listed(self):
        order = ["0x1", "0x2", "0x3", "0x4", "0x5", "0x6", "0x7"]
        assign = {"0x1": ("cas", "vtable:c"), "0x2": ("cas", "called-only-from"), "0x4": ("objects", "calls-into"),
                  "0x5": ("shared-utility", "shared-callers"), "0x7": ("cas", "vtable:c")}
        weak = {"0x2", "0x4", "0x5"}
        SS.settle_by_address(order, assign, weak)
        self.assertEqual(assign["0x2"], ("cas", "inferred:called-only-from"))
        self.assertNotIn("0x2", weak)
        self.assertEqual(assign["0x3"], ("cas", "flanks"))
        self.assertEqual(assign["0x4"], ("objects", "calls-into"))
        self.assertEqual(assign["0x5"], ("cas", "flanks"))
        self.assertEqual(weak, {"0x3", "0x4", "0x5", "0x6"})
        rows = {r["rva"]: r for r in SS.map_rows({r: {} for r in order}, set(), assign, weak)}
        self.assertEqual(rows["0x2"]["subsystem"], "cas")
        self.assertTrue(SS.is_inferred(rows["0x2"]))
        self.assertEqual((rows["0x3"]["subsystem"], rows["0x3"]["guess"]), ("other", "cas"))


class MapRows(unittest.TestCase):
    def test_guesses(self):
        F = {"0x1": {}, "0x2": {}, "0x3": {}, "0x4": {}, "0x5": {}}
        assign = {"0x1": ("scenegraph", "vtable:cA"), "0x2": ("scenegraph", "called-only-from"),
                  "0x3": ("third-party", "data-ref"), "0x4": ("cas", "status:cB")}
        rows = {r["rva"]: r for r in SS.map_rows(F, set(), assign, {"0x2", "0x3"})}
        self.assertEqual(rows["0x1"], {"rva": "0x1", "subsystem": "scenegraph", "basis": "vtable:cA"})
        self.assertEqual(rows["0x2"], {"rva": "0x2", "subsystem": "other", "basis": "called-only-from",
                                       "guess": "scenegraph"})
        self.assertEqual(rows["0x3"], {"rva": "0x3", "subsystem": "third-party", "basis": "data-ref",
                                       "guess": "third-party"})
        self.assertEqual(rows["0x4"]["subsystem"], "cas")
        self.assertEqual(rows["0x5"], {"rva": "0x5", "subsystem": "other", "basis": "unassigned"})


if __name__ == "__main__":
    unittest.main()
