// The frame clock's private heap (host/shared/privateheap.h): which calls it takes, which it passes on, the overflow
// heap, and that the same calls give the same layout in two heaps (no low-fragmentation heap with its random slots, no
// growth into wherever VirtualAlloc finds room). Host-only code: no original function involved.
#include "difftest.h"

#include "../host/shared/privateheap.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using privateheap::Heap;

// A fixed sequence of allocations, frees and resizes through `h` (redirecting `from`): the newest live block's offset
// from the first block after each call (0: nothing live).
std::vector<intptr_t> Layout(Heap &h, HANDLE from)
{
    std::vector<void *> live;
    std::vector<intptr_t> offsets;
    uint32_t x = 12345;
    auto next = [&x] { return x = x * 1103515245u + 12345u, x >> 8; };
    for (int i = 0; i < 6000; ++i) {
        const uint32_t r = next();
        if (!live.empty() && r % 4 == 0) {
            const size_t k = next() % live.size();
            h.Free(from, 0, live[k]);
            live[k] = live.back();
            live.pop_back();
        } else if (!live.empty() && r % 11 == 0) {
            const size_t k = next() % live.size();
            live[k] = h.ReAlloc(from, 0, live[k], 8 + next() % 200);
        } else {
            live.push_back(h.Alloc(from, 0, 8 + (r % 3 ? r % 64 : r % 4000))); // many small blocks of a few sizes
        }
        offsets.push_back(reinterpret_cast<intptr_t>(live.empty() ? nullptr : live.back()));
    }
    const intptr_t base = offsets.front();
    for (intptr_t &o : offsets)
        if (o) o -= base;
    for (void *p : live) h.Free(from, 0, p);
    return offsets;
}

// The heap's blocks in use.
unsigned Busy(HANDLE heap)
{
    unsigned n = 0;
    PROCESS_HEAP_ENTRY e{};
    HeapLock(heap);
    while (HeapWalk(heap, &e))
        n += (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0;
    HeapUnlock(heap);
    return n;
}

bool Filled(const void *p, unsigned char v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (static_cast<const unsigned char *>(p)[i] != v) return false;
    return true;
}

} // namespace

DIFF_TEST(HostPrivateHeap_Routes, 0)
{
    Heap h;
    HANDLE process = HeapCreate(0, 0, 0); // stands for the process heap: its blocks can be counted
    if (!h.Create(process, 64u << 20)) {
        ctx.Fail("HeapCreate failed (%lu)", GetLastError());
        return;
    }
    // Allocations from the redirected heap come from the fixed heap; its own handle works too.
    void *a = h.Alloc(process, HEAP_ZERO_MEMORY, 100);
    void *b = h.Alloc(h.Handle(), 0, 40);
    if (h.Owner(a) != 1 || h.Owner(b) != 1) ctx.Fail("small blocks not in the fixed heap (%d %d)", h.Owner(a), h.Owner(b));
    if (a && !Filled(a, 0, 100)) ctx.Fail("HEAP_ZERO_MEMORY dropped");
    if (h.Size(process, 0, a) != 100) ctx.Fail("size %u", static_cast<unsigned>(h.Size(process, 0, a)));
    void *a2 = h.ReAlloc(process, 0, a, 5000);
    if (h.Owner(a2) != 1 || h.Size(process, 0, a2) != 5000 || !Filled(a2, 0, 100)) ctx.Fail("realloc not in the fixed heap");
    // A large block comes from the overflow heap.
    void *big = h.Alloc(process, 0, 2u << 20);
    if (h.Owner(big) != 2) ctx.Fail("a 2 MB block: owner %d", h.Owner(big));
    ++ctx.cases;

    // Growing a fixed block past the limit moves it to the overflow heap with its contents (and zeroes the rest).
    std::memset(b, 0x5a, 40);
    void *b2 = h.ReAlloc(process, HEAP_ZERO_MEMORY, b, privateheap::kLargeBlock + 100);
    if (h.Owner(b2) != 2 || !Filled(b2, 0x5a, 40) || !Filled(static_cast<char *>(b2) + 40, 0, privateheap::kLargeBlock + 60))
        ctx.Fail("a block grown past the fixed heap's limit: owner %d, contents kept %d", h.Owner(b2), b2 && Filled(b2, 0x5a, 40));
    void *c = h.Alloc(process, 0, 64);
    if (h.ReAlloc(process, HEAP_REALLOC_IN_PLACE_ONLY, c, privateheap::kLargeBlock + 100) || h.Owner(c) != 1)
        ctx.Fail("an in-place-only resize past the limit moved the block");
    ++ctx.cases;

    // Blocks the private heaps don't own: a process-heap block, the stack, null.
    void *real = HeapAlloc(process, 0, 64);
    int local = 0;
    if (h.Owner(real) || h.Owner(&local) || h.Owner(nullptr)) ctx.Fail("owns a block it didn't allocate");
    if (h.Size(process, 0, real) != 64) ctx.Fail("a process-heap block's size not asked of the process heap");
    void *real2 = h.ReAlloc(process, 0, real, 128);
    if (!real2 || h.Owner(real2) || HeapSize(process, 0, real2) != 128) ctx.Fail("a process-heap block resized in the private heap");
    if (!h.Free(process, 0, real2)) ctx.Fail("a process-heap block's free failed");
    if (Busy(process) != 0) ctx.Fail("a process-heap block not freed in the process heap (%u busy)", Busy(process));
    const privateheap::Stats s = h.GetStats();
    // allocs a, b, big, c; reallocs a, b, c; overflow big and b's move; foreign: Size, ReAlloc, Free of the process block
    if (s.allocs != 4 || s.reallocs != 3 || s.overflow != 2 || s.foreign != 3)
        ctx.Fail("stats: allocs %llu reallocs %llu overflow %llu foreign %llu", s.allocs, s.reallocs, s.overflow, s.foreign);
    ++ctx.cases;

    // Another heap passes through untouched.
    HANDLE other = HeapCreate(0, 0, 0);
    void *o = h.Alloc(other, 0, 32);
    if (!o || h.Owner(o) || HeapSize(other, 0, o) != 32) ctx.Fail("another heap's allocation redirected");
    if (!h.Free(other, 0, o)) ctx.Fail("another heap's free failed");
    HeapDestroy(other);
    if (!h.Free(process, 0, a2) || !h.Free(process, 0, b2) || !h.Free(process, 0, big) || !h.Free(process, 0, c))
        ctx.Fail("free failed");
    if (h.GetStats().frees != 4) ctx.Fail("frees %llu", h.GetStats().frees);
    if (!h.Free(process, 0, nullptr)) ctx.Fail("freeing null failed");
    ctx.cases += 2;
    h.Destroy();
    HeapDestroy(process);
}

