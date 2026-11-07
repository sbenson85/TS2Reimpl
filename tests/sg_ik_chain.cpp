// Differential tests for nGZSceneDBAnim::cIKChainConstProxy (primary vtable 0xe685a4, 51 slots) and for the thunks
// of the cIKChain secondary subobject (vtable 0xe6376c) at slots 1 and 25.
//
// Proxy cases: each trial builds one random state three times. The original runs on the first side, ours on the
// second (both through a copy of the vtable with the original slots), and ours through a copy in which the tested
// slot, and slot 23 (the element address, which the functions call through the vtable), are routed to our code.
// The element count (slot 47) and the element slots 15, 28 and 29 are fakes that record their calls by element
// index, so the sides see the same callees and the calls can be compared. Element objects are 43-byte records in a
// block the proxy points at (+0x42); each starts with a vtable pointer to the fake element vtable and holds its
// index at +4.
//
// Subobject cases: the thunk computes real = this - [this - 4]. Each trial stores a random vtordisp (0, 4 or 8) and
// random bytes, runs the original thunk, our member directly, and our member through the subobject vtable.
#include "difftest.h"
#include "game/vtable.h"
#include "sg/cIKChain.h"

#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace {

using Proxy = nGZSceneDBAnim::cIKChainConstProxy;
using Sub = nGZSceneDBAnim::cIKChainSubobject;

constexpr int kSlots = 51;
constexpr int kObjectSize = 0x100;
constexpr int kHolderSize = 0x60;
constexpr int kMaxCount = 8;
constexpr int kStride = 43;
constexpr int kBlockSize = kStride * kMaxCount + 16;
constexpr uint32_t kTagA = 0xfb5b3383u;
constexpr uint32_t kTagB = 0xe9c7f1adu;
constexpr uint32_t kSentinel = 0x5a5a5a5au;

template <typename T> const void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

void StoreWord(uint8_t *at, uintptr_t v)
{
    std::memcpy(at, &v, sizeof(v));
}

uint32_t LoadDword(const uint8_t *at)
{
    uint32_t v;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

// ---- Recording fakes -------------------------------------------------------------------------------------------

using Event = std::array<uint32_t, 4>; // kind, element index or object offset, and up to two values
std::vector<Event> gEvents;
enum : uint32_t { kEvAddRef = 1, kEvCount, kEvSlot15, kEvSlot28, kEvSlot29 };

const void *gCur = nullptr; // the side whose offsets the fakes record

// p as an offset from base (the side it belongs to), so that the same field of two sides compares equal.
uint32_t Rel(const void *p, const void *base)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base));
}

uint32_t gCount = 0;
uint32_t gRes15[kMaxCount];
bool gRes28[kMaxCount];
bool gRes29[kMaxCount];

uint32_t ElementIndex(const void *element)
{
    return LoadDword(static_cast<const uint8_t *>(element) + 4);
}

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gEvents.push_back({kEvAddRef, Rel(self, gCur), 0, 0});
    return 1;
}

uint32_t __fastcall FakeCount(void *self, void *)
{
    gEvents.push_back({kEvCount, Rel(self, gCur), 0, 0});
    return gCount;
}

uint32_t __fastcall FakeSlot15(void *self, void *)
{
    const uint32_t i = ElementIndex(self);
    gEvents.push_back({kEvSlot15, i, 0, 0});
    return gRes15[i];
}

bool __fastcall FakeSlot28(void *self, void *, uint32_t value, uint32_t tag)
{
    const uint32_t i = ElementIndex(self);
    gEvents.push_back({kEvSlot28, i, value, tag});
    return gRes28[i];
}

bool __fastcall FakeSlot29(void *self, void *, uint32_t value)
{
    const uint32_t i = ElementIndex(self);
    gEvents.push_back({kEvSlot29, i, value, 0});
    return gRes29[i];
}

// ---- Proxy sides -----------------------------------------------------------------------------------------------

struct State {
    uint8_t object[kObjectSize];
    uint8_t holder[kHolderSize];
    uint8_t block[kBlockSize];
    int count;
    int matchIndex; // -1, or an element index the IndexOf value is built from
    uint32_t entry; // holder +0x48, read by EntryAddress
    uint32_t f2e;   // object +0x2e
    uint32_t f33;   // object +0x33
    uint32_t f3d;   // object +0x3d
    uint32_t res15[kMaxCount];
    bool res28[kMaxCount];
    bool res29[kMaxCount];
};

