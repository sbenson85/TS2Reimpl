// Differential tests for the director overrides of handoff D2 (lane ET, src/rz/COMDirectors.cpp): the OnStart of
// TSPersist (0x1d3e20), TSSG (0x2454c0), TSSim (0x416370), TSAppFW (0xc2beec), TSCommon (0xcb5110) and TSCAS (0xcb54a0),
// the engine starter 0x14107c, and the hooks-face overrides of cGZSound (0xb224d9 PostAppInit, 0xb2252e PreAppShutdown)
// and cHitMan (0xb2a4c3 PreAppInit, 0xb2a568 PreAppShutdown); lane FB added TSUI OnStart (0x5b72e7) and PostAppInit
// (0x5b763c), with the pieces of the world only they use (gOptionsCase, gSettingsCase).
//
// Method. A fake world in which every object the code calls through a vtable is a fake whose slots log the call (slot,
// the object's identity, the arguments with known objects mapped to identities and stack addresses to one marker):
//  - the framework's COM objects (the argument, id 1, and what the director's slot 12 returns, id 2): slot 3 creates,
//    returning an object and/or true as the scenario says;
//  - the service object in the image's global 0x10c9378 (id 3): slot 3 AddSystemService, slot 5 GetSystemService (gives a
//    fresh object, or none where the scenario allows), slot 7, slot 11;
//  - created objects (ids 0x400+): slot 0 QueryInterface (gives a fresh object), AddRef, Release and the slots the
//    overrides call;
//  - the directors started through their getters: their statics are fake objects whose slot 4 is logged (the guards
//    marked built, so the original's getters and ours return the static); the root director (0x10c9390, for
//    GetDirectorFrameworkObject) likewise, its slot 12 returning COM object 2;
//  - the game classes constructed by original code (0x1d8b90, 0x246110, 0x416810, 0x9704bb; ours of 0x1d8b90, 0x246110, 0x416810 and
//    0x9704bb through their adapters, cTSPersistSystemAdapters::Construct, nTSSG::cTSSGSystemAdapters::Construct,
//    cGZRealtimeDebugDisplayAdapters::Construct, cTSSimSystemAdapters::Construct): their constructors are
//    patched to a fake that logs the allocation size (_msize) and installs fake vtables at +0, +4 and +0xc. The image's
//    operator new / sized delete (0x824632 / 0x824624) are patched to malloc / free, as ours are.
// Three routes per scenario: the original on a director built by the original's base constructor, ours through our
// class's own vtable slot, and the hook's adapter on a director whose vtable has the slot trapped (it must not dispatch).
// The logs, the results and the exception that escapes (if any) must be equal. Scenarios are random outcomes of every
// creation and query; for the first few the k-th loggable call (any but AddRef / Release, which the originals release in
// noexcept regions) throws a C++ exception, for every k: the references held must be released the same way.
// Constraints: where the original dereferences what it got without a test (QueryInterface after a successful creation,
// HitMan's registry and target services, Sound's slot 43 object), the scenarios always give an object.
#include "rz/RZMessageServer.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cGZRealtimeDebugDisplay.h"
#include "rz/COMDirectors.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/RZMemory.h"
#include "sg/cTSSGSystem.h"
#include "ts/cTSPersistSystem.h"
#include "ts/cTSSimSystem.h"

#include <intrin.h>
#include <malloc.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <string>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace {

using namespace comdir;
using Base = rz::cRZCOMDllDirector;

uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---------------------------------------------------------------------------------------------
// Identities and the log

std::vector<uint32_t> gLog;
struct Known {
    const void *p;
    uint32_t id;
};
std::vector<Known> gKnown;
void Know(const void *p, uint32_t id) { gKnown.push_back({p, id}); }

uint32_t IdOf(uint32_t v)
{
    if (!v) return 0;
    for (const Known &k : gKnown)
        if (U(k.p) == v) return k.id;
    auto *tib = reinterpret_cast<NT_TIB *>(NtCurrentTeb());
    if (v >= U(tib->StackLimit) && v < U(tib->StackBase)) return 0x5555; // an out parameter in a frame
    return v;                                                            // a plain value (ids, flags)
}

// ---------------------------------------------------------------------------------------------
// The fakes

struct FakeObject {
    void **vtable;
    uint32_t pad[7];
};

enum Kind { kCom, kService, kObject, kSubDirector, kRoot, kUnderTest, kMessage, kOther };

// TSUI PostAppInit (lane FB): the standard message lives in memory from TestMalloc and carries the image's vtable
// (0xda4064), whose slots 1, 2, 5 and 11 are patched to the recorders while that case runs; the object's slot 9 (the
// settings read) takes two arguments and fills the AnyBoolean.
void **gMessageVt = nullptr;
bool gSettingsCase = false;

