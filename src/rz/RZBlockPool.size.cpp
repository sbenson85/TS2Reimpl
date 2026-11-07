#include "rz/RZBlockPool.h"

#include "game/vtable.h"
#include "rz/GeneralAllocator.h"
#include "rz/cRZCriticalSection.h"

#include <cstring>
#include <new>

namespace {

// MSVC 14.40's tree node for RZRegionMap (the same layout the game's library code uses).
struct Node {
    Node *left, *parent, *right;
    char color, isNil;
    uint16_t pad;
    uint32_t key;   // +0x10 region address
    uint32_t value; // +0x14 bitmap
};
static_assert(sizeof(Node) == 0x18, "tree node layout");

Node *Head(RZRegionMap &map)
{
    return *reinterpret_cast<Node **>(&map);
}

Node *NodeOf(RZRegionMap::iterator it)
{
    return reinterpret_cast<Node *>(reinterpret_cast<uint8_t *>(&*it) - 0x10);
}

// In-order successor, as the library iterator's operator++ (from the last node it reaches the head).
Node *Next(Node *node)
{
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil) node = node->left;
        return node;
    }
    Node *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = node->parent;
    }
    return parent;
}

// x86 shifts use the count modulo 32.
uint32_t Shl(uint32_t value, uint32_t count) { return value << (count & 31); }
uint32_t Shr(uint32_t value, uint32_t count) { return value >> (count & 31); }

using NewRegionFn = uint32_t(__thiscall *)(void *, uint32_t *);
using ReleaseRegionFn = void(__thiscall *)(void *, uint32_t *, uint32_t);

template <uint32_t Granularity> void ClearBits(uint32_t *bitmap, uint32_t offset, uint32_t log2Size)
{
    uint32_t width = Shl(1, log2Size - Granularity);
    uint32_t mask = Shl(1, width) - 1;
    *bitmap &= ~Shl(mask, offset >> Granularity);
}

RZBlockPool *ConstructWithVTable(RZBlockPool *pool, const void *vtable)
{
    pool->mVTable = vtable;
    new (&pool->mPartial) RZRegionMap();
    new (&pool->mFull) RZRegionMap();
    pool->mNextUnit = 0;
    pool->mCurrent = Head(pool->Partial());
    return pool;
}

} // namespace

RZBlockPool *RZBlockPool::ConstructBase()
{
    return ConstructWithVTable(this, &ts2_rva_da3c68);
}

RZBlockPool *RZBlockPool::ConstructSmallBase()
{
    return ConstructWithVTable(this, &ts2_rva_da3c80);
}

RZBlockPool *RZBlockPool::ConstructSmall(RZBlockPool *parent)
{
    ConstructSmallBase();
    mParent = parent;
    mVTable = &ts2_rva_da3c8c;
    return this;
}

template <uint32_t Granularity> uint32_t RZBlockPool::Alloc(uint32_t log2Size)
{
    uint32_t k = log2Size - Granularity;
    RZRegionMap &partial = Partial();
    for (;;) {
        Node *head = Head(partial);
        Node *node = static_cast<Node *>(mCurrent);
        if (node == head) {
            node = head->left;
            mCurrent = node;
        }
        if (node != head) {
            uint32_t width = Shl(1, k);
            uint32_t mask = Shl(1, width) - 1;
            uint32_t start = Shl(0x1f, k) & mNextUnit & 0x1f;
            uint32_t first = Shl(mask, start);
            do {
                uint32_t bits = first;
                for (uint32_t steps = 0; steps < 32; steps += width) {
                    if (!(node->value & bits)) {
                        // ORIGINAL-BUG: a block the size of a whole region (2^(granularity + 5)) has
                        // width 32, so its mask is (1 << 32) - 1 = 0 on x86: it always "fits" and no
                        // bits are set, so the region is handed out again.
                        uint32_t unit = start + steps;
                        node->value |= bits;
                        mCurrent = node;
                        mNextUnit = unit + width;
                        uint32_t address = (Shl(unit, Granularity) & Shl(0x1f, Granularity)) + node->key;
                        if (node->value == 0xffffffffu) {
                            mCurrent = Next(node);
                            Full().insert({node->key, node->value});
                            partial.erase(partial.find(node->key));
                        }
                        return address;
                    }
                    bits = Shl(bits, width) + Shr(bits, 32 - width);
                }
                node = Next(node);
                if (node == head) node = head->left;
            } while (node != mCurrent);
        }
        uint32_t extra = 0;
        uint32_t region = VirtualCall<NewRegionFn, 0>(this, &extra);
        if (!region) return 0;
        mCurrent = NodeOf(partial.insert({region, extra}).first);
    }
}

