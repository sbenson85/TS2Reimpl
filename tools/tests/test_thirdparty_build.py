#!/usr/bin/env python3
"""Tests for tools/thirdparty_build.py's Lua bug-patch reading and applying, and its output naming (synthetic inputs:
no network, no compiler).

Run: tools/py -I tools/tests/test_thirdparty_build.py
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import thirdparty_build as tb  # noqa: E402

# The shape of lua.org/bugs.html: a 5.0 section, each bug an anchor and a <PRE CLASS="patch"> whose old lines start with
# a literal '<' (not escaped there) and whose new lines' '<' / '&' are entities.
BUGS = """<H2><A NAME="5.0.2">Lua 5.0.2</A></H2> ...
<H2><A NAME="5.0">Lua 5.0</A></H2>
<OL>
<LI><A NAME="5.0-3"></A><P>first bug.
<P>Patch:<PRE CLASS="patch">
a.c:
2c2
<   x = 1;
---
>   x = (y &lt; 2);
</PRE>
<LI><A NAME="5.0-4"></A><P>second bug.
<P>Patch:<PRE CLASS="patch">
a.c:
11a12
>   added();
13d13
<   gone();
</PRE>
<LI><A NAME="5.0-5"></A><P>not wanted.
<P>Patch:<PRE CLASS="patch">
b.c:
1c1
< z
---
> w
</PRE>
</OL>
<H2><A NAME="4.0">Lua 4.0</A></H2>
"""


class Patches(unittest.TestCase):
    def test_reads_the_wanted_bugs_by_file(self):
        groups = tb.lua_bug_patches([3, 4], BUGS)
        self.assertEqual([(b, f) for b, f, _ in groups], [("5.0-3", "a.c"), ("5.0-4", "a.c")])
        op, rng, new, old = groups[0][2][0]
        self.assertEqual((op, rng, new, old), ("c", (2, 2), ["  x = (y < 2);"], ["  x = 1;"]))
        self.assertEqual([h[0] for h in groups[1][2]], ["a", "d"])

    def test_applies_with_an_offset_found_from_the_removed_lines(self):
        # The patch numbers say line 13 holds gone(); in the file it is line 3: offset -10 places the insertion too.
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "a.c"
            p.write_text("one\ntwo\n  gone();\nfour")
            hunks = tb.lua_bug_patches([4], BUGS)[0][2]
            tb.apply_hunks(p, hunks, "5.0-4")
            self.assertEqual(p.read_text(), "one\n  added();\ntwo\nfour")

    def test_refuses_a_hunk_whose_old_lines_are_elsewhere(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "a.c"
            p.write_text("nothing to see\n")
            with self.assertRaises(SystemExit):
                tb.apply_hunks(p, tb.lua_bug_patches([3], BUGS)[0][2], "5.0-3")


class Naming(unittest.TestCase):
    def test_output_dirs_are_named_lib_flags_compiler(self):
        self.assertEqual(tb.out_dir("lua50p").name, "lua50p-O2GSDLUAUSERSTATEint")
        self.assertEqual(tb.out_dir("zlib114a").name, "zlib114a-OxsGzGF-ddk")
        self.assertEqual(tb.out_dir("openssl097d").name, "openssl097d-msnoidea-ddk")
        # libmatch reports a thirdparty object under the directory name's first part: distinct per recipe.
        firsts = [tb.out_dir(n).name.split("-")[0] for n in tb.RECIPES]
        self.assertEqual(len(firsts), len(set(firsts)))


if __name__ == "__main__":
    unittest.main()
