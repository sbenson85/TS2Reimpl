// Differential tests for cTSPersistCacheStrategyGenerational's cache: Get (slot 7, 0x1fbf00), Add (slot 8, 0x1fb580),
// Remove (slot 9, 0x1fc160), Contains (slot 10, 0x1fbff0), GetKeys (slot 12, 0x1fbde0), GetKeysOfType (slot 13,
// 0x1fbe70), Clear (slot 18, 0x1fb7b0), Evict (slot 23, 0x1fb870) and the deleting destructor (0x1fb4f0).
//
// The object is built on both sides the same way from a seed: four generation lists and the key index as real MSVC
// 14.40 containers (the game's layout), filled with entries for svcfx fakes (tests/ts_services_fixture.h) standing for
// resources. A resource's slot 3 writes its key (keys drawn so that hash sums collide), slot 11 answers its use count,
// slot 4 logs. Some index keys are stale (not the resource's own key) or aliases (a second key for the same node), so
// Evict's index scan runs; Get and Remove worlds also hold entries without a resource. After the call, compared: the
// result, the log of every fake call, each generation (its size field, every node's resource, generation, age, recent
// flag and stamp; the pad byte is not compared: Add leaves an uninitialised stack byte there), the index (load factor,
// size, mask, max index, every node's key and the node it names in list terms, every bucket's first and last node in
// index order), the tick, count and limit, every fake's reference count, and the call's heap balance (blocks, bytes).
#include "ts_services_fixture.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ts/cTSPersistCacheStrategyGenerational.h"

#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using Cache = cTSPersistCacheStrategyGenerational;
using Key = Cache::Key;
using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Norm;
using svcfx::Obj;

constexpr unsigned kResources = 16; // fakes 0..15
constexpr unsigned kFilter = 16;
constexpr unsigned kSpare = 17; // Add's resource when every resource has an entry
constexpr uint32_t kObjectTag = 0x7e000000;
constexpr uint32_t kTableTag = 0x7c000000;

alignas(16) unsigned char gStorage[0x100];
alignas(16) uint32_t gTable[10];
Key gKeys[kResources];
uint32_t gUses[kResources];

Cache *Object() { return reinterpret_cast<Cache *>(gStorage); }

unsigned FakeIndex(const Obj *self) { return self->id - 200; }

uint32_t ResourceKeyOut(Obj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<Key *>(uintptr_t(args[0]));
    *out = gKeys[FakeIndex(self) % kResources];
    return args[0];
}

uint32_t ResourceUses(Obj *self, uint32_t *) { return gUses[FakeIndex(self) % kResources]; }

uint32_t FilterAccepts(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const Key *>(uintptr_t(args[0]));
    Ev(key->mWords[0]);
    return (key->mWords[0] ^ key->mWords[2]) & 1 ? 0xabcd0001u : 0xabcd0100u; // only al counts
}

struct WorldOptions {
    bool nullResources = false; // entries without a resource (Get and Remove only)
};

// One seed's world: keys, use counts, table, fields and the containers. Both sides call it with the same seed.
struct World {
    std::vector<Key> indexKeys; // every key in the index
    std::vector<unsigned> unused; // resources with no entry
};

Key RandomKey(std::mt19937 &rng)
{
    Key k;
    k.mWords[0] = 0x1000u + rng() % 3;
    k.mWords[1] = rng() % 4;
    k.mWords[2] = uint32_t(rng());
    k.mWords[3] = rng() % 2 ? 0 : uint32_t(rng());
    return k;
}

