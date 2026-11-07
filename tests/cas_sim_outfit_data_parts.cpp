// Differential test for nTSCASCore::cTSSimOutfitData slot 104 (0xb9c110, PreloadParts).
//
// Each trial builds a random part map (a real std::map<uint32_t, vector of 16-byte keys>, whose head pointer goes to
// +0x114 of both sides' objects), a pool of fake resources (Release, age mask, key, item list) and fake items (name,
// key), and fakes for the two cached services the function reads (the resource manager at 0x10fc6bc and the part
// info service at 0x10fc6c0). The object's vtable is a copy of the original's with slot 104 (the recursion) and slot
// 25 (the optional part bits) replaced by recorders. Every call into a fake is recorded with the ids of the objects
// and the contents of the keys passed; the event lists and the object bytes are compared.
// Not compared: whether the item vectors handed out by the fake resources are freed (both sides free them through
// ServiceBlockVector::FreeBlock / 0x195720; the harness has no cheap per-block check for the allocator service).
#include "difftest.h"
#include "simantics_autonomy_records.h" // GameAllocatorFixture
#include "cas/cTSSimOutfitData.h"
#include "rz/RZMemory.h"

#include <array>
#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

using Obj = nTSCASCore::cTSSimOutfitData;
using Key = std::array<uint8_t, 16>;

constexpr int kObjectSize = 0x200;
constexpr int kResources = 6;
constexpr int kItems = 8;
constexpr uint32_t kPartType = 0xebcf3e20;

std::vector<uint32_t> gEvents;

void RecordKey(const void *key)
{
    if (!key) { // a listed part whose key vector is empty passes its null begin
        gEvents.push_back(0xdead0000u);
        return;
    }
    uint32_t words[4];
    std::memcpy(words, key, sizeof(words));
    for (uint32_t w : words) gEvents.push_back(w);
}

struct OutfitFake {
    void **vt;
    uint32_t id;
};

struct Item : OutfitFake {
    const char *name;
    Key key;
};

struct Resource : OutfitFake {
    uint16_t ageMask;
    Key key;
    std::vector<int> items;
};

struct World {
    Item items[kItems];
    Resource resources[kResources];
    uint32_t seed = 0;
    uint32_t partFlags = 0;
    uint32_t optionalBits = 0;
};
World *gWorld = nullptr;

const char *const kNames[] = {"cap", "CAP", "Cap", "boot", "coat", "veil"};

// ---- Fakes ---------------------------------------------------------------------------------------------------------

void __fastcall ResourceRelease(Resource *self, void *) { gEvents.insert(gEvents.end(), {0x10, self->id}); }

uint32_t __fastcall ResourceAgeMask(Resource *self, void *)
{
    gEvents.insert(gEvents.end(), {0x11, self->id});
    return 0xabcd0000u | self->ageMask; // the caller tests only ax
}

void __fastcall ResourceKey(Resource *self, void *, void *out)
{
    gEvents.insert(gEvents.end(), {0x12, self->id});
    std::memcpy(out, self->key.data(), 16);
}

void __fastcall ResourceItems(Resource *self, void *, void **vector)
{
    gEvents.insert(gEvents.end(), {0x13, self->id, static_cast<uint32_t>(vector[0] != nullptr)});
    const size_t n = self->items.size();
    void **block = n ? static_cast<void **>(RZMemory::Malloc(n * 4 + 4)) : nullptr;
    for (size_t i = 0; i < n; ++i) block[i] = &gWorld->items[self->items[i]];
    vector[0] = block;
    vector[1] = block ? block + n : nullptr;
    vector[2] = block ? block + n + 1 : nullptr;
}

const char *__fastcall ItemName(Item *self, void *)
{
    gEvents.insert(gEvents.end(), {0x20, self->id});
    return self->name;
}

const void *__fastcall ItemKey(Item *self, void *, void *out)
{
    gEvents.insert(gEvents.end(), {0x21, self->id});
    std::memcpy(out, self->key.data(), 16);
    return out;
}

uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    return x;
}

bool __fastcall ManagerGet(void *, void *, const void *key, uint32_t type, void **out, uint32_t a, uint32_t b)
{
    gEvents.insert(gEvents.end(), {0x30, type, a, b, static_cast<uint32_t>(*out != nullptr)});
    RecordKey(key);
    if (type != kPartType || !key) return false; // 0xb9bec0's own lookups find nothing
    uint32_t words[4];
    std::memcpy(words, key, sizeof(words));
    const uint32_t h = Mix(words[0] ^ gWorld->seed);
    Resource *r = &gWorld->resources[h % kResources];
    switch ((h >> 8) % 5) {
    case 0: return false;
    case 1: *out = r; return false; // failure that still hands out an object (released by the caller)
    default: *out = r; return true;
    }
}

