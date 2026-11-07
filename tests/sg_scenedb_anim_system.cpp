// Differential tests for nGZSceneDBAnim::cSceneDBAnimSystem (src/sg/cSceneDBAnimSystem.h). Each trial builds one
// random state twice: the original runs on the first side, ours on the second. A side is an object with canaries
// around it and its two lists. List pointers are compared as offsets from their own list, so both sides compare
// equal; the object, the lists and the canaries are compared afterwards.
//
// The drain loops call slot 16 through a fake vtable that records its argument and decrements a counter, so both
// sides see the same callees. FindVirtualByValue keeps the game's primary vtable: its slots 8 and 32 are the
// original list accessors, and its list B holds fake element objects whose slot 13 records nothing and returns
// the element's value.
#include "difftest.h"
#include "sg/cSceneDBAnimSystem.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Sys = nGZSceneDBAnim::cSceneDBAnimSystem;

constexpr int kObjectSize = 0x100;
constexpr int kCap = 16;

// Single-inheritance member pointers are plain code addresses.
template <typename Fn, typename Pmf> Fn AsFn(Pmf pmf)
{
    static_assert(sizeof(Pmf) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return reinterpret_cast<Fn>(p);
}

void StoreWord(uint8_t *at, uintptr_t v)
{
    std::memcpy(at, &v, sizeof(v));
}

uintptr_t LoadWord(const uint8_t *at)
{
    uintptr_t v;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

// The state both sides start from.
struct State {
    uint8_t object[kObjectSize];
    uint32_t listA[kCap];
    uint32_t listB[kCap];
};

// One side: canaries, the object, and the two lists.
struct Side {
    uint8_t front[32];
    uint8_t object[kObjectSize];
    uint32_t listA[kCap];
    uint32_t listB[kCap];
    uint8_t back[32];
};

State RandomState(std::mt19937 &rng)
{
    State st;
    for (auto &b : st.object) b = static_cast<uint8_t>(rng());
    for (auto &v : st.listA) v = static_cast<uint32_t>(rng());
    for (auto &v : st.listB) v = static_cast<uint32_t>(rng());
    return st;
}

// Builds a side: the vtable at +0; list A (begin, end, capacity at +0x20, +0x24, +0x28) with na entries; list B
// (+0x2c, +0x30, +0x34) with nb entries.
void Build(Side &s, const State &st, int na, int nb, const void *vtable)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.object, st.object, sizeof(s.object));
    std::memcpy(s.listA, st.listA, sizeof(s.listA));
    std::memcpy(s.listB, st.listB, sizeof(s.listB));
    StoreWord(s.object, reinterpret_cast<uintptr_t>(vtable));
    StoreWord(s.object + 0x20, reinterpret_cast<uintptr_t>(s.listA));
    StoreWord(s.object + 0x24, reinterpret_cast<uintptr_t>(s.listA + na));
    StoreWord(s.object + 0x28, reinterpret_cast<uintptr_t>(s.listA + kCap));
    StoreWord(s.object + 0x2c, reinterpret_cast<uintptr_t>(s.listB));
    StoreWord(s.object + 0x30, reinterpret_cast<uintptr_t>(s.listB + nb));
    StoreWord(s.object + 0x34, reinterpret_cast<uintptr_t>(s.listB + kCap));
}

