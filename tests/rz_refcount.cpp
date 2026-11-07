// Differential tests for the reference-counting base classes and RZAtomicAdd. Objects carry the
// original class vtables, so deletions (through the vtable) run the original deleting destructor;
// the deleting destructors themselves are tested directly.
#include "difftest.h"
#include "rz/cRZRefCount.h"

#include <windows.h>

#include <cstring>
#include <random>

namespace {

using Thiscall0 = uint32_t(__thiscall *)(void *);
using DeletingDtor = void *(__thiscall *)(void *, uint32_t);

template <typename T> T *Make(const difftest::Context &ctx, uint32_t refs)
{
    auto *p = static_cast<T *>(::operator new(sizeof(T)));
    p->mVTable = ctx.image.At(T::kVTable);
    p->mRefCount = refs;
    return p;
}

// Deletions are made observable: objects get a copy of their class vtable whose deleting-destructor
// slot records the call (and frees), so "released to zero but not deleted" shows up.
int gDeleted = 0;
void *__fastcall RecordingDelete(void *self, void * /*edx*/, uint32_t flags)
{
    ++gDeleted;
    if (flags & 1) ::operator delete(self);
    return self;
}

template <typename T> struct RecordingVTable {
    void *slots[8];
    RecordingVTable(const difftest::Context &ctx, int deleteSlot)
    {
        std::memcpy(slots, ctx.image.At(T::kVTable), sizeof(slots));
        slots[deleteSlot] = reinterpret_cast<void *>(&RecordingDelete);
    }
};

// Release (and RemoveRef where present) over random counts, including counts that delete.
template <typename T, typename Ours>
void ReleaseTest(difftest::Context &ctx, const char *name, uint32_t releaseRva, Ours ours, int deleteSlot, std::mt19937 &rng)
{
    auto orig = ctx.Original<Thiscall0>(releaseRva);
    RecordingVTable<T> vtable(ctx, deleteSlot);
    for (int i = 0; i < 20000; ++i) {
        uint32_t refs = rng() % 6;
        T *a = Make<T>(ctx, refs), *b = Make<T>(ctx, refs);
        a->mVTable = b->mVTable = vtable.slots;
        gDeleted = 0;
        uint32_t ra = orig(a);
        int deletedA = gDeleted;
        gDeleted = 0;
        uint32_t rb = (b->*ours)();
        int deletedB = gDeleted;
        ++ctx.cases;
        if (ra != rb || deletedA != deletedB) ctx.Fail("%s refs %u: original %u (deleted %d), ours %u (deleted %d)", name, refs, ra, deletedA, rb, deletedB);
        bool deleted = deletedA && deletedB;
        if (deletedA != deletedB) continue; // leak the survivor rather than double free
        if (!deleted) {
            if (static_cast<uint32_t>(a->mRefCount) != static_cast<uint32_t>(b->mRefCount)) ctx.Fail("%s refs %u: counts differ", name, refs);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

template <typename T> void DestructorTest(difftest::Context &ctx, const char *name, uint32_t dtorRva, uint32_t deletingRva)
{
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(dtorRva);
    auto deleting = ctx.Original<DeletingDtor>(deletingRva);
    for (uint32_t flags = 0; flags < 4; ++flags) {
        T *a = Make<T>(ctx, 3), *b = Make<T>(ctx, 3);
        a->mVTable = b->mVTable = nullptr;
        dtor(a);
        b->Destruct();
        ++ctx.cases;
        if (a->mVTable != b->mVTable) ctx.Fail("%s destructor vtable differs", name);
        a->mVTable = b->mVTable = nullptr;
        void *ra = deleting(a, flags);
        T *rb = b->DeletingDestructor(flags);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("%s deleting destructor return differs", name);
        if (!(flags & 1)) {
            if (a->mVTable != b->mVTable) ctx.Fail("%s deleting destructor vtable differs", name);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

} // namespace

// Slot 1 of cRZUnknownMT and cRZRefCountMT (0x1b914d, shared with one other class): atomically adds one
// and returns the new count. Counts at the edges (0, 0x7fffffff, -1) included.
DIFF_TEST(RZRefCount_AddRef, 0x1b914d)
{
    auto orig = ctx.Original<Thiscall0>(0x1b914d);
    const int32_t counts[] = {0, 1, 0x7fffffff, -1, -2};
    for (int32_t count : counts) {
        auto *a = Make<cRZUnknownMT>(ctx, static_cast<uint32_t>(count));
        auto *b = Make<cRZUnknownMT>(ctx, static_cast<uint32_t>(count));
        auto *c = Make<cRZRefCountMT>(ctx, static_cast<uint32_t>(count));
        auto *d = Make<cRZRefCountMT>(ctx, static_cast<uint32_t>(count));
        uint32_t ra = orig(a), rb = b->AddRef();
        uint32_t rd = orig(d), rc = c->AddRef();
        ++ctx.cases;
        if (ra != rb || a->mRefCount != b->mRefCount) ctx.Fail("cRZUnknownMT::AddRef(%d): %u vs %u", count, ra, rb);
        ++ctx.cases;
        if (rc != rd || c->mRefCount != d->mRefCount) ctx.Fail("cRZRefCountMT::AddRef(%d): %u vs %u", count, rc, rd);
        ::operator delete(a);
        ::operator delete(b);
        ::operator delete(c);
        ::operator delete(d);
    }
}

DIFF_TEST(RZRefCount_All, 0x1b94e9)
{
    std::mt19937 rng(3);
    ReleaseTest<cRZUnknown>(ctx, "cRZUnknown::Release", 0x1b94e9, &cRZUnknown::Release, 3, rng);
    ReleaseTest<cRZRefCount>(ctx, "cRZRefCount::Release", 0x1b94af, &cRZRefCount::Release, 0, rng);
    ReleaseTest<cRZUnknownMT>(ctx, "cRZUnknownMT::Release", 0x1b9517, &cRZUnknownMT::Release, 3, rng);
    ReleaseTest<cRZUnknownMT>(ctx, "cRZUnknownMT::RemoveRef", 0x1b9544, &cRZUnknownMT::RemoveRef, 3, rng);
    ReleaseTest<cRZRefCountMT>(ctx, "cRZRefCountMT::Release", 0x1b94c5, &cRZRefCountMT::Release, 0, rng);

    DestructorTest<cRZUnknown>(ctx, "cRZUnknown", 0x1b9096, 0x1b90f0);
    DestructorTest<cRZRefCount>(ctx, "cRZRefCount", 0x1b9088, 0x1b90b2);
    DestructorTest<cRZUnknownMT>(ctx, "cRZUnknownMT", 0x1b90ab, 0x1b912e);
    DestructorTest<cRZRefCountMT>(ctx, "cRZRefCountMT", 0x1b908f, 0x1b90d1);

    auto add = ctx.Original<int32_t(__cdecl *)(volatile int32_t *, int32_t)>(0x1bdbad);
    for (int i = 0; i < 20000; ++i) {
        volatile int32_t a = static_cast<int32_t>(rng()), b = a;
        int32_t d = static_cast<int32_t>(rng() % 7) - 3;
        int32_t ra = add(&a, d), rb = RZAtomicAdd(&b, d);
        ++ctx.cases;
        if (ra != rb || a != b) ctx.Fail("RZAtomicAdd(%d): %d vs %d", d, ra, rb);
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
