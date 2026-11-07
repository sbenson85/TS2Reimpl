// Differential test for the device state commands' own vtables (lane VU, src/gz/RenderStates.cpp): cStateHeader 0xd9c594,
// cClippingState 0xd9c5e8, cDepthRangeState 0xd9c5dc, cRenderTargetsState 0xd9ff54 and cViewportState 0xd9c5d0, what an
// image-free program stores in place of the image's tables (standalone/owned_statics.cpp aliases the owned statics to
// them).
//
// Slot for slot, as tests/sg_lod_levels_own_tables.cpp does: every image entry that is hooked is ours exactly when ours is
// what that hook installs (slot 0 the folded deleting destructors 0x14c870 / 0x14c620 / 0x14c650 / 0x186480 / 0x14c900;
// slot 1 CloneInto 0x14e920 / 0x14e9b0 / 0x1864b0 / 0x14ea10); cStateHeader's slot 1 is the image's _purecall, ours our
// CRT's. Then dispatch on whole tables: a random state with the image's table through the image's slots, the same state
// with our table through ours, CloneInto's destinations and the deleting destructor's result, frees and surviving bytes
// compared. The owned statics themselves resolve to the image's tables in the hooked build: checked too.
#include "difftest.h"
#include "free_recorder.h"

#include "gz/RenderStates.h"
#include "hook/hooks.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <random>

extern "C" const void *ts2_own_vt_d9c594[2];
extern "C" const void *ts2_own_vt_d9c5e8[2];
extern "C" const void *ts2_own_vt_d9c5dc[2];
extern "C" const void *ts2_own_vt_d9ff54[2];
extern "C" const void *ts2_own_vt_d9c5d0[2];
extern "C" int __cdecl _purecall();

using namespace nGZGraphic4;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

// Slot for slot against the hooks; `pureSlot1` for cStateHeader (the image's _purecall, ours the CRT's).
void CheckSlots(difftest::Context &ctx, uint32_t vtable, const void *const *ours, const void *owned, bool pureSlot1)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(vtable);
    ++ctx.cases;
    if (owned != image) ctx.Fail("0x%x: the owned static is %p, not the image's table %p", vtable, owned, image);
    for (size_t i = 0; i < 2; ++i) {
        ++ctx.cases;
        if (i == 1 && pureSlot1) {
            if (ours[i] != reinterpret_cast<const void *>(&_purecall)) ctx.Fail("0x%x slot 1: not _purecall", vtable);
            continue;
        }
        const HookSpec *h = HookAt(image[i] - base);
        if (!h) ctx.Fail("0x%x slot %zu: the image's entry %x is not hooked", vtable, i, image[i] - base);
        else if (h->replacement != ours[i])
            ctx.Fail("0x%x slot %zu: ours is %p, the hook of %x installs %p", vtable, i, ours[i], image[i] - base,
                     h->replacement);
    }
}

void Fill(std::mt19937 &rng, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(rng());
}

// Dispatch on whole tables: CloneInto (slot 1) and the deleting destructor (slot 0, flags 0, 1 and random) through the
// image's table on one side and ours on the other, on the same random state (its vtable word set to the table used).
template <class State> void CheckDispatch(difftest::Context &ctx, const void *const *ours, uint32_t seed)
{
    constexpr size_t n = sizeof(State);
    constexpr size_t kSlack = 16;
    const void *const *image = GameAddress<const void *const>(State::kVTable);
    const void *const *tables[2] = {image, ours};
    std::mt19937 rng(seed);
    using Clone = void(__thiscall *)(const void *, void *);
    using Delete = void *(__thiscall *)(void *, uint32_t);
    for (int i = 0; i < 2000; ++i) {
        alignas(16) uint8_t src[2][n], dst[2][n + kSlack];
        Fill(rng, src[0], n);
        Fill(rng, dst[0], n + kSlack);
        std::memcpy(dst[1], dst[0], n + kSlack);
        for (int side = 0; side < 2; ++side) {
            std::memcpy(src[side], src[0], n);
            std::memcpy(src[side], &tables[side], 4);
            reinterpret_cast<Clone>(tables[side][1])(src[side], dst[side]);
        }
        ++ctx.cases;
        if (std::memcmp(dst[0], dst[1], n + kSlack) != 0) {
            ctx.Fail("0x%x case %d: CloneInto through the tables differs", State::kVTable, i);
            return;
        }
    }
    for (int i = 0; i < 400; ++i) {
        const uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : static_cast<uint32_t>(rng());
        uint8_t bytes[n];
        Fill(rng, bytes, n);
        uint8_t *blocks[2];
        void *results[2];
        bool freed[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            blocks[side] = static_cast<uint8_t *>(::operator new(n));
            std::memcpy(blocks[side], bytes, n);
            std::memcpy(blocks[side], &tables[side], 4);
            difftest::FreeRecorder rec;
            results[side] = reinterpret_cast<Delete>(tables[side][0])(blocks[side], flags);
            rec.Stop();
            freed[side] = rec.Freed(blocks[side]);
            frees[side] = rec.Count();
        }
        ++ctx.cases;
        bool same = results[0] == blocks[0] && results[1] == blocks[1] && freed[0] == freed[1] && frees[0] == frees[1];
        if (same && !freed[0]) same = std::memcmp(blocks[0], blocks[1], n) == 0;
        for (int side = 0; side < 2; ++side)
            if (!freed[side]) ::operator delete(blocks[side]);
        if (!same) {
            ctx.Fail("0x%x flags 0x%x: the deleting destructors through the tables differ", State::kVTable, flags);
            return;
        }
    }
}

} // namespace

