#pragma once
// The check world of the FindBestAction tests: the shared person world (tests/simantics_person_world.h) with the fakes
// CheckInteraction (0x515c80) needs added: environment, services and recorder, and the tuning slots of the person and its
// objects. The check-tree runner, the trace log and the UI manager / action-string globals are real on both sides. The
// rejected-record vectors are real heap vectors, described and destroyed after each run; fields the original leaves as stack
// leftovers in a rejected record are not compared.
#include "simantics_autonomy_world.h"
#include "simantics_person_world.h"
#include "simantics_autonomy_records.h"

#include <functional>

namespace {

constexpr uint32_t kGlobalUiManager = 0x10da968;
constexpr uint32_t kGlobalActionString = 0x10da96c;
constexpr uint32_t kGlobalDefaultDistance = 0x1014404;
constexpr uint32_t kNameObjectOffset = 0x340; // a cRZString-layout object the recorder's name slot hands out

// The tree sim's CallTree with the person world's logging, which sometimes raises the action-string flag as a primitive of the
// check tree would.
uint32_t __fastcall CallTreeAndFlag(void *self, void *, uint32_t owner, uint32_t id1a, uint32_t treeId, uint32_t id18, uint32_t id1c,
                                    const uint16_t *args, uint32_t result, uint32_t flag16)
{
    const uint32_t answer = CallTreeFake(self, nullptr, owner, id1a, treeId, id18, id1c, args, result, flag16);
    if (Mix(gSeed, 0xf1a9, gHitCount) % 3 == 0) *GameAddress<uint8_t>(kGlobalActionString) = 1;
    return answer;
}

// The recorder's name slot: the address of a cRZString-layout object in the world.
uint32_t __fastcall NameFake(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(90u << 8);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW->scratch + kNameObjectOffset));
}

// Builds the person world and the fakes of the check step. `gBeforeRun` is left set (the globals of the world); a test that
// needs more globals wraps it.
void BuildCheckWorld(WorldP &w, uint32_t seed, uint32_t salt)
{
    Params p;
    p.nullStackObjects = false;
    BuildPersonWorld(w, seed, salt, p);
    std::mt19937 r(seed * 2654435761u + 0x77);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    auto *self = Person(w);
    QuietBuild quiet;

    Fake &environment = Fk(w, kTarget), &holder = Fk(w, kHolder);
    self->mEnvironment = &environment;
    self->mHolder48 = &holder;
    Use(environment.vt, 31, 0);
    RetValue(&environment, 31, NoiseAl(r, chance(60)), w);
    Use(holder.vt, 63, 0);
    RetObject(&holder, 63, &Fk(w, kRecorder), w);
    Fake &recorder = Fk(w, kRecorder);
    Use(recorder.vt, 8, 0);
    RetValue(&recorder, 8, Edge16(r), w);
    recorder.vt[90] = reinterpret_cast<void *>(&NameFake);

    // The name the record copies: a game string object with a short text.
    EmbeddedString *name = new (w.scratch + kNameObjectOffset) EmbeddedString();
    static const char *const names[] = {"", "Chat", "Work Out", "Fifteen letters!"};
    name->mText = names[r() % 4];

    for (int i = 0; i < kInteractions; ++i) {
        Fake &f = Fk(w, kI0 + i);
        Use(f.vt, 55, 0);
        RetValue(&f, 55, NoiseAl(r, chance(30)), w);
        UseFloat<12, 0>(f.vt);
        RetValue(&f, 12, Edge16(r), w);
    }
    for (int s = 0; s < kObjects; ++s) {
        Fake &f = Fk(w, kS0 + s);
        Use(f.vt, 36, 0);
        RetValue(&f, 36, Edge16(r), w);
        Use(f.vt, 95, 0);
    }
    Use(Fk(w, kSink).vt, 21, 1); // (closing the log at the end of a choice)
    UseFloat<79, 1>(w.vtPerson);
    UseFloat<78, 2>(w.vtPerson);
    UseFloat<128, 2>(w.vtThis);
    Fk(w, kTreeSim).vt[15] = reinterpret_cast<void *>(&CallTreeAndFlag);

    self->mCheckTreeRuns = r() % 100;
    self->mCheckedCount = static_cast<int32_t>(r() % 100);
    self->mResult830 = r();
    Fresh(self->mRejected);
    Fresh(self->mRejectedScores);
    // Records already there (their interactions are the world's), so the vectors grow from full as well.
    const uint32_t existing = r() % 3;
    for (uint32_t i = 0; i < existing; ++i) {
        self->mRejected.push_back(RandomAction(r, w, &Interaction(w, r())));
        self->mRejectedScores.push_back(RandomScoreRecord(r));
    }
    if (chance(50)) {
        self->mRejected.shrink_to_fit();
        self->mRejectedScores.shrink_to_fit();
    }
    const uint32_t actionString = r() % 2, uiManager = r();
    gBeforeRun = [=](WorldP &world) {
        *GameAddress<uint8_t>(kGlobalActionString) = static_cast<uint8_t>(actionString);
        *GameAddress<uint32_t>(kGlobalUiManager) = uiManager;
        *GameAddress<float>(kGlobalDefaultDistance) = 10.0f;
        // SetGlobals (the settings singleton and the trace log) as the person world does.
        *GameAddress<void *>(0x10cc288) = &world.fk[kSettings];
        uint8_t *const log = GameAddress<uint8_t>(0x10149e8);
        log[0] = gLogMode != 0;
        log[1] = gLogMode != 0;
        *reinterpret_cast<int32_t *>(log + 4) = 3;
        *reinterpret_cast<void **>(log + 0x2c) = gLogMode == 2 ? static_cast<void *>(&world.fk[kSink]) : nullptr;
    };
}

// The rejected record as the original leaves it: fields it sets only.
// Checked (DL): 0x515c80 builds the record in a stack local and stores only the ids, score, distance fields, name and
// interaction before pushing it (+0x440); +0x08..+0x27 (parameters, flag, arguments, motive score) are never assigned.
void DescribeRejectedAction(const ActionRecord &a, std::vector<uint32_t> &out)
{
    out.push_back(a.mStackObjectId | (static_cast<uint32_t>(a.mEntryIndex) << 16));
    out.push_back(Bits(a.mScore));
    out.push_back(Bits(a.mDistanceScale));
    out.push_back(Bits(a.mDistance));
    out.push_back(TextCode(a.mName.mText));
    out.push_back(a.mInteraction.Get() ? Off(a.mInteraction.Get()) : 0u);
}

// Describes the check world's state (rejected records, scores, globals) into the trace and tears the vectors down.
void FinishCheckWorld(WorldP &w)
{
    PrimPersonObject *self = Person(w);
    std::vector<uint32_t> d;
    d.push_back(0xa5a50000u | static_cast<uint32_t>(self->mRejected.size()));
    for (const ActionRecord &a : self->mRejected) DescribeRejectedAction(a, d);
    DescribeVector(self->mRejectedScores, d);
    d.push_back(Norm(*GameAddress<uint32_t>(kGlobalUiManager)));
    d.push_back(*GameAddress<uint8_t>(kGlobalActionString));
    gTrace.insert(gTrace.end(), d.begin(), d.end());
    Retire(self->mRejected);
    Retire(self->mRejectedScores);
    reinterpret_cast<EmbeddedString *>(w.scratch + kNameObjectOffset)->~EmbeddedString();
    std::memset(w.scratch + kNameObjectOffset, 0, sizeof(EmbeddedString));
}

} // namespace
