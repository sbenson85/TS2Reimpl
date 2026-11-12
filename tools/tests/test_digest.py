#!/usr/bin/env python3
"""Tests for tools/digest.py: the layout report parser (bases, nested bases, vfptr/vbptr, vtordisp, virtual bases),
the type and filter helpers, member comments against the compiler's offsets, and the header's helper rows, on a
small synthetic report and header. No compiler, no db, no game content.

Run: tools/py -I tools/tests/test_digest.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import digest  # noqa: E402
import symfind  # noqa: E402

# A made-up report in the compiler's format: tab after the offset, `| ` per depth.
REPORT = "\n".join([
    "class cKey\tsize(4):",
    "\t+---",
    " 0\t| mA",
    "\t+---",
    "",
    "class cDerived\tsize(28):",
    "\t+---",
    " 0\t| +--- (base class cBase)",
    " 0\t| | +--- (base class cRoot)",
    " 0\t| | | {vfptr}",
    "\t| | +---",
    " 4\t| | mA",
    "\t| +---",
    " 8\t| {vbptr}",
    "12\t| cKey mKey",
    "16\t| mB",
    "\t+---",
    "20\t| (vtordisp for vbase cVirt)",
    "\t+--- (virtual base cVirt)",
    "24\t| {vfptr}",
    "\t+---",
    "",
    "class `anonymous-namespace'::cFake\tsize(8):",
    "\t+---",
    " 0\t| mX",
    " 4\t| mY",
    "\t+---",
    "",
])

HEADER = """\
#pragma once
#define HOLDER_H
// Mixes two keys into one. More words.
uint32_t MixKeys(uint32_t a, uint32_t b);
constexpr int kSlots = 4;
struct cKey { uint32_t mA; };
class cHolder {
public:
    uint32_t mA; // +0x4 the first word
    void Get();
};
"""


class Layouts(unittest.TestCase):
    def setUp(self):
        self.layouts = digest.parse_layouts(REPORT)

    def test_blocks_and_sizes(self):
        self.assertEqual(set(self.layouts), {"cKey", "cDerived", "`anonymous-namespace'::cFake"})
        self.assertEqual(self.layouts["cDerived"]["size"], 28)
        self.assertEqual(self.layouts["cKey"]["rows"], [(0, None, "+---"), (1, 0, "mA"), (0, None, "+---")])

    def test_entries_kinds_depths_within(self):
        rows = digest.layout_entries(self.layouts["cDerived"])
        got = [(r["kind"], r["depth"], r["offset"], r["name"], r["within"]) for r in rows]
        self.assertEqual(got, [
            ("base", 1, 0, "cBase", ""),
            ("base", 2, 0, "cRoot", "cBase"),
            ("vfptr", 3, 0, "{vfptr}", "cRoot"),
            ("member", 2, 4, "mA", "cBase"),
            ("vbptr", 1, 8, "{vbptr}", ""),
            ("member", 1, 12, "mKey", ""),
            ("member", 1, 16, "mB", ""),
            ("vtordisp", 1, 20, "vtordisp for cVirt", ""),
            ("vbase", 1, 24, "cVirt", ""),  # the header has no offset: its first row's
            ("vfptr", 2, 24, "{vfptr}", "cVirt"),
        ])
        (key,) = [r for r in rows if r["name"] == "mKey"]
        self.assertEqual(key["ctype"], "cKey")

    def test_layout_key(self):
        self.assertEqual(digest.layout_key(self.layouts, "cKey"), "cKey")
        self.assertEqual(digest.layout_key(self.layouts, "cFake"), "`anonymous-namespace'::cFake")
        self.assertEqual(digest.layout_key(self.layouts, "nWidget::cDerived"), "cDerived")
        self.assertIsNone(digest.layout_key(self.layouts, "cMissing"))


class Types(unittest.TestCase):
    def test_type_size(self):
        layouts = digest.parse_layouts(REPORT)
        for typ, size in [("uint32_t", 4), ("const char *", 4), ("float[3]", 12), ("uint8_t[2][4]", 8),
                          ("std::vector<int>", 12), ("cKey", 4), ("nWidget::cKey", 4), ("double", 8),
                          ("cUnknown", None), ("int[kCount]", None)]:
            self.assertEqual(digest.type_size(typ, layouts), size, typ)

    def test_demangle(self):
        self.assertEqual(digest.demangle("?$vector@UcLight@nTS@@V?$allocator@UcLight@nTS@@@std@@"), "vector<cLight>")
        self.assertEqual(digest.demangle("?$vector@IV?$allocator@I@std@@"), "vector<uint32_t>")
        self.assertEqual(digest.demangle("cKey"), "cKey")


class Filters(unittest.TestCase):
    def test_parse_filter(self):
        self.assertEqual(digest.parse_filter("0x22c"), ("off", 0x22C, 0x22C))
        self.assertEqual(digest.parse_filter("+0x50c-0x520"), ("off", 0x50C, 0x520))
        self.assertEqual(digest.parse_filter("slot13"), ("slot", "13"))
        self.assertEqual(digest.parse_filter("s+0x3c:2"), ("slot", "+0x3c:2"))
        self.assertEqual(digest.parse_filter("Mapped"), ("text", "mapped"))
        self.assertIsNone(digest.parse_filter(None))

    def test_keep_member_overlaps_range(self):
        flt = digest.parse_filter("0x50c-0x520")
        self.assertTrue(digest.keep_member((0x508, "x", 8), flt))  # runs into the range
        self.assertFalse(digest.keep_member((0x500, "x", 4), flt))
        self.assertFalse(digest.keep_member((None, "x", 4), flt))

    def test_keep_method_status_exact(self):
        row = " 0x2c9590   571       -  mapped    cFoo::Bar"
        self.assertTrue(digest.keep_method(row, ("text", "mapped")))
        self.assertFalse(digest.keep_method(row.replace("mapped   ", "verified "), ("text", "mapped")))

    def test_slot_key_order(self):
        tags = ["", "+0x3c:1", "13", "2", "+0xc:0"]
        self.assertEqual(sorted(tags, key=digest.slot_key), ["2", "13", "+0xc:0", "+0x3c:1", ""])

    def test_strip_rva_only_its_own(self):
        self.assertEqual(digest.strip_rva("0x2dfc80 (ret 4): gives it", "0x2dfc80"), "gives it")
        self.assertEqual(digest.strip_rva("0x111111: the body", "0x2dfc80"), "0x111111: the body")


class Header(unittest.TestCase):
    def setUp(self):
        self.decls = symfind.parse(HEADER, "src/w/Holder.h")

    def test_member_doc_checks_the_comment_offset(self):
        (a,) = [d for d in self.decls if d["name"] == "mA" and d["scope"] == "cHolder"]
        self.assertEqual(digest.comment_offset(a), 4)
        self.assertEqual(digest.member_doc(a, 4), "the first word")
        self.assertTrue(digest.member_doc(a, 8).startswith("(comment: 0x4!)"))

    def test_helper_rows(self):
        rows, types = digest.helper_rows(self.decls, "cHolder", {"cKey": {"size": 4, "rows": []}})
        text = "\n".join(rows)
        self.assertIn("MixKeys", text)
        self.assertIn("Mixes two keys into one.", text)
        self.assertIn("kSlots", text)
        self.assertNotIn("Get", text)  # a member, shown with the methods
        self.assertNotIn("HOLDER_H", text)  # an include guard
        self.assertEqual(types, ["L6 cKey (4)"])


if __name__ == "__main__":
    unittest.main()
