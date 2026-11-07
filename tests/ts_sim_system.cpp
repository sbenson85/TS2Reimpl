// Differential test for cTSSimSystem (lane IV, src/ts/cTSSimSystem.h): the whole class in the standalone shape.
//
// SimSystem_Run: a system built by the original constructor (0x416810, game vtables 0xdc2f30 / 0xdc2f68 / 0xdc2f84 /
// 0xdc30c0) against one built by ours through the constructor hook's adapter, each driven through its own vtables by the
// same random sequence of operations, then destroyed by the last Release (the deleting destructor 0x417090 behind the +4
// thunk 0x416fef, the destructor 0x416ce0). Operations: Init (0x419100), Shutdown (0x41a950), OnTick (0x419fb0), OnIdle
// (0x419f90), DoMessage (0x417280) with every message kind, QueryInterface (0x41a610) / AddRef / Release on every face, and
// every cITSSimSystem slot: the tuning floats (0x418ab0, 0x41a710), the controller (0x41a8f0), the flag accessors and the
// option setters (0x41a7e0, 0x41a680, 0x41a7b0, 0x41a6b0, 0x41a840, 0x418a50, 0x418b40, 0x418b90, 0x419be0, 0x41b360,
// 0x418c10, 0x41a8a0, 0x418a70, 0x41a6e0, 0x418a80, 0x41a6f0, 0x41a810, 0x418c20, 0x41a8e0, 0x41a670, 0x418c30, 0x418bb0,
// 0x41a830, 0x418b50, 0x417120 and the folded ones), the strings (0x418bf0, 0x41a890, 0x41a860, 0x41a850), the two tables
// (0x4190c0, 0x4190e0), the string-table lookups (0x41a260, 0x41a240, 0x41a220, 0x41a390, 0x41ac80, 0x41a2a0, 0x41abf0,
// through 0x41b1d0 and 0x41ad10), the behaviour forwards (0x4170c0, 0x4170e0, 0x417100, 0x417110, 0x4170d0, 0x4170f0),
// PlaceOnLot (0x419bf0), GetObjectResource (0x418ea0), GetCachedResource (0x418c50) and QueueResourcesOfType (0x41a460).
//
// Every object the system reaches is a recording fake (tests/ts_services_fixture.h, plus a few here); the callees that are
// other classes' code are stood in for on both sides: the message-id walks (0x1c0458 / 0x1c081e, verified), cRZRandom::Seed
// (0x1b8ecb: the clock), the stopwatch's Start / Elapsed (0x1c982b / 0x1c9256: the clock; Elapsed counts up from a value per
// operation), and on the image (the game's code on both sides): the cheat helper 0xb39640, the constants clients' base
// constructor / destructor (0x4df0a0 / 0x4df1b0), the resource manager and debugger getters (0x20deb5 / 0x20ddda), the world
// getter 0x20e087 and the message server 0x9c5a89. Compared after every operation: its result (the low byte where the
// original returns a bool in al), the event log (every fake call with its arguments normalised, keys and texts by content),
// every field of the system (the never-written bytes too: both blocks start as 0xc5), the cache (list order, buckets as
// list indices, mask, size), both key lists and every fake's count.
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocate
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZRandom.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "ts/cTSSimSystem.h"

#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace simsys_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{4, "d"}, {12, "d"}, {17, "d"}, {23, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSSimSystem;

constexpr uint32_t kTag = 0x7d000000; // the system under test

enum : unsigned {
    kApp = 0, kService = 1, kRegistry = 2, kSettings = 3, kComFw = 4, kRef120 = 5, kBehavior = 6, kObjectSource = 7,
    kRef124 = 8, kSubsystem = 9, kResources = 10, kDebugger = 11, kPersist = 12, kOptions = 13, kObjects = 14,
    kController0 = 15, kController1 = 16, kMessage = 17, kRes0 = 18, kRes1 = 19, kRecord = 20, kInfo = 21, kMaker = 22,
    kContainer = 23,
};
// More fakes (static: their addresses are the same on both sides, logged raw).
enum : unsigned { mGroup = 0, mLots, mWorldSource, mWorld, mDefinition, mKind, mServer, mOutString, kMore };
Obj gMore[kMore];
Obj &M(unsigned i) { return gMore[i]; }

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Call0(void *object, unsigned slot)
{
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>((*static_cast<void ***>(object))[slot])(object);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---- the world for a seed ----
struct World {
    bool debugger;
    bool registry;
    bool app;
    bool options;
    bool scripts;
    bool slot23Cached;
    bool frameworkCached;
    bool persistCached;
    uint32_t slot23Answer;
};
World gWorld;

// The keys: a pool with colliding hashes (equal sums) and duplicates.
cGZPersistResourceKey gKeys[24];
void MakeKeys(std::mt19937 &rng)
{
    for (unsigned i = 0; i < 24; ++i) {
        gKeys[i] = {0x1000u + rng() % 4, 0x7000u + rng() % 3, 0x10u + i, rng() % 3};
        if (i >= 20) gKeys[i] = {gKeys[i - 20].group, gKeys[i - 20].type, gKeys[i - 20].instance, 9}; // the same sum
    }
}
void LogKey(const void *key)
{
    const auto *k = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
}

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}

// cRZRandom::Seed: a clock seed (UINT32_MAX) becomes kClockSeed; not logged (our constructor's call may be inlined into
// cRZRandom::Construct, so the seed control below gives ours the same value without a call).
constexpr uint32_t kClockSeed = 0x5eed1234;
void __fastcall StandSeed(cRZRandom *self, void *, uint32_t seed)
{
    if (seed == UINT32_MAX) seed = kClockSeed;
    self->mIntegerSeed = seed;
    self->mRealSeed = int32_t(seed);
}
uint32_t ClockSeed(int, uint32_t) { return kClockSeed; }

