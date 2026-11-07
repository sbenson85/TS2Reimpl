#pragma once
// Differential tests for the SimAntics primitive handlers of src/simantics/PrimMid.h.
//
// The handlers reach everything through virtual calls. Each test world is a set of fake objects (a vtable
// pointer each) whose slots are recorders: every call logs (object, slot, arguments) and returns a scripted
// value, so the original and ours are compared on the calls made, in order, with their arguments, on the
// thrown code, the return value and the memory the handlers write (data cells, counters, out pointers).
// World pointers in arguments are normalised to offsets, so the two worlds (original, ours) compare equal.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZAutoRef.h"
#include "simantics/PrimMid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <limits>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

struct Fake {
    void **vt;
    uint32_t pad;
};
constexpr int kFakes = 14;
constexpr int kSlots = 512;

struct World {
    alignas(16) PrimMidObject obj; // 0x4c bytes of the object as the handlers see it
    uint8_t objTail[0x40];         // the rest of the (opaque) object
    void *vtThis[kSlots];
    void *vtSec[64];
    Fake fk[kFakes];
    void *vtFk[kFakes][kSlots];
    Fake sub[4];             // virtual-base subobjects of fake objects (Prim23)
    void *vtSub[4][64];
    int32_t vbtab[4][2];
    int16_t counters[256];
    int16_t cells[16];
    float floatCell;
    SimFixedPoint pos[6];
    float rank[16];
    uint8_t bytes[0x40];
    uint32_t scratch[8];
    uint8_t param[0x20];
    uint32_t canary;
};

// ---- scripted returns ----------------------------------------------------------------------------------------------

struct RetEntry {
    uint32_t id;
    int slot;
    bool isObject;
    uint32_t value; // the value, or the offset of a world object (0xffffffff: null)
};

World *gW = nullptr;
uint32_t gSeed = 0;
World &SharedWorld();
// Test-only (MID_INJECT_WORLD=1 in the environment): one in eight unscripted fake returns becomes a word inside the
// shared world (its fixed address plus the random word folded into its size: a value, the same on both sides, that
// happens to look like a world pointer), the case that used to need a retry (see SharedWorld).
bool gInjectWorld = std::getenv("MID_INJECT_WORLD") != nullptr;
uint32_t gInjected = 0; // words injected (both sides), printed by Compare
std::vector<RetEntry> gRets;
std::vector<uint32_t> gTrace;

// Exception injection: in some seeds the Nth fake call throws a short, so the handlers' cleanup (references
// released when an error leaves the frame) is compared too. Both sides see the same N.
uint32_t gThrowAt = 0;
uint32_t gHitCount = 0;
void Tick()
{
    if (++gHitCount == gThrowAt) throw static_cast<short>(0x66);
}

uint32_t Off(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gW));
}

uint32_t Norm(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (gW && v >= base && v < base + sizeof(World)) return 0x40000000u + static_cast<uint32_t>(v - base);
    return v;
}

uint32_t Mix(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

// Return script helpers (offsets are relative to the world, so both worlds of a seed use the same script).
void RetValue(const void *objInWorld, int slot, uint32_t value, World &w)
{
    gRets.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(objInWorld) - reinterpret_cast<uintptr_t>(&w)), slot, false, value});
}
void RetObject(const void *objInWorld, int slot, const void *target, World &w)
{
    gRets.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(objInWorld) - reinterpret_cast<uintptr_t>(&w)), slot, true,
                     target ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(&w)) : 0xffffffffu});
}

std::vector<uint32_t> gLookups;
size_t gLookupPos = 0;
void LookupObj(const void *target, World &w)
{
    gLookups.push_back(target ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(&w)) : 0xffffffffu);
}

uint32_t Scripted(void *self, int slot)
{
    const uint32_t id = Off(self);
    for (const RetEntry &e : gRets) {
        if (e.id == id && e.slot == slot) {
            if (!e.isObject) return e.value;
            return e.value == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e.value);
        }
    }
    const uint32_t v = Mix(gSeed, id, static_cast<uint32_t>(slot));
    if (gInjectWorld && (v >> 29) == 0) {
        ++gInjected;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SharedWorld())) + v % static_cast<uint32_t>(sizeof(World));
    }
    return v;
}

uint32_t Hit(void *self, int slot, int n, const uint32_t *a)
{
    if (slot != 2) Tick(); // Release (slot 2) runs while errors unwind and must not throw
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) gTrace.push_back(Norm(a[i]));
    return Scripted(self, slot);
}

// ---- generic thunks: slot S taking N stack arguments (all the handlers' calls are __thiscall) ------------------

