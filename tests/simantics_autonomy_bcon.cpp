// Differential tests for the BCON helpers the autonomy code uses (src/simantics/BconTable.cpp): GetBconTable 0x54a770 and
// GetBconValue 0x54a6b0. The resource service (the cached singleton C, game global 0x10cc290) and the table it hands out are
// recording fakes of the person harness; the key the service receives is logged word by word.
#include "simantics_autonomy_world.h"

namespace {

constexpr int kService = kF0;
constexpr int kTable = kF1;
constexpr uint32_t kCacheC = kGlobalCachedC;

void Build(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 5);
    gTableFake = kTable;
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    w.vtFk[kService][72] = reinterpret_cast<void *>(&GetResourceFake);
    Use(w.vtFk[kTable], 2, 0);
    Use(w.vtFk[kTable], 9, 0);
    Use(w.vtFk[kTable], 11, 1);
    const int32_t counts[] = {0, 1, 2, 5, 100, -1};
    RetValue(&w.fk[kTable], 9, static_cast<uint32_t>(counts[r() % 6]), w);
    RetValue(&w.fk[kService], 72, (r() % 4 == 0 ? 0u : 1u) | ((r() % 3) << 8) | (r() & 0xffff0000u), w);
    gBeforeRun = [](WorldP &world) { *GameAddress<void *>(kCacheC) = &world.fk[kService]; };
}

int32_t PickIndex(uint32_t seed)
{
    const int32_t picks[] = {0, 1, 4, 5, 99, 100, -1, -1000, 0x7fffffff, 3};
    return picks[seed % 10];
}

} // namespace

DIFF_TEST(Bcon_GetBconTable, 0x54a770)
{
    AutonomyGlobals globals;
    auto original = ctx.Original<uint8_t(__cdecl *)(uint32_t, uint32_t, void **)>(0x54a770);
    uint32_t outcomes[2] = {};
    RunSeeds(
        ctx, 4000, Build,
        [&](void *, void *, void *) {
            auto *w = gW;
            return static_cast<uint32_t>(original(0x100 + w->scratch[0], 0x5b267c89 ^ w->scratch[1], reinterpret_cast<void **>(w->scratch + 8)));
        },
        [&](void *, void *, void *) {
            auto *w = gW;
            return static_cast<uint32_t>(GetBconTable(0x100 + w->scratch[0], 0x5b267c89 ^ w->scratch[1], reinterpret_cast<void **>(w->scratch + 8)));
        },
        [&](WorldP &, const Trial &t) { ++outcomes[t.oa.ret != 0]; });
    if (outcomes[0] == 0 || outcomes[1] == 0) ctx.Fail("the cases do not reach both answers (%u/%u)", outcomes[0], outcomes[1]);
}

DIFF_TEST(Bcon_GetBconValue, 0x54a6b0)
{
    AutonomyGlobals globals;
    auto original = ctx.Original<uint8_t(__cdecl *)(uint32_t, int32_t, uint16_t *)>(0x54a6b0);
    uint32_t outcomes[2] = {};
    uint32_t thrown = 0;
    RunSeeds(
        ctx, 6000, Build,
        [&](void *, void *, void *) {
            auto *w = gW;
            return static_cast<uint32_t>(original(0x141 + w->scratch[0] % 3, PickIndex(gSeed), reinterpret_cast<uint16_t *>(w->scratch + 8)));
        },
        [&](void *, void *, void *) {
            auto *w = gW;
            return static_cast<uint32_t>(GetBconValue(0x141 + w->scratch[0] % 3, PickIndex(gSeed), reinterpret_cast<uint16_t *>(w->scratch + 8)));
        },
        [&](WorldP &, const Trial &t) {
            ++outcomes[t.oa.ret != 0];
            thrown += t.oa.thrown >= 0;
        });
    if (outcomes[0] == 0 || outcomes[1] == 0) ctx.Fail("the cases do not reach both answers (%u/%u)", outcomes[0], outcomes[1]);
    if (thrown == 0) ctx.Fail("no case throws from inside the service or the table");
}