void __fastcall StandStart(cRZStopwatch *self, void *)
{
    Ev(0x1c982b);
    self->mStart = 1;
}
uint32_t gElapsed;
uint32_t __fastcall StandElapsed(const cRZStopwatch *, void *)
{
    Ev(0x1c9256);
    return gElapsed++;
}

// A cheat as handed over: its vtable's RVA and its words (the fixed-money ones: amount and the name's address), then freed.
void TakeCheat(uint32_t cheat)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(cheat));
    const uint32_t vt = words[0] - P(GameAddress<void>(0));
    Ev(vt);
    Ev(words[1]);
    Ev(words[2]);
    if (vt == 0xdc31ac) {
        Ev(words[3]);
        Ev(words[4] - P(GameAddress<void>(0)));
    }
    ::operator delete(reinterpret_cast<void *>(uintptr_t(cheat)));
}
void __cdecl StandCheatIfEnabled(uint32_t cheat)
{
    Ev(0xb39640);
    TakeCheat(cheat);
}
uint32_t __fastcall RegistryAdd(Obj *self, void *, uint32_t cheat)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(4);
    TakeCheat(cheat);
    return 0xabcd0001u;
}

// The settings registry: slot 4 (target, id, name, value), slot 5 (target, id, name); the name a std::string over the
// allocator service, the value a cIGZAny (the game's AnyBoolean on one side, ours on the other: its type and byte).
void LogText(const std::string *text)
{
    Ev(uint32_t(text->size()));
    for (char c : *text) Ev(uint8_t(c));
}
uint32_t __fastcall SettingsAdd(Obj *self, void *, uint32_t target, uint32_t id, const std::string *name, void *value)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(4);
    Ev(svcfx::Norm(target));
    Ev(id);
    LogText(name);
    Ev(Call0(value, 4));
    Ev(static_cast<const uint8_t *>(value)[4]);
    return 0x77;
}
uint32_t __fastcall SettingsRemove(Obj *self, void *, uint32_t target, uint32_t id, const std::string *name)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(5);
    Ev(svcfx::Norm(target));
    Ev(id);
    LogText(name);
    return 0x78;
}

// The scripts' slot 54 (name, value): the name a cRZString.
uint32_t __fastcall ScriptEvent(Obj *self, void *, const cRZString *name, uint32_t value)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(54);
    LogText(&name->mString);
    Ev(value);
    return 0;
}

// The constants clients' base constructor (0x4df0a0, thiscall, the table id) and destructor (0x4df1b0).
void *__fastcall StandConstantsConstruct(uint32_t *self, void *, uint32_t id)
{
    Ev(0x4df0a0);
    Ev(id);
    self[1] = 0x1000 + id;
    self[2] = 0x2000;
    return self;
}
void __fastcall StandConstantsDestruct(uint32_t *self, void *)
{
    Ev(0x4df1b0);
    Ev(self[1]);
}

void *__cdecl StandResources() { return &F(kResources); }
void *__cdecl StandDebugger() { return gWorld.debugger ? &F(kDebugger) : nullptr; }
void *__cdecl StandWorldSource() { return &M(mWorldSource); }
void *__cdecl StandServer() { return &M(mServer); }

// ---- fake handlers ----

