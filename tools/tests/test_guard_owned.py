#!/usr/bin/env python3
"""Tests for tools/guard.py's check of a branch's edit to standalone/owned_statics.cpp or owned_graphics.cpp (owned
statics and table aliases for what the branch owns are accepted without --paths).

Run: .venv/Scripts/python -I tools/tests/test_guard_owned.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import guard as G  # noqa: E402

BASE = """#include "game/owned.h"
TS2_OWNED_STATIC(int, 10c93f4){};
#pragma comment(linker, "/alternatename:_ts2_rva_db07d4=_ts2_own_vt_db07d4")
"""
SRC = """// src/rz/cRZLock.cpp
extern "C" const void *const ts2_own_vt_d9b644[];
TS2_OWNED_STATIC(void *, 10f9100);
"""
ALIAS = '#pragma comment(linker, "/alternatename:_ts2_rva_d9b644=_ts2_own_vt_d9b644") // cRZLock\n'


class OwnedStandalone(unittest.TestCase):
    def test_alias_for_a_table_the_branch_owns(self):
        self.assertEqual(G.owned_standalone_change(BASE, BASE + ALIAS, SRC), [])

    def test_owned_static_and_comments(self):
        new = BASE + "\n// the lock's cell (lane X)\nTS2_OWNED_STATIC(void *, 10f9100){};  // cached\n"
        self.assertEqual(G.owned_standalone_change(BASE, new, SRC), [])

    def test_rva_the_branch_does_not_own(self):
        errors = G.owned_standalone_change(BASE, BASE + ALIAS, "// nothing here\n")
        self.assertEqual(len(errors), 1)
        self.assertIn("0xd9b644", errors[0])

    def test_rva_owned_only_by_a_longer_hex(self):
        # ts2_rva_1d9b644 is another address: no match on the tail
        self.assertEqual(len(G.owned_standalone_change(BASE, BASE + ALIAS, "ts2_own_vt_d9b6440\n")), 1)

    def test_other_code_is_an_error(self):
        errors = G.owned_standalone_change(BASE, BASE + "static int helper() { return 1; }\n", SRC)
        self.assertEqual(len(errors), 1)
        self.assertIn("other than", errors[0])

    def test_removing_or_changing_a_line_is_an_error(self):
        errors = G.owned_standalone_change(BASE, BASE.replace("TS2_OWNED_STATIC(int, 10c93f4){};\n", ""), SRC)
        self.assertEqual(len(errors), 1)
        self.assertIn("removes", errors[0])
        changed = BASE.replace("_ts2_own_vt_db07d4", "_ts2_own_vt_d9b644")
        self.assertTrue(G.owned_standalone_change(BASE, changed, SRC))

    def test_alias_to_an_odd_target_is_an_error(self):
        line = '#pragma comment(linker, "/alternatename:_ts2_rva_d9b644=_x /include:_y")\n'
        self.assertEqual(len(G.owned_standalone_change(BASE, BASE + line, SRC)), 1)


if __name__ == "__main__":
    unittest.main()