struct Side {
    uint8_t front[32];
    uint8_t object[kObjectSize];
    uint8_t back[32];
    const void *vt[kSlots];
    uint8_t holder[kHolderSize];
    uint8_t block[kBlockSize];
    const void *elementVT[30];
    uint32_t out; // *ppv target of QueryInterface
};

State RandomState(std::mt19937 &rng)
{
    State st;
    for (auto &b : st.object) b = static_cast<uint8_t>(rng());
    for (auto &b : st.holder) b = static_cast<uint8_t>(rng());
    for (auto &b : st.block) b = static_cast<uint8_t>(rng());
    st.count = static_cast<int>(rng() % (kMaxCount + 1));
    st.matchIndex = (st.count > 0 && rng() % 2) ? static_cast<int>(rng() % st.count) : -1;
    st.entry = static_cast<uint32_t>(rng());
    // Small values for the compared fields and results, so the equal and not-equal paths both occur.
    st.f2e = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 6);
    st.f3d = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 6);
    st.f33 = rng() % 2 ? 0 : static_cast<uint32_t>(1 + rng() % 3);
    for (int i = 0; i < kMaxCount; ++i) {
        st.res15[i] = static_cast<uint32_t>(rng() % 6);
        st.res28[i] = rng() % 3 == 0;
        st.res29[i] = rng() % 3 == 0;
    }
    return st;
}

void InitElementVT(Side &s)
{
    s.elementVT[15] = Addr(&FakeSlot15);
    s.elementVT[28] = Addr(&FakeSlot28);
    s.elementVT[29] = Addr(&FakeSlot29);
}

void Build(Side &s, const State &st, const void *const *vt)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.object, st.object, sizeof(s.object));
    std::memcpy(s.holder, st.holder, sizeof(s.holder));
    std::memcpy(s.block, st.block, sizeof(s.block));
    std::memcpy(s.vt, vt, sizeof(s.vt));
    std::memset(s.elementVT, 0, sizeof(s.elementVT));
    InitElementVT(s);
    s.out = kSentinel;

    StoreWord(s.object + 0x00, reinterpret_cast<uintptr_t>(s.vt));
    StoreWord(s.object + 0x04, reinterpret_cast<uintptr_t>(s.holder));
    std::memcpy(s.object + 0x2e, &st.f2e, sizeof(uint32_t));
    std::memcpy(s.object + 0x33, &st.f33, sizeof(uint32_t));
    std::memcpy(s.object + 0x3d, &st.f3d, sizeof(uint32_t));
    StoreWord(s.object + 0x42, reinterpret_cast<uintptr_t>(s.block));
    std::memcpy(s.holder + 0x48, &st.entry, sizeof(uint32_t));
    for (int i = 0; i < kMaxCount; ++i) {
        uint8_t *element = s.block + kStride * i;
        StoreWord(element, reinterpret_cast<uintptr_t>(s.elementVT));
        const uint32_t index = static_cast<uint32_t>(i);
        std::memcpy(element + 4, &index, sizeof(index));
    }
}

void SetGlobals(const State &st)
{
    gCount = static_cast<uint32_t>(st.count);
    for (int i = 0; i < kMaxCount; ++i) {
        gRes15[i] = st.res15[i];
        gRes28[i] = st.res28[i];
        gRes29[i] = st.res29[i];
    }
}

