// Differential test for cTSMainToolbar (lane JE, src/ui/cTSMainToolbar.h): the class in the standalone shape.
//
// MainToolbar_Run: a toolbar built by the original constructor (0x619068, game vtables 0xde1cd4 / 0xde1d0c / 0xde1d28 /
// 0xde1ea4 / 0xde1eb8 / 0xde1ecc) or the original creator (0x61e0e9) against one built by ours (the constructor hook's
// adapter, or Create), each driven through its own vtables by the same random sequence of operations, then destroyed by the
// last Release (the deleting destructor 0x61aa67 behind the +4 thunk 0x61aa20, the destructor 0x61a06b). Operations: Init
// (0x61e515, also through the toolbar face's thunk 0x61e6d3), Shutdown (0x62232a), OnTick (0x62013a), QueryInterface
// (0x620275; thunks 0x6202b2 / 0x6202ba / 0x6202c2 / 0x6202ca / 0x6202d2) / AddRef / Release on every face (thunks
// 0x61acba / 0x61acc2 / 0x6204af / 0x6204b7), the base's service slots, Write (0x62255c), Read (0x6202da), GetGZCLSID
// (0x61e129), both DoMessages, and every cITSMainToolbar slot with its arguments drawn around their edges:
// 0x61fed0, 0x61b94c, 0x6208e5, 0x61fddd, 0x61e6db, 0x621b33, 0x61e388, 0x61e32f, 0x61e248, 0x62190a, 0x6217a6, 0x61fe0f,
// 0x622462, 0x61fc1e, 0x620bd9, 0x620f58, 0x61e017, 0x61dfc9, 0x621a9c, 0x62245b, 0x61b33c, 0x61b09a, 0x61b120, 0x620997,
// 0x61e1bd, 0x622301, 0x61b909, 0x61dfe6, 0x6209c2, 0x620915, 0x621a6b, 0x622450, 0x621f49, 0x62026e, 0x620ff5, 0x61e4d0,
// 0x620926, 0x6201dd, 0x621cf5, 0x61b59f, 0x61b910, 0x621aa9, 0x620c03, 0x61e2fd, 0x620937, 0x620448, 0x61abfd, 0x6204bf,
// 0x622554, 0x61df34, 0x61e1aa, 0x62179b, 0x61af51, 0x621772, 0x621f42, 0x61fc17, 0x6200be, and the folded bodies the
// game's toolbar vtable shares with other classes: 0x577030, 0x230ec0, 0x229b60, 0x300a0, 0x1674b0, 0x1674c0, 0x147800,
// 0x1694c0, 0x6208de, 0x251bf0, 0x24a310, 0x24bdc0, 0x41a700, 0x41a820, 0x61e010, 0x418a90, 0x145520, 0x169f60, 0x1e0950,
// 0x1e5370, 0x5bb9f1, 0x61e327, 0x24b400, 0x24bdb0, 0x475440, 0x418bc0.
//
// Every object the toolbar reaches is a recording fake (tests/ts_services_fixture.h). Stood in for on both sides: the
// note object's constructor UtilObjectB::Construct (0x9c7b3d, to know the notes the recorders may read) and on the image
// the game's operator new / delete, the cached getters still game code (0x211da3, 0x20df9a, 0x9c5ab4, 0x9c5a89, 0x9c5b7e:
// the cell, never the framework fallback), the trace calls (0x61ea5d, 0x61eb0d, 0x620af9, 0x620b15, 0x620b4d, 0x620b69,
// 0x620bbd), the string entry 0xb39350, cTSString's constructor 0x1d0e90 (and ours), the tick hand-off 0xb42250 (and ours, ApplyScriptConditions), the window
// callback 0x61e12f, the UCP state constructor 0x671b29, the panel's members (0x61b843, 0x620643, 0x61b61e, 0x61b75f) and
// the entries of the slots still game code (0x621006, 0x620c7e, 0x621ba2, 0x61b1d5, 0x61b36c, 0x61fc26, 0x61fed5, and the
// DoMessages 0x61b99c / 0x61bc58): each a recorder logging its arguments. Compared after every operation: the result (the
// low byte where the original answers a bool in al), the event log, every field (the message id set and the tick list by
// content, the help text by content; the bytes the constructor leaves alone too: blocks start as 0xc5), every fake's count.
//
// Left out, with the original code that makes them crash on both sides: slots 12 and 77 without the +0x90 window (0x620448
// calls it unchecked), slot 80 without a controller (0x622554 jumps through it), slots 40 / 41 / 43 when the state
// service's slot 53 object is null (0x61838f queries its virtual base unchecked), slot 81 with no object when the service
// object D's slot 3 answers (0x61df34 calls the object's slot 83 unchecked).
#include "rz/RZMessageServer.h"
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppServices2.h"
#include "game/game.h"
#include "gz/LoadUIScript.h"
#include "misc/UtilitiesC.h"
#include "rz/cRZString.h"
#include "ts/cTSString.h"
#include "ui/cTSMainToolbar.h"

#include <cstring>
#include <list>
#include <new>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace toolbar_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{17, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSMainToolbar;

constexpr uint32_t kTag = 0x7d000000;  // the toolbar
constexpr uint32_t kMoreTag = 0x7b000000; // gMore
constexpr uint32_t kVTag = 0x7a000000; // the virtual-base objects

enum : unsigned {
    kService = 0, kFramework, kTesting, kWalk, kNotifier, kServer, kSound, kClock, kState, kView, kObj38, kApp, kSel, kD,
    kD3, kPersist, kOStream, kIStream, kW78, kW78a, kW78b, kCtrl, kCtrlFace, kW90,
};
enum : unsigned { mPanel = 0, mDC, mBC, mArg, mA, mB, mRef0, mRef3 = mRef0 + 3, mTick0, mTick3 = mTick0 + 3, kMore };
Obj gMore[kMore];
Obj &M(unsigned i) { return gMore[i]; }

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte

