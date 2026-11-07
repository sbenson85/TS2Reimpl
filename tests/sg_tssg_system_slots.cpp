// Differential tests for nTSSG::cTSSGSystem's small primary slots and its reference counting (src/sg/cTSSGSystemSlots.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes with its vtable a table of logging functions
// (slots 1, 33, 49, 52, 88 log and answer what the seed sets); the reference fields a test reads hold recording fakes or
// null; the cached cells the original's getters read (the event manager 0x10cc8a8, the app 0x10cc288, GetCachedSlot23
// 0x10cc2a8, the persist service 0x10faf6c, the detail object 0x10f9028) hold fakes, or are empty and filled through the
// service object (slot 5) as the getters do. Virtual-base references are small blocks whose table reaches a fake.
//
// Compared after each run: the result, the event log, the object's 0x3b8 bytes (normalised), every fake's reference count
// and the cells.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "dd"}};

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlockTag = 0x6d000000;
constexpr uint32_t kDataTag = 0x6e000000;
enum : uint32_t { evSelf = 0x80, evStand };

enum : unsigned {
    fFramework = 0,
    fService,
    fEventManager,
    fEventHolder,
    fApp,
    fAppObject,
    fSlot23,
    fPersist,
    fDetail,
    fShadow,
    fRefB4,
    fRef124,
    fRef124Object,
    fParserTarget,
    fParser,
    fParserProps,
    fSlot49,
    fSlot49Object,
    fSlot33,
    fSlot33Object,
    fBaseA,
    fBaseB,
    fBaseOld,
    fSlot52Base,
};

constexpr uint32_t kEventManagerCell = 0x10cc8a8;
constexpr uint32_t kDetailCell = 0x10f9028;
constexpr uint32_t kCells[] = {kEventManagerCell, kAppCell, kSlot23Cache, kPersistCell, kDetailCell};
constexpr char kCellKinds[] = "ppppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


// ---- the object's own vtable ----
uint32_t gSelfRet[100];
template <int S> uint32_t __fastcall Self0(void *self, void *)
{
    Ev(evSelf), Ev(S), Ev(Norm(P(self)));
    return gSelfRet[S];
}
// Self1<S>'s argument by slot S (svcfx::Field, word S of the string).
constexpr char kSelfArgKinds[] =
    "-------- -------- -------- -------- -------- -------- -------- -------- "
    "-------- -------- -------- d";
template <int S> uint32_t __fastcall Self1(void *self, void *, uint32_t a)
{
    Ev(evSelf), Ev(S), Ev(Norm(P(self))), svcfx::Field(svcfx::gEv, a, kSelfArgKinds, S * 4);
    return gSelfRet[S];
}
void *gSelfVTable[100];

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
VBlock gBlocks[3];

