// Differential tests for cGZPersistCacheStrategyBasic (src/gz/cGZPersistCacheStrategyBasic.cpp, lane QQ), the resource
// manager's basic cache strategy, in the standalone shape (our own vtable). The original side builds one with the game's
// constructor 0x9b35f0 (vtable 0xe60148), ours with cGZPersistCacheStrategyBasic::Construct; both run the same random
// script through the object's own vtable (slot by slot, the game's order), over the test's own fakes:
// - resources (slots 1 / 2 count, 3 copies the key it was given out, 4 takes the key, 11 answers a use count the script
//   sets; every call logged with the key's words),
// - key filters (slots 1 / 2 count, 3 accepts by a rule of the key's words and the filter, logged),
// - the key lists GetKeys / GetKeysOfType fill: KeyList (std::vector on the game's allocator service, AllocatorScope),
//   compared by size, capacity and contents;
// - the object's vtable, copied with slots 12, 18 and 20 wrapped to log, so the calls members make through it show;
//   use counts may decay per answer (sometimes with every resource cached under its own key, then a generation), so
//   AdvanceGeneration's purges run to its eight-pass limit.
// After every step: the answer (bool slots by al), the call log, and the strategy's state: the map's load factor, size,
// mask and max index, its list in order (each node's key and resource), every bucket's two ends (as list positions), the
// access counter, the flag byte, the filter, the count; and every fake's count. The end: Shutdown or not, then the last
// Release, the blocks freed (sizes) and the heap balance over the whole run (difftest::AllocationCounter).
// GZPersistCacheStrategyBasic_VTable checks that the game's vtable holds, slot by slot, the RVAs ours replaces.
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "gz/cGZPersistCacheStrategyBasic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Key = cGZPersistCacheStrategyBasic::Key;
using KeyList = cGZPersistCacheStrategyBasic::KeyList;
constexpr uint32_t kCtor = 0x9b35f0;
constexpr uint32_t kGameVTable = 0xe60148;

// The game's vtable 0xe60148: slot, RVA (0 for the folded bodies other classes share: not ours to replace).
struct VSlot {
    unsigned slot;
    uint32_t rva;
};
constexpr VSlot kSlots[] = {
    {1, 0x9b36c0},  {2, 0x9b3a90},  {4, 0x9b3ae0},  {5, 0x9b3ab0},  {7, 0x9b39c0},  {8, 0x9b36d0},
    {9, 0x9b3b10},  {10, 0x9b3a20}, {11, 0x9b3750}, {12, 0x9b38c0}, {13, 0x9b3960}, {14, 0x9b3a60},
    {17, 0x9b37a0}, {18, 0x9b3770}, {19, 0x9b3680}, {20, 0x9b37d0},
};
// The folded slots: 0 QueryInterface 0x1fc060, 3 0x140fe0, 6 0x147770, 15 0x3730d0, 16 0x2bfcb0.
constexpr VSlot kFolded[] = {{0, 0x1fc060}, {3, 0x140fe0}, {6, 0x147770}, {15, 0x3730d0}, {16, 0x2bfcb0}};

// ---- the log ----
std::vector<uint32_t> gLog;
void Log(std::initializer_list<uint32_t> words)
{
    difftest::NotCounted bookkeeping;
    gLog.insert(gLog.end(), words);
}
void LogKey(const Key *k) { Log({k->mWords[0], k->mWords[1], k->mWords[2], k->mWords[3]}); }

// ---- fakes ----
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t uses;  // resources: what slot 11 answers
    uint32_t salt;  // filters: the accept rule's
    Key key;        // resources: the key slot 4 gave
    bool decay;     // resources: each answer of slot 11 lowers `uses` by one (until 0), so purges go on pass after pass
};
constexpr unsigned kResources = 12, kFilters = 3;
Fake gResources[kResources];
Fake gFilters[kFilters];

uint32_t FakeId(const void *p)
{
    if (!p) return 0;
    for (const Fake &f : gResources)
        if (p == &f) return f.id;
    for (const Fake &f : gFilters)
        if (p == &f) return f.id;
    return 0xbad0bad0;
}

