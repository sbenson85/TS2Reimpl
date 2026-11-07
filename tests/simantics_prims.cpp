// Differential tests for the SimAntics primitive handlers (src/simantics/PrimHandlers.h).
//
// The handlers talk to the object only through virtual calls: slot 7 of the subobject at +4 (the operand
// reader) and slot 55 of the object at +0x48 (the random generator's owner). Each test world carries copies
// of those vtables whose slots are recorders that serve scripted values, so the calls made, their arguments
// (in full, including the trailing zero arguments), the results written through the pointers and the
// generator state afterwards are compared between the original and ours.
#include "difftest.h"
#include "rz/cRZRandom.h"
#include "simantics/PrimHandlers.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using std::uint16_t;
using std::uint32_t;

struct World {
    alignas(16) PrimObject obj;
    void *vtPrimary[4];
    void *vtSub[8];
    void *vtOwner[56];
    void *ownerVTable;  // the owner object is just a vtable pointer
    cRZRandom rng;
    // Prim07's objects: each "object" is just a vtable pointer.
    void *vt40[27];
    void *vtFinder[30];
    void *vtFrame[13];
    void *obj40VTable;
    void *finderVTable;
    void *frameVTable;
    void *target;
    int16_t shortSlot;
    float floatSlot;
    uint16_t canary;
    PrimParamView param;
};

// What the fakes serve for this world (set by Build; the same in both worlds of a seed).
struct Script {
    bool hasTarget = false;     // the destination reader fills a 16-bit target
    bool hasFloat = false;      // ... and a float destination
    int16_t min = -32768, max = 32767;
    uint32_t boundRet = 0;      // eax of the bound read (only ax is used)
    uint32_t junkRet = 0;       // eax of the destination read (ignored by the handler)
    uint32_t unknown0 = 0;
    uint32_t frameRet = 0;      // eax of the frame's slot 12 (only ax is used)
    bool findFails = false;     // the finder's slot 29 returns null
};

Script gScript;
World *gW = nullptr;
std::vector<uint32_t> gTrace;

// Slot 7: the operand reader, __thiscall ret 0x1c (seven stack arguments).
uint32_t __fastcall Read7(void *self, void *, uint32_t scope, uint32_t id, PrimShortDest *dest, float **fdest, uint32_t a5,
                          uint32_t a6, uint32_t a7)
{
    gTrace.insert(gTrace.end(), {7u, scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5, a6, a7});
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) - reinterpret_cast<uintptr_t>(gW)));
    if (dest) {
        dest->mUnknown0 = gScript.unknown0;
        dest->mTarget = gScript.hasTarget ? &gW->shortSlot : nullptr;
        dest->mMin = gScript.min;
        dest->mMax = gScript.max;
        if (fdest) *fdest = gScript.hasFloat ? &gW->floatSlot : nullptr; // the real one zeroes it first
        return gScript.junkRet;
    }
    return gScript.boundRet;
}

// Slot 55: no arguments, returns the generator.
cRZRandom *__fastcall Owner55(void *self, void *)
{
    gTrace.insert(gTrace.end(), {55u, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) - reinterpret_cast<uintptr_t>(gW))});
    return &gW->rng;
}

uint32_t Id(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gW));
}

// Prim07's chain: slot 26 of the object at +0x40, slot 29 of what that returns, slot 12 of the frame.
void *__fastcall Obj40Slot26(void *self, void *)
{
    gTrace.insert(gTrace.end(), {26u, Id(self)});
    return &gW->finderVTable;
}

void *__fastcall FinderSlot29(void *self, void *, int32_t x)
{
    gTrace.insert(gTrace.end(), {29u, Id(self), static_cast<uint32_t>(x)});
    return gScript.findFails ? nullptr : &gW->target;
}

uint32_t __fastcall FrameSlot12(void *self, void *)
{
    gTrace.insert(gTrace.end(), {12u, Id(self)});
    return gScript.frameRet;
}

