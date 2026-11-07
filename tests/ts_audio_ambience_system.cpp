// Differential tests for cTSAudioAmbienceSystem (lane KF, src/audio/cTSAudioAmbienceSystem.h): the whole class in the
// standalone shape.
//
// AudioAmbienceSystem_Run: a system built by the original constructor (0x2229db, game vtables 0xda9db8 / 0xda9de4 /
// 0xda9df8 / 0xda9e30 / 0xda9e4c) or factory (0x22460b) against one built by ours (the Construct adapter / Create), each
// driven through its own vtables by the same random operations, then destroyed by the last Release (cRZSystemService's
// Release reaching the +0xc face's deleting destructor thunk 0x222d4e, the deleting destructor 0x222d56, the destructor
// 0x222c12; the cheat face's thunk 0x222d46 is called directly by the cheat-face op with flags 0).
// Operations: Init (0x224bd4, also through the service face's thunk 0x224cdb: LoadResource64 0x22515a, LoadNames 0x2250a2),
// Shutdown (0x2254bf, thunk 0x2256a5: SetActive 0x225457, ClearLayers 0x2244dc, ShutdownPrimitives 0x22530b), GetItems
// (0x224777), GetState (0x225280), LookupId (0x2248ac), GetPrimitive (0x22477f), GetScale (0x2249a2), OnTick (0x225212:
// Update 0x2256ad with the budget watch around its 500 cap, the tick seconds around the 50-second cap), DoMessage (0x224554:
// OnStop 0x224af6, OnLoadScene 0x224aae, OnLoadLot 0x224b1e, OnModeEnter 0x224a01, OnModeChange 0x224b65, OnReload 0x224bca,
// OnRegistryState 0x224a68 with SetState 0x2254a3, Reload 0x225352, ResetCursors 0x225425, LoadCurve 0x22544f), the cheat
// face (GetName 0x22520c, GetHelp 0x1d0a53, GetDescription 0x2244d6, the folded slots 3 / 4 / 7 / 8 / 11),
// QueryInterface (0x225284) / AddRef (0x2239ab) / Release (0x2252e3) on every face (the thunks 0x2252c3 / 0x2252cb /
// 0x2252db / 0x2252d3, 0x2239b3 / 0x2239bb / 0x2239cb / 0x2239c3, 0x2252eb / 0x2252f3 / 0x225303 / 0x2252fb) and the
// service accessors.
//
// Every object the system reaches is a recording fake (tests/ts_services_fixture.h); the callees that are other classes'
// code are stood in for on both sides: the message-id walks (0x1c0458 / 0x1c081e), the stopwatch's Start / Elapsed (0x1c982b
// / 0x1c9256), the log line (cRZStd's slot 2); on the image (original code on both sides): the scene's constructor
// 0x23e694, the layer builder 0x224ce3, the scene loaders 0x22418d / 0x22334d / 0x223cb4 / 0x2232e1 / 0x222d78, the curve's
// load 0xc0a86a and evaluation 0xc0a6a1, the stopwatch restart 0x1c95ec and the primitive registry 0x236660. Compared after
// every operation: the result, the event log, every field (the never-written bytes too: both blocks start as 0xc5), the
// layer list, the cursors as list positions, the primitives, the scale map (list order, buckets), and every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioAmbienceSystem.h"
#include "game/game.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersA.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace audioamb_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{3, "-----d"}, {7, "d----d"}, {8, "d----d"}, {9, "d----d"}, {11, "dd"}, {12, "dd"}, {13, "dd"}, {14, "---d"}, {15, "dd"}, {19, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSAudioAmbienceSystem;

constexpr uint32_t kTag = 0x7e000000; // the system under test

enum : unsigned {
    kService = 0, kComFw, kClock, kScene, kLayer0, kLayer1, kLayer2, kPrim0, kPrim1, kPrim2, kRM, kRes60, kRes64, kNames,
    kPersistRes, kMsg, kMode, kApp, kAppMode, kRegistry,
};