uint32_t __fastcall FakeQI(Fake *self, void *, uint32_t riid, void **)
{
    Log({0xa0, self->id, riid});
    return 0;
}
uint32_t __fastcall FakeAddRef(Fake *self, void *)
{
    Log({0xa1, self->id});
    return uint32_t(++self->refs);
}
uint32_t __fastcall FakeRelease(Fake *self, void *)
{
    Log({0xa2, self->id});
    return uint32_t(--self->refs);
}
Key *__fastcall ResourceGetKey(Fake *self, void *, Key *out)
{
    Log({0xa3, self->id});
    *out = self->key;
    return out;
}
void __fastcall ResourceSetKey(Fake *self, void *, const Key *key)
{
    Log({0xa4, self->id});
    LogKey(key);
    self->key = *key;
}
uint32_t __fastcall ResourceUses(Fake *self, void *)
{
    Log({0xab, self->id});
    const uint32_t uses = self->uses;
    if (self->decay && self->uses != 0) --self->uses;
    return uses;
}
uint32_t __fastcall FilterAccepts(Fake *self, void *, const Key *key)
{
    Log({0xb3, self->id});
    LogKey(key);
    const bool yes = (key->mWords[0] + key->mWords[2] + self->salt) % 3 != 0;
    return yes ? 0x5a5a5a01u : 0x5a5a5a00u; // a bool in al, the high bytes junk
}
void __fastcall Unexpected(Fake *self, void *)
{
    Log({0xee, self->id});
}

void *gResourceVT[12] = {
    reinterpret_cast<void *>(&FakeQI),         reinterpret_cast<void *>(&FakeAddRef),
    reinterpret_cast<void *>(&FakeRelease),    reinterpret_cast<void *>(&ResourceGetKey),
    reinterpret_cast<void *>(&ResourceSetKey), reinterpret_cast<void *>(&Unexpected),
    reinterpret_cast<void *>(&Unexpected),     reinterpret_cast<void *>(&Unexpected),
    reinterpret_cast<void *>(&Unexpected),     reinterpret_cast<void *>(&Unexpected),
    reinterpret_cast<void *>(&Unexpected),     reinterpret_cast<void *>(&ResourceUses),
};
void *gFilterVT[5] = {
    reinterpret_cast<void *>(&FakeQI), reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease),
    reinterpret_cast<void *>(&FilterAccepts), reinterpret_cast<void *>(&Unexpected),
};

void ResetFakes(uint32_t seed)
{
    for (unsigned i = 0; i < kResources; ++i) {
        gResources[i] = {gResourceVT, 0x100 + i, 50, 0, 0, {}, false};
        gResources[i].key = {{0xdead0000 + i, 0, 0, 0}};
    }
    for (unsigned i = 0; i < kFilters; ++i) gFilters[i] = {gFilterVT, 0x200 + i, 50, 0, seed + i, {}, false};
}

// The object's own vtable, copied, with the slots its members call through the vtable (12 GetKeys from GetKeysOfType,
// 18 Clear from Shutdown, 20 PurgeUnused from OnAccess and AdvanceGeneration) wrapped to log each call: so a member
// that skips such a call, or makes it directly, shows.
constexpr unsigned kWrapSlots = 21;
void *gSavedVT[kWrapSlots];
void *gWrappedVT[kWrapSlots];

uint32_t __fastcall WrapGetKeys(void *self, void *, void *keys, void *filter)
{
    Log({0xf0, 12, keys ? 1u : 0u});
    return reinterpret_cast<uint32_t(__thiscall *)(void *, void *, void *)>(gSavedVT[12])(self, keys, filter);
}
void __fastcall WrapClear(void *self, void *)
{
    Log({0xf0, 18});
    reinterpret_cast<void(__thiscall *)(void *)>(gSavedVT[18])(self);
}
uint32_t __fastcall WrapPurge(void *self, void *)
{
    const uint32_t answer = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gSavedVT[20])(self);
    Log({0xf0, 20, answer & 0xff});
    return answer;
}

