// Differential tests for the SG resource helpers (src/sg/SGResourceHelpers.cpp).
#include "difftest.h"
#include "sg/SGResourceHelpers.h"

#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

// 16 bytes; `this` is at byte 8, so the helper reads the dword at bytes 0..3. Bytes 4..15 are read-only context
// for the call and must not change.
struct Buffer {
    uint8_t bytes[16];
};

uint32_t ReadWord(const Buffer &b)
{
    uint32_t w;
    std::memcpy(&w, b.bytes, sizeof(w));
    return w;
}

void WriteWord(Buffer &b, uint32_t w)
{
    std::memcpy(b.bytes, &w, sizeof(w));
}

// ---- cSGResource secondary subobject fixture (for 0xa3c790) ----
//
// The object is gObj (its primary vtable at +0) and the subobject `this` is gObj + 0x30. Relative to `this`, as
// in src/gz/cSGResource.h: the object at -0x30, the vbptr at -0x14 (object +0x1c), the resource at -0x0c
// (object +0x24), flag A at -0x08 (object +0x28) and flag B at -0x07 (object +0x29). The virtual base is at
// this + vbtable[1] - 0x14 = gObj + 0x7c, where its own vtable pointer sits.
//
// Every peer is a fake whose calls are logged: primary slot 3 (key copy), the virtual base's slot 23, the
// service's slot 5, the holder's slot 2 (Release) and slot 14 (submit). The resource is only passed through. The
// original and the replacement run on the same buffer with the same starting bytes, so pointers in the log are
// equal on both sides; the stack copies of the key are compared by content. Compared per run: the return value
// (al), the object's bytes and the log.

constexpr uint32_t kServiceGlobal = 0x10c9378; // read by GetServiceObject (game global)
constexpr int kVbDisp = 0x60;                  // base at object + 0x1c + 0x60 = gObj + 0x7c

struct Event {
    int slot = 0;
    const void *self = nullptr;
    const void *p1 = nullptr;
    const void *p2 = nullptr;
    uint32_t value = 0;
    uint32_t words[4] = {};
};

std::vector<Event> gLog;

alignas(16) uint8_t gObj[0x100];
uint32_t gVbTable[2];
void *gPrimaryVt[24];
void *gBaseVt[24];
void *gServiceVt[24];
void *gHolderVt[24];

struct FakeObject {
    void *vtable;
    uint32_t pad;
};
FakeObject gServiceObj{}, gHolderObj{}, gRes1{};

uint32_t gBaseAnswer, gHolderAnswer;
bool gHolderThrows;
const uint32_t gKey[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u}; // written by primary slot 3

Event Make(int slot, const void *self, const void *p1 = nullptr, const void *p2 = nullptr, uint32_t value = 0,
           const void *key = nullptr)
{
    Event e;
    e.slot = slot;
    e.self = self;
    e.p1 = p1;
    e.p2 = p2;
    e.value = value;
    if (key) std::memcpy(e.words, key, sizeof(e.words));
    return e;
}

// The resource pointer as the object holds it at the time of a call (a reordering shows up here).
const void *FieldNow()
{
    return *reinterpret_cast<void *const *>(gObj + 0x24);
}

// Fakes are called as __thiscall: `this` in ecx, stack arguments popped by the callee. A __fastcall function with
// a dummy edx takes the same registers and stack layout.
void *__fastcall PrimaryKey(void *self, void * /*edx*/, void *out)
{
    std::memcpy(out, gKey, sizeof(gKey));
    gLog.push_back(Make(3, self, nullptr, nullptr, 0, out));
    return out;
}

uint32_t __fastcall BaseQuery(void *self, void * /*edx*/)
{
    gLog.push_back(Make(23, self, nullptr, nullptr, gBaseAnswer));
    return gBaseAnswer; // only al matters to the caller; the upper bits are noise on purpose
}

void __fastcall ServiceQuery(void *self, void * /*edx*/, uint32_t a, uint32_t b, void **out)
{
    Event e = Make(5, self, nullptr, nullptr, a);
    e.words[0] = b;
    gLog.push_back(e);
    *out = &gHolderObj;
}

void __fastcall HolderRelease(void *self, void * /*edx*/)
{
    gLog.push_back(Make(2, self, FieldNow()));
}

uint32_t __fastcall HolderSubmit(void *self, void * /*edx*/, void *obj, void *key, void *resource)
{
    gLog.push_back(Make(14, self, obj, resource, gHolderAnswer, key));
    if (gHolderThrows) throw 42;
    return gHolderAnswer;
}