uint32_t gServiceGives; // 0: nothing; else the event holder
uint32_t ServiceQuery(Obj *, uint32_t *args)
{
    uint32_t object = 0;
    if (args[0] == 0x5b1e896f && gServiceGives) object = P(&F(fEventHolder));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    std::vector<std::unique_ptr<shape::CodePatch>> image;
    template <class Stand> void Image(uint32_t rva, Stand stand)
    {
        image.emplace_back(new shape::CodePatch(GameAddress<void>(rva), shape::CodeOf(stand)));
    }
    Fixture()
    {
        gSelfVTable[1] = reinterpret_cast<void *>(&Self0<1>);
        gSelfVTable[33] = reinterpret_cast<void *>(&Self0<33>);
        gSelfVTable[49] = reinterpret_cast<void *>(&Self0<49>);
        gSelfVTable[52] = reinterpret_cast<void *>(&Self0<52>);
        gSelfVTable[88] = reinterpret_cast<void *>(&Self1<88>);
        globals.Set(0x10c9378, &F(fService));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";
alignas(16) uint8_t gData[64];
// gData's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kDataKinds[] = "pdppp";
// GetColor's and GetLotBounds' gData: what they write there is all data (a colour's floats, the bounds).
constexpr char kAllDataKinds[] = "dddddddd dddddddd";
const char *gDataKinds = kDataKinds; // this case's (Compare resets it; a prepare may choose another)

Blob Outcome(uint32_t result)
{
    Blob b = gEv;
    b.push_back(0xeeee);
    b.push_back(result);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    svcfx::DumpWords(b, gData, sizeof(gData), gDataKinds);
    CaptureFakes(b, kValueKinds);
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

// Runs both sides from `start`: the fakes reset, `prepare`, then `run` (returns the result to compare).
template <class Prepare, class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, Prepare prepare, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        F(fService).Set(5, "ddp", 0u, &ServiceQuery);
        for (uint32_t cell : kCells) *GameAddress<uint32_t>(cell) = 0;
        std::memset(gData, 0, sizeof(gData));
        gDataKinds = kDataKinds;
        prepare();
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
        AddRange(gData, sizeof(gData), kDataTag);
        ctx.Mark(side ? "ours" : "original");
        const uint32_t result = run(side == 0);
        outcome[side] = Outcome(result);
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

// A start object: random bytes, our logging vtable.
void DrawObject(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    reinterpret_cast<cTSSGSystem *>(start)->mVTable = gSelfVTable;
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

// Typical reference counts: 1 (the last), small, with the top bit (inactive) or not.
int32_t DrawCount(std::mt19937 &gen)
{
    const uint32_t low = gen() % 3 == 0 ? 1 : gen() % 4 == 0 ? 0 : 1 + gen() % 50;
    return int32_t(low | (gen() % 2 ? 0x80000000u : 0));
}

// ---- stand-ins ----
uint32_t gBaseAnswer;
uint8_t __fastcall StandBaseQuery(void *self, void *, uint32_t iid, void **out)
{
    Ev(evStand), Ev(0x148310), Ev(Norm(P(self))), Ev(iid), Ev(Norm(P(out)));
    if (out && gBaseAnswer) *out = &F(fBaseA);
    return uint8_t(gBaseAnswer);
}
void __fastcall StandBehavior(void *self, void *, uint32_t event, uint32_t a, void *data, uint32_t d)
{
    Ev(evStand), Ev(0x24abb0), Ev(Norm(P(self))), Ev(event), Ev(a), Ev(Norm(P(data))), Ev(d);
}
void __fastcall StandTunables(void *self, void *, void *properties)
{
    Ev(evStand), Ev(0x248720), Ev(Norm(P(self))), Ev(Norm(P(properties)));
}
void __cdecl StandFill(void *target) { Ev(evStand), Ev(0x274a50), Ev(Norm(P(target))); }
void __fastcall StandDestructor(void *self, void *) { Ev(evStand), Ev(0x246f00), Ev(Norm(P(self)) != 0); }

} // namespace

// 0x253d70 QueryInterface (and the faces' thunks 0x253daa at +8, 0x253db2 at +0xc): the system's ids, others (the base
// 0x148310 stood in over the image), a null out.
DIFF_TEST(sg_tssg_system_query_interface, 0x253d70)
{
    Fixture fixture;
    fixture.Image(0x148310, &StandBaseQuery);
    const uint32_t rvas[] = {0x253d70, 0x253daa, 0x253db2};
    const uint32_t offsets[] = {0, 8, 0xc};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x253d70u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const unsigned face = seed % 3;
        const uint32_t iids[] = {0xba353ce1, 0xfa353cdb, 0x00000001, gen()};
        const uint32_t iid = iids[gen() % 4];
        const bool nullOut = gen() % 5 == 0;
        const uint32_t answer = gen() % 2;
        auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(rvas[face]);
        Compare(ctx, "query_interface", seed, start, [&] { gBaseAnswer = answer; },
                [&](bool useOriginal) -> uint32_t {
                    void **out = nullOut ? nullptr : reinterpret_cast<void **>(gData);
                    void *self = gObject + offsets[face];
                    if (useOriginal) return original(self, iid, out);
                    if (face == 1) return reinterpret_cast<nTSSG::cTSSGSystemMessageFace *>(self)->QueryInterface(iid, out);
                    if (face == 2) return reinterpret_cast<nTSSG::cTSSGSystemServiceSub *>(self)->QueryInterface(iid, out);
                    return Ours()->QueryInterface(iid, out);
                });
    }
}

// 0x2543e0 Release (and 0x254475 at +8, 0x25447d at +0xc): counts of 1 (the event manager cached, got through the
// service, or none) and above, with the top bit or not.
DIFF_TEST(sg_tssg_system_release, 0x2543e0)
{
    Fixture fixture;
    const uint32_t rvas[] = {0x2543e0, 0x254475, 0x25447d};
    const uint32_t offsets[] = {0, 8, 0xc};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x2543e0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        reinterpret_cast<cTSSGSystem *>(start)->mRefCount = DrawCount(gen);
        const unsigned face = seed % 3;
        const unsigned manager = gen() % 3; // cached, through the service, none
        auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(rvas[face]);
        auto prepare = [&] {
            gServiceGives = manager == 1;
            if (manager == 0) *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
            F(fEventHolder).Set(3, 0, &F(fEventManager));
            F(fEventManager).Set(5, "p", 0u);
        };
        Compare(ctx, "release", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            void *self = gObject + offsets[face];
            if (useOriginal) return original(self);
            if (face == 1) return reinterpret_cast<nTSSG::cTSSGSystemMessageFace *>(self)->Release();
            if (face == 2) return reinterpret_cast<nTSSG::cTSSGSystemServiceSub *>(self)->Release();
            return Ours()->Release();
        });
    }
}

