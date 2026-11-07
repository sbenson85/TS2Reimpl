#!/usr/bin/env python3
"""Tests for tools/missedfuncs.py's --add extent (manual_extent): where a function no record has ends; and the
jump-table pieces (--jump-tables, --switch): table_targets, descend, switch_cuts, switch_entry.

Run: .venv/Scripts/python -I tools/tests/test_missedfuncs.py
"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import missedfuncs as M  # noqa: E402

BASE = 0x1000
NEW, CTOR, CTOR2 = 0x3000, 0x3100, 0x3200


def factory(at, size, ctor):
    """push size; call new; pop ecx; test eax, eax; je null; mov ecx, eax; jmp ctor; null: xor eax, eax; ret (25 bytes)."""
    b = b"\x68" + struct.pack("<I", size) + b"\xe8" + struct.pack("<i", NEW - (at + 10)) + b"\x59\x85\xc0\x74\x08\x8b\xc8"
    b += b"\xe9" + struct.pack("<i", ctor - (at + 22)) + b"\x33\xc0\xc3"
    assert len(b) == 25
    return b


def section(*pieces):
    data = bytearray(b"\x90" * 0x400)
    for at, b in pieces:
        data[at - BASE:at - BASE + len(b)] = b
    return bytes(data)


class Extent(unittest.TestCase):
    def test_packed_factories_end_at_the_null_tail(self):
        # two factories back to back, no padding, the second unknown (KJ: the first was sized 141, swallowing them)
        data = section((0x1000, factory(0x1000, 0xbc, CTOR)), (0x1019, factory(0x1019, 0xc8, CTOR2)),
                       (0x1032, b"\xc3\xcc"))
        end, callees, why = M.manual_extent(data, BASE, 0x1000, {NEW, CTOR, CTOR2})
        self.assertEqual(end - 0x1000, 25)
        self.assertEqual(callees, [hex(NEW)])
        self.assertIn("0x1018", why)
        end, _, _ = M.manual_extent(data, BASE, 0x1019, {NEW, CTOR, CTOR2})
        self.assertEqual(end - 0x1019, 25)

    def test_an_early_return_a_branch_skips(self):
        # test eax, eax; je +1; ret; xor eax, eax; ret; then more code without padding
        data = section((0x1000, b"\x85\xc0\x74\x01\xc3\x33\xc0\xc3\x55\x8b\xec\x5d\xc3\xcc"))
        end, _, why = M.manual_extent(data, BASE, 0x1000, set())
        self.assertEqual(end, 0x1008)
        self.assertIn("no branch goes past it", why)

    def test_a_local_jump_over_a_return(self):
        # jmp +1 (forward, inside); ret; ret: the body is the jmp's target too
        data = section((0x1000, b"\xeb\x01\xc3\xc3\x55\xc3\xcc"))
        self.assertEqual(M.manual_extent(data, BASE, 0x1000, set())[0], 0x1004)

    def test_a_jump_to_a_known_start_is_a_tail_jump(self):
        # je <known start right after>; ret: the je doesn't extend the body
        data = section((0x1000, b"\x74\x01\xc3\x55\xc3"))
        self.assertEqual(M.manual_extent(data, BASE, 0x1000, {0x1003})[0], 0x1003)
        self.assertEqual(M.manual_extent(data, BASE, 0x1000, set())[0], 0x1005)  # unknown: part of the body

    def test_a_tail_jump_ends_it(self):
        # jmp <known start far away>, then the next (unknown) function without padding
        data = section((0x1000, b"\xe9" + struct.pack("<i", 0x1200 - 0x1005) + b"\x55\xc3\xcc"))
        end, _, why = M.manual_extent(data, BASE, 0x1000, {0x1200})
        self.assertEqual(end, 0x1005)
        self.assertIn("jmp 0x1200", why)

    def test_padding_ends_it(self):
        # je over the ret, but the ret is followed by int3 padding: the target is another function's code
        data = section((0x1000, b"\x74\x02\xc3\xcc\xc3"))
        end, _, why = M.manual_extent(data, BASE, 0x1000, set())
        self.assertEqual(end, 0x1003)
        self.assertIn("padding", why)

    def test_next_known_start_ends_it(self):
        data = section((0x1000, b"\x55\x8b\xec\x5d\x55\x8b\xec\x5d\xc3"))
        end, _, why = M.manual_extent(data, BASE, 0x1000, {0x1004})
        self.assertEqual(end, 0x1004)
        self.assertIn("next known function start", why)

    def test_no_end(self):
        data = b"\x90" * 0x40
        end, _, why = M.manual_extent(data, BASE, 0x1000, set())
        self.assertIsNone(end)
        self.assertIn("no end", why)


IB = 0x400000  # image base: jump tables hold VAs


def dd(*rvas):
    return b"".join(struct.pack("<I", IB + r) for r in rvas)


def switch_fn():
    """0x1000: xor eax, eax; jmp [eax*4 + table] (Ghidra's cut, 9 bytes); case 0 at 0x1009: call NEW; ret;
    case 1 at 0x100f: mov eax, 1; ret; padding; the table at 0x1018, then padding; the next function at 0x1030."""
    return section((0x1000, b"\x31\xc0\xff\x24\x85" + struct.pack("<I", IB + 0x1018)),
                   (0x1009, b"\xe8" + struct.pack("<i", NEW - 0x100e) + b"\xc3"),
                   (0x100f, b"\xb8\x01\x00\x00\x00\xc3\xcc\xcc\xcc"),
                   (0x1018, dd(0x1009, 0x100f) + b"\xcc" * 16), (0x1030, b"\xc3"))


class JumpTables(unittest.TestCase):
    def test_descent_through_the_table(self):
        end, tables, callees = M.descend(switch_fn(), BASE, IB, 0x1000, 0x1030)
        self.assertEqual(end, 0x1015)  # the cases' code, not the table after it
        self.assertEqual(tables, {0x1002: [0x1009, 0x100f]})
        self.assertEqual(callees, [NEW])

    def test_table_ends_at_a_bound_out_of_range_or_code_it_targets(self):
        data = switch_fn()
        self.assertEqual(M.table_targets(data, BASE, IB, IB + 0x1018, 0x1000, 0x1030, count=1), [0x1009])
        self.assertEqual(M.table_targets(data, BASE, IB, IB + 0x1018, 0x1000, 0x100f), [0x1009])  # out of range
        # entries pointing at 0x1108, right after the table: the table stops where its own case code starts
        data = section((0x1100, dd(0x1108, 0x1108, 0x1108, 0x1108)))
        self.assertEqual(M.table_targets(data, BASE, IB, IB + 0x1100, 0x1000, 0x1200), [0x1108, 0x1108])

    def test_a_bounds_check_limits_the_table(self):
        # cmp eax, 0; ja +0; jmp [eax*4 + table]: one case, though the table's second entry is in range too
        data = section((0x1000, b"\x83\xf8\x00\x77\x00\xff\x24\x85" + struct.pack("<I", IB + 0x1018)),
                       (0x100d, b"\xc3"), (0x100f, b"\xc3"), (0x1018, dd(0x100d, 0x100f) + b"\xcc" * 4))
        _, tables, _ = M.descend(data, BASE, IB, 0x1000, 0x1030)
        self.assertEqual(tables, {0x1005: [0x100d]})

    def test_the_check_lists_cases_in_no_function(self):
        funcs = [{"rva": "0x1000", "size": 9, "name": "FUN", "callees": []}, {"rva": "0x1030", "size": 1, "name": "G"}]
        status = {"0x1000": {"status": "verified", "name": "C::Switch"}}
        rows = M.switch_cuts(switch_fn(), BASE, IB, funcs, status)
        self.assertEqual(len(rows), 1)
        r = rows[0]
        self.assertEqual((r["rva"], r["name"], r["size"], r["full"]), ("0x1000", "C::Switch", 9, 0x15))
        self.assertEqual(r["cases"], ["0x1009", "0x100f"])
        self.assertEqual(r["tail_callees"], [hex(NEW)])
        self.assertEqual(r["touches"], {"0x1000": "verified"})
        # once the record covers its cases (a resized entry merged in), nothing is listed
        funcs[0]["size"] = 0x15
        self.assertEqual(M.switch_cuts(switch_fn(), BASE, IB, funcs, status), [])

    def test_switch_entry(self):
        e = M.switch_entry(switch_fn(), BASE, IB, {"rva": "0x1000", "size": 9, "callees": []}, 0x1030, "manual: test")
        self.assertEqual((e["rva"], e["size"], e["ghidra_size"], e["kind"]), ("0x1000", 0x15, 9, "resized"))
        self.assertEqual(e["callees"], [hex(NEW)])
        self.assertTrue(e["source"].startswith("manual: switch: 1 jump table(s), 2 cases"))
        self.assertTrue(e["source"].endswith("; test"))
        self.assertIsNone(M.switch_entry(switch_fn(), BASE, IB, {"rva": "0x1000", "size": 0x15}, 0x1030, "x"))


if __name__ == "__main__":
    unittest.main()
