// Differential test for cGZSndSys (lane KK, src/gz/cGZSndSys.h): the whole class in the standalone shape.
//
// SndSys_Run: a sound system built by the original constructor (0xb22fd5, game vtables 0xe80f60 / 0xe81040 / 0xe81078 /
// 0xe8108c / 0xe810bc) or factory (0xb22378) against one built by ours (the Construct adapter / Create), each driven through
// its own vtables by the same random operations, then destroyed by the last Release (0xb24435) or the deleting destructor
// (slot 53, 0xb232e1), both reaching the destructor 0xb231cd.
// Operations: every primary slot (QueryInterface 0xb241e1, AddRef 0x2296a0, Release 0xb24435, the face casts 0x6e70ed /
// 0x6f5105, SendEventText 0xb234b4, SendEventObject 0xb23525, SendEvent 0xb2340d, DispatchEvent 0xb235e1, PostEventText
// 0xb240a9, PostEventObject 0xb24107, PostEvent 0xb24015, QueueEvent 0xb241b0, CreateEvent 0xb23846, CreateEmptyEvent
// 0xb2389f, AddEventHandler 0xb2427d, RemoveEventHandler 0xb24ed7, Update 0xb2506e, IsRunning 0x954fb5, SetName 0xb24678,
// GetName 0x158f90, IsEnabled 0x2296b0, SetEnabled 0x229f40, SetNamedValue 0xb247b5, GetNamedValue 0xb23a10,
// GetNamedValueCount 0x249080, GetNamedValueAt 0xb23ab4, GetNameAfter 0xb23af8, GetLevel 0x230133, ApplyLevel 0xb24865,
// AddUpdateTarget 0xb24379, RemoveUpdateTarget 0xb24fc0, ForEachUpdateTarget 0xb237b5, EnableUpdates 0xb23766,
// DisableUpdates 0xb23406, AddRenderer 0xb24307, RemoveRenderer 0xb24f5d, HasRenderer 0xb23e70, GetRendererCount 0x24ad70,
// QuerySound 0xb238ee, HasSound 0xb23eb5, GetLock 0x6e7c71, GetTime 0xb23a08, the objects 0x418aa0 / 0xb23c31 / 0x418b60 /
// 0xb23c05, HoldUntilDispatched 0xb2331b, StopAllSounds 0xb23edc, the flag 0xb24b35 / 0xb23c73, the timer's 0xb23c5d /
// 0xb24b42, SetRunning 0xb2485b), the service face (0xb2425d / 0x333def / 0xb2444f, 0x1674b0, 0x20c6f0, GetServicePriority
// 0xb23bff, 0x144910, 0x300a0, Init 0xb23ca1, Shutdown 0xb24c1a, 0x18a7a0, 0x148260), the message target (0xb24265 /
// 0xb23303 / 0xb24457, DoMessage 0xb236f3), the listener (0xb24275 / 0xb23313 / 0xb24467, Init / Shutdown thunks 0xb23e68 /
// 0xb24ecf, 0xb23390, 0xb23bc9, 0xb249d9, 0xb23b7c, 0xb248ba, 0xb23388) and the listener-ex (0xb2426d / 0xb2330b /
// 0xb2445f, 0xb23c7a, 0xb24b51, 0xb2395b, SetAll 0xb2446f, 0xb239f9, 0xb24722, 0xb23bf0, 0xb24aa2, 0xb239ea, 0xb2468f).
// Through them: CallUpdateTargets 0xb250f0 (Update), UpdateOne 0xb250e0 (its callback), FindSound 0xb2376d (QuerySound /
// HasSound's callback) and TimerTick 0xb23923 (the timer's callback, called as the timer would).
//
// Every object the system reaches is a recording fake (tests/ts_services_fixture.h): the framework (GetServiceObject's cell
// 0x10c9378) with the name service, the message server (cell 0x10f519c), the director's framework (class objects), the queue
// (0xc66b0d stood in for), the callback timer (0x9ccf32 stood in for) and its thread, the events (the allocator 0xb2878d and
// the constructors 0xb286d4 / 0xb28731 stood in for) with their data, a queued message, six items (handlers, update targets,
// renderers: their slot 3 logs the busy counts and the locks' counts and may call back into the system) and the listener,
// listener-ex and sound faces the items answer. The stopwatches run for real over a counter the test advances (0x1c95c4
// stood in for; ours through cRZStopwatch::sPerformanceCounterOverride), with the mode scale 1. Run for real: the critical
// sections, the strings (LowerCase, TrimBoth, Right). The game's operator new / delete (0x824632 / 0x824624) are ours.
// Compared after every operation: the result and out words, every field (the never-written bytes on the 0xc5 route), the
// vtable words, the lists and the map by content, the locks' counts, the event log and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZMessageQueue.h"
#include "gz/cGZSndSys.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZStopwatch.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace sndsys_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}, {6, "d"}, {9, "d"}, {11, "d"}, {12, "dd"}, {13, "ddd"}, {14, "ddd"}, {15, "ddd"}, {16, "ddd"}, {17, "ddd"}, {18, "ddd"}, {21, "-d"}, {22, "-d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cGZSndSys;