// 0x248790 GetEventManager: cached, or got through the service (the holder there or not).
DIFF_TEST(sg_tssg_system_get_event_manager, 0x248790)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__cdecl *)()>(0x248790);
    for (uint32_t seed = 0; seed < 30; ++seed) {
        uint8_t start[kObjectSize] = {};
        auto prepare = [&] {
            gServiceGives = seed % 3 != 0;
            if (seed % 3 == 2) *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
            F(fEventHolder).Set(3, 0, seed % 5 == 0 ? nullptr : &F(fEventManager));
        };
        Compare(ctx, "get_event_manager", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            return Norm(P(useOriginal ? original() : cTSSGSystem::GetEventManager()));
        });
    }
}

// 0x24abb0 BehaviorOnEvent: the active event or others; the manager's answer 0, 1 or another byte; the top bit of the
// count set or not.
DIFF_TEST(sg_tssg_system_behavior_on_event, 0x24abb0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(0x24abb0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 15485863u + 0x24abb0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        reinterpret_cast<cTSSGSystem *>(start)->mRefCount = DrawCount(gen);
        const uint32_t event = gen() % 4 ? 0x5b51e806u : gen() % 2 ? 0x5b51e807u : gen();
        const uint32_t answers[] = {0, 1, 0x101, 0x100, 2};
        const uint32_t answer = answers[gen() % 5];
        const uint32_t a = gen(), d = gen();
        auto prepare = [&] {
            *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
            F(fEventManager).Set(6, "p", answer);
        };
        Compare(ctx, "behavior_on_event", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, event, a, gData, d);
            else Ours()->BehaviorOnEvent(event, a, gData, d);
            return 0;
        });
    }
}

// 0x24abf0 OnEvent: the base's handler (ours, stood in on both sides); the detail and shadow events with every kind, the
// flags, the shadow manager present or not.
DIFF_TEST(sg_tssg_system_on_event, 0x24abf0)
{
    Fixture fixture;
    shape::StandIn behavior(0x24abb0, &cTSSGSystem::BehaviorOnEvent, &StandBehavior);
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(0x24abf0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x24abf0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        sys->mShadowManager.mObject = gen() % 4 ? &F(fShadow) : nullptr;
        if (gen() % 2) start[0x16a] = 0;
        if (gen() % 2) start[0x16b] = 0;
        const uint32_t events[] = {0x2b5745f8, 0xca7ccbbc, 0x5b51e806, 0x2b5745f9};
        const uint32_t event = events[gen() % 4];
        const uint32_t kind = gen() % 6;
        const uint32_t a = gen(), d = gen();
        auto prepare = [&] {
            reinterpret_cast<uint32_t *>(gData)[1] = kind;
            *GameAddress<void *>(kPersistCell) = &F(fPersist);
            *GameAddress<void *>(kDetailCell) = &F(fDetail);
            F(fPersist).Set(39, 0, 0u);
            F(fShadow).Set(7, 0, 0u);
            F(fDetail).Set(17, "d", 0u);
        };
        Compare(ctx, "on_event", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, event, a, gData, d);
            else Ours()->OnEvent(event, a, gData, d);
            return 0;
        });
    }
}