std::mt19937 gRng;
uint32_t Roll(uint32_t n) { return uint32_t(gRng() % n); }

// ---- the world for a seed ----
struct World {
    bool rm;           // the persist resource manager is there
    bool clock;        // the clock class object is there
    uint32_t resOk;    // GetResource succeeds (bit 0 for +0x64's key, bit 1 for the names)
    uint32_t layers;   // how many layers the builder adds (0..3)
    float clockTime;
    uint32_t tick;     // the tick watch's Elapsed
    uint32_t budget;   // the budget watch's next Elapsed (counts up)
};
World gWorld;
AmbienceItemList gItemsA, gItemsB; // the scene's slot 9 and slot 10 lists
uint8_t *gObject;                  // the system of the current side
cIGZUnknown *U(unsigned i) { return reinterpret_cast<cIGZUnknown *>(&F(i)); }

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
void __fastcall StandStart(cRZStopwatch *self, void *)
{
    Ev(0x1c982b);
    Ev(svcfx::Norm(P(self)));
    self->mStart = 1;
}
// The tick watch (+0xd0) answers the world's tick; any other watch the budget, counting up.
uint32_t __fastcall StandElapsed(const cRZStopwatch *self, void *)
{
    Ev(0x1c9256);
    Ev(svcfx::Norm(P(self)));
    if (P(self) == P(gObject + 0xd0)) return gWorld.tick;
    return gWorld.budget++;
}
void __fastcall StandRestart(cRZStopwatch *self, void *)
{
    Ev(0x1c95ec);
    Ev(svcfx::Norm(P(self)));
    self->mAccumulated = 7;
}

// The log line: cRZStd's slot 2 (cdecl: the object, channel, level, format, arguments: words).
void __cdecl LogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *format, ...)
{
    Ev(0x10600);
    Ev(self->mFirst - P(GameAddress<void>(0)));
    Ev(self->mSecond);
    Ev(channel);
    Ev(level);
    Ev(P(format) - P(GameAddress<void>(0)));
    va_list args;
    va_start(args, format);
    for (const char *f = format; *f; ++f)
        if (*f == '%' && f[1] && f[1] != '%') Ev(va_arg(args, uint32_t));
    va_end(args);
}
struct VtablePatch {
    void **slot;
    void *saved;
    VtablePatch(uint32_t vtable, unsigned index, void *stand) : slot(GameAddress<void *>(vtable + index * 4))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = stand;
    }
    ~VtablePatch() { *slot = saved; }
    VtablePatch(const VtablePatch &) = delete;
    VtablePatch &operator=(const VtablePatch &) = delete;
};

// The scene's constructor: the memory is freed, the fake stands for the scene.
void *__fastcall StandScene(void *memory, void *)
{
    Ev(0x23e694);
    ::operator delete(memory);
    return &F(kScene);
}

void LogText(const std::string &s)
{
    Ev(uint32_t(s.size()));
    for (char c : s) Ev(uint8_t(c));
}

