// Differential test for StartChosenAction (0x515270): the chosen action becomes the person's next action. The person's
// recent-action memory, its action name and the chosen records are real heap objects, described and destroyed after each run;
// the objects it asks (cITSPerson, the person's own vtable, the tree frame, the object table with a stack object, its owner and the
// owner's tracker) are recording fakes. Cases the original crashes on (a missing stack object or tracker) are not generated.
#include "simantics_autonomy_records.h"

namespace {

enum : int { kFrame = kF0, kStackObject, kOwner, kTracker, kHost, kMain, kOtherOwner };

constexpr uint32_t kTableOffset = 0x200;
constexpr uint32_t kGlobalMinimumScore = 0x1014514;
constexpr uint32_t kGlobalTicks = 0x1014528;

// QueryInterface of the owner (slot 0): logs the interface id, always hands the tracker out.
uint32_t __fastcall OwnerQueryFake(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    *out = &gW->fk[kTracker];
    return Scripted(Off(self), 0);
}

// The stack object's slot 280 (GetResolvedObject): a vtable of its own, since the harness's fakes have 256 slots.
uint32_t __fastcall GetOwnerFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(280u << 8);
    return Scripted(Off(self), 280);
}
void *gStackObjectVt[320] = {};

void BuildStart(WorldP &w, uint32_t seed)
{
    gStackObjectVt[280] = reinterpret_cast<void *>(&GetOwnerFake);
    std::mt19937 r(seed * 2654435761u + 53);
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    QuietBuild quiet;

    PrimPersonObject *self = Person(w);
    Use(w.vtPerson, 50, 0);
    Use(w.vtPerson, 10, 2);
    Use(w.vtPerson, 11, 3);
    Use(w.vtThis, 14, 2);
    self->mFrame = &w.fk[kFrame];
    Use(w.vtFk[kFrame], 13, 1);
    w.fk[kStackObject].vt = gStackObjectVt; // (slot 280 is past the harness's 256-slot fake vtables)
    w.vtFk[kOwner][0] = reinterpret_cast<void *>(&OwnerQueryFake);
    w.vtFk[kOtherOwner][0] = reinterpret_cast<void *>(&OwnerQueryFake);
    for (int owner : {kOwner, kOtherOwner}) Use(w.vtFk[owner], 104, 0);
    Use(w.vtFk[kTracker], 6, 0);
    Use(w.vtFk[kTracker], 2, 0);
    Use(w.vtFk[kHost], 11, 0);
    Use(w.vtFk[kMain], 47, 0);
    for (int i = 0; i < kHeldCount; ++i) {
        Use(w.vtFk[kHeld + i], 1, 0);
        Use(w.vtFk[kHeld + i], 2, 0);
    }
    RetValue(&self->mPersonVTable, 50, (r() % 3 == 0) ? 0u : (r() % 2000) + 1, w);
    RetObject(&w.fk[kStackObject], 280, r() % 2 ? &w.fk[kOwner] : &w.fk[kOtherOwner], w);
    RetValue(&w.fk[kOwner], 104, NoiseAl(r, r() % 2), w);
    RetValue(&w.fk[kOtherOwner], 104, NoiseAl(r, r() % 2), w);
    RetObject(&w.fk[kTracker], 6, &w.fk[kHost], w);
    RetObject(&w.fk[kHost], 11, &w.fk[kMain], w);
    RetValue(&w.fk[kMain], 47, Edge16(r), w);

    // The object table: number 1..3 hold the stack object, number 2 is empty.
    void **table = reinterpret_cast<void **>(w.scratch + kTableOffset);
    table[0] = &w.fk[kStackObject];
    table[1] = nullptr;
    table[2] = &w.fk[kStackObject];
    auto *registry = reinterpret_cast<ObjectRegistry *>(new uint8_t[sizeof(ObjectRegistry)]());
    registry->mObjectsBegin = table;
    registry->mObjectsEnd = table + 3;
    self->mObjectRegistry = registry;

    new (&self->mActionName) EmbeddedString();
    self->mActionName.mText = (r() % 2) ? "old name" : "";
    self->mSearchTick = static_cast<int32_t>(r() % 100000);
    self->mHasChoice = static_cast<uint8_t>(r() % 2);
    self->mActionParameterA = r();
    self->mActionParameterB = r();
    self->mActionParameterC = static_cast<int32_t>(r());
    FillRandom(reinterpret_cast<uint8_t *>(self->mActionArguments), sizeof(self->mActionArguments), r);

    std::vector<ActionRecord> &choices = Fresh(self->mChoices);
    for (int i = 0; i < 3; ++i) {
        ActionRecord record = RandomAction(r, w);
        // Stack object numbers 1 and 3 exist; others (2: an empty slot, 0, 4: out of range) are never chosen (the original crashes).
        record.mStackObjectId = (r() % 2) ? 1 : 3;
        if (r() % 4 == 0) record.mArguments[3] = 0xffff;
        if (r() % 6 == 0) record.mScore = (r() % 2) ? std::nanf("") : 0.1f;
        choices.push_back(record);
    }
    self->mChosen = (r() % 8 == 0) ? nullptr : &choices[r() % 3];
    RecentActionVector &recent = Fresh(self->mRecentActions);
    const uint32_t n = r() % 5;
    for (uint32_t i = 0; i < n; ++i) {
        RecentAction a;
        a.mStackObjectId = static_cast<uint16_t>(r());
        a.mEntryIndex = static_cast<uint16_t>(r());
        a.mExpiryTick = static_cast<int32_t>(r() % 5000);
        a.mName.mText = (r() % 2) ? "earlier" : "";
        recent.push_back(a);
    }
    recent.shrink_to_fit();

    const uint32_t ticks = r() % 7000;
    const uint32_t scoreBits = Bits((r() % 3 == 0) ? 0.1f : 0.1f + static_cast<float>(static_cast<int32_t>(r() % 7) - 3) * 0.05f);
    gBeforeRun = [=](WorldP &) {
        *GameAddress<uint32_t>(kGlobalTicks) = ticks;
        *GameAddress<uint32_t>(kGlobalMinimumScore) = scoreBits;
    };
}