void Build(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2246822519u + 11);
    uint8_t *raw = reinterpret_cast<uint8_t *>(&w);
    for (size_t i = 0; i < sizeof(World); ++i) raw[i] = static_cast<uint8_t>(r());
    for (auto &s : w.vtPrimary) s = nullptr;
    for (auto &s : w.vtSub) s = nullptr;
    for (auto &s : w.vtOwner) s = nullptr;
    w.vtSub[7] = reinterpret_cast<void *>(&Read7);
    w.vtOwner[55] = reinterpret_cast<void *>(&Owner55);
    w.ownerVTable = w.vtOwner;
    w.obj.mVTable = w.vtPrimary;
    w.obj.mTreeSimVTable = w.vtSub;
    w.obj.mRandomOwner = &w.ownerVTable;
    for (auto &s : w.vt40) s = nullptr;
    for (auto &s : w.vtFinder) s = nullptr;
    for (auto &s : w.vtFrame) s = nullptr;
    w.vt40[26] = reinterpret_cast<void *>(&Obj40Slot26);
    w.vtFinder[29] = reinterpret_cast<void *>(&FinderSlot29);
    w.vtFrame[12] = reinterpret_cast<void *>(&FrameSlot12);
    w.obj40VTable = w.vt40;
    w.finderVTable = w.vtFinder;
    w.frameVTable = w.vtFrame;
    w.obj.mSubobject40 = &w.obj40VTable;
    // Seeds, including the edges.
    const uint32_t edges[] = {0u, 1u, 0x3039u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    uint32_t s = (seed % 3 == 0) ? edges[(seed / 3) % 6] : static_cast<uint32_t>(r());
    w.rng.mIntegerSeed = s;
    w.rng.mRealSeed = static_cast<int32_t>(s);
    w.rng.mUnknown = static_cast<uint32_t>(r());
    w.shortSlot = 0x1234;
    w.floatSlot = 123.5f;
    w.canary = 0xA55A;
    // The operand words: every scope/id is distinguishable.
    w.param.mWord4 = static_cast<uint16_t>(r());
    w.param.mWord6 = static_cast<uint16_t>(r());
    w.param.mWord8 = static_cast<uint16_t>(r());
    w.param.mWordA = static_cast<uint16_t>(r());

    Script sc;
    sc.hasTarget = r() % 3 != 0;
    sc.hasFloat = r() % 2 == 0;
    switch (r() % 5) {
    case 0: sc.min = -32768; sc.max = 32767; break;
    case 1: sc.min = 0; sc.max = static_cast<int16_t>(r() % 40); break;       // tight: draws often fall outside
    case 2: sc.min = static_cast<int16_t>(r()); sc.max = static_cast<int16_t>(r()); break; // may be inverted
    case 3: sc.min = 1; sc.max = 32767; break;                                   // excludes 0
    default: sc.min = -5; sc.max = 5; break;
    }
    const int32_t bounds[] = {0, 1, -1, 2, 3, 7, 10, 100, 1000, 32767, -32768, -2, -100, 12345};
    int16_t bound = (r() % 4 == 0) ? static_cast<int16_t>(r()) : static_cast<int16_t>(bounds[r() % 14]);
    uint32_t upper = (r() % 2) ? (static_cast<uint32_t>(r()) & 0xffff0000u) : 0u; // junk above ax
    sc.boundRet = upper | static_cast<uint16_t>(bound);
    sc.junkRet = r();
    sc.unknown0 = r();
    sc.frameRet = (r() % 2) ? static_cast<uint32_t>(r()) : (static_cast<uint32_t>(r()) & 0xffff0000u) | (r() % 4);
    sc.findFails = r() % 4 == 0;
    // Selector words of Prim07 (+4 and +6) mostly in range, sometimes not; high bits set sometimes (sign).
    const uint16_t picks[] = {0, 1, 2, 3, 0xffff, 0x8000, 0x0100};
    if (r() % 4 != 0) w.param.mWord4 = picks[r() % 7] & ((r() % 3) ? 0xffff : 3);
    if (r() % 4 != 0) w.param.mWord6 = picks[r() % 7];
    if (r() % 3 == 0) w.param.mWord8 = static_cast<uint16_t>(r() % 4);
    gScript = sc;
}

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1; // thrown short, or -1
    std::vector<uint32_t> trace;
    int16_t shortSlot = 0;
    uint32_t floatBits = 0;
    uint16_t canary = 0;
    cRZRandom rng{};
};

