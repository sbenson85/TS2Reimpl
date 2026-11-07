#pragma once
#include <cstdint>

// The fixed-size object pools behind class-specific operator new (1.18.0.187): cRZFastFixedAllocator2<size>
// (src/rz/RZFastFixedAllocators.h), one class per block size, all on this base. RTTI: cIGZFastFixedAllocator2 (the
// interface: four slots, no destructor slot, no QueryInterface) <- cRZFastFixedAllocator2Common (this file) <-
// cRZFastFixedAllocator2<size> <- cRZFastAllocator<T> (a class's own pool: the same slots, its own vtable).
// The slots: 0 Alloc (one body for every instance, 0x14a2d0), 1 Free(block), 2 NewPage (a page from the slab
// allocator, its blocks chained into a free list), 3 FreePage(page). Pages are 2^Log2-aligned:
//   +0 next, +4 prev (ring links), +8 free-list head, +0xc blocks in use; 0x40000000 is added while
//   the page is on the full ring.
// The pool keeps a ring of pages with free blocks (`mCurrent` is where allocation happens) and a ring
// of full pages (`mFull`, null when empty). Freeing a whole ring (0x1be23b) is
// RZListNodeVisitor::VisitNodesThenHead in src/rz/RZFrameworkHelpers.h.
struct RZFixedPage {
    RZFixedPage *next;
    RZFixedPage *prev;
    void *freeList;
    uint32_t used;
};

namespace rz {

// The interface (RTTI cIGZFastFixedAllocator2), slots in order.
class cIGZFastFixedAllocator2 {
public:
    virtual void *Alloc() = 0;                    // slot 0
    virtual void Free(void *block) = 0;           // slot 1
    virtual RZFixedPage *NewPage() = 0;           // slot 2
    virtual void FreePage(RZFixedPage *page) = 0; // slot 3
};

// The shared base (RTTI cRZFastFixedAllocator2Common): the rings and the size-independent members.
class cRZFastFixedAllocator2Common : public cIGZFastFixedAllocator2 {
public:
    // Slot 0, 0x14a2d0 (every instance's vtable holds it, or the 5-byte jump 0x20feb9 to it; the game's class
    // operator news call it directly): the current page's first free block, taken off its free list. An empty
    // free list first moves the page to the full ring (TakeFreePage).
    void *Alloc() override;

    // 0x1be2bc: the current page has no free block: moves it to the full ring (making a new page first
    // if it was the only one) and returns the next page's free-list head.
    void *TakeFreePage();
    // 0x1be31b: a page on the full ring got a block back: moves it back before the current page.
    // Returns the page that was before the current one (left in eax by the original).
    RZFixedPage *ReturnFullPage(RZFixedPage *page);
    // 0x1be02b: a page became empty: unless it's the only one on its ring, unlinks it (moving the
    // current page back if it was this one) and frees it through slot 3.
    void ReleaseEmptyPage(RZFixedPage *page);
    // 0x1be23b on both rings (the destructor's work): every page back through slot 3.
    void FreeRings();

    RZFixedPage *mCurrent; // +4
    RZFixedPage *mFull;    // +8
};
static_assert(sizeof(cRZFastFixedAllocator2Common) == 12, "cRZFastFixedAllocator2Common: 12 bytes");

} // namespace rz

// The hook-shaped view of the same 12 bytes, for code that reaches a pool the game built (GameAddress) and calls its
// members directly, as the original's compiler did for a global object of known type (cTSBehaviorTree, TreeFrame,
// SimAnticsParams). Its members are the hooks' adapters: each forwards, qualified, to the class above.
class RZFixedAllocatorBase {
public:
    void *Alloc();                                  // 0x14a2d0
    void *TakeFreePage();                           // 0x1be2bc
    RZFixedPage *ReturnFullPage(RZFixedPage *page); // 0x1be31b
    void ReleaseEmptyPage(RZFixedPage *page);       // 0x1be02b

    const void *mVTable;     // +0
    RZFixedPage *mCurrent;   // +4
    RZFixedPage *mFull;      // +8

    rz::cRZFastFixedAllocator2Common *Self() { return reinterpret_cast<rz::cRZFastFixedAllocator2Common *>(this); }
};