// Object bytes, with the pointer fields as offsets from their side, and the holder and the canaries. The block
// holds vtable pointers of its own side, so it is compared through the proxy's own state only.
std::vector<uint8_t> Snapshot(const Side &s)
{
    uint8_t obj[kObjectSize];
    std::memcpy(obj, s.object, sizeof(obj));
    StoreWord(obj + 0x00, Rel(s.vt, &s));
    StoreWord(obj + 0x04, Rel(s.holder, &s));
    StoreWord(obj + 0x42, Rel(s.block, &s));
    std::vector<uint8_t> out(obj, obj + sizeof(obj));
    out.insert(out.end(), s.holder, s.holder + sizeof(s.holder));
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

// The vtable copies: the original's slots with the peer fakes in slots 1 and 47, and a copy in which the tested
// slot and slot 23 point at our code.
struct Slots {
    const void *slots[kSlots];
};

Slots PlainProxySlots(const difftest::Context &ctx)
{
    Slots s{};
    std::memcpy(s.slots, ctx.image.At(Proxy::kVTable), sizeof(s.slots));
    s.slots[1] = Addr(&FakeAddRef);
    s.slots[47] = Addr(&FakeCount);
    return s;
}

Slots Routed(const Slots &plain, int slot, const void *fn)
{
    Slots s = plain;
    s.slots[23] = Addr(&Proxy::ElementAddress);
    s.slots[slot] = fn;
    return s;
}

// Runs the operation on the three sides, then compares the result, the recorded calls, the object and the canaries.
template <typename Run>
void CompareProxy(difftest::Context &ctx, const char *name, const State &st, const Slots &plain, const Slots &routed,
                  Run run)
{
    static Side a, b, c;
    Side *sides[3] = {&a, &b, &c};
    Build(a, st, plain.slots);
    Build(b, st, plain.slots);
    Build(c, st, routed.slots);

    uint64_t result[3];
    std::vector<Event> events[3];
    for (int w = 0; w < 3; ++w) {
        gCur = sides[w];
        SetGlobals(st);
        gEvents.clear();
        result[w] = run(w, *sides[w]);
        events[w] = gEvents;
    }

    ++ctx.cases;
    if (result[0] != result[1] || result[0] != result[2]) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name,
                 static_cast<unsigned long long>(result[0]), static_cast<unsigned long long>(result[1]),
                 static_cast<unsigned long long>(result[2]));
    }
    if (events[0] != events[1] || events[0] != events[2]) {
        ctx.Fail("%s: calls made differ (original %zu, ours %zu, via slot %zu)", name, events[0].size(),
                 events[1].size(), events[2].size());
    }
    if (Snapshot(a) != Snapshot(b) || Snapshot(a) != Snapshot(c)) ctx.Fail("%s: object state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b) || !CanariesIntact(c)) ctx.Fail("%s: canaries overwritten", name);
}

// What QueryInterface stored through ppv, as a small code: 1 the object, 2 untouched, 3 anything else.
uint64_t OutCode(const Side &s)
{
    if (s.out == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.object))) return 1;
    if (s.out == kSentinel) return 2;
    return 3;
}

// ---- Proxy checks ----------------------------------------------------------------------------------------------

void CheckQueryInterface(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 0, Addr(&Proxy::QueryInterface));
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xa331a0);
    static const uint32_t kIds[] = {1, 0x1d55b138u, 0x5b0aaa74u, 0x1d55b137u, 0x5b0aaa75u, 0x3db4f93cu,
                                    0xbb11a53eu, 0, 2};

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t riid = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : kIds[rng() % 9];
        const bool nullPpv = rng() % 5 == 0;
        CompareProxy(ctx, "QueryInterface", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            void **ppv = nullPpv ? nullptr : reinterpret_cast<void **>(&s.out);
            bool r;
            if (which == 0) {
                r = orig(s.object, riid, ppv);
            } else if (which == 1) {
                r = reinterpret_cast<Proxy *>(s.object)->QueryInterface(riid, ppv);
            } else {
                r = VirtualSlot<bool(__thiscall *)(void *, uint32_t, void **)>(s.object, 0)(s.object, riid, ppv);
            }
            return (r ? 1u : 0u) | (OutCode(s) << 8);
        });
    }
}

void CheckElementAddress(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 23, Addr(&Proxy::ElementAddress));
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xa33110);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t index = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 64);
        CompareProxy(ctx, "ElementAddress", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            uint32_t r;
            if (which == 0) {
                r = orig(s.object, index);
            } else if (which == 1) {
                r = reinterpret_cast<Proxy *>(s.object)->ElementAddress(index);
            } else {
                r = VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(s.object, 23)(s.object, index);
            }
            // The result is block + 43 * index: compare it relative to the block of its side.
            return static_cast<uint32_t>(r - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.block)));
        });
    }
}

void CheckEntryAddress(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 25, Addr(&Proxy::EntryAddress));
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa33030);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        CompareProxy(ctx, "EntryAddress", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            if (which == 0) return orig(s.object);
            if (which == 1) return reinterpret_cast<Proxy *>(s.object)->EntryAddress();
            return VirtualSlot<uint32_t(__thiscall *)(void *)>(s.object, 25)(s.object);
        });
    }
}

void CheckIndexOf(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 46, Addr(&Proxy::IndexOf));
    auto orig = ctx.Original<int32_t(__thiscall *)(void *, uint32_t)>(0xa33140);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t random = static_cast<uint32_t>(rng());
        CompareProxy(ctx, "IndexOf", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            // A value that element matchIndex returns, as this side's address; otherwise a random value.
            const uint32_t value = st.matchIndex >= 0
                                       ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.block)) +
                                             kStride * static_cast<uint32_t>(st.matchIndex)
                                       : random;
            uint32_t r;
            if (which == 0) {
                r = static_cast<uint32_t>(orig(s.object, value));
            } else if (which == 1) {
                r = static_cast<uint32_t>(reinterpret_cast<Proxy *>(s.object)->IndexOf(value));
            } else {
                r = VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t)>(s.object, 46)(s.object, value);
            }
            return r;
        });
    }
}

