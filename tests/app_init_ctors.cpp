// Differential tests for the constructors in src/app/AppInit.* :
//   0x1bec45 cGZPropertySetBase, 0x22b157 cGZPropertySetString
//   (0xd0b6c0 nTSWorld::cWorldDB moved to tests/world_db.cpp, lane JC; 0xc26045 cGZPropertyManagerString to
//   tests/gz_property_manager.cpp, lane PU; 0xc644aa cRZNetSSLManager to tests/rz_net_ssl_manager.cpp, lane PW: each
//   is our class there).
//
// Method: two blocks with the same random fill (constructors leave padding alone), the original
// constructor on one and ours on the other. The blocks must be byte-identical afterwards except for
// the heap pointers (sentinel nodes, the critical section's block, the id set's head), which are
// blanked in copies and checked separately: a sentinel node is three self pointers and the word
// 0x101 at +0xc, of the same heap size on both sides; the id set is compared through its contents.
#include "difftest.h"
#include "app/AppInit.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <set>
#include <vector>

namespace {

constexpr size_t kTail = 16;

struct Block {
    uint8_t *a;
    uint8_t *b;
    size_t size;
};

Block MakeBlock(size_t size, uint32_t seed)
{
    Block blk{static_cast<uint8_t *>(::operator new(size + kTail)), static_cast<uint8_t *>(::operator new(size + kTail)), size};
    std::mt19937 rng(seed);
    for (size_t i = 0; i < size + kTail; ++i) blk.a[i] = blk.b[i] = static_cast<uint8_t>(rng());
    return blk;
}

void FreeBlock(Block &blk)
{
    ::operator delete(blk.a);
    ::operator delete(blk.b);
}

// A sentinel node as the code makes them: self pointers, then 0x101 at +0xc. Returns the heap size.
bool NodeShape(const void *node, SIZE_T *heapSize)
{
    if (!node) return false;
    const uint32_t *w = static_cast<const uint32_t *>(node);
    uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    *heapSize = HeapSize(GetProcessHeap(), 0, const_cast<void *>(node));
    return w[0] == self && w[1] == self && w[2] == self && *reinterpret_cast<const uint16_t *>(&w[3]) == 0x101;
}

void Blank(uint8_t *block, std::initializer_list<size_t> offsets)
{
    for (size_t off : offsets) std::memset(block + off, 0, 4);
}

// Compares the two blocks with the listed pointer slots (offsets) blanked; each of them holds a sentinel node.
void CompareBlocks(difftest::Context &ctx, const char *what, const Block &blk, std::initializer_list<size_t> nodeSlots, std::initializer_list<size_t> otherPointers)
{
    ++ctx.cases;
    std::vector<uint8_t> ca(blk.a, blk.a + blk.size + kTail), cb(blk.b, blk.b + blk.size + kTail);
    for (size_t off : nodeSlots) {
        void *pa = *reinterpret_cast<void **>(blk.a + off);
        void *pb = *reinterpret_cast<void **>(blk.b + off);
        SIZE_T sa = 0, sb = 0;
        if (!NodeShape(pa, &sa) || !NodeShape(pb, &sb) || sa != sb) ctx.Fail("%s: the node at +0x%zx differs (heap sizes %zu/%zu)", what, off, static_cast<size_t>(sa), static_cast<size_t>(sb));
        std::memset(ca.data() + off, 0, 4);
        std::memset(cb.data() + off, 0, 4);
    }
    for (size_t off : otherPointers) {
        void *pa = *reinterpret_cast<void **>(blk.a + off);
        void *pb = *reinterpret_cast<void **>(blk.b + off);
        if ((pa == nullptr) != (pb == nullptr)) ctx.Fail("%s: pointer at +0x%zx null on one side only", what, off);
        std::memset(ca.data() + off, 0, 4);
        std::memset(cb.data() + off, 0, 4);
    }
    if (ca != cb) {
        size_t first = 0;
        while (ca[first] == cb[first]) ++first;
        ctx.Fail("%s: blocks differ at +0x%zx (original %02x, ours %02x)", what, first, ca[first], cb[first]);
    }
}

} // namespace

DIFF_TEST(AppInit_PropertySetBase_Construct, 0x1bec45)
{
    auto orig = ctx.Original<appinit::cGZPropertySetBase *(__thiscall *)(void *)>(0x1bec45);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        Block blk = MakeBlock(sizeof(appinit::cGZPropertySetBase), seed);
        auto *ra = orig(blk.a);
        auto *rb = reinterpret_cast<appinit::cGZPropertySetBase *>(blk.b)->Construct();
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(ra) != blk.a || reinterpret_cast<uint8_t *>(rb) != blk.b || std::memcmp(blk.a, blk.b, blk.size + kTail) != 0)
            ctx.Fail("seed %u: constructor differs", seed);
        FreeBlock(blk);
    }
}

DIFF_TEST(AppInit_PropertySetString_Construct, 0x22b157)
{
    auto orig = ctx.Original<appinit::cGZPropertySetString *(__thiscall *)(void *)>(0x22b157);
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        Block blk = MakeBlock(sizeof(appinit::cGZPropertySetString), seed);
        auto *ra = orig(blk.a);
        auto *rb = reinterpret_cast<appinit::cGZPropertySetString *>(blk.b)->Construct();
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(ra) != blk.a || reinterpret_cast<uint8_t *>(rb) != blk.b || std::memcmp(blk.a, blk.b, blk.size + kTail) != 0)
            ctx.Fail("seed %u: constructor differs", seed);
        FreeBlock(blk);
    }
}

// 0xc644aa (cRZNetSSLManager's constructor) moved to tests/rz_net_ssl_manager.cpp (lane PW: it builds our class).
