// Differential tests for the choice step of the person's FindBestAction (src/simantics/FindBestAction.cpp): ChooseAction 0x5150d0
// and the two score comparators 0x512d70 / 0x512db0. ChooseAction runs over the check world (tests/simantics_autonomy_checkworld.h):
// the interaction checks it makes are real on both sides (original 0x515c80 against ours) over the person world's fakes, the
// random generator is a real cRZRandom in the world, and the record vectors are real heap vectors, described and destroyed after
// each run.
#include "simantics_autonomy_checkworld.h"

#include "simantics/SimTraceLog.h"

#include <cmath>

namespace {

constexpr uint32_t kRandomOffset = 0x2c0; // the generator's three words in the scratch area
constexpr uint32_t kGlobalWidth = 0x1014518;
constexpr uint32_t kGlobalLimit = 0x1014520;

void BuildChoose(WorldP &w, uint32_t seed)
{
    BuildCheckWorld(w, seed, 0x5150d0);
    std::mt19937 r(seed * 2654435761u + 17);
    QuietBuild quiet;
    PrimPersonObject *self = Person(w);

    Use(Fk(w, kHolder).vt, 55, 0);
    RetObject(&Fk(w, kHolder), 55, w.scratch + kRandomOffset, w);
    Use(w.vtPerson, 30, 0);
    // (The person's cITSPerson subobject answers as the object at its vtable pointer.)
    RetValue(&self->mPersonVTable, 30, NoiseAl(r, r() % 3 == 0), w);

    uint32_t *generator = reinterpret_cast<uint32_t *>(w.scratch + kRandomOffset);
    generator[0] = r();
    generator[1] = r();
    generator[2] = r();

    self->mLoopFlag = r() % 3 == 0 ? 1 : 0;
    self->mCheckedCount = (r() % 3 == 0) ? static_cast<int32_t>(r() % 400) : static_cast<int32_t>(r() % 6);
    self->mChosenInput = static_cast<int32_t>(0x77770000 + r() % 16);
    self->mHasChoice = static_cast<uint8_t>(r() % 2);
    // A stale choice no record can be at: an odd address inside the world, not a random word, which could fall inside one side's
    // choice vector on the heap and describe as an index there only (a fake that throws before the choice is reset leaves it as it
    // is; lane NB found this in the search test, tests/simantics_autonomy_searchworld.h).
    self->mChosen = reinterpret_cast<ActionRecord *>(w.scratch + 1 + 2 * (r() % 32));

    auto action = [&]() { return RandomAction(r, w, &Interaction(w, r()), false); };
    const uint32_t n = (r() % 6 == 0) ? 0 : 1 + r() % 9;
    std::vector<ActionRecord> &choices = Fresh(self->mChoices);
    for (uint32_t i = 0; i < n; ++i) choices.push_back(action());
    const uint32_t m = r() % 5;
    std::vector<ActionRecord> &candidates = Fresh(self->mCandidates);
    std::vector<ScoreRecord> &scores = Fresh(self->mCandidateScores);
    for (uint32_t i = 0; i < m; ++i) {
        candidates.push_back(action());
        scores.push_back(RandomScoreRecord(r));
    }

    const uint32_t widths[] = {1, 2, 3, 10, 4, 100};
    const uint32_t limits[] = {0, 1, 2, 5, 200, 3};
    const uint32_t width = widths[r() % 6], limit = limits[r() % 6];
    auto world = gBeforeRun;
    gBeforeRun = [=](WorldP &wd) {
        world(wd);
        *GameAddress<uint32_t>(kGlobalWidth) = width;
        *GameAddress<uint32_t>(kGlobalLimit) = limit;
    };
}

struct Coverage {
    uint32_t chosen = 0, none = 0, notFirst = 0, closed = 0, empty = 0, rejected = 0;
};

// What a run leaves: the record vectors, the choice, the check world. (Counts the original's side.)
void Finish(WorldP &w, Coverage *cover)
{
    PrimPersonObject *self = Person(w);
    const uint8_t *log = GameAddress<uint8_t>(SimTraceLog::kGlobal);
    const bool inRange = self->mChosen >= self->mChoices.data() && self->mChosen < self->mChoices.data() + self->mChoices.size();
    if (cover) {
        cover->empty += self->mChoices.empty();
        cover->chosen += inRange;
        cover->none += !inRange;
        cover->notFirst += inRange && self->mChosen != self->mChoices.data();
        cover->closed += log[1] == 0;
        cover->rejected += !self->mRejected.empty();
    }
    std::vector<uint32_t> d;
    DescribeVector(self->mChoices, d);
    DescribeVector(self->mCandidates, d);
    DescribeVector(self->mCandidateScores, d);
    d.push_back(inRange ? static_cast<uint32_t>(self->mChosen - self->mChoices.data()) : 0xdeadu + (self->mChosen == nullptr));
    d.push_back(self->mHasChoice);
    d.push_back(static_cast<uint32_t>(self->mChosenInput));
    d.push_back(log[0] | (log[1] << 8));
    d.push_back(Norm(*reinterpret_cast<const uint32_t *>(log + 0x2c)));
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    Retire(self->mChoices);
    Retire(self->mCandidates);
    Retire(self->mCandidateScores);
    self->mChosen = nullptr;
    FinishCheckWorld(w);
}

} // namespace

