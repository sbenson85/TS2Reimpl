// Differential tests for cEdithObjectTestSim (src/simantics/EdithObjectTestSim.h): the constructor 0x44f510, the destructor
// 0x44f540, Begin 0x451060 (slot 3) and the check-tree runner 0x451f00 (slot 9), and the two trace-log writers they use,
// 0x526fc0 and 0x526b60. Every object the code reaches is a recording fake of the person-primitive harness
// (tests/simantics_prims_person.h); the game globals the runner reads (the settings singleton 0x10cc288 and the trace log
// 0x10149e8) are set in the mapped image, which ours reads too.
#include "simantics_prims_person.h"

#include "simantics/EdithObjectTestSim.h"
#include "simantics/SimTraceLog.h"

namespace {

constexpr int kSimOffset = 0x40; // the test sim inside the world's scratch area
constexpr int kNameOffset = 0x100;
constexpr int kRecordOffset = 0x180;

// Fake indexes of the check-tree runner's world.
enum : int { kInteraction = kF0, kActor, kTableEntry, kStackObject, kActorObject, kTreeSim, kRecorder, kSettings, kTuning, kSink, kOwner };

cEdithObjectTestSim &SimOf(WorldP &w)
{
    return *reinterpret_cast<cEdithObjectTestSim *>(w.scratch + kSimOffset);
}

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
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

uint32_t Noise(std::mt19937 &r)
{
    return r() & 0xffff0000u;
}

void BuildCheck(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x451f00);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.scratch, sizeof(w.scratch), r);
    FillRandom(w.obj, sizeof(w.obj), r);
    std::memcpy(w.scratch + kNameOffset, "Stack Object", 13);
    for (int i = 0; i < kFk; ++i) w.fk[i].vt = w.vtFk[i];

    auto chance = [&](uint32_t percent) { return r() % 100 < percent; };

    cEdithObjectTestSim &sim = SimOf(w);
    sim.mFlagA = chance(30) ? 1 : 0;
    sim.mFlagB = chance(20) ? 1 : 0;
    sim.mSavedFor = chance(40) ? &w.fk[kActor] : nullptr;

    // The interaction.
    Fake &e = w.fk[kInteraction];
    const int32_t id = chance(12) ? -1 - static_cast<int32_t>(r() % 3) : static_cast<int32_t>(r() % 6);
    Fixed(w, kInteraction, 18, 0, static_cast<uint32_t>(id));
    e.vt[40] = reinterpret_cast<void *>(&SetCheckPassedFake);
    Use(e.vt, 38, 1);
    Fixed(w, kInteraction, 11, 0, chance(50) ? 1u : r());
    FixedObject(w, kInteraction, 17, 0, chance(6) ? nullptr : &w.fk[kActor]);
    FixedObject(w, kInteraction, 24, 0, chance(6) ? nullptr : &w.fk[kTableEntry]);
    FixedObject(w, kInteraction, 16, 0, chance(6) ? nullptr : &w.fk[kStackObject]);
    for (int slot : {42, 56, 75, 77}) Use(e.vt, slot, 1);

    // The actor's person interface and the Sim object it hands out.
    Fixed(w, kActor, 52, 0, chance(75) ? 0x1u << (r() % 6) : 0u);
    const uint32_t entryMask = chance(75) ? 0xffffffffu : 0x1u << (r() % 6);
    Fixed(w, kTableEntry, 18, 0, entryMask);
    FixedObject(w, kActor, 8, 0, &w.fk[kActorObject]);
    FixedObject(w, kActor, 9, 0, &w.fk[kTreeSim]);
    Fixed(w, kActor, 30, 0, NoiseAl(r, chance(35)));
    Fixed(w, kActor, 38, 0, NoiseAl(r, chance(50)));
    Fixed(w, kActor, 20, 0, r() % 17);

    // The table entry.
    Fixed(w, kTableEntry, 11, 1, NoiseAl(r, chance(80)));
    Fixed(w, kTableEntry, 33, 0, NoiseAl(r, chance(25)));
    Fixed(w, kTableEntry, 39, 0, NoiseAl(r, chance(25)));
    Fixed(w, kTableEntry, 78, 0, chance(15) ? Noise(r) : Noise(r) | (1 + r() % 0xffff));

    // The stack object.
    Fixed(w, kStackObject, 135, 0, NoiseAl(r, chance(12)));
    Fixed(w, kStackObject, 190, 1, NoiseAl(r, chance(12)));
    Fixed(w, kStackObject, 25, 1, NoiseAl(r, chance(60)));
    Fixed(w, kStackObject, 47, 0, Noise(r) | (r() % 0x400));
    FixedObject(w, kStackObject, 89, 0, &w.fk[kOwner]);
    FixedObject(w, kStackObject, 92, 0, w.scratch + kNameOffset);
    Fixed(w, kStackObject, 131, 0, Noise(r) | (r() % 3));

    // The actor's Sim object: attributes, tree arguments.
    Use(w.fk[kActorObject].vt, 13, 1);
    // GetAttribute answers per call, in the order the runner asks (attributes 0x32, 6, 0x65, 0x68).
    for (int call = 0; call < 4; ++call) EnqueueValue(&w.fk[kActorObject], 13, Noise(r) | (chance(40) ? 0u : r() % 4), w);
    Use(w.fk[kActorObject].vt, 14, 2);
    Use(w.fk[kActorObject].vt, 19, 1);
    Use(w.fk[kActorObject].vt, 20, 2);
    Fixed(w, kActorObject, 47, 0, Noise(r) | (r() % 0x400));
    Fixed(w, kActorObject, 131, 0, Noise(r) | (r() % 3));

    // The tree sim, the settings and the recorder.
    w.fk[kTreeSim].vt[15] = reinterpret_cast<void *>(&CallTreeFake);
    RetValue(&w.fk[kTreeSim], 15, NoiseAl(r, chance(55)), w);
    FixedObject(w, kSettings, 3, 0, &w.fk[kTuning]);
    Fixed(w, kSettings, 31, 0, NoiseAl(r, chance(60)));
    Fixed(w, kTuning, 15, 1, chance(50) ? Noise(r) : Noise(r) | (1 + r() % 0xffff));
    Use(w.fk[kRecorder].vt, 7, 1);
    uint16_t *record = reinterpret_cast<uint16_t *>(w.scratch + kRecordOffset);
    const int16_t counters[] = {0, 1, -1, 5, -7, 32767, -32768};
    record[3] = static_cast<uint16_t>(counters[r() % 7]);
    record[4] = static_cast<uint16_t>(r());
    FixedObject(w, kRecorder, 57, 4, chance(20) ? nullptr : static_cast<const void *>(record));

    // The trace sink.
    w.fk[kSink].vt[14] = reinterpret_cast<void *>(&WriteTextFake);
    Use(w.fk[kSink].vt, 9, 1);
    gLogMode = chance(35) ? (chance(70) ? 2u : 1u) : 0u;
}

