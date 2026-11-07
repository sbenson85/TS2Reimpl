#include "rz/RZFixedAllocator.h"

#include "rz/RZFrameworkHelpers.h"

namespace rz {

void *cRZFastFixedAllocator2Common::TakeFreePage()
{
    RZFixedPage *page = mCurrent;
    if (page == page->prev) {
        RZFixedPage *fresh = NewPage();
        RZFixedPage *before = mCurrent->prev;
        before->next = fresh;
        mCurrent->prev = fresh;
        fresh->next = mCurrent;
        fresh->prev = before;
        mCurrent = fresh;
    }
    RZFixedPage *prev = page->prev, *next = page->next;
    prev->next = next;
    next->prev = prev;
    mCurrent = prev;
    page->used += 0x40000000;
    if (RZFixedPage *full = mFull) {
        RZFixedPage *last = full->prev;
        page->next = full;
        page->prev = last;
        last->next = page;
        full->prev = page;
    } else {
        page->prev = page;
        page->next = page;
    }
    mFull = page;
    return mCurrent->freeList;
}

RZFixedPage *cRZFastFixedAllocator2Common::ReturnFullPage(RZFixedPage *page)
{
    RZFixedPage *prev = page->prev, *next = page->next;
    page->used += 0xc0000000;
    prev->next = next;
    next->prev = prev;
    if (mFull == page) mFull = prev != page ? prev : nullptr;
    RZFixedPage *current = mCurrent, *before = current->prev;
    page->next = current;
    page->prev = before;
    current->prev = page;
    before->next = page;
    return before;
}

void cRZFastFixedAllocator2Common::ReleaseEmptyPage(RZFixedPage *page)
{
    RZFixedPage *prev = page->prev;
    if (page == prev) return;
    RZFixedPage *next = page->next;
    next->prev = prev;
    prev->next = next;
    if (mCurrent == page) mCurrent = prev;
    FreePage(page);
}

void cRZFastFixedAllocator2Common::FreeRings()
{
    // 0x1be23b calls slot 3 of its `this` for each page: this object.
    auto *visitor = reinterpret_cast<RZListNodeVisitor *>(this);
    visitor->VisitNodesThenHead(mCurrent);
    visitor->VisitNodesThenHead(mFull);
}

} // namespace rz

// The view's members: the hooks' adapters (qualified calls, inlined).
void *RZFixedAllocatorBase::TakeFreePage()
{
    [[msvc::forceinline_calls]] return Self()->rz::cRZFastFixedAllocator2Common::TakeFreePage();
}

RZFixedPage *RZFixedAllocatorBase::ReturnFullPage(RZFixedPage *page)
{
    [[msvc::forceinline_calls]] return Self()->rz::cRZFastFixedAllocator2Common::ReturnFullPage(page);
}

void RZFixedAllocatorBase::ReleaseEmptyPage(RZFixedPage *page)
{
    [[msvc::forceinline_calls]] Self()->rz::cRZFastFixedAllocator2Common::ReleaseEmptyPage(page);
}
