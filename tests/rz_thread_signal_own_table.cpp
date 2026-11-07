// cRZThreadSignal's owned table (lane VX, src/rz/cRZThreadSignalSlots.size.cpp): the image-free program stores
// ts2_own_vt_e61b7c where the hooked game stores the image's table 0xe61b7c. Checked here: the owned static is the
// image's table in this build, each image slot is the RVA we name, and our table holds, slot for slot, what the hooked
// game runs for that slot (the hook's replacement). Slots 0 and 7 are bodies folded with cRZMutex's slots 0 and 6
// (0x9ccb85, 0x9ccb69; the same image entries in cRZMutex's table 0xe9ebf0, checked here too): not hooked, and ours
// there is cRZMutex's member for that body (tests/rz_mutex.cpp verifies it). The slots' behaviour is
// RZThreadSignal_Slots (tests/rz_thread_signal.cpp).
#include "difftest.h"
#include "hook/hooks.h"
#include "rz/cRZMutex.h"
#include "rz/cRZThreadSignal.h"

#include <cstdint>
#include <cstring>

extern "C" const void *ts2_own_vt_e61b7c[8];

namespace {

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

} // namespace

DIFF_TEST(RZThreadSignal_OwnTable, 0xe61b7c)
{
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0)));
    const uint32_t *image = static_cast<const uint32_t *>(ctx.image.At(0xe61b7c));
    const uint32_t *mutex = static_cast<const uint32_t *>(ctx.image.At(cRZMutex::kVTable));
    ++ctx.cases;
    if (static_cast<const void *>(&ts2_rva_e61b7c) != image) ctx.Fail("the owned static is not the image's table");
    const uint32_t expect[8] = {0x9ccb85, 0x9ccb93, 0x9ccb57, 0x9ccbb9, 0x9ccb7a, 0x9ccb38, 0x9ccbfb, 0x9ccb69};
    const void *const folded[8] = {Member(&cRZMutex::Delete), nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, Member(&cRZMutex::GetHandle)};
    ++ctx.cases;
    if (mutex[0] - base != expect[0] || mutex[6] - base != expect[7]) ctx.Fail("cRZMutex's slots 0 / 6 are not the folded bodies");
    for (int i = 0; i < 8; ++i) {
        ++ctx.cases;
        if (image[i] - base != expect[i]) ctx.Fail("slot %d: the image's entry is %x", i, image[i] - base);
        if (folded[i]) {
            if (ts2_own_vt_e61b7c[i] != folded[i]) ctx.Fail("slot %d: not cRZMutex's member for the folded body", i);
            if (HookAt(expect[i])) ctx.Fail("slot %d: the folded body %x is hooked", i, expect[i]);
            continue;
        }
        const HookSpec *h = HookAt(expect[i]);
        if (!h) ctx.Fail("slot %d: %x is not hooked", i, expect[i]);
        else if (h->replacement != ts2_own_vt_e61b7c[i]) ctx.Fail("slot %d: ours is not what the hook of %x installs", i, expect[i]);
    }
}
