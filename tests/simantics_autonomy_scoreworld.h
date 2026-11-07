#pragma once
// The score world of the FindBestAction tests: the check world (tests/simantics_autonomy_checkworld.h) with the fakes the
// scoring step (ScoreInteraction 0x516160, ScorePending 0x516c40) reaches added: the interactions' name, arguments, group and
// parameter slots, the table entries' ad / recent-use slots, the person's scale and factor slots (floats), the motives object
// (the person's +0x60c), the world clock, the kind list with its entry and preference table, the object currently scoring, and
// a stack object interface query that sometimes answers.
#include "simantics_autonomy_checkworld.h"

#include "gz/cIGZString.h"

#include <algorithm>
#include <map>

namespace {

constexpr int kScoringObject = kFrame; // (the tree frame of the idle test; free here)
constexpr int kClock = kActorObject;
constexpr int kKindList = kHolder;
constexpr int kKindEntry = kRecorder;
constexpr uint32_t kArgumentsOffset = 0x380; // eight argument bytes the interactions hand out
constexpr uint32_t kTableAdsOffset = 0x3a0;   // (unused: ads vectors are built by the fakes below)

// Stack object slot 0 (interface id, out): the tracker chain or nothing in *out, and a success byte independent of it.
uint32_t __fastcall ScoreQueryFake(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    *out = (iid == 0x5a6216f6u) ? &gW->fk[kChain] : reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(Off(self), 0)));
    // (A success without an interface would crash the original; the answer is zero then. The reverse, an interface with a zero
    // answer, is generated: the caller releases it.)
    const uint32_t success = Scripted(Off(self), 0x1000 | (iid & 0xff));
    return *out ? success : 0u;
}

// Interaction slot 73 (second, first): writes the two parameter words the script gives.
uint32_t __fastcall GetParametersFake(void *self, void *, uint32_t *second, uint32_t *first)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(73u << 8 | 2u);
    *second = Scripted(Off(self), 73);
    *first = Mix(Scripted(Off(self), 73), 1, 2);
    return 0;
}

// Interaction slot 15: the address of eight argument bytes in the world.
uint32_t __fastcall GetArgumentsFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(15u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->scratch + kArgumentsOffset));
}

// Interaction slot 25: the world's name object.
uint32_t __fastcall GetNameObjectFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(25u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->scratch + kNameObjectOffset));
}

// Stack object slot 42 (out): fills the 12-byte buffer from the seed and returns it.
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

// An entry's slots 60 / 61 (out vector, life stage): fill the caller's vector with ads from the script and return it (or another
// vector, to exercise the move-from-the-returned-pointer path).
std::vector<TreeTableAd> gOtherTableAds;
std::vector<std::vector<TraitAdValue>> gOtherTraitAds;
uint32_t __fastcall GetTableAdsFake(void *self, void *, void *out, uint32_t lifeStage)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(60u << 8 | 2u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    gTrace.push_back(lifeStage);
    auto *vec = new (out) std::vector<TreeTableAd>();
    const uint32_t n = Scripted(Off(self), 60) % 3;
    for (uint32_t i = 0; i < n; ++i) {
        TreeTableAd ad;
        ad.mFirst = Mix(gSeed, 60, i);
        ad.mSecond = static_cast<uint16_t>(Mix(gSeed, 61, i));
        vec->push_back(ad);
    }
    return reinterpret_cast<uint32_t>(vec);
}
uint32_t __fastcall GetTraitAdsFake(void *self, void *, void *out, uint32_t lifeStage)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(61u << 8 | 2u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    gTrace.push_back(lifeStage);
    auto *vec = new (out) std::vector<std::vector<TraitAdValue>>();
    const uint32_t n = Scripted(Off(self), 61) % 3;
    for (uint32_t i = 0; i < n; ++i) {
        std::vector<TraitAdValue> inner;
        for (uint32_t k = 0; k < (i + Scripted(Off(self), 61)) % 3; ++k) {
            TraitAdValue v;
            v.mFirst = Mix(gSeed, 62, i * 4 + k);
            v.mSecond = static_cast<uint16_t>(Mix(gSeed, 63, i * 4 + k));
            inner.push_back(v);
        }
        vec->push_back(std::move(inner));
    }
    return reinterpret_cast<uint32_t>(vec);
}

