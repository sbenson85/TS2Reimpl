#pragma once
#include "rz/RZFixedAllocator.h"

#include <cstdint>

namespace rz {

// A pool's page: the smallest 2^Log2 (at least 2 KB) that holds 64 blocks after the 16-byte header. Every instance's
// constants (its disassembly: the RZBlockPoolAlloc argument, the chain's count and stride, the full mark) follow it.
constexpr uint32_t PageLog2(uint32_t blockSize)
{
    uint32_t log2 = 11;
    while (((1u << log2) - 16u) / blockSize < 64u) ++log2;
    return log2;
}

// cRZFastFixedAllocator2<size> (1.18.0.187), one class per block size:
//   slot 1 Free(block): page = block & ~(2^Log2 - 1); the block goes on the page's free list and the use
//       count drops by one. Zero -> ReleaseEmptyPage; down to kFullMark (0x40000000 + three quarters of the
//       blocks, the count of a full page) -> ReturnFullPage.
//   slot 2 NewPage(): a page from RZBlockPoolAlloc(Log2). Its kBlocks blocks of BlockSize bytes, at
//       kHeader + k * BlockSize, are chained into the free list from the front; the count is 0.
//   slot 3 FreePage(page): RZBlockPoolFree(page, Log2). The copies are folded by page size (0x15aa70, 0x14a380 /
//       0x20fef0, 0x163170, 0xa35050), except 0x164630 (0x19c) and 0x22ee06 (0x1b0).
// The constructor makes the first page (NewPage) the current ring of one; the destructor (not virtual: the vtables
// have no destructor slot) gives every page of both rings back through slot 3.
template <uint32_t BlockSize>
class cRZFastFixedAllocator2 : public cRZFastFixedAllocator2Common {
public:
    static constexpr uint32_t kLog2 = PageLog2(BlockSize);
    static constexpr uint32_t kPageSize = 1u << kLog2;
    static constexpr uint32_t kBlockSize = BlockSize;
    static constexpr uint32_t kHeader = sizeof(RZFixedPage);
    static constexpr uint32_t kBlocks = (kPageSize - kHeader) / BlockSize;
    static constexpr uint32_t kFullMark = 0x40000000u + kBlocks * 3u / 4u;

    cRZFastFixedAllocator2();
    ~cRZFastFixedAllocator2();

    void Free(void *block) override;           // slot 1
    RZFixedPage *NewPage() override;           // slot 2
    void FreePage(RZFixedPage *page) override; // slot 3
};

// cRZFastAllocator<T>: a class's own pool (its class operator new / delete), a cRZFastFixedAllocator2<sizeof(T)>
// with its own vtable (the same four slots). The pooled types are tags here: only their sizes matter.
template <class T> struct FastAllocatorBlock;
template <class T> class cRZFastAllocator : public cRZFastFixedAllocator2<FastAllocatorBlock<T>::kSize> {};

namespace pooled {
struct cTSAudioEvent;
struct cTSAudioSoundInstance;
struct cTSBehaviorNode;
struct cTSWantInstance;
struct cTSTreeStackElem;
struct cGZSndEventStd;
struct cGenericDataTableContainer; // cITSDataTables::cGenericDataTableContainer
struct XPrimParam;
} // namespace pooled

template <> struct FastAllocatorBlock<pooled::cTSAudioEvent> { static constexpr uint32_t kSize = 0x34; };
template <> struct FastAllocatorBlock<pooled::cTSAudioSoundInstance> { static constexpr uint32_t kSize = 0x1b0; };
template <> struct FastAllocatorBlock<pooled::cTSBehaviorNode> { static constexpr uint32_t kSize = 0x10; };
template <> struct FastAllocatorBlock<pooled::cTSWantInstance> { static constexpr uint32_t kSize = 0x3c; };
template <> struct FastAllocatorBlock<pooled::cTSTreeStackElem> { static constexpr uint32_t kSize = 0x3c; };
template <> struct FastAllocatorBlock<pooled::cGZSndEventStd> { static constexpr uint32_t kSize = 0x38; };
template <> struct FastAllocatorBlock<pooled::cGenericDataTableContainer> { static constexpr uint32_t kSize = 0x20; };
template <> struct FastAllocatorBlock<pooled::XPrimParam> { static constexpr uint32_t kSize = 0x18; };

// The constructors' and destructors' adapters (the hooks on the image's out-of-line copies: ecx = the object).
template <class X> struct FastAllocatorAdapters {
    static X *__fastcall Construct(X *self);
    static void __fastcall Destruct(X *self);
};

} // namespace rz