// 0x24a320 RefreshDetailFloat: the two flags set or not.
DIFF_TEST(sg_tssg_system_refresh_detail_float, 0x24a320)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24a320);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed * 31337u + 0x24a320u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        if (gen() % 2) start[0x16a] = 0;
        if (gen() % 2) start[0x16b] = 0;
        auto prepare = [&] {
            *GameAddress<void *>(kDetailCell) = &F(fDetail);
            F(fDetail).Set(17, "d", 0u);
        };
        Compare(ctx, "refresh_detail_float", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else Ours()->RefreshDetailFloat();
            return 0;
        });
    }
}

// 0x248770 / 0x249110: the event manager's slots 3 / 4 with this and the three arguments.
// ---- the listener range (slots 5 and 6) ----
// The event manager's slot 10 fills the range (iterator fApp, second fAppObject, end fSlot23; each may be missing but
// the iterator); the iterator walks gListenerCount of the listeners fBaseA, fBaseB, fBaseOld. Its position is its
// value[0], the slot-5 reads so far its value[1].
unsigned gListenerCount;
uint32_t gRangeMissing; // bit 0: no second reference, bit 1: no end
constexpr unsigned kListeners[] = {fBaseA, fBaseB, fBaseOld};
uint32_t RangeFill(Obj *, uint32_t *args)
{
    auto *range = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    range[0] = P(&F(fApp));
    range[1] = gRangeMissing & 1 ? 0 : P(&F(fAppObject));
    range[2] = gRangeMissing & 2 ? 0 : P(&F(fSlot23));
    return args[0];
}
uint32_t RangeAtEnd(Obj *self, uint32_t *) { return self->value[0] >= gListenerCount; }
uint32_t RangeCurrent(Obj *self, uint32_t *args)
{
    auto *pair = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    const uint32_t at = self->value[0];
    pair[0] = P(&F(kListeners[at % 3]));
    pair[1] = 0x1000 + at * 16 + self->value[1]++;
    return 0;
}
uint32_t RangeNext(Obj *self, uint32_t *)
{
    ++self->value[0];
    return 0;
}
void WireListeners(std::mt19937 &gen, uint32_t *answers)
{
    *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
    F(fEventManager).Set(10, "ppd", 0u, &RangeFill);
    F(fApp).Set(4, "p", 0u, &RangeAtEnd);
    F(fApp).Set(5, "p", 0u, &RangeCurrent);
    F(fApp).Set(6, 0, 0u, &RangeNext);
    for (unsigned i = 0; i < 3; ++i) {
        F(kListeners[i]).Set(3, "dddp");
        F(kListeners[i]).Set(4, "ddd", answers[i]);
    }
    (void)gen;
}

// 0x254c70 NotifyListeners: inactive (no call) or active, 0..3 listeners, the range's references present or not.
DIFF_TEST(sg_tssg_system_notify_listeners, 0x254c70)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x254c70);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x254c70u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        reinterpret_cast<cTSSGSystem *>(start)->mRefCount = DrawCount(gen);
        const uint32_t kind = gen(), a = gen(), b = gen();
        const unsigned count = gen() % 4;
        const uint32_t missing = gen() % 4;
        uint32_t answers[3] = {};
        Compare(ctx, "notify_listeners", seed, start,
                [&] { gListenerCount = count, gRangeMissing = missing, WireListeners(gen, answers); },
                [&](bool useOriginal) -> uint32_t {
                    if (useOriginal) original(gObject, kind, a, b);
                    else Ours()->NotifyListeners(kind, a, b);
                    return 0;
                });
    }
}

// 0x24a350 AskListeners: inactive, no listeners, and 1..3 listeners answering 0, 1 or another byte (high bits set too).
DIFF_TEST(sg_tssg_system_ask_listeners, 0x24a350)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x24a350);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x24a350u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        reinterpret_cast<cTSSGSystem *>(start)->mRefCount = DrawCount(gen);
        const uint32_t kind = gen(), a = gen(), b = gen();
        const unsigned count = gen() % 4;
        const uint32_t missing = gen() % 4;
        const uint32_t choices[] = {0, 1, 1, 1, 0x100, 0x101, 2};
        uint32_t answers[3];
        for (uint32_t &answer : answers) answer = choices[gen() % 7];
        Compare(ctx, "ask_listeners", seed, start,
                [&] { gListenerCount = count, gRangeMissing = missing, WireListeners(gen, answers); },
                [&](bool useOriginal) -> uint32_t {
                    if (useOriginal) return original(gObject, kind, a, b);
                    return Ours()->AskListeners(kind, a, b);
                });
    }
}