void InitFakes()
{
    std::memset(gPrimaryVt, 0, sizeof(gPrimaryVt));
    std::memset(gBaseVt, 0, sizeof(gBaseVt));
    std::memset(gServiceVt, 0, sizeof(gServiceVt));
    std::memset(gHolderVt, 0, sizeof(gHolderVt));
    gPrimaryVt[3] = reinterpret_cast<void *>(&PrimaryKey);
    gBaseVt[23] = reinterpret_cast<void *>(&BaseQuery);
    gServiceVt[5] = reinterpret_cast<void *>(&ServiceQuery);
    gHolderVt[2] = reinterpret_cast<void *>(&HolderRelease);
    gHolderVt[14] = reinterpret_cast<void *>(&HolderSubmit);
    gServiceObj.vtable = gServiceVt;
    gHolderObj.vtable = gHolderVt;
}

struct Case {
    uint32_t seed;
    void *resource;
    uint8_t flagA;   // at this - 0x08
    uint32_t base;   // the virtual base's slot 23 answer
    uint32_t holder; // the holder's slot 14 answer
    bool throws;     // the holder's slot 14 throws a C++ exception
};

// Rebuilds the object for a run: the same starting bytes for the same seed, then the pointers and the case's values.
void Setup(difftest::Context &ctx, const Case &c)
{
    std::mt19937 gen(c.seed);
    for (auto &b : gObj) b = static_cast<uint8_t>(gen());
    *reinterpret_cast<void **>(gObj) = gPrimaryVt;
    gVbTable[0] = 0;
    gVbTable[1] = kVbDisp;
    *reinterpret_cast<void **>(gObj + 0x1c) = gVbTable;
    *reinterpret_cast<void **>(gObj + 0x7c) = gBaseVt;
    *reinterpret_cast<void **>(gObj + 0x24) = c.resource;
    gObj[0x28] = c.flagA;
    gBaseAnswer = c.base;
    gHolderAnswer = c.holder;
    gHolderThrows = c.throws;
    gLog.clear();
    *ctx.image.At<void *>(kServiceGlobal) = &gServiceObj;
}

struct Snap {
    uint64_t ret = 0;
    uint8_t obj[0x100];
    std::vector<Event> log;
    bool escaped = false;
};

template <typename Fn> Snap Capture(difftest::Context &ctx, const Case &c, Fn call)
{
    Setup(ctx, c);
    Snap s;
    try {
        s.ret = call(gObj + 0x30);
    } catch (int) {
        s.escaped = true; // only the throwing case expects this
    }
    std::memcpy(s.obj, gObj, sizeof(s.obj));
    s.log = gLog;
    return s;
}

bool SameEvents(const std::vector<Event> &a, const std::vector<Event> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Event &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.p1 != y.p1 || x.p2 != y.p2 || x.value != y.value ||
            std::memcmp(x.words, y.words, sizeof(x.words)) != 0)
            return false;
    }
    return true;
}

void Compare(difftest::Context &ctx, const char *name, const Case &c, const Snap &a, const Snap &b)
{
    ++ctx.cases;
    if (a.ret != b.ret || a.escaped != b.escaped || std::memcmp(a.obj, b.obj, sizeof(a.obj)) != 0 || !SameEvents(a.log, b.log)) {
        ctx.Fail("%s seed %u flagA %02x: original ret %llx (escaped %d), ours %llx (escaped %d); %zu vs %zu calls; object %s",
                 name, c.seed, c.flagA, static_cast<unsigned long long>(a.ret), a.escaped,
                 static_cast<unsigned long long>(b.ret), b.escaped, a.log.size(), b.log.size(),
                 std::memcmp(a.obj, b.obj, sizeof(a.obj)) ? "differs" : "same");
    }
}

using cSGSecondary = SGResourceHelpers::cSGSecondary;

} // namespace

