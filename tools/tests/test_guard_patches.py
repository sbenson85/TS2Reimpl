#!/usr/bin/env python3
"""Tests for tools/guard_patches.py (tests that patch, or plant the statics of, a function the branch makes ours, on
the game's side only), on small synthetic test trees.

Run: .venv/Scripts/python -I tools/tests/test_guard_patches.py
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import guard_patches as G  # noqa: E402

REG = 0x253de0     # a hooked registry
HOLDER = 0x2455e0  # its holder, in effect through it
MAP, ONCE = 0x10f60e8, 0x10f60f8  # their statics
TARGETS = {
    REG: {"why": "hooked", "tests": {"tests/sg_event_registries.cpp"}, "name": "Registry", "callers_only": False},
    HOLDER: {"why": "verified", "tests": {"tests/sg_event_registries.cpp"}, "name": "Holder", "callers_only": True},
}
CELLS = {MAP: {REG}, ONCE: {HOLDER}}
CALLEES = {REG: {HOLDER}}


def run(files, ours_cells=frozenset()):
    return sorted((p, line, rva, kind) for p, line, rva, kind, _ in G.scan(files, TARGETS, CELLS, ours_cells, CALLEES))


class Patches(unittest.TestCase):
    def test_code_patch_on_the_image_only(self):
        files = {"tests/a.cpp": "void f()\n{\n    shape::CodePatch registry{GameAddress<void>(0x253de0), shape::CodeOf(&Stand)};\n}\n"}
        self.assertEqual(run(files), [("tests/a.cpp", 3, REG, "patch")])

    def test_both_sides_stand_in_is_fine(self):
        files = {"tests/a.cpp": "shape::StandIn registry{0x253de0, &Traits::RegistryFor, &Stand};\n"
                                "patches.Both(0x253de0, &Traits::RegistryFor, &Stand);\n"}
        self.assertEqual(run(files), [])

    def test_constant_from_an_included_header(self):
        files = {"tests/fx.h": "inline constexpr uint32_t kRegistry = 0x253de0;\n",
                 "tests/a.cpp": '#include "fx.h"\nvoid f(Patches &p)\n{\n    p.Image(kRegistry, &Stand);\n}\n'}
        self.assertEqual(run(files), [("tests/a.cpp", 4, REG, "patch")])

    def test_a_local_name_shadows_another_files_constant(self):
        files = {"tests/fx.h": "constexpr uint32_t kLookup = 0x253de0;\n",
                 "tests/a.cpp": "constexpr uint32_t kLookup = 0x236660;\nRedirect registry(ctx.image.At(kLookup), &Fake);\n"}
        self.assertEqual(run(files), [])

    def test_a_brace_initialised_variable_is_not_a_constant(self):
        files = {"tests/a.cpp": "shape::StandIn stand{0x253de0, &Ours, &Stand};\n",
                 "tests/b.cpp": "void g(uint32_t stand) { shape::CodePatch p(stand, shape::CodeOf(&X)); }\n"}
        self.assertEqual(run(files), [])

    def test_calls_and_comments_are_not_patches(self):
        files = {"tests/a.cpp": "// shape::CodePatch over 0x253de0 once\n"
                                "auto r = ctx.Original<Fn>(0x253de0)(0, nullptr);\n"
                                "using Orig = OriginalFunction<Fn, 0x253de0>; auto s = shape::CodeOf(&Orig::Call);\n"}
        self.assertEqual(run(files), [])


class Plants(unittest.TestCase):
    def test_planting_the_statics_of_a_hooked_function(self):
        files = {"tests/b.cpp": "constexpr Statics kBuilder{0x10f60e8, 0x10f60f0, 0x10f60f8, 0x10f6100};\n"}
        self.assertEqual(run(files), [("tests/b.cpp", 1, HOLDER, "plant"), ("tests/b.cpp", 1, REG, "plant")])
        lines = G.report(G.scan(files, TARGETS, CELLS, frozenset(), CALLEES), TARGETS, names={})
        self.assertEqual(len(lines), 1)
        self.assertIn("0x253de0 Registry (hooked)", lines[0])
        self.assertNotIn("Holder", lines[0])  # its helper folded into the hooked caller
        self.assertIn(G.HINT, lines[0])

    def test_a_stand_in_in_an_included_header_covers_it_and_its_helpers(self):
        files = {"tests/fx.h": "struct Scope { std::unique_ptr<shape::StandIn> s = std::make_unique<shape::StandIn>("
                               "0x253de0, &Ours, &Stand); };\n",
                 "tests/b.cpp": '#include "fx.h"\nconstexpr Statics k{0x10f60e8, 0x10f60f8};\n'}
        self.assertEqual(run(files), [])

    def test_the_functions_own_test_plants_for_the_original(self):
        files = {"tests/sg_event_registries.cpp": "constexpr Statics k{0x10f60e8, 0x10f60f8};\n"}
        self.assertEqual(run(files), [])

    def test_a_helpers_static_alone_is_not_reported(self):
        files = {"tests/startup.cpp": "constexpr uint32_t kFirst = 0x10f60f8, kEnd = 0x10f9000;\n"}
        self.assertEqual(run(files), [])

    def test_state_our_source_shares(self):
        files = {"tests/b.cpp": "*GameAddress<uint32_t>(0x10f60e8) = 0;\n"}
        self.assertEqual(run(files, ours_cells={MAP}), [])
        self.assertEqual(G.ours_cell_names({"src/x.cpp": "auto *m = GameAddress<Map>(0x10f60e8);\n"}), {MAP})


class NewlyOurs(unittest.TestCase):
    def test_hooks_and_statuses(self):
        def entry(rva, status, **kw):
            return json.dumps({"rva": rva, "name": f"F{rva}", "status": status, "test": "tests/t.cpp", **kw})
        hook = '    {"Name", 0x111110, 32, 0x1, reinterpret_cast<void *>(&F)},\n'
        base = {"src/hook/tables/A.cpp": hook,
                "db/status/A.jsonl": "\n".join([entry("0x222220", "mapped"), entry("0x333330", "verified")])}
        branch = {"src/hook/tables/B.cpp": hook + '    {"New", 0x444440, 32, 0x2, nullptr},\n',  # 0x111110 moved
                  "db/status/A.jsonl": "\n".join([entry("0x222220", "verified"), entry("0x333330", "verified"),
                                                  entry("0x555550", "verified", in_effect="callers")])}
        out = G.newly_ours(base, branch)
        self.assertEqual(sorted(out), [0x222220, 0x444440, 0x555550])
        self.assertEqual(out[0x444440]["why"], "hooked")
        self.assertTrue(out[0x555550]["callers_only"])
        self.assertFalse(out[0x222220]["callers_only"])
        self.assertEqual(out[0x222220]["tests"], {"tests/t.cpp"})


if __name__ == "__main__":
    unittest.main()