void SetGlobals(difftest::Context &ctx, WorldP &w)
{
    *ctx.image.At<void *>(0x10cc288) = &w.fk[kSettings];
    uint8_t *const log = ctx.image.At<uint8_t>(0x10149e8);
    log[0] = gLogMode != 0;
    log[1] = gLogMode != 0;
    *reinterpret_cast<int32_t *>(log + 4) = 3;
    *reinterpret_cast<void **>(log + 0x2c) = gLogMode == 2 ? static_cast<void *>(&w.fk[kSink]) : nullptr;
}

using RunFn = void(__thiscall *)(void *, void *, uint32_t, void *, uint32_t, uint32_t, uint32_t);

struct Args {
    uint32_t callMode, runTree, extra, bypass;
    bool recorder;
};

Args ArgsFor(uint32_t seed)
{
    std::mt19937 r(seed * 7919u + 3);
    Args a;
    a.callMode = r() % 4 == 0 ? r() : 2;
    a.runTree = r() % 5 == 0 ? 0 : (r() % 2 ? 1 : (r() | 0x100)); // (upper bytes are noise: only the byte counts)
    a.extra = r() % 3 == 0 ? r() : 0;
    a.bypass = r() % 4 == 0 ? (r() % 2 ? 1 : (r() & ~0xffu)) : 0;
    a.recorder = r() % 4 != 0;
    return a;
}

} // namespace

