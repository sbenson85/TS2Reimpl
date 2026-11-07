// Differential tests for the trace log members of the FindBestAction search (src/simantics/SimTraceLogChecktree.cpp):
// SimTraceLog::CloseSink 0x526b40.
#include "simantics_autonomy_world.h"

#include "simantics/SimTraceLog.h"

namespace {

constexpr int kSink = kF0;
constexpr uint32_t kLogOffset = 0x40;

void BuildLog(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 41);
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    Use(w.vtFk[kSink], 21, 1);
    SimTraceLog *log = reinterpret_cast<SimTraceLog *>(w.scratch + kLogOffset);
    log->mActive = static_cast<uint8_t>(r() % 3 == 0 ? 0 : 1 + r() % 200);
    log->mSink = (r() % 3 == 0) ? nullptr : static_cast<void *>(&w.fk[kSink]);
}

} // namespace

DIFF_TEST(Autonomy_SimTraceLog_CloseSink, 0x526b40)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x526b40);
    uint32_t withSink = 0, without = 0;
    RunSeeds(
        ctx, 2000, BuildLog,
        [&](void *, void *, void *) {
            original(gW->scratch + kLogOffset);
            return 0u;
        },
        [&](void *, void *, void *) {
            reinterpret_cast<SimTraceLog *>(gW->scratch + kLogOffset)->CloseSink();
            return 0u;
        },
        [&](WorldP &, const Trial &t) { (t.oa.trace.empty() ? without : withSink)++; });
    if (!withSink || !without) ctx.Fail("coverage: sink %u no sink %u", withSink, without);
}
