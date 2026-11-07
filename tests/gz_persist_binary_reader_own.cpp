// Differential tests for cGZPersistBinaryReader's constructor (0x9bf9a0) and its own vtable (lane QI, milestone N-d):
// the constructor against ours, and slot 2 (the Release, folded 0x178b40 across four classes: never hooked) called
// through OwnVTable() against the original body, with the deleting destructor (slot 29) replaced by a recorder.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZPersistBinaryReader.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>

namespace {

using Reader = cGZPersistBinaryReader;

struct Call {
    void *self;
    uint32_t flags;
    int calls;
} gCall;

void *__fastcall DeleteRecorder(void *self, void *, uint32_t flags)
{
    gCall.self = self;
    gCall.flags = flags;
    ++gCall.calls;
    return self;
}

} // namespace

DIFF_TEST(cGZPersistBinaryReader_Constructor, 0x9bf9a0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x9bf9a0);
    for (int i = 0; i < 200; ++i) {
        uint32_t words[2][10];
        long long blocks[2], bytes[2];
        for (int side = 0; side < 2; ++side) {
            auto *r = static_cast<uint32_t *>(::operator new(sizeof(Reader)));
            std::mt19937 fill(0x1000 + i);
            for (int k = 0; k < 10; ++k) r[k] = fill();
            void *result;
            difftest::AllocationCounter heap;
            result = side == 0 ? orig(r) : static_cast<Reader *>(static_cast<void *>(r))->Construct();
            heap.Stop();
            blocks[side] = heap.Blocks();
            bytes[side] = heap.Bytes();
            ++ctx.cases;
            if (result != r) ctx.Fail("returns this: %p vs %p", result, static_cast<void *>(r));
            // The ring's node is a pointer value: normalise it (a heap block whose two words are &ring[0] and 0).
            const uint32_t *node = reinterpret_cast<const uint32_t *>(uintptr_t(r[2]));
            if (node == nullptr || node[0] != uint32_t(uintptr_t(r + 2)) || node[1] != 0)
                ctx.Fail("side %d: ring node %p", side, static_cast<const void *>(node));
            std::memcpy(words[side], r, sizeof(words[side]));
            words[side][2] = 0;
            std::destroy_at(&static_cast<Reader *>(static_cast<void *>(r))->Ring());
            ::operator delete(r);
        }
        ++ctx.cases;
        if (std::memcmp(words[0], words[1], sizeof(words[0])) != 0) {
            ctx.Fail("object words differ (case %d)", i);
            for (int k = 0; k < 10; ++k) ctx.Fail("  [%d] %08x vs %08x", k, words[0][k], words[1][k]);
            break;
        }
        if (words[0][0] != uint32_t(uintptr_t(ctx.image.At(Reader::kVTable)))) ctx.Fail("original's vtable word %08x", words[0][0]);
        if (blocks[0] != blocks[1] || bytes[0] != bytes[1]) ctx.Fail("heap: %lld/%lld vs %lld/%lld", blocks[0], bytes[0], blocks[1], bytes[1]);
    }
}

// Slot 2 on both sides: the count drops; at zero the deleting destructor (slot 29) is called once with 1 and the
// result is 0; otherwise the new count comes back and nothing is called.
DIFF_TEST(cGZPersistBinaryReader_Release, 0x178b40)
{
    const void *const *ours = static_cast<const void *const *>(Reader::OwnVTable());
    for (int i = 0; i < 30; ++i) {
        ++ctx.cases;
        if (reinterpret_cast<uintptr_t>(ours[i]) - ctx.image.Base() < ctx.image.Size()) ctx.Fail("our vtable slot %d points into the image", i);
    }
    // Slot 29 is the recorder on both sides.
    const void *origSlots[30], *ourSlots[30];
    std::memcpy(origSlots, ctx.image.At(Reader::kVTable), sizeof(origSlots));
    std::memcpy(ourSlots, ours, sizeof(ourSlots));
    origSlots[29] = ourSlots[29] = reinterpret_cast<const void *>(&DeleteRecorder);
    auto release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x178b40);
    const uint32_t counts[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    std::mt19937 rng(0x178b);
    for (int i = 0; i < 2000; ++i) {
        const uint32_t count = i < 8 ? counts[i] : (i % 3 == 0 ? 1 + rng() % 3 : uint32_t(rng()));
        uint32_t a[3] = {uint32_t(uintptr_t(origSlots)), count, 0x55555555}, b[3] = {uint32_t(uintptr_t(ourSlots)), count, 0x55555555};
        gCall = {};
        const uint32_t ra = release(a);
        const Call ca = gCall;
        gCall = {};
        const uint32_t rb = reinterpret_cast<uint32_t(__thiscall *)(void *)>(ours[2])(b);
        const Call cb = gCall;
        ++ctx.cases;
        if (ra != rb || a[1] != b[1] || a[2] != b[2] || ca.calls != cb.calls ||
            (ca.calls && (ca.flags != cb.flags || ca.self != static_cast<void *>(a) || cb.self != static_cast<void *>(b))))
            ctx.Fail("count %u: ret %u/%u, left %u/%u, calls %d/%d flags %u/%u", count, ra, rb, a[1], b[1], ca.calls, cb.calls, ca.flags, cb.flags);
    }
}

// The three small shared bodies the reader's vtable also holds, called through OwnVTable() against the originals:
// slot 1 (0x148fb0, the folded AddRef: ++count at +4), slot 27 (0x147d00, returns the stream at +0x1c) and slot 28
// (0x15e970, returns the group at +0x24).
namespace {

void CompareSlot(difftest::Context &ctx, int slot, uint32_t rva, int field)
{
    const void *const *ours = static_cast<const void *const *>(Reader::OwnVTable());
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(rva);
    auto mine = reinterpret_cast<uint32_t(__thiscall *)(void *)>(ours[slot]);
    std::mt19937 rng(rva);
    for (int i = 0; i < 500; ++i) {
        uint32_t a[10], b[10];
        for (int k = 0; k < 10; ++k) a[k] = b[k] = rng();
        if (i < 4) a[1] = b[1] = (i & 1) ? 0xffffffffu : 0x7fffffffu + (i >> 1);
        const uint32_t ra = orig(a), rb = mine(b);
        ++ctx.cases;
        if (ra != rb || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("slot %d (field %d): ret %08x/%08x", slot, field, ra, rb);
    }
}

} // namespace

DIFF_TEST(cGZPersistBinaryReader_AddRefSlot, 0x148fb0) { CompareSlot(ctx, 1, 0x148fb0, 1); }
DIFF_TEST(cGZPersistBinaryReader_GetStreamSlot, 0x147d00) { CompareSlot(ctx, 27, 0x147d00, 7); }
DIFF_TEST(cGZPersistBinaryReader_GetGroupSlot, 0x15e970) { CompareSlot(ctx, 28, 0x15e970, 9); }