World Build(uint32_t seed, const WorldOptions &options)
{
    std::mt19937 rng(seed);
    World w;
    svcfx::ResetAll();
    for (unsigned i = 0; i <= kSpare; ++i) {
        if (i == kFilter) continue;
        F(i).Set(3, "p", 0, &ResourceKeyOut);
        F(i).Set(11, 0, 0, &ResourceUses);
        F(i).Set(4, "p");
        if (i >= kResources) continue; // the spare shares resource 1's key and uses
        gUses[i] = rng() % 3 == 0 ? 2 + rng() % 2 : rng() % 2;
        gKeys[i] = RandomKey(rng);
        if (i > 0 && rng() % 5 == 0) { // same hash sum, different key
            gKeys[i] = gKeys[i - 1];
            std::swap(gKeys[i].mWords[0], gKeys[i].mWords[2]);
        }
    }
    F(kFilter).Set(3, "p", 0, &FilterAccepts);

    std::memset(gStorage, 0, sizeof(gStorage));
    auto *words = reinterpret_cast<uint32_t *>(gStorage);
    words[0] = svcfx::P(GameAddress<void>(Cache::kVTable));
    words[1] = svcfx::P(GameAddress<void>(Cache::kSubVTable));
    words[2] = 1;
    Cache *c = Object();
    for (uint32_t g = 0; g < 4; ++g) new (&c->Gen(g)) Cache::Generation();
    new (&c->Map()) Cache::Index();

    const uint32_t tick = rng() % 4 == 0 ? uint32_t(rng()) : 20 + rng() % 10;
    for (unsigned i = 0; i < kResources; ++i) {
        if (rng() % 4 == 0) {
            w.unused.push_back(i);
            continue;
        }
        Cache::Entry e{};
        e.mResource = options.nullResources && rng() % 4 == 0 ? nullptr : static_cast<void *>(&F(i));
        e.mGeneration = uint8_t(rng() % 4);
        e.mAge = uint8_t(rng() % 4);
        e.mRecent = uint8_t(rng() % 2);
        e.mPad = 0;
        e.mStamp = tick - rng() % 8;
        Cache::Generation &list = c->Gen(e.mGeneration);
        const bool front = rng() % 2;
        const auto node = front ? list.insert(list.begin(), e) : list.insert(list.end(), e);
        Key key = gKeys[i];
        if (rng() % 7 == 0) key = RandomKey(rng); // stale
        c->Map()[key] = node;
        if (rng() % 8 == 0) c->Map()[RandomKey(rng)] = node; // alias
    }
    for (const auto &item : c->Map()) w.indexKeys.push_back(item.first);
    uint32_t entries = 0;
    for (uint32_t g = 0; g < 4; ++g) entries += uint32_t(c->Gen(g).size());

    words[0x50 / 4] = tick;
    words[0x54 / 4] = rng() % 6 == 0 ? entries + rng() % 3 : entries;
    words[0x58 / 4] = rng() % 5 == 0 ? 0xffffffffu : uint32_t(rng() % (entries + 3));
    words[0x5c / 4] = svcfx::P(gTable);
    gTable[0] = rng() % 6;                                   // minimum age
    gTable[1] = rng() % 4;                                   // vf22's budget word (not used here)
    for (int i = 2; i < 6; ++i) gTable[i] = rng() % 20;      // generation limits (vf22)
    for (int i = 6; i < 10; ++i) gTable[i] = rng() % 4 == 0 ? 0x100u | (rng() % 3) : 1 + rng() % 3; // thresholds
    svcfx::AddRange(gStorage, sizeof(gStorage), kObjectTag);
    svcfx::AddRange(gTable, sizeof(gTable), kTableTag);
    return w;
}

// A node's place in list terms: generation << 8 | position, or 0xdead when it is in no list.
uint32_t Place(const void *node)
{
    Cache *c = Object();
    for (uint32_t g = 0; g < 4; ++g) {
        uint32_t i = 0;
        for (auto it = c->Gen(g).begin(); it != c->Gen(g).end(); ++it, ++i)
            if (&*it == node) return g << 8 | i;
    }
    return 0xdead;
}

void Capture(Blob &b)
{
    Cache *c = Object();
    const auto *words = reinterpret_cast<const uint32_t *>(gStorage);
    for (uint32_t g = 0; g < 4; ++g) {
        b.push_back(words[(0x0c + 8 * g) / 4 + 1]); // size field
        for (const Cache::Entry &e : c->Gen(g)) {
            b.push_back(Norm(svcfx::P(e.mResource)));
            b.push_back(e.mGeneration | e.mAge << 8 | e.mRecent << 16);
            b.push_back(e.mStamp);
        }
    }
    const uint32_t *m = words + 0x2c / 4;
    b.push_back(m[0]); // max load factor (bits)
    b.push_back(m[2]); // size
    b.push_back(m[6]); // mask
    b.push_back(m[7]); // max index
    std::map<uint32_t, uint32_t> order; // index node -> position
    uint32_t position = 0;
    for (auto it = c->Map().begin(); it != c->Map().end(); ++it, ++position) {
        for (uint32_t w : it->first.mWords) b.push_back(w);
        b.push_back(Place(&*it->second));
        order[svcfx::P(&*it)] = position;
    }
    // Buckets: first and last node of each, by position in the index (0xffff: the list head).
    const auto *vecBegin = reinterpret_cast<const uint32_t *>(uintptr_t(m[3]));
    const auto *vecEnd = reinterpret_cast<const uint32_t *>(uintptr_t(m[4]));
    b.push_back(uint32_t(vecEnd - vecBegin));
    for (const uint32_t *p = vecBegin; p != vecEnd; ++p) {
        // The node pointers in the buckets are the list nodes' addresses; our iteration gave each value's address
        // (+8 into the node).
        const auto found = order.find(*p + 8);
        b.push_back(*p == m[1] ? 0xffffu : found == order.end() ? 0xbadu : found->second);
    }
    b.push_back(words[0x50 / 4]);
    b.push_back(words[0x54 / 4]);
    b.push_back(words[0x58 / 4]);
    svcfx::CaptureFakes(b);
}