DIFF_TEST(sg_tssg_system_event_manager_slot3, 0x248770)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x248770);
    for (uint32_t seed = 0; seed < 20; ++seed) {
        std::mt19937 gen(seed + 0x248770u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const uint32_t a = gen(), b = gen(), c = gen();
        auto prepare = [&] {
            *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
            F(fEventManager).Set(3, "pddd", 0u);
            F(fEventManager).Set(4, 4, 0u);
        };
        Compare(ctx, "event_manager_slot3", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, a, b, c);
            else Ours()->EventManagerSlot3(a, b, c);
            return 0;
        });
    }
}

DIFF_TEST(sg_tssg_system_event_manager_slot4, 0x249110)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x249110);
    for (uint32_t seed = 0; seed < 20; ++seed) {
        std::mt19937 gen(seed + 0x249110u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const uint32_t a = gen(), b = gen(), c = gen();
        auto prepare = [&] {
            *GameAddress<void *>(kEventManagerCell) = &F(fEventManager);
            F(fEventManager).Set(3, 4, 0u);
            F(fEventManager).Set(4, "pddd", 0u);
        };
        Compare(ctx, "event_manager_slot4", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, a, b, c);
            else Ours()->EventManagerSlot4(a, b, c);
            return 0;
        });
    }
}

// 0x254f80 SetRefC0: the new object null, the same, or another; the old one null or not.
DIFF_TEST(sg_tssg_system_set_ref_c0, 0x254f80)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x254f80);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 7u + 0x254f80u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const unsigned oldPick = gen() % 3, newPick = gen() % 4;
        reinterpret_cast<cTSSGSystem *>(start)->mRefC0.mObject = oldPick ? &gBlocks[oldPick - 1] : nullptr;
        void *object = newPick == 3 ? reinterpret_cast<cTSSGSystem *>(start)->mRefC0.mObject
                       : newPick ? static_cast<void *>(&gBlocks[newPick - 1]) : nullptr;
        auto prepare = [&] {
            gBlocks[0].Point(&F(fBaseA), &F(fBaseOld));
            gBlocks[1].Point(&F(fBaseB), &F(fBaseOld));
        };
        Compare(ctx, "set_ref_c0", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, object);
            else Ours()->SetRefC0(object);
            return 0;
        });
    }
}

// 0x247b00 AdvanceCounters: the counters frozen (0x80000100), zero or other; deltas of either sign.
DIFF_TEST(sg_tssg_system_advance_counters, 0x247b00)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t)>(0x247b00);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 11u + 0x247b00u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        const int32_t picks[] = {int32_t(0x80000100u), 0, int32_t(gen()), 5};
        sys->mCounter194 = picks[gen() % 4];
        sys->mCounter198 = picks[gen() % 4];
        const int32_t delta = gen() % 2 ? int32_t(gen() % 100) - 50 : int32_t(gen());
        Compare(ctx, "advance_counters", seed, start, [] {}, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, delta);
            else Ours()->AdvanceCounters(delta);
            return 0;
        });
    }
}

// 0x254d60 SetValue38: the value null or not, slot 52's object null or a block whose entry-2 base answers slot 11.
DIFF_TEST(sg_tssg_system_set_value_38, 0x254d60)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x254d60);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 13u + 0x254d60u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        void *value = gen() % 3 ? static_cast<void *>(&F(fBaseB)) : nullptr;
        const bool object = gen() % 4 != 0;
        const uint32_t answer = gen();
        auto prepare = [&] {
            gBlocks[0].Point(&F(fBaseA), &F(fSlot52Base));
            gSelfRet[52] = object ? P(&gBlocks[0]) : 0;
            F(fSlot52Base).Set(11, 0, answer);
        };
        Compare(ctx, "set_value_38", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, value);
            else Ours()->SetValue38(value);
            return 0;
        });
    }
}