// 0xa3bdb0: `mov eax,[ecx-8]; shr eax,31; ret`. The result is compared as the full 32-bit eax, and the buffer
// is compared afterwards so a write would show up.
// The image globals these tests point at their fakes: put them back when each test ends, so a later test (another
// file in the same runner process) does not find pointers to this file's fakes (the skin extension and tangent builder
// tests failed after this file, 2026-10-30).
struct GlobalsGuard {
    difftest::Context &ctx;
    void *saved[3];
    static constexpr uint32_t kGlobals[3] = {0x10c9378, 0x10f9070, 0x10f8f2c}; // service, cache object, key service
    explicit GlobalsGuard(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 3; ++i) saved[i] = *ctx.image.At<void *>(kGlobals[i]);
    }
    ~GlobalsGuard()
    {
        for (int i = 0; i < 3; ++i) *ctx.image.At<void *>(kGlobals[i]) = saved[i];
    }
};

DIFF_TEST(sg_sign_bit_before_this, 0xa3bdb0)
{
    GlobalsGuard globalsGuard(ctx);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa3bdb0);
    std::mt19937 gen(0x5a10);

    const uint32_t edges[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu, 0xfffffffeu, 0x00ffffffu};
    for (uint32_t i = 0; i < 20000; ++i) {
        uint32_t word = i < std::size(edges) ? edges[i] : gen();
        Buffer a, b;
        for (auto &x : a.bytes) x = static_cast<uint8_t>(gen());
        WriteWord(a, word);
        std::memcpy(b.bytes, a.bytes, sizeof(a.bytes));

        uint32_t want = original(a.bytes + 8);
        uint32_t got = reinterpret_cast<SGResourceHelpers::cSignBitBeforeThis *>(b.bytes + 8)->SignBit();
        if (want != got) ctx.Fail("sg_sign_bit_before_this: word %08x original %08x ours %08x", word, want, got);
        if (std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0 || ReadWord(a) != word) {
            ctx.Fail("sg_sign_bit_before_this: buffer changed for word %08x", word);
        }
        ++ctx.cases;
    }
}

// 0xa3c750 (no arguments, 20 bytes): the virtual base when the object (this - 0x30) is not null. A null object is
// `this` = 0x30, which must return null without reading the vbptr.
DIFF_TEST(sg_virtual_base_if_object, 0xa3c750)
{
    GlobalsGuard globalsGuard(ctx);
    InitFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa3c750);
    void *selfs[] = {gObj + 0x30, reinterpret_cast<void *>(0x30)};
    uint32_t seed = 700;
    for (void *s : selfs) {
        Case c{seed++, &gRes1, 1, 0, 0, false};
        Setup(ctx, c);
        void *ra = orig(s);
        uint8_t objA[0x100];
        std::memcpy(objA, gObj, sizeof(objA));

        Setup(ctx, c);
        void *rb = reinterpret_cast<cSGSecondary *>(s)->VirtualBaseIfObject();
        ++ctx.cases;
        if (ra != rb || std::memcmp(objA, gObj, sizeof(objA)) != 0)
            ctx.Fail("VirtualBaseIfObject this %p: original %p, ours %p", s, ra, rb);
    }
}

// 0xa3c790 (208 bytes, FuncInfo flags 1: an unwind-only cleanup region for the holder, no catch). Flag A and the
// base's slot 23 decide the early returns; otherwise the path runs to the holder's slot 14. Tested over the flag,
// base and holder answers (with garbage in the upper bits), with and without a resource.
DIFF_TEST(sg_submit_key_to_service, 0xa3c790)
{
    GlobalsGuard globalsGuard(ctx);
    InitFakes();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0xa3c790);
    const uint8_t flags[] = {0x00, 0x01, 0x80};
    const uint32_t bases[] = {0u, 1u, 0x100u, 0xffffff00u, 0xfeu};
    const uint32_t holders[] = {0u, 1u, 0x80u, 0xffu, 0x100u, 0x1ffu};
    void *resources[] = {nullptr, &gRes1};
    uint32_t seed = 1;
    for (uint8_t f : flags)
        for (uint32_t b : bases)
            for (uint32_t h : holders)
                for (void *r : resources) {
                    Case c{seed++, r, f, b, h, false};
                    Snap a = Capture(ctx, c, [&](void *s) { return uint64_t(orig(s)); });
                    Snap o = Capture(ctx, c, [](void *s) {
                        return uint64_t(reinterpret_cast<cSGSecondary *>(s)->SubmitKeyToService());
                    });
                    Compare(ctx, "SubmitKeyToService", c, a, o);
                }

    // The holder's slot 14 throws: the original's cleanup releases the holder while the exception unwinds, and the
    // exception then reaches the caller. Base slot 23 must answer non-zero to reach slot 14.
    Case thrower{seed++, &gRes1, 0x01, 1u, 0x01u, true};
    Snap a = Capture(ctx, thrower, [&](void *s) { return uint64_t(orig(s)); });
    Snap o = Capture(ctx, thrower, [](void *s) {
        return uint64_t(reinterpret_cast<cSGSecondary *>(s)->SubmitKeyToService());
    });
    if (!a.escaped) ctx.Fail("SubmitKeyToService: the original did not reach the throwing slot 14");
    Compare(ctx, "SubmitKeyToService throws", thrower, a, o);
}

