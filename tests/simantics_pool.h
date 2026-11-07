#pragma once
// A stand-in for the stack frame pool (the allocator object at 0x1016994 of the mapped image, 0x3c-byte blocks in 4 KB
// pages) for the tests of the code that takes frames from it and gives them back. The pages are real, page-aligned
// memory (the free function finds a block's page by masking its address); the allocator's own vtable is a fake whose
// NewPage slot hands out a prepared fresh page and whose FreePage slot records the page it is given.
#include "simantics_treesim.h"

#include "rz/RZFixedAllocator.h"

#include <windows.h>

namespace tsw {

constexpr uint32_t kPoolObject = 0x1016994;
constexpr int kPoolPages = 4;      // pages 0..2 in use by a case, page 3 the "fresh" page NewPage hands out
constexpr uint32_t kBlockSize = 0x3c;
constexpr uint32_t kBlocksPerPage = 68;

struct PoolFx {
    uint8_t *mem = nullptr;
    void *vt[8] = {};
    uint32_t saved[3] = {};
    std::vector<uint32_t> freedPages; // pages the FreePage slot was given (as page indices)

    RZFixedPage *Page(int i) const { return reinterpret_cast<RZFixedPage *>(mem + 4096u * static_cast<uint32_t>(i)); }
    uint8_t *BlockAt(int page, uint32_t k) const { return mem + 4096u * static_cast<uint32_t>(page) + 0x10 + k * kBlockSize; }
    int IndexOf(const void *p) const
    {
        if (!p) return -1;
        const uintptr_t d = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(mem);
        return d < 4096u * kPoolPages ? static_cast<int>(d / 4096) : 99;
    }
};

inline PoolFx gPool;

// Slot 2 (NewPage): the prepared page 3, its blocks chained.
TSW_FN0(PoolNewPage)
{
    (void)self;
    RecId(0x77000001u, 2, {});
    RZFixedPage *page = gPool.Page(3);
    uint8_t *first = nullptr;
    for (uint32_t k = kBlocksPerPage; k-- > 0;) {
        uint8_t *block = gPool.BlockAt(3, k);
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    page->freeList = first;
    page->used = 0;
    page->next = page->prev = page;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(page));
}
TSW_FN1(PoolFreePage, page)
{
    (void)self;
    RecId(0x77000001u, 3, {page});
    gPool.freedPages.push_back(static_cast<uint32_t>(gPool.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(page)))));
    return 0;
}

// Builds the pool for a case: page 0 current (with a free list unless the case wants it exhausted), optionally pages 1
// and 2 in the ring; every block's bytes are random so the bytes the code leaves alone are checked too.
inline void BuildPool(uint32_t seed, bool exhausted = false)
{
    if (!gPool.mem) gPool.mem = static_cast<uint8_t *>(VirtualAlloc(nullptr, 4096u * kPoolPages, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 11);
    for (uint32_t i = 0; i < 4096u * kPoolPages; ++i) gPool.mem[i] = static_cast<uint8_t>(r());
    gPool.freedPages.clear();
    gPool.vt[2] = reinterpret_cast<void *>(&PoolNewPage);
    gPool.vt[3] = reinterpret_cast<void *>(&PoolFreePage);
    const int ringSize = 1 + static_cast<int>(r() % 3);
    for (int p = 0; p < 3; ++p) {
        RZFixedPage *page = gPool.Page(p);
        uint8_t *first = nullptr;
        for (uint32_t k = kBlocksPerPage; k-- > 0;) {
            uint8_t *block = gPool.BlockAt(p, k);
            *reinterpret_cast<uint8_t **>(block) = first;
            first = block;
        }
        page->freeList = (p == 0 && exhausted) ? nullptr : first;
        page->used = r() % 5 + (p == 0 && exhausted ? kBlocksPerPage - 3 : 0);
        const int next = (p + 1) % ringSize, prev = (p + ringSize - 1) % ringSize;
        page->next = p < ringSize ? gPool.Page(next) : page;
        page->prev = p < ringSize ? gPool.Page(prev) : page;
    }
    uint32_t *object = GameAddress<uint32_t>(kPoolObject);
    static bool saved = false;
    if (!saved) {
        for (int i = 0; i < 3; ++i) gPool.saved[i] = object[i];
        saved = true;
    }
    object[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPool.vt));
    object[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPool.Page(0)));
    object[2] = (r() % 3 == 0) ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gPool.Page(1))) : 0u;
    gBase2 = gPool.mem;
    gBase2Size = 4096u * kPoolPages;
}

inline void RestorePool()
{
    uint32_t *object = GameAddress<uint32_t>(kPoolObject);
    for (int i = 0; i < 3; ++i) object[i] = gPool.saved[i];
    gBase2 = nullptr;
    gBase2Size = 0;
}

// The pool's state for comparison: the pages' headers (links as page indices) and the first words of the block the
// free list points to, plus the allocator object and the pages the FreePage slot saw.
inline void CapturePool(std::vector<uint32_t> &s)
{
    for (int p = 0; p < 4; ++p) {
        RZFixedPage *page = gPool.Page(p);
        s.push_back(static_cast<uint32_t>(gPool.IndexOf(page->next)));
        s.push_back(static_cast<uint32_t>(gPool.IndexOf(page->prev)));
        s.push_back(page->freeList ? static_cast<uint32_t>(reinterpret_cast<uint8_t *>(page->freeList) - gPool.mem) : 0xffffffffu);
        s.push_back(page->used);
    }
    const uint32_t *object = GameAddress<uint32_t>(kPoolObject);
    s.push_back(static_cast<uint32_t>(gPool.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(object[1])))));
    s.push_back(static_cast<uint32_t>(gPool.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(object[2])))));
    s.push_back(static_cast<uint32_t>(gPool.freedPages.size()));
    for (uint32_t f : gPool.freedPages) s.push_back(f);
    // The bytes of all blocks (the code under test must leave the ones it does not write alone).
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 4096u * kPoolPages; ++i) h = (h ^ gPool.mem[i]) * 16777619u;
    s.push_back(h);
}

} // namespace tsw
