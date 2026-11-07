// Differential tests for the scoring step of the person's FindBestAction (src/simantics/FindBestActionScore.cpp): ScoreInteraction
// 0x516160 and ScorePending 0x516c40. Everything they call is real on both sides over the score world
// (tests/simantics_autonomy_scoreworld.h): the check step, the score factor, the BCON lookups; the fakes behind the vtables
// answer. The record vectors are real heap vectors, described and destroyed after each run (every field of the records
// ScoreInteraction builds is set, so they are compared whole).
#include "simantics_autonomy_scoreworld.h"

namespace {

struct ScoreCoverage {
    uint32_t candidates = 0, rejected = 0, recorded = 0, byColumns = 0, recent = 0, own = 0;
};

// Describes the whole state a scoring run leaves and tears it down.
void FinishScore(WorldP &w, ScoreCoverage *cover, bool fullRejected = true)
{
    PrimPersonObject *self = Person(w);
    if (cover) {
        cover->candidates += !self->mCandidates.empty();
        cover->rejected += !self->mRejected.empty();
        cover->recorded += !self->mCandidateScores.empty();
        cover->recent += !self->mRecentActions.empty();
        for (const ScoreRecord &s : self->mCandidateScores) cover->byColumns += (s.mFlags & 2) != 0;
        for (const ScoreRecord &s : self->mCandidateScores) cover->own += (s.mFlags & 1) != 0;
    }
    std::vector<uint32_t> d;
    DescribeVector(self->mCandidates, d);
    DescribeVector(self->mCandidateScores, d);
    DescribeVector(self->mChoices, d);
    if (fullRejected) {
        DescribeVector(self->mRejected, d);
    } else {
        // (Records the check step rejected carry stack leftovers in the original in some fields: those are not compared.)
        d.push_back(static_cast<uint32_t>(self->mRejected.size()));
        for (const ActionRecord &a : self->mRejected) DescribeRejectedAction(a, d);
    }
    DescribeVector(self->mRejectedScores, d);
    DescribeVector(self->mRecentActions, d);
    d.push_back(Norm(reinterpret_cast<uint32_t>(self->mScoringObject)));
    d.push_back(self->mAcceptedCount);
    d.push_back(static_cast<uint32_t>(self->mCheckedCount));
    d.push_back(self->mCheckTreeRuns);
    const float *table = GameAddress<float>(0x10dac68);
    for (int i = 0; i < 16; ++i) d.push_back(Bits(table[i]));
    d.push_back(*GameAddress<uint8_t>(0x10dac5a));
    auto *flagCache = GameAddress<std::map<uint64_t, uint8_t>>(0x10dac2c);
    d.push_back(static_cast<uint32_t>(flagCache->size()));
    for (const auto &e : *flagCache) {
        d.push_back(static_cast<uint32_t>(e.first));
        d.push_back(static_cast<uint32_t>(e.first >> 32));
        d.push_back(e.second);
    }
    flagCache->~map();
    std::memset(flagCache, 0, sizeof(*flagCache));
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    Retire(self->mCandidates);
    Retire(self->mCandidateScores);
    Retire(self->mChoices);
    Retire(self->mRecentActions);
    FinishCheckWorld(w); // (describes the rejected records on their set fields again and tears them down)
}

void BuildScore(WorldP &w, uint32_t seed)
{
    BuildScoreWorld(w, seed, 0x516160);
    std::mt19937 r(seed * 2654435761u + 0x99);
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);
    Fresh(self->mCandidates);
    Fresh(self->mCandidateScores);
    Fresh(self->mChoices);
    // Records already there, so the vectors grow from full too.
    const uint32_t existing = r() % 3;
    for (uint32_t i = 0; i < existing; ++i) {
        self->mCandidates.push_back(RandomAction(r, w, &Interaction(w, r()), false));
        self->mCandidateScores.push_back(RandomScoreRecord(r));
        self->mChoices.push_back(RandomAction(r, w, &Interaction(w, r()), false));
    }
    if (r() % 2) {
        self->mCandidates.shrink_to_fit();
        self->mCandidateScores.shrink_to_fit();
        self->mChoices.shrink_to_fit();
        self->mRejected.shrink_to_fit();
        self->mRejectedScores.shrink_to_fit();
    }
}

} // namespace