// ---- ResetAndSubmit (0xa3c890) ----
//
// Relative to this = object + 0x30: the ServiceCell at -0x10 (object + 0x20, reset through the cache object's slot 7
// when non-empty), flag A at -0x08, the virtual base at -0x14 (slot 4 gives an out object X and a pointer R whose
// dword at +4 goes to primary slot 8), and the primary slots 3 (key), 8, 9 and 10. Flag A decides whether the first
// holder (from the service, slot 5) is used. The service hands out holder 1 first (only when flag A is set) and then
// holder 2. The original's calls are compared with ours: return value (al), object bytes, and the log of calls.
namespace {

constexpr uint32_t kCacheObjectGlobal = 0x10f9070; // GetCachedObjectB's cache (game global)

// Which call throws a C++ exception (the test then checks both sides release what the original releases).
enum ThrowAt : int { kNoThrow, kThrowBase4, kThrowKey, kThrowP8, kThrowP9, kThrowP10, kThrowH10, kThrowH9 };

int gThrowAt = kNoThrow;
void *gBaseOut = nullptr;   // what the virtual base's slot 4 writes to its out pointer
uint32_t gRBlock[4] = {};   // what it returns (gRBlock[1] is read at R + 4)
uint32_t gH9Answer = 0;     // holder 2's slot 9 answer (the upper bits are noise on purpose)
void *gSvcHolders[2] = {};  // what the service hands out, in call order
int gSvcCalls = 0;

FakeObject gR13Svc{}, gR13H1{}, gR13H2{}, gR13X{}, gR13Cache{};
void *gR13PrimVt[24], *gR13BaseVt[24], *gR13H1Vt[24], *gR13H2Vt[24], *gR13XVt[24], *gR13SvcVt[24], *gR13CacheVt[24];

void *__fastcall R13Key(void *self, void * /*edx*/, void *out)
{
    std::memcpy(out, gKey, sizeof(gKey));
    gLog.push_back(Make(3, self, nullptr, nullptr, 0, out));
    if (gThrowAt == kThrowKey) throw 42;
    return out; // the original returns the key pointer in eax (ret 4), which the caller passes on
}

void *__fastcall R13Base4(void *self, void * /*edx*/, void **out)
{
    *out = gBaseOut;
    gLog.push_back(Make(4, self, nullptr, nullptr, gBaseOut != nullptr));
    if (gThrowAt == kThrowBase4) throw 42;
    return gRBlock;
}

void __fastcall R13P8(void *self, void * /*edx*/, uint32_t a)
{
    gLog.push_back(Make(8, self, nullptr, nullptr, a));
    if (gThrowAt == kThrowP8) throw 42;
}

void __fastcall R13P9(void *self, void * /*edx*/, void *a)
{
    gLog.push_back(Make(9, self, nullptr, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))));
    if (gThrowAt == kThrowP9) throw 42;
}

void __fastcall R13P10(void *self, void * /*edx*/, void *a)
{
    gLog.push_back(Make(10, self, nullptr, nullptr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))));
    if (gThrowAt == kThrowP10) throw 42;
}

void __fastcall R13H10(void *self, void * /*edx*/, void *key)
{
    gLog.push_back(Make(10, self, nullptr, nullptr, 0, key));
    if (gThrowAt == kThrowH10) throw 42;
}

// Holder 2's slot 9 takes two stack arguments, the key pointer (first) and the object, and pops both (ret 8): the
// original pushes the object before the key copy and leaves it on the stack for this call to pop.
uint32_t __fastcall R13H9(void *self, void * /*edx*/, void *key, void *object)
{
    Event e = Make(9, self, object, nullptr, gH9Answer, key);
    gLog.push_back(e);
    if (gThrowAt == kThrowH9) throw 42;
    return gH9Answer;
}

void __fastcall R13XRelease(void *self, void * /*edx*/)
{
    gLog.push_back(Make(2, self));
}