DIFF_TEST(HostPrivateHeap_Full, 0)
{
    // A fixed heap of 1 MB: the allocations that don't fit come from the overflow heap, and all of them work.
    Heap h;
    HANDLE process = GetProcessHeap();
    if (!h.Create(process, 1u << 20)) {
        ctx.Fail("HeapCreate failed");
        return;
    }
    std::vector<void *> blocks;
    int fixed = 0, overflow = 0;
    for (int i = 0; i < 3000; ++i) {
        void *p = h.Alloc(process, 0, 1000);
        if (!p) {
            ctx.Fail("allocation %d failed", i);
            break;
        }
        std::memset(p, i & 0xff, 1000);
        (h.Owner(p) == 1 ? fixed : overflow)++;
        blocks.push_back(p);
    }
    if (fixed < 500 || overflow < 1000) ctx.Fail("fixed %d, overflow %d", fixed, overflow);
    if (h.GetStats().overflow != static_cast<uint64_t>(overflow)) ctx.Fail("overflow count %llu", h.GetStats().overflow);
    // Free some filled overflow blocks, then grow a fixed block (the full fixed heap can't): it moves into the overflow
    // heap's used memory, keeps its contents, and HEAP_ZERO_MEMORY zeroes the rest.
    for (size_t i = blocks.size() - 50; i < blocks.size(); ++i) h.Free(process, 0, blocks[i]);
    blocks.resize(blocks.size() - 50);
    const size_t k = 10; // an early block: in the fixed heap, its neighbours in use
    void *grown = h.ReAlloc(process, HEAP_ZERO_MEMORY, blocks[k], 1800);
    if (h.Owner(grown) != 2 || !Filled(grown, static_cast<unsigned char>(k), 1000) ||
        !Filled(static_cast<char *>(grown) + 1000, 0, 800))
        ctx.Fail("a fixed block grown in a full heap: owner %d, contents %d, zeroed %d", h.Owner(grown),
                 grown && Filled(grown, static_cast<unsigned char>(k), 1000), grown && Filled(static_cast<char *>(grown) + 1000, 0, 800));
    if (grown) {
        std::memset(grown, static_cast<int>(k), 1000);
        blocks[k] = grown;
    }
    for (size_t i = 0; i < blocks.size(); ++i)
        if (!Filled(blocks[i], static_cast<unsigned char>(i & 0xff), 1000) || !h.Free(process, 0, blocks[i])) {
            ctx.Fail("block %u overwritten or not freed", static_cast<unsigned>(i));
            break;
        }
    ++ctx.cases;
    h.Destroy();
}

DIFF_TEST(HostPrivateHeap_SameLayout, 0)
{
    // Two private heaps, the same calls: the same offsets. (Two ordinary heaps get the LFH, whose random slots differ.)
    HANDLE from = GetProcessHeap();
    Heap h1, h2;
    if (!h1.Create(from, 16u << 20) || !h2.Create(from, 16u << 20)) {
        ctx.Fail("HeapCreate failed");
        return;
    }
    const std::vector<intptr_t> l1 = Layout(h1, from), l2 = Layout(h2, from);
    size_t differ = 0, first = 0;
    for (size_t i = 0; i < l1.size() && i < l2.size(); ++i)
        if (l1[i] != l2[i] && !differ++) first = i;
    if (l1.size() != l2.size() || differ)
        ctx.Fail("layouts differ at %u of %u calls, first at %u", static_cast<unsigned>(differ), static_cast<unsigned>(l1.size()),
                 static_cast<unsigned>(first));
    ++ctx.cases;
    h1.Destroy();
    h2.Destroy();
}
