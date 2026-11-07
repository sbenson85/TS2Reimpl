#include "rz/RZFastFixedAllocators.h"

#include "rz/RZBlockPool.h"

#include <new>

namespace rz {

void *cRZFastFixedAllocator2Common::Alloc()
{
    void *block = mCurrent->freeList;
    if (!block) block = TakeFreePage();
    mCurrent->freeList = *static_cast<void **>(block);
    ++mCurrent->used;
    return block;
}

template <uint32_t BlockSize> cRZFastFixedAllocator2<BlockSize>::cRZFastFixedAllocator2()
{
    mCurrent = NewPage();
    mFull = nullptr;
    mCurrent->prev = mCurrent;
    mCurrent->next = mCurrent->prev;
}

template <uint32_t BlockSize> cRZFastFixedAllocator2<BlockSize>::~cRZFastFixedAllocator2()
{
    FreeRings();
}

template <uint32_t BlockSize> void cRZFastFixedAllocator2<BlockSize>::Free(void *block)
{
    auto *page = reinterpret_cast<RZFixedPage *>(reinterpret_cast<uintptr_t>(block) & ~static_cast<uintptr_t>(kPageSize - 1));
    *static_cast<void **>(block) = page->freeList;
    uint32_t used = page->used - 1;
    page->freeList = block;
    page->used = used;
    if (used == 0)
        ReleaseEmptyPage(page);
    else if (used == kFullMark)
        ReturnFullPage(page);
}

template <uint32_t BlockSize> RZFixedPage *cRZFastFixedAllocator2<BlockSize>::NewPage()
{
    auto page = static_cast<uintptr_t>(RZBlockPoolAlloc(kLog2));
    // Chain from the last block down, so the list runs upwards from the first block.
    uint32_t next = 0;
    for (uint32_t k = kBlocks; k-- > 0;) {
        auto *block = reinterpret_cast<uint32_t *>(page + kHeader + static_cast<uintptr_t>(k) * BlockSize);
        *block = next;
        next = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    }
    auto *fresh = reinterpret_cast<RZFixedPage *>(page);
    fresh->freeList = reinterpret_cast<void *>(static_cast<uintptr_t>(next));
    fresh->used = 0;
    return fresh;
}

template <uint32_t BlockSize> void cRZFastFixedAllocator2<BlockSize>::FreePage(RZFixedPage *page)
{
    RZBlockPoolFree(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(page)), kLog2);
}

template <class X> X *__fastcall FastAllocatorAdapters<X>::Construct(X *self)
{
    [[msvc::forceinline_calls]] return ::new (self) X;
}

template <class X> void __fastcall FastAllocatorAdapters<X>::Destruct(X *self)
{
    [[msvc::forceinline_calls]] self->X::~X();
}

// The instances: one class per block size in the game, and the pooled classes' own.
template class cRZFastFixedAllocator2<0x0c>;
template class cRZFastFixedAllocator2<0x10>;
template class cRZFastFixedAllocator2<0x14>;
template class cRZFastFixedAllocator2<0x18>;
template class cRZFastFixedAllocator2<0x1c>;
template class cRZFastFixedAllocator2<0x20>;
template class cRZFastFixedAllocator2<0x28>;
template class cRZFastFixedAllocator2<0x34>;
template class cRZFastFixedAllocator2<0x38>;
template class cRZFastFixedAllocator2<0x3c>;
template class cRZFastFixedAllocator2<0x54>;
template class cRZFastFixedAllocator2<0x58>;
template class cRZFastFixedAllocator2<0x5c>;
template class cRZFastFixedAllocator2<0x74>;
template class cRZFastFixedAllocator2<0x98>;
template class cRZFastFixedAllocator2<0x9c>;
template class cRZFastFixedAllocator2<0x19c>;
template class cRZFastFixedAllocator2<0x1b0>;
template class cRZFastAllocator<pooled::cTSAudioEvent>;
template class cRZFastAllocator<pooled::cTSAudioSoundInstance>;
template class cRZFastAllocator<pooled::cTSBehaviorNode>;
template class cRZFastAllocator<pooled::cTSWantInstance>;
template class cRZFastAllocator<pooled::cTSTreeStackElem>;
template class cRZFastAllocator<pooled::cGZSndEventStd>;
template class cRZFastAllocator<pooled::cGenericDataTableContainer>;
template class cRZFastAllocator<pooled::XPrimParam>;