void Teardown()
{
    Cache *c = Object();
    std::destroy_at(&c->Map());
    for (uint32_t g = 0; g < 4; ++g) std::destroy_at(&c->Gen(g));
}

template <class Call> Blob RunSide(uint32_t seed, const WorldOptions &options, Call &&call)
{
    const World w = Build(seed, options);
    svcfx::gEv.reserve(4096);
    difftest::AllocationCounter heap;
    const uint32_t result = call(w);
    heap.Stop();
    Blob b = svcfx::gEv;
    b.push_back(0xa110c);
    b.push_back(result);
    b.push_back(uint32_t(heap.Blocks()));
    b.push_back(uint32_t(heap.Bytes()));
    Capture(b);
    Teardown();
    return b;
}

Key PickKey(std::mt19937 &rng, const World &w)
{
    if (!w.indexKeys.empty() && rng() % 4 != 0) return w.indexKeys[rng() % w.indexKeys.size()];
    if (rng() % 2) return gKeys[rng() % kResources];
    return RandomKey(rng);
}

} // namespace

DIFF_TEST(TSPersistCacheStrategyGenerational_Evict, 0x1fb870)
{
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed * 7 + 1);
        static const int32_t kLevels[] = {-1, 0, 1, 2, 3, 4, 5, 9};
        static const uint32_t kBudgets[] = {0, 1, 2, 3, 5, 0xffffffffu, 0xffffffffu, 0xffffffffu};
        const int32_t levels = kLevels[rng() % 8];
        const uint32_t force = rng() % 3 == 0 ? 0x101u : rng() % 2 ? 0x100u : 0u; // only the low byte counts
        const uint32_t budget = kBudgets[rng() % 8];
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {}, [&](const World &) -> uint32_t {
                if (side == 0)
                    return ctx.Original<uint32_t(__thiscall *)(void *, int32_t, uint32_t, uint32_t)>(0x1fb870)(
                               Object(), levels, force, budget) & 0xff;
                return Object()->Evict(levels, uint8_t(force), budget);
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Evict", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_Get, 0x1fbf00)
{
    alignas(16) static uint32_t out[2];
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {true}, [&](const World &w) -> uint32_t {
                std::mt19937 rng(seed ^ 0x5eed);
                const Key key = PickKey(rng, w);
                out[0] = 0x0badf00d;
                svcfx::AddRange(out, sizeof(out), 0x7d000000);
                const uint32_t r = side == 0
                    ? ctx.Original<uint32_t(__thiscall *)(void *, const Key *, void **)>(0x1fbf00)(Object(), &key, reinterpret_cast<void **>(out)) & 0xff
                    : uint32_t(Object()->Get(&key, reinterpret_cast<void **>(out)));
                Ev(svcfx::NormOut(out[0], 0x0badf00d));
                return r;
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Get", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_Add, 0x1fb580)
{
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {}, [&](const World &w) -> uint32_t {
                std::mt19937 rng(seed ^ 0xadd);
                const Key key = rng() % 3 == 0 ? PickKey(rng, w) : RandomKey(rng);
                void *resource = w.unused.empty() ? &F(kSpare) : &F(w.unused[rng() % w.unused.size()]);
                return side == 0
                    ? ctx.Original<uint32_t(__thiscall *)(void *, const Key *, void *)>(0x1fb580)(Object(), &key, resource) & 0xff
                    : uint32_t(Object()->Add(&key, resource));
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Add", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_Remove, 0x1fc160)
{
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {true}, [&](const World &w) -> uint32_t {
                std::mt19937 rng(seed ^ 0x4e40);
                const Key key = PickKey(rng, w);
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const Key *)>(0x1fc160)(Object(), &key) & 0xff
                                 : uint32_t(Object()->Remove(&key));
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Remove", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_Contains, 0x1fbff0)
{
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {true}, [&](const World &w) -> uint32_t {
                std::mt19937 rng(seed ^ 0xc0);
                const Key key = PickKey(rng, w);
                return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, const Key *)>(0x1fbff0)(Object(), &key) & 0xff
                                 : uint32_t(Object()->Contains(&key));
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Contains", seed)) break;
    }
}

namespace {
void LogKeys(const Cache::KeyList &keys)
{
    Ev(uint32_t(keys.size()));
    for (const Key &k : keys)
        for (uint32_t w : k.mWords) Ev(w);
}
} // namespace

DIFF_TEST(TSPersistCacheStrategyGenerational_GetKeys, 0x1fbde0)
{
    svcfx::AllocatorScope allocator; // the key list grows through the allocator service
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {}, [&](const World &) -> uint32_t {
                std::mt19937 rng(seed ^ 0x6e7);
                const bool withFilter = rng() % 2;
                const bool withKeys = rng() % 4 != 0;
                Cache::KeyList keys;
                if (rng() % 2) keys.push_back(RandomKey(rng)); // appended to, not replaced
                if (rng() % 2) keys.reserve(2 + rng() % 8);
                void *filter = withFilter ? &F(kFilter) : nullptr;
                const uint32_t r = side == 0
                    ? ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x1fbde0)(Object(), withKeys ? &keys : nullptr, filter)
                    : Object()->GetKeys(withKeys ? &keys : nullptr, filter);
                LogKeys(keys);
                return r;
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "GetKeys", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_GetKeysOfType, 0x1fbe70)
{
    svcfx::AllocatorScope allocator; // the key list grows through the allocator service
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {}, [&](const World &) -> uint32_t {
                std::mt19937 rng(seed ^ 0x7e9);
                const uint32_t type = 0x1000u + rng() % 4;
                Cache::KeyList keys;
                const bool withKeys = rng() % 4 != 0;
                const uint32_t r = side == 0
                    ? ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x1fbe70)(Object(), withKeys ? &keys : nullptr, type)
                    : Object()->GetKeysOfType(withKeys ? &keys : nullptr, type);
                LogKeys(keys);
                return r;
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "GetKeysOfType", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_Clear, 0x1fb7b0)
{
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            sides[side] = RunSide(seed, {}, [&](const World &) -> uint32_t {
                if (side == 0)
                    ctx.Original<void(__thiscall *)(void *)>(0x1fb7b0)(Object());
                else
                    Object()->Clear();
                return 0;
            });
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "Clear", seed)) break;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_DeletingDestructor, 0x1fb4f0)
{
    // The built object is moved to the heap (the containers hold no pointers to themselves), the watcher at +0x4c is
    // a fake or null, and the destructor runs with flags 0 or 1. Compared: the log, the result, the number and the
    // sorted sizes of the blocks freed, whether the object was freed, its vtable words when it was not, the fakes.
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed * 3 + 7);
        const uint32_t flags = rng() % 2 ? 1u : rng() % 2 ? 0x100u : 0u;
        const bool watcher = rng() % 3 != 0;
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            Build(seed, {});
            if (watcher) reinterpret_cast<uint32_t *>(gStorage)[0x4c / 4] = svcfx::P(&F(kSpare));
            auto *object = static_cast<Cache *>(::operator new(Cache::kObjectSize));
            std::memcpy(static_cast<void *>(object), gStorage, Cache::kObjectSize);
            reinterpret_cast<uint32_t *>(object)[0] = 0x0b0b0b0b; // both vtables are set anew
            reinterpret_cast<uint32_t *>(object)[1] = 0x0c0c0c0c;
            svcfx::gEv.reserve(1024); // the log's own growth would free its old buffer inside the recording
            difftest::FreeRecorder frees;
            Cache *result = side == 0 ? ctx.Original<Cache *(__thiscall *)(void *, uint32_t)>(0x1fb4f0)(object, flags)
                                      : object->DeletingDestructor(flags);
            frees.Stop();
            Blob &b = sides[side];
            b = svcfx::gEv;
            b.push_back(result == object);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[128];
            const size_t n = frees.SortedSizes(sizes, 128);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(object));
            if ((flags & 1) == 0) {
                const auto *words = reinterpret_cast<const uint32_t *>(object);
                b.push_back(words[0] - svcfx::P(GameAddress<void>(Cache::kVTable)));
                b.push_back(words[1]);
                ::operator delete(object);
            }
            svcfx::CaptureFakes(b);
        }
        if (!svcfx::Same(ctx, sides[0], sides[1], "DeletingDestructor", seed)) break;
    }
}
