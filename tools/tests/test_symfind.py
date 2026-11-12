#!/usr/bin/env python3
"""Tests for tools/symfind.py: the declaration parser (comments attached, scopes, fields, file headers), the
comment's first sentence, and the query ranking, on small synthetic sources. No game content, no real index.

Run: tools/py -I tools/tests/test_symfind.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import symfind  # noqa: E402

HEADER = """\
// The widget header: everything about widgets.
#pragma once
#include <cstdint>

namespace nWidget {

// ---- the registry ----

// 0x123450: walks the message ids and calls slot 5 for each.
uint32_t WalkIds(void *registry, uint32_t count);

struct cWidgetKey {
    uint32_t mType;  // +0 the resource type
    uint32_t mGroup; // +4
};

class cWidget : public cBase {
public:
    // Builds the widget's render group registry. Other words follow here.
    void BuildRegistry();
    int mCount = 0; // how many there are
    static constexpr int kMax = 8;
private:
    void Helper() { int local = 1; (void)local; }
};

}  // namespace nWidget

#define WIDGET_LIMIT 16
"""


def decls_of(text, path="src/w/Widget.h"):
    return symfind.parse(text, path)


def by_name(decls, name):
    return [d for d in decls if d["name"] == name]


class Lexing(unittest.TestCase):
    def test_comments_and_strings_blanked_same_length(self):
        text = 'int a; // one\nconst char *s = "x // not a comment";\n/* two\nthree */ int b;\n'
        code, comments = symfind.lex(text)
        self.assertEqual(len(code), len(text))
        self.assertEqual(code.count("\n"), text.count("\n"))
        self.assertNotIn("one", code)
        self.assertNotIn("not a comment", code)
        self.assertIn("int b;", code)
        self.assertEqual(comments[1], ["one"])
        self.assertNotIn(2, comments)  # the string's `//` is no comment


class Parsing(unittest.TestCase):
    def setUp(self):
        self.decls = decls_of(HEADER)

    def test_file_header_is_its_own_entry_not_the_first_declaration(self):
        f = [d for d in self.decls if d["kind"] == "file"]
        self.assertEqual(len(f), 1)
        self.assertIn("widget header", f[0]["above"])
        for d in self.decls:
            if d["kind"] != "file":
                self.assertNotIn("widget header", d["above"])

    def test_function_comment_without_heading_and_with_rva(self):
        (d,) = by_name(self.decls, "WalkIds")
        self.assertEqual(d["kind"], "function")
        self.assertEqual(d["scope"], "nWidget")
        self.assertNotIn("registry ----", d["above"])
        self.assertIn(0x123450, d["rvas"])
        self.assertEqual(symfind.first_sentence(symfind.comment_of(d)),
                         "walks the message ids and calls slot 5 for each.")

    def test_fields_take_the_trailing_comment(self):
        (t,) = by_name(self.decls, "mType")
        self.assertEqual((t["kind"], t["scope"], t["type"]), ("field", "nWidget::cWidgetKey", "uint32_t"))
        self.assertIn("resource type", symfind.comment_of(t))
        (c,) = by_name(self.decls, "mCount")
        self.assertEqual(c["kind"], "field")
        self.assertEqual(symfind.comment_of(c), "how many there are")

    def test_brace_initialised_field(self):
        (a,) = by_name(decls_of("struct cS {\n    cFloat3 mAxis{-1, {1.0f, 0.0f}}; // the axis\n};\n"), "mAxis")
        self.assertEqual((a["kind"], a["type"], symfind.comment_of(a)), ("field", "cFloat3", "the axis"))

    def test_conversion_operator_and_array_field(self):
        decls = decls_of("struct S {\n    explicit operator bool() const { return p; }\n    T *mObject;\n"
                         "    uint8_t mOwner[8];\n};\n")
        self.assertEqual([d["kind"] for d in decls if d["name"].startswith("operator")], ["method"])
        (o,) = by_name(decls, "mObject")
        self.assertEqual(o["sig"], "T * mObject")
        (w,) = by_name(decls, "mOwner")
        self.assertEqual(w["sig"], "uint8_t mOwner[8]")

    def test_methods_types_constants_macros(self):
        (b,) = by_name(self.decls, "BuildRegistry")
        self.assertEqual((b["kind"], b["scope"]), ("method", "nWidget::cWidget"))
        (w,) = [d for d in self.decls if d["name"] == "cWidget" and d["kind"] == "type"]
        self.assertIn("public cBase", w["sig"])
        (k,) = by_name(self.decls, "kMax")
        self.assertEqual(k["kind"], "const")
        (m,) = by_name(self.decls, "WIDGET_LIMIT")
        self.assertEqual(m["kind"], "macro")
        # An inline body's locals are not declarations of their own.
        self.assertEqual(by_name(self.decls, "local"), [])
        self.assertEqual(len(by_name(self.decls, "Helper")), 1)


class Sentences(unittest.TestCase):
    def test_lead_tag_dropped(self):
        self.assertEqual(symfind.first_sentence("0x2dfda0 slot 0 (ret 8): gives this. More."), "gives this.")
        self.assertEqual(symfind.first_sentence("+0x50c the groups. Then"), "the groups.")

    def test_prose_before_a_colon_kept(self):
        self.assertTrue(symfind.first_sentence("the order matters: a before b").startswith("the order matters"))

    def test_long_sentence_cut(self):
        s = symfind.first_sentence("word " * 60, limit=40)
        self.assertLessEqual(len(s), 40)
        self.assertTrue(s.endswith("..."))

    def test_scope_split_over_lines_joined(self):
        self.assertIn("Holder::Get", symfind.first_sentence("asks Holder::\nGet for it"))


class Ranking(unittest.TestCase):
    def setUp(self):
        other = """\
// Unrelated: a list of registry things.
struct cThing {
    int mRegistry; // a registry of something
};
// Lerps the light set's keys.
void LerpSetKeys(void *set);
void SetKeys(int n);
"""
        self.index = {"src/w/Widget.h": [symfind.compact(d) for d in decls_of(HEADER)],
                      "tests/fakes.h": [symfind.compact(d) for d in decls_of(other, "tests/fakes.h")]}

    def names(self, query, **kw):
        return [d["name"] for _, _, d in symfind.search(self.index, query.split(), **kw)]

    def test_name_match_ranks_first(self):
        self.assertEqual(self.names("lerp light set keys")[0], "LerpSetKeys")

    def test_comment_words_find_an_uncommon_name(self):
        self.assertEqual(self.names("walk the message ids and call slot 5 for each")[0], "WalkIds")

    def test_all_words_beat_some(self):
        got = self.names("render group registry")
        self.assertEqual(got[0], "BuildRegistry")

    def test_fields_left_out_unless_asked(self):
        self.assertNotIn("mRegistry", self.names("registry"))
        self.assertIn("mRegistry", self.names("registry", fields=True))

    def test_where_filter(self):
        self.assertEqual({p for _, p, _ in symfind.search(self.index, ["keys"], where="tests")}, {"tests/fakes.h"})


if __name__ == "__main__":
    unittest.main()