DIFF_TEST(Autonomy_ScoreInteraction, 0x516160)
{
    AutonomyGlobals globals;
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, void *, void *, uint32_t, uint32_t)>(0x516160);
    ScoreCoverage cover;
    RunSeeds(
        ctx, 30000, BuildScore,
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            std::mt19937 r(gSeed);
            const uint32_t gathered = NoiseAl(r, r() % 2), checked = NoiseAl(r, r() % 2);
            try {
                original(o, &Interaction(*gW, gSeed % 5), &Fk(*gW, kT0 + gSeed % kEntries), &Fk(*gW, kRowTable), gathered, checked);
            } catch (...) {
                FinishScore(*gW, nullptr);
                throw;
            }
            FinishScore(*gW, &cover);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            std::mt19937 r(gSeed);
            const uint32_t gathered = NoiseAl(r, r() % 2), checked = NoiseAl(r, r() % 2);
            try {
                reinterpret_cast<PrimPersonObject *>(o)->ScoreInteraction(&Interaction(*gW, gSeed % 5), &Fk(*gW, kT0 + gSeed % kEntries),
                                                                         &Fk(*gW, kRowTable), static_cast<uint8_t>(gathered),
                                                                         static_cast<uint8_t>(checked));
            } catch (...) {
                FinishScore(*gW, nullptr);
                throw;
            }
            FinishScore(*gW, nullptr);
            return 0u;
        },
        [&](WorldP &, const Trial &) {});
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (!cover.candidates || !cover.rejected || !cover.recorded || !cover.byColumns || !cover.recent || !cover.own)
        ctx.Fail("coverage: candidates %u rejected %u recorded %u recent-discounted %u had recent memory %u own-list %u", cover.candidates,
                 cover.rejected, cover.recorded, cover.byColumns, cover.recent, cover.own);
}

DIFF_TEST(Autonomy_ScorePending, 0x516c40)
{
    AutonomyGlobals globals;
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x516c40);
    ScoreCoverage cover;
    auto setPending = [](WorldP &w, uint32_t seed) {
        std::mt19937 r(seed * 7 + 3);
        PrimPersonObject *self = Person(w);
        const uint32_t n = r() % 6;
        void **list = new void *[n + 1];
        for (uint32_t i = 0; i < n; ++i) list[i] = &Interaction(w, r());
        self->mPending.mFirst = list;
        self->mPending.mLast = list + n;
        self->mPending.mEnd = list + n;
    };
    auto freePending = [](WorldP &w) {
        PrimPersonObject *self = Person(w);
        delete[] self->mPending.mFirst;
        self->mPending.mFirst = self->mPending.mLast = self->mPending.mEnd = nullptr;
    };
    RunSeeds(
        ctx, 20000, [&](WorldP &w, uint32_t seed) { BuildScore(w, seed); setPending(w, seed); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            try {
                original(o);
            } catch (...) {
                FinishScore(*gW, nullptr, false);
                freePending(*gW);
                throw;
            }
            FinishScore(*gW, &cover, false);
            freePending(*gW);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            try {
                reinterpret_cast<PrimPersonObject *>(o)->ScorePending();
            } catch (...) {
                FinishScore(*gW, nullptr, false);
                freePending(*gW);
                throw;
            }
            FinishScore(*gW, nullptr, false);
            freePending(*gW);
            return 0u;
        },
        [&](WorldP &, const Trial &) {});
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (!cover.candidates || !cover.rejected)
        ctx.Fail("coverage: candidates %u rejected %u recorded %u", cover.candidates, cover.rejected, cover.recorded);
}