// The layer builder: the world's layers onto the list, a few scales, the divisor, +0x60 and the two curve names.
void __fastcall StandLayers(uint8_t *self, void *)
{
    Ev(0x224ce3);
    auto &layers = *reinterpret_cast<AmbienceLayerList *>(self + 0x40);
    for (uint32_t i = 0; i < gWorld.layers; ++i) layers.push_back(AudioRef(U(kLayer0 + (i + Roll(3)) % 3)));
    auto &scales = *reinterpret_cast<std::unordered_map<uint32_t, uint32_t> *>(self + 0x114);
    for (int i = 0; i < 3; ++i) {
        static const uint32_t kValues[] = {0, 1, 3, 100, 0x7fffffff, 0x80000000, 0xffffffff, 12345};
        scales[1 + Roll(12)] = kValues[Roll(8)];
    }
    static const float kDivisors[] = {1.0f, 2.0f, 100.0f, 0.5f};
    *reinterpret_cast<float *>(self + 0x110) = kDivisors[Roll(4)];
    reinterpret_cast<AudioRef *>(self + 0x60)->Assign(Roll(3) ? U(kRes60) : nullptr);
    reinterpret_cast<cRZString *>(self + 0x88)->mString = Roll(2) ? "day" : "night";
    reinterpret_cast<cRZString *>(self + 0xa8)->mString = Roll(2) ? "lot" : "";
}
// The scene loaders: logged, sometimes a layer more, an answer on a roll.
bool __fastcall StandLoadA(uint8_t *self, void *)
{
    Ev(0x22418d);
    Ev(svcfx::Norm(P(self)));
    return Roll(5) != 0;
}
bool __fastcall StandLoadB(uint8_t *self, void *)
{
    Ev(0x22334d);
    if (Roll(3) == 0) reinterpret_cast<AmbienceLayerList *>(self + 0x40)->push_back(AudioRef(U(kLayer1)));
    return Roll(5) != 0;
}
bool __fastcall StandLoadLot(uint8_t *self, void *)
{
    Ev(0x223cb4);
    Ev(svcfx::Norm(P(self)));
    return Roll(4) != 0;
}
void __fastcall StandLoadC(uint8_t *self, void *)
{
    Ev(0x2232e1);
    Ev(svcfx::Norm(P(self)));
}
void __fastcall StandLoadD(uint8_t *self, void *)
{
    Ev(0x222d78);
    Ev(svcfx::Norm(P(self)));
}
void __fastcall StandCurveLoad(void *curve, void *, cRZString *name)
{
    Ev(0xc0a86a);
    Ev(svcfx::Norm(P(curve)));
    Ev(svcfx::Norm(P(name)));
    LogText(name->mString);
}
double __fastcall StandCurveEval(void *curve, void *, float x)
{
    Ev(0xc0a6a1);
    Ev(svcfx::Norm(P(curve)));
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    Ev(bits);
    return double(x) * 0.75 + 0.125;
}
void *__cdecl StandRegistry()
{
    Ev(0x236660);
    return &F(kRegistry);
}

// ---- fake handlers ----

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}