// Object bytes and list contents, with each list pointer replaced by its offset from that side's list.
std::vector<uint8_t> Snapshot(const Side &s)
{
    uint8_t obj[kObjectSize];
    std::memcpy(obj, s.object, sizeof(obj));
    const uintptr_t baseA = reinterpret_cast<uintptr_t>(s.listA);
    const uintptr_t baseB = reinterpret_cast<uintptr_t>(s.listB);
    for (int off : {0x20, 0x24, 0x28}) StoreWord(obj + off, LoadWord(s.object + off) - baseA);
    for (int off : {0x2c, 0x30, 0x34}) StoreWord(obj + off, LoadWord(s.object + off) - baseB);
    std::vector<uint8_t> out(obj, obj + sizeof(obj));
    const uint8_t *a = reinterpret_cast<const uint8_t *>(s.listA);
    const uint8_t *b = reinterpret_cast<const uint8_t *>(s.listB);
    out.insert(out.end(), a, a + sizeof(s.listA));
    out.insert(out.end(), b, b + sizeof(s.listB));
    return out;
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

void CheckState(difftest::Context &ctx, const char *name, const Side &a, const Side &b)
{
    if (Snapshot(a) != Snapshot(b)) ctx.Fail("%s: object or list state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
}

// Bits of a float, so NaN and signed zero compare exactly.
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// The game's own primary vtable, as mapped in the image.
const void *GameVTable(difftest::Context &ctx)
{
    return ctx.image.At<void>(Sys::kVTable);
}

// ---------------------------------------------------------------------------------------------------------
// Indexed list accessors (ListAElementAt, ListBElementAt). The index stays inside the 16-entry list: the original
// reads begin[index] without a bound.

void CheckIndexedList(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t (Sys::*pmf)(uint32_t),
                      std::mt19937 &rng, int trials)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(pmf);
    const void *vt = GameVTable(ctx);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t index = static_cast<uint32_t>(rng() % kCap);
        Side a, b;
        Build(a, st, kCap, kCap, vt);
        Build(b, st, kCap, kCap, vt);
        uint32_t ra = orig(a.object, index);
        uint32_t rb = ours(b.object, index);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("%s: index %u original 0x%08x, ours 0x%08x", name, index, ra, rb);
        CheckState(ctx, name, a, b);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Search accessors (FindInListA, FindInListB). Values come from a small set so repeats occur (the first match
// counts), or are arbitrary 32-bit values (usually absent).

void CheckFind(difftest::Context &ctx, const char *name, uint32_t rva, int32_t (Sys::*pmf)(uint32_t), bool onListB,
               std::mt19937 &rng, int trials)
{
    using Fn = int32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(pmf);
    const void *vt = GameVTable(ctx);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        for (auto &v : st.listA) v %= 6;
        for (auto &v : st.listB) v %= 6;
        int n = static_cast<int>(rng() % (kCap + 1));
        uint32_t value = static_cast<uint32_t>(rng() % 8);
        if (rng() % 4 == 0) value = static_cast<uint32_t>(rng());
        int na = onListB ? 0 : n;
        int nb = onListB ? n : 0;
        Side a, b;
        Build(a, st, na, nb, vt);
        Build(b, st, na, nb, vt);
        int32_t ra = orig(a.object, value);
        int32_t rb = ours(b.object, value);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("%s: count %d value 0x%x, original %d, ours %d", name, n, value, ra, rb);
        CheckState(ctx, name, a, b);
    }
}

// ---------------------------------------------------------------------------------------------------------
// ListBCount: (end - begin) >> 2 for arbitrary differences, including unaligned ones (sar rounds down). The end
// pointer is never dereferenced, so it may point anywhere.

void CheckListBCount(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    using Fn = int32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9db270);
    auto ours = AsFn<Fn>(&Sys::ListBCount);
    const void *vt = GameVTable(ctx);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        int delta = static_cast<int>(rng() % 121) - 40; // -40 .. 80 bytes from list B's begin
        Side a, b;
        Build(a, st, 0, 0, vt);
        Build(b, st, 0, 0, vt);
        StoreWord(a.object + 0x30, reinterpret_cast<uintptr_t>(a.listB) + delta);
        StoreWord(b.object + 0x30, reinterpret_cast<uintptr_t>(b.listB) + delta);
        int32_t ra = orig(a.object);
        int32_t rb = ours(b.object);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("ListBCount: delta %d original %d, ours %d", delta, ra, rb);
        CheckState(ctx, "ListBCount", a, b);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Drain loops. Slot 16 of a fake vtable records its argument and decrements the counter at gDecOffset (when it
// is non-zero), so the loops end. Both sides' recorded calls must match.

std::vector<uint32_t> gDrainCalls;
uint32_t gDecOffset = 0x68;
void *gDrainVTable[17];

// Some calls also rewrite the +0x64 word: SetCounter64AndDrain's loop compares against it after each call, so a
// callee that changes it must be seen. The counter still decreases on every call, so the loops end.
void __fastcall DrainSlot16(void *self, void * /*edx*/, uint32_t arg)
{
    gDrainCalls.push_back(arg);
    uint8_t *object = static_cast<uint8_t *>(self);
    uint32_t &counter = *reinterpret_cast<uint32_t *>(object + gDecOffset);
    if (counter > 0) --counter;
    if (arg % 3 == 0) {
        uint32_t rewritten = arg / 3 + 1;
        std::memcpy(object + 0x64, &rewritten, sizeof(rewritten));
    }
}

void SetupDrainVTable()
{
    gDrainVTable[16] = reinterpret_cast<void *>(&DrainSlot16);
}

// SetCounter64AndDrain (value argument): the loop compares the +0x68 counter with the value, then with +0x64.
void CheckSetCounter64(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    using Fn = void(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9dba10);
    auto ours = AsFn<Fn>(&Sys::SetCounter64AndDrain);
    SetupDrainVTable();
    gDecOffset = 0x68;
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t counter = static_cast<uint32_t>(rng() % 12);
        std::memcpy(st.object + 0x68, &counter, sizeof(counter));
        // Mostly small values (the loop runs); sometimes above the counter (no call).
        uint32_t value = (rng() % 3 == 0) ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 12);
        Side a, b;
        Build(a, st, 0, 0, gDrainVTable);
        Build(b, st, 0, 0, gDrainVTable);
        gDrainCalls.clear();
        orig(a.object, value);
        std::vector<uint32_t> ca = gDrainCalls;
        gDrainCalls.clear();
        ours(b.object, value);
        std::vector<uint32_t> cb = gDrainCalls;
        ++ctx.cases;
        if (ca != cb) ctx.Fail("SetCounter64AndDrain(%u): recorded calls differ (%zu vs %zu)", value, ca.size(), cb.size());
        CheckState(ctx, "SetCounter64AndDrain", a, b);
    }
}

// DrainWhileCounter68 / DrainWhileCounter54: no argument; the counter at `counterOffset` is drained.
void CheckDrainWhile(difftest::Context &ctx, const char *name, uint32_t rva, void (Sys::*pmf)(), uint32_t counterOffset,
                     std::mt19937 &rng, int trials)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(pmf);
    SetupDrainVTable();
    gDecOffset = counterOffset;
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t counter = static_cast<uint32_t>(rng() % 12);
        std::memcpy(st.object + counterOffset, &counter, sizeof(counter));
        Side a, b;
        Build(a, st, 0, 0, gDrainVTable);
        Build(b, st, 0, 0, gDrainVTable);
        gDrainCalls.clear();
        orig(a.object);
        std::vector<uint32_t> ca = gDrainCalls;
        gDrainCalls.clear();
        ours(b.object);
        std::vector<uint32_t> cb = gDrainCalls;
        ++ctx.cases;
        if (ca != cb) ctx.Fail("%s: recorded calls differ (%zu vs %zu)", name, ca.size(), cb.size());
        CheckState(ctx, name, a, b);
    }
}

