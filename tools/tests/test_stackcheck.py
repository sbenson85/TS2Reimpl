#!/usr/bin/env python3
"""Tests for tools/stackcheck.py: a direct call or jump from our code into the game image's span
(OriginalFunction<Fn, 0xRVA>() compiles to one, src/game/original.h) is the original function there, whose
`ret N` is known; it must not become an unknown pop amount in our side's stack equations.

Synthetic only (hand-assembled code, a fake original image); no game bytes.

Run: .venv/Scripts/python -I tools/tests/test_stackcheck.py
"""
import os
import struct
import sys
import unittest

import capstone

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import stackcheck as S  # noqa: E402

BASE = 0x400000
RVA = 0x1000


class FakeGame:
    def __init__(self, rets):
        self._rets = {k: set(v) for k, v in rets.items()}

    def rets(self, rva, depth=0):
        return self._rets.get(rva, set())

    def noreturn(self, rva, depth):
        return False

    def moves_esp(self, rva):
        return False


def ours(code, game=None):
    """An Image of our own code at RVA (only what call_pops reads)."""
    img = S.Image.__new__(S.Image)
    img.base, img.game = BASE, game
    img.systems, img.call_pops_cache, img.cache, img.complete, img.tables, img.esp_cache = {}, {}, {}, {}, {}, {}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    img.instructions = lambda rva: {i.address - BASE: i for i in md.disasm(code, BASE + RVA)}
    img.rets = lambda rva, depth=0: set()
    img.noreturn = lambda rva, depth: False
    img.moves_esp = lambda rva: False
    return img


def call_to(address, at):
    return b"\xe8" + struct.pack("<I", (address - (BASE + at + 5)) & 0xFFFFFFFF)


# push 1; push 2; call <target>; ret
def code(target):
    return b"\x6a\x01\x6a\x02" + call_to(target, RVA + 4) + b"\xc3"


class GameCalls(unittest.TestCase):
    def test_callee_maps_the_game_span(self):
        img = ours(b"", FakeGame({}))
        self.assertEqual(img.callee(S.GAME_BASE + 0x1234), (img.game, 0x1234))
        self.assertEqual(img.callee(BASE + 0x2000), (img, 0x2000))
        bare = ours(b"")  # no game image given: the address is this image's own
        self.assertEqual(bare.callee(S.GAME_BASE + 0x1234), (bare, S.GAME_BASE + 0x1234 - BASE))

    def test_known_original_ret_is_used(self):
        target = S.GAME_BASE + 0x5000
        known = ours(code(target), FakeGame({0x5000: [8]}))
        pops, *_ = S.call_pops(known, RVA)
        self.assertEqual(known.systems[RVA][0], {})  # no unknown pop amount: the callee's `ret 8` was applied
        self.assertEqual(pops, {})
        unknown = ours(code(target), None)
        S.call_pops(unknown, RVA)
        self.assertEqual(len(unknown.systems[RVA][0]), 1)  # without the original image the call is an unknown


if __name__ == "__main__":
    unittest.main(verbosity=2)