template <typename Call> Outcome Run(World &w, Call call)
{
    gW = &w;
    gTrace.clear();
    Outcome o;
    try {
        o.ret = call(w);
    } catch (short code) {
        o.thrown = code;
    }
    o.trace = gTrace;
    o.shortSlot = w.shortSlot;
    std::memcpy(&o.floatBits, &w.floatSlot, 4);
    o.canary = w.canary;
    o.rng = w.rng;
    gW = nullptr;
    return o;
}

} // namespace

DIFF_TEST(PrimRandom, 0x5a1060)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, const void *)>(0x5a1060);
    uint32_t thrownCount = 0, floatCount = 0, shortCount = 0, zeroCount = 0, nothing = 0;
    for (uint32_t seed = 0; seed < 20000; ++seed) {
        World a, b;
        Build(a, seed);
        Build(b, seed);
        void *frame = reinterpret_cast<void *>(0x12345678);
        Outcome oa = Run(a, [&](World &w) { return original(&w.obj, frame, &w.param); });
        Outcome ob = Run(b, [&](World &w) { return w.obj.Random(frame, &w.param); });
        ++ctx.cases;
        if (oa.thrown >= 0) ++thrownCount;
        else if (gScript.hasTarget) ++shortCount;
        (void)floatCount; (void)zeroCount; (void)nothing;
        bool bad = oa.thrown != ob.thrown || oa.trace != ob.trace || oa.shortSlot != ob.shortSlot ||
                   oa.floatBits != ob.floatBits || oa.canary != ob.canary ||
                   std::memcmp(&oa.rng, &ob.rng, sizeof(cRZRandom)) != 0 || (oa.thrown < 0 && oa.ret != ob.ret);
        if (bad) {
            ctx.Fail("seed %u: thrown %d/%d ret 0x%x/0x%x short %d/%d float 0x%x/0x%x rng 0x%x/0x%x trace %zu/%zu", seed, oa.thrown,
                     ob.thrown, oa.ret, ob.ret, oa.shortSlot, ob.shortSlot, oa.floatBits, ob.floatBits, oa.rng.mIntegerSeed,
                     ob.rng.mIntegerSeed, oa.trace.size(), ob.trace.size());
        }
    }
    if (thrownCount < 500 || shortCount < 500) ctx.Fail("coverage: %u throws, %u short stores", thrownCount, shortCount);
}

// The three small helpers every handler uses on its destination.
DIFF_TEST(PrimShortDest_Construct, 0x57c8d0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x57c8d0);
    std::mt19937 r(5);
    for (int i = 0; i < 200; ++i) {
        PrimShortDest a, b;
        uint8_t *pa = reinterpret_cast<uint8_t *>(&a), *pb = reinterpret_cast<uint8_t *>(&b);
        for (size_t k = 0; k < sizeof(a); ++k) pa[k] = pb[k] = static_cast<uint8_t>(r());
        void *ra = original(&a);
        void *rb = b.Construct();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || ra != &a || rb != &b) ctx.Fail("construct %d differs", i);
    }
}

DIFF_TEST(PrimShortDest_HasTarget, 0x57cc90)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x57cc90);
    int16_t slot = 0;
    for (int i = 0; i < 4; ++i) {
        PrimShortDest d{};
        d.mTarget = (i & 1) ? &slot : nullptr;
        ++ctx.cases;
        // Only al is set by the original (setne al); the rest of eax is the caller's.
        if (static_cast<uint8_t>(original(&d)) != static_cast<uint8_t>(d.HasTarget())) ctx.Fail("has target %d", i);
    }
}