// ---------------------------------------------------------------------------------------------------------
// ResetWords: no arguments, two words zeroed.

void CheckResetWords(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9db870);
    auto ours = AsFn<Fn>(&Sys::ResetWords);
    const void *vt = GameVTable(ctx);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Side a, b;
        Build(a, st, 0, 0, vt);
        Build(b, st, 0, 0, vt);
        orig(a.object);
        ours(b.object);
        ++ctx.cases;
        CheckState(ctx, "ResetWords", a, b);
    }
}

// ---------------------------------------------------------------------------------------------------------
// FindVirtualByValue (slot 9). Through the game's vtable it calls slot 32 (ListBCount) and slot 8 (ListBElementAt);
// each element is a fake object whose vtable slot 13 returns the element's value.

struct Elem {
    void *vt;
    uint32_t value;
};

uint32_t __fastcall ElemValue(void *self, void * /*edx*/)
{
    return static_cast<Elem *>(self)->value;
}

void *gElemVTable[14];
Elem gElems[kCap];

// ---------------------------------------------------------------------------------------------------------
// The cRZRandom at +0x38 (mIntegerSeed, mRealSeed, mUnknown). mRealSeed is kept non-zero: zero seeds from the
// clock, which the harness can't run.

void SeedRandom(State &st, std::mt19937 &rng, uint32_t fixed, bool useFixed)
{
    uint32_t integerSeed = useFixed ? fixed : static_cast<uint32_t>(rng());
    uint32_t realSeed = static_cast<uint32_t>(rng()) | 1u;
    std::memcpy(st.object + 0x38, &integerSeed, sizeof(integerSeed));
    std::memcpy(st.object + 0x3c, &realSeed, sizeof(realSeed));
}

} // namespace

