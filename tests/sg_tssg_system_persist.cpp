// Differential tests for nTSSG::cTSSGSystem's resource lookups and save state (src/sg/cTSSGSystemPersist.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes with its vtable a table of logging functions
// (slots 25, 52, 76, 77, 96, 97, 98 log their arguments and answer what the seed sets), its animation system, persist
// service and entry list (mRefB4) recording fakes; the stream is a fake whose dword reads (slots 8 and 9) take the next
// word of a queue the seed draws, and whose string slot (15) logs the string it is given. cRZStopwatch::Elapsed (ours,
// 0x1c9256) is stood in on both sides with a counter. Keys handed by address are logged word by word by the fakes.
//
// Compared after each run: the result, the event log, the object's 0x3b8 bytes (normalised), the out words, every
// fake's reference count and the live-household cell.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlockTag = 0x6d000000;
constexpr uint32_t kDataTag = 0x6e000000;
constexpr uint32_t kHouseholdCell = 0x10cc2ac;
enum : uint32_t { evSelf = 0x80, evStand, evKey, evString };

enum : unsigned {
    fFramework = 0,
    fAnim,
    fPersist,
    fPersistObject,
    fList,
    fEntry,
    fRecord,
    fData,
    fHolder,
    fStream,
    fState,
    fHousehold,
    fClockOther,
    fClock,
    fFound,
    fPre,
    fNameObject,
    fRes0,
    fRes1,
    fRes2,
    fItem0,
    fItem1,
    fItem2,
};

// Blocks with a virtual-base table at +4 whose entries 1 and 2 reach fakes.
struct VBlock {
    uint32_t words[2];
    int32_t table[3];
    void Point(Obj *entry1, Obj *entry2)
    {
        words[0] = 0x0b1ec700;
        words[1] = P(table);
        table[0] = 0;
        table[1] = int32_t(P(entry1) - P(&words[1]));
        table[2] = int32_t(P(entry2) - P(&words[1]));
    }
};
// 0: slot 52's object (entry 2 the clock); 1: the named resource (entry 1 its name object); 2..4: resources.
VBlock gBlocks[5];

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddp pddddddd dddddddd dddddpdd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";
alignas(16) uint32_t gData[16];
// gData's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kDataKinds[] = "dddddddd dddddddd";
char gText[2][32]; // names the fakes give: [0] the resource's or entry's, [1] the caller's

uint32_t KeyWord(uint32_t address, int i) { return reinterpret_cast<const uint32_t *>(uintptr_t(address))[i]; }
void LogKey(uint32_t address, int words)
{
    Ev(evKey);
    for (int i = 0; i < words; ++i) Ev(KeyWord(address, i));
}
void LogText(const char *text)
{
    Ev(evString);
    if (!text) return Ev(0xdead);
    for (; *text; ++text) Ev(uint8_t(*text));
}
void Put(uint32_t address, uint32_t value) { *reinterpret_cast<uint32_t *>(uintptr_t(address)) = value; }

// ---- the seed's answers ----
struct Answers {
    uint32_t self[100];
    bool self25Gives, self98Gives;
    uint32_t elapsedStep;
    // the animation system
    uint32_t animLooks, animFinds; // slot 17: result; 0 leave *out, 1 null, 2 fFound
    // the persist service
    uint32_t persistFinds, persistFlag; // slot 7: 0 null, 1 the named resource; the flag byte
    uint32_t recordGives, holderGives, queryGives, stateGives, createGives, recordLeaves;
    // the stream queue
    uint32_t queue[24];
    unsigned items; // the animation list's length (WriteState)
};
Answers gA;
unsigned gQueuePos, gResourceNext, gElapsed;