// The sizes whose constructor / destructor the image has out of line (the hooks).
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x0c>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x10>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x14>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x18>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x1c>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x20>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x28>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x34>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x38>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x3c>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x58>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x74>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x98>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x9c>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x1b0>>;
// No out-of-line copy in the image (constructed and destroyed inline only); instantiated for the tests' table.
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x54>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x5c>>;
template struct FastAllocatorAdapters<cRZFastFixedAllocator2<0x19c>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cTSAudioEvent>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cTSAudioSoundInstance>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cTSBehaviorNode>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cTSWantInstance>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cTSTreeStackElem>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cGZSndEventStd>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::cGenericDataTableContainer>>;
template struct FastAllocatorAdapters<cRZFastAllocator<pooled::XPrimParam>>;

} // namespace rz

// The view's members: the slot hooks' adapters (qualified calls, inlined).
void *RZFixedAllocatorBase::Alloc()
{
    [[msvc::forceinline_calls]] return Self()->rz::cRZFastFixedAllocator2Common::Alloc();
}

template <uint32_t Log2, uint32_t BlockSize, uint32_t Blocks, uint32_t Threshold>
void RZFastFixedAllocatorN<Log2, BlockSize, Blocks, Threshold>::Free(void *block)
{
    [[msvc::forceinline_calls]] Self()->Shape::Free(block);
}

template <uint32_t Log2, uint32_t BlockSize, uint32_t Blocks, uint32_t Threshold>
RZFixedPage *RZFastFixedAllocatorN<Log2, BlockSize, Blocks, Threshold>::NewPage()
{
    [[msvc::forceinline_calls]] return Self()->Shape::NewPage();
}

template <uint32_t Log2, uint32_t BlockSize, uint32_t Blocks, uint32_t Threshold>
void RZFastFixedAllocatorN<Log2, BlockSize, Blocks, Threshold>::FreePage(RZFixedPage *page)
{
    [[msvc::forceinline_calls]] Self()->Shape::FreePage(page);
}

// The views, one per class in the game (explicit instantiation makes the members linkable).
template class RZFastFixedAllocatorN<11, 0x0c, 169, 126>;
template class RZFastFixedAllocatorN<11, 0x10, 127, 95>;
template class RZFastFixedAllocatorN<11, 0x14, 101, 75>;
template class RZFastFixedAllocatorN<11, 0x18, 84, 63>;
template class RZFastFixedAllocatorN<11, 0x1c, 72, 54>;
template class RZFastFixedAllocatorN<12, 0x20, 127, 95>;
template class RZFastFixedAllocatorN<12, 0x28, 102, 76>;
template class RZFastFixedAllocatorN<12, 0x34, 78, 58>;
template class RZFastFixedAllocatorN<12, 0x38, 72, 54>;
template class RZFastFixedAllocatorN<12, 0x3c, 68, 51>;
template class RZFastFixedAllocatorN<13, 0x54, 97, 72>;
template class RZFastFixedAllocatorN<13, 0x58, 92, 69>;
template class RZFastFixedAllocatorN<13, 0x5c, 88, 66>;
template class RZFastFixedAllocatorN<13, 0x74, 70, 52>;
template class RZFastFixedAllocatorN<14, 0x98, 107, 80>;
template class RZFastFixedAllocatorN<14, 0x9c, 104, 78>;
template class RZFastFixedAllocatorN<15, 0x19c, 79, 59>;
template class RZFastFixedAllocatorN<15, 0x1b0, 75, 56>;