// 0x9dabf0: list B elements are fake objects whose values are known; the value to find is one of them or random.
DIFF_TEST(cSceneDBAnimSystem_FindVirtualByValue, 0x9dabf0)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9dabf0);
    auto ours = AsFn<Fn>(&Sys::FindVirtualByValue);
    const void *vt = GameVTable(ctx);
    gElemVTable[13] = reinterpret_cast<void *>(&ElemValue);

    std::mt19937 rng(0x5eed0009);
    for (int i = 0; i < 20000; ++i) {
        State st = RandomState(rng);
        for (int k = 0; k < kCap; ++k) {
            gElems[k].vt = gElemVTable;
            gElems[k].value = (rng() % 4 == 0) ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 6);
            st.listB[k] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gElems[k]));
        }
        int nb = static_cast<int>(rng() % (kCap + 1));
        uint32_t value = (rng() % 4 == 0) ? static_cast<uint32_t>(rng()) : gElems[rng() % kCap].value;
        Side a, b;
        Build(a, st, 0, nb, vt);
        Build(b, st, 0, nb, vt);
        uint32_t ra = orig(a.object, value);
        uint32_t rb = ours(b.object, value);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("FindVirtualByValue(0x%x) with %d elements: original 0x%08x, ours 0x%08x", value, nb, ra, rb);
        CheckState(ctx, "FindVirtualByValue", a, b);
    }
}

DIFF_TEST(cSceneDBAnimSystem_ListAElementAt, 0x9dac40)
{
    std::mt19937 rng(0x5eed0003);
    CheckIndexedList(ctx, "ListAElementAt", 0x9dac40, &Sys::ListAElementAt, rng, 5000);
}

DIFF_TEST(cSceneDBAnimSystem_ListBElementAt, 0x9dabe0)
{
    std::mt19937 rng(0x5eed0007);
    CheckIndexedList(ctx, "ListBElementAt", 0x9dabe0, &Sys::ListBElementAt, rng, 5000);
}