struct StartCoverage {
    uint32_t started = 0, refused = 0, noChoice = 0, tracked = 0, growth = 0;
};

void FinishStart(WorldP &w, StartCoverage *cover, uint32_t result)
{
    PrimPersonObject *self = Person(w);
    if (cover) {
        cover->started += result == 1;
        cover->refused += result == 0 && self->mChosen != nullptr;
        cover->noChoice += self->mChosen == nullptr;
    }
    std::vector<uint32_t> d;
    DescribeVector(self->mChoices, d);
    DescribeVector(self->mRecentActions, d);
    d.push_back(TextCode(self->mActionName.mText));
    d.push_back(self->mActionParameterA);
    d.push_back(self->mActionParameterB);
    d.push_back(static_cast<uint32_t>(self->mActionParameterC));
    for (int i = 0; i < 8; i += 2) d.push_back(self->mActionArguments[i] | (static_cast<uint32_t>(self->mActionArguments[i + 1]) << 16));
    d.push_back(self->mHasChoice);
    d.push_back(self->mChosen ? static_cast<uint32_t>(self->mChosen - self->mChoices.data()) : 0xdeadu);
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    Retire(self->mChoices);
    Retire(self->mRecentActions);
    self->mActionName.~EmbeddedString();
    std::memset(&self->mActionName, 0, sizeof(self->mActionName));
    delete[] reinterpret_cast<uint8_t *>(self->mObjectRegistry);
    self->mObjectRegistry = nullptr;
    self->mChosen = nullptr;
}

} // namespace

DIFF_TEST(Autonomy_StartChosenAction, 0x515270)
{
    AutonomyGlobals globals;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x515270);
    GameAllocatorFixture allocator;
    StartCoverage cover;
    RunSeeds(
        ctx, 12000, BuildStart,
        [&](void *obj, void *, void *) {
            uint32_t result = 0xbad;
            try {
                result = original(obj);
            } catch (...) {
                FinishStart(*gW, nullptr, 0xbad);
                throw;
            }
            FinishStart(*gW, &cover, result);
            return result;
        },
        [&](void *obj, void *, void *) {
            uint32_t result = 0xbad;
            try {
                result = reinterpret_cast<PrimPersonObject *>(obj)->StartChosenAction();
            } catch (...) {
                FinishStart(*gW, nullptr, 0xbad);
                throw;
            }
            FinishStart(*gW, nullptr, result);
            return result;
        },
        [&](WorldP &, const Trial &) {});
    if (!cover.started || !cover.refused || !cover.noChoice)
        ctx.Fail("coverage: started %u refused %u no choice %u", cover.started, cover.refused, cover.noChoice);
}