void __fastcall R13Service(void *self, void * /*edx*/, uint32_t a, uint32_t b, void **out)
{
    Event e = Make(5, self, nullptr, nullptr, a);
    e.words[0] = b;
    e.words[1] = static_cast<uint32_t>(gSvcCalls);
    gLog.push_back(e);
    *out = gSvcCalls < 2 ? gSvcHolders[gSvcCalls] : nullptr;
    ++gSvcCalls;
}

void __fastcall R13Cache7(void *self, void * /*edx*/, uint32_t value)
{
    gLog.push_back(Make(7, self, nullptr, nullptr, value));
}

void R13Init()
{
    for (void **vt : {gR13PrimVt, gR13BaseVt, gR13H1Vt, gR13H2Vt, gR13XVt, gR13SvcVt, gR13CacheVt})
        std::memset(vt, 0, sizeof(gR13PrimVt));
    gR13PrimVt[3] = reinterpret_cast<void *>(&R13Key);
    gR13PrimVt[8] = reinterpret_cast<void *>(&R13P8);
    gR13PrimVt[9] = reinterpret_cast<void *>(&R13P9);
    gR13PrimVt[10] = reinterpret_cast<void *>(&R13P10);
    gR13BaseVt[4] = reinterpret_cast<void *>(&R13Base4);
    gR13H1Vt[2] = reinterpret_cast<void *>(&HolderRelease);
    gR13H1Vt[10] = reinterpret_cast<void *>(&R13H10);
    gR13H2Vt[2] = reinterpret_cast<void *>(&HolderRelease);
    gR13H2Vt[9] = reinterpret_cast<void *>(&R13H9);
    gR13XVt[2] = reinterpret_cast<void *>(&R13XRelease);
    gR13SvcVt[5] = reinterpret_cast<void *>(&R13Service);
    gR13CacheVt[7] = reinterpret_cast<void *>(&R13Cache7);
    gR13Svc.vtable = gR13SvcVt;
    gR13H1.vtable = gR13H1Vt;
    gR13H2.vtable = gR13H2Vt;
    gR13X.vtable = gR13XVt;
    gR13Cache.vtable = gR13CacheVt;
}

struct C13 {
    uint32_t seed;
    uint8_t flagA;     // at this - 0x08
    uint32_t cell;     // ServiceCell at this - 0x10
    bool xSet;         // the virtual base's slot 4 gives an out object
    int throwAt;       // ThrowAt
    uint32_t a1, a2;   // the arguments
    uint32_t rval;     // the dword at R + 4
    uint32_t h9;       // holder 2's slot 9 answer
};

void Setup13(difftest::Context &ctx, const C13 &c)
{
    std::mt19937 gen(c.seed);
    for (auto &b : gObj) b = static_cast<uint8_t>(gen());
    *reinterpret_cast<void **>(gObj) = gR13PrimVt;
    gVbTable[0] = 0;
    gVbTable[1] = kVbDisp;
    *reinterpret_cast<void **>(gObj + 0x1c) = gVbTable;
    *reinterpret_cast<void **>(gObj + 0x7c) = gR13BaseVt;
    *reinterpret_cast<uint32_t *>(gObj + 0x20) = c.cell;
    gObj[0x28] = c.flagA;
    gBaseOut = c.xSet ? static_cast<void *>(&gR13X) : nullptr;
    gRBlock[1] = c.rval;
    gH9Answer = c.h9;
    gSvcHolders[0] = c.flagA ? static_cast<void *>(&gR13H1) : static_cast<void *>(&gR13H2);
    gSvcHolders[1] = &gR13H2;
    gSvcCalls = 0;
    gThrowAt = c.throwAt;
    *ctx.image.At<void *>(kServiceGlobal) = &gR13Svc;
    *ctx.image.At<void *>(kCacheObjectGlobal) = &gR13Cache;
    gLog.clear();
}

struct Snap13 {
    uint8_t ret = 0;
    uint8_t obj[0x100];
    std::vector<Event> log;
    bool escaped = false;
};

template <typename Fn> Snap13 Capture13(difftest::Context &ctx, const C13 &c, Fn call)
{
    Setup13(ctx, c);
    Snap13 s;
    try {
        s.ret = call(gObj + 0x30);
    } catch (int) {
        s.escaped = true; // only the throwing cases expect this
    }
    std::memcpy(s.obj, gObj, sizeof(s.obj));
    s.log = gLog;
    return s;
}

