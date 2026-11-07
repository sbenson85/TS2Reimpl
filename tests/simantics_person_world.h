#pragma once
// The person world of the interaction-queue tests (tests/simantics_person_queue.cpp, tests/simantics_prims_person_idle.cpp):
// the harness's person (tests/simantics_prims_person.h) with recording fakes for everything the person's interaction
// bookkeeping (src/simantics/PersonInteractions.cpp), the queues (RefCountedDeque) and the check-tree runner
// (cEdithObjectTestSim) reach: interactions, their table entries and stack objects, the actor with its Sim object and tree
// sim, the settings singleton and the trace sink. The person's two queues and its history are real heap deques; a test
// wraps each run in a `Queues` guard that reduces them to their contents in the trace and destroys them.
#include "simantics_prims_person.h"

#include "simantics/EdithObjectTestSim.h"
#include "simantics/SimTraceLog.h"

#include <deque>
#include <new>

namespace {

constexpr int kInteractions = 6;
constexpr int kEntries = 3;
constexpr int kObjects = 3;
enum : int {
    kFrame = 0, // (the tree frame of IdleForInput; RunSeeds hands fake 0 to the handler)
    kI0,
    kT0 = kI0 + kInteractions,
    kS0 = kT0 + kEntries,
    kAttributes = kS0 + kObjects,
    kRowTable,
    kChain, // the tracker interface and the accessors behind it (one fake, distinct slots)
    kActor,
    kActorObject,
    kTreeSim,
    kSettings, // the settings singleton, which is also the tuning object it hands out
    kSink,
    kRecorder,
    kHolder,
    kTarget,
    kFakeCount
};
static_assert(kFakeCount <= kFk, "too many fakes for the harness");

constexpr int kNameOffset = 0x100;
constexpr int kRecordOffset = 0x180;

Fake &Fk(WorldP &w, int i)
{
    return w.fk[i];
}

uint32_t Val16Noise(std::mt19937 &r)
{
    return (r() & 0xffff0000u) | (r() & 0xffffu);
}

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

// The out-parameter QueryInterface of a stack object: scripted answer = the object to hand out (or none).
uint32_t __fastcall QueryInterfaceFake(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    const uint32_t found = Scripted(Off(self), 0);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(found));
    return 0;
}

// SetOwnerPerson of an interaction that sometimes removes itself from the person's action queue while it is told (the
// code must not skip the entry that slides into its place).
uint32_t __fastcall SetOwnerPersonFake(void *self, void *, uint32_t person)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(67u << 8 | 1u);
    gTrace.push_back(Norm(person));
    if (Mix(gSeed, Off(self), gHitCount) % 3 == 0) {
        std::deque<void *> *q = reinterpret_cast<std::deque<void *> *>(gW->obj + offsetof(PrimPersonObject, mQueueB));
        for (auto it = q->begin(); it != q->end(); ++it) {
            if (*it == self) {
                q->erase(it);
                gTrace.push_back(0x5e1f0000u);
                break;
            }
        }
    }
    return Scripted(Off(self), 67);
}

// WriteText of the trace sink: the text itself is compared (its address differs between ours and the game's literals).
uint32_t __fastcall WriteTextFake(void *self, void *, const char *text)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 1u);
    gTrace.push_back(text ? TextHash(text) : 0u);
    return Scripted(Off(self), 14);
}

// The interaction's SetCheckPassed (slot 40): the low byte, and whether the upper three bytes are the interaction's own address.
uint32_t __fastcall SetCheckPassedFake(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(40u << 8 | 1u);
    gTrace.push_back(arg & 0xffu);
    gTrace.push_back((arg >> 8) == (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) >> 8) ? 1u : 0u);
    return Scripted(Off(self), 40);
}

// The tree sim's CallTree (slot 15, eight arguments); the argument words behind the pointer are logged too.
uint32_t __fastcall CallTreeFake(void *self, void *, uint32_t owner, uint32_t id1a, uint32_t treeId, uint32_t id18, uint32_t id1c,
                                 const uint16_t *args, uint32_t result, uint32_t flag16)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(15u << 8 | 8u);
    for (uint32_t v : {owner, id1a, treeId, id18, id1c, result, flag16}) gTrace.push_back(Norm(v));
    for (int i = 0; i < 4; ++i) gTrace.push_back(args[i]);
    return Scripted(Off(self), 15);
}

// Slots that run inside a noexcept function of the original (the check-tree runner's destructor, the deque's AddRef in
// PushBack) must not throw: they log like any fake but never count towards the injected error.
uint32_t __fastcall AddRefFake(void *self, void *)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(1u << 8 | 0u);
    return Scripted(Off(self), 1);
}

