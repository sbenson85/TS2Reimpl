// Differential tests for the start of the person's FindBestAction search (src/simantics/FindBestActionBegin.cpp): BeginSearch
// 0x515940 and the check-tree log opener SimTraceLog::LogSearchStart 0x5273b0. The file class the log opener creates its sink with
// (0x1b4dd2) is a recording stand-in in the mapped image, so both sides reach the same function. The person's vectors are real
// heap vectors, described and destroyed after each run.
#include "simantics_autonomy_scoreworld.h"

#include "simantics/SimTraceLog.h"
#include "code_patch.h"
#include "rz/RZFileStreams.h"

namespace {

constexpr uint32_t kSnapshotSourceOffset = 0x240;
constexpr uint32_t kGlobalRecentLimit = 0x1014524;
constexpr int kLogSink = kSink;

// Person slot 92 (the name): the world's name bytes.
uint32_t __fastcall PersonNameFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(92u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->scratch + 0x100));
}

// The motives object's slot 9 (out): fills the snapshot buffer from the seed.
uint32_t __fastcall CopySnapshotFake(void *self, void *, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    for (int i = 0; i < 35; ++i) out[i] = Mix(gSeed, 9, i) | 0x80000000u; // (no word can equal an address of the world)
    return 0;
}

// The stand-in for the game's text file class constructor (0x1b4dd2): logs the file name and hands the log sink fake back.
uint32_t __fastcall FileClassStandIn(void *, void *, const cRZString *name, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Tick();
    gTrace.push_back(0x801b4dd2u);
    gTrace.push_back(TextCode(name->mString));
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    gTrace.push_back(d);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->fk[kLogSink]));
}

// The sink's WriteText: the text is compared, not its address.
uint32_t __fastcall SinkText(void *self, void *, const char *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 1u);
    gTrace.push_back(text ? TextCode(text) : 0u);
    return 0;
}

void FillRefVector(RefPtrVector &v, WorldP &w, std::mt19937 &r, uint32_t maxCount)
{
    const uint32_t n = r() % (maxCount + 1);
    void **block = new void *[n + 2];
    for (uint32_t i = 0; i < n; ++i) block[i] = &Interaction(w, r());
    v.mFirst = block;
    v.mLast = block + n;
    v.mEnd = block + n + 2;
}

void DescribeRef(const RefPtrVector &v, std::vector<uint32_t> &out)
{
    out.push_back(static_cast<uint32_t>(v.mLast - v.mFirst));
    for (void *const *p = v.mFirst; p != v.mLast; ++p) out.push_back(Off(*p));
}

void FreeRef(RefPtrVector &v)
{
    delete[] v.mFirst;
    v.mFirst = v.mLast = v.mEnd = nullptr;
}