constexpr uint32_t kTag = 0x7e000000; // the system under test

enum : unsigned {
    kService = 0, kNameSvc, kMsgSrv, kDirector, kObjA, kObjB, kQueue, kTimer, kThread, kEvent, kData, kEmpty, kMsg,
    kItem0, kItems = 6, kListener = kItem0 + kItems, kEx, kSound0, kSound1, kOther
};
static_assert(kOther < svcfx::kFakes, "fakes");

constexpr uint32_t kEventType = 0xdb53383b, kEventIid = 0x5b523ba4, kSoundIid = 0x206077d2, kOtherIid = 0x11111111;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

struct World {
    bool name;          // the name service answers
    bool objects[2];    // the director's class objects answer
    bool timerStarts;   // the timer's slot 5
    uint64_t now;       // the performance counter
    void *tickFn;       // the timer's callback and its argument (slot 9)
    void *tickArg;
};
World gWorld;
uint8_t *gObject; // the system under test
bool gOurs;       // this side is ours

bool __cdecl ForEach(cIGZUnknown *target, uint32_t a, uint32_t b);

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

constexpr uint32_t kBusy[] = {0x9c, 0xa0, 0xb4, 0xd8, 0xdc, 0xf8};
constexpr uint32_t kLocks[] = {0x68, 0xa4, 0xb8, 0xe0, 0xfc};

// The busy counts and each lock's count (its block's +0x18).
void LogState()
{
    if (!gObject) return;
    for (uint32_t off : kBusy) Ev(Word(gObject, off));
    for (uint32_t off : kLocks) {
        const uint32_t block = Word(gObject, off + 4);
        Ev(block ? *reinterpret_cast<const uint32_t *>(uintptr_t(block) + 0x18) : 0xdead0001);
    }
}

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// ---- the fakes ----

uint32_t CountedAddRef(Obj *self, uint32_t *)
{
    LogState();
    return uint32_t(++self->refs);
}
uint32_t CountedRelease(Obj *self, uint32_t *)
{
    LogState();
    return uint32_t(--self->refs);
}
uint32_t Answer(Obj &face, uint32_t *a)
{
    ++face.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &face;
    return 0x101;
}
// An item: a handler, update target or renderer. QueryInterface by its mask (value[0]): bit 0 a sound (value[2] picks
// which), bit 1 the listener, bit 2 the listener-ex, bit 3 the other iid (itself).
uint32_t ItemQuery(Obj *self, uint32_t *a)
{
    LogState();
    const uint32_t mask = self->value[0];
    if (a[0] == kSoundIid && (mask & 1)) return Answer(F(kSound0 + self->value[2] % 2), a);
    if (a[0] == cIGZSnd3DListener::kIid && (mask & 2)) return Answer(F(kListener), a);
    if (a[0] == cIGZSnd3DListenerEx::kIid && (mask & 4)) return Answer(F(kEx), a);
    if (a[0] == kOtherIid && (mask & 8)) return Answer(*self, a);
    return 0x100;
}
// Slot 3 (HandleEvent / Update / SetLevel): logs the state; now and then calls back into the system (adding or removing a
// handler or an update target while a walk may be running).
uint32_t ItemSlot3(Obj *self, uint32_t *)
{
    LogState();
    const uint32_t pick = Draw(12);
    if (gObject && pick < 5) {
        void *const item = &F(kItem0 + Draw(kItems));
        static const uint32_t kPeriods[] = {0x14, 0xfa, 0x15};
        switch (pick) {
        case 0: Ev(Slot<uint32_t>(gObject, 15, item) & 0xff); break;
        case 1: Ev(Slot<uint32_t>(gObject, 16, item) & 0xff); break;
        case 2: Ev(Slot<uint32_t>(gObject, 30, item, kPeriods[Draw(3)]) & 0xff); break;
        case 3: Ev(Slot<uint32_t>(gObject, 31, item) & 0xff); break;
        case 4: Ev(Slot<uint32_t>(gObject, 47, item) & 0xff); break; // held until the dispatch ends
        }
    }
    return 0x300u | (self->value[1] >> Draw(4) & 1);
}
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] != 0x2073215d || a[1] != 0xe0203660 || !gWorld.name) return 0;
    ++F(kNameSvc).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kNameSvc);
    return 1;
}
const char *const kNames[] = {"", "Alpha", "alpha", " beta ", ".gamma", ".", "Gamma", "delta", "  ", ".Alpha", "x"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);
uint32_t NameText(Obj *self, uint32_t *) { return P(kNames[self->value[0] % kNameCount]); }
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    const int which = a[0] == 0x9d4c6b95 && a[1] == 0x5d4c6b90 ? 0 : a[0] == 0xbdf613b4 && a[1] == 0x3df613a1 ? 1 : -1;
    if (which < 0 || !gWorld.objects[which]) return 0;
    ++F(kObjA + which).refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &F(kObjA + which);
    return 1;
}
uint32_t QueuePush(Obj *self, uint32_t *)
{
    ++self->value[0];
    return 0;
}
// Slot 18: the next message (the queued message fake, counted), false when none is left.
uint32_t QueuePop(Obj *self, uint32_t *a)
{
    if (self->value[0] == 0) return 0x1800;
    --self->value[0];
    ++F(kMsg).refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &F(kMsg);
    return 0x1801;
}
// The event: QueryInterface answers its own iid (value[0] bit 0; bit 1 answers true with a null), the other iid (bit 2).
uint32_t EventQuery(Obj *self, uint32_t *a)
{
    LogState();
    const uint32_t mask = self->value[0] >> Draw(3);
    if (a[0] == kEventIid && (mask & 2)) {
        *reinterpret_cast<void **>(uintptr_t(a[1])) = nullptr;
        return 0x101;
    }
    if ((a[0] == kEventIid && (mask & 1)) || (a[0] == kOtherIid && (mask & 4))) return Answer(*self, a);
    return 0x100;
}
// The queued message: its type (value[0]), and its event (value[1]: 1 answers, 2 answers true with a null).
uint32_t MessageType(Obj *self, uint32_t *) { return self->value[0] % 3 ? kEventType : 0x1234; }
uint32_t MessageQuery(Obj *self, uint32_t *a)
{
    if (a[0] != kEventIid) return 0x100;
    const uint32_t how = (self->value[1] >> Draw(2)) % 3;
    if (how == 1) return Answer(F(kEvent), a);
    if (how == 2) {
        *reinterpret_cast<void **>(uintptr_t(a[1])) = nullptr;
        return 0x101;
    }
    return 0x100;
}
uint32_t SoundQuery(Obj *self, uint32_t *a)
{
    if (a[0] == kOtherIid && (self->value[1] & 1)) return Answer(*self, a);
    return 0x100;
}
uint32_t TimerStart(Obj *, uint32_t *) { return gWorld.timerStarts ? 0x501 : 0x500; }
uint32_t TimerThread(Obj *, uint32_t *) { return P(&F(kThread)); }
uint32_t EventDataOf(Obj *, uint32_t *) { return P(&F(kData)); }