// 0x252410 SetParserValue: the parser target null or not, its value the same or not; ReadLotTunables (ours) stood in on
// both sides.
DIFF_TEST(sg_tssg_system_set_parser_value, 0x252410)
{
    Fixture fixture;
    shape::StandIn tunables(0x248720, &cTSSGSystem::ReadLotTunables, &StandTunables);
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x252410);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 17u + 0x252410u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        sys->mParserTarget = gen() % 4 ? &F(fParserTarget) : nullptr;
        sys->mCommandParser.mObject = &F(fParser);
        const uint32_t value = gen() % 5;
        const uint32_t current = gen() % 5;
        auto prepare = [&] {
            F(fParserTarget).Set(6, 0, current);
            F(fParserTarget).Set(5, "d", 0u);
            F(fParser).Set(9, 0, &F(fParserProps));
            gSelfRet[49] = P(&F(fSlot49));
            gSelfRet[33] = P(&F(fSlot33));
            F(fSlot49).Set(25, 0, &F(fSlot49Object));
            F(fSlot33).Set(17, 0, &F(fSlot33Object));
            F(fSlot49Object).Set(9, "p", 0u);
        };
        Compare(ctx, "set_parser_value", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, value);
            else Ours()->SetParserValue(value);
            return 0;
        });
    }
}

// 0x24b210 IsSlot56: mRefB4 / mRef124 null or not, mRef124's slot 33 object null or not, its slot 60 answer.
DIFF_TEST(sg_tssg_system_is_slot56, 0x24b210)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0x24b210);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 19u + 0x24b210u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        sys->mRefB4.mObject = gen() % 4 ? &F(fRefB4) : nullptr;
        sys->mRef124.mObject = gen() % 4 ? &F(fRef124) : nullptr;
        const bool object = gen() % 4 != 0;
        const uint32_t answers[] = {0, 1, 0x100, 2};
        const uint32_t answer = answers[gen() % 4];
        auto prepare = [&] {
            F(fRef124).Set(33, 0, object ? &F(fRef124Object) : nullptr);
            F(fRef124Object).Set(60, 0, answer);
        };
        Compare(ctx, "is_slot56", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            return useOriginal ? original(gObject) : Ours()->IsSlot56();
        });
    }
}

// 0x247f80 / 0x24a930: the app's slot 22 object's slot 20 / 25 result.
DIFF_TEST(sg_tssg_system_app_object_slot20, 0x247f80)
{
    Fixture fixture;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x247f80);
    for (uint32_t seed = 0; seed < 20; ++seed) {
        uint8_t start[kObjectSize] = {};
        auto prepare = [&] {
            *GameAddress<void *>(kAppCell) = &F(fApp);
            F(fApp).Set(22, 0, &F(fAppObject));
            F(fAppObject).Set(20, 0, seed * 77u);
            F(fAppObject).Set(25, 0, seed * 91u);
        };
        Compare(ctx, "app_object_slot20", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            return useOriginal ? original(gObject) : Ours()->AppObjectSlot20();
        });
    }
}

DIFF_TEST(sg_tssg_system_app_object_slot25, 0x24a930)
{
    Fixture fixture;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x24a930);
    for (uint32_t seed = 0; seed < 20; ++seed) {
        uint8_t start[kObjectSize] = {};
        auto prepare = [&] {
            *GameAddress<void *>(kAppCell) = &F(fApp);
            F(fApp).Set(22, 0, &F(fAppObject));
            F(fAppObject).Set(20, 0, seed * 77u);
            F(fAppObject).Set(25, 0, seed * 91u);
        };
        Compare(ctx, "app_object_slot25", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            return useOriginal ? original(gObject) : Ours()->AppObjectSlot25();
        });
    }
}

// 0x24b1e0 BothOptions18E, 0x247ad0 GetLotTunable (the float's bits), 0x24ad30 GetColor (the 16 bytes and the returned
// pointer), 0x24bed0 GetLotBounds.
DIFF_TEST(sg_tssg_system_both_options_18e, 0x24b1e0)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0x24b1e0);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed + 0x24b1e0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        if (gen() % 2) start[0x18e] = 0;
        if (gen() % 2) start[0x18f] = 0;
        Compare(ctx, "both_options_18e", seed, start, [] {}, [&](bool useOriginal) -> uint32_t {
            return useOriginal ? original(gObject) : Ours()->BothOptions18E();
        });
    }
}