// The framework service's slot 5 GetSystemService(class, iid, out).
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x2a4915ea) return gWorld.registry ? HandOut(a[2], F(kRegistry)) : 0;
    if (a[0] == 0x0ac1887e) return HandOut(a[2], F(kSettings));
    if (a[0] == 0x056b906e) return HandOut(a[2], F(kPersist));
    return 0;
}
// The COM framework's slot 3 GetClassObject(class, iid, out).
uint32_t GetClassObject(Obj *self, uint32_t *a)
{
    unsigned index;
    switch (a[0]) {
    case 0x1b5a2044: index = kRef120; break;
    case 0x1b269450: index = kBehavior; break;
    case 0x9b7ef292: index = kObjectSource; break;
    case 0x5b561442: index = kRef124; break;
    case 0x2f7f3357: index = kSubsystem; break;
    case 0x1cb36828: case 0x5452434e: index = kMaker; break;
    default: return 0;
    }
    // value[0]: a bit per optional class saying it is missing.
    if (index != kSubsystem && index != kMaker && (self->value[0] >> (index & 7) & 1)) return 0;
    return HandOut(a[2], F(index));
}
// Resource manager slot 3 GetResource(key, iid, out, 0, 0).
uint32_t GetResource(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    const uint32_t roll = Draw(4);
    if (roll == 0) return 0x5500u; // not there
    HandOut(a[2], a[1] == 0x089b7dc8 ? F(kRes0) : F(roll == 1 ? kRes0 : kRes1));
    return 0x5501u;
}
uint32_t KeyBool(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    return 0x4400u | Draw(2);
}
// Resource manager slot 18 (keys, filter): the filter by content, then 0-5 keys of the pool in an allocator-service block.
// The filter is a heap object on both sides (logged by content, not address): a shim instead of the generic log.
void *gHeldFilter;
uint32_t __fastcall KeysOfType(Obj *self, void *, uint32_t keysOut, uint32_t filterAt)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(18);
    Ev(svcfx::Norm(keysOut));
    const uint32_t a[2] = {keysOut, filterAt};
    const auto *filter = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    Ev(filter[0] - P(GameAddress<void>(0)));
    Ev(filter[1]);
    Ev(filter[2]);
    // Held (its own AddRef, the image's code on both sides) so its count after the call shows the caller's Release.
    gHeldFilter = reinterpret_cast<void *>(uintptr_t(filterAt));
    Call0(gHeldFilter, 1);
    auto *keys = reinterpret_cast<cGZPersistResourceKey **>(uintptr_t(a[0]));
    const uint32_t n = Draw(6);
    if (!n && Draw(2)) return 0;
    auto *block = static_cast<cGZPersistResourceKey *>(GZServiceAllocate((n + 1) * 16));
    for (uint32_t i = 0; i < n; ++i) block[i] = gKeys[Draw(24)];
    keys[0] = block;
    keys[1] = block + n;
    keys[2] = block + n + 1;
    return 0;
}
// Resource manager slot 34 (key, out record).
uint32_t GetRecord(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    // A record whenever it answers true (the original uses it unchecked then: 0x418d10).
    if (Draw(3) == 0) return 0;
    return HandOut(a[1], F(kRecord));
}
// QueryInterface of the resource and record fakes: self handed out on a roll; the record gives the info fake.
uint32_t QueryResource(Obj *self, uint32_t *a)
{
    if (Draw(5) == 0) return 0x3300u;
    HandOut(a[1], self == &F(kRecord) ? F(kInfo) : *self);
    return 0x3301u;
}
uint32_t KeyLog(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    return 0;
}
// Persist slot 6 (key, 0, 60000, 1, 0), 8 / 9 (key), 7 (key, 1, out, loaded).
uint32_t PersistLoad(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    if (Draw(4) == 0) return 0x2200u;
    HandOut(a[2], F(Draw(2) ? kRes0 : kRes1));
    *reinterpret_cast<uint8_t *>(uintptr_t(a[3])) = uint8_t(Draw(2));
    return 0x2201u;
}
uint32_t PersistState(Obj *self, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(uintptr_t(a[0])));
    return 0x1100u | (Draw(8) < self->value[0] ? 1u : 0u);
}
// The message's QueryInterface (0x65297976 -> itself).
uint32_t QueryMessage(Obj *self, uint32_t *a)
{
    if (a[0] != 0x65297976 || self->value[0]) return 0;
    return HandOut(a[1], *self);
}
// The container's slot 17 (instance): the group or null.
uint32_t ContainerGroup(Obj *self, uint32_t *) { return self->value[0] ? P(&M(mGroup)) : 0; }
// The lots' slot 76 (x, y, centre out): writes the centre.
float gCentre[2];
int32_t gLevel;
uint32_t LotCentre(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[2]));
    std::memcpy(out, gCentre, 8);
    out[2] = uint32_t(gLevel);
    return self->value[0];
}

// The placed object: slots 98 (kind), 171 (position, 0, 1), 186 (request, position out, 0); beyond the fixture's 80 slots.
struct BigObj {
    void **vt;
    int32_t refs;
    uint32_t kind;
    uint32_t place[2]; // the results of the first and second try
    unsigned tries;
};
BigObj gPlaced;
void *gPlacedTable[200];
uint32_t __fastcall PlacedAddRef(BigObj *self, void *) { Ev(0xb1); return uint32_t(++self->refs); }
uint32_t __fastcall PlacedRelease(BigObj *self, void *) { Ev(0xb2); return uint32_t(--self->refs); }
uint32_t __fastcall PlacedKind(BigObj *self, void *)
{
    Ev(0xb98);
    return self->kind;
}
void LogPosition(const uint32_t *p)
{
    for (int i = 0; i < 3; ++i) Ev(p[i]);
}
uint32_t __fastcall PlacedMove(BigObj *, void *, const uint32_t *position, uint32_t a, uint32_t b)
{
    Ev(0xb171);
    LogPosition(position);
    Ev(a);
    Ev(b);
    return 0;
}
uint32_t __fastcall PlacedTry(BigObj *self, void *, const uint8_t *request, uint32_t *position, uint32_t c)
{
    Ev(0xb186);
    // The request's bytes the original writes (+1..+3 and +0x21..+0x23 are stack leftovers there).
    Ev(request[0]);
    for (int off = 4; off < 0x21; ++off) Ev(request[off]);
    LogPosition(position);
    Ev(c);
    const uint32_t result = self->place[self->tries++ & 1];
    position[0] = 0x40400000 + self->tries; // somewhere new
    position[1] = 0xc0000000;
    position[2] = self->tries;
    return result;
}

// ---- building the fakes ----