// The hook-shaped view of a game-built instance (see RZFixedAllocatorBase): code that calls a pool's slot directly,
// as the original's compiler did for a global of known type. Its members are the slot hooks' adapters, forwarding
// (qualified) to rz::cRZFastFixedAllocator2<BlockSize>; the template arguments are checked against that class.
template <uint32_t Log2, uint32_t BlockSize, uint32_t Blocks, uint32_t Threshold>
class RZFastFixedAllocatorN : public RZFixedAllocatorBase {
public:
    using Shape = rz::cRZFastFixedAllocator2<BlockSize>;
    static constexpr uint32_t kLog2 = Log2;
    static constexpr uint32_t kPageSize = 1u << Log2;
    static constexpr uint32_t kBlockSize = BlockSize;
    static constexpr uint32_t kBlocks = Blocks;
    static constexpr uint32_t kHeader = sizeof(RZFixedPage);
    static constexpr uint32_t kFullMark = 0x40000000u + Threshold;
    static_assert(Shape::kLog2 == Log2 && Shape::kBlocks == Blocks && Shape::kFullMark == kFullMark,
                  "the instance's constants follow its block size");

    void Free(void *block);           // slot 1
    RZFixedPage *NewPage();           // slot 2
    void FreePage(RZFixedPage *page); // slot 3

    Shape *Self() { return reinterpret_cast<Shape *>(this); }
};

// The instances, named by block size: page 2^Log2 bytes, Blocks blocks, full threshold Threshold
// (the use count at which a full page goes back to the current ring). Constants from each instance's
// disassembly; the RVAs are in src/hook/tables/RZFastFixedAllocators.cpp.
using RZFastFixed0xc = RZFastFixedAllocatorN<11, 0x0c, 169, 126>;
using RZFastFixed0x10 = RZFastFixedAllocatorN<11, 0x10, 127, 95>;
using RZFastFixed0x14 = RZFastFixedAllocatorN<11, 0x14, 101, 75>;
using RZFastFixed0x18 = RZFastFixedAllocatorN<11, 0x18, 84, 63>;
using RZFastFixed0x1c = RZFastFixedAllocatorN<11, 0x1c, 72, 54>;
using RZFastFixed0x20 = RZFastFixedAllocatorN<12, 0x20, 127, 95>;
using RZFastFixed0x28 = RZFastFixedAllocatorN<12, 0x28, 102, 76>;
using RZFastFixed0x34 = RZFastFixedAllocatorN<12, 0x34, 78, 58>;
using RZFastFixed0x38 = RZFastFixedAllocatorN<12, 0x38, 72, 54>;
using RZFastFixed0x3c = RZFastFixedAllocatorN<12, 0x3c, 68, 51>;
using RZFastFixed0x54 = RZFastFixedAllocatorN<13, 0x54, 97, 72>;
using RZFastFixed0x58 = RZFastFixedAllocatorN<13, 0x58, 92, 69>;
using RZFastFixed0x5c = RZFastFixedAllocatorN<13, 0x5c, 88, 66>;
using RZFastFixed0x74 = RZFastFixedAllocatorN<13, 0x74, 70, 52>;
using RZFastFixed0x98 = RZFastFixedAllocatorN<14, 0x98, 107, 80>;
using RZFastFixed0x9c = RZFastFixedAllocatorN<14, 0x9c, 104, 78>;
using RZFastFixed0x19c = RZFastFixedAllocatorN<15, 0x19c, 79, 59>;
using RZFastFixed0x1b0 = RZFastFixedAllocatorN<15, 0x1b0, 75, 56>;
