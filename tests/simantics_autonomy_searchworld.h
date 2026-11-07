#pragma once
// The search world of the opcode 3 test (tests/simantics_prims_person_03.cpp): the gather world (tests/simantics_autonomy_gatherworld.h,
// over the score, check and person worlds) with what the rest of a whole FindBestAction search reaches added: the begin step's services
// (the autonomy conditions, the clock, the state snapshot, the trace log's sink), the choice's random generator and the start step's
// stack objects and tuning, and what the primitive itself uses (the holder it toggles, the input object, the singleton's object lookup,
// the pairs of interactions and table entries the tree leaves in the two reference vectors while the search runs).
//
// Everything the primitive calls is real on both sides: BeginSearch, GatherInteractions, ScorePending, ScoreInteraction, ChooseAction,
// StartChosenAction, and below them the check-tree runner, the interaction test sim, the vector helpers, the score factor and the trace
// log. Only the file class the trace log's opener creates its sink with (0x1b4dd2) is a stand-in in the mapped image, as in the begin
// step's test. The record vectors and the lists are real heap objects, described into the trace and torn down after each run.
#include "simantics_autonomy_gatherworld.h"

#include "simantics/SimTraceLog.h"

namespace {

constexpr uint32_t kSnapshotSourceOffset = 0x240;
constexpr uint32_t kGeneratorOffset = 0x3c0;
constexpr int kHolderObject = kFrame; // what the holder's slot 67 gives: slots 8, 9, 10 (the tree frame fake's other slots are the start step's)
constexpr int kSearchSink = kSink;
constexpr uint32_t kGlobalRecentLimit = 0x1014524;
constexpr uint32_t kGlobalWidth = 0x1014518;
constexpr uint32_t kGlobalCheckLimit = 0x1014520;
constexpr uint32_t kGlobalInput = 0x101451c;
constexpr uint32_t kGlobalMinimumScore = 0x1014514;
constexpr uint32_t kGlobalTicks = 0x1014528;

// ---- the begin step's fakes (as tests/simantics_autonomy_begin.cpp) ---------------------------------------------------------

// Person slot 92 (the name): the world's name bytes.
uint32_t __fastcall SearchPersonNameFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(92u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->scratch + 0x100));
}

// The motives object's slot 9 (out): fills the snapshot buffer from the seed.
uint32_t __fastcall SearchCopySnapshotFake(void *self, void *, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(9u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    for (int i = 0; i < 35; ++i) out[i] = Mix(gSeed, 9, i) | 0x80000000u; // (no word can equal an address of the world)
    return 0;
}

// The stand-in for the game's text file class constructor (0x1b4dd2): logs the file name and hands the log sink fake back.
uint32_t __fastcall SearchFileClassStandIn(void *, void *, const cRZString *name, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Tick();
    gTrace.push_back(0x801b4dd2u);
    gTrace.push_back(TextCode(name->mString));
    gTrace.push_back(a);
    gTrace.push_back(b);
    gTrace.push_back(c);
    gTrace.push_back(d);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gW->fk[kSearchSink]));
}

// The sink's WriteText: the text is compared, not its address.
uint32_t __fastcall SearchSinkText(void *self, void *, const char *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 1u);
    gTrace.push_back(text ? TextCode(text) : 0u);
    return 0;
}

// ---- the primitive's own fakes ---------------------------------------------------------------------------------------------

// Slot 8 of the holder object (a flag): the original's last call pushes a whole stack slot of which only the low byte was written, so
// only the low byte is logged.
uint32_t __fastcall HolderObject8(void *self, void *, uint32_t flag)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(8u << 8 | 1u);
    gTrace.push_back(flag & 0xffu);
    return Scripted(Off(self), 8);
}