// The framework service's slot 5 GetSystemService(class, iid, out): the persist resource manager when the world has one.
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc && gWorld.rm) return HandOut(a[2], F(kRM));
    return 0;
}
// The COM framework's slot 3 GetClassObject(class, iid, out): the clock, and the primitives 0x100..0x102.
uint32_t GetClassObject(Obj *, uint32_t *a)
{
    if (a[0] == 0xac4da0f7) return gWorld.clock ? HandOut(a[2], F(kClock)) : 0x1200;
    if (a[0] >= 0x100 && a[0] <= 0x102) return HandOut(a[2], F(kPrim0 + a[0] - 0x100)) | 0x1200;
    return 0x1200;
}
// QueryInterface on the fakes: value[0] the iid answered, value[1] the fake handed out (1 + index; 0 = itself).
uint32_t Query(Obj *self, uint32_t *a)
{
    if (a[0] != self->value[0] && a[0] != 1) return 0x3300;
    Obj &out = self->value[1] ? F(self->value[1] - 1) : *self;
    return HandOut(a[1], out) | 0x3300;
}
// Resource manager slot 3 GetResource(key, iid, out, 0, 0): +0x64's key (bit 0) and the names' (bit 1) on the world's word.
uint32_t GetResource(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (key[2] == *GameAddress<uint32_t>(0x10cc540) && (gWorld.resOk & 1)) return HandOut(a[2], F(kRes64));
    if (key[2] == *GameAddress<uint32_t>(0x10cc548) && (gWorld.resOk & 2)) return HandOut(a[2], F(kNames));
    return 0x4400;
}
// Resource manager slot 10 ForgetResource(key).
uint32_t Forget(Obj *, uint32_t *a)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return 1;
}
// The persist face's slot 3: the key into `out` (16 bytes), answered as the pointer.
uint32_t GetKey(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = 0xebfee33f;
    out[1] = 0x4c1940c5;
    out[2] = 0x500 + self->value[3]++;
    out[3] = 0x77;
    difftest::FillOut(out, 16);
    return a[0];
}
std::string KeyText(uint32_t key) { return std::string(reinterpret_cast<const GZServiceString *>(uintptr_t(key))->c_str()); }
uint32_t TextHash(const std::string &s)
{
    uint32_t h = 17;
    for (char c : s) h = h * 31 + uint8_t(c);
    return h;
}
// The name table's slot 10 HasProperty(key) and slot 9 GetPropertyValue(key, any).
uint32_t HasName(Obj *, uint32_t *a)
{
    const std::string s = KeyText(a[0]);
    LogText(s);
    return (TextHash(s) % 3 ? 1u : 0u) | 0x5500;
}
uint32_t GetName(Obj *, uint32_t *a)
{
    const std::string s = KeyText(a[0]);
    LogText(s);
    if (TextHash(s) % 5 == 0) return 0x5500;
    reinterpret_cast<uint32_t *>(uintptr_t(a[1]))[1] = TextHash(s);
    return 0x5501;
}
// The primitive registry's slots 6 and 5: 12 bytes each (the third word a float).
uint32_t RegistryFill(Obj *self, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = 0x600 + self->value[0]++;
    out[1] = gRng();
    const float weight = float(Roll(1000)) / 10.0f;
    std::memcpy(&out[2], &weight, 4);
    difftest::FillOut(out, 12);
    return 0;
}
uint32_t PrimInit(Obj *self, uint32_t *) { return self->value[5] | 0x6600; }
uint32_t SceneInit(Obj *self, uint32_t *) { return self->value[5] | 0x6600; }
float __fastcall ClockTime(Obj *, void *)
{
    Ev(0xc10c);
    return gWorld.clockTime;
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    std::mt19937 rng(seed);
    gWorld.rm = rng() % 6 != 0;
    gWorld.clock = rng() % 8 != 0;
    gWorld.resOk = rng() % 5 ? 3 : rng() % 4;
    gWorld.layers = rng() % 4;
    gWorld.clockTime = float(rng() % 2000) / 8.0f;
    gWorld.tick = 0;
    gWorld.budget = 498;
    gItemsA.clear();
    gItemsB.clear();
    for (uint32_t i = 0, n = rng() % 3; i < n; ++i) gItemsA.push_back(reinterpret_cast<void *>(uintptr_t(0x9a00 + i)));
    for (uint32_t i = 0, n = rng() % 4; i < n; ++i)
        gItemsB.push_back(i == 1 ? static_cast<void *>(&F(kPrim2)) : reinterpret_cast<void *>(uintptr_t(0x9b00 + i)));

    Obj &service = F(kService);
    for (unsigned s : {3u, 4u, 9u, 10u}) service.Set(s, "p", 0x300 + s);
    service.Set(5, "ddp", 0, &GetSystemService);
    F(kComFw).Set(3, "ddp", 0, &GetClassObject);
    F(kClock).table[7] = reinterpret_cast<void *>(&ClockTime);
    Obj &scene = F(kScene);
    scene.Set(3, 0, 0, &SceneInit);
    scene.value[5] = rng() % 6 != 0;
    scene.Set(4, 0, 0x404);
    scene.Set(6, 0, 0x606);
    scene.Set(9, 0, &gItemsA);
    scene.Set(10, "pdd", &gItemsB);
    for (unsigned l = kLayer0; l <= kLayer2; ++l) {
        for (unsigned s = 4; s <= 9; ++s) F(l).Set(s, s == 4 || s == 8 ? 0 : 1, 0x700 + s);
        F(l).Data(5, 1), F(l).Ptr(6, 1), F(l).Data(7, 1), F(l).Data(9, 1);
    }
    for (unsigned p = kPrim0; p <= kPrim2; ++p) {
        F(p).Set(0, "dp", 0, &Query);
        F(p).value[0] = 0x2bcd0000 + p;
        F(p).Set(3, 0, 0, &PrimInit);
        F(p).value[5] = rng() % 5 != 0;
        F(p).Set(4, 0, 0x804);
        F(p).Set(5, 0, 0x100 + p - kPrim0);
        F(p).Set(6, "d", 0x806);
    }
    F(kRM).Set(3, "pdp--", 0, &GetResource);
    F(kRM).Set(10, "p", 0, &Forget);
    for (unsigned r : {kRes60, kRes64, kNames}) {
        F(r).Set(0, "dp", 0, &Query);
        F(r).value[0] = rng() % 4 ? 0x456b8f1du : 0x1234u;
        F(r).value[1] = 1 + kPersistRes;
    }
    F(kPersistRes).Set(3, "p", 0, &GetKey);
    F(kNames).Set(10, "p", 0, &HasName);
    F(kNames).Set(9, "pp", 0, &GetName);
    F(kMsg).Set(0, "dp", 0, &Query);
    F(kMsg).value[0] = 0x65297976;
    F(kMsg).value[1] = 1 + kMode;
    F(kMsg).Set(4, 0, 0u);
    F(kMode).Set(9, 0, 0u);
    F(kApp).Set(23, 0, rng() % 4 ? &F(kAppMode) : nullptr);
    F(kAppMode).Set(5, 0, rng() % 3 ? 0x0bea28c2u : 0x1234u);
    F(kRegistry).Set(5, "p", 0, &RegistryFill);
    F(kRegistry).Set(6, "p", 0, &RegistryFill);

    *GameAddress<void *>(0x10c9378) = &F(kService);
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? &F(kService) : nullptr;
    *GameAddress<void *>(svcfx::kAppCell) = rng() % 5 ? &F(kApp) : nullptr;
    for (uint32_t cell : {0x10cc540u, 0x10cc544u, 0x10cc548u, 0x10cc54cu}) *GameAddress<uint32_t>(cell) = 0x1000 + rng() % 4;
    if (*GameAddress<uint32_t>(0x10cc548) == *GameAddress<uint32_t>(0x10cc540)) *GameAddress<uint32_t>(0x10cc548) ^= 0x10;
    svcfx::gFrameworkIndex = kComFw;
}