void CheckFindTagged(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 48, Addr(&Proxy::FindTagged));
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, uint32_t)>(0xa332e0);
    static const uint32_t kTags[] = {kTagA, kTagB, 0, 1, 2};

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t tag = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : kTags[rng() % 5];
        const uint32_t value = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : (rng() % 2 ? st.f2e : st.f3d);
        CompareProxy(ctx, "FindTagged", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            bool r;
            if (which == 0) {
                r = orig(s.object, value, tag);
            } else if (which == 1) {
                r = reinterpret_cast<Proxy *>(s.object)->FindTagged(value, tag);
            } else {
                r = VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t)>(s.object, 48)(s.object, value, tag);
            }
            return r ? 1 : 0;
        });
    }
}

void CheckAnyElementAccepts(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 49, Addr(&Proxy::AnyElementAccepts));
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa33350);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t value = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : (rng() % 2 ? st.f2e : static_cast<uint32_t>(rng() % 6));
        CompareProxy(ctx, "AnyElementAccepts", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            bool r;
            if (which == 0) {
                r = orig(s.object, value);
            } else if (which == 1) {
                r = reinterpret_cast<Proxy *>(s.object)->AnyElementAccepts(value);
            } else {
                r = VirtualSlot<bool(__thiscall *)(void *, uint32_t)>(s.object, 49)(s.object, value);
            }
            return r ? 1 : 0;
        });
    }
}

void CheckContainsValue(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainProxySlots(ctx);
    Slots routed = Routed(plain, 50, Addr(&Proxy::ContainsValue));
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa33090);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        const uint32_t value = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 7);
        CompareProxy(ctx, "ContainsValue", st, plain, routed, [&](int which, Side &s) -> uint64_t {
            bool r;
            if (which == 0) {
                r = orig(s.object, value);
            } else if (which == 1) {
                r = reinterpret_cast<Proxy *>(s.object)->ContainsValue(value);
            } else {
                r = VirtualSlot<bool(__thiscall *)(void *, uint32_t)>(s.object, 50)(s.object, value);
            }
            return r ? 1 : 0;
        });
    }
}

// ---- Subobject thunks ------------------------------------------------------------------------------------------

constexpr int kHostSize = 0x400;
constexpr int kRealOffset = 0x200; // real `this` inside the host; the body's fields are at real - K

struct SubSide {
    uint8_t front[32];
    uint8_t host[kHostSize];
    uint8_t back[32];
    const void *vt[kSlots];
};

void BuildSub(SubSide &s, const std::vector<uint8_t> &bytes, int disp, const void *const *vt)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.host, bytes.data(), sizeof(s.host));
    std::memcpy(s.vt, vt, sizeof(s.vt));
    const uint32_t d = static_cast<uint32_t>(disp);
    // The vtordisp at subobject - 4, and the subobject's vtable pointer at the subobject.
    std::memcpy(s.host + kRealOffset + disp - 4, &d, sizeof(d));
    StoreWord(s.host + kRealOffset + disp, reinterpret_cast<uintptr_t>(s.vt));
}

std::vector<uint8_t> SubSnapshot(const SubSide &s, int disp)
{
    std::vector<uint8_t> out(s.host, s.host + sizeof(s.host));
    // The subobject's vtable pointer is the side's own copy; compare its offset from the side instead.
    StoreWord(out.data() + kRealOffset + disp, Rel(s.vt, &s));
    return out;
}