struct World {
    bool testing; // the framework hands out the testing service
    bool sound;
    bool clock;
    bool view;    // the state service's slot 53 object
    bool obj38;
    bool sel;     // the app's slot 26 object
    bool d3;
    uint32_t entry;
};
World gWorld;

// ---- virtual-base objects (the state holder and the view it comes from) ----

// +4 holds the vbtable pointer; its second entry (4) puts the cIGZUnknown base at +8.
const int32_t kVbTable[2] = {0, 4};
struct VObj {
    void **vt;
    const int32_t *vbtab;
    Obj base;
};
VObj gView, gHolder;
void *gHolderVt[17];
uint32_t __fastcall HolderSlot0(VObj *, void *, uint32_t flag)
{
    Ev(0x4f00);
    Ev(flag);
    return gRng();
}
uint32_t __fastcall HolderSlot16(VObj *, void *)
{
    Ev(0x4f10);
    return Bool();
}
// The view's base QueryInterface: the holder (counted) or a refusal.
uint32_t ViewQuery(Obj *self, uint32_t *a)
{
    if (self->value[0] == 0) return 0;
    ++gHolder.base.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gHolder;
    return 1;
}

// ---- the object slot 81 calls slot 83 on ----

struct Big {
    void **vt;
    uint32_t id;
};
void *gBigVt[84];
uint32_t __fastcall BigSlot83(Big *self, void *, uint32_t a, uint32_t b)
{
    Ev(0xb183);
    Ev(self->id);
    Ev(a);
    Ev(b);
    return gRng();
}
Big gBig;

// ---- the UCP state (0x671b29 stand-in): an object at +8 with a counting AddRef / Release ----

void *gUcpVt[3];
uint32_t __fastcall UcpCount(void *, void *)
{
    Ev(0x7c01);
    return 1;
}
void *__fastcall StandUcp(void *self, void *)
{
    Ev(0x671b29);
    *reinterpret_cast<void ***>(static_cast<uint8_t *>(self) + 8) = gUcpVt;
    return self;
}

// ---- stand-ins on the image ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
template <uint32_t Cell> void *__cdecl StandGetter() { return *GameAddress<void *>(Cell); }

void __cdecl StandTraceA(uint32_t a, uint32_t b)
{
    Ev(0x61ea5d);
    Ev(a);
    Ev(b);
}
void __cdecl StandTraceB(uint32_t a, uint32_t b)
{
    Ev(0x61eb0d);
    Ev(a);
    Ev(b);
}
// The traces' values by trace: 'p' a pointer to a pair on the stack (the pair is logged too), 'd' data.
constexpr char TraceKind(uint32_t rva)
{
    switch (rva) {
    case 0x620af9: case 0x620b69: return 'p';
    case 0x620b15: case 0x620b4d: case 0x620bbd: return 'd';
    default: return 0;
    }
}
// The value the trace reads, and when it is a pointer (to a pair on the stack), the pair.
template <uint32_t Rva> void __cdecl StandTrace(void *, const uint32_t *value)
{
    Ev(Rva);
    const uint32_t w = *value;
    char what[64];
    std::snprintf(what, sizeof(what), "StandTrace<%#x> value", Rva);
    Ev(svcfx::Word(TraceKind(Rva), w, [&] { return std::string(what); }, "in TraceKind"));
    if (TraceKind(Rva) == 'p' && w) {
        Ev(reinterpret_cast<const uint32_t *>(uintptr_t(w))[0] - P(GameAddress<void>(0)));
        Ev(reinterpret_cast<const uint32_t *>(uintptr_t(w))[1] - P(GameAddress<void>(0)));
    }
}
uint32_t __cdecl StandEntry()
{
    Ev(0xb39350);
    return gWorld.entry;
}
void __cdecl StandTick(void *object)
{
    Ev(0xb42250);
    Ev(svcfx::Norm(P(object)));
}
uint32_t __cdecl StandWindowCallback(uint32_t a, uint32_t b, void *window, const uint8_t *value)
{
    Ev(0x61e12f);
    Ev(a);
    Ev(b);
    Ev(svcfx::Norm(P(window)));
    Ev(*value);
    return gRng();
}
cTSString *__fastcall StandTSString(cTSString *self, void *, uint16_t table, uint16_t entry, uint32_t group, uint32_t type)
{
    Ev(0x1d0e90);
    Ev(table);
    Ev(entry);
    Ev(group);
    Ev(type);
    self->mString.Construct(Draw(3) == 0 ? "Pause the game (P)" : "x");
    return self;
}