// ---- driving a system ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)
bool gStale;   // shut down with the layer cursor inside the list (see RunOp)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) return ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x22460b)();
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    if (ours) cTSAudioAmbienceSystemAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x2229db)(memory);
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
// The scale map: its words, its list (key, value), the buckets as list indices.
void DumpHash(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0));
    b.push_back(Word(at, 8));
    b.push_back(Word(at, 0x18));
    b.push_back(Word(at, 0x1c));
    std::vector<uint32_t> order;
    const uint32_t head = Word(at, 4);
    for (uint32_t n = *reinterpret_cast<const uint32_t *>(uintptr_t(head)); n != head;
         n = *reinterpret_cast<const uint32_t *>(uintptr_t(n))) {
        order.push_back(n);
        const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(n));
        b.push_back(w[2]);
        b.push_back(w[3]);
    }
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
// A list iterator as (list, position): 1 the layers, 2 / 3 the scene's lists; the end as the size; 0 for none.
template <class L> uint32_t Position(uint32_t node, const L &list, uint32_t id)
{
    uint32_t index = 0;
    for (auto it = list.begin();; ++it, ++index) {
        if (P(*reinterpret_cast<void *const *>(&it)) == node) return id << 16 | index;
        if (it == list.end()) return 0;
    }
}
uint32_t Cursor(uint32_t node, const AmbienceLayerList &layers)
{
    if (!node) return 0;
    if (uint32_t at = Position(node, layers, 1)) return at;
    if (uint32_t at = Position(node, gItemsA, 2)) return at;
    if (uint32_t at = Position(node, gItemsB, 3)) return at;
    return 0xdddddddd;
}
void DumpWatch(Blob &b, const uint8_t *at)
{
    b.push_back(Word(at, 0));
    b.push_back(gFactory ? 0 : Word(at, 4)); // never written: the allocator's bytes from a factory
    for (uint32_t off = 8; off < 0x18; off += 4) b.push_back(Word(at, off));
}