void *Ptr(uint32_t v)
{
    return reinterpret_cast<void *>(static_cast<uintptr_t>(v));
}

} // namespace

DIFF_TEST(sg_reset_and_submit, 0xa3c890)
{
    GlobalsGuard globalsGuard(ctx);
    R13Init();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *, void *)>(0xa3c890);
    std::mt19937 gen(0x13c890);
    int escapes = 0;
    uint32_t seed = 1300;
    for (uint8_t flagA : {uint8_t{0}, uint8_t{1}})
        for (uint32_t cell : {0xffffffffu, 0x7u})
            for (bool xSet : {false, true})
                for (int throwAt = kNoThrow; throwAt <= kThrowH9; ++throwAt)
                    for (int rep = 0; rep < 4; ++rep) {
                        C13 c{seed++, flagA, cell, xSet, throwAt, gen(), gen(), gen(), gen()};
                        Snap13 a = Capture13(ctx, c, [&](void *s) {
                            return orig(s, Ptr(c.a1), Ptr(c.a2));
                        });
                        Snap13 o = Capture13(ctx, c, [&](void *s) {
                            return reinterpret_cast<cSGSecondary *>(s)->ResetAndSubmit(Ptr(c.a1), Ptr(c.a2));
                        });
                        ++ctx.cases;
                        if (a.escaped) ++escapes;
                        if (a.ret != o.ret || a.escaped != o.escaped || std::memcmp(a.obj, o.obj, sizeof(a.obj)) != 0 ||
                            !SameEvents(a.log, o.log)) {
                            ctx.Fail("ResetAndSubmit flag %u cell %08x x %d throw %d: original ret %02x (escaped %d), ours "
                                     "%02x (escaped %d); %zu vs %zu calls; object %s",
                                     flagA, cell, xSet, throwAt, a.ret, a.escaped, o.ret, o.escaped, a.log.size(),
                                     o.log.size(), std::memcmp(a.obj, o.obj, sizeof(a.obj)) ? "differs" : "same");
                        }
                    }
    // The throwing cases must really throw in the original (else they test nothing).
    if (escapes == 0) ctx.Fail("ResetAndSubmit: no case threw in the original");
}

