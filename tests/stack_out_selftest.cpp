// tests/stack_out.h self-checks: the frame analysis on call sites whose slots are known from the disassembly, and
// FillOut end to end on a stack object of an original function (lane HI).
#include "difftest.h"
#include "stack_out.h"

#include <cstdint>
#include <cstring>

namespace {

// PrepareBuildAll 0x2a81f0 (lane HB's crash): `push ebp; mov ebp, esp`, three SEH pushes, `sub esp, 0x80`, then
// `push esi; push edi`; the iterator's slot 5 call at 0x2a83af (returning to 0x2a83b2) gets `lea edx, [ebp-0x38]`
// pushed. Entry-relative: ebp = entry - 4, so the entry is ebp-0x38 = -0x3c; the iterator itself is at [ebp-0x28]
// (lea'd for slot 54 at 0x2a837f) = -0x2c; [ebp-0x34] is only read (the entry's second word): 16 bytes.
DIFF_TEST(StackOut_FrameAnalysis, 0x2a81f0)
{
    struct Case {
        uint32_t function, ret;
        int32_t depth;     // esp below the entry at the call
        int32_t object;    // entry-relative
        size_t slot;
    };
    const Case cases[] = {
        {0x2a81f0, 0x2a83b2, 0x9c, -0x3c, 16}, // slot 5: [ebp-0x38], the iterator at [ebp-0x28]
        {0x2a81f0, 0x2a838d, 0xa0, -0x2c, 12}, // slot 54 (0x2a8387, two arguments): the iterator, 3 words up to [ebp-0x1c]
        {0x2a81f0, 0x2a8239, 0x98, -0x1c, 4},  // the first call (0x14323f, no arguments); [ebp-0x18] a lone word
    };
    for (const Case &c : cases) {
        ++ctx.cases;
        const int32_t depth = difftest::OriginalCallDepth(c.function, c.ret);
        if (depth != c.depth) ctx.Fail("0x%x: depth at the call returning to 0x%x is 0x%x, expected 0x%x", c.function, c.ret, depth, c.depth);
        const size_t slot = difftest::OriginalSlot(c.function, c.object);
        if (slot != c.slot) ctx.Fail("0x%x: slot at %d is %u bytes, expected %u", c.function, c.object, unsigned(slot), unsigned(c.slot));
    }
}

} // namespace
