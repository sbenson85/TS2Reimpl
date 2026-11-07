#!/usr/bin/env python3
"""Tests for tools/testweaken.py (test-weakening candidates between a base and a branch version of a test file).

Run: .venv/Scripts/python -I tools/tests/test_testweaken.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import testweaken as W  # noqa: E402

BASE = """\
#include "difftest.h"
namespace {
constexpr int kSeeds = 100;
void CompareState(difftest::Context &ctx, const State &a, const State &b)
{
    if (a.x != b.x) ctx.Fail("x %d/%d", a.x, b.x);
    if (a.y != b.y) ctx.Fail("y %d/%d", a.y, b.y);
}
void ChildCases(difftest::Context &ctx, int cases)
{
    for (int i = 0; i < cases; ++i) Run(ctx, i);
}
} // namespace

DIFF_TEST(Foo_Get, 0x1000)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        State a = Orig(seed), b = Ours(seed);
        CompareState(ctx, a, b);
        if (a.r != b.r) ctx.Fail("ret");
    }
}

DIFF_TEST(Foo_Set, 0x1010)
{
    for (int s = 0; s < kSeeds; ++s) {
        State a = Orig(s), b = Ours(s);
        if (memcmp(&a, &b, sizeof a)) ctx.Fail("state");
    }
}