// ---- OpenAssignAndSubmit (0xa3ca00) and AssignAndSubmit (0xa3cbf0) ----
//
// Both take a ServiceCell argument a1 (a uint32 value, opened with (a1, 1) and assigned into the cell at this - 0x10
// through the cached object's slots 5, 6 and 7), then the virtual base's slot 15 (a2 or a3), the flag A block, the
// virtual base's slot 4 and slot 17 (the key text), ParseKey with the dword at R + 4, primary slot 4 (the Result, logged
// by its words), and for slot 14 primary slot 9 with a2; the second holder's slot 9 gives the result. The fakes below
// add the calls the ResetAndSubmit fakes do not make. ParseKey uses the service cache at 0x10f8f2c for names, so that
// global points at a fake whose slot 0 hashes the name (the same answer on both sides).
namespace {

constexpr uint32_t kKeyServiceGlobal = 0x10f8f2c; // ParseKey's cached service object (game global)

// Throw points past the ones above (the same gThrowAt): base slot 15, base slot 17, primary slot 4, cache slot 5 (Open),
// cache slot 6 on its first call (Open) and on its second call (Assign). Cache slot 7 is not thrown: it runs inside the
// noexcept ServiceCell::Close on the argument cell.
constexpr int kThrowBase15 = 8;
constexpr int kThrowBase17 = 9;
constexpr int kThrowP4 = 10;
constexpr int kThrowCache5 = 11;
constexpr int kThrowCache6Open = 12;
constexpr int kThrowCache6Assign = 13;

const char *const kKeyTexts[] = {"Alpha!Beta", "*!Gamma", "#1f!Delta", "##12!Eps", "plain text", "!Zeta", "Name!"};
int gKeyIndex = 0;
uint32_t gCache5Answer = 0;
int gCache6Calls = 0;
FakeObject gKeyObj{};
void *gKeyVt[8];

uint32_t __fastcall KeyLookup(void *self, void * /*edx*/, const char *name)
{
    // The hash of the name (not the pointer: ParseKey's copy of the name is at a heap address).
    uint32_t h = 0;
    for (const char *p = name; *p; ++p) h = h * 31u + static_cast<uint8_t>(*p);
    gLog.push_back(Make(0, self, nullptr, nullptr, h));
    return h;
}

void __fastcall R14P4(void *self, void * /*edx*/, void *out)
{
    // The Result's words are logged; the stack address is not (it differs between the two runs).
    gLog.push_back(Make(4, self, nullptr, nullptr, 0, out));
    if (gThrowAt == kThrowP4) throw 42;
}

void __fastcall R14Base15(void *self, void * /*edx*/, void *a)
{
    gLog.push_back(Make(15, self, a));
    if (gThrowAt == kThrowBase15) throw 42;
}

void *__fastcall R14Base17(void *self, void * /*edx*/)
{
    gLog.push_back(Make(17, self, nullptr, nullptr, static_cast<uint32_t>(gKeyIndex)));
    if (gThrowAt == kThrowBase17) throw 42;
    return const_cast<char *>(kKeyTexts[gKeyIndex]);
}

uint32_t __fastcall R14Cache5(void *self, void * /*edx*/, uint32_t a, uint32_t b)
{
    Event e = Make(5, self, nullptr, nullptr, gCache5Answer);
    e.words[0] = a;
    e.words[1] = b;
    gLog.push_back(e);
    if (gThrowAt == kThrowCache5) throw 42;
    return gCache5Answer;
}

uint32_t __fastcall R14Cache6(void *self, void * /*edx*/, uint32_t a)
{
    ++gCache6Calls;
    gLog.push_back(Make(6, self, nullptr, nullptr, a));
    if ((gThrowAt == kThrowCache6Open && gCache6Calls == 1) || (gThrowAt == kThrowCache6Assign && gCache6Calls == 2))
        throw 42;
    return 0x66;
}

void R14Init()
{
    R13Init();
    gR13PrimVt[4] = reinterpret_cast<void *>(&R14P4);
    gR13BaseVt[15] = reinterpret_cast<void *>(&R14Base15);
    gR13BaseVt[17] = reinterpret_cast<void *>(&R14Base17);
    gR13CacheVt[5] = reinterpret_cast<void *>(&R14Cache5);
    gR13CacheVt[6] = reinterpret_cast<void *>(&R14Cache6);
    std::memset(gKeyVt, 0, sizeof(gKeyVt));
    gKeyVt[0] = reinterpret_cast<void *>(&KeyLookup);
    gKeyObj.vtable = gKeyVt;
}

struct C14 {
    C13 c;       // seed, flag A, cell, X, throw point, a1 (the ServiceCell value), a2, R + 4, holder 2's answer
    uint32_t a3; // slot 14's third argument (0 for slot 12)
    int key;     // index into kKeyTexts
    uint32_t cache5;
};

void Setup14(difftest::Context &ctx, const C14 &k)
{
    Setup13(ctx, k.c);
    gKeyIndex = k.key;
    gCache5Answer = k.cache5;
    gCache6Calls = 0;
    *ctx.image.At<void *>(kKeyServiceGlobal) = &gKeyObj;
}

template <typename Fn> Snap13 Capture14(difftest::Context &ctx, const C14 &k, Fn call)
{
    Setup14(ctx, k);
    Snap13 s;
    try {
        s.ret = call(gObj + 0x30);
    } catch (int) {
        s.escaped = true; // only the throwing cases expect this
    }
    std::memcpy(s.obj, gObj, sizeof(s.obj));
    s.log = gLog;
    return s;
}

bool SameSnap(const Snap13 &a, const Snap13 &b)
{
    return a.ret == b.ret && a.escaped == b.escaped && std::memcmp(a.obj, b.obj, sizeof(a.obj)) == 0 && SameEvents(a.log, b.log);
}

} // namespace