// Slot 8 of the input object (the +0x60c object: global input, function table, accumulated): answers the next input from the script and,
// as the tree primitives do while a search runs, sometimes leaves an interaction and its table entry in the two reference vectors the
// person points at (storage of the test: a block of four each).
uint32_t __fastcall InputObject8(void *self, void *, uint32_t global, uint32_t *table, uint32_t accumulated)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(8u << 8 | 3u);
    gTrace.push_back(global);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(table)));
    gTrace.push_back(accumulated);
    PrimPersonObject *person = Person(*gW);
    if (person->mRefsAInUse && person->mRefsBInUse && Mix(gSeed, 0x8a, gHitCount) % 3 == 0) {
        const uint32_t pairs = 1 + Mix(gSeed, 0x8b, gHitCount) % 2;
        for (uint32_t i = 0; i < pairs; ++i) {
            RefPtrVector *vectors[2] = {person->mRefsAInUse, person->mRefsBInUse};
            void *items[2] = {&Interaction(*gW, Mix(gSeed, 0x8c, gHitCount + i)), &Fk(*gW, kT0 + Mix(gSeed, 0x8d, gHitCount + i) % kEntries)};
            for (RefPtrVector *v : vectors) {
                if (!v->mFirst) {
                    v->mFirst = v->mLast = new void *[4];
                    v->mEnd = v->mFirst + 4;
                }
            }
            if (vectors[0]->mLast == vectors[0]->mEnd || vectors[1]->mLast == vectors[1]->mEnd) continue; // (full: the vectors stay parallel)
            for (int k = 0; k < 2; ++k) {
                *vectors[k]->mLast++ = items[k];
                VirtualSlot<uint32_t(__thiscall *)(void *)>(items[k], 1)(items[k]);
            }
        }
    }
    return Scripted(Off(self), 8);
}

// ---- the world -------------------------------------------------------------------------------------------------------------