std::set<const void *> gNotes;
UtilObjectB *__fastcall StandNote(UtilObjectB *self, void *, uint32_t arg)
{
    Ev(0x9c7b3d);
    Ev(arg);
    self->mVTable = GameAddress<void>(0xe6180c);
    self->mArg = arg;
    self->mZero8 = 0;
    self->mWordC = 0;
    self->mWordE = 6;
    self->mZero10 = 0;
    gNotes.insert(self);
    return self;
}
// Record's arguments by the stood-in function, as logged (svcfx::detail::DumpKinds letters, one per argument; the
// second is a note when it is one the test made, else by its letter).
constexpr const char *RecordKinds(uint32_t rva)
{
    switch (rva) { // the test's random words: data
    case 0x61b1d5: return "d";
    case 0x61b36c: return "d";
    case 0x61b61e: return "dd";
    case 0x61b75f: return "dd";
    case 0x61b843: return "d";
    case 0x61b99c: return "d";
    case 0x61bc58: return "d";
    case 0x61fc26: return "ddd";
    case 0x61fed5: return "ddddd";
    case 0x620c7e: return "d";
    case 0x621006: return "dd";
    case 0x621ba2: return "d";
    default: return "";
    }
}
template <class What> void LogMaybeNote(uint32_t word, char kind, What &&what)
{
    if (!gNotes.count(reinterpret_cast<const void *>(uintptr_t(word))))
        return svcfx::detail::KindWord(svcfx::gEv, kind ? kind : '-', word, what);
    const auto *note = reinterpret_cast<const uint32_t *>(uintptr_t(word));
    Ev(0x407e);
    Ev(note[0] - P(GameAddress<void>(0)));
    for (int i = 1; i < 5; ++i) Ev(note[i]);
}

// The recorders over the slots still game code and the panel's members: `this`, the arguments, a drawn result.
template <uint32_t Rva, unsigned... I> uint32_t __fastcall Record(void *self, void *, shape::StackWord<I>... w)
{
    const uint32_t words[] = {0u, w...};
    Ev(0xf0000000u | Rva);
    Ev(svcfx::Norm(P(self)));
    const char *kinds = RecordKinds(Rva);
    for (unsigned i = 0; i < sizeof...(I); ++i) {
        const char kind = i < std::strlen(kinds) ? kinds[i] : '-';
        auto what = [&] {
            char text[80];
            std::snprintf(text, sizeof(text), "Record %#x argument %u", Rva, i);
            return std::string(text);
        };
        if (i == 1) LogMaybeNote(words[i + 1], kind, what);
        else svcfx::detail::KindWord(svcfx::gEv, kind, words[i + 1], what);
    }
    return gRng();
}
template <uint32_t Rva, unsigned... I> const void *RecordAt(std::integer_sequence<unsigned, I...>)
{
    return reinterpret_cast<const void *>(&Record<Rva, I...>);
}
template <uint32_t Rva, unsigned N> const void *RecordFor() { return RecordAt<Rva>(std::make_integer_sequence<unsigned, N>()); }

// ---- fake handlers ----

void LogText(const char *text, uint32_t size)
{
    Ev(size);
    for (uint32_t i = 0; i < size; ++i) Ev(uint8_t(text[i]));
}
void LogRaw(uint32_t at) // an as2::RawString
{
    const auto *s = reinterpret_cast<const as2::RawString *>(uintptr_t(at));
    LogText(s->mCapacity > 15 ? s->mPtr : s->mBuf, s->mSize);
    Ev(s->mCapacity);
}
// The framework's slot 5 (class, iid, out): the testing service for its ids, the walker's target for any other.
uint32_t ServiceQuery(Obj *, uint32_t *a)
{
    Obj *const out = a[0] == 0x0ac1887e ? (gWorld.testing ? &F(kTesting) : nullptr) : &F(kWalk);
    if (out) ++out->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = out;
    return out ? 1 : 0;
}
uint32_t TestingAdd(Obj *, uint32_t *a)
{
    LogRaw(a[2]);
    return gRng();
}
uint32_t NotifierMessage(Obj *, uint32_t *a)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(words[i]);
    return gRng();
}
// The view's slot 20 (a mode name): logged like a generic slot, but the name by content (each side's literal lives in its
// own image).
uint32_t __fastcall ViewName(Obj *self, void *, const char *text)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(20);
    Ev(1);
    LogText(text, uint32_t(std::strlen(text)));
    return 0;
}
uint32_t HandOutIf(Obj &obj, uint32_t out, bool yes)
{
    if (!yes) return 0x100;
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 0x101;
}
uint32_t W78Query(Obj *, uint32_t *a) { return HandOutIf(F(kW78a), a[1], Draw(4) != 0); }
uint32_t W78aQuery(Obj *, uint32_t *a) { return HandOutIf(F(kW78b), a[1], Draw(4) != 0); }
uint32_t CtrlQuery(Obj *, uint32_t *a) { return HandOutIf(F(kCtrlFace), a[1], Draw(3) != 0); }
uint32_t Drawn(Obj *, uint32_t *) { return gRng(); }
uint32_t DrawnBool(Obj *, uint32_t *) { return Bool(); }
uint32_t DrawnSmall(Obj *, uint32_t *) { return Draw(6); }
uint32_t ReadWord(Obj *self, uint32_t *a)
{
    // The stream's words: a version from {0, 1, 2, 3}, then values around 0 / 1.
    const uint32_t n = self->value[0]++;
    uint32_t v = n == 0 ? Draw(4) : Draw(3) == 0 ? gRng() : Draw(3);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = v;
    return 0;
}

