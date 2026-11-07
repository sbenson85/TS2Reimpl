// Differential test for InteractionTreeHasFlags (0x516e00): the flags of an interaction's check tree, remembered per (tree id,
// identity) in the std::map at game global 0x10dac2c. The map is built in the image for each run (random contents, so cache hits
// and misses both occur), described and destroyed after it. The interaction, its stack object, entry, tree owner and tree are
// recording fakes.
#include "simantics_autonomy_world.h"

#include "simantics/AutonomyHelpers.h"

#include <algorithm>
#include <map>

namespace {

enum : int { kInteraction = kF0, kStackObject, kEntry, kOwner, kTree };

constexpr uint32_t kCache = 0x10dac2c;
using Cache = std::map<uint64_t, uint8_t>;

uint64_t KeyFor(uint16_t treeId, uint32_t identity)
{
    const int32_t signedId = static_cast<int16_t>(treeId);
    const uint32_t sign = static_cast<uint32_t>(signedId >> 31);
    const uint32_t upper = (static_cast<uint16_t>(treeId - 0x100) > 0xeff) ? (sign | identity) : sign;
    return (static_cast<uint64_t>(upper) << 32) | static_cast<uint32_t>(signedId);
}

void BuildFlags(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 61);
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    Use(w.vtFk[kInteraction], 24, 0);
    Use(w.vtFk[kInteraction], 16, 0);
    Use(w.vtFk[kStackObject], 89, 0);
    Use(w.vtFk[kEntry], 78, 0);
    Use(w.vtFk[kOwner], 12, 1);
    Use(w.vtFk[kOwner], 23, 1);
    Use(w.vtFk[kTree], 16, 0);
    Use(w.vtFk[kTree], 34, 1);
    const uint16_t ids[] = {0, 0x100, 0xfff, 0x1000, 0x7fff, 0x8000, 0xffff, 0xeff, 0x0ff, 0x2345};
    const uint16_t treeId = (r() % 3 == 0) ? static_cast<uint16_t>(r()) : ids[r() % 10];
    const uint32_t identity = r() % 4 == 0 ? 0u : r();
    RetObject(&w.fk[kInteraction], 24, &w.fk[kEntry], w);
    RetObject(&w.fk[kInteraction], 16, &w.fk[kStackObject], w);
    RetObject(&w.fk[kStackObject], 89, &w.fk[kOwner], w);
    RetValue(&w.fk[kEntry], 78, treeId | (r() & 0xffff0000u), w);
    RetValue(&w.fk[kOwner], 12, identity, w);
    RetObject(&w.fk[kOwner], 23, r() % 6 == 0 ? nullptr : &w.fk[kTree], w);
    // The tree's flags: bit 0 set (no cache), or two different reads (before and after the preparation).
    EnqueueValue(&w.fk[kTree], 16, (r() % 3 == 0) ? 1u | (r() & 0xffffff7eu) : (r() & 0xffffff7eu), w);
    EnqueueValue(&w.fk[kTree], 16, r() & 0xffffff7eu, w);
    EnqueueValue(&w.fk[kTree], 16, r(), w);

    const uint32_t pick = r() % 4;
    const uint32_t extra = r() % 5;
    gBeforeRun = [=](WorldP &) {
        // Cache contents: sometimes the key (a hit), sometimes only other entries.
        std::mt19937 q(seed + 99);
        Cache *cache = new (GameAddress<Cache>(kCache)) Cache();
        if (pick == 0) cache->emplace(KeyFor(treeId, identity), static_cast<uint8_t>(q()));
        for (uint32_t i = 0; i < extra; ++i) cache->emplace((static_cast<uint64_t>(q()) << 32) | (q() % 4 ? q() : 5), static_cast<uint8_t>(q()));
    };
    w.scratch[0] = static_cast<uint8_t>(r());
}

void Finish()
{
    Cache *cache = GameAddress<Cache>(kCache);
    gTrace.push_back(static_cast<uint32_t>(cache->size()));
    for (const auto &e : *cache) {
        gTrace.push_back(static_cast<uint32_t>(e.first));
        gTrace.push_back(static_cast<uint32_t>(e.first >> 32));
        gTrace.push_back(e.second);
    }
    cache->~Cache();
    std::memset(cache, 0, sizeof(Cache));
}

} // namespace

DIFF_TEST(Autonomy_InteractionTreeHasFlags, 0x516e00)
{
    AutonomyGlobals globals;
    auto original = ctx.Original<uint32_t(__stdcall *)(void *, uint32_t)>(0x516e00);
    uint32_t hits = 0, misses = 0, noTree = 0;
    RunSeeds(
        ctx, 12000, BuildFlags,
        [&](void *, void *, void *) {
            uint32_t result = 0;
            try {
                result = original(&gW->fk[kInteraction], gW->scratch[0] | 0xa5a5a500u) & 0xff;
            } catch (...) {
                Finish();
                throw;
            }
            Finish();
            return result;
        },
        [&](void *, void *, void *) {
            uint32_t result = 0;
            try {
                result = InteractionTreeHasFlags(reinterpret_cast<ifaces::cTSInteraction *>(&gW->fk[kInteraction]), gW->scratch[0] | 0xa5a5a500u) ? 1u : 0u;
            } catch (...) {
                Finish();
                throw;
            }
            Finish();
            return result;
        },
        [&](WorldP &w, const Trial &t) {
            const uint32_t treeOffset = OffsetIn(&w.fk[kTree], w);
            const std::vector<uint32_t> &tr = t.oa.trace;
            const bool read = std::find(tr.begin(), tr.end(), treeOffset) != tr.end();
            const bool prepared = std::find(tr.begin(), tr.end(), (34u << 8 | 1u)) != tr.end();
            ++(read ? (prepared ? misses : hits) : noTree);
        });
    if (!hits || !misses || !noTree) ctx.Fail("coverage: no tree %u read only %u prepared %u", noTree, hits, misses);
}