template <int S, int N> struct Th;
template <int S> struct Th<S, 0> {
    static uint32_t __fastcall F(void *self, void *) { return Hit(self, S, 0, nullptr); }
};
template <int S> struct Th<S, 1> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1)
    {
        const uint32_t a[] = {a1};
        return Hit(self, S, 1, a);
    }
};
template <int S> struct Th<S, 2> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2)
    {
        const uint32_t a[] = {a1, a2};
        return Hit(self, S, 2, a);
    }
};
template <int S> struct Th<S, 3> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
    {
        const uint32_t a[] = {a1, a2, a3};
        return Hit(self, S, 3, a);
    }
};
template <int S> struct Th<S, 4> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
    {
        const uint32_t a[] = {a1, a2, a3, a4};
        return Hit(self, S, 4, a);
    }
};
template <int S> struct Th<S, 5> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5};
        return Hit(self, S, 5, a);
    }
};
template <int S> struct Th<S, 6> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5, a6};
        return Hit(self, S, 6, a);
    }
};

constexpr int kThunkSlots = 320;
void *gThunk[kThunkSlots][7];

template <int... S> void FillThunks(std::integer_sequence<int, S...>)
{
    ((gThunk[S][0] = reinterpret_cast<void *>(&Th<S, 0>::F), gThunk[S][1] = reinterpret_cast<void *>(&Th<S, 1>::F),
      gThunk[S][2] = reinterpret_cast<void *>(&Th<S, 2>::F), gThunk[S][3] = reinterpret_cast<void *>(&Th<S, 3>::F),
      gThunk[S][4] = reinterpret_cast<void *>(&Th<S, 4>::F), gThunk[S][5] = reinterpret_cast<void *>(&Th<S, 5>::F),
      gThunk[S][6] = reinterpret_cast<void *>(&Th<S, 6>::F)),
     ...);
}

void EnsureThunks()
{
    static bool done = false;
    if (!done) {
        FillThunks(std::make_integer_sequence<int, kThunkSlots>());
        done = true;
    }
}

void Use(void **vtable, int slot, int args)
{
    vtable[slot] = gThunk[slot][args];
}

// ---- special thunks ------------------------------------------------------------------------------------------------

// The operand reader (secondary slot 7, seven stack arguments): logs, fills the destination (a SimDataRef the handler
// holds on its stack: logged as present/absent) from the script, returns ax from the script.
struct ReadScript {
    bool hasTarget = false;
    int cell = 0;
    int16_t min = -32768, max = 32767;
};
ReadScript gRead;

uint32_t __fastcall Read7(void *self, void *, uint32_t scope, uint32_t id, SimDataRef *dest, float **fdest, uint32_t *a5,
                          uint32_t a6, uint32_t a7)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8 | 7u);
    gTrace.insert(gTrace.end(), {scope, id, dest ? 1u : 0u, fdest ? 1u : 0u, a5 ? 1u : 0u, a6, a7});
    if (dest) {
        dest->mLength = 1;
        dest->mIndex = 0;
        dest->mPtr = gRead.hasTarget ? &gW->cells[gRead.cell] : nullptr;
        dest->mMin = gRead.min;
        dest->mMax = gRead.max;
    }
    return Scripted(self, 7);
}

// The frame's slot 35: a pointer to counter `index`.
int16_t *__fastcall Frame35(void *self, void *, int32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(35u << 8 | 1u);
    gTrace.push_back(static_cast<uint32_t>(index));
    return &gW->counters[static_cast<uint32_t>(index) & 255];
}

// Slot 128 of the first object of DistanceTo: (other object, float), the float result in st0.
float __fastcall Dist128(void *self, void *, uint32_t other, uint32_t floatBits)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(128u << 8 | 2u);
    gTrace.push_back(Norm(other));
    gTrace.push_back(floatBits);
    const uint32_t bits = Scripted(self, 128);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// A lookup slot (slot 29 of a table, argument: an id): returns the next scripted object (offset, or 0xffffffff:
// null), whatever the id; the id is logged. The script is the same for both worlds of a seed.
uint32_t __fastcall Lookup29(void *self, void *, int32_t id)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(29u << 8 | 1u);
    gTrace.push_back(static_cast<uint32_t>(id));
    const uint32_t e = gLookupPos < gLookups.size() ? gLookups[gLookupPos++] : 0xffffffffu;
    return e == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e);
}

// Slot 33: a pointer to a position.
uint32_t __fastcall Pos33(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(33u << 8);
    return Scripted(self, 33);
}

// Slot 0 (QueryInterface-style): (iid, void **out); writes the scripted object to *out (null: nothing found).
uint32_t __fastcall Qi0(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const uint32_t v = Scripted(self, 0);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(v));
    return 1;
}