// ---- building the world ----

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    gNotes.clear();
    for (unsigned i = 0; i < kMore; ++i) gMore[i].Reset(300 + i);
    gView.base.Reset(400);
    gHolder.base.Reset(401);
    gView.vbtab = gHolder.vbtab = kVbTable;
    gView.vt = nullptr;
    gHolder.vt = gHolderVt;
    gHolderVt[0] = reinterpret_cast<void *>(&HolderSlot0);
    gHolderVt[16] = reinterpret_cast<void *>(&HolderSlot16);
    gBig.vt = gBigVt;
    gBig.id = 0xb16;
    gBigVt[83] = reinterpret_cast<void *>(&BigSlot83);
    gUcpVt[1] = gUcpVt[2] = reinterpret_cast<void *>(&UcpCount);

    gWorld.testing = rng() % 4 != 0;
    gWorld.sound = rng() % 4 != 0;
    gWorld.clock = rng() % 5 != 0;
    gWorld.view = rng() % 6 != 0;
    gWorld.obj38 = rng() % 3 != 0;
    gWorld.sel = rng() % 4 != 0;
    gWorld.d3 = rng() % 3 != 0;
    gWorld.entry = rng() % 3 == 0 ? 0xffffu : rng() % 0x30;

    F(kService).Set(5, "ddp", 0, &ServiceQuery);
    F(kFramework).Set(9, "p", 0x99);
    F(kTesting).Set(4, "pdp-", 0, &TestingAdd);
    F(kTesting).Set(5, "pdp", 0, &TestingAdd);
    F(kWalk).Set(5, "pd", 0x55);
    F(kWalk).Set(6, "pd", 0x66);
    F(kNotifier).Set(4, "p-", 0, &NotifierMessage);
    F(kNotifier).Set(5, "pd", 0x55);
    F(kNotifier).Set(6, "pd", 0x66);
    svcfx::UseAsMessageServer(F(kServer));
    F(kSound).Set(11, "dd---", 0x11);
    F(kClock).Set(21, 0, 0, &DrawnSmall);
    F(kClock).Set(26, 0, 0, &DrawnBool);
    F(kClock).Set(48, 0, 0, &DrawnSmall);
    F(kClock).Set(49, "d", 0x49);
    F(kState).Set(53, 0, gWorld.view ? &F(kView) : nullptr);
    F(kState).Set(38, 0, gWorld.obj38 ? &F(kObj38) : nullptr);
    F(kView).Set(20, 1, 0u);
    F(kView).table[20] = reinterpret_cast<void *>(&ViewName);
    F(kView).Set(25, "d", 0x25);
    F(kView).Set(37, 0, &gView);
    F(kObj38).Set(13, 0, 0x13);
    gView.base.Set(0, "dp", 0, &ViewQuery);
    gView.base.value[0] = rng() % 4 != 0;
    F(kApp).Set(26, 0, gWorld.sel ? &F(kSel) : nullptr);
    F(kSel).Set(39, 0, 0, &Drawn);
    F(kSel).Set(41, 0, 0x41);
    F(kD).Set(22, "p", 0, &DrawnBool);
    F(kD).Set(42, "p", 0, &Drawn);
    F(kD).Set(3, 0, gWorld.d3 ? &F(kD3) : nullptr);
    F(kD3).Set(19, "p", 0x19);
    F(kPersist).Set(72, "pdd", 0, &Drawn);
    F(kPersist).Set(73, "pdd-", 0, &Drawn);
    F(kOStream).Set(8, "d", 0x88);
    F(kOStream).Set(18, 0, 0, &DrawnBool);
    F(kIStream).Set(8, "p", 0, &ReadWord);
    F(kIStream).Set(18, 0, 0, &DrawnBool);
    F(kW78).Set(0, "dp", 0, &W78Query);
    F(kW78a).Set(9, "dp", 0, &W78aQuery);
    F(kW78b).Set(6, "d", 0, &Drawn);
    F(kCtrl).Set(0, "dp", 0, &CtrlQuery);
    F(kCtrlFace).Set(15, 0, 0, &Drawn);
    F(kW90).Set(3, "d", 0, &DrawnBool);
    F(kW90).Set(6, "ddd-", 0x66);
    F(kW90).Set(7, "dd-", 0x77);
    F(kW90).Set(8, "dd", 0x88);
    M(mPanel).Set(0, "d", 0u);
    M(mDC).Set(5, "d", 0x55);
    M(mDC).Set(6, 0, 0x66);
    M(mArg).Set(8, 0, &M(mA));
    M(mA).Set(61, 0, &M(mB));
    M(mB).Set(28, 0, 0, &Drawn);

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = &F(kFramework);
    *GameAddress<void *>(0x10f5198) = &F(kNotifier);
    *GameAddress<void *>(0x10f519c) = &F(kServer);
    *GameAddress<void *>(0x10f51b4) = gWorld.sound ? &F(kSound) : nullptr;
    *GameAddress<void *>(0x10cc2ac) = gWorld.clock ? &F(kClock) : nullptr;
    *GameAddress<void *>(0x10cc458) = &F(kState);
    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10f51ac) = &F(kD);
    *GameAddress<void *>(svcfx::kPersistCell) = &F(kPersist);
    // The build / ceiling modes' first-use bytes (0x10dc598 / 0x10dc599).
    auto *once = GameAddress<uint8_t>(0x10dc598);
    once[0] = uint8_t(rng() % 2);
    once[1] = uint8_t(rng() % 2);
}

// ---- driving a toolbar ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t CallWords(void *face, unsigned slot, unsigned words, const uint32_t *a)
{
    switch (words) {
    case 0: return Slot<uint32_t>(face, slot);
    case 1: return Slot<uint32_t>(face, slot, a[0]);
    case 2: return Slot<uint32_t>(face, slot, a[0], a[1]);
    case 3: return Slot<uint32_t>(face, slot, a[0], a[1], a[2]);
    case 4: return Slot<uint32_t>(face, slot, a[0], a[1], a[2], a[3]);
    default: return Slot<uint32_t>(face, slot, a[0], a[1], a[2], a[3], a[4]);
    }
}

// The bytes the constructor leaves alone (compared as 0xc5 when the constructor runs on filled memory).
bool Unwritten(uint32_t off)
{
    static const uint32_t kRanges[][2] = {{0x19, 0x1c}, {0x35, 0x38}, {0x60, 0x64}, {0x71, 0x74}, {0x7d, 0x80}, {0x89, 0x8c},
                                          {0xa1, 0xa8}, {0xac, 0xad}, {0xae, 0xb0}, {0xb6, 0xb8}, {0xc4, 0xc8}, {0x12a, 0x130}};
    for (const auto &r : kRanges)
        if (off >= r[0] && off < r[1]) return true;
    return false;
}