// TSUI OnStart (lane FB): the service object's slot 21 gives an options object (a created object) whose slot 8 reads
// "XBreak" into a cRZString (found or not, a value from kOptionValues); the director's class map holds the tool classes
// OnStart may erase (and two others), and its keys are logged after the call.
bool gOptionsCase = false;
int gOptionReads = 0;
const char *const kOptionValues[] = {"7", "1,7", "8", "3,8,7", ",,7,", "07", "7x", " 7", "x", "", "8,7", "70", "-7", "2,3,4,5,6,7,8,9,10,11,12,13"};
constexpr uint32_t kMapClasses[] = {0x6bb9232a, 0x27d98e43, 0xa9e3b32b, 0x29638d09, 0x4be71e86, 0x11111111, 0x7d0e0f00};

void *gComVt[64], *gServiceVt[64], *gObjectVt[64], *gSubVt[64], *gRootVt[64], *gServerVt[64];
FakeObject gComArg{}, gComThis{}, gService{}, gRoot{}, gServer{};
FakeObject gPool[64];
int gPoolUsed = 0;
uint8_t *gUnderTest = nullptr;

// The scenario.
uint64_t gCreateOut = 0, gCreateRet = 0, gQueryNull = 0, gQiNull = 0;
int gCreates = 0, gQueries = 0, gQis = 0, gCalls = 0, gThrowAt = -1;

Kind KindOf(void *self)
{
    if (self == gUnderTest) return kUnderTest;
    void **vt = *static_cast<void ***>(self);
    if (vt == gComVt) return kCom;
    if (vt == gServiceVt) return kService;
    if (vt == gObjectVt) return kObject;
    if (vt == gSubVt) return kSubDirector;
    if (vt == gRootVt) return kRoot;
    if (gMessageVt && vt == gMessageVt) return kMessage;
    return kOther;
}

void *NewPoolObject()
{
    FakeObject *o = &gPool[gPoolUsed++ & 63];
    o->vtable = gObjectVt;
    return o;
}

uint32_t Behave(void *self, int slot, int arity, const uint32_t *args)
{
    Kind kind = KindOf(self);
    gLog.push_back(0x100 + slot);
    gLog.push_back(IdOf(U(self)));
    for (int i = 0; i < arity; ++i) gLog.push_back(IdOf(args[i]));
    bool refCount = (kind == kObject || kind == kMessage) && (slot == 1 || slot == 2);
    if (kind == kMessage && slot == 1 && reinterpret_cast<uint32_t *>(self)[1] == 0)
        for (int w = 2; w < 11; ++w) gLog.push_back(reinterpret_cast<uint32_t *>(self)[w]); // as constructed
    if (!refCount && gCalls++ == gThrowAt) throw 9;
    switch (kind) {
    case kMessage: {
        uint32_t &count = reinterpret_cast<uint32_t *>(self)[1];
        if (slot == 1) return ++count;
        if (slot == 2) {
            if (--count == 0) {
                gLog.push_back(0xF7EE);
                free(self);
            }
            return count;
        }
        break;
    }
    case kCom:
        if (slot == 3) {
            int k = gCreates++;
            if ((gCreateOut >> k) & 1) *reinterpret_cast<void **>(args[2]) = NewPoolObject();
            return (gCreateRet >> k) & 1;
        }
        break;
    case kService:
        if (slot == 21 && gOptionsCase) return U(NewPoolObject());
        if (slot == 5) {
            int k = gQueries++;
            if (!((gQueryNull >> k) & 1)) *reinterpret_cast<void **>(args[2]) = NewPoolObject();
            return 1;
        }
        break;
    case kObject:
        if (slot == 0) {
            int k = gQis++;
            if ((gQiNull >> k) & 1) return 0;
            *reinterpret_cast<void **>(args[1]) = NewPoolObject();
            return 1;
        }
        if (slot == 43) return U(NewPoolObject());
        if (slot == 8 && gOptionsCase) {
            // The key (a cRZString: vtable, std::string at +4, count at +0x1c), then the out string filled.
            auto *key = reinterpret_cast<const uint32_t *>(args[0]);
            gLog.push_back(key[0]);
            gLog.push_back(key[7]);
            const std::string &text = *reinterpret_cast<const std::string *>(key + 1);
            gLog.push_back(static_cast<uint32_t>(text.size()));
            for (char c : text) gLog.push_back(static_cast<uint8_t>(c));
            auto *out = reinterpret_cast<uint32_t *>(args[1]);
            gLog.push_back(out[0]);
            int k = gOptionReads++;
            const size_t count = sizeof(kOptionValues) / sizeof(kOptionValues[0]);
            reinterpret_cast<std::string *>(out + 1)->assign(kOptionValues[(gCreateRet >> (8 * k + 16)) % count]);
            return ((gCreateOut >> (k + 20)) & 1) ? 0x101 : 0x100;
        }
        if (slot == 9 && gSettingsCase) {
            // The name (a std::string-shaped service string: text at +0 or through it, size at +0x10, capacity at +0x14).
            auto *s = reinterpret_cast<const uint32_t *>(args[0]);
            const char *text = s[5] > 15 ? reinterpret_cast<const char *>(s[0]) : reinterpret_cast<const char *>(s);
            gLog.push_back(s[4]);
            for (uint32_t i = 0; i < s[4]; ++i) gLog.push_back(static_cast<uint8_t>(text[i]));
            gLog.push_back(reinterpret_cast<uint8_t *>(args[1])[4]);
            reinterpret_cast<uint8_t *>(args[1])[4] = static_cast<uint8_t>(gCreateRet >> 8);
            gLog.push_back(reinterpret_cast<uint32_t *>(args[1])[0]); // the AnyBoolean's vtable (an image address)
        }
        break;
    case kSubDirector:
        if (slot == 4) return 1;
        break;
    case kRoot:
    case kUnderTest:
        if (slot == 12) return U(&gComThis);
        break;
    default: break;
    }
    return 0;
}

