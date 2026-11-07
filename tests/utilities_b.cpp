// Differential tests for the UtilitiesB group (src/misc/UtilitiesB.h).
//
// 0x18bf80 stores -1 in the word at `this` and returns `this`. The test runs both on words
// surrounded by a pattern, so a write to a neighbouring byte or a wrong return fails.
#include "difftest.h"
#include "misc/UtilitiesB.h"

#include <cstring>
#include <random>

namespace {

using SetInvalidFn = void *(__thiscall *)(void *);

} // namespace

DIFF_TEST(UtilitiesB_SetInvalidIndex, 0x18bf80)
{
    auto orig = ctx.Original<SetInvalidFn>(0x18bf80);
    std::mt19937 gen(18);
    for (int round = 0; round < 4096; ++round) {
        // Words around the target: 4 bytes before, the target, 8 bytes after.
        uint8_t a[16], b[16];
        for (uint8_t &byte : a) byte = static_cast<uint8_t>(gen());
        std::memcpy(b, a, sizeof(a));
        uint8_t *targetA = a + 4;
        uint8_t *targetB = b + 4;

        void *retA = orig(targetA);
        void *retB = UtilitiesB_SetInvalidIndex(targetB, nullptr);
        ++ctx.cases;
        if (retA != targetA || retB != targetB)
            ctx.Fail("return differs (round %d): original %p, ours %p", round, retA, retB);
        if (std::memcmp(a, b, sizeof(a)) != 0)
            ctx.Fail("written bytes differ (round %d): original %08x, ours %08x", round,
                     *reinterpret_cast<uint32_t *>(targetA), *reinterpret_cast<uint32_t *>(targetB));
        if (*reinterpret_cast<int32_t *>(targetB) != -1)
            ctx.Fail("ours did not store -1 (round %d)", round);
    }
}