void Dump(Blob &b, void *object, bool ours)
{
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &sys = *static_cast<const Sys *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[5][2] = {{0, 0xda9db8}, {4, 0xda9de4}, {8, 0xda9df8}, {0xc, 0xda9e30}, {0x24, 0xda9e4c}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(ours ? !game : game);
    }
    for (uint32_t off = 0x10; off < 0x20; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x20] : Word(m, 0x20)); // the running flag; its padding is the allocator's from a factory
    b.push_back(Word(m, 0x28));
    b.push_back(Word(m, 0x2c));
    b.push_back(gFactory ? m[0x30] : Word(m, 0x30));
    b.push_back(uint32_t(sys.mMessageIds.size()));
    for (uint32_t id : sys.mMessageIds) b.push_back(id);
    b.push_back(svcfx::Norm(Word(m, 0x3c)));
    b.push_back(uint32_t(sys.mLayers.size()));
    for (const AudioRef &layer : sys.mLayers) b.push_back(svcfx::Norm(P(layer.Get())));
    for (uint32_t off = 0x48; off < 0x68; off += 4) b.push_back(off >= 0x60 ? svcfx::Norm(Word(m, off)) : Word(m, off));
    b.push_back(uint32_t(sys.mPrimitives.size()));
    b.push_back(uint32_t(sys.mPrimitives.capacity()));
    for (const AudioRef &p : sys.mPrimitives) b.push_back(svcfx::Norm(P(p.Get())));
    b.push_back(svcfx::Norm(Word(m, 0x74)));
    b.push_back(Word(m, 0x7c) - Word(m, 0x78));
    b.push_back(Word(m, 0x80) - Word(m, 0x78));
    b.push_back(Word(m, 0x84));
    DumpString(b, m + 0x88);
    DumpString(b, m + 0xa8);
    b.push_back(svcfx::Norm(Word(m, 0xc8)));
    b.push_back(gFactory ? 0 : Word(m, 0xcc));
    DumpWatch(b, m + 0xd0);
    DumpWatch(b, m + 0xe8);
    for (uint32_t off = 0x100; off < 0x110; off += 4) b.push_back(Cursor(Word(m, off), sys.mLayers));
    b.push_back(Word(m, 0x110));
    DumpHash(b, m + 0x114);
    b.push_back(gFactory ? 0 : Word(m, 0x134));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 30;
    if (rng() % 6) ops.push_back({0, rng() | 1, rng()});
    for (unsigned i = 0; i < n; ++i) {
        unsigned kind = rng() % 14;
        if (kind == 13 && rng() % 2) kind = 7; // more ticks and messages
        if (kind == 12 && rng() % 2) kind = 8;
        ops.push_back({kind, rng(), rng()});
    }
    return ops;
}

// Whether the scene-driven paths are safe: the original calls the scene, the clock and the layers unchecked (and the
// update walk dereferences the layer list's head when there are no layers, see Update's ORIGINAL-BUG), so the run leaves
// them out without a scene, a clock or layers.
bool Safe(void *object)
{
    const auto &sys = *static_cast<const Sys *>(object);
    return sys.mScene && sys.mClock && !sys.mLayers.empty();
}

const uint32_t kMessages[] = {0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb9, 0xb1, 0xba, 0x0b8d81d5, 0x4c3220e7, 0xedbeead0, 0xb2b28373, 0x1234};
const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, 0x0c1e612a, 0x12345678, 0xaada39c7};
const uint32_t kTicks[] = {0, 999, 1000, 1999, 25000, 49999, 50000, 50999, 51000, 2000};
const char *const kNamesText[] = {"Wind", "RAIN", "12", "0x1f", "birds", "", "Crickets", "077"};