DIFF_TEST(cSceneDBAnimSystem_FindInListA, 0x9daef0)
{
    std::mt19937 rng(0x5eed0029);
    CheckFind(ctx, "FindInListA", 0x9daef0, &Sys::FindInListA, false, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_FindInListB, 0x9daec0)
{
    std::mt19937 rng(0x5eed001f);
    CheckFind(ctx, "FindInListB", 0x9daec0, &Sys::FindInListB, true, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_ListBCount, 0x9db270)
{
    std::mt19937 rng(0x5eed0020);
    CheckListBCount(ctx, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_SetCounter64AndDrain, 0x9dba10)
{
    std::mt19937 rng(0x5eed0010);
    CheckSetCounter64(ctx, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_DrainWhileCounter68, 0x9dada0)
{
    std::mt19937 rng(0x5eed0021);
    CheckDrainWhile(ctx, "DrainWhileCounter68", 0x9dada0, &Sys::DrainWhileCounter68, 0x68, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_DrainWhileCounter54, 0x9dadd0)
{
    std::mt19937 rng(0x5eed0054);
    CheckDrainWhile(ctx, "DrainWhileCounter54", 0x9dadd0, &Sys::DrainWhileCounter54, 0x54, rng, 20000);
}

DIFF_TEST(cSceneDBAnimSystem_ResetWords, 0x9db870)
{
    std::mt19937 rng(0x5eed0026);
    CheckResetWords(ctx, rng, 5000);
}

// 0x9db390: forwards to cRZRandom::RandomUint32Uniform (0x1b8d75) on the cRZRandom at +0x38. Several calls per
// trial, so the state carried between calls is compared too.
DIFF_TEST(cSceneDBAnimSystem_RandomUint32, 0x9db390)
{
    using Fn = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9db390);
    auto ours = AsFn<Fn>(&Sys::RandomUint32);
    const void *vt = GameVTable(ctx);
    std::mt19937 rng(0x5eed0038);
    const uint32_t edges[] = {0u, 1u, 0x3039u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (int i = 0; i < 20000; ++i) {
        State st = RandomState(rng);
        SeedRandom(st, rng, edges[i % 6], i < 6 || i % 5 == 0);
        Side a, b;
        Build(a, st, 0, 0, vt);
        Build(b, st, 0, 0, vt);
        for (int step = 0; step < 4; ++step) {
            uint32_t ra = orig(a.object);
            uint32_t rb = ours(b.object);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("RandomUint32 trial %d step %d: original 0x%08x, ours 0x%08x", i, step, ra, rb);
            CheckState(ctx, "RandomUint32", a, b);
        }
    }
}

// 0x9db370: (float) of cRZRandom::RandomDoubleUniform (0x1b8c78), returned in st(0). Compared as bits.
DIFF_TEST(cSceneDBAnimSystem_RandomFloat, 0x9db370)
{
    using Fn = float(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9db370);
    auto ours = AsFn<Fn>(&Sys::RandomFloat);
    const void *vt = GameVTable(ctx);
    std::mt19937 rng(0x5eed0037);
    for (int i = 0; i < 20000; ++i) {
        State st = RandomState(rng);
        SeedRandom(st, rng, 0, false);
        Side a, b;
        Build(a, st, 0, 0, vt);
        Build(b, st, 0, 0, vt);
        for (int step = 0; step < 4; ++step) {
            uint32_t ra = Bits(orig(a.object));
            uint32_t rb = Bits(ours(b.object));
            ++ctx.cases;
            if (ra != rb) ctx.Fail("RandomFloat trial %d step %d: original 0x%08x, ours 0x%08x", i, step, ra, rb);
            CheckState(ctx, "RandomFloat", a, b);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// QueryInterface (slot 0). The object carries recording vtables at +0, +4 and +8. The fall-through (riid other than
// 0x287fb697 and the two it stores) reaches cRZSystemService::QueryInterface on +4, which tail-calls
// RZUnknownQueryInterface on its own +4, i.e. the object's +8. Compared: the returned al, the recorded AddRef calls (with the offset they hit), the
// object bytes, and where the result pointer points (as an offset in the object, or the raw value).

namespace {

using OrigQI = uint8_t(__thiscall *)(void *, uint32_t, void **);
using OurQI = bool(__thiscall *)(void *, uint32_t, void **);

struct QIBlock {
    uint8_t front[32];
    uint8_t object[kObjectSize];
    uint8_t back[32];
};

uint8_t *gQIBase = nullptr;
std::vector<int> gQIEvents;
void *gQIVTable0[3];
void *gQIVTable4[3];
void *gQIVTable8[3];
void *const kQIUnset = reinterpret_cast<void *>(static_cast<uintptr_t>(0xdeadbeefu));

uint32_t __fastcall RecAddRef(void *self, void * /*edx*/)
{
    gQIEvents.push_back(1000 + static_cast<int>(static_cast<uint8_t *>(self) - gQIBase));
    return 1;
}

void SetupQIVTables()
{
    gQIVTable0[1] = reinterpret_cast<void *>(&RecAddRef);
    gQIVTable4[1] = reinterpret_cast<void *>(&RecAddRef);
    gQIVTable8[1] = reinterpret_cast<void *>(&RecAddRef);
}

uintptr_t NormaliseOut(void *out, const uint8_t *base)
{
    if (out == kQIUnset) return 0xabcdef00u;
    if (out == nullptr) return 0;
    const uint8_t *p = static_cast<const uint8_t *>(out);
    if (p >= base && p < base + kObjectSize) return 0x10000u + static_cast<uintptr_t>(p - base);
    return reinterpret_cast<uintptr_t>(out);
}

struct QIOutcome {
    uint8_t al;
    std::vector<int> events;
    std::vector<uint8_t> object;
    uintptr_t out;
    bool canaries;
};

QIOutcome RunQI(bool useOurs, OrigQI orig, uint32_t riid, bool nullPpv, const uint8_t *init)
{
    QIBlock blk;
    std::memset(blk.front, 0xA5, sizeof(blk.front));
    std::memset(blk.back, 0x5A, sizeof(blk.back));
    std::memcpy(blk.object, init, sizeof(blk.object));
    StoreWord(blk.object, reinterpret_cast<uintptr_t>(gQIVTable0));
    StoreWord(blk.object + 4, reinterpret_cast<uintptr_t>(gQIVTable4));
    StoreWord(blk.object + 8, reinterpret_cast<uintptr_t>(gQIVTable8));
    gQIBase = blk.object;
    gQIEvents.clear();

    void *out = kQIUnset;
    void **ppv = nullPpv ? nullptr : &out;
    QIOutcome r{};
    if (useOurs) {
        r.al = AsFn<OurQI>(&Sys::QueryInterface)(blk.object, riid, ppv) ? 1 : 0;
    } else {
        r.al = orig(blk.object, riid, ppv);
    }
    r.events = gQIEvents;
    r.object.assign(blk.object, blk.object + sizeof(blk.object));
    r.out = NormaliseOut(out, blk.object);
    r.canaries = true;
    for (uint8_t b : blk.front)
        if (b != 0xA5) r.canaries = false;
    for (uint8_t b : blk.back)
        if (b != 0x5A) r.canaries = false;
    gQIBase = nullptr;
    return r;
}

} // namespace

DIFF_TEST(cSceneDBAnimSystem_QueryInterface, 0x9db320)
{
    auto orig = ctx.Original<OrigQI>(0x9db320);
    SetupQIVTables();

    // Ids the function compares against, their neighbours, and random values.
    std::vector<uint32_t> ids = {0u,          1u,          2u,          0x5afeed72u, 0xfafeed6cu, 0x5afeed71u,
                                 0x5afeed73u, 0xfafeed6bu, 0xfafeed6du, 0x287fb697u, 0x287fb696u, 0x287fb698u,
                                 0x22e85d8eu, 0xffffffffu};
    std::mt19937 rng(0x5eed0001);
    for (int i = 0; i < 300; ++i) ids.push_back(static_cast<uint32_t>(rng()));

    for (uint32_t riid : ids) {
        for (int t = 0; t < 3; ++t) {
            std::vector<uint8_t> init(kObjectSize);
            for (auto &byte : init) byte = static_cast<uint8_t>(rng());
            bool nullPpv = (t == 2);
            QIOutcome a = RunQI(false, orig, riid, nullPpv, init.data());
            QIOutcome b = RunQI(true, orig, riid, nullPpv, init.data());
            ++ctx.cases;
            if (a.al != b.al) ctx.Fail("QueryInterface riid 0x%08x: al original %u, ours %u", riid, a.al, b.al);
            if (a.events != b.events) ctx.Fail("QueryInterface riid 0x%08x: AddRef calls differ", riid);
            if (a.out != b.out) ctx.Fail("QueryInterface riid 0x%08x: result pointer differs", riid);
            if (a.object != b.object) ctx.Fail("QueryInterface riid 0x%08x: object bytes differ", riid);
            if (!a.canaries || !b.canaries) ctx.Fail("QueryInterface riid 0x%08x: canaries overwritten", riid);
        }
    }
}