DIFF_TEST(PrimShortDest_Set, 0x57ed80)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(0x57ed80);
    std::mt19937 r(9);
    const int32_t edges[] = {0, 1, -1, 32767, -32768, 32768, -32769, 65535, 65536, 0x7fffffff, INT32_MIN, 0x12345};
    for (int i = 0; i < 20000; ++i) {
        int16_t sa = 0x4321, sb = 0x4321;
        PrimShortDest a{}, b{};
        bool target = r() % 5 != 0;
        a.mTarget = target ? &sa : nullptr;
        b.mTarget = target ? &sb : nullptr;
        a.mMin = b.mMin = (r() % 3 == 0) ? int16_t(-32768) : static_cast<int16_t>(r() % 200 - 100);
        a.mMax = b.mMax = (r() % 3 == 0) ? int16_t(32767) : static_cast<int16_t>(r() % 200 - 100);
        int32_t v = (i < 12) ? edges[i] : (r() % 2 ? static_cast<int32_t>(r()) : static_cast<int32_t>(r() % 300) - 150);
        uint32_t ra = original(&a, v);
        bool rb = b.Set(v);
        ++ctx.cases;
        // The failure paths set only al (xor al, al), the success path mov al, 1.
        if (static_cast<uint8_t>(ra) != static_cast<uint8_t>(rb) || sa != sb)
            ctx.Fail("set %d (min %d max %d): ret %u/%u slot %d/%d", v, a.mMin, a.mMax, ra & 0xff, rb, sa, sb);
    }
}

namespace {

// Runs a handler on both sides over many seeds and compares results, the thrown code, the calls made
// and the state they leave. The frame is the world's frame object.
template <typename Ours>
void CompareHandler(difftest::Context &ctx, uint32_t rva, uint32_t seeds, Ours ours, uint32_t *throws, uint32_t *returns)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, const void *)>(rva);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        World a, b;
        Build(a, seed);
        Build(b, seed);
        Outcome oa = Run(a, [&](World &w) { return original(&w.obj, &w.frameVTable, &w.param); });
        Outcome ob = Run(b, [&](World &w) { return ours(&w.obj, &w.frameVTable, &w.param); });
        ++ctx.cases;
        if (oa.thrown >= 0) ++*throws;
        else ++*returns;
        bool bad = oa.thrown != ob.thrown || oa.trace != ob.trace || oa.shortSlot != ob.shortSlot ||
                   oa.floatBits != ob.floatBits || oa.canary != ob.canary ||
                   std::memcmp(&oa.rng, &ob.rng, sizeof(cRZRandom)) != 0 || (oa.thrown < 0 && oa.ret != ob.ret);
        if (bad)
            ctx.Fail("seed %u: thrown %d/%d ret 0x%x/0x%x trace %zu/%zu", seed, oa.thrown, ob.thrown, oa.ret, ob.ret,
                     oa.trace.size(), ob.trace.size());
    }
}

} // namespace

DIFF_TEST(Prim07, 0x5a86b0)
{
    uint32_t throws = 0, returns = 0;
    CompareHandler(ctx, 0x5a86b0, 20000, [](PrimObject *o, void *f, const PrimParamView *p) { return o->Prim07(f, p); }, &throws, &returns);
    if (throws < 1000 || returns < 1000) ctx.Fail("coverage: %u throws, %u returns", throws, returns);
}

DIFF_TEST(Prim0F, 0x5a7bb0)
{
    uint32_t throws = 0, returns = 0;
    CompareHandler(ctx, 0x5a7bb0, 20000, [](PrimObject *o, void *f, const PrimParamView *p) { return o->Prim0F(f, p); }, &throws, &returns);
    if (throws < 1000 || returns < 1000) ctx.Fail("coverage: %u throws, %u returns", throws, returns);
}