void RunOp(Blob &b, void *object, const Op &op)
{
    auto &sys = *static_cast<Sys *>(object);
    void *const primary = object;
    void *const target = Face(object, 4);
    void *const service = Face(object, 8);
    void *const unknown = Face(object, 0xc);
    void *const cheat = Face(object, 0x24);
    const uint32_t a = op.arg, c = op.arg2;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    switch (op.kind) {
    case 0:
        // Shutdown leaves the layer cursor on a freed node unless it was at the end; a later Init and update would walk
        // it (a use-after-free of the original, only reached by re-initialising a shut-down service): left out.
        if (gStale) return;
        b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff);
        break;
    case 1:
        if (Cursor(P(*reinterpret_cast<void *const *>(&sys.mLayerCursor)), sys.mLayers) >> 16 == 1 &&
            sys.mLayerCursor != sys.mLayers.end())
            gStale = true;
        b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff);
        break;
    case 2:
        if (!sys.mScene) return;
        b.push_back(svcfx::Norm(Slot<uint32_t>(primary, 5)));
        break;
    case 3: b.push_back(Slot<uint32_t>(primary, 6) - P(object)); break;
    case 4: { // LookupId
        if (!sys.mNames) return; // the original calls the name table unchecked
        cRZString name;
        name.Construct(kNamesText[a % 8]);
        b.push_back(Slot<uint32_t>(primary, 7, &name));
        name.Destruct();
        break;
    }
    case 5: { // GetPrimitive
        const uint32_t id = 0xff + a % 5;
        const uint32_t iid = (c & 1) ? 1u : 0x2bcd0000u + kPrim0 + (c >> 1) % 4;
        b.push_back(Slot<uint32_t>(primary, 8, id, iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out)));
        break;
    }
    case 6: { // GetScale (st0)
        const float v = Slot<float>(primary, 9, a % 3 ? 1 + a % 13 : 0u);
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        b.push_back(bits);
        break;
    }
    case 7: { // OnTick, a few times
        const unsigned times = 1 + a % 3;
        for (unsigned t = 0; t < times; ++t) {
            if (sys.mActive && !Safe(object)) return;
            gWorld.tick = kTicks[gRng() % 10];
            gWorld.budget = 489 + gRng() % 14;
            b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        }
        break;
    }
    case 8: { // DoMessage
        const uint32_t type = kMessages[a % 13];
        // The scene loads need only the scene (no update runs in them): they also run with no layers (the builder may add
        // none), where the system must come out inactive.
        const bool load = type == 0xb3 || type == 0xb6;
        if (type != 0xb2 && type != 0x0b8d81d5 && type != 0x1234 && type != 0xb1 && type != 0xba && type != 0xb5 &&
            !(load ? sys.mScene && sys.mClock : Safe(object)))
            return;
        F(kMsg).ret[4] = type;
        F(kMsg).value[0] = c % 5 ? 0x65297976u : 0x1u;
        F(kMode).ret[9] = (c >> 4) % 4;
        gWorld.budget = 489 + gRng() % 14;
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff);
        break;
    }
    case 9: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, target, service, unknown, cheat};
        void *q = out;
        const uint32_t ok = Slot<uint32_t>(faces[a % 5], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 5], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 5], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 10: // the cIGZSystemService accessors and cRZUnknownMT's
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<uint32_t>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(unknown, 5));
        break;
    case 11: // the cheat face
        Slot<uint32_t>(cheat, 3, a, c);
        Slot<uint32_t>(cheat, 4, c);
        b.push_back(Slot<uint32_t>(cheat, 5) - P(GameAddress<void>(0)));
        b.push_back(Slot<uint32_t>(cheat, 6, a) - P(GameAddress<void>(0)));
        b.push_back(Slot<uint32_t>(cheat, 7) & 0xff);
        b.push_back(Slot<uint32_t>(cheat, 8));
        b.push_back(Slot<uint32_t>(cheat, 10) - P(GameAddress<void>(0)));
        b.push_back(Slot<uint32_t>(cheat, 11));
        break;
    case 12: // the world changes
        gWorld.rm = a % 4 != 0;
        gWorld.clock = (a >> 4) % 5 != 0;
        gWorld.resOk = (a >> 8) % 4;
        F(kScene).value[5] = (a >> 12) % 4 != 0;
        F(kPrim0 + (a >> 16) % 3).value[5] = (a >> 18) % 2;
        break;
    default: // the stand-alone non-virtual paths through the message handlers' types
        if (!Safe(object)) return;
        F(kMsg).ret[4] = a % 2 ? 0xb2b28373u : 0xb9u;
        gWorld.budget = a % 3 ? 499 : 500;
        b.push_back(Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff);
        break;
    }
}

} // namespace audioamb_test

