// Differential test for CheckInteraction (0x515c80, src/simantics/FindBestActionCheck.cpp): runs one interaction's check tree for
// the person. Everything below it is real on both sides over the check world (tests/simantics_autonomy_checkworld.h).
#include "simantics_autonomy_checkworld.h"

namespace {

struct CheckCoverage {
    uint32_t accepted = 0, rejected = 0, recorded = 0, actionString = 0;
};

void FinishCheck(WorldP &w, CheckCoverage *cover, uint32_t result)
{
    PrimPersonObject *self = Person(w);
    if (cover) {
        cover->accepted += (result & 0xff) != 0;
        cover->rejected += (result & 0xff) == 0;
        cover->recorded += !self->mRejected.empty();
        cover->actionString += *GameAddress<uint8_t>(kGlobalActionString) != 0;
    }
    FinishCheckWorld(w);
}

void BuildCheck(WorldP &w, uint32_t seed)
{
    BuildCheckWorld(w, seed, 0x515c80);
}

} // namespace

DIFF_TEST(Autonomy_CheckInteraction, 0x515c80)
{
    AutonomyGlobals globals;
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x515c80);
    CheckCoverage cover;
    RunSeeds(
        ctx, 30000, BuildCheck,
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = original(o, &Interaction(*gW, gSeed)) & 0xff;
            } catch (...) {
                FinishCheck(*gW, nullptr, 0xbad);
                throw;
            }
            FinishCheck(*gW, &cover, result);
            return result;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = reinterpret_cast<PrimPersonObject *>(o)->CheckInteraction(&Interaction(*gW, gSeed)) ? 1u : 0u;
            } catch (...) {
                FinishCheck(*gW, nullptr, 0xbad);
                throw;
            }
            FinishCheck(*gW, nullptr, result);
            return result;
        },
        [&](WorldP &, const Trial &) {});
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (!cover.accepted || !cover.rejected || !cover.recorded || !cover.actionString)
        ctx.Fail("coverage: accepted %u rejected %u recorded %u action string %u", cover.accepted, cover.rejected, cover.recorded, cover.actionString);
}