void ResetMore()
{
    for (unsigned i = 0; i < kMore; ++i) gMore[i].Reset(300 + i);
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    ResetMore();
    MakeKeys(rng);
    gWorld.debugger = rng() % 5 == 0;
    gWorld.registry = rng() % 4 != 0;
    gWorld.app = true;
    gWorld.options = rng() % 4 != 0;
    gWorld.scripts = rng() % 5 != 0;
    gWorld.slot23Cached = rng() % 2;
    gWorld.frameworkCached = rng() % 2;
    gWorld.persistCached = rng() % 2;
    gWorld.slot23Answer = rng() % 4 ? 0xabea28c7u : 0x1234u;

    Obj &app = F(kApp);
    app.Set(3, 0, gWorld.options ? &F(kOptions) : nullptr);
    app.Set(23, 0, &F(kOptions));
    app.Set(26, 0, gWorld.scripts ? &F(kObjects) : nullptr);
    Obj &service = F(kService);
    service.Set(5, "ddp", 0, &GetSystemService);
    for (unsigned s = 9; s <= 12; ++s) service.Set(s, "p", 0x900 + s);
    F(kRegistry).Set(4, 1);
    F(kRegistry).table[4] = reinterpret_cast<void *>(&RegistryAdd);
    F(kSettings).Set(4, 4);
    F(kSettings).table[4] = reinterpret_cast<void *>(&SettingsAdd);
    F(kSettings).Set(5, 3);
    F(kSettings).table[5] = reinterpret_cast<void *>(&SettingsRemove);
    F(kComFw).Set(3, "ddp", 0, &GetClassObject);
    F(kComFw).value[0] = rng() % 3 ? 0 : rng();
    Obj &behavior = F(kBehavior);
    behavior.Set(41, "p", 0x4100);
    behavior.Set(42, 0, 0x4200);
    behavior.Set(44, "d", rng());
    behavior.Set(45, "d", rng());
    behavior.Set(46, "dp", 0x4600);
    behavior.Set(47, "d", rng());
    Obj &sub = F(kSubsystem);
    for (unsigned s = 3; s <= 4; ++s) sub.Set(s, 0, 0x300 + s);
    for (unsigned s = 5; s <= 6; ++s) sub.Set(s, "d", 0x300 + s);
    Obj &res = F(kResources);
    res.Set(3, "pdp--", 0, &GetResource);
    res.Set(8, "p", 0x800);
    res.Set(11, "p", 0, &KeyBool);
    res.Set(18, 2);
    res.table[18] = reinterpret_cast<void *>(&KeysOfType);
    res.Set(34, "pp", 0, &GetRecord);
    Obj &persist = F(kPersist);
    persist.Set(6, "p-dd-", 0, &KeyLog);
    persist.Set(7, "pdpp", 0, &PersistLoad);
    persist.Set(8, "p", 0, &PersistState);
    persist.Set(9, "p", 0, &PersistState);
    persist.value[0] = 1 + rng() % 6;
    Obj &options = F(kOptions);
    options.Set(5, 0, gWorld.slot23Answer);
    options.Set(16, "dd", 0x1600);
    Obj &objects = F(kObjects);
    objects.Set(16, "p---", (rng() & 0xffff0000u) | (rng() % 3 ? 0x8001u : 0x0042u));
    objects.Set(17, "d", 0x1700);
    objects.Set(29, "d", rng() % 5 ? &gPlaced : nullptr);
    objects.Set(54, 2);
    objects.table[54] = reinterpret_cast<void *>(&ScriptEvent);
    for (unsigned c : {kController0, kController1}) {
        F(c).Set(4, 0, 0x400);
        F(c).Set(5, 0, 0x500);
    }
    Obj &message = F(kMessage);
    message.Set(0, "dp", 0, &QueryMessage);
    message.value[0] = rng() % 8 == 0; // the query fails
    message.Set(4, 0, 0u); // the type: set by each DoMessage
    for (unsigned s : {8u, 13u}) message.Set(s, 0, 0x100u | (rng() % 3 != 0));
    message.Set(9, 0, rng());
    message.Set(14, 0, rng());
    static uint8_t any[8];
    any[4] = uint8_t(rng() % 3);
    message.Set(15, 0, any);
    for (unsigned r : {kRes0, kRes1}) {
        F(r).Set(0, "dp", 0, &QueryResource);
        F(r).Set(4, "p", 0, &KeyLog);
    }
    static const char kText[] = "Sim system string table text, long enough to need the heap";
    F(kRes0).Set(6, 0, kText);
    F(kRes0).Set(7, 0, uint32_t(rng() % 2 ? 5 : sizeof(kText) - 1));
    F(kRecord).Set(0, "dp", 0, &QueryResource);
    F(kInfo).Set(6, 0, rng() % 3 ? 1u : 2u);
    Obj &maker = F(kMaker);
    maker.Set(6, "pd", 0x100u | (rng() % 3 != 0));
    maker.Set(9, 0, 0x900);
    maker.Set(10, "pd", rng() % 3 ? 0u : 4u);
    maker.Set(23, 0, rng() % 4 ? &F(kRes0) : nullptr);
    maker.Set(32, 0, rng() % 4 ? &F(kRes1) : nullptr);
    F(kContainer).Set(17, "d", 0, &ContainerGroup);
    F(kContainer).value[0] = rng() % 5 != 0;
    M(mGroup).Set(11, 0, 0x7f00 + rng() % 4);
    M(mLots).Set(76, "ddp", 0, &LotCentre);
    M(mLots).value[0] = rng() % 4 != 0;
    gCentre[0] = float(int32_t(rng() % 200) - 50) / 4.0f;
    gCentre[1] = float(int32_t(rng() % 200) - 50) / 4.0f;
    gLevel = int32_t(rng() % 4) - 1;
    M(mWorldSource).Set(3, 0, &M(mWorld));
    M(mWorld).Set(49, 0, uint32_t(int32_t(rng() % 70) - 3));
    M(mWorld).Set(50, 0, uint32_t(int32_t(rng() % 70) - 3));
    M(mDefinition).Set(4, 0, &M(mKind));
    M(mKind).Set(3, "d", rng() % 4 ? 3u : 7u);
    svcfx::UseAsMessageServer(M(mServer));
    M(mOutString).Set(16, 1, 0x1616);
    M(mOutString).Set(17, "p", 0x1717);

    gPlaced.vt = gPlacedTable;
    gPlaced.refs = 1000;
    gPlaced.kind = rng() % 3 ? 0x13u : 0x5u;
    gPlaced.place[0] = 0x100u | (rng() % 3 != 0);
    gPlaced.place[1] = 0x100u | (rng() % 2);
    gPlaced.tries = 0;
    gPlacedTable[1] = reinterpret_cast<void *>(&PlacedAddRef);
    gPlacedTable[2] = reinterpret_cast<void *>(&PlacedRelease);
    gPlacedTable[98] = reinterpret_cast<void *>(&PlacedKind);
    gPlacedTable[171] = reinterpret_cast<void *>(&PlacedMove);
    gPlacedTable[186] = reinterpret_cast<void *>(&PlacedTry);

    // The cache cells, as the seed has them (the getters fill them during a run: set again for each side).
    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = gWorld.frameworkCached ? &F(kService) : nullptr;
    *GameAddress<void *>(svcfx::kPersistCell) = gWorld.persistCached ? &F(kPersist) : nullptr;
    *GameAddress<void *>(svcfx::kSlot23Cache) = gWorld.slot23Cached ? &F(kOptions) : nullptr;
    *GameAddress<void *>(0x10cc28c) = &M(mLots);
    svcfx::gFrameworkIndex = kComFw;
}

