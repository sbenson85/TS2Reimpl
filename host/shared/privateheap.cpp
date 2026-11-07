// The private heap for the image's process-heap calls: the rules are in privateheap.h.
#include "privateheap.h"

#include <cstring>

namespace privateheap {

namespace {

struct Exclusive {
    explicit Exclusive(SRWLOCK &lock) : mLock(lock) { AcquireSRWLockExclusive(&mLock); }
    ~Exclusive() { ReleaseSRWLockExclusive(&mLock); }
    SRWLOCK &mLock;
};

constexpr DWORD kNoSerialize = HEAP_NO_SERIALIZE;

} // namespace

bool Heap::Create(HANDLE redirect, SIZE_T reserve)
{
    mRedirect = redirect;
    mFixed = HeapCreate(kNoSerialize, 0, reserve);
    mOverflow = HeapCreate(kNoSerialize, 0, 0);
    if (!mFixed || !mOverflow) {
        Destroy();
        return false;
    }
    // The fixed heap's range: its whole reservation, which starts at the handle.
    mFixedBegin = mFixedEnd = reinterpret_cast<uintptr_t>(mFixed);
    MEMORY_BASIC_INFORMATION mbi{};
    while (VirtualQuery(reinterpret_cast<void *>(mFixedEnd), &mbi, sizeof mbi) &&
           reinterpret_cast<uintptr_t>(mbi.AllocationBase) == mFixedBegin && mbi.RegionSize)
        mFixedEnd += mbi.RegionSize;
    return mFixedEnd > mFixedBegin;
}

void Heap::Destroy()
{
    if (mFixed) HeapDestroy(mFixed);
    if (mOverflow) HeapDestroy(mOverflow);
    mFixed = mOverflow = nullptr;
    mFixedBegin = mFixedEnd = 0;
}

int Heap::OwnerLocked(const void *block)
{
    if (!block) return 0;
    const auto at = reinterpret_cast<uintptr_t>(block);
    if (at >= mFixedBegin && at < mFixedEnd) return 1;
    // HeapValidate with a block checks that block only, and fails for an address outside the heap's segments and large
    // blocks (never null: that would validate the whole heap).
    return HeapValidate(mOverflow, kNoSerialize, block) ? 2 : 0;
}

int Heap::Owner(const void *block)
{
    const Exclusive lock(mLock);
    return OwnerLocked(block);
}

void *Heap::AllocLocked(DWORD flags, SIZE_T bytes)
{
    if (bytes < kLargeBlock) {
        // Full: no exception from the fixed heap, the overflow one answers.
        if (void *p = HeapAlloc(mFixed, (flags & ~HEAP_GENERATE_EXCEPTIONS) | kNoSerialize, bytes)) return p;
    }
    ++mOverflowAllocs;
    return HeapAlloc(mOverflow, flags | kNoSerialize, bytes);
}

void *Heap::Alloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    if (!Ours(heap)) return HeapAlloc(heap, flags, bytes);
    const Exclusive lock(mLock);
    ++mAllocs;
    return AllocLocked(flags, bytes);
}

BOOL Heap::Free(HANDLE heap, DWORD flags, void *block)
{
    if (!Ours(heap)) return HeapFree(heap, flags, block);
    {
        const Exclusive lock(mLock);
        if (const int owner = OwnerLocked(block)) {
            ++mFrees;
            return HeapFree(HeapOf(owner), flags | kNoSerialize, block);
        }
    }
    if (block) ++mForeign;
    return HeapFree(mRedirect, flags, block);
}

void *Heap::ReAlloc(HANDLE heap, DWORD flags, void *block, SIZE_T bytes)
{
    if (!Ours(heap)) return HeapReAlloc(heap, flags, block, bytes);
    {
        const Exclusive lock(mLock);
        const int owner = OwnerLocked(block);
        if (owner == 2) {
            ++mReallocs;
            return HeapReAlloc(mOverflow, flags | kNoSerialize, block, bytes); // stays in the overflow heap
        }
        if (owner == 1) {
            ++mReallocs;
            if (bytes < kLargeBlock) {
                if (void *p = HeapReAlloc(mFixed, (flags & ~HEAP_GENERATE_EXCEPTIONS) | kNoSerialize, block, bytes)) return p;
            }
            if (flags & HEAP_REALLOC_IN_PLACE_ONLY) return nullptr;
            // Too large for the fixed heap, or it is full: a new block in the overflow heap, the contents copied.
            const SIZE_T old = HeapSize(mFixed, kNoSerialize, block);
            ++mOverflowAllocs;
            void *p = HeapAlloc(mOverflow, flags | kNoSerialize, bytes); // HEAP_ZERO_MEMORY zeroes what isn't copied
            if (!p) return nullptr;
            std::memcpy(p, block, old < bytes ? old : bytes);
            HeapFree(mFixed, kNoSerialize, block);
            return p;
        }
    }
    ++mForeign;
    return HeapReAlloc(mRedirect, flags, block, bytes);
}

SIZE_T Heap::Size(HANDLE heap, DWORD flags, const void *block)
{
    if (!Ours(heap)) return HeapSize(heap, flags, block);
    {
        const Exclusive lock(mLock);
        if (const int owner = OwnerLocked(block)) return HeapSize(HeapOf(owner), flags | kNoSerialize, block);
    }
    ++mForeign;
    return HeapSize(mRedirect, flags, block);
}

Stats Heap::GetStats() const
{
    Stats s;
    s.allocs = mAllocs.load();
    s.frees = mFrees.load();
    s.reallocs = mReallocs.load();
    s.overflow = mOverflowAllocs.load();
    s.foreign = mForeign.load();
    return s;
}

} // namespace privateheap