template <uint32_t Granularity> void RZBlockPool::Free(uint32_t address, uint32_t log2Size)
{
    constexpr uint32_t kRegionSize = 0x20u << Granularity;
    RZRegionMap &partial = Partial();
    auto it = partial.upper_bound(address);
    if (it != partial.begin()) {
        --it;
        Node *node = NodeOf(it);
        uint32_t offset = address - node->key;
        if (offset < kRegionSize) {
            ClearBits<Granularity>(&node->value, offset, log2Size);
            if (node->value != 0) return;
            VirtualCall<ReleaseRegionFn, 1>(this, &node->value, node->key);
            if (node == mCurrent) mCurrent = Next(node);
            partial.erase(partial.find(node->key));
            return;
        }
    }
    RZRegionMap &full = Full();
    auto fit = full.upper_bound(address);
    if (fit == full.begin()) return;
    --fit;
    Node *node = NodeOf(fit);
    uint32_t offset = address - node->key;
    if (offset >= kRegionSize) return;
    ClearBits<Granularity>(&node->value, offset, log2Size);
    partial.insert({node->key, node->value});
    full.erase(full.find(node->key));
}

uint32_t RZBlockPool::AllocSmall(uint32_t log2Size)
{
    return Alloc<9>(log2Size);
}

uint32_t RZBlockPool::AllocLarge(uint32_t log2Size)
{
    return Alloc<14>(log2Size);
}

void RZBlockPool::FreeSmall(uint32_t address, uint32_t log2Size)
{
    Free<9>(address, log2Size);
}

void RZBlockPool::FreeLarge(uint32_t address, uint32_t log2Size)
{
    Free<14>(address, log2Size);
}

uint32_t RZBlockPool::LargeNewRegion(uint32_t * /*unused*/)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GeneralAllocatorOs::virtualAlloc(nullptr, 0x80000, MEM_COMMIT, PAGE_READWRITE)));
}

void RZBlockPool::LargeReleaseRegion(uint32_t * /*bitmap*/, uint32_t region)
{
    GeneralAllocatorOs::virtualFree(reinterpret_cast<void *>(static_cast<uintptr_t>(region)), 0, MEM_RELEASE);
}

uint32_t RZBlockPool::SmallNewRegion(uint32_t * /*unused*/)
{
    return mParent->AllocLarge(14);
}

void RZBlockPool::SmallReleaseRegion(uint32_t * /*bitmap*/, uint32_t region)
{
    mParent->FreeLarge(region, 14);
}

void RZBlockPool::ClearSmallBits(uint32_t *bitmap, uint32_t offset, uint32_t log2Size)
{
    ClearBits<9>(bitmap, offset, log2Size);
}

void RZBlockPool::ClearLargeBits(uint32_t *bitmap, uint32_t offset, uint32_t log2Size)
{
    ClearBits<14>(bitmap, offset, log2Size);
}

void __cdecl RZBlockPoolsCreate()
{
    // The original's operator new (library) throws, so its null checks are dead.
    auto *large = static_cast<RZBlockPool *>(::operator new(0x1c));
    large->ConstructBase();
    large->mVTable = &ts2_rva_da3c74;
    ts2_rva_10cb088 = large;
    auto *smallPool = static_cast<RZBlockPool *>(::operator new(0x20));
    smallPool->ConstructSmall(large);
    ts2_rva_10cb08c = smallPool;
}

uint32_t __cdecl RZBlockPoolAlloc(uint32_t log2Size)
{
    if (!ts2_rva_10cb088) RZBlockPoolsCreate();
    cRZCriticalSection *lock = &ts2_rva_10cb090;
    lock->Lock();
    uint32_t address = log2Size < 14 ? ts2_rva_10cb08c->AllocSmall(log2Size)
                                     : ts2_rva_10cb088->AllocLarge(log2Size);
    lock->Unlock();
    return address;
}

void __cdecl RZBlockPoolFree(uint32_t address, uint32_t log2Size)
{
    if (!ts2_rva_10cb088) RZBlockPoolsCreate();
    cRZCriticalSection *lock = &ts2_rva_10cb090;
    lock->Lock();
    if (log2Size < 14) ts2_rva_10cb08c->FreeSmall(address, log2Size);
    else ts2_rva_10cb088->FreeLarge(address, log2Size);
    lock->Unlock();
}

// The tables an image-free program stores in place of the image's (standalone/owned_statics.cpp aliases the owned statics
// to them): the bases' two pure slots, and our region members slot for slot (each the function the hooked game runs for
// that image slot: 0x1be007 / 0x1be216, 0x1be01e / 0x1be22a).
extern "C" int __cdecl _purecall();
namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace
extern "C" const void *ts2_own_vt_da3c68[2] = {reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall)};
extern "C" const void *ts2_own_vt_da3c74[2] = {Member(&RZBlockPool::LargeNewRegion), Member(&RZBlockPool::LargeReleaseRegion)};
extern "C" const void *ts2_own_vt_da3c80[2] = {reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall)};
extern "C" const void *ts2_own_vt_da3c8c[2] = {Member(&RZBlockPool::SmallNewRegion), Member(&RZBlockPool::SmallReleaseRegion)};