// Debugging aid: runs `thunk(arg)` once and, if it crashes, prints where the call that crashed came from (the return address of a
// call through a null vtable slot) relative to the image base, and the first stack words.
void DiagnoseCrash(void (*thunk)(void *), void *arg, uintptr_t base)
{
    __try {
        thunk(arg);
    } __except (std::fprintf(stderr, "crash at %#lx (rva %#lx), fault address %#lx, caller rva %#lx, esp words %#lx %#lx %#lx\n",
                             static_cast<unsigned long>(GetExceptionInformation()->ContextRecord->Eip),
                             static_cast<unsigned long>(GetExceptionInformation()->ContextRecord->Eip - base),
                             static_cast<unsigned long>(GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1]),
                             static_cast<unsigned long>(*reinterpret_cast<uintptr_t *>(GetExceptionInformation()->ContextRecord->Esp) - base),
                             static_cast<unsigned long>(reinterpret_cast<uintptr_t *>(GetExceptionInformation()->ContextRecord->Esp)[1]),
                             static_cast<unsigned long>(reinterpret_cast<uintptr_t *>(GetExceptionInformation()->ContextRecord->Esp)[2]),
                             static_cast<unsigned long>(reinterpret_cast<uintptr_t *>(GetExceptionInformation()->ContextRecord->Esp)[3])),
                EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Replaces the fixed answer of (object, slot): drops the ones the earlier setup gave, then adds the new one.
void DropFixed(const void *object, int slot, WorldP &w)
{
    const uint32_t id = OffsetIn(object, w);
    gRets.erase(std::remove_if(gRets.begin(), gRets.end(), [&](const RetEntry &e) { return e.id == id && e.slot == slot; }), gRets.end());
}
void OverrideObject(WorldP &w, const void *object, int slot, const void *target)
{
    DropFixed(object, slot, w);
    RetObject(object, slot, target, w);
}
void OverrideValue(WorldP &w, const void *object, int slot, uint32_t value)
{
    DropFixed(object, slot, w);
    RetValue(object, slot, value, w);
}

// Builds the check world, then the scoring fakes. `salt` keeps the worlds of different tests apart.
void BuildScoreWorld(WorldP &w, uint32_t seed, uint32_t salt)
{
    BuildCheckWorld(w, seed, salt);
    std::mt19937 r(seed * 2654435761u + 0x5c0);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);

    // The person.
    Use(w.vtPerson, 38, 0);
    RetValue(&self->mPersonVTable, 38, NoiseAl(r, chance(40)), w);
    Use(w.vtPerson, 30, 0);
    RetValue(&self->mPersonVTable, 30, NoiseAl(r, chance(35)), w);
    Use(w.vtPerson, 20, 0);
    RetValue(&self->mPersonVTable, 20, r() % 17, w);
    Use(w.vtPerson, 8, 0);
    RetObject(&self->mPersonVTable, 8, &Fk(w, kActorObject), w);
    // (The person factor is the person's slot 366, past the harness's 256-slot vtable: the person gets a longer copy.)
    static void *bigVtable[400];
    UseFloat<128, 2>(w.vtThis);
    Use(w.vtThis, 188, 0);
    RetValue(w.obj, 188, NoiseAl(r, chance(70)), w);
    std::memcpy(bigVtable, w.vtThis, sizeof(w.vtThis));
    bigVtable[366] = reinterpret_cast<void *>(&FloatSlot<366, 1>::F);
    self->mVTable = bigVtable;
    RetValue(w.obj, 366, Edge16(r), w);
    RetValue(w.obj, 128, Edge16(r), w);
    RetValue(&self->mPersonVTable, 78, Edge16(r), w);
    RetValue(&self->mPersonVTable, 79, Edge16(r), w);
    // The motives object (the person's +0x60c).
    UseFloat<6, 0>(w.vt60c);
    UseFloat<7, 1>(w.vt60c);
    RetValue(w.obj + 0x60c, 6, Edge16(r), w);
    RetValue(w.obj + 0x60c, 7, Edge16(r), w);

    // The scoring object (+0x828): none, the person, or a fake that is sometimes not available.
    Fake &scoring = Fk(w, kScoringObject);
    scoring.vt = w.vtFk[kScoringObject];
    Use(scoring.vt, 188, 0);
    RetValue(&scoring, 188, NoiseAl(r, chance(70)), w);
    UseFloat<128, 2>(scoring.vt);
    RetValue(&scoring, 128, Edge16(r), w);
    const uint32_t scoringChoice = r() % 3;
    self->mScoringObject = scoringChoice == 0 ? nullptr : (scoringChoice == 1 ? static_cast<void *>(w.obj) : static_cast<void *>(&scoring));
    self->mUseNodeObject = 0;

    // The world clock, the kind list / entry / preferences.
    self->mClock = &Fk(w, kClock);
    Use(Fk(w, kClock).vt, 18, 0);
    RetValue(&Fk(w, kClock), 18, 100 + r() % 50, w);
    Use(Fk(w, kTarget).vt, 10, 0);
    RetObject(&Fk(w, kTarget), 10, &Fk(w, kKindList), w);
    Use(Fk(w, kKindList).vt, 16, 2);
    RetObject(&Fk(w, kKindList), 16, &Fk(w, kKindEntry), w);
    Use(Fk(w, kKindEntry).vt, 12, 0);
    RetObject(&Fk(w, kKindEntry), 12, &Fk(w, kRowTable), w);
    // (The preference table's cell answers are per call, from the world's script.)
    for (int i = 0; i < 10; ++i) EnqueueValue(&Fk(w, kRowTable), 9, chance(50) ? 0x800u | (r() & 0xf7ff) : (r() & 0xf7ff), w);
    for (int i = 0; i < 10; ++i) EnqueueValue(&Fk(w, kRowTable), 5, chance(40) ? r() % 2 : 2 + r() % 4, w);

    // The stack objects: the interface query and the chain behind it.
    Fake &chain = Fk(w, kChain);
    Use(chain.vt, 10, 2);
    RetValue(&chain, 10, (r() % 3 == 0) ? (r() & 0xffff0000u) | (r() % 6) : (r() % 6), w);
    for (int s = 0; s < kObjects; ++s) {
        Fake &f = Fk(w, kS0 + s);
        f.vt[0] = reinterpret_cast<void *>(&ScoreQueryFake);
        RetObject(&f, 0, chance(75) ? &chain : nullptr, w);
        // The success bytes of the two interface queries (the query is told apart by the interface id's low byte).
        RetValue(&f, 0x1000 | 0xd5, NoiseAl(r, chance(70)), w);
        RetValue(&f, 0x1000 | 0xf6, NoiseAl(r, chance(70)), w);
        Use(f.vt, 36, 0);
        f.vt[42] = reinterpret_cast<void *>(&GetDefinitionKeyFake);
        RetValue(&f, 36, Edge16(r), w);
    }

    // The stack objects' tree owner (the row table fake) hands out the check tree (the chain fake): its flags, before and after the
    // preparation, decide whether a gathered interaction is checked first.
    Use(Fk(w, kRowTable).vt, 12, 1);
    Use(Fk(w, kRowTable).vt, 23, 1);
    RetValue(&Fk(w, kRowTable), 12, chance(25) ? 0u : r(), w);
    RetObject(&Fk(w, kRowTable), 23, chance(10) ? nullptr : &Fk(w, kChain), w);
    Use(chain.vt, 16, 0);
    Use(chain.vt, 34, 1);
    {
        const uint32_t treeBits[] = {0, 2, 4, 6};
        for (int i = 0; i < 8; ++i) EnqueueValue(&chain, 16, (chance(40) ? 1u : 0u) | treeBits[r() % 4] | (chance(30) ? 8u : 0u), w);
    }

    // The score factor's services (0x512860): the resource service and BCON table (the scoring object and the tree sim fake play
    // them), the cached service D (the attributes fake) with its known set, the selection (the settings fake).
    w.vtFk[kScoringObject][72] = reinterpret_cast<void *>(&GetResourceFake);
    gTableFake = kTreeSim;
    RetValue(&Fk(w, kScoringObject), 72, (r() % 6 == 0 ? 0u : 1u) | 0x100u, w);
    Use(Fk(w, kTreeSim).vt, 2, 0);
    Use(Fk(w, kTreeSim).vt, 9, 0);
    Use(Fk(w, kTreeSim).vt, 11, 1);
    RetValue(&Fk(w, kTreeSim), 9, 20 + r() % 10, w);
    for (int i = 0; i < 12; ++i) EnqueueValue(&Fk(w, kTreeSim), 11, (r() % 5 == 0) ? (r() & 0xffff) : (r() % 301) - 100, w);
    Use(Fk(w, kAttributes).vt, 100, 0);
    RetObject(&Fk(w, kAttributes), 100, &Fk(w, kAttributes), w);
    Fk(w, kAttributes).vt[9] = reinterpret_cast<void *>(&ContainsFake);
    RetValue(&Fk(w, kAttributes), 9, r() % 3 == 0 ? 0u : 1u | (r() & 0xffffff00u), w);
    Use(Fk(w, kSettings).vt, 33, 0);
    RetObject(&Fk(w, kSettings), 33, r() % 5 == 0 ? nullptr : &Fk(w, kSettings), w);
    Use(Fk(w, kSettings).vt, 25, 0);
    Use(Fk(w, kSettings).vt, 27, 0);
    RetValue(&Fk(w, kSettings), 25, r() % 4, w);
    RetValue(&Fk(w, kSettings), 27, r() % 4, w);
    Use(Fk(w, kSettings).vt, 2, 0);

    // The interactions.
    for (int i = 0; i < kInteractions; ++i) {
        Fake &f = Fk(w, kI0 + i);
        Use(f.vt, 19, 0);
        Use(f.vt, 20, 0);
        Use(f.vt, 31, 0);
        Use(f.vt, 63, 0);
        f.vt[15] = reinterpret_cast<void *>(&GetArgumentsFake);
        f.vt[25] = reinterpret_cast<void *>(&GetNameObjectFake);
        f.vt[73] = reinterpret_cast<void *>(&GetParametersFake);
        RetValue(&f, 19, chance(50) ? r() & 0xffff0000u | 1 : r() & 0xffff0000u, w);
        RetValue(&f, 20, chance(40) ? (r() & 0xffff0000u) | 1 : r() & 0xffff0000u, w);
        RetValue(&f, 31, NoiseAl(r, chance(30)), w);
        RetObject(&f, 63, chance(50) ? nullptr : &Fk(w, kS0 + r() % kObjects), w);
        RetValue(&f, 73, r(), w);
    }

    // The table entries.
    for (int t = 0; t < kEntries; ++t) {
        Fake &f = Fk(w, kT0 + t);
        for (int slot : {8, 21, 82, 90}) Use(f.vt, slot, 0);
        for (int slot : {73, 74}) Use(f.vt, slot, 3);
        UseFloat<82, 0>(f.vt);
        f.vt[60] = reinterpret_cast<void *>(&GetTableAdsFake);
        f.vt[61] = reinterpret_cast<void *>(&GetTraitAdsFake);
        f.vt[90] = reinterpret_cast<void *>(&NameFake);
        RetValue(&f, 8, chance(30) ? (r() & 0xffff0000u) | (r() % 4) : (r() % 4), w);
        RetValue(&f, 21, NoiseAl(r, chance(70)), w);
        RetValue(&f, 74, NoiseAl(r, chance(25)), w);
        RetValue(&f, 82, Edge16(r), w);
        RetValue(&f, 60, r(), w);
        RetValue(&f, 61, r(), w);
    }
    // The person's own entry (the recorder fake) answers the same way.
    Fake &own = Fk(w, kKindEntry);
    for (int slot : {60, 61}) own.vt[slot] = (slot == 60) ? reinterpret_cast<void *>(&GetTableAdsFake) : reinterpret_cast<void *>(&GetTraitAdsFake);
    RetValue(&own, 60, r(), w);
    RetValue(&own, 61, r(), w);

    // The arguments the interactions hand out and the name object are in the scratch area.
    for (int i = 0; i < 8; ++i) w.scratch[kArgumentsOffset + i] = static_cast<uint8_t>(r());

    self->mRespectCurrentInteraction = static_cast<uint8_t>(r() % 2);
    self->mSameObjectAsCurrent = static_cast<uint8_t>(r() % 2);
    self->mSearchTick = 100 + static_cast<int32_t>(r() % 50);
    // The recent-action memory (real heap vector, the game's allocator).
    RecentActionVector &recent = Fresh(self->mRecentActions);
    const uint32_t n = r() % 4;
    for (uint32_t i = 0; i < n; ++i) {
        RecentAction a;
        // Mostly the ids of the world's stack objects (so the memory matches), their entry indexes and the tick edge.
        const uint32_t pick = r() % 3;
        a.mStackObjectId = r() % 4 == 0 ? static_cast<uint16_t>(r()) : static_cast<uint16_t>(Scripted(OffsetIn(&Fk(w, kS0 + pick), w), 47));
        a.mEntryIndex = static_cast<uint16_t>(r() % 4);
        a.mExpiryTick = r() % 3 == 0 ? self->mSearchTick : static_cast<int32_t>(80 + r() % 80);
        recent.push_back(a);
    }
    // Interaction 5 is on the person itself, with the last table entry, which is always "used lately" (so nothing past the
    // self-use test runs for it).
    OverrideObject(w, &Fk(w, kI0 + kInteractions - 1), 16, w.obj);
    OverrideObject(w, &Fk(w, kI0 + kInteractions - 1), 24, &Fk(w, kT0 + kEntries - 1));
    OverrideValue(w, &Fk(w, kT0 + kEntries - 1), 74, 1);
    Use(w.vtThis, 47, 0);

    // Tuning globals the scoring reads (the thresholds include zero and tiny, where only the minimum score decides).
    const uint32_t cappedProximity = r() % 2, noRecent = r() % 4 == 0;
    const float plainThresholds[] = {0.2f, 0.0f, 1.0e-6f, 0.2f};
    const float lowThresholds[] = {0.05f, 0.0f, 0.05f, 1.0e-6f};
    const uint32_t thresholdBits = Bits(plainThresholds[r() % 4]), lowBits = Bits(lowThresholds[r() % 4]), capBits = Bits(2.0f);
    auto base = gBeforeRun;
    const uint32_t optionMask = (r() % 3 == 0) ? 0x1080u : 0x1000u | (r() & 0x7fu);
    const uint8_t factorReady = r() % 4 == 0;
    gBeforeRun = [=](WorldP &world) {
        base(world);
        new (GameAddress<std::map<uint64_t, uint8_t>>(0x10dac2c)) std::map<uint64_t, uint8_t>(); // the tree flag cache (0x516e00)
        *GameAddress<void *>(0x10cc290) = &world.fk[kScoringObject];
        *GameAddress<void *>(0x10cc45c) = &world.fk[kAttributes];
        *GameAddress<uint32_t>(0x10faf94) = optionMask;
        *GameAddress<uint8_t>(0x10dac5a) = factorReady;
        float *table = GameAddress<float>(0x10dac68);
        for (int i = 0; i < 16; ++i) table[i] = factorReady ? 0.25f * (i + 1) : 1.0f;
        *GameAddress<int32_t>(0x10dac40) = static_cast<int32_t>(cappedProximity);
        *GameAddress<int32_t>(0x10dac44) = static_cast<int32_t>(noRecent);
        *GameAddress<uint32_t>(0x101450c) = thresholdBits;
        *GameAddress<uint32_t>(0x1014510) = lowBits;
        *GameAddress<uint32_t>(0x1014530) = capBits;
        // The Sim interface of the score factor, the cached services and the BCON service as the other tests set them.
    };
}

} // namespace