// The timer's slot 9 (callback, argument): remembered so the test can tick as the timer would; the callback is logged as
// whether it is the side's own tick.
void __fastcall TimerSetCallback(Obj *, void *, void *fn, void *arg)
{
    LogState();
    gWorld.tickFn = fn;
    gWorld.tickArg = arg;
    Ev(0x9000 | uint32_t(fn == (gOurs ? reinterpret_cast<void *>(&Sys::TimerTick) : GameAddress<void>(0xb23923))));
    Ev(svcfx::Norm(P(arg)));
}

// The stand-ins.
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
alignas(8) uint8_t gEventMemory[0x40];
void *__cdecl StandEventMemory(uint32_t size)
{
    Ev(0xe1000000 | size);
    return gEventMemory;
}
// The event constructor's words (type, d1..d4) as logged, by the operation that sends the event (svcfx::DumpWords): 'p'
// pointer, 'd' data.
unsigned gOpKind; // the operation RunOp is running
constexpr char kTextEventKinds[] = "d";
constexpr char kObjectEventKinds[] = "d";
constexpr char kWordEventKinds[] = "d";
constexpr char kCreateEventKinds[] = "ddddd";
constexpr char kOtherEventKinds[] = "";
void *__fastcall StandEvent(void *memory, void *, uint32_t type, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    Ev(0xe2000000 | uint32_t(memory == gEventMemory));
    const uint32_t words[] = {type, d1, d2, d3, d4};
    switch (gOpKind) {
    case 2: svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kTextEventKinds); break;
    case 3: svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kObjectEventKinds); break;
    case 4: svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kWordEventKinds); break;
    case 7: svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kCreateEventKinds); break;
    default: svcfx::DumpWords(svcfx::gEv, words, sizeof(words), kOtherEventKinds); break;
    }
    return &F(kEvent);
}
void *__fastcall StandEmptyEvent(void *memory, void *)
{
    Ev(0xe3000000 | uint32_t(memory == gEventMemory));
    return reinterpret_cast<uint8_t *>(&F(kEmpty)) - 0x2c;
}
void *__fastcall StandQueue(void *memory, void *)
{
    Ev(0xe4000000);
    ::operator delete(memory);
    return &F(kQueue);
}
void *__fastcall StandTimer(void *memory, void *)
{
    Ev(0xe5000000);
    ::operator delete(memory);
    return &F(kTimer);
}
int32_t __cdecl StandCounter(uint64_t *out)
{
    *out = gWorld.now;
    return 1;
}
void StandCounterOurs(uint64_t *out) { *out = gWorld.now; }

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 5);
    svcfx::ResetAll();
    gWorld = {};
    gWorld.name = rng() % 4 != 0;
    gWorld.objects[0] = rng() % 3 != 0;
    gWorld.objects[1] = rng() % 3 != 0;
    gWorld.timerStarts = rng() % 4 != 0;
    gWorld.now = 1000;
    F(kService).Set(5, "ddp", 0, &GetSystemService);
    F(kNameSvc).Set(7, 0, 0, &NameText);
    F(kNameSvc).value[0] = rng() % kNameCount;
    F(kMsgSrv).Set(5, "pd", 0x55);
    F(kMsgSrv).Set(6, "pd", 0x66);
    F(kDirector).Set(3, "ddp", 0, &GetClassObject);
    F(kQueue).Set(3, "p", 0, &QueuePush);
    F(kQueue).Set(18, "p", 0, &QueuePop);
    F(kQueue).value[0] = rng() % 3;
    F(kTimer).Set(0, "d", 0x7000);
    F(kTimer).Set(1, "d", 0x7001);
    F(kTimer).Set(3, "d", 0x7003);
    F(kTimer).Set(5, "d-", 0, &TimerStart);
    F(kTimer).Set(7, 0, 0x7007);
    F(kTimer).Set(9, 2);
    F(kTimer).table[9] = reinterpret_cast<void *>(&TimerSetCallback);
    F(kTimer).Set(12, 0, 0, &TimerThread);
    F(kThread).Set(5, "d", 0x8005);
    F(kThread).Set(7, 0, 0x8007);
    F(kEvent).Set(0, "dp", 0, &EventQuery);
    F(kEvent).Set(1, 0, 0, &CountedAddRef);
    F(kEvent).Set(2, 0, 0, &CountedRelease);
    F(kEvent).Set(6, 0, 0, &EventDataOf);
    F(kEvent).value[0] = rng() % 64;
    for (unsigned slot : {11u, 16u, 21u, 26u}) F(kData).Set(slot, "d", 0xd000 + slot);
    for (unsigned slot : {30u, 33u}) F(kData).Set(slot, "p", 0xd000 + slot);
    F(kEmpty).Set(0, "dp", 0, &EventQuery);
    F(kEmpty).Set(1, 0, 0, &CountedAddRef);
    F(kEmpty).Set(2, 0, 0, &CountedRelease);
    F(kEmpty).value[0] = rng() % 64;
    F(kMsg).Set(0, "dp", 0, &MessageQuery);
    F(kMsg).Set(4, 0, 0, &MessageType);
    F(kMsg).value[0] = rng() % 7;
    F(kMsg).value[1] = rng() % 256;
    for (unsigned i = 0; i < kItems; ++i) {
        Obj &item = F(kItem0 + i);
        item.Set(0, "dp", 0, &ItemQuery);
        item.Set(1, 0, 0, &CountedAddRef);
        item.Set(2, 0, 0, &CountedRelease);
        item.Set(3, "d", 0, &ItemSlot3);
        item.value[0] = rng() % 16;
        item.value[1] = rng() % 16;
        item.value[2] = rng() % 2;
    }
    F(kListener).Set(5, 0, 0x6005);
    F(kListener).Set(7, "ddd", 0x6007);
    F(kListener).Set(9, "dddddd", 0x6009);
    F(kEx).Set(4, "ddd", 0x6104);
    F(kEx).Set(8, "d", 0x6108);
    F(kEx).Set(10, "d", 0x610a);
    F(kEx).Set(12, "d", 0x610c);
    for (unsigned s : {kSound0, kSound1}) {
        F(s).Set(0, "dp", 0, &SoundQuery);
        F(s).Set(9, 0, 0x6209);
        F(s).Set(12, 0, rng() % 3);
        F(s).value[1] = rng() % 2;
    }
    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f519c) = &F(kMsgSrv);
    *GameAddress<uint32_t>(0x10fad88) = 0; // TimerTick's last time
}

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xb22378)();
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    if (ours) cGZSndSysAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0xb22fd5)(memory);
    return memory;
}