void __fastcall ManagerPreload(void *, void *, const void *key, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.insert(gEvents.end(), {0x31, a, b, c, d});
    RecordKey(key);
}

uint32_t __fastcall PartInfoFlags(void *, void *, uint32_t part)
{
    gEvents.insert(gEvents.end(), {0x40, part});
    return gWorld->partFlags;
}

void __fastcall OutfitRecurse(void *, void *, uint32_t age, uint32_t part)
{
    gEvents.insert(gEvents.end(), {0x50, age, part});
}

uint32_t __fastcall OutfitOptionalBits(void *, void *)
{
    gEvents.push_back(0x51);
    return gWorld->optionalBits;
}

void *gResourceVt[13];
void *gItemVt[6];
void *gManagerVt[7];
void *gPartInfoVt[6];

void InitVTables()
{
    gResourceVt[2] = reinterpret_cast<void *>(&ResourceRelease);
    gResourceVt[7] = reinterpret_cast<void *>(&ResourceAgeMask);
    gResourceVt[10] = reinterpret_cast<void *>(&ResourceKey);
    gResourceVt[12] = reinterpret_cast<void *>(&ResourceItems);
    gItemVt[4] = reinterpret_cast<void *>(&ItemName);
    gItemVt[5] = reinterpret_cast<void *>(&ItemKey);
    gManagerVt[5] = reinterpret_cast<void *>(&ManagerGet);
    gManagerVt[6] = reinterpret_cast<void *>(&ManagerPreload);
    gPartInfoVt[5] = reinterpret_cast<void *>(&PartInfoFlags);
}

Key RandomKey(std::mt19937 &rng)
{
    Key k;
    for (auto &b : k) b = static_cast<uint8_t>(rng());
    return k;
}

void BuildWorld(World &w, std::mt19937 &rng)
{
    w.seed = rng();
    w.partFlags = rng() % 2 ? rng() : (rng() & 1);
    w.optionalBits = rng() % 4 == 0 ? 0xffffffffu : rng();
    for (int i = 0; i < kItems; ++i) {
        w.items[i].vt = gItemVt;
        w.items[i].id = 0x100 + i;
        w.items[i].name = kNames[rng() % 6];
        w.items[i].key = RandomKey(rng);
    }
    for (int i = 0; i < kResources; ++i) {
        Resource &r = w.resources[i];
        r.vt = gResourceVt;
        r.id = 0x200 + i;
        const uint32_t m = rng() % 4;
        r.ageMask = m == 0 ? 0 : static_cast<uint16_t>(m == 1 ? 0xffff : rng());
        r.key = RandomKey(rng);
        r.items.clear();
        const int n = static_cast<int>(rng() % 5); // 0..4 items; one item takes the single-candidate path
        for (int j = 0; j < n; ++j) r.items.push_back(static_cast<int>(rng() % kItems));
    }
}

using PartMap = std::map<uint32_t, std::vector<Key>>;

const uint32_t kAges[] = {0xffffffffu, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x3, 0x88};
const uint32_t kSingleParts[] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000};

void BuildMap(PartMap &map, std::mt19937 &rng, uint32_t age, uint32_t part)
{
    map.clear();
    auto add = [&](uint32_t key) {
        std::vector<Key> keys;
        const int n = static_cast<int>(rng() % 4);
        for (int i = 0; i < n; ++i) keys.push_back(RandomKey(rng));
        map[key] = keys;
    };
    const uint32_t a = age == 0xffffffffu ? 1 : age;
    if (rng() % 5) add(a << 16 | (part & 0xffff));
    if (rng() % 5 == 0) add(8u << 16 | (part & 0xffff)); // the age 0x80 -> 8 substitution
    if (rng() % 2) add(0x800000u | (part & 0xffff));
    for (uint32_t k : {0x800004u, 0x800010u, 0x800008u}) {
        if (rng() % 2) add(k);
    }
    const int noise = static_cast<int>(rng() % 6);
    for (int i = 0; i < noise; ++i) {
        add(rng() % 2 ? rng() : (kAges[rng() % 11] << 16 | kSingleParts[rng() % 14]));
    }
}