uint32_t __fastcall GetSimObjectFake(void *self, void *)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(8u << 8 | 0u);
    return Scripted(Off(self), 8);
}

uint32_t __fastcall SetArgumentFake(void *self, void *, uint32_t index, uint32_t value)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(20u << 8 | 2u);
    gTrace.push_back(index);
    gTrace.push_back(value);
    return Scripted(Off(self), 20);
}

// GetArgument fills the test sim's saved-arguments buffer, which the noexcept destructor writes back whole: an error in the
// middle of that loop would restore uninitialised stack words, so the call never fails (it cannot in the game).
uint32_t __fastcall GetArgumentFake(void *self, void *, uint32_t index)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(index);
    return Scripted(Off(self), 19);
}

uint32_t gLogMode = 0; // 0: trace log off, 1: on without a sink, 2: on with the fake sink

void Fixed(WorldP &w, int fake, int slot, int args, uint32_t value)
{
    Use(w.fk[fake].vt, slot, args);
    RetValue(&w.fk[fake], slot, value, w);
}

void FixedObject(WorldP &w, int fake, int slot, int args, const void *target)
{
    Use(w.fk[fake].vt, slot, args);
    RetObject(&w.fk[fake], slot, target, w);
}

// The person's queues (and history), kept as real deques while a test runs.
struct Queues {
    static constexpr int kCount = 3;
    std::deque<void *> *q[kCount];
    explicit Queues(uint8_t *obj)
    {
        q[0] = reinterpret_cast<std::deque<void *> *>(obj + offsetof(PrimPersonObject, mQueueA));
        q[1] = reinterpret_cast<std::deque<void *> *>(obj + offsetof(PrimPersonObject, mQueueB));
        q[2] = reinterpret_cast<std::deque<void *> *>(obj + offsetof(PrimPersonObject, mHistory));
    }
    static void Describe(std::deque<void *> *d)
    {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(d);
        gTrace.push_back(0xde9e0000u | (words[1] != 0));
        gTrace.push_back(words[2]);
        gTrace.push_back(words[3]);
        gTrace.push_back(words[4]);
        void *const *map = reinterpret_cast<void *const *>(static_cast<uintptr_t>(words[1]));
        uint32_t blocks = 0;
        for (uint32_t i = 0; words[1] && i < words[2]; ++i) blocks += map[i] != nullptr;
        gTrace.push_back(blocks);
        for (uint32_t i = 0; i < words[4]; ++i) {
            const uint32_t pos = words[3] + i;
            void **block = static_cast<void **>(map[(pos >> 2) & (words[2] - 1)]);
            gTrace.push_back(Norm(reinterpret_cast<uint32_t>(block[pos & 3])));
        }
    }
    // Logs the contents of the queues and tears them down (the memory is then zero).
    ~Queues()
    {
        for (std::deque<void *> *d : q) Describe(d);
        for (std::deque<void *> *d : q) {
            d->~deque();
            std::memset(d, 0, sizeof(*d));
        }
    }
};

struct Params {
    bool nullStackObjects = false; // interactions may lack a stack object (only where the code checks)
    bool nullTableEntries = false; // ... or a table entry
    bool currentNullable = false;  // the Person Data accessor may report no current interaction
    bool bigQueue = false;
    bool selfRemoving = false;     // SetOwnerPerson may take the interaction out of the action queue
    bool previousAlways = false;   // mPreviousInteraction is never null
    bool historyNotEmpty = false;  // the history holds a few elements
};