DIFF_TEST(RenderStates_OwnTable_cStateHeader, 0xd9c594)
{
    CheckSlots(ctx, cStateHeader::kVTable, ts2_own_vt_d9c594, cStateHeader::VTable(), true);
    // Slot 0 only (slot 1 is pure): the destructor through each table, flags 0 (the block survives) and 1.
    using Delete = void *(__thiscall *)(void *, uint32_t);
    const void *const *image = GameAddress<const void *const>(cStateHeader::kVTable);
    for (uint32_t flags = 0; flags < 2; ++flags) {
        uint8_t *blocks[2];
        bool freed[2];
        void *results[2];
        for (int side = 0; side < 2; ++side) {
            blocks[side] = static_cast<uint8_t *>(::operator new(8));
            std::memset(blocks[side], 0x5a, 8);
            difftest::FreeRecorder rec;
            results[side] = reinterpret_cast<Delete>((side ? ts2_own_vt_d9c594 : image)[0])(blocks[side], flags);
            rec.Stop();
            freed[side] = rec.Freed(blocks[side]);
        }
        ++ctx.cases;
        if (results[0] != blocks[0] || results[1] != blocks[1] || freed[0] != freed[1] ||
            (!freed[0] && std::memcmp(blocks[0], blocks[1], 8) != 0))
            ctx.Fail("cStateHeader flags %u: the deleting destructors through the tables differ", flags);
        for (int side = 0; side < 2; ++side)
            if (!freed[side]) ::operator delete(blocks[side]);
    }
}

DIFF_TEST(RenderStates_OwnTable_cClippingState, 0xd9c5e8)
{
    CheckSlots(ctx, cClippingState::kVTable, ts2_own_vt_d9c5e8, cClippingState::VTable(), false);
    CheckDispatch<cClippingState>(ctx, ts2_own_vt_d9c5e8, 1);
}

DIFF_TEST(RenderStates_OwnTable_cDepthRangeState, 0xd9c5dc)
{
    CheckSlots(ctx, cDepthRangeState::kVTable, ts2_own_vt_d9c5dc, cDepthRangeState::VTable(), false);
    CheckDispatch<cDepthRangeState>(ctx, ts2_own_vt_d9c5dc, 2);
}

DIFF_TEST(RenderStates_OwnTable_cRenderTargetsState, 0xd9ff54)
{
    CheckSlots(ctx, cRenderTargetsState::kVTable, ts2_own_vt_d9ff54, cRenderTargetsState::VTable(), false);
    CheckDispatch<cRenderTargetsState>(ctx, ts2_own_vt_d9ff54, 3);
}

DIFF_TEST(RenderStates_OwnTable_cViewportState, 0xd9c5d0)
{
    CheckSlots(ctx, cViewportState::kVTable, ts2_own_vt_d9c5d0, cViewportState::VTable(), false);
    CheckDispatch<cViewportState>(ctx, ts2_own_vt_d9c5d0, 4);
}