bool SubCanariesIntact(const SubSide &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

template <typename Run>
void CompareSub(difftest::Context &ctx, const char *name, const std::vector<uint8_t> &bytes, int disp,
                const Slots &plain, const Slots &routed, Run run)
{
    static SubSide a, b, c;
    BuildSub(a, bytes, disp, plain.slots);
    BuildSub(b, bytes, disp, plain.slots);
    BuildSub(c, bytes, disp, routed.slots);
    SubSide *sides[3] = {&a, &b, &c};
    uint32_t result[3];
    for (int w = 0; w < 3; ++w) {
        result[w] = run(w, *sides[w], disp);
    }
    ++ctx.cases;
    if (result[0] != result[1] || result[0] != result[2]) {
        ctx.Fail("%s: result original %u, ours %u, via slot %u", name, result[0], result[1], result[2]);
    }
    if (SubSnapshot(a, disp) != SubSnapshot(b, disp) || SubSnapshot(a, disp) != SubSnapshot(c, disp)) {
        ctx.Fail("%s: host state differs", name);
    }
    if (!SubCanariesIntact(a) || !SubCanariesIntact(b) || !SubCanariesIntact(c)) {
        ctx.Fail("%s: canaries overwritten", name);
    }
}

Slots PlainSubSlots(const difftest::Context &ctx)
{
    Slots s{};
    std::memcpy(s.slots, ctx.image.At(Sub::kVTable), sizeof(s.slots));
    return s;
}

void CheckIncrementWord(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainSubSlots(ctx);
    Slots routed = plain;
    routed.slots[1] = Addr(&Sub::IncrementWord);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x9e7cb0);
    static const int kDisps[3] = {0, 4, 8};

    for (int i = 0; i < trials; ++i) {
        std::vector<uint8_t> bytes(kHostSize);
        for (auto &x : bytes) x = static_cast<uint8_t>(rng());
        const int disp = kDisps[rng() % 3];
        // Counters near the top of their range wrap the same way on both sides.
        if (rng() % 4 == 0) std::memset(bytes.data() + kRealOffset - 0x164, 0xFF, 4);
        CompareSub(ctx, "IncrementWord", bytes, disp, plain, routed,
                   [&](int which, SubSide &s, int d) -> uint32_t {
                       void *sub = s.host + kRealOffset + d;
                       if (which == 0) return orig(sub);
                       if (which == 1) return reinterpret_cast<Sub *>(sub)->IncrementWord();
                       return VirtualSlot<uint32_t(__thiscall *)(void *)>(sub, 1)(sub);
                   });
    }
}

void CheckGetWord0C(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    Slots plain = PlainSubSlots(ctx);
    Slots routed = plain;
    routed.slots[25] = Addr(&Sub::GetWord0C);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x9e7cce);
    static const int kDisps[3] = {0, 4, 8};

    for (int i = 0; i < trials; ++i) {
        std::vector<uint8_t> bytes(kHostSize);
        for (auto &x : bytes) x = static_cast<uint8_t>(rng());
        const int disp = kDisps[rng() % 3];
        CompareSub(ctx, "GetWord0C", bytes, disp, plain, routed, [&](int which, SubSide &s, int d) -> uint32_t {
            void *sub = s.host + kRealOffset + d;
            if (which == 0) return orig(sub);
            if (which == 1) return reinterpret_cast<Sub *>(sub)->GetWord0C();
            return VirtualSlot<uint32_t(__thiscall *)(void *)>(sub, 25)(sub);
        });
    }
}

} // namespace

DIFF_TEST(IKChain_QueryInterface, 0xa331a0)
{
    std::mt19937 rng(0x1a01);
    CheckQueryInterface(ctx, rng, 4000);
}

DIFF_TEST(IKChain_ElementAddress, 0xa33110)
{
    std::mt19937 rng(0x1a23);
    CheckElementAddress(ctx, rng, 4000);
}

DIFF_TEST(IKChain_EntryAddress, 0xa33030)
{
    std::mt19937 rng(0x1a25);
    CheckEntryAddress(ctx, rng, 4000);
}

DIFF_TEST(IKChain_IndexOf, 0xa33140)
{
    std::mt19937 rng(0x1a46);
    CheckIndexOf(ctx, rng, 4000);
}

DIFF_TEST(IKChain_FindTagged, 0xa332e0)
{
    std::mt19937 rng(0x1a48);
    CheckFindTagged(ctx, rng, 4000);
}

DIFF_TEST(IKChain_AnyElementAccepts, 0xa33350)
{
    std::mt19937 rng(0x1a49);
    CheckAnyElementAccepts(ctx, rng, 4000);
}

DIFF_TEST(IKChain_ContainsValue, 0xa33090)
{
    std::mt19937 rng(0x1a50);
    CheckContainsValue(ctx, rng, 4000);
}

DIFF_TEST(IKChain_IncrementWord, 0x9e7cb0)
{
    std::mt19937 rng(0x1b01);
    CheckIncrementWord(ctx, rng, 4000);
}

DIFF_TEST(IKChain_GetWord0C, 0x9e7cce)
{
    std::mt19937 rng(0x1b25);
    CheckGetWord0C(ctx, rng, 4000);
}