void SetupInteraction(WorldP &w, std::mt19937 &r, int index, const Params &p)
{
    Fake &f = Fk(w, kI0 + index);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    for (int slot : {2, 6, 11, 13, 16, 17, 18, 22, 24, 28, 29, 33, 35, 39, 47, 49, 66, 68}) Use(f.vt, slot, 0);
    f.vt[1] = reinterpret_cast<void *>(&AddRefFake);
    for (int slot : {38, 42, 44, 56, 67, 70, 75, 77}) Use(f.vt, slot, 1);
    Use(f.vt, 15, 0); // no arguments (the three pushed before it belong to the person's slot 248)
    f.vt[40] = reinterpret_cast<void *>(&SetCheckPassedFake);
    if (p.selfRemoving) f.vt[67] = reinterpret_cast<void *>(&SetOwnerPersonFake);
    RetValue(&f, 18, static_cast<uint32_t>(static_cast<int32_t>(r() % 40) - 5 - (chance(10) ? 40 : 0)), w);
    RetValue(&f, 22, r(), w);
    RetValue(&f, 29, NoiseAl(r, chance(50)), w);
    RetValue(&f, 33, NoiseAl(r, chance(50)), w);
    RetValue(&f, 47, NoiseAl(r, chance(50)), w);
    RetValue(&f, 66, Val16Noise(r), w);
    RetValue(&f, 68, chance(30) ? (r() & 0xffff0000u) : Val16Noise(r), w);
    RetValue(&f, 6, NoiseAl(r, chance(50)), w);
    RetValue(&f, 35, NoiseAl(r, chance(50)), w);
    RetValue(&f, 39, NoiseAl(r, chance(60)), w);
    RetValue(&f, 49, NoiseAl(r, chance(50)), w);
    RetValue(&f, 13, chance(15) ? (r() & 0xffff0000u) : Val16Noise(r) | 1u, w);
    RetValue(&f, 15, r(), w);
    RetValue(&f, 11, chance(50) ? 1u : r(), w);
    RetObject(&f, 17, chance(6) ? nullptr : &Fk(w, kActor), w);
    RetObject(&f, 16, p.nullStackObjects && chance(20) ? nullptr : &Fk(w, kS0 + r() % kObjects), w);
    RetObject(&f, 24, p.nullTableEntries && chance(10) ? nullptr : &Fk(w, kT0 + r() % kEntries), w);
}

void SetupObjects(WorldP &w, std::mt19937 &r)
{
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    const uint32_t entryMask = chance(75) ? 0xffffffffu : 0x1u << (r() % 6);
    for (int t = 0; t < kEntries; ++t) {
        Fake &f = Fk(w, kT0 + t);
        for (int slot : {14, 18, 29, 33, 39, 78, 94}) Use(f.vt, slot, 0);
        Use(f.vt, 11, 1);
        RetValue(&f, 14, NoiseAl(r, chance(50)), w);
        RetValue(&f, 18, entryMask, w);
        RetValue(&f, 29, NoiseAl(r, chance(40)), w);
        RetValue(&f, 33, NoiseAl(r, chance(25)), w);
        RetValue(&f, 39, NoiseAl(r, chance(25)), w);
        RetValue(&f, 78, chance(15) ? (r() & 0xffff0000u) : (r() & 0xffff0000u) | (1 + r() % 0xffff), w);
        RetValue(&f, 94, NoiseAl(r, chance(70)), w);
        RetValue(&f, 11, NoiseAl(r, chance(80)), w);
    }
    for (int s = 0; s < kObjects; ++s) {
        Fake &f = Fk(w, kS0 + s);
        for (int slot : {47, 64, 89, 92, 95, 98, 104, 109, 61, 131, 135}) Use(f.vt, slot, 0);
        for (int slot : {25, 190}) Use(f.vt, slot, 1);
        Use(f.vt, 238, 5);
        f.vt[0] = reinterpret_cast<void *>(&QueryInterfaceFake);
        RetValue(&f, 47, Val16Noise(r), w);
        RetValue(&f, 98, chance(25) ? 2u : r() % 5, w);
        RetValue(&f, 104, NoiseAl(r, chance(45)), w);
        RetValue(&f, 95, r(), w);
        RetValue(&f, 131, Val16Noise(r) & 0xffff0003u, w);
        RetValue(&f, 135, NoiseAl(r, chance(12)), w);
        RetValue(&f, 190, NoiseAl(r, chance(12)), w);
        RetValue(&f, 25, NoiseAl(r, chance(60)), w);
        RetObject(&f, 64, &Fk(w, kAttributes), w);
        RetObject(&f, 89, &Fk(w, kRowTable), w);
        RetObject(&f, 92, w.scratch + kNameOffset, w);
        RetObject(&f, 109, chance(10) ? nullptr : &Fk(w, kRowTable), w);
        RetObject(&f, 61, &Fk(w, kChain), w);
        RetObject(&f, 0, chance(70) ? &Fk(w, kChain) : nullptr, w);
    }
    Use(Fk(w, kAttributes).vt, 3, 1);
    RetValue(&Fk(w, kAttributes), 3, chance(60) ? 1u | (r() & ~1u) : (r() & ~1u), w);
    Use(Fk(w, kRowTable).vt, 5, 1);
    for (int i = 0; i < 4; ++i) EnqueueValue(&Fk(w, kRowTable), 5, chance(35) ? r() % 2 : 2 + r() % 3, w);
    Use(Fk(w, kRowTable).vt, 6, 3);
    Use(Fk(w, kRowTable).vt, 9, 2);
    const uint32_t cells[] = {0, 1, 98, 99, 100, 101, 5000};
    RetValue(&Fk(w, kRowTable), 9, cells[r() % 7], w);
    Use(Fk(w, kRowTable).vt, 10, 3);
    // The chain behind a stack object's interface query: tracker (2, 6) -> host (11) -> first (27) -> second (4).
    Fake &chain = Fk(w, kChain);
    Use(chain.vt, 2, 0);
    FixedObject(w, kChain, 6, 0, &chain);
    FixedObject(w, kChain, 11, 0, &Fk(w, kS0 + r() % kObjects));
    FixedObject(w, kChain, 27, 0, &chain);
    FixedObject(w, kChain, 4, 0, &Fk(w, kAttributes));
}

