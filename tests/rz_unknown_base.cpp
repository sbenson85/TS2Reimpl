// Differential test for rz::cRZUnknown's slot 4 (src/rz/RZUnknownBase.{h,cpp}): the game's shared RemoveRef body 0x1b9538
// (shared_vf, folded into the vtables of about 289 unrelated classes, so never hooked: docs/verification.md). Ours is called
// through our class's own vtable, the original on a plain {vtable, count} object.
#include "difftest.h"
#include "rz/RZUnknownBase.h"

#include <cstdint>
#include <random>

namespace {

struct PlainUnknown {
    void *vtable;
    uint32_t count;
};

} // namespace

DIFF_TEST(RZUnknownBase_RemoveRef, 0x1b9538)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b9538);
    std::mt19937 rng(0x1b9538);
    const uint32_t fixed[] = {0, 1, 2, 3, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    for (int i = 0; i < 20000; ++i) {
        const uint32_t count = i < 8 ? fixed[i] : (rng() % 2 ? rng() % 5 : static_cast<uint32_t>(rng()));
        PlainUnknown theirs{reinterpret_cast<void *>(0x13572468), count};
        const uint32_t resultA = orig(&theirs);
        rz::cRZUnknown ours;
        ours.mRefCount = count;
        rz::cRZUnknown *face = &ours; // through our vtable
        const uint32_t resultB = face->RemoveRef();
        ++ctx.cases;
        if (resultA != resultB || theirs.count != ours.mRefCount ||
            theirs.vtable != reinterpret_cast<void *>(0x13572468))
            ctx.Fail("RemoveRef(count %u): %u/%u, count %u/%u", count, resultA, resultB, theirs.count, ours.mRefCount);
        ours.mRefCount = 0;
    }
}