void BuildBegin(WorldP &w, uint32_t seed)
{
    BuildScoreWorld(w, seed, 0x515940);
    std::mt19937 r(seed * 2654435761u + 0xb6);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);

    // The conditions and services BeginSearch reads.
    Use(w.vtPerson, 31, 0);
    Use(w.vtPerson, 56, 0);
    Use(w.vtPerson, 59, 1);
    for (int slot : {30, 31, 38, 56}) RetValue(&self->mPersonVTable, slot, NoiseAl(r, chance(slot == 30 ? 25 : 70)), w);
    Use(Fk(w, kActorObject).vt, 15, 1);
    Use(Fk(w, kActorObject).vt, 36, 0);
    RetValue(&Fk(w, kActorObject), 15, chance(80) ? 0u : Edge16(r), w);
    RetValue(&Fk(w, kActorObject), 36, Edge16(r), w);
    Use(Fk(w, kHolder).vt, 53, 0);
    RetValue(&Fk(w, kHolder), 53, NoiseAl(r, chance(15)), w);
    self->mVTable[92] = reinterpret_cast<void *>(&PersonNameFake);
    self->mVTable[14] = gThunk[14][2]; // SetAttribute
    w.vt60c[9] = reinterpret_cast<void *>(&CopySnapshotFake);
    Fk(w, kLogSink).vt[14] = reinterpret_cast<void *>(&SinkText);
    Use(Fk(w, kLogSink).vt, 9, 1);
    std::memcpy(w.scratch + 0x100, "Pat", 4);
    Use(w.vtThis, 47, 0);
    self->mVTable[47] = w.vtThis[47];

    // The person's previous search: the vectors hold records, the tick is sometimes this tick already.
    self->mSearchTick = chance(15) ? 100 + static_cast<int32_t>(Scripted(OffsetIn(&Fk(w, kClock), w), 18) - 100) : static_cast<int32_t>(r() % 200);
    self->mSearchTime = 3.5f;
    self->mHasChoice = 1;
    self->mChosenInput = 77;
    self->mChosen = reinterpret_cast<ActionRecord *>(static_cast<uintptr_t>(r() | 1));
    self->mLifeStageAtSearch = 999;
    self->mSearchFlag18 = r();
    Fresh(self->mCandidates);
    Fresh(self->mCandidateScores);
    Fresh(self->mChoices);
    for (uint32_t i = 0; i < r() % 3; ++i) {
        self->mCandidates.push_back(RandomAction(r, w, &Interaction(w, r()), false));
        self->mCandidateScores.push_back(RandomScoreRecord(r));
        self->mChoices.push_back(RandomAction(r, w, &Interaction(w, r()), false));
        self->mRejected.push_back(RandomAction(r, w, &Interaction(w, r()), false));
        self->mRejectedScores.push_back(RandomScoreRecord(r));
    }
    FillRefVector(self->mPending, w, r, 4);
    FillRefVector(self->mRefsA, w, r, 3);
    FillRefVector(self->mRefsB, w, r, 3);
    self->mRefsAInUse = nullptr;
    self->mRefsBInUse = nullptr;
    self->mCheckTreeRuns = r();
    self->mAcceptedCount = r();

    // The snapshot the search copies: a real SearchSnapshot in the scratch area.
    SearchSnapshot *source = reinterpret_cast<SearchSnapshot *>(w.scratch + kSnapshotSourceOffset);
    for (uint32_t &word : source->mData) word = r();
    source->mReference = chance(30) ? nullptr : static_cast<void *>(&Fk(w, kHeld + r() % kHeldCount));
    self->mSnapshotSource = source;
    for (int i = 0; i < kHeldCount; ++i) {
        Use(w.fk[kHeld + i].vt, 1, 0);
        Use(w.fk[kHeld + i].vt, 2, 0);
    }
    self->mSnapshotA.mReference = chance(30) ? nullptr : static_cast<void *>(&Fk(w, kHeld + r() % kHeldCount));

    // The recent-action memory: random expiry around the tick, some past, some current, some far.
    RecentActionVector &recent = self->mRecentActions;
    const int32_t tick = static_cast<int32_t>(Scripted(OffsetIn(&Fk(w, kClock), w), 18));
    recent.clear();
    const uint32_t n = r() % 12;
    for (uint32_t i = 0; i < n; ++i) {
        RecentAction a;
        a.mStackObjectId = static_cast<uint16_t>(r() % 6);
        a.mEntryIndex = static_cast<uint16_t>(r() % 6);
        a.mExpiryTick = (r() % 4 == 0) ? tick : tick + static_cast<int32_t>(r() % 21) - 10;
        a.mName.mText = (r() % 2) ? "x" : "a longer remembered action name";
        recent.push_back(a);
    }

    const uint32_t limit = r() % 9;
    const uint32_t logEnabled = r() % 2, logSink = r() % 2;
    auto base = gBeforeRun;
    gBeforeRun = [=](WorldP &world) {
        base(world);
        *GameAddress<uint32_t>(kGlobalRecentLimit) = limit;
        uint8_t *log = GameAddress<uint8_t>(SimTraceLog::kGlobal);
        std::memset(log, 0, 0x30);
        log[0] = static_cast<uint8_t>(logEnabled);
        *reinterpret_cast<void **>(log + 0x2c) = logSink ? static_cast<void *>(&world.fk[kLogSink]) : nullptr;
    };
}