void WrapVTable(void *object)
{
    void **&vt = *static_cast<void ***>(object);
    for (unsigned i = 0; i < kWrapSlots; ++i) gSavedVT[i] = gWrappedVT[i] = vt[i];
    gWrappedVT[12] = reinterpret_cast<void *>(&WrapGetKeys);
    gWrappedVT[18] = reinterpret_cast<void *>(&WrapClear);
    gWrappedVT[20] = reinterpret_cast<void *>(&WrapPurge);
    vt = gWrappedVT;
}

// The keys: few types and instances, so that types repeat (GetKeysOfType) and instances collide (buckets).
Key MakeKey(std::mt19937 &rng)
{
    static const uint32_t kTypes[] = {0xac4f8687, 0x7ba3838c, 0x49596978, 0};
    static const uint32_t kInstances[] = {0, 1, 2, 3, 8, 9, 16, 0x80000001, 0xffffffff};
    Key k;
    k.mWords[0] = kTypes[rng() % 4];
    k.mWords[1] = rng() % 3 ? 0x7f000000 : rng() % 4;
    k.mWords[2] = kInstances[rng() % 9];
    k.mWords[3] = rng() % 4 ? 0 : rng() % 3;
    return k;
}

// ---- calling a slot through the object's own vtable ----
template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}
uint32_t CallBool(void *object, unsigned slot) { return Call<uint32_t>(object, slot) & 0xff; }
template <class... A> uint32_t CallBool(void *object, unsigned slot, A... args)
{
    return Call<uint32_t>(object, slot, args...) & 0xff;
}

// ---- the state ----
void LogState(void *object)
{
    const auto *w = static_cast<const uint32_t *>(object);
    // The map at +4: load factor, list head, size, bucket vector (begin, end, capacity end), mask, max index.
    const uint32_t head = w[2];
    Log({0xc0, w[1], w[3], w[7], w[8]});
    std::vector<uint32_t> nodes;
    {
        difftest::NotCounted bookkeeping;
        uint32_t node = *reinterpret_cast<const uint32_t *>(uintptr_t(head));
        while (node != head && nodes.size() < 1000) {
            nodes.push_back(node);
            node = *reinterpret_cast<const uint32_t *>(uintptr_t(node));
        }
    }
    Log({0xc1, uint32_t(nodes.size())});
    for (uint32_t node : nodes) {
        const auto *n = reinterpret_cast<const uint32_t *>(uintptr_t(node));
        Log({n[2], n[3], n[4], n[5], FakeId(reinterpret_cast<const void *>(uintptr_t(n[6])))});
    }
    auto position = [&](uint32_t p) -> uint32_t {
        if (p == head) return 0xffff;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i] == p) return uint32_t(i);
        return 0xbad;
    };
    const auto *begin = reinterpret_cast<const uint32_t *>(uintptr_t(w[4]));
    const auto *end = reinterpret_cast<const uint32_t *>(uintptr_t(w[5]));
    const auto *cap = reinterpret_cast<const uint32_t *>(uintptr_t(w[6]));
    Log({0xc2, uint32_t(end - begin), uint32_t(cap - begin)});
    for (const uint32_t *b = begin; b != end; ++b) Log({position(*b)});
    Log({0xc3, w[9], w[10] & 0xff, FakeId(reinterpret_cast<const void *>(uintptr_t(w[11]))), w[12]});
    for (const Fake &f : gResources) Log({uint32_t(f.refs)});
    for (const Fake &f : gFilters) Log({uint32_t(f.refs)});
}

void LogKeys(const KeyList &keys)
{
    Log({0xd0, uint32_t(keys.size()), uint32_t(keys.capacity())});
    for (const Key &k : keys) LogKey(&k);
}

void *FilterArg(std::mt19937 &rng)
{
    const unsigned pick = rng() % (kFilters + 2);
    return pick < kFilters ? &gFilters[pick] : nullptr;
}