void DumpString(Blob &b, const cRZString &s)
{
    b.push_back(uint32_t(s.mString.size()));
    for (char ch : s.mString) b.push_back(uint8_t(ch));
}

void DumpList(Blob &b, const Sys::RefList &list)
{
    b.push_back(uint32_t(list.size()));
    for (const AudioRef &r : list) b.push_back(svcfx::Norm(P(r.Get())));
    for (auto it = list.rbegin(); it != list.rend(); ++it) b.push_back(svcfx::Norm(P(it->Get())));
}

void DumpStopwatch(Blob &b, const uint8_t *w, bool unusedWritten)
{
    b.push_back(Word(w, 0));
    if (unusedWritten) b.push_back(Word(w, 4));
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(w, off));
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &sys = *static_cast<const Sys *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[5][2] = {{0, 0xe80f60}, {4, 0xe81040}, {8, 0xe81078}, {0xc, 0xe8108c}, {0x10, 0xe810bc}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    // Bytes never written (padding) are the allocator's on a factory route; the 0xc5 fill otherwise.
    b.push_back(Word(m, 0x14) & (gFactory ? 0xff : 0xffffffff));
    DumpString(b, sys.mName.s);
    b.push_back(Word(m, 0x18 + 0x1c)); // the name's own count
    b.push_back(svcfx::Norm(Word(m, 0x38)));
    b.push_back(Word(m, 0x3c));
    b.push_back(Word(m, 0x40));
    b.push_back(Word(m, 0x44) & (gFactory ? 0xffff : 0xffffffff));
    DumpStopwatch(b, m + 0x48, !gFactory);
    b.push_back(Word(m, 0x60) != 0);
    if (const uint32_t uptime = Word(m, 0x60)) DumpStopwatch(b, reinterpret_cast<const uint8_t *>(uintptr_t(uptime)), false);
    b.push_back(svcfx::Norm(Word(m, 0x64)));
    b.push_back(Word(m, 0x70) & (gFactory ? 0xff : 0xffffffff));
    for (const Sys::RefList *list : {&sys.mFastTargets, &sys.mSlowTargets, &sys.mPendingFast, &sys.mPendingSlow,
                                     &sys.mPendingTargetRemovals, &sys.mRenderers, &sys.mHandlers, &sys.mPendingHandlers,
                                     &sys.mPendingHandlerRemovals, &sys.mHeld})
        DumpList(b, *list);
    for (uint32_t off : kBusy) b.push_back(Word(m, off));
    b.push_back(uint32_t(sys.mNamedValues.size()));
    for (const auto &e : sys.mNamedValues) {
        DumpString(b, e.first.s);
        b.push_back(e.second);
    }
    b.push_back(svcfx::Norm(Word(m, 0x104)));
    b.push_back(svcfx::Norm(Word(m, 0x108)));
    for (uint32_t off = 0x10c; off < 0x14c; off += 4) b.push_back(Word(m, off));
    b.push_back(Word(m, 0x14c) & (gFactory ? 0xffff : 0xffffffff));
    LogState(); // the locks' counts
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

constexpr unsigned kKinds = 37;

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 11);
    std::vector<Op> ops;
    const unsigned n = 10 + rng() % 60;
    if (rng() % 6) ops.push_back({0, rng(), 0}); // Init
    for (unsigned i = 0; i < n; ++i) {
        // Weighted toward the adds (so the walks have something to walk).
        const unsigned r = rng() % (kKinds + 8);
        const unsigned kind = r < kKinds ? r : (r - kKinds) % 4 == 0 ? 10 : (r - kKinds) % 4 == 1 ? 20 : (r - kKinds) % 4 == 2 ? 24 : 12;
        ops.push_back({kind, rng(), rng()});
    }
    return ops;
}