// 0xa3ca00 (`ret 0xc`). Over flag A, the cell (empty or 7), the base's X, the throw point (every call of the
// function, the holders and the key), a3 null or not, the key text, and the cache answer. The return (al), the object
// bytes and the call log must match. Both paths to a non-zero result must be reached.
DIFF_TEST(sg_open_assign_and_submit, 0xa3ca00)
{
    GlobalsGuard globalsGuard(ctx);
    R14Init();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *, void *)>(0xa3ca00);
    std::mt19937 gen(0x14ca00);
    int escapes = 0, submitted = 0, rejected = 0;
    uint32_t seed = 1400;
    for (uint8_t flagA : {uint8_t{0}, uint8_t{1}})
        for (uint32_t cell : {0xffffffffu, 0x7u})
            for (bool xSet : {false, true})
                for (int throwAt = kNoThrow; throwAt <= kThrowCache6Assign; ++throwAt)
                    for (bool a3Null : {false, true})
                        for (int rep = 0; rep < 2; ++rep) {
                            C14 k{C13{seed++, flagA, cell, xSet, throwAt, gen(), gen(), gen(), gen()}, a3Null ? 0u : gen(),
                                  static_cast<int>(gen() % std::size(kKeyTexts)), gen()};
                            Snap13 a = Capture14(ctx, k, [&](void *s) {
                                return orig(s, k.c.a1, Ptr(k.c.a2), Ptr(k.a3));
                            });
                            Snap13 o = Capture14(ctx, k, [&](void *s) {
                                return reinterpret_cast<cSGSecondary *>(s)->OpenAssignAndSubmit(k.c.a1, Ptr(k.c.a2), Ptr(k.a3));
                            });
                            ++ctx.cases;
                            if (a.escaped) ++escapes;
                            else if (a.ret) ++submitted;
                            else ++rejected;
                            if (!SameSnap(a, o)) {
                                ctx.Fail("OpenAssignAndSubmit flag %u cell %08x x %d throw %d a3 %08x: original ret %02x "
                                         "(escaped %d), ours %02x (escaped %d); %zu vs %zu calls; object %s",
                                         flagA, cell, xSet, throwAt, k.a3, a.ret, a.escaped, o.ret, o.escaped, a.log.size(),
                                         o.log.size(), std::memcmp(a.obj, o.obj, sizeof(a.obj)) ? "differs" : "same");
                            }
                        }
    if (escapes == 0) ctx.Fail("OpenAssignAndSubmit: no case threw in the original");
    if (submitted == 0) ctx.Fail("OpenAssignAndSubmit: no case reached the second holder's result");
    if (rejected == 0) ctx.Fail("OpenAssignAndSubmit: no case took an early return");
}

// 0xa3cbf0 (`ret 8`): the same over flag A, the cell, X, the throw point (a2 takes the place of a3 and the base's
// slot 15 argument; there is no primary slot 9 call), a2 null or not, and the key text.
DIFF_TEST(sg_assign_and_submit, 0xa3cbf0)
{
    GlobalsGuard globalsGuard(ctx);
    R14Init();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void *)>(0xa3cbf0);
    std::mt19937 gen(0x13cbf0);
    int escapes = 0, submitted = 0, rejected = 0;
    uint32_t seed = 1500;
    for (uint8_t flagA : {uint8_t{0}, uint8_t{1}})
        for (uint32_t cell : {0xffffffffu, 0x7u})
            for (bool xSet : {false, true})
                for (int throwAt = kNoThrow; throwAt <= kThrowCache6Assign; ++throwAt)
                    for (bool a2Null : {false, true})
                        for (int rep = 0; rep < 2; ++rep) {
                            C14 k{C13{seed++, flagA, cell, xSet, throwAt, gen(), a2Null ? 0u : gen(), gen(), gen()}, 0u,
                                  static_cast<int>(gen() % std::size(kKeyTexts)), gen()};
                            Snap13 a = Capture14(ctx, k, [&](void *s) {
                                return orig(s, k.c.a1, Ptr(k.c.a2));
                            });
                            Snap13 o = Capture14(ctx, k, [&](void *s) {
                                return reinterpret_cast<cSGSecondary *>(s)->AssignAndSubmit(k.c.a1, Ptr(k.c.a2));
                            });
                            ++ctx.cases;
                            if (a.escaped) ++escapes;
                            else if (a.ret) ++submitted;
                            else ++rejected;
                            if (!SameSnap(a, o)) {
                                ctx.Fail("AssignAndSubmit flag %u cell %08x x %d throw %d a2 %08x: original ret %02x "
                                         "(escaped %d), ours %02x (escaped %d); %zu vs %zu calls; object %s",
                                         flagA, cell, xSet, throwAt, k.c.a2, a.ret, a.escaped, o.ret, o.escaped, a.log.size(),
                                         o.log.size(), std::memcmp(a.obj, o.obj, sizeof(a.obj)) ? "differs" : "same");
                            }
                        }
    if (escapes == 0) ctx.Fail("AssignAndSubmit: no case threw in the original");
    if (submitted == 0) ctx.Fail("AssignAndSubmit: no case reached the second holder's result");
    if (rejected == 0) ctx.Fail("AssignAndSubmit: no case took an early return");
}