// One side: the object built by `build`, the script for `seed` (with `bias`, a slot to favour), the end.
std::vector<uint32_t> RunSide(void *(*build)(difftest::Context &), difftest::Context &ctx, uint32_t seed, unsigned bias)
{
    {
        difftest::NotCounted bookkeeping;
        gLog.clear();
        gLog.reserve(1 << 16);
    }
    ResetFakes(seed);
    std::mt19937 rng(seed * 7919 + bias);
    std::vector<Key> pool;
    {
        difftest::NotCounted bookkeeping;
        for (int i = 0; i < 24; ++i) pool.push_back(MakeKey(rng));
    }
    auto key = [&]() -> const Key * { return &pool[rng() % pool.size()]; };
    difftest::AllocationCounter counter;
    void *object = build(ctx);
    Log({0x10, object ? 1u : 0u});
    LogState(object);
    WrapVTable(object);
    Log({0x11, Call<uint32_t>(object, 1)}); // AddRef: the old count
    const int steps = 60 + int(rng() % 120);
    for (int step = 0; step < steps; ++step) {
        unsigned op = rng() % 24;
        if (bias && rng() % 3 == 0) op = bias;
        Log({0x20, op});
        switch (op) {
        case 0: {
            static const uint32_t kIids[] = {1, 0x6bb923e4, 0x8865cd89, 0};
            void *out = reinterpret_cast<void *>(0x5e5e5e5e);
            const uint32_t r = CallBool(object, 0, kIids[rng() % 4], &out);
            Log({r, out == object ? 1u : out == reinterpret_cast<void *>(0x5e5e5e5e) ? 2u : 3u});
            break;
        }
        case 1: Log({Call<uint32_t>(object, 1)}); break;
        case 2: { // Release only while it would not destroy the object
            if (static_cast<const uint32_t *>(object)[12] > 1) Log({Call<uint32_t>(object, 2)});
            break;
        }
        case 3: Call<void>(object, 3); break; // an empty function: nothing to compare but the state
        case 4: if (rng() % 4 == 0) Call<void>(object, 4); break;
        case 5: Call<void>(object, 5, FilterArg(rng)); break;
        case 6: Log({FakeId(Call<void *>(object, 6))}); break;
        case 7: {
            void *out = reinterpret_cast<void *>(0x5e5e5e5e);
            const uint32_t r = CallBool(object, 7, key(), &out);
            Log({r, out == reinterpret_cast<void *>(0x5e5e5e5e) ? 0x5e5eu : FakeId(out)});
            break;
        }
        case 8:
        case 21:
        case 22:
        case 23: Log({CallBool(object, 8, key(), &gResources[rng() % kResources])}); break;
        case 9: Log({CallBool(object, 9, key())}); break;
        case 10: Log({CallBool(object, 10, key())}); break;
        case 11: Log({CallBool(object, 11, key())}); break;
        case 12:
        case 13: {
            KeyList *keys = nullptr;
            {
                difftest::NotCounted bookkeeping;
                if (rng() % 4) {
                    keys = new KeyList;
                }
            }
            if (keys && rng() % 3 == 0) keys->push_back(*key()); // appended to, not cleared
            const uint32_t r = op == 12 ? Call<uint32_t>(object, 12, keys, FilterArg(rng))
                                        : Call<uint32_t>(object, 13, keys, pool[rng() % pool.size()].mWords[0]);
            Log({r});
            if (keys) {
                LogKeys(*keys);
                KeyList().swap(*keys);
                difftest::NotCounted bookkeeping;
                delete keys;
            }
            break;
        }
        case 14: {
            const unsigned n = rng() % 2 ? 1 : 1 + rng() % 140;
            for (unsigned i = 0; i < n; ++i) Log({CallBool(object, 14, uint32_t(rng()))});
            break;
        }
        case 15: Log({Call<uint32_t>(object, 15) & 0xff}); break;
        case 16: Call<void>(object, 16, uint32_t(rng() % 3 == 0 ? 0x1234500 : rng() % 2 ? 0x1234501 : 0x0000ff02)); break;
        case 17: Log({CallBool(object, 17)}); break;
        case 18: if (rng() % 4 == 0) Call<void>(object, 18); break;
        case 20: Log({CallBool(object, 20)}); break;
        case 19: { // the use counts PurgeUnused reads: fixed, or decaying from 1..12 (a purge on each of up to 12 passes)
            const bool decay = rng() % 3 == 0;
            uint32_t order[kResources];
            for (unsigned i = 0; i < kResources; ++i) order[i] = i + 1;
            std::shuffle(order, order + kResources, rng);
            for (unsigned i = 0; i < kResources; ++i) {
                gResources[i].decay = decay;
                gResources[i].uses = decay ? order[i] : rng() % 3;
            }
            if (decay && rng() % 2) { // every resource cached under its own key, then a generation: up to 12 passes
                for (unsigned i = 0; i < kResources; ++i) Log({CallBool(object, 8, &pool[i], &gResources[i])});
                Log({CallBool(object, 17)});
            }
            break;
        }
        }
        LogState(object);
    }
    if (rng() % 2) {
        Call<void>(object, 4);
        LogState(object);
    }
    while (static_cast<const uint32_t *>(object)[12] > 1) Log({Call<uint32_t>(object, 2)});
    difftest::FreeRecorder rec;
    Log({0x30, Call<uint32_t>(object, 2)});
    rec.Stop();
    size_t sizes[8];
    const size_t n = rec.SortedSizes(sizes, 8);
    Log({0x31, uint32_t(n)});
    for (size_t i = 0; i < n && i < 8; ++i) Log({uint32_t(sizes[i])});
    counter.Stop();
    for (const Fake &f : gResources) Log({uint32_t(f.refs)});
    for (const Fake &f : gFilters) Log({uint32_t(f.refs)});
    Log({0x32, uint32_t(counter.Blocks()), uint32_t(counter.Bytes()), uint32_t(counter.Allocations())});
    difftest::NotCounted bookkeeping;
    return gLog;
}