void *PickItem(uint32_t a, bool allowNull)
{
    if (allowNull && a % 9 == 0) return nullptr;
    return &F(kItem0 + a % kItems);
}
uint32_t PickWord(uint32_t a)
{
    static const uint32_t kWords[] = {0, 1, 0xffffffff, 0x80000000, 7, 0x14, 0xfa};
    return a % 2 ? kWords[(a >> 1) % 7] : a;
}
float PickFloat(uint32_t a)
{
    static const float kFloats[] = {0.0f, 1.0f, -2.5f, 3.25f, 1e10f, -0.0f, 0.125f, 100.0f};
    return kFloats[a % 8];
}

// The queue is used unchecked once there is a reason to read it (the original's unchecked pointers).
bool HasQueue(const uint8_t *m) { return Word(m, 0x38) != 0; }
bool CanUpdate(const uint8_t *m) { return !m[0x45] || HasQueue(m); }

void RunOp(Blob &b, void *object, const Op &op)
{
    auto *const m = static_cast<uint8_t *>(object);
    void *const service = Face(object, 4);
    void *const target = Face(object, 8);
    void *const listener = Face(object, 0xc);
    void *const ex = Face(object, 0x10);
    const uint32_t a = op.arg, c = op.arg2;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    uint32_t word = 0x99;
    static const uint32_t kIids[] = {kEventIid, kOtherIid, 0x22222222};
    const uint32_t iid = kIids[(c >> 8) % 3];
    gOpKind = op.kind;
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(service, 8) & 0xff); break;
    case 1: b.push_back(Slot<uint32_t>(service, 9) & 0xff); break;
    case 2: { // SendEventText / PostEventText
        cRZString text;
        text.Construct(kNames[a % kNameCount]);
        if (a % 2 == 0) b.push_back(Slot<uint32_t>(object, 5, c, text.AsInterface(), 1u, 2u, 3u, 4u) & 0xff);
        else if (HasQueue(m)) b.push_back(Slot<uint32_t>(object, 9, c, text.AsInterface(), 1u, 2u, 3u, 4u) & 0xff);
        text.Destruct();
        break;
    }
    case 3: // SendEventObject / PostEventObject
        if (a % 2 == 0) b.push_back(Slot<uint32_t>(object, 6, c, PickItem(a >> 1, true), a, c, a ^ c, 5u) & 0xff);
        else if (HasQueue(m)) b.push_back(Slot<uint32_t>(object, 10, c, PickItem(a >> 1, true), a, c, a ^ c, 5u) & 0xff);
        break;
    case 4: // SendEvent / PostEvent
        if (a % 2 == 0) b.push_back(Slot<uint32_t>(object, 7, c, a, 2u, 3u, c ^ 9) & 0xff);
        else if (HasQueue(m)) b.push_back(Slot<uint32_t>(object, 11, c, a, 2u, 3u, c ^ 9) & 0xff);
        break;
    case 5: b.push_back(Slot<uint32_t>(object, 8, static_cast<void *>(&F(kEvent))) & 0xff); break;
    case 6: // QueueEvent
        if (HasQueue(m)) b.push_back(Slot<uint32_t>(object, 12, static_cast<void *>(&F(kEvent))) & 0xff);
        break;
    case 7: // CreateEvent
        b.push_back(Slot<uint32_t>(object, 13, c, iid, &out, a, 2u, 3u, 4u) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    case 8: // CreateEmptyEvent
        b.push_back(Slot<uint32_t>(object, 14, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    case 9: case 10: b.push_back(Slot<uint32_t>(object, 15, PickItem(a, false)) & 0xff); break;
    case 11: b.push_back(Slot<uint32_t>(object, 16, PickItem(a, true)) & 0xff); break;
    case 12: { // Update, a few times
        const unsigned times = 1 + a % 3;
        for (unsigned t = 0; t < times && CanUpdate(m); ++t) b.push_back(Slot<uint32_t>(object, 17, c >> t) & 0xff);
        break;
    }
    case 13: { // SetName / GetName / IsRunning / IsEnabled
        if (a % 2) {
            cRZString name;
            name.Construct(kNames[(a >> 1) % kNameCount]);
            Slot<void>(object, 19, static_cast<const cIGZString *>(name.AsInterface()));
            name.Destruct();
        }
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 20)));
        b.push_back(Slot<uint32_t>(object, 18) & 0xff);
        b.push_back(Slot<uint32_t>(object, 21) & 0xff);
        break;
    }
    case 14: // SetEnabled / SetRunning
        if (a % 2) Slot<void>(object, 22, bool(c % 2));
        else Slot<void>(object, 54, bool(c % 3 == 0));
        break;
    case 15: case 16: { // SetNamedValue / GetNamedValue (and the count)
        cRZString name;
        name.Construct(kNames[a % kNameCount]);
        if (op.kind == 15) b.push_back(Slot<uint32_t>(object, 23, static_cast<const cIGZString *>(name.AsInterface()), c) & 0xff);
        else b.push_back(Slot<uint32_t>(object, 24, static_cast<const cIGZString *>(name.AsInterface()), &word) & 0xff);
        b.push_back(word);
        b.push_back(Slot<uint32_t>(object, 25));
        name.Destruct();
        break;
    }
    case 17: // GetNamedValueAt (the indices around the count)
        b.push_back(Slot<uint32_t>(object, 26, a % 6, &word) & 0xff);
        b.push_back(word);
        break;
    case 18: { // GetNameAfter
        cRZString name;
        name.Construct(kNames[a % kNameCount]);
        b.push_back(Slot<uint32_t>(object, 27, c % 4, name.AsInterface()) & 0xff);
        DumpString(b, name);
        name.Destruct();
        break;
    }
    case 19: // GetLevel / ApplyLevel
        b.push_back(Slot<uint32_t>(object, 28));
        b.push_back(Slot<uint32_t>(object, 29, c) & 0xff);
        break;
    case 20: { // AddUpdateTarget
        static const uint32_t kPeriods[] = {0x14, 0xfa, 0, 0x13, 0x15};
        b.push_back(Slot<uint32_t>(object, 30, PickItem(a, false), kPeriods[c % 5]) & 0xff);
        break;
    }
    case 21: b.push_back(Slot<uint32_t>(object, 31, PickItem(a, true)) & 0xff); break;
    case 22: // ForEachUpdateTarget
        b.push_back(Slot<uint32_t>(object, 32, &ForEach, a, c) & 0xff);
        break;
    case 23: b.push_back(Slot<uint32_t>(object, a % 2 ? 33 : 34) & 0xff); break;
    case 24: b.push_back(Slot<uint32_t>(object, 35, PickItem(a, false)) & 0xff); break;
    case 25: // RemoveRenderer / HasRenderer / the count
        if (a % 2) b.push_back(Slot<uint32_t>(object, 36, PickItem(a >> 1, true)) & 0xff);
        b.push_back(Slot<uint32_t>(object, 37, PickItem(c, true)) & 0xff);
        b.push_back(Slot<uint32_t>(object, 38));
        break;
    case 26: // QuerySound / HasSound
        b.push_back(Slot<uint32_t>(object, 39, a % 4, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        b.push_back(Slot<uint32_t>(object, 40, c % 4) & 0xff);
        break;
    case 27: { // GetLock, GetTime, the objects
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 41)));
        if (Word(m, 0x60)) b.push_back(Slot<uint32_t>(object, 42)); // the uptime stopwatch is used unchecked
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 43)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 45)));
        void *got = out;
        b.push_back(Slot<uint32_t>(object, a % 2 ? 44 : 46, &got) & 0xff);
        b.push_back(svcfx::Norm(P(got)));
        if (got) Slot<uint32_t>(got, 2);
        break;
    }
    case 28: b.push_back(Slot<uint32_t>(object, 47, PickItem(a, false)) & 0xff); break;
    case 29: Slot<void>(object, 48); break;
    case 30: // the flag, the timer's
        Slot<void>(object, 49, bool(a % 2));
        b.push_back(Slot<uint32_t>(object, 50) & 0xff);
        b.push_back(Slot<uint32_t>(object, 51));
        if (Word(m, 0x64)) b.push_back(Slot<uint32_t>(object, 52, c)); // eax is left as it is without a timer
        break;
    case 31: { // QueryInterface on a face, AddRef / Release through others
        static const uint32_t kAsk[] = {1, 0x452294aa, cIGZSndSys::kIid, 0x406077d2, 0x5b30e7b3, 0x287fb697, 0x12345678};
        void *faces[] = {object, service, target, listener, ex};
        void *q = out;
        const uint32_t ask = kAsk[(a >> 4) % 7];
        const uint32_t ok = Slot<uint32_t>(faces[a % 5], 0, ask, &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 5], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 5], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        // The face casts.
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 3)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(object, 4)));
        b.push_back(svcfx::Norm(Slot<uint32_t>(listener, 10)));
        break;
    }
    case 32: // the service accessors
        b.push_back(Slot<uint32_t>(service, 3));
        if (a % 4 == 0) Slot<void>(service, 4, c);
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        Slot<void>(service, 7, bool(a % 2));
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        break;
    case 33: // DoMessage
        F(kMsg).value[0] = a % 7;
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff);
        break;
    case 34: { // the listener
        float v[6] = {9, 9, 9, 9, 9, 9};
        switch (a % 6) {
        case 0: Slot<void>(listener, 5); break;
        case 1: Slot<void>(listener, 6, &v[0], &v[1], &v[2]); break;
        case 2: b.push_back(Slot<uint32_t>(listener, 7, PickFloat(c), PickFloat(c >> 3), PickFloat(c >> 6)) & 0xff); break;
        case 3: Slot<void>(listener, 8, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]); break;
        case 4:
            b.push_back(Slot<uint32_t>(listener, 9, PickFloat(c), PickFloat(c >> 3), PickFloat(c >> 6), PickFloat(c >> 9),
                                       PickFloat(c >> 12), PickFloat(c >> 15)) & 0xff);
            break;
        case 5: b.push_back(Slot<uint32_t>(listener, (a >> 3) % 2 ? 3 : 4) & 0xff); break; // Init / Shutdown thunks
        }
        for (float f : v) b.push_back(*reinterpret_cast<const uint32_t *>(&f));
        break;
    }
    case 35: { // the listener-ex
        float v[12] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
        const float f = PickFloat(c);
        switch (a % 10) {
        case 0: Slot<void>(ex, 3, &v[0], &v[1], &v[2]); break;
        case 1: b.push_back(Slot<uint32_t>(ex, 4, f, PickFloat(c >> 3), PickFloat(c >> 6)) & 0xff); break;
        case 2: Slot<void>(ex, 5, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]); break;
        case 3:
            b.push_back(Slot<uint32_t>(ex, 6, f, f, f, PickFloat(c >> 3), f, f, f, f, f, f, f, PickFloat(c >> 6)) & 0xff);
            break;
        case 4: Slot<void>(ex, 7, &v[0]); break;
        case 5: b.push_back(Slot<uint32_t>(ex, 8, f) & 0xff); break;
        case 6: Slot<void>(ex, 9, &v[0]); break;
        case 7: b.push_back(Slot<uint32_t>(ex, 10, f) & 0xff); break;
        case 8: Slot<void>(ex, 11, &v[0]); break;
        case 9: b.push_back(Slot<uint32_t>(ex, 12, f) & 0xff); break;
        }
        for (float g : v) b.push_back(*reinterpret_cast<const uint32_t *>(&g));
        break;
    }
    case 36: // the timer ticks (its callback with its argument), as the timer thread would
        if (gWorld.tickFn && Word(m, 0x60) && CanUpdate(m)) reinterpret_cast<void(__cdecl *)(void *)>(gWorld.tickFn)(gWorld.tickArg);
        b.push_back(*GameAddress<uint32_t>(0x10fad88));
        break;
    }
}

} // namespace sndsys_test