// ---- the object's own vtable ----
void *gSelfVTable[100];
uint32_t __fastcall Self25(void *self, void *, uint32_t key, uint32_t name, uint32_t a3, uint32_t out, uint32_t force)
{
    Ev(evSelf), Ev(25), Ev(Norm(P(self))), Ev(Norm(name)), Ev(a3), Ev(Norm(out)), Ev(force & 0xff);
    LogKey(key, 2);
    if (gA.self25Gives && gResourceNext < 3) Put(out, P(&gBlocks[2 + gResourceNext++]));
    return gA.self[25];
}
uint32_t __fastcall Self98(void *self, void *, uint32_t key, uint32_t name, uint32_t out)
{
    Ev(evSelf), Ev(98), Ev(Norm(P(self))), Ev(Norm(name)), Ev(Norm(out));
    LogKey(key, 2);
    if (gA.self98Gives) Put(out, P(&F(fPre)));
    return gA.self[98];
}
uint32_t __fastcall Self52(void *self, void *)
{
    Ev(evSelf), Ev(52), Ev(Norm(P(self)));
    return P(&gBlocks[0]);
}
template <int S> uint32_t __fastcall SelfEntry(void *self, void *, uint32_t group, uint32_t name)
{
    Ev(evSelf), Ev(S), Ev(Norm(P(self))), Ev(group), Ev(name);
    return gA.self[S];
}
template <int S> uint32_t __fastcall SelfStream(void *self, void *, uint32_t stream)
{
    Ev(evSelf), Ev(S), Ev(Norm(P(self))), Ev(Norm(stream));
    return gA.self[S];
}

// ---- the stand-in stopwatch ----
uint32_t __fastcall StandElapsed(const void *watch, void *)
{
    Ev(evStand), Ev(0x1c9256), Ev(Norm(P(watch)));
    gElapsed += gA.elapsedStep;
    return gElapsed;
}

// ---- fakes' custom slots ----
uint32_t AnimLook(Obj *, uint32_t *args) // slot 17 (key, name, out)
{
    LogKey(args[0], 2);
    if (gA.animFinds == 1) Put(args[2], 0);
    if (gA.animFinds == 2) Put(args[2], P(&F(fFound)));
    return gA.animLooks;
}
uint32_t PersistFind(Obj *, uint32_t *args) // slot 7 (key, type, out, flag)
{
    LogKey(args[0], 4);
    Put(args[2], gA.persistFinds ? P(&gBlocks[1]) : 0);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[3])) = uint8_t(gA.persistFlag);
    return gA.persistFinds;
}
uint32_t NameOf(Obj *, uint32_t *) { return P(gText[0]); }
uint32_t RecordOpen(Obj *, uint32_t *args) // persist object slot 39 (key, &record, 0); service slot 34 (key, type, &holder)
{
    LogKey(args[0], 4);
    return 0;
}
uint32_t LoadRecord(Obj *self, uint32_t *args)
{
    RecordOpen(self, args);
    if (gA.recordGives) Put(args[1], P(&F(fRecord)));
    return gA.recordGives;
}
uint32_t OpenHolder(Obj *self, uint32_t *args)
{
    RecordOpen(self, args);
    if (gA.holderGives) Put(args[2], P(&F(fHolder)));
    return gA.holderGives;
}
uint32_t HolderOpen(Obj *self, uint32_t *args) // holder slot 22 (key, &record, 2)
{
    RecordOpen(self, args);
    if (gA.recordGives || gA.recordLeaves) Put(args[1], P(&F(fRecord))); // leaves: a record although it fails
    return gA.recordGives;
}
uint32_t RecordQuery(Obj *, uint32_t *args) // record slot 0 (iid, &data)
{
    if (gA.queryGives) Put(args[1], P(&F(fData)));
    return gA.queryGives;
}
uint32_t MakeState(Obj *, uint32_t *args) // service slot 70 (type, &state)
{
    Put(args[1], P(&F(fState)));
    return 0;
}
uint32_t FindState(Obj *, uint32_t *args) // service slot 69 (stream, iid, 0, type, &state, 0)
{
    if (gA.stateGives) Put(args[4], P(&F(fState)));
    return gA.stateGives;
}
uint32_t StreamRead(Obj *, uint32_t *args) // stream slots 8 / 9 when reading
{
    Put(args[0], gA.queue[gQueuePos++ % 24]);
    return 1;
}
uint32_t StreamString(Obj *, uint32_t *args) // stream slot 15 (cIGZString *)
{
    const auto *text = reinterpret_cast<const cRZString *>(uintptr_t(args[0]));
    Ev(P(text->mVTable) == P(GameAddress<void>(cRZString::kVTable))), Ev(text->mRefCount);
    Ev(uint32_t(text->mString.capacity()));
    LogText(text->mString.c_str());
    return 1;
}
uint32_t GroupOf(Obj *, uint32_t *args) // persist service slot 11 (cIGZString *)
{
    StreamString(nullptr, args);
    return gA.self[12];
}
uint32_t HolderCreate(Obj *self, uint32_t *args) // holder slot 23 (key, &record)
{
    RecordOpen(self, args);
    if (gA.createGives) Put(args[1], P(&F(fRecord)));
    return gA.createGives;
}
uint32_t ItemKey(Obj *self, uint32_t *args) // item slot 32 (&pair)
{
    Put(args[0], self->id * 3);
    Put(args[0] + 4, self->id * 5 + 1);
    return 0;
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed};
    Fixture()
    {
        gSelfVTable[25] = reinterpret_cast<void *>(&Self25);
        gSelfVTable[52] = reinterpret_cast<void *>(&Self52);
        gSelfVTable[76] = reinterpret_cast<void *>(&SelfEntry<76>);
        gSelfVTable[77] = reinterpret_cast<void *>(&SelfEntry<77>);
        gSelfVTable[96] = reinterpret_cast<void *>(&SelfStream<96>);
        gSelfVTable[97] = reinterpret_cast<void *>(&SelfStream<97>);
        gSelfVTable[98] = reinterpret_cast<void *>(&Self98);
        globals.SetWord(kHouseholdCell, 0);
        gFrameworkIndex = fFramework;
    }
};