struct BeginCoverage {
    uint32_t started = 0, refused = 0, logged = 0, trimmed = 0;
};

void FinishBegin(WorldP &w, BeginCoverage *cover, uint32_t result)
{
    PrimPersonObject *self = Person(w);
    const uint8_t *log = GameAddress<uint8_t>(SimTraceLog::kGlobal);
    if (cover) {
        cover->started += result == 1;
        cover->refused += result == 0;
        cover->logged += log[1] != 0;
    }
    std::vector<uint32_t> d;
    DescribeVector(self->mCandidates, d);
    DescribeVector(self->mCandidateScores, d);
    DescribeVector(self->mChoices, d);
    DescribeVector(self->mRejected, d);
    DescribeVector(self->mRejectedScores, d);
    DescribeVector(self->mRecentActions, d);
    DescribeRef(self->mPending, d);
    DescribeRef(self->mRefsA, d);
    DescribeRef(self->mRefsB, d);
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mRefsAInUse)));
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mRefsBInUse)));
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mChosen)));
    d.push_back(static_cast<uint32_t>(self->mSearchTick));
    d.push_back(Bits(self->mSearchTime));
    d.push_back(self->mHasChoice);
    d.push_back(static_cast<uint32_t>(self->mChosenInput));
    d.push_back(static_cast<uint32_t>(self->mLifeStageAtSearch));
    d.push_back(self->mSearchFlag18);
    d.push_back(self->mCheckTreeRuns);
    d.push_back(self->mAcceptedCount);
    for (uint32_t word : self->mSnapshotA.mData) d.push_back(word);
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mSnapshotA.mReference)));
    const uint32_t *b = reinterpret_cast<const uint32_t *>(&self->mSnapshotB);
    for (size_t i = 2; i < sizeof(SearchSnapshot) / 4; ++i) d.push_back(b[i]);
    d.push_back(log[0] | (log[1] << 8));
    d.push_back(Norm(*reinterpret_cast<const uint32_t *>(log + 0x2c)));
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    Retire(self->mCandidates);
    Retire(self->mCandidateScores);
    Retire(self->mChoices);
    Retire(self->mRecentActions);
    FreeRef(self->mPending);
    FreeRef(self->mRefsA);
    FreeRef(self->mRefsB);
    self->mChosen = nullptr;
    FinishCheckWorld(w);
}

void FinishFlagCache()
{
    auto *flagCache = GameAddress<std::map<uint64_t, uint8_t>>(0x10dac2c);
    flagCache->~map();
    std::memset(flagCache, 0, sizeof(*flagCache));
}

} // namespace

DIFF_TEST(Autonomy_BeginSearch, 0x515940)
{
    AutonomyGlobals globals;
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    // The file class is ours (rz/RZFileStreams.h): the stand-in covers the image's constructor and ours.
    shape::StandIn fileClass(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &FileClassStandIn);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x515940);
    BeginCoverage cover;
    RunSeeds(
        ctx, 30000, BuildBegin,
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = original(o) & 0xff;
            } catch (...) {
                FinishBegin(*gW, nullptr, 0xbad);
                FinishFlagCache();
                throw;
            }
            FinishBegin(*gW, &cover, result);
            FinishFlagCache();
            return result;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = reinterpret_cast<PrimPersonObject *>(o)->BeginSearch() ? 1u : 0u;
            } catch (...) {
                FinishBegin(*gW, nullptr, 0xbad);
                FinishFlagCache();
                throw;
            }
            FinishBegin(*gW, nullptr, result);
            FinishFlagCache();
            return result;
        },
        [&](WorldP &, const Trial &) {});
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (!cover.started || !cover.refused || !cover.logged)
        ctx.Fail("coverage: started %u refused %u log opened %u", cover.started, cover.refused, cover.logged);
}