template <int S> uint32_t __fastcall Rec0(void *self, void *) { return Behave(self, S, 0, nullptr); }
template <int S> uint32_t __fastcall Rec1(void *self, void *, uint32_t a)
{
    uint32_t v[] = {a};
    return Behave(self, S, 1, v);
}
template <int S> uint32_t __fastcall Rec2(void *self, void *, uint32_t a, uint32_t b)
{
    uint32_t v[] = {a, b};
    return Behave(self, S, 2, v);
}
template <int S> uint32_t __fastcall Rec3(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t v[] = {a, b, c};
    return Behave(self, S, 3, v);
}

template <size_t... S> std::array<std::array<void *, 64>, 4> MakeRecs(std::index_sequence<S...>)
{
    return {{{reinterpret_cast<void *>(&Rec0<S>)...},
             {reinterpret_cast<void *>(&Rec1<S>)...},
             {reinterpret_cast<void *>(&Rec2<S>)...},
             {reinterpret_cast<void *>(&Rec3<S>)...}}};
}
const std::array<std::array<void *, 64>, 4> kRecs = MakeRecs(std::make_index_sequence<64>{});

// A vtable whose slots take the stack-argument counts given (slot, arity) and none elsewhere.
void BuildVt(void **vt, std::initializer_list<std::pair<int, int>> arities)
{
    for (int s = 0; s < 64; ++s) vt[s] = kRecs[0][s];
    for (auto [slot, arity] : arities) vt[slot] = kRecs[arity][slot];
}

void BuildWorld(bool objectSlot3TakesId)
{
    BuildVt(gComVt, {{3, 3}});
    BuildVt(gServiceVt, {{3, 1}, {5, 3}, {7, 1}, {11, 1}});
    BuildVt(gObjectVt, {{0, 2}, {3, objectSlot3TakesId ? 1 : 0}, {15, 1}, {20, 3}, {24, 1}});
    if (gSettingsCase) gObjectVt[9] = kRecs[2][9];
    if (gOptionsCase) gObjectVt[8] = kRecs[3][8];
    BuildVt(gSubVt, {{4, 1}});
    BuildVt(gRootVt, {});
    BuildVt(gServerVt, {{4, 2}});
    gServer.vtable = gServerVt;
    gComArg.vtable = gComThis.vtable = gComVt;
    gService.vtable = gServiceVt;
    gRoot.vtable = gRootVt;
    for (FakeObject &o : gPool) o.vtable = gObjectVt;
}

// ---------------------------------------------------------------------------------------------
// Patches of the image