// Sets every fake's slots (the answers come from gA).
void Wire(bool streamReads)
{
    F(fAnim).Set(17, "ppp", 0u, &AnimLook);
    F(fAnim).Set(14, "p");
    F(fAnim).Set(15, "pd");
    F(fAnim).Set(20, "d");
    F(fAnim).Set(33, 0, gA.self[0]);
    F(fAnim).Set(34, 0, gA.self[1]);
    F(fAnim).Set(36, 0, gA.self[2]);
    F(fAnim).Set(19, 0, gA.items ? P(&F(fItem0)) : 0u);
    for (unsigned i = 0; i < 3; ++i) {
        F(fItem0 + i).Set(32, "p", 0u, &ItemKey);
        F(fItem0 + i).Set(61, 0, i + 1 < gA.items ? P(&F(fItem0 + i + 1)) : 0u);
    }
    F(fFound).Set(59, 0, gA.self[3]);
    F(fPre).Set(59, 0, gA.self[3] ^ 1);
    F(fPersist).Set(7, "pdpp", 0u, &PersistFind);
    F(fPersist).Set(3, 0, &F(fPersistObject));
    F(fPersist).Set(34, "pdp", 0u, &OpenHolder);
    F(fPersist).Set(69, "pd-dp-", 0u, &FindState);
    F(fPersist).Set(70, "dp", 0u, &MakeState);
    F(fPersist).Set(71, "ppd--");
    F(fPersist).Set(11, "p", 0u, &GroupOf);
    F(fAnim).Set(27, "p");
    F(fAnim).Set(28, "p");
    F(fAnim).Set(40, "p");
    F(fPersistObject).Set(39, "pp-", 0u, &LoadRecord);
    F(fNameObject).Set(17, 0, 0u, &NameOf);
    F(fList).Set(35, "p", gA.self[4]);
    F(fList).Set(36, "d", gA.self[5] ? P(&F(fEntry)) : 0u);
    F(fEntry).Set(12, 0, 0u, &NameOf);
    F(fEntry).Set(11, "p", gA.self[6]);
    F(fEntry).Set(10, "p", gA.self[7]);
    F(fRecord).Set(0, "dp", 0u, &RecordQuery);
    F(fRecord).Set(6, 0, gA.self[8]);
    F(fHolder).Set(22, "ppd", 0u, &HolderOpen);
    F(fHolder).Set(24, "p");
    F(fHolder).Set(10, 0);
    F(fHolder).Set(23, "pp", 0u, &HolderCreate);
    F(fStream).Set(11, 0, gA.self[9]);
    if (streamReads) {
        F(fStream).Set(8, "p", 0u, &StreamRead);
        F(fStream).Set(9, "p", 0u, &StreamRead);
    } else {
        F(fStream).Set(8, "d");
        F(fStream).Set(9, "d");
    }
    F(fStream).Set(15, "p", 0u, &StreamString);
    F(fState).Set(3, 0, &F(fStream));
    F(fState).Set(4, 0, &F(fStream));
    F(fHousehold).Set(18, 0, gA.self[10]);
    F(fClock).Set(2, "d");
    F(fClock).Set(11, 0, gA.self[11]);
    gBlocks[0].Point(&F(fClockOther), &F(fClock));
    gBlocks[1].Point(&F(fNameObject), &F(fClockOther));
    for (unsigned i = 0; i < 3; ++i) gBlocks[2 + i].Point(&F(fRes0 + i), &F(fClockOther));
}

