// nGZEvent::cVectorEvent<cVector<4, float>> (lane FD; src/gz/cVector4fEvent.h). Every slot of the image's table run
// against ours on random events (the folded ones too: ours are only reached through our table), compared as the bytes of
// the target and the result; the owned cells are the image's in a hooked build; our tables for image-free programs hold
// our members in the image's order.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h" // difftest::AllocationCounter
#include "gz/cVector4fEvent.h"

#include <cstring>
#include <random>

namespace {

using nGZEvent::cVector4fEvent;

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

struct Raw {
    uint32_t word[5];
};

Raw RandomEvent(std::mt19937 &gen, const void *table)
{
    Raw r{};
    r.word[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));
    for (int i = 1; i < 5; ++i) r.word[i] = gen();
    return r;
}

const void *SlotOf(const OriginalImage &image, int slot)
{
    return reinterpret_cast<const void *const *>(image.At(0xdb07d4))[slot];
}

} // namespace

DIFF_TEST(cVector4fEvent_Cells, 0xdb07d4)
{
    ++ctx.cases;
    if (&ts2_rva_db07d4 != ctx.image.At(0xdb07d4)) ctx.Fail("ts2_rva_db07d4 %p, the image's %p", &ts2_rva_db07d4, ctx.image.At(0xdb07d4));
    ++ctx.cases;
    if (&ts2_rva_d9ca58 != ctx.image.At(0xd9ca58)) ctx.Fail("ts2_rva_d9ca58 %p, the image's %p", &ts2_rva_d9ca58, ctx.image.At(0xd9ca58));
    // The image's table holds the slots the hooks and our table name.
    const uint32_t rvas[7] = {0x154eb0, 0x258ca0, 0x258ca0, 0x269ba0, 0x1559e0, 0x1561d0, 0x25b130};
    for (int i = 0; i < 7; ++i) {
        ++ctx.cases;
        if (SlotOf(ctx.image, i) != ctx.image.At(rvas[i])) ctx.Fail("image slot %d is not 0x%x", i, rvas[i]);
    }
    // Our tables: the image's order, each slot ours.
    const void *expected[7] = {Member(&cVector4fEvent::DeletingDestructor), Member(&cVector4fEvent::Assign),
                               Member(&cVector4fEvent::Assign),             Member(&cVector4fEvent::GetEventType),
                               Member(&cVector4fEvent::GetSize),            Member(&cVector4fEvent::GetBodySize),
                               Member(&cVector4fEvent::CloneInto)};
    for (int i = 0; i < 7; ++i) {
        ++ctx.cases;
        if (ts2_own_vt_db07d4[i] != expected[i]) ctx.Fail("own table slot %d: %p, expected %p", i, ts2_own_vt_db07d4[i], expected[i]);
    }
    ++ctx.cases;
    if (ts2_own_vt_d9ca58[0] != Member(&nGZEvent::cIEventHeader::DeletingDestructor)) ctx.Fail("own header table slot 0");
    for (int i = 1; i < 7; ++i) {
        ++ctx.cases;
        if (ts2_own_vt_d9ca58[i] != ts2_own_vt_d9ca58[1] || !ts2_own_vt_d9ca58[i]) ctx.Fail("own header table slot %d", i);
    }
    // The constructor stores the image's table (the word the game's code stores).
    const cVector4fEvent event(1.0f, 2.0f, 3.0f, 4.0f);
    ++ctx.cases;
    if (static_cast<const void *>(event.mVTable) != ctx.image.At(0xdb07d4)) ctx.Fail("constructor stores %p", event.mVTable);
}

// Slot 0 (folded 0x154eb0) with flags 0: the header's table left, nothing freed, this returned.
DIFF_TEST(cVector4fEvent_DeletingDestructor, 0x154eb0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x154eb0);
    std::mt19937 gen(0x154eb0);
    for (int i = 0; i < 64; ++i) {
        Raw a = RandomEvent(gen, ctx.image.At(0xdb07d4)), b = a;
        const uint32_t flags = gen() & ~1u;
        void *ra = orig(&a, flags);
        void *rb = reinterpret_cast<cVector4fEvent *>(&b)->DeletingDestructor(flags);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || ra != &a || rb != &b)
            ctx.Fail("flags 0x%x: original leaves %08x (returns %p), ours %08x (returns %p)", flags, a.word[0], ra, b.word[0], rb);
    }
}