// Prim14's slot 248: five stack arguments; the fourth is the frame pointer with its low byte replaced.
uint32_t gFrameHigh = 0;
uint32_t __fastcall Slot248(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(248u << 8 | 5u);
    gTrace.insert(gTrace.end(), {Norm(a1), Norm(a2), a3, a4 & 0xff, (a4 & ~0xffu) == gFrameHigh ? 1u : 0u, a5});
    return Scripted(self, 248);
}

// OnTimer's slot 301: (count, id, flags, who, n, pointer): the pointer is either world memory or the handler's
// three local words (logged by value).
uint32_t __fastcall Timer301(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t n, const void *p)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(301u << 8 | 6u);
    gTrace.insert(gTrace.end(), {a1, a2, a3, a4, n});
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (reinterpret_cast<uintptr_t>(p) >= base && reinterpret_cast<uintptr_t>(p) < base + sizeof(World)) {
        gTrace.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))));
    } else {
        const int16_t *w = static_cast<const int16_t *>(p);
        gTrace.insert(gTrace.end(), {0x1000u, static_cast<uint32_t>(static_cast<uint16_t>(w[0])), static_cast<uint32_t>(static_cast<uint16_t>(w[1])),
                                     static_cast<uint32_t>(static_cast<uint16_t>(w[2]))});
    }
    return Scripted(self, 301);
}

// ---- world construction ---------------------------------------------------------------------------------------------

void Base(World &w, uint32_t seed, std::mt19937 &r)
{
    EnsureThunks();
    // Zero the world, then give the plain-data parts random bytes (the vtable arrays are the bulk and stay zero).
    std::memset(&w, 0, sizeof(World));
    auto fill = [&](void *p, size_t n) {
        for (size_t i = 0; i < n; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(r());
    };
    fill(&w.obj, sizeof(w.obj));
    fill(w.objTail, sizeof(w.objTail));
    fill(w.fk, sizeof(w.fk));
    fill(w.cells, sizeof(w.cells));
    fill(&w.floatCell, sizeof(w.floatCell));
    fill(w.pos, sizeof(w.pos));
    fill(w.rank, sizeof(w.rank));
    fill(w.bytes, sizeof(w.bytes));
    fill(w.param, sizeof(w.param));
    std::memset(w.vtThis, 0, sizeof(w.vtThis));
    std::memset(w.vtSec, 0, sizeof(w.vtSec));
    std::memset(w.vtFk, 0, sizeof(w.vtFk));
    std::memset(w.vtSub, 0, sizeof(w.vtSub));
    for (int i = 0; i < 4; ++i) w.sub[i].vt = w.vtSub[i];
    w.obj.mVTable = w.vtThis;
    w.obj.mSecondary = w.vtSec;
    for (int i = 0; i < kFakes; ++i) w.fk[i].vt = w.vtFk[i];
    w.obj.mContext = &w.fk[0];
    w.obj.mOwner = &w.fk[1];
    for (int16_t &c : w.counters) c = 0;
    // Words that may hold world pointers afterwards start as zero (from when the two sides ran in two worlds at
    // different addresses; with one shared world a random word here would normalise the same on both sides).
    for (uint32_t &s : w.scratch) s = 0;
    gRets.clear();
    gLookups.clear();
    gLookupPos = 0;
    gSeed = seed;
    gThrowAt = (r() % 8 == 0) ? 1 + r() % 6 : 0;
    gRead = ReadScript();
    w.canary = 0xC0DEC0DEu;
}

// Set by tests that need global game state to point into the world being run (called before each run).
std::function<void(World &)> gBeforeRun;

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
};

// The state captured after a run: counters, cells, scratch words, the float cell, the global rank table pointer.
void Capture(World &w, Outcome &o)
{
    for (int16_t c : w.counters) o.state.push_back(static_cast<uint16_t>(c));
    for (int16_t c : w.cells) o.state.push_back(static_cast<uint16_t>(c));
    uint32_t f;
    std::memcpy(&f, &w.floatCell, 4);
    o.state.push_back(f);
    for (uint32_t s : w.scratch) o.state.push_back(Norm(s));
    o.state.push_back(w.canary);
    o.state.push_back(Norm(*reinterpret_cast<uint32_t *>(GameAddress<void>(0x10dbf50))));
}

template <typename Call> Outcome Run(World &w, Call call)
{
    gW = &w;
    gLookupPos = 0;
    gHitCount = 0;
    gTrace.clear();
    if (gBeforeRun) gBeforeRun(w);
    Outcome o;
    try {
        o.ret = call(w);
    } catch (short code) {
        o.thrown = code;
    }
    o.trace = gTrace;
    Capture(w, o);
    gW = nullptr;
    return o;
}

struct Stats {
    uint32_t throws = 0, returns = 0;
    uint32_t byRet[8] = {};
};

struct Trial {
    bool bad = false;
    Outcome oa, ob;
};