using namespace audioamb_test;

DIFF_TEST(AudioAmbienceSystem_Run, 0x2229db)
{
    // Also reached (see the file comment): 0x22460b (factory), 0x222c12, 0x222d56, 0x222d4e, 0x222d46, 0x225284, 0x2239ab,
    // 0x2252e3, 0x224bd4, 0x224cdb, 0x2254bf, 0x2256a5, 0x224777, 0x2248ac, 0x22477f, 0x2249a2, 0x225212, 0x224554,
    // 0x22520c, 0x1d0a53, 0x2244d6, 0x2256ad, 0x225457, 0x225425, 0x225352, 0x2244dc, 0x22530b, 0x22515a, 0x2250a2,
    // 0x2254a3, 0x22544f, 0x224af6, 0x224aae, 0x224b1e, 0x224a01, 0x224b65, 0x224bca, 0x224a68, the adjustor thunks
    // 0x2252c3, 0x2252cb, 0x2252db, 0x2252d3, 0x2239b3, 0x2239bb, 0x2239cb, 0x2239c3, 0x2252eb, 0x2252f3, 0x225303,
    // 0x2252fb.
    char traceFlag[4];
    svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    for (uint32_t cell : {0x10c9378u, 0x10f5194u, svcfx::kAppCell, 0x10cc540u, 0x10cc544u, 0x10cc548u, 0x10cc54cu})
        globals.SetWord(cell, 0);
    globals.SetWord(0xff5f10, 0x10f5);
    globals.SetWord(0xff5f14, 0x10f6);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch scene(0x23e694, shape::CodeOf(&StandScene));
    shape::CodePatch layers(0x224ce3, shape::CodeOf(&StandLayers));
    shape::CodePatch loadA(0x22418d, shape::CodeOf(&StandLoadA));
    shape::CodePatch loadB(0x22334d, shape::CodeOf(&StandLoadB));
    shape::CodePatch loadLot(0x223cb4, shape::CodeOf(&StandLoadLot));
    shape::CodePatch loadC(0x2232e1, shape::CodeOf(&StandLoadC));
    shape::CodePatch loadD(0x222d78, shape::CodeOf(&StandLoadD));
    shape::CodePatch curveLoad(0xc0a86a, shape::CodeOf(&StandCurveLoad));
    shape::CodePatch curveEval(0xc0a6a1, shape::CodeOf(&StandCurveEval));
    shape::CodePatch restart(0x1c95ec, shape::CodeOf(&StandRestart));
    shape::CodePatch registry(0x236660, shape::CodeOf(&StandRegistry));
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn start(0x1c982b, &cRZStopwatch::Start, &StandStart);
    shape::StandIn elapsed(0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed);
    VtablePatch logLine(cRZStd::kVTable, 2, reinterpret_cast<void *>(&LogLine));

    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 7 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            void *object = Build(ctx, side == 1);
            gObject = static_cast<uint8_t *>(object);
            gStale = false;
            svcfx::AddRange(object, Sys::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1);
                } else if (seed % 5 == 0) {
                    // The cheat face's deleting destructor (thunk 0x222d46) with flags 0: destroyed, not freed.
                    Slot<uint32_t>(Face(object, 0x24), 9, 0u);
                    b.push_back(Word(static_cast<uint8_t *>(object), 0x10));
                    ::operator delete(object);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "AudioAmbienceSystem_Run", seed)) {
                if (svcfx::gTrace)
                    for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                        fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                                w < results[1][i].size() ? results[1][i][w] : 0);
                ctx.Fail("  at op %u of %u (kind %u)", unsigned(i), unsigned(ops.size()), i < ops.size() ? ops[i].kind : 99u);
                return;
            }
        }
    }
}