Blob Outcome(uint32_t result)
{
    Blob b = gEv;
    b.push_back(0xeeee);
    b.push_back(result);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    svcfx::DumpWords(b, gData, sizeof(gData), kDataKinds);
    CaptureFakes(b);
    b.push_back(Norm(*GameAddress<uint32_t>(kHouseholdCell)));
    return b;
}

template <class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, const uint32_t *data,
             bool streamReads, bool household, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        gQueuePos = 0, gResourceNext = 0, gElapsed = 1000;
        Wire(streamReads);
        *GameAddress<void *>(kHouseholdCell) = household ? &F(fHousehold) : nullptr;
        std::memcpy(gObject, start, kObjectSize);
        std::memcpy(gData, data, sizeof(gData));
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
        AddRange(gData, sizeof(gData), kDataTag);
        ctx.Mark(side ? "ours" : "original");
        const uint32_t result = run(side == 0);
        outcome[side] = Outcome(result);
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

// A start object: random bytes, our logging vtable, the fakes in the members the code reads, a bucket in range.
void DrawWorld(std::mt19937 &gen, uint8_t *start, uint32_t *data)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto *sys = reinterpret_cast<cTSSGSystem *>(start);
    sys->mVTable = gSelfVTable;
    sys->mAnimSystem.mObject = &F(fAnim);
    sys->mPersistService.mObject = &F(fPersist);
    sys->mRefB4.mObject = gen() % 8 ? &F(fList) : nullptr;
    sys->mStatBucket = gen() % 10;
    for (unsigned i = 0; i < 16; ++i) data[i] = gen();
    std::memset(&gA, 0, sizeof(gA));
    for (uint32_t &v : gA.self) v = gen() % 3 ? gen() % 2 : gen();
    for (unsigned s : {25u, 76u, 77u, 96u, 97u, 98u}) gA.self[s] = gen() % 2;
    for (unsigned s : {3u, 5u, 6u, 7u, 8u}) gA.self[s] = gen() % 4 ? gen() % 2 : gen() % 2;
    gA.self25Gives = gen() % 2, gA.self98Gives = gen() % 3 == 0;
    gA.elapsedStep = gen() % 1000;
    gA.animLooks = gen() % 2, gA.animFinds = gen() % 3;
    gA.persistFinds = gen() % 4 != 0, gA.persistFlag = gen() % 3 ? gen() % 2 : 2;
    gA.recordGives = gen() % 4 != 0, gA.holderGives = gen() % 4 != 0, gA.queryGives = gen() % 4 != 0;
    gA.stateGives = gen() % 2, gA.createGives = gen() % 3 != 0, gA.recordLeaves = gen() % 2;
    for (uint32_t &w : gA.queue) w = gen() % 4 ? gen() % 6 : gen();
    gA.items = gen() % 4;
    const char *names[] = {"Wall_Lights", "wall_lights", "Floor", "", "WALL_LIGHTS", "Wall_Lights2"};
    std::strcpy(gText[0], names[gen() % 6]);
    std::strcpy(gText[1], names[gen() % 6]);
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x24a470 FindAnimResource: the animation system finds it (counted by its slot 59) or not; then slot 98 when the lookup
// failed or `force`, slot 14 when that fails too; slot 15 for anything found.
DIFF_TEST(sg_tssg_system_find_anim_resource, 0x24a470)
{
    Fixture fixture;
    auto original =
        ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *, const char *, uint32_t, void **, uint8_t)>(0x24a470);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24a470u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        data[4] = gen() % 2 ? P(&F(fPre)) : 0;
        const bool named = gen() % 2;
        const uint32_t a3 = gen();
        const uint8_t force = uint8_t(gen() % 3);
        Compare(ctx, "find_anim_resource", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            const char *name = named ? gText[1] : nullptr;
            void **out = reinterpret_cast<void **>(&gData[4]);
            if (useOriginal) return original(gObject, gData, name, a3, out, force);
            return Ours()->FindAnimResource(gData, name, a3, out, force);
        });
    }
}