// The actor, its Sim object and tree sim, the settings and the recorder of the check-tree runner.
void SetupCheckWorld(WorldP &w, std::mt19937 &r)
{
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    Fixed(w, kActor, 52, 0, chance(75) ? 0x1u << (r() % 6) : 0u);
    Fk(w, kActor).vt[8] = reinterpret_cast<void *>(&GetSimObjectFake);
    RetObject(&Fk(w, kActor), 8, &Fk(w, kActorObject), w);
    FixedObject(w, kActor, 9, 0, &Fk(w, kTreeSim));
    Fixed(w, kActor, 30, 0, NoiseAl(r, chance(35)));
    Fixed(w, kActor, 38, 0, NoiseAl(r, chance(50)));
    Fixed(w, kActor, 20, 0, r() % 17);

    Use(Fk(w, kActorObject).vt, 13, 1);
    // GetAttribute answers per call, in the order the runner asks (attributes 0x32, 6, 0x65, 0x68).
    for (int call = 0; call < 4; ++call) EnqueueValue(&Fk(w, kActorObject), 13, (r() & 0xffff0000u) | (chance(40) ? 0u : r() % 4), w);
    Use(Fk(w, kActorObject).vt, 14, 2);
    Fk(w, kActorObject).vt[19] = reinterpret_cast<void *>(&GetArgumentFake);
    Fk(w, kActorObject).vt[20] = reinterpret_cast<void *>(&SetArgumentFake);
    Fixed(w, kActorObject, 47, 0, Val16Noise(r) & 0xffff03ffu);
    Fixed(w, kActorObject, 131, 0, Val16Noise(r) & 0xffff0003u);

    Fk(w, kTreeSim).vt[15] = reinterpret_cast<void *>(&CallTreeFake);
    RetValue(&Fk(w, kTreeSim), 15, NoiseAl(r, chance(55)), w);
    FixedObject(w, kSettings, 3, 0, &Fk(w, kSettings));
    Fixed(w, kSettings, 31, 0, NoiseAl(r, chance(60)));
    Fixed(w, kSettings, 15, 1, chance(50) ? (r() & 0xffff0000u) : (r() & 0xffff0000u) | (1 + r() % 0xffff));

    Use(Fk(w, kRecorder).vt, 7, 1);
    uint16_t *record = reinterpret_cast<uint16_t *>(w.scratch + kRecordOffset);
    const int16_t counters[] = {0, 1, -1, 5, -7, 32767, -32768};
    record[3] = static_cast<uint16_t>(counters[r() % 7]);
    record[4] = static_cast<uint16_t>(r());
    FixedObject(w, kRecorder, 57, 4, chance(20) ? nullptr : static_cast<const void *>(record));

    Fk(w, kSink).vt[14] = reinterpret_cast<void *>(&WriteTextFake);
    Use(Fk(w, kSink).vt, 9, 1);
    gLogMode = chance(35) ? (chance(70) ? 2u : 1u) : 0u;
}

// The game globals the check-tree runner reads: the settings singleton (0x10cc288) and the trace log (0x10149e8).
void SetGlobals(difftest::Context &ctx, WorldP &w)
{
    *ctx.image.At<void *>(0x10cc288) = &w.fk[kSettings];
    uint8_t *const log = ctx.image.At<uint8_t>(0x10149e8);
    log[0] = gLogMode != 0;
    log[1] = gLogMode != 0;
    *reinterpret_cast<int32_t *>(log + 4) = 3;
    *reinterpret_cast<void **>(log + 0x2c) = gLogMode == 2 ? static_cast<void *>(&w.fk[kSink]) : nullptr;
}