DIFF_TEST(cEdithObjectTestSim_RunCheckTree, 0x451f00)
{
    auto original = ctx.Original<RunFn>(0x451f00);
    void *const savedSettings = *ctx.image.At<void *>(0x10cc288);
    uint8_t savedLog[0x30];
    std::memcpy(savedLog, ctx.image.At<uint8_t>(0x10149e8), sizeof(savedLog));

    uint32_t early = 0, vetoed = 0, ranTree = 0, noTree = 0, recorded = 0, countersTouched = 0, logged = 0, saved = 0;
    gBeforeRun = [&](WorldP &w) { SetGlobals(ctx, w); };
    RunSeeds(
        ctx, 40000, BuildCheck,
        [&](uint8_t *o, void *f, const void *) {
            WorldP &w = *reinterpret_cast<WorldP *>(o - offsetof(WorldP, obj));
            const Args a = ArgsFor(gSeed);
            original(w.scratch + kSimOffset, f, a.callMode, a.recorder ? &w.fk[kRecorder] : nullptr, a.runTree, a.extra, a.bypass);
            gTrace.push_back(*reinterpret_cast<int32_t *>(ctx.image.At<uint8_t>(0x10149e8) + 4));
            return 0u;
        },
        [&](uint8_t *o, void *f, const void *) {
            WorldP &w = *reinterpret_cast<WorldP *>(o - offsetof(WorldP, obj));
            const Args a = ArgsFor(gSeed);
            SimOf(w).RunCheckTree(f, a.callMode, a.recorder ? &w.fk[kRecorder] : nullptr, a.runTree, a.extra, a.bypass);
            gTrace.push_back(*reinterpret_cast<int32_t *>(ctx.image.At<uint8_t>(0x10149e8) + 4));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            const std::vector<uint32_t> &tr = t.oa.trace;
            auto has = [&](uint32_t slotWord) {
                for (size_t i = 0; i + 1 < tr.size(); ++i)
                    if (tr[i + 1] == slotWord) return true;
                return false;
            };
            if (t.oa.thrown >= 0) return;
            if (tr.size() < 6) ++early;
            const bool tree = has(15u << 8 | 8u);
            ranTree += tree;
            noTree += !tree && tr.size() >= 6;
            vetoed += has(33u << 8 | 0u) || has(39u << 8 | 0u);
            recorded += has(7u << 8 | 1u);
            countersTouched += has(57u << 8 | 4u);
            logged += has(14u << 8 | 1u);
            saved += has(19u << 8 | 1u);
        });
    gBeforeRun = nullptr;
    std::memcpy(ctx.image.At<uint8_t>(0x10149e8), savedLog, sizeof(savedLog));
    *ctx.image.At<void *>(0x10cc288) = savedSettings;
    if (early < 500 || vetoed < 2000 || ranTree < 2000 || noTree < 2000 || recorded < 2000 || countersTouched < 300 || logged < 300 || saved < 500)
        ctx.Fail("coverage: early %u vetoed %u ranTree %u noTree %u recorded %u counters %u logged %u saved %u", early, vetoed, ranTree, noTree, recorded,
                 countersTouched, logged, saved);
}

namespace {

constexpr int kLogOffset = 0x1c0;

// A world with the test sim and the objects the small members reach.
void BuildSimBase(WorldP &w, uint32_t seed, uint32_t salt)
{
    std::mt19937 r(seed * 2654435761u + salt);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.scratch, sizeof(w.scratch), r);
    FillRandom(w.obj, sizeof(w.obj), r);
    for (int i = 0; i < kFk; ++i) w.fk[i].vt = w.vtFk[i];
    std::memcpy(w.scratch + kNameOffset, "Name", 5);
}

void BuildConstruct(WorldP &w, uint32_t seed)
{
    BuildSimBase(w, seed, 0x44f510);
}

void BuildDestruct(WorldP &w, uint32_t seed)
{
    BuildSimBase(w, seed, 0x44f540);
    gThrowAt = 0; // the original destructor is noexcept (a C++ exception leaving it terminates), so no error is injected
    std::mt19937 r(seed + 99);
    cEdithObjectTestSim &sim = SimOf(w);
    sim.mSavedFor = r() % 3 == 0 ? nullptr : &w.fk[kActor];
    FixedObject(w, kActor, 8, 0, &w.fk[kActorObject]);
    Use(w.fk[kActorObject].vt, 20, 2);
}

void BuildBegin(WorldP &w, uint32_t seed)
{
    BuildSimBase(w, seed, 0x451060);
    std::mt19937 r(seed + 7);
    SimOf(w).mVTable = w.vtThis;
    Use(w.vtThis, 5, 1);
    RetValue(w.scratch + kSimOffset, 5, r(), w);
    Fixed(w, kStackObject, 95, 0, r());
}

void BuildLog(WorldP &w, uint32_t seed)
{
    BuildSimBase(w, seed, 0x526fc0);
    std::mt19937 r(seed + 11);
    uint8_t *const log = w.scratch + kLogOffset;
    *reinterpret_cast<int32_t *>(log + 4) = static_cast<int32_t>(r() % 9) - 3;
    *reinterpret_cast<void **>(log + 0x2c) = r() % 4 == 0 ? nullptr : static_cast<void *>(&w.fk[kSink]);
    w.fk[kSink].vt[14] = reinterpret_cast<void *>(&WriteTextFake);
    Use(w.fk[kSink].vt, 9, 1);
    if (r() % 3 == 0) w.scratch[kNameOffset] = static_cast<uint8_t>('A' + r() % 26);
}