bool gFactory;
// 0x5bb9f1 on the original's side, the hook's adapter on ours.
uint32_t(__fastcall *gIsBuildMode)(void *face, void *edx);
void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        // The creator's block holds each side's heap leftovers where the constructor writes nothing: those bytes are set
        // alike on both sides afterwards (slots 43 and 57 read +0xa4 / +0xac).
        void *face = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x61e0e9)();
        b.push_back(face ? 1 : 0);
        auto *object = static_cast<uint8_t *>(face) - 0x1c;
        for (uint32_t off = 0; off < Sys::kSize; ++off)
            if (Unwritten(off)) object[off] = 0xc5;
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(cTSMainToolbarAdapters::Construct(memory, nullptr))
                   : ctx.Original<void *(__thiscall *)(void *)>(0x619068)(memory);
    b.push_back(r == memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] =
    "--dd--d- ---ddddd ---ddddd dddddppd --dppppd ddddddpp dd----pp p-pppppp "
    "p------- -ddd";
void Dump(Blob &b, void *object, bool ours, const shape::OurTables<6> &tables)
{

    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[6][2] = {{0, Sys::kVTable},        {4, Sys::kVTable4},         {0x1c, Sys::kToolbarVTable},
                                          {0x20, Sys::kMessageVTable}, {0x24, Sys::kMessage2VTable}, {0x28, Sys::kSerialVTable}};
    for (unsigned f = 0; f < 6; ++f) {
        const uint32_t w = Word(m, kFaces[f][0]);
        b.push_back(ours ? w == P(tables.face[f]) : w == P(GameAddress<void>(kFaces[f][1])));
    }
    for (uint32_t off = 8; off < Sys::kSize; off += 4) {
        if (off >= 0x1c && off <= 0x28) continue;
        if (off == 0x40) { // the message id set, in order
            const auto &ids = *reinterpret_cast<const std::set<uint32_t> *>(m + 0x40);
            b.push_back(uint32_t(ids.size()));
            for (uint32_t id : ids) b.push_back(id);
            continue;
        }
        if (off == 0x44) continue;
        if (off == 0x80) { // the tick list
            const auto &list = *reinterpret_cast<const std::list<void *> *>(m + 0x80);
            b.push_back(uint32_t(list.size()));
            for (void *o : list) b.push_back(svcfx::Norm(P(o)));
            continue;
        }
        if (off == 0x84) continue;
        if (off == 0xe4) {
            b.push_back(Word(m, off) != 0);
            continue;
        }
        if (off == 0x104) { // the help text
            const auto *s = reinterpret_cast<const cRZString *>(m + 0x104);
            b.push_back(Word(m, 0x104) - P(GameAddress<void>(0)));
            b.push_back(uint32_t(s->mString.size()));
            b.push_back(uint32_t(s->mString.capacity()));
            for (char c : s->mString) b.push_back(uint8_t(c));
            b.push_back(Word(m, 0x120));
            continue;
        }
        if (off > 0x104 && off < 0x124) continue;
        svcfx::Field(b, Word(m, off), kObjectKinds, off);
    }
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2, arg3;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 40;
    if (rng() % 3) ops.push_back({0, 0, 0, 0});
    // A state as the game code the toolbar still forwards to (slots 25, 29, the DoMessages) leaves it: fields poked.
    const unsigned pokes = rng() % 16;
    for (unsigned i = 0; i < pokes; ++i) ops.push_back({8, uint32_t(rng()), uint32_t(rng()), 0});
    for (unsigned i = 0; i < n; ++i) ops.push_back({1 + unsigned(rng() % 12), uint32_t(rng()), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

const uint32_t kIids[] = {1, cITSMainToolbar::kIID, Sys::kSerialIid, 0x287fb697, 0x452294aa, 0x12345678, 0};
const uint32_t kWords[] = {0, 1, 2, 3, 4, 5, 0xb, 0x100, 0x101, 0xffffffff, 0x80000000, 0x7fffffff, 0x1ff, 0xff};
// Floats around the 32-step test of slot 60: zeros, the last tiny values in and the first out, ordinary ones.
const uint32_t kFloats[] = {0, 0x80000000, 1, 0x1f, 0x20, 0x21, 0x8000001f, 0x80000020, 0x80000021, 0x3f800000, 0xbf800000,
                            0x7f800000, 0x7fc00000, 0xffffffff, 0x00800000};

uint32_t Pick(uint32_t r) { return r % 3 == 0 ? r : kWords[(r >> 2) % (sizeof(kWords) / 4)]; }

// Toolbar slots by argument words (ret N / 4); 0..2 are the cIGZUnknown slots, run separately.
constexpr unsigned kToolbarWords[94] = {
    2, 0, 0, 0, 0, 0, 0, 1, 1, 0, // 0
    1, 0, 1, 0, 0, 1, 3, 2, 0, 0, // 10
    0, 1, 1, 0, 0, 2, 0, 1, 0, 1, // 20
    1, 0, 1, 1, 0, 0, 0, 1, 1, 0, // 30
    1, 2, 0, 0, 1, 0, 0, 0, 1, 2, // 40
    2, 1, 1, 1, 0, 0, 0, 0, 0, 1, // 50
    1, 2, 1, 1, 0, 0, 0, 1, 1, 0, // 60
    1, 0, 1, 0, 0, 2, 3, 1, 0, 0, // 70
    0, 3, 5, 0, 0, 0, 1, 0, 0, 1, // 80
    0, 0, 0, 1,                   // 90 (93: one dword the original pops and never reads)
};

// What each toolbar slot answers: v void, b a bool in al, h a word in ax, w a dword.
// The word results by slot (svcfx::Field, word `slot` of the string): 'p' pointer, 'd' data.
constexpr char kWordResultKinds[] =
    "-------- ---p-ddd -------d dddddd-- ----pdd- ---d-ppp -------- -------- "
    "ddd----- ----d--- dddpppp- pp";
constexpr char kResult[] = "www"
                           "bvbbbbvvwv" // 3..12
                           "wwwvvvvbvv" // 13..22
                           "wwwwwwwvhv" // 23..32
                           "vbbwwwvvvv" // 33..42
                           "wvwwwvvvvv" // 43..52
                           "vbbbbbvbvv" // 53..62
                           "vwwwvvvvbv" // 63..72
                           "bbvwvvvwww" // 73..82
                           "wwwwbwwbbb" // 83..92
                           "v";         // 93
static_assert(sizeof(kResult) == 95, "one letter per slot");

void Poke(uint8_t *m, uint32_t a, uint32_t c)
{
    auto ref = [&](uint32_t off, Obj &o) {
        ++o.refs;
        *reinterpret_cast<void **>(m + off) = &o;
    };
    switch (a % 20) {
    case 14: *reinterpret_cast<int32_t *>(m + 0x4c) = int32_t(c % 4) - 1; break; // the pause count
    case 15: *reinterpret_cast<int32_t *>(m + 0x50 + 4 * (c % 2)) = int32_t((c >> 1) % 4); break; // Word50 / Word54
    case 16: *reinterpret_cast<int32_t *>(m + 0x68) = int32_t(c % 4); break;
    case 17: m[0x70] = uint8_t(c % 2); break; // paused
    case 18: m[0x31 + c % 3] = uint8_t((c >> 2) % 2); break; // the modified flags
    case 19: *reinterpret_cast<int32_t *>(m + 0x2c) = int32_t(c % 3); break; // the mode
    case 0: {
        static const int32_t kModes[] = {0xb, 0xb, 0xa, 3, 4, 0, 1, 2};
        *reinterpret_cast<int32_t *>(m + 0x58) = kModes[c % 8];
        break;
    }
    case 1: *reinterpret_cast<void **>(m + 0x74) = c % 3 ? &M(mPanel) : nullptr; break;
    case 2: ref(0x90, F(kW90)); break;
    case 3: ref(0xdc, M(mDC)); break;
    case 4: ref(0xbc, M(mBC)); break;
    case 5: ref(0xb8 + 4 * 0, M(mRef0 + c % 4)); break;
    case 6: { // the tick list and its flag
        auto &list = *reinterpret_cast<std::list<void *> *>(m + 0x80);
        const uint32_t n = c % 4;
        for (uint32_t i = 0; i < n; ++i) { // counted references (the list's elements are cRZAutoRefCounts)
            Obj *const o = (c >> (4 + i)) % 5 == 4 ? nullptr : &M(mTick0 + (c >> (8 + 2 * i)) % 4);
            if (o) ++o->refs;
            list.push_back(o);
        }
        m[0x7c] = uint8_t((c >> 20) % 3 != 0);
        break;
    }
    case 7: m[0xaa] = uint8_t(c % 2); break;
    case 8: m[0xab] = uint8_t(c % 2); break;
    case 9: m[0x124 + c % 6] = uint8_t((c >> 4) % 2); break;
    case 10: m[0x9c + c % 5] = uint8_t((c >> 4) % 2); break;
    case 11: *reinterpret_cast<uint32_t *>(m + 0x5c) = Pick(c); break;
    case 12: ref(0x94 + 4 * (c % 2), M(mRef0 + (c >> 2) % 4)); break;
    default: ref(c % 11 == 1 ? 0xd8 : 0xd8 + 4 * (c % 11), M(mRef0 + (c >> 4) % 4)); break; // +0xdc: case 3 (a window)
    }
}

// One toolbar slot with arguments drawn for it.
void RunToolbarSlot(Blob &b, uint8_t *m, unsigned slot, uint32_t a, uint32_t c)
{
    void *const face = m + 0x1c;
    uint32_t w[5] = {Pick(a), Pick(c), Pick(a ^ c), Pick(a >> 7), Pick(c >> 9)};
    const uint32_t boolA = (a & 1) | (a & 2 ? 0x100 : 0), boolC = (c & 1) | (c & 0x10 ? 0xff00 : 0);
    const bool view = gWorld.view;
    switch (slot) {
    case 10: w[0] = a % 5 ? P(&F(kW78)) : 0; break;
    case 12: // 0x620448 calls the +0x90 window unchecked
        if (!Word(m, 0x90)) return;
        w[0] = a % 4 ? P(&F(kCtrl)) : 0;
        break;
    case 77:
        if (!Word(m, 0x90)) return;
        break;
    case 80: // 0x622554 jumps through the controller
        if (!Word(m, 0x8c)) return;
        break;
    case 16: w[0] = a % 8 < 6 ? a % 8 : Pick(a); w[1] = boolC; w[2] = (c >> 3) & 1 ? 0x101 : 0x100; break;
    case 17: w[0] = boolA; w[1] = boolC; break;
    case 21: case 33: case 48: case 51: case 52: case 53: case 70: case 72: w[0] = boolA; break;
    case 40: case 43: // 0x61838f: the view is used unchecked
        if (!view) return;
        w[0] = boolA;
        break;
    case 41:
        if (!view) return;
        w[0] = a % 3 ? P(&M(mArg)) : 0;
        w[1] = boolC;
        break;
    case 49: w[1] = boolC; break;
    case 59: w[0] = kFloats[a % (sizeof(kFloats) / 4)]; break;
    case 60: {
        float out = -2.0f;
        b.push_back(Slot<uint32_t>(face, 60, a % 3 ? &out : nullptr) & 0xff);
        uint32_t bits;
        std::memcpy(&bits, &out, 4);
        b.push_back(bits);
        return;
    }
    case 63: w[0] = a % 5 == 4 ? Pick(a) : a % 5 - 1; break;
    case 67: w[0] = a % 10 - 1; break;
    case 68: w[0] = uint32_t(int32_t(a % 17) - 8); break;
    case 81: { // 0x61df34 calls the object's slot 83 unchecked when the service object D's slot 3 answers
        const bool via = (c & 1) != 0;
        w[0] = a % 4 || !(via || !gWorld.d3) ? P(&gBig) : 0;
        w[1] = via ? (c & 2 ? 0x101 : 1) : (c & 2 ? 0x100 : 0);
        break;
    }
    case 86: case 89: w[0] = a % 3 ? P(&M(mRef0 + c % 4)) : 0; break;
    default: break;
    }
    if (svcfx::gTrace) fprintf(stderr, "toolbar slot %u (%08x %08x %08x)\n", slot, w[0], w[1], w[2]);
    uint32_t r = CallWords(face, slot, kToolbarWords[slot], w);
    b.push_back(slot);
    if (slot == 14) r = r != 0; // the UCP state: a block of each side's heap (+0xe4 is compared the same way)
    switch (kResult[slot]) {
    case 'b': b.push_back(r & 0xff); break;
    case 'h': b.push_back(r & 0xffff); break;
    case 'w': svcfx::Field(b, r, kWordResultKinds, slot * 4); break; // by slot
    default: break; // void: eax is whatever the body left
    }
}

void RunOp(Blob &b, void *object, const Op &op)
{
    auto *const m = static_cast<uint8_t *>(object);
    const uint32_t a = op.arg, c = op.arg2;
    static const uint32_t kFaceOffsets[6] = {0, 4, 0x1c, 0x20, 0x24, 0x28};
    switch (op.kind) {
    case 0: // Init, on the service face or through the toolbar face's slot 3 (0x61e6d3)
        b.push_back(Slot<uint32_t>(a % 2 ? m : m + 0x1c, a % 2 ? 8 : 3) & 0xff);
        break;
    case 1: b.push_back(Slot<uint32_t>(m, 9) & 0xff); break; // Shutdown
    case 2: b.push_back(Slot<uint32_t>(m, 10, Pick(a)) & 0xff); break; // OnTick
    case 3: { // QueryInterface on a face; the reference given back
        void *face = m + kFaceOffsets[a % 6];
        void *out = reinterpret_cast<void *>(0x1234);
        const uint32_t r = Slot<uint32_t>(face, 0, kIids[(a >> 3) % (sizeof(kIids) / 4)], &out);
        b.push_back(r & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0x1234)); // left as set, or the face
        if ((r & 0xff) && out) b.push_back(Slot<uint32_t>(out, 2));
        break;
    }
    case 4: { // AddRef then Release on a face
        void *face = m + kFaceOffsets[a % 6];
        b.push_back(Slot<uint32_t>(face, 1));
        b.push_back(Slot<uint32_t>(m + kFaceOffsets[(a >> 3) % 6], 2));
        break;
    }
    case 5: { // the base's service slots (id, priority, running, tick)
        static const unsigned kBase[][3] = {{3, 0, ~0u}, {4, 1, 0}, {5, 0, ~0u}, {6, 0, 0xff}, {7, 1, 0}, {11, 1, 0xff}, {12, 0, ~0u}};
        const auto &s = kBase[a % 7];
        const uint32_t w[1] = {Pick(c)};
        b.push_back(CallWords(m, s[0], s[1], w) & s[2]); // the mask: a dword, al, or nothing (the setters)
        break;
    }
    case 6: { // the serializable face: Write, Read, GetGZCLSID
        void *face = m + 0x28;
        if (a % 3 == 0) b.push_back(Slot<uint32_t>(face, 3, &F(kOStream)) & 0xff);
        else if (a % 3 == 1) b.push_back(Slot<uint32_t>(face, 4, &F(kIStream)) & 0xff);
        else b.push_back(Slot<uint32_t>(face, 5));
        break;
    }
    case 7:
        if (a % 4 == 3) { // slot 71's hook (0x5bb9f1, called directly by cTSWinSceneGraph): the RVA's code on the face, then
                          // the adapter (on the original's side, the adapter route: an object with the game's vtables)
            b.push_back(gIsBuildMode(m + 0x1c, nullptr) & 0xff);
            b.push_back(cTSMainToolbarAdapters::IsBuildMode(reinterpret_cast<cITSMainToolbar *>(m + 0x1c), nullptr) & 0xff);
            break;
        }
        // the message faces (both still game code)
        b.push_back(Slot<uint32_t>(m + (a % 2 ? 0x20 : 0x24), 3, Pick(c)));
        break;
    case 8: Poke(m, a, c); break;
    default: { // a toolbar slot (3..93)
        const unsigned slot = 3 + a % 91;
        RunToolbarSlot(b, m, slot, c, op.arg3);
        break;
    }
    }
}

} // namespace toolbar_test

using namespace toolbar_test;

DIFF_TEST(MainToolbar_Run, 0x619068)
{
    // Also reached: every RVA in the file comment.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, 0x10f5198u, 0x10f519cu, 0x10f51b4u, 0x10cc2acu, 0x10cc458u,
                          svcfx::kAppCell, 0x10f51acu, svcfx::kPersistCell, 0x10dc598u})
        globals.SetWord(cell, *GameAddress<uint32_t>(cell));
    // The sound keys (the audio code fills them; the harness doesn't): fastest, fast, pause, play told apart.
    globals.SetWord(0x10de54c, 0x50000004);
    globals.SetWord(0x10de550, 0x50000003);
    globals.SetWord(0x10de554, 0x50000001);
    globals.SetWord(0x10de558, 0x50000002);

    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch getState(0x211da3, shape::CodeOf(&StandGetter<0x10cc458>));
    shape::CodePatch getClock(0x20df9a, shape::CodeOf(&StandGetter<0x10cc2ac>));
    shape::CodePatch getNotifier(0x9c5ab4, shape::CodeOf(&StandGetter<0x10f5198>));
    shape::StandIn getServer(0x9c5a89, &GetCachedMessageServer, &StandGetter<0x10f519c>);
    shape::CodePatch getSound(0x9c5b7e, shape::CodeOf(&StandGetter<0x10f51b4>));
    shape::CodePatch traceA(0x61ea5d, shape::CodeOf(&StandTraceA));
    shape::CodePatch traceB(0x61eb0d, shape::CodeOf(&StandTraceB));
    shape::CodePatch trace1(0x620af9, shape::CodeOf(&StandTrace<0x620af9>));
    shape::CodePatch trace2(0x620b15, shape::CodeOf(&StandTrace<0x620b15>));
    shape::CodePatch trace4(0x620b4d, shape::CodeOf(&StandTrace<0x620b4d>));
    shape::CodePatch trace5(0x620b69, shape::CodeOf(&StandTrace<0x620b69>));
    shape::CodePatch trace8(0x620bbd, shape::CodeOf(&StandTrace<0x620bbd>));
    shape::CodePatch entry(0xb39350, shape::CodeOf(&StandEntry));
    shape::StandIn tick(0xb42250, &ApplyScriptConditions, &StandTick);
    shape::CodePatch callback(0x61e12f, shape::CodeOf(&StandWindowCallback));
    shape::CodePatch ucp(0x671b29, shape::CodeOf(&StandUcp));
    using TSStringConstruct = cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint32_t);
    shape::StandIn tsString(0x1d0e90, static_cast<TSStringConstruct>(&cTSString::Construct), &StandTSString);
    shape::StandIn note(0x9c7b3d, &UtilObjectB::Construct, &StandNote);
    shape::CodePatch p1(0x61b843, RecordFor<0x61b843, 2>());
    shape::CodePatch p2(0x620643, RecordFor<0x620643, 0>());
    shape::CodePatch p3(0x61b61e, RecordFor<0x61b61e, 2>());
    shape::CodePatch p4(0x61b75f, RecordFor<0x61b75f, 2>());
    shape::CodePatch r25(0x621006, RecordFor<0x621006, 2>());
    shape::CodePatch r27(0x620c7e, RecordFor<0x620c7e, 1>());
    shape::CodePatch r29(0x621ba2, RecordFor<0x621ba2, 1>());
    shape::CodePatch r37(0x61b1d5, RecordFor<0x61b1d5, 1>());
    shape::CodePatch r38(0x61b36c, RecordFor<0x61b36c, 1>());
    shape::CodePatch r76(0x61fc26, RecordFor<0x61fc26, 3>());
    shape::CodePatch r82(0x61fed5, RecordFor<0x61fed5, 5>());
    shape::CodePatch m1(0x61b99c, RecordFor<0x61b99c, 1>());
    shape::CodePatch m2(0x61bc58, RecordFor<0x61bc58, 1>());

    // The forwarded slots are where the game's toolbar vtable has them.
    static const uint32_t kForwarded[][2] = {{25, 0x621006}, {27, 0x620c7e}, {29, 0x621ba2}, {37, 0x61b1d5},
                                             {38, 0x61b36c}, {76, 0x61fc26}, {82, 0x61fed5}};
    for (const auto &g : kForwarded) {
        ++ctx.cases;
        if (GameAddress<void *>(Sys::kToolbarVTable)[g[0]] != GameAddress<void>(g[1]))
            ctx.Fail("game toolbar slot %u is not %x", g[0], g[1]);
    }

    static const uint32_t kOffsets[6] = {0, 4, 0x1c, 0x20, 0x24, 0x28};
    const shape::OurTables<6> &tables = shape::Ours<Sys>(kOffsets);
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            svcfx::AddRange(gMore, sizeof(gMore), kMoreTag);
            svcfx::AddRange(&gView, sizeof(gView), kVTag);
            svcfx::AddRange(&gHolder, sizeof(gHolder), kVTag | 0x10000);
            svcfx::AddRange(&gBig, sizeof(gBig), kVTag | 0x20000);
            gRng.seed(seed * 1000 + 3);
            Blob first;
            using BuildModeFn = uint32_t(__fastcall *)(void *, void *);
            gIsBuildMode = side == 1 ? reinterpret_cast<BuildModeFn>(&cTSMainToolbarAdapters::IsBuildMode)
                                     : ctx.Original<BuildModeFn>(0x5bb9f1);
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Dump(first, object, side == 1, tables);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1, tables);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                for (const Obj &o : gMore) b.push_back(uint32_t(o.refs));
                b.push_back(uint32_t(gView.base.refs));
                b.push_back(uint32_t(gHolder.base.refs));
                b.push_back(*GameAddress<uint16_t>(0x10dc598));
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "MainToolbar_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (kind %u arg %08x)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op].kind : 99u, i && op < ops.size() ? ops[op].arg : 0u);
                seed = 3001;
                break;
            }
        }
    }
}