// Slot 0 with flags & 1: both sides delete the 0x14-byte block once, sized (shape::DeleteRecorder: the original
// through the game's delete 0x824624, ours through ::operator delete, never the game's), and return it. The original
// leaves the header's table word first.
DIFF_TEST(cVector4fEvent_DeletingDestructorFree, 0x154eb0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x154eb0);
    shape::DeleteRecorder deletes;
    std::mt19937 gen(0x154eb1);
    for (int i = 0; i < 32; ++i) {
        const Raw seed = RandomEvent(gen, ctx.image.At(0xdb07d4));
        const uint32_t flags = gen() | 1u;
        void *const ha = ::operator new(sizeof(Raw));
        std::memcpy(ha, &seed, sizeof(Raw));
        deletes.Begin();
        void *const ra = orig(ha, flags);
        deletes.End();
        const int na = deletes.Count();
        const bool freedA = na == 1 && deletes.Is(0, ha, sizeof(Raw));
        Raw after{};
        std::memcpy(&after, ha, sizeof(Raw)); // still kept by the recorder
        deletes.Release();
        difftest::AllocationCounter counter; // ours allocates nothing it keeps
        void *const hb = ::operator new(sizeof(Raw));
        std::memcpy(hb, &seed, sizeof(Raw));
        deletes.Begin();
        void *const rb = static_cast<cVector4fEvent *>(hb)->DeletingDestructor(flags);
        deletes.End();
        const int nb = deletes.Count(), nbImage = deletes.FromImage();
        const bool freedB = nb == 1 && deletes.Is(0, hb, sizeof(Raw));
        deletes.Release();
        counter.Stop();
        if (counter.Blocks() != 0 || counter.Bytes() != 0)
            ctx.Fail("flags 0x%x: ours left %lld blocks (%lld bytes) allocated", flags, counter.Blocks(), counter.Bytes());
        ++ctx.cases;
        if (!freedA || !freedB) ctx.Fail("flags 0x%x: %d / %d deletes (each should free its 0x14-byte block)", flags, na, nb);
        if (nbImage != 0) ctx.Fail("flags 0x%x: ours reached the game's delete", flags);
        if (ra != ha || rb != hb) ctx.Fail("flags 0x%x: returns %p/%p, expected %p/%p", flags, ra, rb, ha, hb);
        if (after.word[0] != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0xd9ca58))))
            ctx.Fail("flags 0x%x: the original left %08x", flags, after.word[0]);
    }
}

// Slots 1 and 2 (0x258ca0): the four floats copied; the target's table kept.
DIFF_TEST(cVector4fEvent_Assign, 0x258ca0)
{
    using Fn = void(__thiscall *)(void *, const void *);
    auto orig = ctx.Original<Fn>(0x258ca0);
    std::mt19937 gen(0x258ca0);
    for (int i = 0; i < 64; ++i) {
        Raw a = RandomEvent(gen, reinterpret_cast<const void *>(static_cast<uintptr_t>(gen()))), b = a;
        const Raw source = RandomEvent(gen, ctx.image.At(0xdb07d4));
        orig(&a, &source);
        cVector4fEvent *const r = reinterpret_cast<cVector4fEvent *>(&b)->Assign(reinterpret_cast<const cVector4fEvent *>(&source));
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || r != reinterpret_cast<cVector4fEvent *>(&b))
            ctx.Fail("case %d: targets differ (word 1 %08x vs %08x)", i, a.word[1], b.word[1]);
    }
}

// Slots 3, 4, 5 (0x269ba0; folded 0x1559e0, 0x1561d0): constants.
DIFF_TEST(cVector4fEvent_Constants, 0x269ba0)
{
    using Fn = uint32_t(__thiscall *)(const void *);
    std::mt19937 gen(0x269ba0);
    const Raw e = RandomEvent(gen, ctx.image.At(0xdb07d4));
    const auto *const ours = reinterpret_cast<const cVector4fEvent *>(&e);
    const uint32_t original[3] = {ctx.Original<Fn>(0x269ba0)(&e), ctx.Original<Fn>(0x1559e0)(&e), ctx.Original<Fn>(0x1561d0)(&e)};
    const uint32_t mine[3] = {ours->GetEventType(), ours->GetSize(), ours->GetBodySize()};
    for (int i = 0; i < 3; ++i) {
        ++ctx.cases;
        if (original[i] != mine[i]) ctx.Fail("slot %d: original 0x%x, ours 0x%x", i + 3, original[i], mine[i]);
    }
}

// Slot 6 (0x25b130): a copy built in random memory, the image's table stored.
DIFF_TEST(cVector4fEvent_CloneInto, 0x25b130)
{
    using Fn = void(__thiscall *)(const void *, void *);
    auto orig = ctx.Original<Fn>(0x25b130);
    std::mt19937 gen(0x25b130);
    for (int i = 0; i < 64; ++i) {
        const Raw source = RandomEvent(gen, ctx.image.At(0xdb07d4));
        uint32_t a[7], b[7];
        for (int k = 0; k < 7; ++k) a[k] = b[k] = gen();
        orig(&source, &a[1]);
        cVector4fEvent *const r = reinterpret_cast<const cVector4fEvent *>(&source)->CloneInto(&b[1]);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || r != reinterpret_cast<cVector4fEvent *>(&b[1]))
            ctx.Fail("case %d: copies differ (table %08x vs %08x)", i, a[1], b[1]);
    }
}