struct Side {
    uint8_t front[32];
    uint8_t body[kObjectSize];
    uint8_t back[32];
};

struct GlobalsSaved {
    void *manager, *partInfo, *queried;
};

// While alive, the image's allocator service cache (0x10c95d4, guard 0x10c95d8) holds an object with the game's own
// allocator service vtable (0xda3cd4), so blocks from RZMemory::Malloc can be freed by the original's 0x195720.
struct AllocatorScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16] = {};
    explicit AllocatorScope(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        const void *vtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
    }
    AllocatorScope(const AllocatorScope &) = delete;
    ~AllocatorScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

} // namespace

DIFF_TEST(cTSSimOutfitData_PreloadParts, 0xb9c110)
{
    InitVTables();
    GameAllocatorFixture gameAllocator;
    AllocatorScope allocator(ctx);
    void **managerCache = static_cast<void **>(ctx.image.At(0x10fc6bc));
    void **partInfoCache = static_cast<void **>(ctx.image.At(0x10fc6c0));
    void **queriedCache = static_cast<void **>(ctx.image.At(0x10f8fcc));
    const GlobalsSaved saved{*managerCache, *partInfoCache, *queriedCache};
    OutfitFake manager{gManagerVt, 1};
    OutfitFake partInfo{gPartInfoVt, 2};
    uint32_t queriedDummy = 0;
    *managerCache = &manager;
    *partInfoCache = &partInfo;
    *queriedCache = &queriedDummy;

    const void **vt = new const void *[116];
    std::memcpy(vt, ctx.image.At(Obj::kVTable), 116 * sizeof(void *));
    vt[104] = reinterpret_cast<const void *>(&OutfitRecurse);
    vt[25] = reinterpret_cast<const void *>(&OutfitOptionalBits);

    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xb9c110);
    static World world;
    gWorld = &world;
    static Side a, b;
    PartMap map;
    std::mt19937 rng(0x104b9c);

    for (int trial = 0; trial < 6000; ++trial) {
        BuildWorld(world, rng);
        const uint32_t age = rng() % 6 == 0 ? rng() : kAges[rng() % 11];
        uint32_t part;
        switch (rng() % 8) {
        case 0: part = rng() % 2 ? 0 : 0xffffffffu; break;
        case 1: part = rng() & 0x3fff; break;
        case 2: part = rng(); break;
        case 3: part = (rng() % 3 == 0) ? 0x4000u << (rng() % 4) : 3u; break;
        case 4: part = 4u << (rng() % 3); break;
        default: part = kSingleParts[rng() % 14]; break;
        }
        BuildMap(map, rng, age, part);

        uint8_t init[kObjectSize];
        for (auto &x : init) x = static_cast<uint8_t>(rng());
        const void *vtp = vt;
        std::memcpy(init, &vtp, 4);
        void *head = *reinterpret_cast<void **>(&map);
        std::memcpy(init + 0x114, &head, 4);

        std::vector<uint32_t> ev[2];
        for (int side = 0; side < 2; ++side) {
            Side &s = side == 0 ? a : b;
            std::memset(s.front, 0xA5, sizeof(s.front));
            std::memset(s.back, 0x5A, sizeof(s.back));
            std::memcpy(s.body, init, sizeof(init));
            gEvents.clear();
            if (side == 0) {
                orig(s.body, age, part);
            } else {
                reinterpret_cast<Obj *>(s.body)->PreloadParts(age, part);
            }
            ev[side] = gEvents;
        }
        ++ctx.cases;
        if (ev[0] != ev[1]) {
            ctx.Fail("PreloadParts(age %08x, part %08x): calls differ (original %zu events, ours %zu)", age, part,
                     ev[0].size(), ev[1].size());
        }
        if (std::memcmp(a.body, b.body, sizeof(a.body)) != 0) ctx.Fail("PreloadParts: object state differs");
        bool canaries = true;
        for (int i = 0; i < 32; ++i) {
            canaries = canaries && a.front[i] == 0xA5 && b.front[i] == 0xA5 && a.back[i] == 0x5A && b.back[i] == 0x5A;
        }
        if (!canaries) ctx.Fail("PreloadParts: canaries overwritten");
        if (ctx.failures.size() > 5) break;
    }

    *managerCache = saved.manager;
    *partInfoCache = saved.partInfo;
    *queriedCache = saved.queried;
    delete[] vt;
}