// 0x24b410 FindPersistResource: found or not, flagged or not, the name absent, matching (case-insensitively) or not;
// the time spent added to the bucket.
DIFF_TEST(sg_tssg_system_find_persist_resource, 0x24b410)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *, const char *, void **)>(0x24b410);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x24b410u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const bool named = gen() % 3 != 0;
        Compare(ctx, "find_persist_resource", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            const char *name = named ? gText[1] : nullptr;
            void **out = reinterpret_cast<void **>(&gData[4]);
            if (useOriginal) return original(gObject, gData, name, out);
            return Ours()->FindPersistResource(gData, name, out);
        });
    }
}

// 0x24b720 LoadEntry: no list, no entry, no record, no interface, and the entry's and record's answers.
DIFF_TEST(sg_tssg_system_load_entry, 0x24b720)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, const char *)>(0x24b720);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 15485863u + 0x24b720u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const uint32_t group = gen();
        Compare(ctx, "load_entry", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, group, gText[1]);
            return Ours()->LoadEntry(group, gText[1]);
        });
    }
}

// 0x2548b0 SaveEntry: no list, no entry, no holder, no record, no interface, and the entry's answer.
DIFF_TEST(sg_tssg_system_save_entry, 0x2548b0)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, const char *)>(0x2548b0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x2548b0u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const uint32_t group = gen();
        Compare(ctx, "save_entry", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, group, gText[1]);
            return Ours()->SaveEntry(group, gText[1]);
        });
    }
}

// 0x24c890 Save: the three entries (slot 77), the state written (slot 97's answer) and stored.
DIFF_TEST(sg_tssg_system_save, 0x24c890)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x24c890);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24c890u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        Compare(ctx, "save", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, &F(fStream));
            return Ours()->Save(&F(fStream));
        });
    }
}

// 0x24bf20 Load: the three entries (slot 76), then the stored state (slot 96) or the household's default.
DIFF_TEST(sg_tssg_system_load, 0x24bf20)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x24bf20);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x24bf20u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        Compare(ctx, "load", seed, start, data, false, true, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, &F(fStream));
            return Ours()->Load(&F(fStream));
        });
    }
}

// 0x24b9a0 ReadState: versions 0..5 (and others), counts 0..5 (and others capped by the queue's words), slot 25 giving a
// resource or not.
DIFF_TEST(sg_tssg_system_read_state, 0x24b9a0)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x24b9a0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 15485863u + 0x24b9a0u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        // The queue: id, version, counter (slot 8), clock offset, count, then the keys. The count stays small.
        gA.queue[4] = gen() % 6;
        Compare(ctx, "read_state", seed, start, data, true, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, &F(fStream));
            return Ours()->ReadState(&F(fStream));
        });
    }
}

// 0x254a30 WriteState: the clock, the animation values and lists of 0..3 resources.
DIFF_TEST(sg_tssg_system_write_state, 0x254a30)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x254a30);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x254a30u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        Compare(ctx, "write_state", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject, &F(fStream));
            return Ours()->WriteState(&F(fStream));
        });
    }
}

// 0x24b520 LoadNeighborhoodAnimation: holder, record and interface there or not.
DIFF_TEST(sg_tssg_system_load_neighborhood_animation, 0x24b520)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24b520);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24b520u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        Compare(ctx, "load_neighborhood_animation", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else Ours()->LoadNeighborhoodAnimation();
            return 0;
        });
    }
}

// 0x2546a0 SaveNeighborhoodAnimation: holder there or not, the record opened, created or neither, interface or not.
DIFF_TEST(sg_tssg_system_save_neighborhood_animation, 0x2546a0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2546a0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x2546a0u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        Compare(ctx, "save_neighborhood_animation", seed, start, data, false, false, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else Ours()->SaveNeighborhoodAnimation();
            return 0;
        });
    }
}
