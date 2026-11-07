#pragma once
// A private heap for the game image's process-heap calls (the frame clock's heap=private, host/control_frameclock.cpp;
// tests: tests/host_privateheap.cpp). Win32 only, like the host.
//
// Why: Windows's low-fragmentation heap (LFH), which the process heap uses for small blocks, picks a random free slot
// for each allocation, so the same allocations land in a different address order on every run. The game orders some
// containers by address (the neighbourhood's draw order: the same objects, drawn in a different order), so frames
// differ with every clock and input equal. A heap of fixed size, or one created with HEAP_NO_SERIALIZE, never gets the
// LFH (both are used: the fixed one has both properties, so dropping either flag alone changes nothing): its blocks come
// from the back-end allocator, whose placement depends only on the sequence of calls, as long as the heap doesn't grow:
// a growable heap's new segments go wherever VirtualAlloc finds room, which depends on every other allocation in the
// process (Direct3D's, the host's threads'). So the main heap is fixed: its whole range is reserved at the start
// (`reserve` bytes, committed as used). Blocks of kLargeBlock bytes and up (which a fixed heap refuses), and anything
// once the fixed heap is full, come from a growable overflow heap; their addresses are not reproducible.
// The class serialises the calls itself (an SRW lock: the game's threads run one at a time under the lockstep, but
// threads outside it may allocate).
//
// The image's only heap imports are GetProcessHeap, HeapAlloc, HeapFree, HeapReAlloc and HeapSize, and it gets its one
// heap handle from GetProcessHeap. So: an allocation from the process heap comes from the private heaps; a free, resize
// or size of a block they own goes to them, and anything else (a block allocated before the switch, or by a system DLL
// and handed over) to the real call. Other heaps pass through.
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>

namespace privateheap {

constexpr SIZE_T kLargeBlock = 0x7f000; // a fixed heap's largest block is a little under 512 KB

struct Stats {
    uint64_t allocs = 0, frees = 0, reallocs = 0; // calls the private heaps served
    uint64_t overflow = 0;                        // allocations from the overflow heap (large, or the fixed heap full)
    uint64_t foreign = 0;                         // calls passed to the real heap for its own block
};

class Heap {
public:
    // Creates the heaps (HEAP_NO_SERIALIZE): the fixed one with `reserve` bytes, the growable overflow one. `redirect`
    // is the heap whose allocations they take over (the process heap in the host; any heap in tests).
    bool Create(HANDLE redirect, SIZE_T reserve);
    void Destroy();
    HANDLE Handle() const { return mFixed; }

    // Which private heap the block came from: 1 the fixed one, 2 the overflow one, 0 neither (null: 0).
    int Owner(const void *block);

    void *Alloc(HANDLE heap, DWORD flags, SIZE_T bytes);
    BOOL Free(HANDLE heap, DWORD flags, void *block);
    void *ReAlloc(HANDLE heap, DWORD flags, void *block, SIZE_T bytes);
    SIZE_T Size(HANDLE heap, DWORD flags, const void *block);

    Stats GetStats() const;

private:
    bool Ours(HANDLE heap) const { return heap == mRedirect || heap == mFixed || heap == mOverflow; }
    int OwnerLocked(const void *block);
    void *AllocLocked(DWORD flags, SIZE_T bytes);
    HANDLE HeapOf(int owner) const { return owner == 1 ? mFixed : mOverflow; }

    HANDLE mFixed = nullptr, mOverflow = nullptr, mRedirect = nullptr;
    uintptr_t mFixedBegin = 0, mFixedEnd = 0;
    SRWLOCK mLock = SRWLOCK_INIT;
    std::atomic<uint64_t> mAllocs{0}, mFrees{0}, mReallocs{0}, mOverflowAllocs{0}, mForeign{0};
};

} // namespace privateheap