using Ctor = void *(__thiscall *)(void *);
using Dtor = void(__thiscall *)(void *);
using BeginFn = uint32_t(__thiscall *)(void *, void *, void *, uint32_t, uint32_t);
using LogStartFn = void(__thiscall *)(void *, int32_t, int32_t, const char *, int32_t);
using LogEndFn = void(__thiscall *)(void *);

WorldP &WorldOf(uint8_t *o)
{
    return *reinterpret_cast<WorldP *>(o - offsetof(WorldP, obj));
}

} // namespace

DIFF_TEST(cEdithObjectTestSim_Construct, 0x44f510)
{
    auto original = ctx.Original<Ctor>(0x44f510);
    RunSeeds(
        ctx, 2000, BuildConstruct,
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            return static_cast<uint32_t>(static_cast<uint8_t *>(original(w.scratch + kSimOffset)) - (w.scratch + kSimOffset));
        },
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            return static_cast<uint32_t>(reinterpret_cast<uint8_t *>(SimOf(w).Construct()) - (w.scratch + kSimOffset));
        },
        [](WorldP &, const Trial &) {});
}

DIFF_TEST(cEdithObjectTestSim_Destruct, 0x44f540)
{
    auto original = ctx.Original<Dtor>(0x44f540);
    uint32_t restored = 0, nothing = 0;
    RunSeeds(
        ctx, 10000, BuildDestruct,
        [&](uint8_t *o, void *, const void *) {
            original(WorldOf(o).scratch + kSimOffset);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            SimOf(WorldOf(o)).Destruct();
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            restored += !t.oa.trace.empty() && t.oa.thrown < 0;
            nothing += t.oa.trace.empty();
        });
    if (restored < 3000 || nothing < 2000) ctx.Fail("coverage: restored %u nothing %u", restored, nothing);
}

DIFF_TEST(cEdithObjectTestSim_Begin, 0x451060)
{
    auto original = ctx.Original<BeginFn>(0x451060);
    RunSeeds(
        ctx, 10000, BuildBegin,
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            std::mt19937 r(gSeed + 5);
            const uint32_t flagA = r(), flagB = r();
            return original(w.scratch + kSimOffset, &w.fk[kActor], &w.fk[kStackObject], flagA, flagB) & 0xffu;
        },
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            std::mt19937 r(gSeed + 5);
            const uint32_t flagA = r(), flagB = r();
            return static_cast<uint32_t>(SimOf(w).Begin(&w.fk[kActor], &w.fk[kStackObject], flagA, flagB));
        },
        [](WorldP &, const Trial &) {});
}

DIFF_TEST(SimTraceLog_LogInteractionCheckStart, 0x526fc0)
{
    auto original = ctx.Original<LogStartFn>(0x526fc0);
    RunSeeds(
        ctx, 5000, BuildLog,
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            std::mt19937 r(gSeed + 3);
            const int32_t unused = static_cast<int32_t>(r()), stackId = static_cast<int32_t>(r()), index = static_cast<int32_t>(r());
            original(w.scratch + kLogOffset, unused, stackId, reinterpret_cast<const char *>(w.scratch + kNameOffset), index);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            WorldP &w = WorldOf(o);
            std::mt19937 r(gSeed + 3);
            const int32_t unused = static_cast<int32_t>(r()), stackId = static_cast<int32_t>(r()), index = static_cast<int32_t>(r());
            reinterpret_cast<SimTraceLog *>(w.scratch + kLogOffset)
                ->LogInteractionCheckStart(unused, stackId, reinterpret_cast<const char *>(w.scratch + kNameOffset), index);
            return 0u;
        },
        [](WorldP &, const Trial &) {});
}

DIFF_TEST(SimTraceLog_LogInteractionCheckComplete, 0x526b60)
{
    auto original = ctx.Original<LogEndFn>(0x526b60);
    RunSeeds(
        ctx, 2000, BuildLog,
        [&](uint8_t *o, void *, const void *) {
            original(WorldOf(o).scratch + kLogOffset);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<SimTraceLog *>(WorldOf(o).scratch + kLogOffset)->LogInteractionCheckComplete();
            return 0u;
        },
        [](WorldP &, const Trial &) {});
}
