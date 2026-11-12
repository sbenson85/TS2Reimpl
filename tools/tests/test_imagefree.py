#!/usr/bin/env python3
"""Tests for tools/imagefree.py on a synthetic graph: closures through a chain and a cycle, 'any' and 'only' blocker
attribution (a sole blocker, a shared one), the member-to-class-vtable edges of the lower bound and their removal, and
the image symbol keys.

Run: tools/py -I tools/tests/test_imagefree.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import imagefree  # noqa: E402
import noimage  # noqa: E402

X = ("address", 0x10cc288)
Y = ("original", 0x211da3)
Z = ("address", 0xd9b470)


def graph(names, edges, refs):
    """A Graph of one object with the given node names, edges (name -> names) and direct references (name -> keys)."""
    g = imagefree.Graph()
    idx = {n: g.node(0, n) for n in names}
    for a, bs in edges.items():
        g.edges[idx[a]].update(idx[b] for b in bs)
    for a, ks in refs.items():
        g.refs[idx[a]].update(ks)
    for n, i in idx.items():
        g.defs[n].append(i)
    return g, idx


def reached(g, idx, comp_bits, keys, name):
    return {keys[i] for i in imagefree.iter_bits(comp_bits[idx[name]])}


class Closures(unittest.TestCase):
    def setUp(self):
        # chain a -> b -> c -> X; cycle p -> q -> r -> p, r -> Y, s -> p; shared: both -> c and q; free -> leaf
        self.g, self.idx = graph(
            ["a", "b", "c", "p", "q", "r", "s", "both", "free", "leaf"],
            {"a": ["b"], "b": ["c"], "p": ["q"], "q": ["r"], "r": ["p"], "s": ["p"], "both": ["c", "q"],
             "free": ["leaf"]},
            {"c": [X], "r": [Y]})
        self.comp_bits, self.keys = imagefree.closures(self.g)

    def test_chain(self):
        for n in ("a", "b", "c"):
            self.assertEqual(reached(self.g, self.idx, self.comp_bits, self.keys, n), {X}, n)

    def test_cycle(self):
        # every member of the cycle reaches what any member references, and so does a node entering it
        for n in ("p", "q", "r", "s"):
            self.assertEqual(reached(self.g, self.idx, self.comp_bits, self.keys, n), {Y}, n)

    def test_union_and_free(self):
        self.assertEqual(reached(self.g, self.idx, self.comp_bits, self.keys, "both"), {X, Y})
        self.assertEqual(self.comp_bits[self.idx["free"]], 0)
        self.assertEqual(self.comp_bits[self.idx["leaf"]], 0)

    def test_attribution(self):
        # verified functions by rva: two blocked by X alone, one by Y alone, one by both, one free, one not located
        node_of = {0x100: "a", 0x200: "b", 0x300: "s", 0x400: "both", 0x500: "free"}
        size = {0x100: 10, 0x200: 20, 0x300: 40, 0x400: 80, 0x500: 160, 0x600: 320}.get
        bits = {r: self.comp_bits[self.idx[n]] for r, n in node_of.items()}
        bits[0x600] = None
        anyb, only = imagefree.attribute(bits, size)
        by = {self.keys[i]: (anyb[i], only[i]) for i in anyb}
        self.assertEqual(by[X], (10 + 20 + 80, 10 + 20))  # sole blocker of a and b; shared with Y in 'both'
        self.assertEqual(by[Y], (40 + 80, 40))
        self.assertEqual(set(by), {X, Y})  # the free and the unlocated add nothing

    def test_shared_blocker_has_no_only(self):
        bits = {0x400: self.comp_bits[self.idx["both"]]}
        anyb, only = imagefree.attribute(bits, {0x400: 8}.get)
        self.assertEqual(sorted(anyb.values()), [8, 8])
        self.assertFalse(any(only.values()))


class ClassEdges(unittest.TestCase):
    def test_member_reaches_own_vtable_and_is_removed(self):
        member = "?Get@cFoo@@QAEHXZ"
        free_fn = "?Helper@@YAHXZ"
        vt = "??_7cFoo@@6B@"
        other_vt = "??_7cBar@@6B@"
        g, idx = graph([member, free_fn, vt, other_vt, "slot"], {vt: ["slot"]}, {"slot": [Z]})
        before = [set(e) for e in g.edges]
        static_bits, _ = imagefree.closures(g)
        self.assertEqual(static_bits[idx[member]], 0)
        added = g.add_class_edges()
        self.assertEqual(added, {idx[member]: {idx[vt]}})  # not the free function, not another class's vtable
        lower_bits, keys = imagefree.closures(g)
        self.assertEqual({keys[i] for i in imagefree.iter_bits(lower_bits[idx[member]])}, {Z})
        self.assertEqual(lower_bits[idx[free_fn]], 0)
        g.remove_edges(added)
        self.assertEqual([set(e) for e in g.edges], before)

    def test_member_pattern(self):
        for name, scope in (("?Get@cFoo@@QAEHXZ", "cFoo"), ("??0cFoo@nS@@QAE@XZ", "cFoo@nS"),
                            ("??1cFoo@@UAE@XZ", "cFoo"), ("?Run@cFoo@nS@@MAEXXZ", "cFoo@nS"),
                            ("??4cFoo@@QAEAAV0@ABV0@@Z", "cFoo"), ("??_GcFoo@@UAEPAXI@Z", "cFoo")):
            m = imagefree.MEMBER.match(name)
            self.assertIsNotNone(m, name)
            self.assertEqual(m.group(1), scope, name)
        for name in ("?Helper@@YAHXZ", "?Make@cFoo@@SAPAV1@XZ", "??_7cFoo@@6B@"):  # free, static member, vtable
            self.assertIsNone(imagefree.MEMBER.match(name), name)


class ImageKeys(unittest.TestCase):
    def test_keys(self):
        self.assertEqual(imagefree.image_key(noimage.GAME_BASE_SYMBOL, 0x10cc288, set()), X)
        self.assertEqual(imagefree.image_key(noimage.GAME_BASE_SYMBOL, 0, set()), ("address", 0))
        self.assertEqual(imagefree.image_key("_ts2_rva_d9b470", 4, set()), ("address", 0xd9b474))
        self.assertIsNone(imagefree.image_key("_ts2_rva_d9b470", 0, {"_ts2_rva_d9b470"}))  # an owned static we define
        self.assertIsNone(imagefree.image_key("?Helper@@YAHXZ", 0, set()))


if __name__ == "__main__":
    unittest.main()