struct Jump {
    uint8_t *site;
    uint8_t saved[5];
    Jump(void *at, const void *to) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(U(to) - (U(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    Jump(const Jump &) = delete;
    Jump &operator=(const Jump &) = delete;
    ~Jump() { std::memcpy(site, saved, 5); }
};

void *__cdecl ImageNew(size_t size) { return malloc(size); }
void __cdecl ImageSizedDelete(void *p, size_t) { free(p); }

std::vector<void *> gConstructed;
void *gThrownMemory = nullptr;
// Records the frees from the throw to the end of the route: whether the frame freed the memory is then exact.
std::optional<difftest::FreeRecorder> gThrowRecorder;
int gCtorCount = 0;
void *__fastcall FakeCtor(void *memory, void *)
{
    gLog.push_back(0xC7);
    gLog.push_back(static_cast<uint32_t>(_msize(memory)));
    if (gCalls++ == gThrowAt) {
        gThrownMemory = memory; // the frame's to free (checked after the route)
        gThrowRecorder.emplace();
        throw 10;
    }
    auto *p = static_cast<uint8_t *>(memory);
    uint32_t id = 0x300 + 0x10 * gCtorCount++;
    for (uint32_t offset : {0u, 4u, 0xcu}) {
        *reinterpret_cast<void **>(p + offset) = gObjectVt;
        Know(p + offset, id + offset);
    }
    gConstructed.push_back(memory);
    return memory;
}

// The thread's TLS copy of the statics epoch (com_directors.cpp): -1, so a guard marked built stays on the fast path.
struct EpochScope {
    int *tls;
    int saved;
    explicit EpochScope(difftest::Context &ctx)
    {
        uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        tls = array[index];
        saved = tls[1];
        tls[1] = -1;
    }
    ~EpochScope() { tls[1] = saved; }
};

// A static replaced by a fake director for the test (bytes and guard restored after).
struct FakeStatic {
    uint8_t *object;
    int *guard;
    uint8_t saved[0x64];
    int savedGuard = 0;
    FakeStatic(difftest::Context &ctx, uint32_t objectRva, uint32_t guardRva, void **vt)
        : object(ctx.image.At<uint8_t>(objectRva)), guard(guardRva ? ctx.image.At<int>(guardRva) : nullptr)
    {
        std::memcpy(saved, object, sizeof(saved));
        if (guard) savedGuard = *guard;
        std::memset(object, 0, sizeof(saved));
        *reinterpret_cast<void **>(object) = vt;
        if (guard) *guard = INT_MIN + 1;
    }
    FakeStatic(const FakeStatic &) = delete;
    FakeStatic &operator=(const FakeStatic &) = delete;
    ~FakeStatic()
    {
        std::memcpy(object, saved, sizeof(saved));
        if (guard) *guard = savedGuard;
    }
};

// ---------------------------------------------------------------------------------------------
// The directors under test

// Our director of class `index`, built without its steps.
template <size_t... I> std::array<Base *(*)(void *), kDirectorCount> MakeOursTable(std::index_sequence<I...>)
{
    return {{[](void *p) -> Base * { return ::new (p) rz::cCOMDirectorT<I>(rz::NoSteps{}); }...}};
}
const auto kOurs = MakeOursTable(std::make_index_sequence<kDirectorCount>{});

uint8_t *NewBlock()
{
    auto *p = static_cast<uint8_t *>(::operator new(0x64));
    std::memset(p, 0x5a, 0x64);
    return p;
}
void FreeDirector(uint8_t *p)
{
    reinterpret_cast<Base *>(p)->Base::~cRZCOMDllDirector();
    ::operator delete(p);
}

bool gTrapHit = false;
uint32_t __fastcall Trap0(void *, void *) { gTrapHit = true; return 0x7a7a; }
uint32_t __fastcall Trap1(void *, void *, void *) { gTrapHit = true; return 0x7a7a; }

enum class What { OnStart, Hook, Free };

struct Case {
    const char *name;
    uint32_t rva;
    What what;
    unsigned index;   // the director
    int hookSlot;     // slot of the +4 face for What::Hook
    void *adapter;    // the hook's adapter (__fastcall)
    std::vector<unsigned> subs; // statics faked as sub-directors
    bool qi;          // a successful creation always gives an object (QueryInterface follows)
    bool nullableQueries;
    bool objectSlot3TakesId;
    bool root;        // the root director (GetDirectorFrameworkObject)
    bool soundCache;  // the cached sound service global 0x10f51b4
    bool alwaysOut = false; // every creation gives an object (the original uses it untested)
};

// 0x5b7a9c (TSUI's, cdecl, no arguments; still the game's): patched to a logged call.
void __cdecl FakeTSUIRegister()
{
    gLog.push_back(0x5b7a);
    if (gCalls++ == gThrowAt) throw 11;
}

struct Outcome {
    std::vector<uint32_t> log;
    unsigned long escaped;
    bool result;
    int calls;
};

struct Invocation {
    void *fn;
    void *self;
    void *arg;
    What what;
    bool result;
};
void Invoke(Invocation *c)
{
    switch (c->what) {
    case What::OnStart: c->result = reinterpret_cast<bool(__thiscall *)(void *, void *)>(c->fn)(c->self, c->arg); break;
    case What::Hook: c->result = reinterpret_cast<bool(__thiscall *)(void *)>(c->fn)(c->self); break;
    case What::Free:
        reinterpret_cast<void(__cdecl *)(void *)>(c->fn)(c->arg);
        c->result = true;
        break;
    }
}
unsigned long InvokeGuarded(Invocation *c)
{
    __try {
        Invoke(c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    return 0;
}

// One route of one scenario: 0 original, 1 ours, 2 adapter.
Outcome RunRoute(difftest::Context &ctx, const Case &t, int route, uint32_t cacheValue)
{
    uint8_t *d = NewBlock();
    void **saved0 = nullptr, **saved4 = nullptr;
    void *fn = nullptr;
    static void *vt0[18], *vt4[11];
    if (route == 1) {
        kOurs[t.index](d);
        saved0 = *reinterpret_cast<void ***>(d);
        saved4 = *reinterpret_cast<void ***>(d + 4);
        fn = t.what == What::OnStart ? saved0[4] : t.what == What::Hook ? saved4[t.hookSlot] : nullptr;
        std::memcpy(vt0, saved0, 17 * sizeof(void *));
        std::memcpy(vt4, saved4, sizeof(vt4));
    } else {
        ctx.Original<void *(__thiscall *)(void *)>(0x1adffa)(d);
        saved0 = *reinterpret_cast<void ***>(d);
        saved4 = *reinterpret_cast<void ***>(d + 4);
        std::memcpy(vt0, ctx.image.At<void *>(0xda265c), 17 * sizeof(void *));
        std::memcpy(vt4, ctx.image.At<void *>(0xda26a4), sizeof(vt4));
        if (route == 0) {
            fn = ctx.image.At(t.rva);
        } else {
            fn = t.adapter;
            if (t.what == What::OnStart) vt0[4] = reinterpret_cast<void *>(&Trap1);
            if (t.what == What::Hook) vt4[t.hookSlot] = reinterpret_cast<void *>(&Trap0);
        }
    }
    if (t.what == What::Free) fn = route == 0 ? ctx.image.At(t.rva) : reinterpret_cast<void *>(&StartEngineDirectors);
    vt0[12] = kRecs[0][12];
    *reinterpret_cast<void ***>(d) = vt0;
    *reinterpret_cast<void ***>(d + 4) = vt4;
    gUnderTest = d;
    gKnown.clear();
    Know(&gComArg, 1);
    Know(&gComThis, 2);
    Know(&gService, 3);
    Know(&gRoot, 0x2ff);
    Know(&gServer, 0x600);
    Know(d, 0x10);
    Know(d + 4, 0x14);
    for (unsigned i : t.subs) Know(ctx.image.At<void>(kDirectorSpecs[i].staticRva), 0x200 + i);
    for (int k = 0; k < 64; ++k) Know(&gPool[k], 0x400 + k);
    gPoolUsed = 0;
    gCtorCount = 0;
    gCreates = gQueries = gQis = gCalls = 0;
    gOptionReads = 0;
    if (gOptionsCase)
        for (uint32_t clsid : kMapClasses) reinterpret_cast<Base *>(d)->Base::AddCls(clsid, &gRoot);
    gLog.clear();
    gTrapHit = false;
    if (t.soundCache) *ctx.image.At<uint32_t>(0x10f51b4) = cacheValue;

    void *self = t.what == What::Hook ? static_cast<void *>(d + 4) : static_cast<void *>(d);
    Invocation call{fn, self, &gComArg, t.what, false};
    Outcome out;
    out.escaped = InvokeGuarded(&call);
    if (gThrowRecorder) gThrowRecorder->Stop();
    out.result = call.result;
    out.calls = gCalls;
    out.log = gLog;
    if (gOptionsCase) {
        // The class map after the call, in key order.
        std::vector<uint32_t> keys;
        for (const auto &entry : reinterpret_cast<Base *>(d)->mClasses) keys.push_back(entry.first);
        std::sort(keys.begin(), keys.end());
        out.log.push_back(0xC1A5);
        out.log.insert(out.log.end(), keys.begin(), keys.end());
    }
    if (gThrownMemory) {
        // The constructor threw: the frame must have freed the memory (malloc's heap is the process heap).
        out.log.push_back(0xF4EE);
        out.log.push_back(gThrowRecorder && gThrowRecorder->Freed(gThrownMemory) ? 1 : 0);
        gThrownMemory = nullptr;
    }
    gThrowRecorder.reset();
    if (gTrapHit) ctx.Fail("%s: the adapter dispatched to its own slot", t.name);

    *reinterpret_cast<void ***>(d) = saved0;
    *reinterpret_cast<void ***>(d + 4) = saved4;
    gUnderTest = nullptr;
    FreeDirector(d);
    for (void *p : gConstructed) free(p);
    gConstructed.clear();
    return out;
}

void RunCase(difftest::Context &ctx, const Case &t)
{
    BuildWorld(t.objectSlot3TakesId);
    EpochScope epoch(ctx);
    Jump imageNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&ImageNew));
    Jump imageDelete(ctx.image.At(0x824624), reinterpret_cast<void *>(&ImageSizedDelete));
    std::vector<std::unique_ptr<Jump>> ctors;
    for (uint32_t rva : {0x1d8b90u, 0x246110u, 0x416810u, 0x9704bbu})
        ctors.push_back(std::make_unique<Jump>(ctx.image.At(rva), reinterpret_cast<void *>(&FakeCtor)));
    // Ours builds the persist system with our constructor through its adapter (same convention as 0x1d8b90): the same fake.
    ctors.push_back(std::make_unique<Jump>(reinterpret_cast<void *>(&cTSPersistSystemAdapters::Construct), reinterpret_cast<void *>(&FakeCtor)));
    // Likewise the scene-graph system (0x246110), ours through nTSSG::cTSSGSystemAdapters::Construct.
    ctors.push_back(std::make_unique<Jump>(reinterpret_cast<void *>(&nTSSG::cTSSGSystemAdapters::Construct), reinterpret_cast<void *>(&FakeCtor)));
    // And the realtime debug display (0x9704bb), ours through cGZRealtimeDebugDisplayAdapters::Construct.
    ctors.push_back(std::make_unique<Jump>(reinterpret_cast<void *>(&cGZRealtimeDebugDisplayAdapters::Construct), reinterpret_cast<void *>(&FakeCtor)));
    // And the sim system (0x416810), ours through cTSSimSystemAdapters::Construct.
    ctors.push_back(std::make_unique<Jump>(reinterpret_cast<void *>(&cTSSimSystemAdapters::Construct), reinterpret_cast<void *>(&FakeCtor)));
    Jump tsuiRegister(ctx.image.At(0x5b7a9c), reinterpret_cast<void *>(&FakeTSUIRegister));
    std::vector<std::unique_ptr<FakeStatic>> statics;
    for (unsigned i : t.subs)
        statics.push_back(std::make_unique<FakeStatic>(ctx, kDirectorSpecs[i].staticRva, (kDirectorSpecs[i].flags & kStaticInit) ? 0 : kDirectorSpecs[i].guardRva, gSubVt));
    std::unique_ptr<FakeStatic> root;
    if (t.root) root = std::make_unique<FakeStatic>(ctx, 0x10c9390, 0x10c93f4, gRootVt);
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    *serviceGlobal = &gService;
    uint32_t savedCache = *ctx.image.At<uint32_t>(0x10f51b4);

    std::mt19937 rng(0xE7000 + t.rva);
    unsigned withThrows = 0, compared = 0;
    for (int scenario = 0; scenario < 48; ++scenario) {
        gCreateOut = (static_cast<uint64_t>(rng()) << 32) | rng();
        gCreateRet = (static_cast<uint64_t>(rng()) << 32) | rng();
        if (scenario == 0) gCreateOut = gCreateRet = ~0ull;
        if (scenario == 1) gCreateOut = gCreateRet = 0;
        if (t.qi) gCreateOut |= gCreateRet;
        if (t.alwaysOut) gCreateOut = ~0ull;
        gQiNull = scenario == 0 ? 0 : (static_cast<uint64_t>(rng()) << 32) | rng(); // QueryInterface may give nothing
        gQueryNull = t.nullableQueries ? ((static_cast<uint64_t>(rng()) << 32) | rng()) : 0;
        if (t.nullableQueries && scenario == 1) gQueryNull = ~0ull;
        if (scenario == 0) gQueryNull = 0; // every service present: the exception runs reach every holder
        uint32_t cache = t.soundCache && (rng() & 1) ? U(&gPool[63]) : 0;

        gThrowAt = -1;
        Outcome first = RunRoute(ctx, t, 0, cache);
        int throwModes = scenario < 4 ? first.calls : 0;
        // A dropped adapter (null; lane GP: only the game's vtables reach the original, trapped under the director's switch)
        // has no adapter route.
        const int routes = t.adapter || t.what == What::Free ? 3 : 2;
        for (int mode = -1; mode < throwModes; ++mode) {
            gThrowAt = mode;
            Outcome o[3];
            for (int route = 0; route < routes; ++route) o[route] = mode == -1 && route == 0 ? first : RunRoute(ctx, t, route, cache);
            ++ctx.cases;
            ++compared;
            if (mode >= 0) ++withThrows;
            for (int route = 1; route < routes; ++route) {
                bool same = o[0].escaped == o[route].escaped && o[0].log == o[route].log && (o[0].escaped != 0 || o[0].result == o[route].result);
                if (!same) {
                    size_t at = 0;
                    while (at < o[0].log.size() && at < o[route].log.size() && o[0].log[at] == o[route].log[at]) ++at;
                    ctx.Fail("%s %s (scenario %d, throw at %d): escaped %lx/%lx, result %d/%d, logs %zu/%zu words, first difference at word %zu (%x/%x)",
                             t.name, route == 1 ? "ours" : "adapter", scenario, mode, o[0].escaped, o[route].escaped, o[0].result, o[route].result,
                             o[0].log.size(), o[route].log.size(), at, at < o[0].log.size() ? o[0].log[at] : 0, at < o[route].log.size() ? o[route].log[at] : 0);
                    break;
                }
            }
        }
    }
    gThrowAt = -1;
    *serviceGlobal = savedService;
    *ctx.image.At<uint32_t>(0x10f51b4) = savedCache;
    if (withThrows == 0 && t.what != What::Free) ctx.Fail("%s: no exception scenario ran", t.name);
    if (compared == 0) ctx.Fail("%s: nothing compared", t.name);
}

template <unsigned N> void *OnStartAdapter() { return reinterpret_cast<void *>(&rz::cCOMDirectorAdapters<N>::OnStart); }

// TSUI PostAppInit's allocation (the image's RZMalloc 0x1be3b0 behind 0x1c03b7, and our RZMemory::Malloc): plain poisoned
// memory, logged with its size; the message is known by identity 0x700.
void *__cdecl TestMalloc(uint32_t bytes)
{
    gLog.push_back(0xA110);
    gLog.push_back(bytes);
    if (gCalls++ == gThrowAt) throw 12;
    void *p = malloc(bytes);
    std::memset(p, 0xcd, bytes);
    Know(p, 0x700);
    return p;
}
// The message server getter 0x9c5a89 (still the game's on both sides).
void *__cdecl FakeMessageServer()
{
    gLog.push_back(0x9c5a);
    if (gCalls++ == gThrowAt) throw 13;
    return &gServer;
}

// Slots of the image's message vtable (0xda4064) patched to the recorders while alive.
struct MessageVtablePatch {
    void **vt;
    void *saved[12];
    explicit MessageVtablePatch(difftest::Context &ctx) : vt(ctx.image.At<void *>(0xda4064))
    {
        DWORD old;
        VirtualProtect(vt, sizeof(saved), PAGE_READWRITE, &old);
        std::memcpy(saved, vt, sizeof(saved));
        vt[1] = kRecs[0][1];
        vt[2] = kRecs[0][2];
        vt[5] = kRecs[1][5];
        vt[11] = kRecs[1][11];
        gMessageVt = vt;
    }
    MessageVtablePatch(const MessageVtablePatch &) = delete;
    MessageVtablePatch &operator=(const MessageVtablePatch &) = delete;
    ~MessageVtablePatch()
    {
        std::memcpy(vt, saved, sizeof(saved));
        gMessageVt = nullptr;
    }
};

// TSUI OnStart's dispatchers (cdecl, two arguments; the game's on both sides), patched to logged fakes. The two queries
// answer from the scenario; the two checks give their argument back or the value that skips the erase.
template <uint32_t Rva> int __cdecl FakeDispatch(int a, int b)
{
    gLog.push_back(Rva);
    gLog.push_back(static_cast<uint32_t>(a));
    gLog.push_back(static_cast<uint32_t>(b));
    if (gCalls++ == gThrowAt) throw 14;
    switch (Rva) {
    case 0x5b696a: return ((gCreateRet >> 40) & 1) ? 0x1cf : 0x77;
    case 0x5b6a72: return ((gCreateRet >> 41) & 1) ? -0x16 : 0x55;
    case 0x5b68ba: return ((gCreateRet >> 42) & 1) ? a : 0x1cf;
    case 0x5b69c2: return ((gCreateRet >> 43) & 1) ? b : -0x16;
    default: return 0x3c3c;
    }
}
// 0x830972 (atoi, the CRT's): the library function, so the original's import-using copy is not run.
int __cdecl LibraryAtoi(const char *text) { return std::atoi(text); }

const std::vector<unsigned> kAppSubs = {kDir_Event, kDir_CompGeom, kDir_Graphics, kDir_Draw2D, kDir_TSSG, kDir_TSUI, kDir_TSPersist, kDir_TSSim,
                                        kDir_PathPlan, kDir_Expat, kDir_TSWeb, kDir_TSCommon, kDir_TSCAS, kDir_Lua5, kDir_MadCow};

} // namespace

DIFF_TEST(COMDirectorsStart_TSPersist, 0x1d3e20)
{
    RunCase(ctx, {"TSPersist", 0x1d3e20, What::OnStart, kDir_TSPersist, 0, nullptr, {}, false, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_TSSG, 0x2454c0)
{
    RunCase(ctx, {"TSSG", 0x2454c0, What::OnStart, kDir_TSSG, 0, nullptr, {kDir_SceneGraph, kDir_SceneDBAnim}, false, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_TSSim, 0x416370)
{
    RunCase(ctx, {"TSSim", 0x416370, What::OnStart, kDir_TSSim, 0, OnStartAdapter<kDir_TSSim>(), {}, true, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_TSCommon, 0xcb5110)
{
    RunCase(ctx, {"TSCommon", 0xcb5110, What::OnStart, kDir_TSCommon, 0, OnStartAdapter<kDir_TSCommon>(), {}, false, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_TSCAS, 0xcb54a0)
{
    // Adapter dropped (lane GT; trapped under the constructor and destructor-body switches): two routes.
    RunCase(ctx, {"TSCAS", 0xcb54a0, What::OnStart, kDir_TSCAS, 0, nullptr, {}, true, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_TSAppFW, 0xc2beec)
{
    RunCase(ctx, {"TSAppFW", 0xc2beec, What::OnStart, kDir_TSAppFW, 0, OnStartAdapter<kDir_TSAppFW>(), kAppSubs, false, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_StartEngineDirectors, 0x14107c)
{
    RunCase(ctx, {"StartEngineDirectors", 0x14107c, What::Free, kDir_TSAppFW, 0, nullptr, kAppSubs, false, false, true, false, false});
}

DIFF_TEST(COMDirectorsStart_SoundPostAppInit, 0xb224d9)
{
    RunCase(ctx, {"SoundPostAppInit", 0xb224d9, What::Hook, kDir_Sound, 5, nullptr, {}, false,
                  true, true, false, false});
}

DIFF_TEST(COMDirectorsStart_SoundPreAppShutdown, 0xb2252e)
{
    RunCase(ctx, {"SoundPreAppShutdown", 0xb2252e, What::Hook, kDir_Sound, 6, nullptr, {},
                  false, true, true, false, true});
}

DIFF_TEST(COMDirectorsStart_HitManPreAppInit, 0xb2a4c3)
{
    RunCase(ctx, {"HitManPreAppInit", 0xb2a4c3, What::Hook, kDir_HitMan, 4, nullptr, {}, false,
                  false, true, true, false});
}

DIFF_TEST(COMDirectorsStart_TSUIPreFrameWorkInit, 0x5b7722)
{
    // Adapter dropped (lane GT; trapped under TSUI's constructor and destructor-body switches): two routes.
    Case t{"TSUIPreFrameWorkInit", 0x5b7722, What::Hook, kDir_TSUI, 3, nullptr, {},
           false, true, true, false, false};
    t.alwaysOut = true;
    RunCase(ctx, t);
}

DIFF_TEST(COMDirectorsStart_HitManPreAppShutdown, 0xb2a568)
{
    RunCase(ctx, {"HitManPreAppShutdown", 0xb2a568, What::Hook, kDir_HitMan, 6, nullptr, {},
                  false, true, false, false, false});
}

// Lane FB. The settings service may be absent (nullable queries); the setting's value is a byte from the scenario; the
// message is real memory with the image's vtable, its four used slots recorded (its first AddRef logs it as constructed;
// the last Release frees it and logs that), so a leaked or doubly released message shows in the log.
DIFF_TEST(COMDirectorsStart_TSUIPostAppInit, 0x5b763c)
{
    gSettingsCase = true;
    {
        MessageVtablePatch messages(ctx);
        Jump theirMalloc(ctx.image.At(0x1be3b0), reinterpret_cast<void *>(&TestMalloc));
        Jump ourMalloc(reinterpret_cast<void *>(&RZMemory::Malloc), reinterpret_cast<void *>(&TestMalloc));
        Jump server(ctx.image.At(0x9c5a89), reinterpret_cast<void *>(&FakeMessageServer));
        Jump ourServer(reinterpret_cast<void *>(&GetCachedMessageServer), reinterpret_cast<void *>(&FakeMessageServer)); // ours
        RunCase(ctx, {"TSUIPostAppInit", 0x5b763c, What::Hook, kDir_TSUI, 5, nullptr /* adapter dropped, lane GT */, {}, false,
                      true, true, false, false});
    }
    gSettingsCase = false;
}

// Lane FB. The four sub-directors are fake statics; the COM argument creates the five services (or not, with any
// answer); the options object's two reads and the four dispatchers answer from the scenario; the class map's keys after
// the call are compared. (Every erased class is present, so the original's erase of an absent class, end(), is not run.)
DIFF_TEST(COMDirectorsStart_TSUIOnStart, 0x5b72e7)
{
    gOptionsCase = true;
    {
        std::vector<std::unique_ptr<Jump>> patches;
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b696a), reinterpret_cast<void *>(&FakeDispatch<0x5b696a>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b6a72), reinterpret_cast<void *>(&FakeDispatch<0x5b6a72>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b68ba), reinterpret_cast<void *>(&FakeDispatch<0x5b68ba>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b69c2), reinterpret_cast<void *>(&FakeDispatch<0x5b69c2>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b6aca), reinterpret_cast<void *>(&FakeDispatch<0x5b6aca>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b6912), reinterpret_cast<void *>(&FakeDispatch<0x5b6912>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b6b22), reinterpret_cast<void *>(&FakeDispatch<0x5b6b22>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x5b6a1a), reinterpret_cast<void *>(&FakeDispatch<0x5b6a1a>)));
        patches.push_back(std::make_unique<Jump>(ctx.image.At(0x830972), reinterpret_cast<void *>(&LibraryAtoi)));
        RunCase(ctx, {"TSUIOnStart", 0x5b72e7, What::OnStart, kDir_TSUI, 0, nullptr /* adapter dropped, lane GT */, {kDir_Win, kDir_Script, kDir_UIScript, kDir_XmlUtils},
                      false, false, true, false, false});
    }
    gOptionsCase = false;
}