// ForEachUpdateTarget's callback (cdecl): logs the target and the words, and stops now and then.
bool __cdecl sndsys_test::ForEach(cIGZUnknown *target, uint32_t a, uint32_t b)
{
    LogState();
    Ev(svcfx::Norm(P(target)));
    Ev(a);
    Ev(b);
    return Draw(4) != 0;
}

using namespace sndsys_test;

DIFF_TEST(SndSys_Run, 0xb22fd5)
{
    // Also reached (see the file comment): 0xb22378 (factory), 0xb231cd, 0xb232e1, 0xb241e1, 0x2296a0, 0xb24435, 0x6e70ed,
    // 0x6f5105, 0xb234b4, 0xb23525, 0xb2340d, 0xb235e1, 0xb240a9, 0xb24107, 0xb24015, 0xb241b0, 0xb23846, 0xb2389f, 0xb2427d,
    // 0xb24ed7, 0xb2506e, 0x954fb5, 0xb24678, 0x158f90, 0x2296b0, 0x229f40, 0xb247b5, 0xb23a10, 0x249080, 0xb23ab4,
    // 0xb23af8, 0x230133, 0xb24865, 0xb24379, 0xb24fc0, 0xb237b5, 0xb23766, 0xb23406, 0xb24307, 0xb24f5d, 0xb23e70,
    // 0x24ad70, 0xb238ee, 0xb23eb5, 0x6e7c71, 0xb23a08, 0x418aa0, 0xb23c31, 0x418b60, 0xb23c05, 0xb2331b, 0xb23edc,
    // 0xb24b35, 0xb23c73, 0xb23c5d, 0xb24b42, 0xb2485b, 0xb2425d, 0x333def, 0xb2444f, 0x1674b0, 0x20c6f0, 0xb23bff,
    // 0x144910, 0x300a0, 0xb23ca1, 0xb24c1a, 0x18a7a0, 0x148260, 0xb24265, 0xb23303, 0xb24457, 0xb236f3, 0xb24275,
    // 0xb23313, 0xb24467, 0xb23e68, 0xb24ecf, 0xb23390, 0xb23bc9, 0xb249d9, 0xb23b7c, 0xb248ba, 0xb23388, 0xb2426d,
    // 0xb2330b, 0xb2445f, 0xb23c7a, 0xb24b51, 0xb2395b, 0xb2446f, 0xb239f9, 0xb24722, 0xb23bf0, 0xb24aa2, 0xb239ea,
    // 0xb2468f, 0xb250f0, 0xb250e0, 0xb2376d, 0xb23923.
    svcfx::Globals globals;
    globals.SetWord(0x10c9378, 0);
    globals.SetWord(0x10f519c, 0);
    globals.SetWord(0x10fad88, 0);
    for (uint32_t i = 0; i < 5; ++i) globals.SetWord(cRZStopwatch::kScalesRva + 4 * i, 0x3f800000); // 1.0f
    globals.SetWord(cRZStopwatch::kOverheadRva, 0);
    globals.SetWord(cRZStopwatch::kOverheadRva + 4, 0);
    svcfx::gFrameworkIndex = kDirector;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch eventMemory(0xb2878d, shape::CodeOf(&StandEventMemory));
    shape::CodePatch event(0xb286d4, shape::CodeOf(&StandEvent));
    shape::CodePatch emptyEvent(0xb28731, shape::CodeOf(&StandEmptyEvent));
    shape::StandIn queue(0xc66b0d, &cGZMessageQueueAdapters::Construct2, &StandQueue);
    shape::StandIn timer(0x9ccf32, &cRZCallbackTimer::Construct, &StandTimer);
    shape::CodePatch counter(0x1c95c4, shape::CodeOf(&StandCounter));
    const auto savedCounter = cRZStopwatch::sPerformanceCounterOverride;
    cRZStopwatch::sPerformanceCounterOverride = &StandCounterOurs;

    static const uint32_t kSteps[] = {0, 1, 0xfa, 0xfb, 0x7d, 0x12c, 0xf9, 3};
    bool failed = false;
    for (uint32_t seed = 1; seed <= 2500 && !failed; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gOurs = side == 1;
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                gWorld.now += kSteps[Draw(8)];
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else {
                    // The last release destroys it; every fifth seed the deleting destructor (slot 53, flags 1) instead.
                    // (No state log: the destructor frees the locks before the lists release their items.)
                    gObject = nullptr;
                    if (seed % 5) b.push_back(Slot<uint32_t>(object, 2));
                    else b.push_back(svcfx::Norm(Slot<uint32_t>(object, 53, 1u)));
                    gObject = nullptr;
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "SndSys_Run", seed)) {
                char flag[4];
                if (svcfx::gTrace || GetEnvironmentVariableA("SNDSYS_WORDS", flag, sizeof(flag)))
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                failed = true;
                break;
            }
        }
    }
    cRZStopwatch::sPerformanceCounterOverride = savedCounter;
    svcfx::gFrameworkIndex = 0;
}