DIFF_TEST(sg_tssg_system_get_lot_tunable, 0x247ad0)
{
    Fixture fixture;
    auto original = ctx.Original<float(__thiscall *)(void *, int32_t)>(0x247ad0);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed + 0x247ad0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const int32_t which = seed < 8 ? int32_t(seed) - 1 : int32_t(gen());
        Compare(ctx, "get_lot_tunable", seed, start, [] {}, [&](bool useOriginal) -> uint32_t {
            const float value = useOriginal ? original(gObject, which) : Ours()->GetLotTunable(which);
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            return bits;
        });
    }
}

DIFF_TEST(sg_tssg_system_get_color, 0x24ad30)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x24ad30);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed + 0x24ad30u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const uint32_t index = seed < 12 ? seed : seed < 14 ? 0xffffffffu - seed : gen() % 10;
        Compare(ctx, "get_color", seed, start, [] { gDataKinds = kAllDataKinds; }, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return Norm(P(original(gObject, gData, index)));
            const cTSSGSystem::Color4 color = Ours()->GetColor(index);
            std::memcpy(gData, &color, sizeof(color));
            return Norm(P(gData));
        });
    }
}

DIFF_TEST(sg_tssg_system_get_lot_bounds, 0x24bed0)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x24bed0);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed + 0x24bed0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        if (gen() % 3 == 0) start[0x35c] = 0;
        const uint32_t world = gen() % 3 ? 0x0bea28c2u : 0x0bea28c3u;
        auto prepare = [&] {
            *GameAddress<void *>(kSlot23Cache) = &F(fSlot23);
            F(fSlot23).Set(5, 0, world);
            gDataKinds = kAllDataKinds;
        };
        Compare(ctx, "get_lot_bounds", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            return useOriginal ? original(gObject, gData) : Ours()->GetLotBounds(gData);
        });
    }
}

// 0x253dd0 FillTarget: 0x274a50 stood in over the image.
DIFF_TEST(sg_tssg_system_fill_target, 0x253dd0)
{
    Fixture fixture;
    fixture.Image(0x274a50, &StandFill);
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x253dd0);
    for (uint32_t seed = 0; seed < 10; ++seed) {
        uint8_t start[kObjectSize] = {};
        void *target = seed % 2 ? static_cast<void *>(&F(fBaseA)) : gData;
        Compare(ctx, "fill_target", seed, start, [] {}, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, target);
            else Ours()->FillTarget(target);
            return 0;
        });
    }
}

// 0x247aa0 DeletingDestructor: the destructor stood in on both sides (the image's 0x246f00 and ours, Destruct); heap
// objects, flags with bit 0 or not; which block each side freed is compared.
DIFF_TEST(sg_tssg_system_deleting_destructor, 0x247aa0)
{
    Fixture fixture;
    shape::StandIn destruct(0x246f00, &cTSSGSystem::Destruct, &StandDestructor);
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x247aa0);
    std::mt19937 gen(0x247aa0u);
    for (uint32_t i = 0; i < 40; ++i) {
        const uint32_t flags = gen() % 4 | (i % 3 == 0 ? gen() & 0xfffffff0u : 0u);
        void *blocks[2];
        bool freed[2];
        void *returned[2];
        Blob logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            blocks[side] = ::operator new(kObjectSize);
            std::memset(blocks[side], 0, kObjectSize);
            difftest::FreeRecorder rec;
            returned[side] = side == 0 ? original(blocks[side], flags)
                                       : static_cast<cTSSGSystem *>(blocks[side])->DeletingDestructor(flags);
            rec.Stop();
            freed[side] = rec.Freed(blocks[side]);
            logs[side] = gEv;
            if (!freed[side]) ::operator delete(blocks[side]);
        }
        ++ctx.cases;
        if (returned[0] != blocks[0] || returned[1] != blocks[1]) ctx.Fail("deleting destructor flags %x: wrong return", flags);
        if (freed[0] != freed[1]) ctx.Fail("deleting destructor flags %x: freed original %d, ours %d", flags, freed[0], freed[1]);
        if (logs[0] != logs[1]) ctx.Fail("deleting destructor flags %x: the destructor calls differ", flags);
    }
}
