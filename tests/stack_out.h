#pragma once
// Out-buffers on the caller's stack, sized by the original's frame (lane HI, 2026-10-18).
//
// The bug class: a fake for a callee that fills an out-buffer writes only the bytes the caller reads (8), the game's
// real callee writes the whole object (16, the original caller's frame slot), and our replacement reserved only the 8
// it reads. The difftest passes (nothing writes past 8 on either side) and the game then overwrites our next local
// (lane HB, PrepareBuildAll 0x2a81f0: the iterator's entry, slot 5, a smoke-run crash after 48 mutants and 600 seeds).
//
// difftest::FillOut(p, wrote), called by a fake after it wrote the first `wrote` bytes at `p`:
//  - p in a frame of original code (the closest return address below p is in the image, in a function the harness
//    knows: a hooked or tested RVA): the stack object at p in that function's frame is measured from its code (from
//    p up to the next offset the function writes, pushes or takes the address of: the slot the compiler reserved;
//    read-only words in between are fields of the object, unused ones its padding). The bytes after `wrote` up to
//    that size are filled with kOutPattern, and the size is remembered for this fake (the FillOut call site).
//  - p in a frame of our code (or the test's): the same tail is filled up to the largest size remembered for this
//    fake, so the replacement gets what the game's callee would write. A buffer that is too small in ours then has its
//    neighbour overwritten with the pattern: the test fails (a wrong result, or 0xa5a5a5a5 used as a pointer).
//  - p not on the stack (heap, globals): nothing beyond `wrote`.
// The routes must run the original first in each case for the size to be known on ours (the usual order); a fake used
// before any original-route call fills only `wrote`. `cap` limits the fill (an object the measurement over-estimates:
// a padding word or dead local after it in the original's frame, while ours keeps a live local there; say why).
//
// OutSlot(p) is the measurement alone (0 when p is not in a known original frame), for tests that want to log it.
#include <cstddef>
#include <cstdint>

namespace difftest {

constexpr uint8_t kOutPattern = 0xa5;

// Returns the bytes now written at p (wrote, or the measured / remembered slot size if larger, up to cap).
size_t FillOut(void *p, size_t wrote, size_t cap = ~size_t(0));

// The size of the original's stack object at p, measured from the code of the original function whose frame holds p;
// 0 when p is not in such a frame (ours, the test's, the heap) or the function can't be analysed.
size_t OutSlot(const void *p);

// The analysis on its own, by RVA (tests/stack_out_selftest.cpp): the slot size at entry-relative offset `entry` (negative,
// the return address at 0) in the frame of the original function at `function`, and the esp depth below its entry at
// the call returning to `ret` (-1: not found or not analysable).
size_t OriginalSlot(uint32_t function, int32_t entry);
int32_t OriginalCallDepth(uint32_t function, uint32_t ret);

} // namespace difftest