DIFF_TEST(Autonomy_ChooseAction, 0x5150d0)
{
    AutonomyGlobals globals;
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x5150d0);
    Coverage cover;
    RunSeeds(
        ctx, 12000, BuildChoose,
        [&](void *obj, void *, void *) {
            Queues guard(static_cast<uint8_t *>(obj));
            try {
                original(obj);
            } catch (...) {
                Finish(*gW, &cover);
                throw;
            }
            Finish(*gW, &cover);
            return 0u; // (the original returns leftover register contents)
        },
        [&](void *obj, void *, void *) {
            Queues guard(static_cast<uint8_t *>(obj));
            try {
                reinterpret_cast<PrimPersonObject *>(obj)->ChooseAction();
            } catch (...) {
                Finish(*gW, nullptr);
                throw;
            }
            Finish(*gW, nullptr);
            return 0u;
        },
        [&](WorldP &, const Trial &) {});
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (cover.chosen == 0 || cover.none == 0 || cover.notFirst == 0 || cover.closed == 0 || cover.empty == 0 || cover.rejected == 0)
        ctx.Fail("coverage: chosen %u none %u not-first %u closed %u empty %u rejected %u", cover.chosen, cover.none, cover.notFirst, cover.closed,
                 cover.empty, cover.rejected);
}

DIFF_TEST(Autonomy_CompareActionRecords, 0x512d70)
{
    auto original = ctx.Original<int(__cdecl *)(const void *, const void *)>(0x512d70);
    std::mt19937 r(5);
    ActionRecord a, b;
    uint32_t classes[3] = {};
    for (int i = 0; i < 60000; ++i) {
        a.mScore = RandomScore(r);
        b.mScore = (r() % 3 == 0) ? a.mScore + (static_cast<int>(r() % 5) - 2) * 5e-8f : RandomScore(r);
        if (r() % 50 == 0) a.mScore = std::nanf("");
        if (r() % 50 == 0) b.mScore = (r() % 2) ? std::nanf("") : INFINITY;
        const int expected = original(&a, &b), actual = CompareActionRecords(&a, &b);
        ++ctx.cases;
        ++classes[expected + 1];
        if (expected != actual) ctx.Fail("scores %g / %g: original %d, ours %d", a.mScore, b.mScore, expected, actual);
    }
    if (!classes[0] || !classes[1] || !classes[2]) ctx.Fail("not all three answers reached");
}

DIFF_TEST(Autonomy_CompareScoreRecords, 0x512db0)
{
    auto original = ctx.Original<int(__cdecl *)(const void *, const void *)>(0x512db0);
    std::mt19937 r(6);
    ScoreRecord a, b;
    uint32_t classes[3] = {};
    for (int i = 0; i < 60000; ++i) {
        a.mScore = RandomScore(r);
        b.mScore = (r() % 3 == 0) ? a.mScore + (static_cast<int>(r() % 5) - 2) * 5e-8f : RandomScore(r);
        if (r() % 50 == 0) a.mScore = std::nanf("");
        if (r() % 50 == 0) b.mScore = (r() % 2) ? std::nanf("") : -INFINITY;
        const int expected = original(&a, &b), actual = CompareScoreRecords(&a, &b);
        ++ctx.cases;
        ++classes[expected + 1];
        if (expected != actual) ctx.Fail("scores %g / %g: original %d, ours %d", a.mScore, b.mScore, expected, actual);
    }
    if (!classes[0] || !classes[1] || !classes[2]) ctx.Fail("not all three answers reached");
}
