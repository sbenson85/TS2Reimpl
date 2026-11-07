// Differential test for the fixed-size pool allocator 0x149040 (nGZGraphic4::cGZMessagePool::Allocate, src/gz/cGZCanvas.cpp;
// lane RB): the canvas's message pools and cTSOverlayManager's pools take elements from it.
//
// Each round builds a pool (element size, block size, a block-list token, a free list of 0..3 static elements or none),
// then takes 1..12 elements from it, on each side from the same state at the same addresses: the static elements and an
// arena standing in for RZMemory::Malloc (0x1be3b0 on the original's side, ours on ours: shape::StandIn), which hands out
// pattern-filled blocks from the arena's start each run. Compared: every element returned, the pool's four words, the
// arena's and the static elements' bytes, and the sizes asked of Malloc. The block size is at least one element (with
// none, the original writes its end link one element before the block; no pool is built so).
#include "code_patch.h"
#include "difftest.h"

#include "gz/cGZCanvas.h"
#include "rz/RZMemory.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using nGZGraphic4::cGZMessagePool;

namespace {

constexpr size_t kArena = 0x4000;
alignas(16) uint8_t gArena[kArena];
size_t gArenaUsed = 0;
std::vector<uint32_t> gAsked;

void *__cdecl ArenaMalloc(uint32_t bytes)
{
    gAsked.push_back(bytes);
    const size_t at = gArenaUsed;
    gArenaUsed += (size_t(bytes) + 15) & ~size_t(15);
    if (gArenaUsed > kArena) return nullptr; // the round sizes keep within the arena
    return gArena + at;
}

alignas(16) uint8_t gStatic[4][0x40];

struct State {
    uint32_t pool[4];
    std::vector<uint32_t> returned, asked;
    std::vector<uint8_t> arena, statics;
};

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }

} // namespace

DIFF_TEST(cGZMessagePool_Allocate, 0x149040)
{
    using Fn = void *(__thiscall *)(cGZMessagePool *);
    auto original = ctx.Original<Fn>(0x149040);
    shape::StandIn malloc{0x1be3b0, &RZMemory::Malloc, &ArenaMalloc};
    std::mt19937 gen(0x149040);
    for (unsigned round = 0; round < 2000; ++round) {
        ++ctx.cases;
        const uint32_t element = 4 * (1 + gen() % 16);        // 4..0x40
        const uint32_t count = 1 + gen() % 8;                  // elements a block holds
        const uint32_t blockSize = element * count + (gen() % 3 == 0 ? gen() % element : 0); // sometimes a remainder
        const uint32_t blocks = gen();
        const unsigned freeCount = gen() % 5;                   // 4: no free list
        const unsigned takes = 1 + gen() % 12;
        const uint32_t seed = gen();
        State s[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 fill(seed);
            for (auto &b : gArena) b = uint8_t(fill());
            for (auto &e : gStatic)
                for (auto &b : e) b = uint8_t(fill());
            gArenaUsed = 0;
            gAsked.clear();
            cGZMessagePool pool{element, blockSize, reinterpret_cast<void *>(uintptr_t(blocks)), nullptr};
            if (freeCount < 4) {
                // A free list of freeCount static elements (the last links nothing).
                for (unsigned i = 0; i < freeCount; ++i)
                    *reinterpret_cast<void **>(gStatic[i]) = i + 1 < freeCount ? gStatic[i + 1] : nullptr;
                pool.mFree = freeCount ? gStatic[0] : nullptr;
            }
            for (unsigned t = 0; t < takes; ++t)
                s[side].returned.push_back(Word(side == 0 ? original(&pool) : pool.Allocate()));
            std::memcpy(s[side].pool, &pool, 16);
            s[side].asked = gAsked;
            s[side].arena.assign(gArena, gArena + kArena);
            s[side].statics.assign(&gStatic[0][0], &gStatic[0][0] + sizeof(gStatic));
        }
        if (s[0].returned != s[1].returned) ctx.Fail("round %u (element %u, block %u): the elements returned differ", round, element, blockSize);
        if (std::memcmp(s[0].pool, s[1].pool, 16) != 0)
            ctx.Fail("round %u: pool %08x %08x %08x %08x / %08x %08x %08x %08x", round, s[0].pool[0], s[0].pool[1], s[0].pool[2],
                     s[0].pool[3], s[1].pool[0], s[1].pool[1], s[1].pool[2], s[1].pool[3]);
        if (s[0].asked != s[1].asked) ctx.Fail("round %u: Malloc asked %zu / %zu times (first %u / %u)", round, s[0].asked.size(),
                                               s[1].asked.size(), s[0].asked.empty() ? 0 : s[0].asked[0], s[1].asked.empty() ? 0 : s[1].asked[0]);
        if (s[0].arena != s[1].arena) {
            size_t at = 0;
            while (s[0].arena[at] == s[1].arena[at]) ++at;
            ctx.Fail("round %u (element %u, block %u): the blocks differ at %zu", round, element, blockSize, at);
        }
        if (s[0].statics != s[1].statics) ctx.Fail("round %u: the static elements differ", round);
        if (!ctx.failures.empty()) return;
    }
}