// ---- driving a system ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// DoMessage through the +0x20 face, keeping the registers the caller owns. ORIGINAL-BUG (0x417280: 0x4173e2..0x4173e8,
// 0x41742e..0x417438, 0x417490..0x4174a0): the original pushes three / two zeros to the message's slots 9 and 14, which take
// none (0x149cf0 / 0x149d00 end in a plain ret), and its epilogue (0x41750d) pops the leftovers into edi / ebx / esi before
// `mov esp, ebp`; this harness depends on them, so they are saved around the call (on both sides).
__declspec(naked) uint32_t __cdecl CallDoMessage(void *target, void *message)
{
    __asm {
        push esi
        push edi
        push ebx
        mov ecx, [esp + 16]
        push dword ptr [esp + 20]
        mov eax, [ecx]
        call dword ptr [eax + 12]
        pop ebx
        pop edi
        pop esi
        ret
    }
}

void *Build(difftest::Context &ctx, bool ours)
{
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    if (ours) cTSSimSystemAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x416810)(memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

void DumpString(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0) - P(GameAddress<void>(0)));
    const auto *s = reinterpret_cast<const std::string *>(at + 4);
    b.push_back(uint32_t(s->size()));
    b.push_back(uint32_t(s->capacity()));
    for (char c : *s) b.push_back(uint8_t(c));
    b.push_back(Word(at, 0x1c));
}
void DumpKeyList(Blob &b, const uint8_t *at)
{
    const auto &l = *reinterpret_cast<const SimResourceKeyList *>(at);
    b.push_back(uint32_t(l.size()));
    for (const auto &k : l) b.push_back(k.type), b.push_back(k.group), b.push_back(k.instance), b.push_back(k.resource);
    for (auto it = l.rbegin(); it != l.rend(); ++it) b.push_back(it->instance);
}
void DumpCache(Blob &b, const uint8_t *at)
{
    const auto &c = *reinterpret_cast<const SimResourceCache *>(at);
    b.push_back(Word(at, 0)); // max load factor
    b.push_back(uint32_t(c.size()));
    b.push_back(Word(at, 0x18)); // mask
    b.push_back(Word(at, 0x1c)); // max index
    std::vector<uint32_t> order;
    const uint32_t head = Word(at, 4);
    for (uint32_t n = *reinterpret_cast<const uint32_t *>(uintptr_t(head)); n != head;
         n = *reinterpret_cast<const uint32_t *>(uintptr_t(n))) {
        order.push_back(n);
        const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(n));
        for (int i = 2; i < 6; ++i) b.push_back(w[i]);
        b.push_back(svcfx::Norm(w[6]));
    }
    // The bucket vector as list indices (the head as 0xffffffff).
    const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0xc)));
    const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(Word(at, 0x10)));
    b.push_back(uint32_t(last - first));
    for (const uint32_t *p = first; p != last; ++p) {
        uint32_t index = 0xffffffff;
        for (uint32_t i = 0; i < order.size(); ++i)
            if (order[i] == *p) index = i;
        if (*p != head && index == 0xffffffff) index = 0xeeeeeeee;
        b.push_back(index);
    }
}

// What the test knows of the constants clients: 0 never made (+0x38 / +0x3c are the 0xc5 fill), 1 alive, 2 deleted.
unsigned gConstants;

