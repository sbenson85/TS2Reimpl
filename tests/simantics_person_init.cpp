// Differential test of cTSPerson::vf198 (0x51da50, PrimPersonObject::Initialise198), in the person primitives' recording
// world (tests/simantics_prims_person.h). The base (cTSObject::vf198 0x5342d0) and the two reserve workers (0x50f560 and
// 0x50f4d0, which have their own tests) are logging stand-ins on both sides; the vectors' pointers are made-up addresses
// (never dereferenced by the stand-ins) with the top bit set, equal in both worlds.
#include "simantics_prims_person.h"
#include "simantics/PersonVectors.h"
#include "hook/hooks.h"
#include "ts/cTSObject.h"

namespace {

void __fastcall BaseInitialise(void *self, void *, uint32_t argument)
{
    const uint32_t a[] = {argument};
    HelperHit(0x5342d0, self, 1, a);
}

void LogReserve(uint32_t rva, void *self, const uint32_t *count)
{
    Tick();
    gTrace.push_back(0x80000000u | rva);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(self)));
    gTrace.push_back(*count);
}

void __fastcall ReserveCandidates(void *self, void *, const uint32_t *count)
{
    LogReserve(0x50f560, self, count);
}

void __fastcall ReserveMotives(void *self, void *, const uint32_t *count)
{
    LogReserve(0x50f4d0, self, count);
}

void BuildInit(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x198);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);

    // The candidate vector's capacity around 512 (or none), the motive vector's around 2 (sometimes a limit below the start).
    auto *candidates = reinterpret_cast<ActionRecordVector *>(&self->mCandidates);
    const uint32_t capacities[] = {0, 1, 100, 510, 511, 512, 513, 600};
    const uint32_t capacity = capacities[r() % 8];
    const uintptr_t first = 0x90000000u + (r() % 0x1000) * 16;
    candidates->mFirst = reinterpret_cast<ActionRecord *>(first);
    candidates->mEnd = candidates->mFirst + capacity;
    candidates->mLast = candidates->mFirst + (capacity ? r() % (capacity + 1) : 0);
    const int motiveCapacity = static_cast<int>(r() % 5) - 1; // -1..3
    self->mMotiveBegin = reinterpret_cast<uint8_t *>(0xa0000000u + (r() % 0x1000) * 16);
    self->mMotiveLimit = self->mMotiveBegin + motiveCapacity * 16;
    self->mMotiveEnd = self->mMotiveBegin;

    // +0x48 gives the helper (kF1), +0x40 the person list (kF3).
    self->mHolder48 = &w.fk[kF0];
    self->mEnvironment = &w.fk[kF2];
    Use(w.vtFk[kF0], 65, 0);
    RetObject(&w.fk[kF0], 65, &w.fk[kF1], w);
    Use(w.vtFk[kF1], 2, 0);
    Use(w.vtFk[kF1], 3, 0);
    RetValue(&w.fk[kF1], 2, NoiseAl(r, r() % 2 == 0), w);
    Use(w.vtFk[kF2], 6, 0);
    RetObject(&w.fk[kF2], 6, &w.fk[kF3], w);
    Use(w.vtFk[kF3], 28, 1);
    Use(w.vt60c, 4, 1);
    Use(w.vtPerson, 166, 0);
}

bool Always(const Outcome &)
{
    return true; // void: eax is not a result
}

} // namespace

DIFF_TEST(PersonInit_Initialise198, 0x51da50)
{
    CodePatch base(ctx, 0x5342d0, reinterpret_cast<void *>(&BaseInitialise));
    CodePatch baseOurs(MemberAddress(&cTSObject::vf198), reinterpret_cast<void *>(&BaseInitialise));
    CodePatch candidates(ctx, 0x50f560, reinterpret_cast<void *>(&ReserveCandidates));
    CodePatch candidatesOurs(reinterpret_cast<void *>(&ActionRecordsReallocateExactly), reinterpret_cast<void *>(&ReserveCandidates));
    CodePatch motives(ctx, 0x50f4d0, reinterpret_cast<void *>(&ReserveMotives));
    CodePatch motivesOurs(reinterpret_cast<void *>(&MotiveReallocateExactly), reinterpret_cast<void *>(&ReserveMotives));
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x51da50);
    uint32_t reservedCandidates = 0, reservedMotives = 0, prepared = 0, injected = 0, completed = 0;
    RunSeeds(
        ctx, 20000, BuildInit,
        [&](uint8_t *o, void *, const void *p) {
            original(o, *static_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            reinterpret_cast<PrimPersonObject *>(o)->Initialise198(*static_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            const Outcome &o = t.oa;
            if (o.thrown >= 0) ++injected;
            else ++completed;
            for (size_t i = 0; i + 2 < o.trace.size(); ++i) {
                if (o.trace[i] == 0x8050f560u) ++reservedCandidates;
                if (o.trace[i] == 0x8050f4d0u) ++reservedMotives;
                if (o.trace[i] == (3u << 8 | 0u)) ++prepared; // the helper's slot 3 (logged as slot << 8 | argument count)
            }
        },
        &Always);
    if (reservedCandidates < 5000 || reservedMotives < 5000 || completed < 10000 || injected < 300)
        ctx.Fail("coverage: reserved %u/%u completed %u injected %u (prepare seen %u)", reservedCandidates, reservedMotives, completed, injected, prepared);
}