void BuildSearchWorld(WorldP &w, uint32_t seed)
{
    GatherParams params;
    params.consistent = true;
    BuildGatherWorld(w, seed, 0x521860, params);
    std::mt19937 r(seed * 2654435761u + 0x5ea);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);

    // The node: the input word (0: poll), the flag byte.
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    const uint32_t input = r() % 2 ? 0 : 1 + r() % 3;
    w.param[4] = static_cast<uint8_t>(input);
    w.param[5] = 0;
    w.obj[0x70] = static_cast<uint8_t>(r() % 2);

    // Begin step.
    Use(w.vtPerson, 31, 0);
    Use(w.vtPerson, 56, 0);
    Use(w.vtPerson, 59, 1);
    for (int slot : {31, 56}) RetValue(&self->mPersonVTable, slot, NoiseAl(r, chance(70)), w);
    OverrideValue(w, &self->mPersonVTable, 30, NoiseAl(r, chance(25)));
    Use(Fk(w, kActorObject).vt, 15, 1);
    Use(Fk(w, kActorObject).vt, 36, 0);
    RetValue(&Fk(w, kActorObject), 15, chance(80) ? 0u : Edge16(r), w);
    RetValue(&Fk(w, kActorObject), 36, Edge16(r), w);
    Use(Fk(w, kHolder).vt, 53, 0);
    RetValue(&Fk(w, kHolder), 53, NoiseAl(r, chance(15)), w);
    self->mVTable[92] = reinterpret_cast<void *>(&SearchPersonNameFake);
    self->mVTable[14] = gThunk[14][2]; // SetAttribute
    self->mVTable[47] = gThunk[47][0];
    w.vt60c[9] = reinterpret_cast<void *>(&SearchCopySnapshotFake);
    Fk(w, kSearchSink).vt[14] = reinterpret_cast<void *>(&SearchSinkText);
    Use(Fk(w, kSearchSink).vt, 9, 1);

    // The person's state before the search: the previous search's vectors and lists.
    self->mSearchTick = chance(15) ? 100 + static_cast<int32_t>(Scripted(OffsetIn(&Fk(w, kClock), w), 18) - 100) : static_cast<int32_t>(r() % 200);
    self->mSearchTime = 3.5f;
    self->mHasChoice = 1;
    self->mChosenInput = 77;
    // A stale choice no record can be at: an odd address inside the world. A random word (r() | 1, as before) could fall inside one
    // side's choice vector on the heap and describe as an index there only (lane NB: seed 11717 leaves 0x0abe3611 there, a search refused
    // before it starts keeps the stale pointer, and one sharded run described it as index 0 on the original's side: in a young shard's
    // heap the original's one-record vector covered that address).
    self->mChosen = reinterpret_cast<ActionRecord *>(w.scratch + 1 + 2 * (r() % 32));
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
    for (RefPtrVector *refs : {&self->mRefsA, &self->mRefsB}) {
        const uint32_t n = r() % 3;
        void **block = new void *[n + 2];
        for (uint32_t i = 0; i < n; ++i) block[i] = refs == &self->mRefsA ? static_cast<void *>(&Interaction(w, r())) : static_cast<void *>(&Fk(w, kT0 + r() % kEntries));
        refs->mFirst = block;
        refs->mLast = block + n;
        refs->mEnd = block + n + 2;
    }
    self->mRefsAInUse = nullptr;
    self->mRefsBInUse = nullptr;
    self->mCheckTreeRuns = r();
    self->mAcceptedCount = r();
    for (int i = 0; i < kInteractions; ++i) Use(Fk(w, kI0 + i).vt, 1, 0);
    for (int t = 0; t < kEntries; ++t) {
        Use(Fk(w, kT0 + t).vt, 1, 0);
        Use(Fk(w, kT0 + t).vt, 2, 0);
    }

    // The snapshot the search copies: a real SearchSnapshot in the scratch area, and its counted reference.
    SearchSnapshot *source = reinterpret_cast<SearchSnapshot *>(w.scratch + kSnapshotSourceOffset);
    for (uint32_t &word : source->mData) word = r();
    source->mReference = chance(30) ? nullptr : static_cast<void *>(&Fk(w, kS0 + r() % kObjects));
    self->mSnapshotSource = source;
    for (int s = 0; s < kObjects; ++s) {
        Use(Fk(w, kS0 + s).vt, 1, 0);
        Use(Fk(w, kS0 + s).vt, 2, 0);
    }
    self->mSnapshotA.mReference = chance(30) ? nullptr : static_cast<void *>(&Fk(w, kS0 + r() % kObjects));

    // The recent-action memory of the search: expiries around the tick.
    RecentActionVector &recent = self->mRecentActions;
    const int32_t tick = static_cast<int32_t>(Scripted(OffsetIn(&Fk(w, kClock), w), 18));
    recent.clear();
    const uint32_t recentCount = r() % 12;
    for (uint32_t i = 0; i < recentCount; ++i) {
        RecentAction a;
        a.mStackObjectId = static_cast<uint16_t>(r() % 6);
        a.mEntryIndex = static_cast<uint16_t>(r() % 6);
        a.mExpiryTick = (r() % 4 == 0) ? tick : tick + static_cast<int32_t>(r() % 21) - 10;
        a.mName.mText = (r() % 2) ? "x" : "a longer remembered action name";
        recent.push_back(a);
    }

    // The choice: the random generator the holder hands out (three words), the person that takes the best only (set above).
    Use(Fk(w, kHolder).vt, 55, 0);
    RetObject(&Fk(w, kHolder), 55, w.scratch + kGeneratorOffset, w);
    uint32_t *generator = reinterpret_cast<uint32_t *>(w.scratch + kGeneratorOffset);
    generator[0] = r();
    generator[1] = r();
    generator[2] = r();

    // The start: the action fields, the tree frame (the fake the handler is given), the person's minimum-score mode.
    new (&self->mActionName) EmbeddedString();
    self->mActionName.mText = (r() % 2) ? "old name" : "";
    self->mActionParameterA = r();
    self->mActionParameterB = r();
    self->mActionParameterC = static_cast<int32_t>(r());
    FillRandom(reinterpret_cast<uint8_t *>(self->mActionArguments), sizeof(self->mActionArguments), r);
    Use(w.vtPerson, 50, 0);
    RetValue(&self->mPersonVTable, 50, (r() % 3 == 0) ? 0u : (r() % 2000) + 1, w);
    Use(w.vtFk[kFrame], 13, 1);

    // The primitive: the holder and what it hands out, the singleton's lookup, the post-state call, the inputs.
    Use(Fk(w, kHolder).vt, 67, 0);
    RetObject(&Fk(w, kHolder), 67, &Fk(w, kHolderObject), w);
    Use(Fk(w, kHolderObject).vt, 9, 0);
    RetValue(&Fk(w, kHolderObject), 9, NoiseAl(r, chance(50)), w);
    Fk(w, kHolderObject).vt[8] = reinterpret_cast<void *>(&HolderObject8);
    Use(Fk(w, kHolderObject).vt, 10, 0);
    Use(w.vtTree, 32, 1);
    Use(Fk(w, kSettings).vt, 26, 0);
    Use(Fk(w, kSettings).vt, 29, 1);
    RetObject(&Fk(w, kSettings), 26, chance(75) ? static_cast<const void *>(&Fk(w, kSettings)) : nullptr, w);
    RetObject(&Fk(w, kSettings), 29, chance(66) ? static_cast<const void *>(&Fk(w, kHolderObject)) : nullptr, w);
    w.vt60c[8] = reinterpret_cast<void *>(&InputObject8);
    const uint32_t inputs = r() % 4;
    for (uint32_t i = 0; i < inputs; ++i) EnqueueValue(w.obj + 0x60c, 8, r() % 3 ? 1 + r() % 5 : 0, w);
    RetValue(w.obj + 0x60c, 8, 0, w);

    // The tuning globals and the trace log.
    const uint32_t recentLimit = r() % 9, width = (r() % 6 == 0) ? 100 : 1 + r() % 10;
    const uint32_t checkLimit = (r() % 3 == 0) ? 0 : 1 + r() % 3, inputGlobal = r();
    const uint32_t ticks = r() % 7000;
    const uint32_t minimumBits = Bits((r() % 3 == 0) ? 0.1f : 0.1f + static_cast<float>(static_cast<int32_t>(r() % 7) - 3) * 0.05f);
    const uint32_t logEnabled = r() % 2, logSink = r() % 2;
    auto base = gBeforeRun;
    gBeforeRun = [=](WorldP &world) {
        base(world);
        ArenaOf(world).autonomy = true; // (the search's gather is the autonomy gather)
        *GameAddress<uint32_t>(kGlobalRecentLimit) = recentLimit;
        *GameAddress<uint32_t>(kGlobalWidth) = width;
        *GameAddress<uint32_t>(kGlobalCheckLimit) = checkLimit;
        *GameAddress<uint32_t>(kGlobalInput) = inputGlobal;
        *GameAddress<uint32_t>(kGlobalMinimumScore) = minimumBits;
        *GameAddress<uint32_t>(kGlobalTicks) = ticks;
        uint8_t *log = GameAddress<uint8_t>(SimTraceLog::kGlobal);
        std::memset(log, 0, 0x30);
        log[0] = static_cast<uint8_t>(logEnabled);
        *reinterpret_cast<void **>(log + 0x2c) = logSink ? static_cast<void *>(&world.fk[kSearchSink]) : nullptr;
    };
}

