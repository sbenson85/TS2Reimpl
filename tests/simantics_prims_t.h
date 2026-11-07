#pragma once
// Shared pieces of the lane T primitive tests (src/simantics/PrimT.h): the recording-fake framework of
// tests/simantics_prims_mid.h plus the queue-driven operand reader of tests/simantics_prims_r.h.
#include "simantics_prims_r.h"
#include "simantics/PrimT.h"

#include <climits>

namespace {

PrimTObject *SelfT(PrimMidObject *o)
{
    return reinterpret_cast<PrimTObject *>(o);
}

// The allocator service the game caches (global 0x10c95d4, guard 0x10c95d8): a fake whose allocate (slot 3) and free
// (slot 4) go to the process heap, so the original's std-style allocations and ours (AllocateViaAllocatorService,
// FreeThroughAllocatorService) share one source. Installed for the life of the object; the old values are restored.
void *__fastcall FakeAlloc3(void *, void *, uint32_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void __fastcall FakeFree4(void *, void *, void *mem)
{
    HeapFree(GetProcessHeap(), 0, mem);
}

struct FakeAllocatorService {
    void *mVTable[8];
    void *mObject;
    void **mCache;
    int *mGuard;
    void *mSavedCache;
    int mSavedGuard;
    explicit FakeAllocatorService(difftest::Context &ctx)
    {
        for (void *&v : mVTable) v = nullptr;
        mVTable[3] = reinterpret_cast<void *>(&FakeAlloc3);
        mVTable[4] = reinterpret_cast<void *>(&FakeFree4);
        mObject = mVTable;
        mCache = ctx.image.At<void *>(0x10c95d4);
        mGuard = ctx.image.At<int>(0x10c95d8);
        mSavedCache = *mCache;
        mSavedGuard = *mGuard;
        *mCache = &mObject;
        *mGuard = INT_MIN;
    }
    FakeAllocatorService(const FakeAllocatorService &) = delete;
    ~FakeAllocatorService()
    {
        *mCache = mSavedCache;
        *mGuard = mSavedGuard;
    }
};

} // namespace