// Both sides run in this one world, built afresh from the seed before each run, so they see it at the same address
// (as in tests/simantics_prims_aa.h, lane EH). Why: the trace and the captured state hold words that may be pointers
// into the world (Norm turns those into world offsets) next to scripted random words (Mix answers, random cells). With
// two worlds at different addresses, a random word that happened to fall inside one world's range was normalised on
// that side only, and the seed differed although nothing diverged; the old retry on two more worlds made that rarer but
// not impossible. At one address Norm is the same function on both sides, so equal words stay equal and different words
// stay different: no comparison is weakened (a word the original logs as a world pointer must still be that same pointer
// in ours), and none depends on where the world sits. Prim14's frame check (gFrameHigh) is the same on both sides too.
// The outcome copies everything it compares out of the world before it is rebuilt. Base() zeroes the whole world first,
// so a rebuild leaves nothing of the previous run.
World &SharedWorld()
{
    static World *const w = static_cast<World *>(::operator new(sizeof(World))); // (raw memory, kept for the process)
    return *w;
}

template <typename Build, typename Orig, typename Ours>
Trial Try(World &w, uint32_t seed, Build build, Orig original, Ours ours, bool compareRet)
{
    Trial t;
    gFrameHigh = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.fk[1])) & ~0xffu;
    build(w, seed);
    t.oa = Run(w, [&](World &x) { return original(&x.obj, &x.fk[1], x.param); });
    build(w, seed);
    t.ob = Run(w, [&](World &x) { return ours(&x.obj, &x.fk[1], x.param); });
    t.bad = t.oa.thrown != t.ob.thrown || t.oa.trace != t.ob.trace || t.oa.state != t.ob.state ||
            (compareRet && t.oa.thrown < 0 && t.oa.ret != t.ob.ret);
    return t;
}

// Runs every seed on the shared world (see SharedWorld); a mismatch is reported as it is, without a retry.
template <typename Build, typename Orig, typename Ours>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Build build, Orig original, Ours ours, Stats &st,
             bool compareRet = true)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Trial t = Try(SharedWorld(), seed, build, original, ours, compareRet);
        ++ctx.cases;
        const Outcome &oa = t.oa, &ob = t.ob;
        if (oa.thrown >= 0) ++st.throws;
        else {
            ++st.returns;
            if (oa.ret < 8) ++st.byRet[oa.ret];
        }
        if (t.bad) {
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            ctx.Fail("%s seed %u: (original/ours) thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state %s", name, seed, oa.thrown,
                     ob.thrown, oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k, k < oa.trace.size() ? oa.trace[k] : 0xdeadu,
                     k < ob.trace.size() ? ob.trace[k] : 0xdeadu, oa.state == ob.state ? "same" : "DIFFERENT");
        }
    }
    if (gInjectWorld) std::printf("      %s MID_INJECT_WORLD: %u world-range words returned by fakes\n", name, gInjected);
    gInjected = 0;
}

using OrigFn = uint32_t(__thiscall *)(void *, void *, const void *);

template <typename Build, typename Ours>
Stats RunHandler(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t seeds, Build build, Ours ours, bool compareRet = true)
{
    auto original = ctx.Original<OrigFn>(rva);
    Stats st;
    Compare(ctx, name, seeds, build, [&](PrimMidObject *o, void *f, const void *p) { return original(o, f, p); },
            [&](PrimMidObject *o, void *f, const void *p) { return ours(o, f, static_cast<const PrimParamView *>(p)); }, st, compareRet);
    return st;
}

uint32_t Pick(std::mt19937 &r, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + r() % values.size());
}

void SetParam(World &w, std::mt19937 &r)
{
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
}

void PutWord(World &w, size_t off, uint16_t v)
{
    std::memcpy(w.param + off, &v, 2);
}

uint32_t FloatBits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

float RandomFloat(std::mt19937 &r)
{
    switch (r() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return static_cast<float>(static_cast<int>(r() % 2001) - 1000) / 8.0f;
    case 3: return static_cast<float>(static_cast<int>(r() % 200001) - 100000) / 100.0f;
    case 4: return (r() % 2) ? 3.0e9f : -3.0e9f;
    case 5: return (r() % 2) ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    case 6: return static_cast<float>(static_cast<int>(r() % 70000) - 35000) / 100.0f; // x100 lands near 16-bit limits
    default: {
        uint32_t b = r();
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    }
}

int16_t Edge16(std::mt19937 &r)
{
    const int16_t e[] = {0, 1, -1, 2, 3, 7, 8, 9, 10, 100, 360, 359, 32767, -32768, 12345, -12345};
    return (r() % 3 == 0) ? static_cast<int16_t>(r()) : e[r() % 16];
}

} // namespace