// The object's words Dump logs through svcfx::Field ('p' pointer, 'd' data; by offset / 4).
constexpr char kObjectKinds[] = "-------- ---ppp";
void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[4][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kSimVTable}, {0x20, Sys::kMessageVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(Word(m, off));
    b.push_back(Word(m, 0x24) ? 1u : 0u);
    for (uint32_t id : *reinterpret_cast<const std::set<uint32_t> *>(m + 0x24)) b.push_back(id);
    for (uint32_t off : {0x2cu, 0x30u, 0x34u}) svcfx::Field(b, Word(m, off), kObjectKinds, off);
    if (gConstants == 0) {
        b.push_back(Word(m, 0x38));
        b.push_back(Word(m, 0x3c));
    } else if (gConstants == 1) {
        const auto *n = reinterpret_cast<const uint8_t *>(uintptr_t(Word(m, 0x38)));
        const auto *a = reinterpret_cast<const uint8_t *>(uintptr_t(Word(m, 0x3c)));
        b.push_back(Word(n, 0) - P(GameAddress<void>(0)));
        // +0xd..+0xf and +0x1c are never written (Init 0x419100 stores the byte at +0xc only): heap leftovers.
        for (uint32_t off = 4; off < 0x1c; off += 4) b.push_back(off == 0xc ? n[0xc] : Word(n, off));
        b.push_back(Word(n, 0x20));
        b.push_back(Word(n, 0x24));
        b.push_back(Word(a, 0) - P(GameAddress<void>(0)));
        b.push_back(Word(a, 4));
        b.push_back(Word(a, 8));
    } else {
        // Deleted by Shutdown (0x41a950), the pointers kept: whether they are still set.
        b.push_back(Word(m, 0x38) != 0);
        b.push_back(Word(m, 0x3c) != 0);
    }
    for (uint32_t off = 0x40; off < 0xb4; off += 4) b.push_back(Word(m, off));
    DumpString(b, m + 0xb4);
    DumpString(b, m + 0xd4);
    for (uint32_t off = 0xf4; off < 0x120; off += 4) b.push_back(Word(m, off));
    for (uint32_t off = 0x120; off < 0x130; off += 4) b.push_back(svcfx::Norm(Word(m, off)));
    DumpCache(b, m + 0x130);
    DumpKeyList(b, m + 0x150);
    DumpKeyList(b, m + 0x158);
    b.push_back(Word(m, 0x160));
    b.push_back(Word(m, 0x164));
    b.push_back(svcfx::Norm(Word(m, 0x168)));
    b.push_back(Word(m, 0x16c));
}

// A string out-parameter after a lookup: the new cRZString by content (then released), a fake, or null.
void TakeString(Blob &b, void *&out)
{
    void *s = out;
    if (!s) {
        b.push_back(0);
    } else if (s == &M(mOutString)) {
        b.push_back(1);
    } else {
        b.push_back(2);
        DumpString(b, static_cast<const uint8_t *>(s));
        Call0(s, 2);
    }
    out = nullptr;
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 24;
    bool inited = false;
    if (rng() % 6) ops.push_back({0, 0, 0}), inited = true;
    for (unsigned i = 0; i < n; ++i) {
        unsigned kind = 1 + rng() % 22;
        // Init / Shutdown alternate (a second Init leaks, a second Shutdown deletes the constants again: the game never
        // does either); what needs Init's pointers runs only after it.
        if (kind == 1) kind = inited ? 1 : 0;
        if (!inited && (kind == 2 || kind == 11 || kind == 13 || kind == 14 || kind == 15)) kind = 6;
        if (kind == 0) inited = true;
        if (kind == 1) inited = false;
        ops.push_back({kind, rng(), rng()});
    }
    return ops;
}

static const uint32_t kMessageTypes[] = {0xb2, 0xabd68fed, 0xebd68ff9, 0xa4, 0xa6, 0xaf, 0xc6, 0xa5, 0xc7, 0xa3, 0x1234};
static const uint32_t kIids[] = {1, Sys::kIid, Sys::kMessageTargetIid, 0xdae3783f, 0xcbc1ce69, 0x12345678};

// The word getters' results by slot (svcfx::Field, word `slot` of the string): 'p' pointer, 'd' data.
constexpr char kGetterKinds[] =
    "---p--p- -------- -------- p--p---- -------- -----d-- -d-d---p -------p "
    "--pp";