// ---- describing and tearing down -------------------------------------------------------------------------------------------

void DescribeRefs(const RefPtrVector &v, std::vector<uint32_t> &out)
{
    out.push_back(static_cast<uint32_t>(v.mLast - v.mFirst));
    for (void *const *p = v.mFirst; p != v.mLast; ++p) out.push_back(Off(*p));
}

void FreeRefs(RefPtrVector &v)
{
    delete[] v.mFirst;
    v.mFirst = v.mLast = v.mEnd = nullptr;
}

// Describes the whole state a search leaves and tears it down (the lists and the records, the choice, the action, the globals).
void FinishSearchWorld(WorldP &w)
{
    PrimPersonObject *self = Person(w);
    const uint8_t *log = GameAddress<uint8_t>(SimTraceLog::kGlobal);
    std::vector<uint32_t> d;
    d.push_back(0x5ea00000u);
    DescribeVector(self->mCandidates, d);
    DescribeVector(self->mCandidateScores, d);
    DescribeVector(self->mChoices, d);
    d.push_back(static_cast<uint32_t>(self->mRejected.size())); // (the records themselves: FinishCheckWorld, on their set fields)
    DescribeVector(self->mRejectedScores, d);
    DescribeVector(self->mRecentActions, d);
    DescribeList(self->mPending, d);
    DescribeRefs(self->mRefsA, d);
    DescribeRefs(self->mRefsB, d);
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mRefsAInUse)));
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mRefsBInUse)));
    const bool chosenInRange = self->mChosen >= self->mChoices.data() && self->mChosen < self->mChoices.data() + self->mChoices.size();
    d.push_back(chosenInRange ? static_cast<uint32_t>(self->mChosen - self->mChoices.data()) : 0xdeadu + (self->mChosen == nullptr));
    d.push_back(static_cast<uint32_t>(self->mSearchTick));
    d.push_back(Bits(self->mSearchTime));
    d.push_back(self->mHasChoice);
    d.push_back(static_cast<uint32_t>(self->mChosenInput));
    d.push_back(static_cast<uint32_t>(self->mLifeStageAtSearch));
    d.push_back(self->mSearchFlag18);
    d.push_back(self->mCheckTreeRuns);
    d.push_back(self->mAcceptedCount);
    d.push_back(static_cast<uint32_t>(self->mCheckedCount));
    d.push_back(TextCode(self->mActionName.mText));
    d.push_back(self->mActionParameterA);
    d.push_back(self->mActionParameterB);
    d.push_back(static_cast<uint32_t>(self->mActionParameterC));
    for (int i = 0; i < 8; i += 2) d.push_back(self->mActionArguments[i] | (static_cast<uint32_t>(self->mActionArguments[i + 1]) << 16));
    for (uint32_t word : self->mSnapshotA.mData) d.push_back(word);
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mSnapshotA.mReference)));
    const uint32_t *b = reinterpret_cast<const uint32_t *>(&self->mSnapshotB);
    for (size_t i = 2; i < sizeof(SearchSnapshot) / 4; ++i) d.push_back(b[i]);
    d.push_back(log[0] | (log[1] << 8));
    d.push_back(Norm(*reinterpret_cast<const uint32_t *>(log + 0x2c)));
    d.push_back(Norm(*GameAddress<uint32_t>(0x10da968)));
    d.push_back(*GameAddress<uint8_t>(0x10da96c));
    const float *scoreTable = GameAddress<float>(0x10dac68);
    for (int i = 0; i < 16; ++i) d.push_back(Bits(scoreTable[i]));
    d.push_back(*GameAddress<uint8_t>(0x10dac5a));
    auto *flagCache = GameAddress<std::map<uint64_t, uint8_t>>(0x10dac2c);
    d.push_back(static_cast<uint32_t>(flagCache->size()));
    for (const auto &e : *flagCache) {
        d.push_back(static_cast<uint32_t>(e.first));
        d.push_back(static_cast<uint32_t>(e.first >> 32));
        d.push_back(e.second);
    }
    gTrace.insert(gTrace.end(), d.begin(), d.end());

    flagCache->~map();
    std::memset(flagCache, 0, sizeof(*flagCache));
    Retire(self->mCandidates);
    Retire(self->mCandidateScores);
    Retire(self->mChoices);
    Retire(self->mRecentActions);
    FreeList(self->mPending);
    FreeRefs(self->mRefsA);
    FreeRefs(self->mRefsB);
    self->mChosen = nullptr;
    self->mActionName.~EmbeddedString();
    std::memset(&self->mActionName, 0, sizeof(self->mActionName));
    GatherArena &arena = ArenaOf(w);
    delete[] arena.registryBytes;
    arena.registryBytes = nullptr;
    self->mObjectRegistry = nullptr;
    FreeList(arena.list);
    FinishCheckWorld(w); // (describes the rejected records' set fields again and tears them down)
}

} // namespace