struct SavedGlobals {
    void *settings;
    uint8_t log[0x30];
    explicit SavedGlobals(difftest::Context &ctx)
    {
        settings = *ctx.image.At<void *>(0x10cc288);
        std::memcpy(log, ctx.image.At<uint8_t>(0x10149e8), sizeof(log));
    }
    void Restore(difftest::Context &ctx) const
    {
        *ctx.image.At<void *>(0x10cc288) = settings;
        std::memcpy(ctx.image.At<uint8_t>(0x10149e8), log, sizeof(log));
    }
};

void BuildPersonWorld(WorldP &w, uint32_t seed, uint32_t salt, const Params &p)
{
    std::mt19937 r(seed * 2654435761u + salt);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    FillRandom(w.scratch, sizeof(w.scratch), r);
    std::memcpy(w.scratch + kNameOffset, "Stack Object", 13);
    InitPerson(w);
    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);

    for (int i = 0; i < kInteractions; ++i) SetupInteraction(w, r, i, p);
    SetupObjects(w, r);
    SetupCheckWorld(w, r);

    // The person's queues and interaction fields.
    auto interaction = [&]() { return &Fk(w, kI0 + r() % kInteractions); };
    auto queueOf = [&](std::deque<void *> *q, uint32_t maxSize, uint32_t minSize) {
        new (q) std::deque<void *>();
        const uint32_t pushes = minSize + r() % (maxSize + 3);
        for (uint32_t i = 0; i < pushes; ++i) q->push_back(interaction());
        const uint32_t pops = pushes > minSize && chance(40) ? r() % (pushes - minSize) : 0;
        for (uint32_t i = 0; i < pops; ++i) q->pop_front();
    };
    queueOf(reinterpret_cast<std::deque<void *> *>(self->mQueueA), p.bigQueue ? 20 : 4, 0);
    queueOf(reinterpret_cast<std::deque<void *> *>(self->mQueueB), p.bigQueue ? 20 : 4, 0);
    queueOf(reinterpret_cast<std::deque<void *> *>(self->mHistory), p.historyNotEmpty ? 14 : 2, 0);
    self->mCurrentInteraction = interaction();
    self->mPreviousInteraction = p.previousAlways || chance(60) ? static_cast<void *>(interaction()) : nullptr;
    self->mSelectedInteraction = chance(60) ? static_cast<void *>(interaction()) : nullptr;
    self->mReactionBegin = w.scratch + 0x40;
    self->mReactionEnd = w.scratch + 0x40 + (chance(20) ? 0 : 12 * (r() % 30) + (chance(30) ? r() % 12 : 0));
    self->mMotiveBegin = w.scratch + 0x200;
    self->mMotiveEnd = chance(50) ? self->mMotiveBegin : self->mMotiveBegin + 16 * (1 + r() % 3);
    self->mMotiveLimit = self->mMotiveEnd + 16;
    self->mReactionCountSelected = static_cast<uint16_t>(r());
    self->mReactionCountCurrent = static_cast<uint16_t>(r());

    // The person's own slots.
    Use(w.vtThis, 47, 0);
    RetValue(w.obj, 47, Val16Noise(r) & 0xffff03ffu, w);
    Use(w.vtThis, 221, 0);
    Use(w.vtTree, 29, 0);
    RetValue(w.obj + 4, 29, NoiseAl(r, chance(50)), w);
    Use(w.vtPerson, 10, 2);
    for (int i = 0; i < 4; ++i) EnqueueValue(w.obj + 0x2f0, 10, chance(30) ? r() & 0xffff0000u : Val16Noise(r), w);
    Use(w.vtPerson, 11, 3);
    Use(w.vtPerson, 57, 0);
    RetValue(w.obj + 0x2f0, 57, Val16Noise(r), w);
    Use(w.vtPerson, 132, 0);
    Use(w.vtPerson, 133, 0);
    RetObject(w.obj + 0x2f0, 132, p.currentNullable && chance(25) ? nullptr : static_cast<const void *>(interaction()), w);
    RetObject(w.obj + 0x2f0, 133, chance(40) ? nullptr : static_cast<const void *>(interaction()), w);
}

WorldP &WorldOf(uint8_t *o)
{
    return *reinterpret_cast<WorldP *>(o - offsetof(WorldP, obj));
}

Fake &Interaction(WorldP &w, uint32_t pick)
{
    return Fk(w, kI0 + pick % kInteractions);
}

using VoidThis = void(__thiscall *)(void *);
using VoidThisOne = void(__thiscall *)(void *, void *);

} // namespace
