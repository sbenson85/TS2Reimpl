// Differential tests for the small helpers of the autonomy code (src/simantics/AutonomyHelpers.cpp): the stack object score
// factor 0x512860. Every service it reaches (the option mask, the BCON resource service, the service object that hands out
// the cached services, the known-object set, the selection) is a recording fake; the factor table and its ready byte are game
// globals, reset before and compared after each run.
#include "simantics_autonomy_world.h"

#include "simantics/AutonomyHelpers.h"

namespace {

enum : int { kBconService = kF0, kBconTable, kServiceObject, kServiceD, kKnownSet, kSelectionHolder, kSelection, kStackObject };

constexpr uint32_t kTableBase = 0x10dac68;
constexpr uint32_t kTableReady = 0x10dac5a;
constexpr uint32_t kCachedD = 0x10cc45c;

// Stack object slot 42 (out): fills the 12-byte buffer from the script and returns it.
uint32_t __fastcall GetDefinitionKeyFake(void *self, void *, uint8_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(42u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    for (int i = 0; i < 12; ++i) out[i] = static_cast<uint8_t>(Mix(gSeed, 42, i));
    return reinterpret_cast<uint32_t>(out);
}

// Known set slot 9 (key): logs the 12 bytes behind the pointer.
uint32_t __fastcall ContainsFake(void *self, void *, const uint32_t *key)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(key)));
    for (int i = 0; i < 3; ++i) gTrace.push_back(key[i]);
    return Scripted(Off(self), 9);
}

void Build(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 11);
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    gTableFake = kBconTable;
    gHandsOut = {{0x4a037ee3u, kServiceD}, {0x7a3e667du, kSelectionHolder}};

    w.vtFk[kBconService][72] = reinterpret_cast<void *>(&GetResourceFake);
    Use(w.vtFk[kBconTable], 2, 0);
    Use(w.vtFk[kBconTable], 9, 0);
    Use(w.vtFk[kBconTable], 11, 1);
    w.vtFk[kServiceObject][5] = reinterpret_cast<void *>(&AcquireFake);
    Use(w.vtFk[kServiceD], 2, 0);
    Use(w.vtFk[kServiceD], 100, 0);
    w.vtFk[kKnownSet][9] = reinterpret_cast<void *>(&ContainsFake);
    Use(w.vtFk[kSelectionHolder], 2, 0);
    Use(w.vtFk[kSelectionHolder], 33, 0);
    Use(w.vtFk[kSelection], 25, 0);
    Use(w.vtFk[kSelection], 27, 0);
    w.vtFk[kStackObject][42] = reinterpret_cast<void *>(&GetDefinitionKeyFake);

    RetValue(&w.fk[kBconTable], 9, 20 + r() % 10, w);
    RetValue(&w.fk[kBconService], 72, r() % 6 == 0 ? 0u : (1u | 0x100u), w);
    RetObject(&w.fk[kServiceD], 100, &w.fk[kKnownSet], w);
    RetValue(&w.fk[kKnownSet], 9, r() % 3 == 0 ? 0u : 1u | (r() & 0xffffff00u), w);
    RetObject(&w.fk[kSelectionHolder], 33, r() % 5 == 0 ? nullptr : &w.fk[kSelection], w);
    RetValue(&w.fk[kSelection], 25, r() % 4, w);
    RetValue(&w.fk[kSelection], 27, r() % 4, w);
    for (int i = 0; i < 12; ++i) EnqueueValue(&w.fk[kBconTable], 11, (r() % 5 == 0) ? (r() & 0xffff) : (r() % 301) - 100, w);

    gBeforeRun = [seed](WorldP &world) {
        *GameAddress<void *>(kGlobalServiceObject) = &world.fk[kServiceObject];
        *GameAddress<void *>(kGlobalCachedC) = &world.fk[kBconService];
        *GameAddress<void *>(kCachedD) = (seed % 3 == 0) ? static_cast<void *>(&world.fk[kServiceD]) : nullptr;
        *GameAddress<void *>(kGlobalSingletonA) = (seed % 5 == 0) ? static_cast<void *>(&world.fk[kSelectionHolder]) : nullptr;
        *GameAddress<uint32_t>(kGlobalOptionMask) = (seed % 7 == 0) ? 0x1080u : 0x1000u | (seed & 0x7fu);
        const uint8_t ready = (seed % 4 == 0) ? 1 : 0;
        *GameAddress<uint8_t>(kTableReady) = ready;
        float *table = GameAddress<float>(kTableBase);
        for (int i = 0; i < 16; ++i) table[i] = ready ? 0.25f * (i + 1) : static_cast<float>(Mix(seed, i, 3) & 0xff);
    };
}

// Copies the factor table and its ready byte into the compared scratch area after the call.
void SaveGlobals(WorldP &w)
{
    std::memcpy(w.scratch, GameAddress<float>(kTableBase), 64);
    w.scratch[64] = *GameAddress<uint8_t>(kTableReady);
    w.scratch[65] = 0;
    uint32_t cached = Norm(reinterpret_cast<uint32_t>(*GameAddress<void *>(kCachedD)));
    std::memcpy(w.scratch + 68, &cached, 4);
    cached = Norm(reinterpret_cast<uint32_t>(*GameAddress<void *>(kGlobalSingletonA)));
    std::memcpy(w.scratch + 72, &cached, 4);
}

} // namespace

DIFF_TEST(Autonomy_GetStackObjectScoreFactor, 0x512860)
{
    AutonomyGlobals globals;
    auto original = ctx.Original<float(__cdecl *)(void *)>(0x512860);
    uint32_t seen[3] = {};
    RunSeeds(
        ctx, 6000, Build,
        [&](void *, void *, void *) {
            const float v = original(&gW->fk[kStackObject]);
            SaveGlobals(*gW);
            return Bits(v);
        },
        [&](void *, void *, void *) {
            const float v = GetStackObjectScoreFactor(reinterpret_cast<ifaces::cTSObject *>(&gW->fk[kStackObject]));
            SaveGlobals(*gW);
            return Bits(v);
        },
        [&](WorldP &, const Trial &t) { ++seen[t.oa.ret == Bits(1.0f) ? 0 : 1]; });
    if (seen[0] == 0 || seen[1] == 0) ctx.Fail("the cases do not reach both a table factor and 1.0 (%u/%u)", seen[0], seen[1]);
}