DIFF_TEST(Foo_Children, 0x1020) { ChildCases(ctx, 150); }
"""


def run(new, base=BASE, exists=None):
    return W.analyse("tests/foo.cpp", base, new, test_exists=exists)


class Weakening(unittest.TestCase):
    def test_identical_is_silent(self):
        self.assertEqual(run(BASE), [])

    def test_removed_test(self):
        new = BASE.replace("DIFF_TEST(Foo_Children, 0x1020) { ChildCases(ctx, 150); }\n", "")
        self.assertEqual(run(new), ["Foo_Children: removed (not registered anywhere in the branch's tests/, "
                                    "rva 0x1020 named nowhere there)"])
        self.assertEqual(run(new, exists=lambda n: n == "Foo_Children"), [])  # moved to another file
        self.assertEqual(W.analyse("tests/foo.cpp", BASE, new, rva_named=lambda r: r == 0x1020), [])

    def test_removed_test_folded_into_another(self):
        new = BASE.replace("DIFF_TEST(Foo_Children, 0x1020) { ChildCases(ctx, 150); }\n", "").replace(
            "DIFF_TEST(Foo_Set, 0x1010)", "// also covers Foo::Children (0x00001020)\nDIFF_TEST(Foo_Set, 0x1010)")
        self.assertEqual(run(new), [])

    def test_removed_test_with_a_constant_rva(self):
        base = BASE.replace("DIFF_TEST(Foo_Children, 0x1020)", "DIFF_TEST(Foo_Children, kChildrenRva)").replace(
            "constexpr int kSeeds = 100;", "constexpr int kSeeds = 100;\nconstexpr uint32_t kChildrenRva = 0x1020;")
        new = base.replace("DIFF_TEST(Foo_Children, kChildrenRva) { ChildCases(ctx, 150); }\n", "")
        self.assertEqual(run(new, base), [])  # the constant still names it
        new = new.replace("constexpr uint32_t kChildrenRva = 0x1020;\n", "")
        self.assertEqual(run(new, base), ["Foo_Children: removed (not registered anywhere in the branch's tests/, "
                                          "rva 0x1020 named nowhere there)"])

    def test_dropped_comparison(self):
        new = BASE.replace('        if (a.r != b.r) ctx.Fail("ret");\n', "")
        self.assertEqual(run(new), ["Foo_Get: 4 -> 3 comparison/failure calls (Fail, Compare*, Same*, Check*, Expect*, "
                                    "Dump*, memcmp; in the test and the helpers it calls)"])

    def test_dropped_comparison_in_a_helper(self):
        new = BASE.replace('    if (a.y != b.y) ctx.Fail("y %d/%d", a.y, b.y);\n', "")
        self.assertEqual([w.split(":")[0] for w in run(new)], ["Foo_Get"])

    def test_moving_checks_into_a_helper_is_neutral(self):
        new = BASE.replace('        if (a.r != b.r) ctx.Fail("ret");\n', "        CheckRet(ctx, a, b);\n").replace(
            "} // namespace", 'void CheckRet(difftest::Context &ctx, const State &a, const State &b)\n'
                              '{\n    if (a.r != b.r) ctx.Fail("ret");\n}\n} // namespace')
        self.assertEqual([w for w in run(new) if "comparison" in w], [])

    def test_loop_bound(self):
        new = BASE.replace("seed < 200", "seed < 20")
        self.assertEqual(run(new), ["Foo_Get: loop bound 200 -> 20: `for (uint32_t seed = 0; seed < 20)`"])
        self.assertEqual(run(BASE.replace("seed < 200", "seed < 2000")), [])  # strengthening

    def test_seed_constant(self):
        new = BASE.replace("kSeeds = 100", "kSeeds = 10")
        self.assertIn("Foo_Set: constant kSeeds 100 -> 10", run(new))

    def test_count_argument(self):
        new = BASE.replace("ChildCases(ctx, 150)", "ChildCases(ctx, 15)")
        self.assertEqual(run(new), ["Foo_Children: argument 150 -> 15: `DIFF_TEST(Foo_Children, 0x1020) { "
                                    "ChildCases(ctx, 15); }`"])
        self.assertEqual(run(BASE.replace("ChildCases(ctx, 150)", "ChildCases(ctx, 1500)")), [])

    def test_new_continue_and_mask(self):
        new = BASE.replace("        State a = Orig(seed), b = Ours(seed);\n",
                           "        State a = Orig(seed), b = Ours(seed);\n        if (seed == 7) continue;\n"
                           "        a.flags &= ~0x80u;\n")
        ws = run(new)
        self.assertTrue(any("Foo_Get: new `continue;`" in w for w in ws), ws)
        self.assertTrue(any("Foo_Get: new `&= ~`" in w for w in ws), ws)

    def test_zeroed_field_before_a_check(self):
        new = BASE.replace("        CompareState(ctx, a, b);\n", "        b.y = 0;\n        CompareState(ctx, a, b);\n")
        ws = run(new)
        self.assertEqual(ws, ["Foo_Get: new `b.y = 0; before a check on b`: b.y = 0;"])

    def test_new_skip_in_a_helper_names_its_tests(self):
        new = BASE.replace("    if (a.y != b.y)", "    if (a.ignoreY) return;\n    if (a.y != b.y)")
        ws = run(new)
        self.assertTrue(any(w.startswith("Foo_Get (via CompareState): new ") for w in ws), ws)

    def test_return_after_a_failed_check_is_not_a_skip(self):
        new = BASE.replace("        CompareState(ctx, a, b);\n", "        if (!CheckSame(ctx, a, b)) return;\n"
                                                                    "        CompareState(ctx, a, b);\n")
        self.assertEqual(run(new), [])

    def test_return_on_the_line_after_a_failure_is_not_a_skip(self):
        new = BASE.replace('        if (a.r != b.r) ctx.Fail("ret");\n',
                           '        if (a.r != b.r) {\n            ctx.Fail("ret");\n            return;\n        }\n')
        self.assertEqual(run(new), [])

    def test_a_skip_moved_between_helpers_is_not_new(self):
        base = BASE.replace("    for (int i = 0; i < cases; ++i) Run(ctx, i);\n",
                            "    for (int i = 0; i < cases; ++i) {\n        if (i == 3) continue;\n        Run(ctx, i);\n    }\n")
        new = base.replace("    for (int i = 0; i < cases; ++i) {\n        if (i == 3) continue;\n        Run(ctx, i);\n    }\n",
                           "    for (int i = 0; i < cases; ++i) Step(ctx, i);\n").replace(
            "constexpr int kSeeds = 100;\n", "constexpr int kSeeds = 100;\nvoid Step(difftest::Context &ctx, int i)\n{\n"
            "    for (int i = 0; i < cases; ++i) {\n        if (i == 3) continue;\n        Run(ctx, i);\n    }\n}\n")
        self.assertEqual(run(new, base), [])  # a new helper an existing test calls, holding the base's lines
        changed = new.replace("    for (int i = 0; i < cases; ++i) {\n        if (i == 3) continue;",
                              "    for (int i = 0; i < cases; ++i) {\n        Run(ctx, -1);\n        if (i == 3) continue;")
        self.assertEqual(run(changed, base), ["Foo_Children (via Step): new `continue;`: if (i == 3) continue;"])

    def test_kind_mask_is_not_a_mask(self):
        new = BASE.replace('        if (a.r != b.r) ctx.Fail("ret");\n',
                           '        if (a.r != b.r) ctx.Fail("ret");\n        uint32_t want = a.flags & KindMask;\n')
        self.assertEqual(run(new), [])

    def test_helper_users_are_listed_once(self):
        overload = ("void CompareState(difftest::Context &ctx, const Other &a, const Other &b)\n{\n"
                    "    if (a.z != b.z) ctx.Fail(\"z\");\n}\n")
        base = BASE.replace("void ChildCases(", overload + "void ChildCases(")
        new = base.replace("    if (a.y != b.y)", "    if (skipY) return;\n    if (a.y != b.y)")
        self.assertEqual([w.split(":")[0] for w in run(new, base)], ["Foo_Get (via CompareState)"])
        new = BASE.replace("        CompareState(ctx, a, b);\n", "        CompareState(ctx, a, b);\n"
                                                                  "        CompareState(ctx, b, a);\n").replace(
            "    if (a.y != b.y)", "    if (skipY) return;\n    if (a.y != b.y)")
        ws = [w for w in run(new) if "via" in w]
        self.assertEqual([w.split(":")[0] for w in ws], ["Foo_Get (via CompareState)"])

    def test_new_test_with_skips_is_not_a_weakening(self):
        new = BASE + "DIFF_TEST(Foo_New, 0x1030)\n{\n    for (int i = 0; i < 3; ++i) {\n        if (i == 1) continue;\n        Check(ctx, i);\n    }\n}\n"
        self.assertEqual(run(new), [])

    def test_strengthening_is_silent(self):
        new = BASE.replace('        if (a.r != b.r) ctx.Fail("ret");\n',
                           '        if (a.r != b.r) ctx.Fail("ret");\n        if (a.z != b.z) ctx.Fail("z");\n')
        self.assertEqual(run(new), [])

    def test_wrapper_macro_tests(self):
        base = ("#define FACE_TEST(Name, Rva) \\\n    DIFF_TEST(Lot_##Name, Rva) \\\n"
                "    { for (int seed = 0; seed < 200; ++seed) Pair(ctx, seed); }\n"
                "FACE_TEST(GetId, 0x3000)\nFACE_TEST(GetName, 0x3010)\n")
        self.assertEqual(run(base.replace("FACE_TEST(GetName, 0x3010)\n", ""), base),
                         ["Lot_GetName: removed (not registered anywhere in the branch's tests/, "
                          "rva 0x3010 named nowhere there)"])


if __name__ == "__main__":
    unittest.main()