void RunOp(Blob &b, void *object, const Op &op, bool ours)
{
    void *const primary = object;
    void *const sim = Face(object, 0x1c);
    void *const target = Face(object, 0x20);
    const uint32_t a = op.arg, c = op.arg2;
    static void *out;
    static cGZPersistResourceKey keyArg;
    switch (op.kind) {
    case 0: { // Init (the generator scrambled first: Init reseeds it)
        auto *random = reinterpret_cast<uint32_t *>(Face(object, 0x108));
        random[0] = 0x1111;
        random[1] = 0x2222;
        b.push_back(Slot<uint32_t>(primary, 8) & 0xff);
        gConstants = 1;
        break;
    }
    case 1: b.push_back(Slot<uint32_t>(primary, 9) & 0xff), gConstants = gConstants ? 2 : 0; break;
    case 2: gElapsed = a % 14; b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff); break;
    case 3: b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff); break;
    case 4: { // DoMessage
        F(kMessage).ret[4] = kMessageTypes[a % 11];
        void *message = a % 13 == 0 ? nullptr : &F(kMessage);
        b.push_back(CallDoMessage(target, message) & 0xff);
        break;
    }
    case 5: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), sim, target};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 6: { // the plain accessors
        static const unsigned kGetters[] = {3, 6, 8, 10, 12, 14, 16, 18, 22, 24, 27, 39, 41, 43, 45, 49, 51, 53, 55, 63, 64,
                                            65, 66, 67, 68, 75, 76};
        static const unsigned kSetters[] = {9, 11, 13, 15, 17, 19, 23, 40, 42, 44, 46, 50, 52, 54, 69, 77};
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t r = gRng();
            switch (r % 8) {
            case 0: case 1: case 2: {
                const unsigned s = kGetters[(r >> 3) % (sizeof(kGetters) / 4)];
                const uint32_t v = Slot<uint32_t>(sim, s);
                // Byte getters return the byte in al (the rest of eax is left over); pointers by range.
                const bool byte = s == 8 || s == 10 || s == 12 || s == 14 || s == 16 || s == 18 || s == 22 || s == 39 ||
                                  s == 41 || s == 43 || s == 53 || s == 68 || s == 76;
                b.push_back(s);
                if (s == 75) b.push_back(v - P(GameAddress<void>(0)));
                else if (s == 64 || s == 65) b.push_back(v == Word(static_cast<uint8_t *>(object), s == 64 ? 0x38 : 0x3c));
                else if (byte) b.push_back(v & 0xff);
                else svcfx::Field(b, v, kGetterKinds, s * 4); // by the getter's slot
                break;
            }
            case 3: case 4: {
                const unsigned s = kSetters[(r >> 3) % (sizeof(kSetters) / 4)];
                const uint32_t v = (r >> 9) % 4 == 0 ? r : (r >> 11) % 3;
                b.push_back(s);
                Slot<uint32_t>(sim, s, v);
                break;
            }
            case 5: b.push_back(20), Slot<uint32_t>(sim, 20); break;
            case 6: b.push_back(21), Slot<uint32_t>(sim, 21); break;
            default: {
                b.push_back(30 + (r >> 3) % 2);
                b.push_back(Slot<uint32_t>(sim, 30 + (r >> 3) % 2, (r >> 4) % 12));
                b.push_back(Slot<uint32_t>(sim, 74, int32_t((r >> 8) % 70) - 5));
                b.push_back(Slot<uint32_t>(sim, 70, 1u, 2u, 3u, 4u, 5u) & 0xff);
                break;
            }
            }
        }
        break;
    }
    case 7: { // tuning
        const uint32_t id = 998 + a % 10;
        float f = -7.0f;
        b.push_back(Slot<uint32_t>(sim, 4, id, &f) & 0xff);
        b.push_back(Bits(f));
        b.push_back(Slot<uint32_t>(sim, 5, id + (c % 3) - 1, float(c % 1000) / 8.0f) & 0xff);
        Slot<uint32_t>(sim, 48, float(c % 77) / 3.0f);
        b.push_back(Bits(Slot<float>(sim, 47)));
        break;
    }
    case 8: { // the controller
        static const unsigned kChoices[] = {kController0, kController1, kController0, 99};
        const unsigned which = kChoices[a % 4];
        Slot<uint32_t>(sim, 7, which == 99 ? nullptr : static_cast<void *>(&F(which)));
        break;
    }
    case 9: { // strings
        static cRZString text;
        static bool built = false;
        if (!built) text.Construct("assigned text that is longer than fifteen"), built = true;
        switch (a % 5) {
        case 0: b.push_back(Slot<uint32_t>(sim, 25, static_cast<void *>(&M(mOutString))) & 0xff); break;
        case 1: b.push_back(svcfx::Norm(Slot<uint32_t>(sim, 26, static_cast<void *>(&text)))); break;
        case 2: b.push_back(svcfx::Norm(Slot<uint32_t>(sim, 29, static_cast<void *>(&text)))); break;
        case 3: Slot<uint32_t>(sim, 28, c % 3 ? (c % 2 ? "short" : "a text over the small buffer size") : nullptr); break;
        default: Slot<uint32_t>(sim, 28, ""); break;
        }
        break;
    }
    case 10: { // the string tables
        static const uint32_t kSkills[] = {0, 1, 2, 5, 7, 0x19, 0x30, 0x63, 0x7c, 0x81, 0x89, 0x92, 0x9e, 0xa6, 0xa7, 0xa8, 0x4a,
                                           0x83, 0x6e, 0x3b, 0x58, 0x8c};
        static const uint32_t kFlags[] = {0, 1, 2, 3, 4, 8, 0x800, 0x1000, 0x2000, 0x4000, 0x8000};
        out = c % 4 == 0 ? static_cast<void *>(&M(mOutString)) : nullptr;
        uint32_t r = 0;
        switch (a % 8) {
        case 0: case 1: r = Slot<uint32_t>(sim, 32, kSkills[(a >> 3) % 22] + (a >> 8) % 2 * ((a >> 9) % 0xa8), &out); break;
        case 2: r = Slot<uint32_t>(sim, 33, &out); break;
        case 3: r = Slot<uint32_t>(sim, 34, &out); break;
        case 4: r = Slot<uint32_t>(sim, 35, kFlags[(a >> 3) % 11], &out); break;
        case 5: r = Slot<uint32_t>(sim, 36, (a >> 3) % 14, &out); break;
        case 6: r = Slot<uint32_t>(sim, 37, kFlags[(a >> 3) % 11], &out); break;
        default: r = Slot<uint32_t>(sim, 38, (a >> 3) % 14, &out); break;
        }
        b.push_back(r & 0xff);
        TakeString(b, out);
        break;
    }
    case 11: { // the behaviour forwards: only with a behaviour object (the originals use +0x30 unchecked, e.g. 0x4170f3)
        if (!Word(static_cast<uint8_t *>(object), 0x30)) break;
        switch (a % 6) {
        case 0: b.push_back(Slot<uint32_t>(sim, 56, c)); break;
        case 1: b.push_back(Slot<uint32_t>(sim, 57, c)); break;
        case 2: Slot<uint32_t>(sim, 58, static_cast<void *>(&M(mOutString))); break;
        case 3: Slot<uint32_t>(sim, 59); break;
        case 4: Slot<uint32_t>(sim, 60, c, static_cast<void *>(&M(mOutString))); break;
        default: b.push_back(Slot<uint32_t>(sim, 61, c)); break;
        }
        break;
    }
    case 12: // PlaceOnLot: only with the app's slot 26 object (the original uses it unchecked: 0x419c25)
        if (gWorld.scripts) Slot<uint32_t>(sim, 62, static_cast<void *>(&M(mDefinition)));
        break;
    case 13: { // GetObjectResource
        static const uint32_t kTypes[] = {0x54505250, 0x5452434e, 0x12345678};
        out = nullptr;
        b.push_back(Slot<uint32_t>(sim, 71, static_cast<void *>(&F(kContainer)), kTypes[a % 3], (a >> 4) % 3 ? 0x8123u : 0x0042u,
                                   0xabcdu, &out) & 0xff);
        b.push_back(svcfx::Norm(P(out)));
        break;
    }
    case 14: { // GetCachedResource
        keyArg = gKeys[a % 24];
        out = nullptr;
        b.push_back(Slot<uint32_t>(sim, 72, &keyArg, c % 3 ? 1u : 0x4321u, &out) & 0xff);
        b.push_back(svcfx::Norm(P(out)));
        break;
    }
    case 15: // QueueResourcesOfType, then the filter's count (the test's reference left) and the test's Release
        Slot<uint32_t>(sim, 73, 0x1000u + a % 4, c);
        if (gHeldFilter) {
            b.push_back(static_cast<const uint32_t *>(gHeldFilter)[1]);
            Call0(gHeldFilter, 2);
            gHeldFilter = nullptr;
        }
        break;
    case 16: case 17: case 18: { // more ticks and messages (the loader's states)
        if (op.kind == 18) {
            F(kMessage).ret[4] = kMessageTypes[a % 3];
            b.push_back(CallDoMessage(target, &F(kMessage)) & 0xff);
        } else {
            b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
            b.push_back(Slot<uint32_t>(sim, 76) & 0xff);
        }
        break;
    }
    default: { // the cIGZSystemService accessors
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        break;
    }
    }
    (void)ours;
}

} // namespace simsys_test