void *BuildOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cGZPersistCacheStrategyBasic::kObjectSize);
    std::memset(memory, 0xcd, cGZPersistCacheStrategyBasic::kObjectSize);
    return ctx.Original<void *(__thiscall *)(void *)>(kCtor)(memory);
}
void *BuildOurs(difftest::Context &)
{
    void *memory = ::operator new(cGZPersistCacheStrategyBasic::kObjectSize);
    std::memset(memory, 0xcd, cGZPersistCacheStrategyBasic::kObjectSize);
    return cGZPersistCacheStrategyBasic::Construct(memory);
}

void Compare(difftest::Context &ctx, unsigned bias, uint32_t seeds)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        const std::vector<uint32_t> a = RunSide(&BuildOriginal, ctx, seed, bias);
        const std::vector<uint32_t> b = RunSide(&BuildOurs, ctx, seed, bias);
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i == a.size() && i == b.size()) continue;
        // The step the difference is in: the last op marker before it.
        uint32_t lastOp = 0xffff;
        for (size_t j = 0; j + 1 < i; ++j)
            if (a[j] == 0x20) lastOp = a[j + 1]; // approximate: a data word 0x20 can mislead
        ctx.Fail("bias %u seed %u: log word %zu of %zu / %zu: original 0x%x, ours 0x%x (near op %u)", bias, seed, i,
                 a.size(), b.size(), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0, lastOp);
        return;
    }
}

} // namespace

// The constructor and the whole script (every slot, mixed).
DIFF_TEST(GZPersistCacheStrategyBasic_Script, 0x9b35f0)
{
    Compare(ctx, 0, 300);
}

// The same with one slot favoured, per slot RVA.
DIFF_TEST(GZPersistCacheStrategyBasic_Slots, 0x9b36d0)
{
    for (unsigned bias : {0u, 1u, 4u, 5u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 17u, 18u, 19u, 20u}) {
        Compare(ctx, bias, 40);
        if (!ctx.failures.empty()) return;
    }
}

DIFF_TEST(GZPersistCacheStrategyBasic_VTable, 0xe60148)
{
    void *const *table = GameAddress<void *const>(kGameVTable);
    for (const VSlot &v : kSlots) {
        ++ctx.cases;
        if (table[v.slot] != GameAddress<void>(v.rva)) ctx.Fail("vtable slot %u is not 0x%x", v.slot, v.rva);
    }
    for (const VSlot &v : kFolded) {
        ++ctx.cases;
        if (table[v.slot] != GameAddress<void>(v.rva)) ctx.Fail("vtable slot %u is not 0x%x (folded)", v.slot, v.rva);
    }
}