using namespace simsys_test;

DIFF_TEST(SimSystem_Run, 0x416810)
{
    // Also reached (see the file comment): 0x416ce0 (destructor), 0x417090 (deleting destructor), 0x416fef (its +4 thunk),
    // 0x41a610 (QueryInterface), 0x419100 (Init), 0x41a950 (Shutdown), 0x419fb0 (OnTick), 0x419f90 (OnIdle), 0x417280
    // (DoMessage), the QueryInterface adjustor thunks 0x41a64d / 0x41a655 / 0x41a65d, 0x418ab0, 0x41a710, 0x41a8f0, 0x41a7e0, 0x41a680, 0x41a7b0, 0x41a6b0, 0x41a840, 0x418a50, 0x418b40,
    // 0x418b90, 0x419be0, 0x41b360, 0x418c10, 0x41a8a0, 0x418a70, 0x41a6e0, 0x418a80, 0x41a6f0, 0x41a810, 0x418c20, 0x41a8e0,
    // 0x41a670, 0x418c30, 0x418bb0, 0x41a830, 0x418b50, 0x417120, 0x418bf0, 0x41a890, 0x41a860, 0x41a850, 0x4190c0,
    // 0x4190e0, 0x41a260, 0x41a240, 0x41a220, 0x41a390, 0x41ac80, 0x41a2a0, 0x41abf0, 0x41b1d0, 0x41ad10, 0x4170c0,
    // 0x4170e0, 0x417100, 0x417110, 0x4170d0, 0x4170f0, 0x419bf0, 0x418ea0, 0x418c50, 0x41a460; the folded slots 0x418b70,
    // 0x418bc0, 0x418a90, 0x41a700, 0x41a820, 0x3730d0, 0x2bfcb0, 0x418be0, 0x418bd0, 0x418b80, 0x41a8b0, 0x418c40,
    // 0x254690, 0x41a8d0, 0x418a40, 0x418aa0, 0x418b60, 0x4190b0, 0x18a5a0, 0x418a60, 0x148ba0 and the +0x20 face's AddRef /
    // Release thunks 0x1cc335 / 0x1cd7b5.
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {svcfx::kAppCell, 0x10c9378u, 0x10f5194u, svcfx::kPersistCell, svcfx::kSlot23Cache, 0x10cc28cu})
        globals.SetWord(cell, 0);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch resources(0x20deb5, shape::CodeOf(&StandResources));
    shape::CodePatch debugger(0x20ddda, shape::CodeOf(&StandDebugger));
    shape::CodePatch worldSource(0x20e087, shape::CodeOf(&StandWorldSource));
    shape::StandIn server(0x9c5a89, &GetCachedMessageServer, &StandServer);
    shape::CodePatch cheats(0xb39640, shape::CodeOf(&StandCheatIfEnabled));
    shape::CodePatch constantsCtor(0x4df0a0, shape::CodeOf(&StandConstantsConstruct));
    shape::CodePatch constantsDtor(0x4df1b0, shape::CodeOf(&StandConstantsDestruct));
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn seed(0x1b8ecb, &cRZRandom::Seed, &StandSeed);
    shape::StandIn start(0x1c982b, &cRZStopwatch::Start, &StandStart);
    shape::StandIn elapsed(0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed);
    auto *const savedClock = gRZRandomClockSeedOverride;
    gRZRandomClockSeedOverride = &ClockSeed;

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gConstants = 0;
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i], side == 1);
                    Dump(b, object, side == 1);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                for (const Obj &o : gMore) b.push_back(uint32_t(o.refs));
                b.push_back(uint32_t(gPlaced.refs));
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "SimSystem_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                gRZRandomClockSeedOverride = savedClock;
                return;
            }
        }
    }
    gRZRandomClockSeedOverride = savedClock;
}
