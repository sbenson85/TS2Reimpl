// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JY: the morph pushes (slots 48 0xcf54e0, 49 0xcf5770),
// the per-part import (slot 5 0xce09a0) and the face-0 resource cache (face-0 slot 14 0xcf43f0). See cas_body_part_system.h
// for the fixture. The mesh entries, the source object, the part entries and the persist service are fakes built the
// same way for each side; the record code and the containers run for real.
#include "cas_body_part_system.h"
#include "rz/cRZString.h"

#include <map>

namespace {

using namespace bpt;
using nTSCASCore::CasAlloc;
using nTSCASCore::PartMeshEntry;
using nTSCASCore::PartMeshGroup;
using nTSCASCore::PartMeshVertex;
using nTSCASCore::PartPoint;
using nTSCASCore::PartPoints;
using System = nTSCASCore::cTSCASBodyPartSystem;

template <typename T> void PutAt(Scenario &sc, int ifaceOffset, T value)
{
    std::memcpy(sc.bytes + kIfaceOffset + ifaceOffset, &value, sizeof(value));
}

uint32_t Hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t TextHash(const char *s)
{
    return Hash(2166136261u, s, std::strlen(s));
}

uint32_t gScriptSeed;
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

World *gWorld; // the side running

int32_t StackIndex(const BodyPartEntry *e)
{
    return static_cast<int32_t>(e - gWorld->stack->mFirst);
}

// ---- Mesh entries (what 0xcf06e0 collects) --------------------------------------------------------------------------

struct MeshPool {
    std::vector<PartMeshEntry *> entries;
};
MeshPool gPools[2];
MeshPool *gPool;

int32_t EntryIndex(const PartMeshEntry *e)
{
    for (size_t i = 0; i < gPool->entries.size(); ++i)
        if (gPool->entries[i] == e) return static_cast<int32_t>(i);
    return -1;
}

struct MeshSpec {
    uint32_t seed;
    int count;
};

void BuildPool(MeshPool &pool, const MeshSpec &spec, const Iface *s)
{
    std::mt19937 rng(spec.seed);
    const int32_t records = static_cast<int32_t>(s->mStack->mLast - s->mStack->mFirst);
    for (int i = 0; i < spec.count; ++i) {
        auto *e = reinterpret_cast<PartMeshEntry *>(new uint8_t[sizeof(PartMeshEntry)]);
        for (size_t k = 0; k < sizeof(PartMeshEntry); ++k) reinterpret_cast<uint8_t *>(e)[k] = static_cast<uint8_t>(rng());
        static const int32_t kOffsets[] = {0, 0, 0, 1, -1};
        e->mStackIndex = rng() % 6 == 0 ? -1 : records + kOffsets[rng() % 5];
        e->mEnabled = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        e->mKind = rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 4) : s->mLastKind;
        e->mKey = rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 4) : s->mLastKey;
        const int groups = static_cast<int>(rng() % 3);
        auto *g = new PartMeshGroup[groups + 1];
        for (int k = 0; k < groups; ++k) {
            const int n = static_cast<int>(rng() % 3);
            auto *v = new PartMeshVertex[n + 1];
            for (int j = 0; j < n; ++j)
                for (size_t b = 0; b < sizeof(PartMeshVertex); ++b) reinterpret_cast<uint8_t *>(&v[j])[b] = static_cast<uint8_t>(rng());
            g[k] = {v, v + n, v + n};
        }
        e->mGroupsFirst = g;
        e->mGroupsLast = g + groups;
        pool.entries.push_back(e);
    }
}

// The entries' state (index field and every vertex), then the pool freed.
uint32_t PoolState(MeshPool &pool)
{
    uint32_t h = 2166136261u;
    for (PartMeshEntry *e : pool.entries) {
        h = Hash(h, &e->mStackIndex, 4);
        for (PartMeshGroup *g = e->mGroupsFirst; g != e->mGroupsLast; ++g) {
            h = Hash(h, g->mFirst, (g->mLast - g->mFirst) * sizeof(PartMeshVertex));
            delete[] g->mFirst;
        }
        delete[] e->mGroupsFirst;
        delete[] reinterpret_cast<uint8_t *>(e);
    }
    pool.entries.clear();
    return h;
}

struct RawPtrVector {
    void **first, **last, **end;
};

void __fastcall F_cf06e0(void *self, void *, RawPtrVector *out)
{
    gEvents.push_back({0xcf06e0, Rel(self), out->first == nullptr});
    const size_t n = gPool->entries.size();
    if (n == 0) return;
    auto **p = static_cast<void **>(ServiceAlloc(4 * n));
    for (size_t i = 0; i < n; ++i) p[i] = gPool->entries[i];
    out->first = p;
    out->last = p + n;
    out->end = p + n;
}
void __fastcall F_ce18c0(void *self, void *, BodyPartEntry *top, PartMeshEntry *e)
{
    gEvents.push_back({0xce18c0, Rel(self), static_cast<uint32_t>(StackIndex(top)), static_cast<uint32_t>(EntryIndex(e)),
                       FloatBits(top->mWeight)});
}
void __fastcall F_ce1550(void *self, void *, BodyPartEntry *top, PartMeshEntry *e, float from, float to)
{
    gEvents.push_back({0xce1550, Rel(self), static_cast<uint32_t>(StackIndex(top)), static_cast<uint32_t>(EntryIndex(e)),
                       FloatBits(from), FloatBits(to), FloatBits(top->mWeight)});
}
float __fastcall Slot50(void *self, void *, const char *name, uint32_t zero)
{
    gEvents.push_back({50, Rel(self), TextHash(name), zero});
    static const float kValues[] = {0.0f, 0.5f, -2.0f, 1.25f};
    return kValues[Pick() % 4];
}

const float kWeights[] = {0.0f, 0.25f, -0.25f, 0.5f, 1.0f, -1.0f, 2.0f, 0.75f, std::numeric_limits<float>::quiet_NaN()};

// The newest record made a kind-`kind` record named like the query (in another case at times) about half of the time.
const char *MorphQuery(Scenario &sc, std::mt19937 &rng, int32_t kind)
{
    static const char *const kCased[] = {"alpha", "ALPHA", "Alpha", "beta", "BETA", "x", "a_rather_long_name_over_fifteen",
                                         "A_RATHER_LONG_NAME_OVER_FIFTEEN"};
    const char *query = kCased[rng() % 8];
    if (!sc.stack.empty() && rng() % 3 != 0) {
        sc.stack.back().kind = rng() % 5 == 0 ? 3 : kind;
        sc.stack.back().name = kCased[rng() % 8];
        if (rng() % 2) sc.stack.back().name = query;
    }
    return query;
}

void SetLastPart(Scenario &sc, std::mt19937 &rng)
{
    PutAt<uint32_t>(sc, 0x110, rng() % 3);
    PutAt<uint32_t>(sc, 0x114, rng() % 3);
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_AddMorph, 0xcf54e0)
{
    std::mt19937 rng(0xcf54e0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, float, uint8_t)>(0xcf54e0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    Redirect r1(ctx, 0xcf06e0, reinterpret_cast<const void *>(&F_cf06e0));
    Redirect r2(ctx, 0xce18c0, reinterpret_cast<const void *>(&F_ce18c0));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLastPart(sc, rng);
        const char *name = MorphQuery(sc, rng, 1);
        const float weight = rng() % 4 == 0 ? static_cast<float>(static_cast<int>(rng() % 400) - 200) / 64.0f : kWeights[rng() % 9];
        const uint8_t merge = rng() % 4 == 0 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        const MeshSpec mesh{static_cast<uint32_t>(rng()), static_cast<int>(rng() % 4)};
        Compare(ctx, "AddMorph", sc, vt, [&](World &w, bool original) -> uint64_t {
            gWorld = &w;
            gPool = &gPools[original ? 0 : 1];
            BuildPool(*gPool, mesh, w.S());
            if (original) orig(w.S(), name, weight, merge);
            else w.S()->AddMorph(name, weight, merge);
            return PoolState(*gPool);
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_AddNamedWeight, 0xcf5770)
{
    std::mt19937 rng(0xcf5770);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, float)>(0xcf5770);
    VTable vt = CopyVTable(ctx);
    vt.slots[50] = reinterpret_cast<const void *>(&Slot50);
    Env env(ctx);
    Redirect r1(ctx, 0xcf06e0, reinterpret_cast<const void *>(&F_cf06e0));
    Redirect r2(ctx, 0xce1550, reinterpret_cast<const void *>(&F_ce1550));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLastPart(sc, rng);
        const char *name = rng() % 10 == 0 ? nullptr : MorphQuery(sc, rng, 7);
        const float weight = rng() % 4 == 0 ? static_cast<float>(static_cast<int>(rng() % 400) - 200) / 64.0f : kWeights[rng() % 9];
        const MeshSpec mesh{static_cast<uint32_t>(rng()), static_cast<int>(rng() % 4)};
        gScriptSeed = static_cast<uint32_t>(rng());
        Compare(ctx, "AddNamedWeight", sc, vt, [&](World &w, bool original) -> uint64_t {
            gWorld = &w;
            gPool = &gPools[original ? 0 : 1];
            BuildPool(*gPool, mesh, w.S());
            if (original) orig(w.S(), name, weight);
            else w.S()->AddNamedWeight(name, weight);
            return PoolState(*gPool);
        });
    }
}

// ---- Slot 5: per-part data from another object ----------------------------------------------------------------------

namespace {

struct Source {
    const void *const *vtable;
};
Source gSource;
std::array<const void *, 32> gSourceVt;
struct RawIds {
    uint32_t *first, *last, *end;
};
RawIds gIds;
uint32_t gIdStore[8];
PartPoints gPoints[4];
PartPoint gPointStore[4][5];

const RawIds *__fastcall SrcIds(Source *, void *)
{
    gEvents.push_back({0x200e});
    return &gIds;
}
void __fastcall SrcInfo(Source *, void *, uint32_t id, uint32_t *kind, uint32_t *key, uint32_t *c, uint32_t *d)
{
    gEvents.push_back({0x2010, id});
    const uint32_t x = Pick();
    static const uint32_t kKinds[] = {1, 2, 3, 7, 0x12};
    *kind = kKinds[x % 5];
    *key = (x >> 4) % 4;
    *c = (x >> 8) % 3;
    *d = (x >> 12) % 3 == 0 ? 0 : (x >> 16) % 5;
}
const PartPoints *__fastcall SrcPoints(Source *, void *, uint32_t id)
{
    gEvents.push_back({0x200d, id});
    const uint32_t x = Pick() % 5;
    return x == 4 ? nullptr : &gPoints[x];
}

// The part entries 0xcec500 hands out: per side three 0x50-byte blocks, the point vector at +0x40.
uint8_t gEntries[2][3][0x50];
uint8_t (*gSideEntries)[0x50];
uint8_t *__fastcall F_cec500(void *self, void *, const char *name, uint32_t kind, uint32_t key, uint32_t d, uint32_t c, uint32_t z, uint32_t one)
{
    gEvents.push_back({0xcec500, Rel(self), TextHash(name), kind, key, d, c, z, one});
    return gSideEntries[(kind + key * 3 + d) % 3];
}

void SetupEntries(int side, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (auto &e : gEntries[side]) {
        for (auto &b : e) b = static_cast<uint8_t>(rng());
        auto *v = reinterpret_cast<PartPoints *>(e + 0x40);
        const int n = static_cast<int>(rng() % 4), cap = n + static_cast<int>(rng() % 3);
        if (cap == 0) {
            *v = {nullptr, nullptr, nullptr};
            continue;
        }
        auto *p = static_cast<PartPoint *>(ServiceAlloc(cap * sizeof(PartPoint)));
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < 3; ++j) p[k].mXyz[j] = static_cast<float>(rng() % 100);
        *v = {p, p + n, p + cap};
    }
}

uint32_t EntriesState(int side)
{
    uint32_t h = 2166136261u;
    for (auto &e : gEntries[side]) {
        auto *v = reinterpret_cast<PartPoints *>(e + 0x40);
        const uint32_t n = static_cast<uint32_t>(v->mLast - v->mFirst), cap = static_cast<uint32_t>(v->mEnd - v->mFirst);
        h = Hash(h, &n, 4);
        h = Hash(h, &cap, 4);
        if (n) h = Hash(h, v->mFirst, n * sizeof(PartPoint));
        h = Hash(h, e, 0x40);
    }
    return h;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ImportPartData, 0xce09a0)
{
    std::mt19937 rng(0xce09a0);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const char *)>(0xce09a0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gSourceVt = {};
    gSourceVt[14] = reinterpret_cast<const void *>(&SrcIds);
    gSourceVt[16] = reinterpret_cast<const void *>(&SrcInfo);
    gSourceVt[13] = reinterpret_cast<const void *>(&SrcPoints);
    gSource.vtable = gSourceVt.data();
    Redirect r1(ctx, 0xcec500, reinterpret_cast<const void *>(&F_cec500));
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        const int ids = static_cast<int>(rng() % 6);
        for (int k = 0; k < ids; ++k) gIdStore[k] = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 8;
        gIds = {gIdStore, gIdStore + ids, gIdStore + 8};
        for (int k = 0; k < 4; ++k) {
            const int n = k == 0 ? 0 : static_cast<int>(rng() % 6);
            for (int j = 0; j < n; ++j)
                for (int c = 0; c < 3; ++c) gPointStore[k][j].mXyz[c] = static_cast<float>(static_cast<int>(rng() % 1000) - 500) / 8.0f;
            gPoints[k] = {gPointStore[k], gPointStore[k] + n, gPointStore[k] + 5};
        }
        const bool nullSource = rng() % 12 == 0;
        const char *name = rng() % 12 == 0 ? nullptr : RandomName(rng);
        const uint32_t entrySeed = static_cast<uint32_t>(rng());
        Compare(ctx, "ImportPartData", sc, vt, [&](World &w, bool original) -> uint64_t {
            const int side = original ? 0 : 1;
            SetupEntries(side, entrySeed);
            gSideEntries = gEntries[side];
            void *source = nullSource ? nullptr : &gSource;
            if (original) orig(w.S(), source, name);
            else w.S()->ImportPartData(source, name);
            return EntriesState(side);
        });
    }
}

// ---- Face-0 slot 14: the resource cache -----------------------------------------------------------------------------

namespace {

struct FakeRes {
    const void *const *vtable;
    uint32_t index;
};
std::array<const void *, 8> gResVt;
FakeRes gRes[2][6];

uint32_t ResIndex(const void *p)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 6; ++k)
            if (p == &gRes[s][k]) return 0x100u + k;
    return p == nullptr ? 0 : 0xdead;
}

uint32_t __fastcall ResRelease(FakeRes *self, void *)
{
    gEvents.push_back({0x3002, ResIndex(self)});
    return 0;
}

using ResMap = std::map<uint32_t, FakeRes *, std::less<uint32_t>, CasAlloc<std::pair<const uint32_t, FakeRes *>>>;
int gSide;

struct Persist {
    const void *const *vtable;
} gPersist;
std::array<const void *, 8> gPersistVt;

bool __fastcall PersistGet(Persist *, void *, const uint32_t *key, uint32_t iid, FakeRes **out, uint32_t a, uint32_t b)
{
    gEvents.push_back({0x4005, key[0], key[1], key[2], key[3], iid, ResIndex(*out), a, b});
    const uint32_t x = Pick();
    if (x % 4 == 0) return false;
    *out = &gRes[gSide][x % 6];
    return true;
}

uint8_t __fastcall F_cf18b0(void *self, void *, uint32_t kind, uint32_t key, uint32_t flag, cRZString *name)
{
    gEvents.push_back({0xcf18b0, Rel(self), kind, key, flag, static_cast<uint32_t>(name->mString.size())});
    static const char *const kTexts[] = {"abc", "Body_Part_Name", "a_rather_long_part_name_for_the_heap", ""};
    const uint32_t x = Pick();
    if (x % 4 == 0) return static_cast<uint8_t>(x & 0xfe);
    name->mString = kTexts[(x >> 4) % 4];
    return static_cast<uint8_t>((x & 0xfe) | 1);
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_GetPartResource, 0xcf43f0)
{
    std::mt19937 rng(0xcf43f0);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0xcf43f0);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    gResVt = {};
    gResVt[2] = reinterpret_cast<const void *>(&ResRelease);
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 6; ++k) gRes[s][k] = {gResVt.data(), static_cast<uint32_t>(k)};
    gPersistVt = {};
    gPersistVt[5] = reinterpret_cast<const void *>(&PersistGet);
    gPersist.vtable = gPersistVt.data();
    void **const persistCache = ctx.image.At<void *>(0x10faf6c);
    void *const savedPersist = *persistCache;
    *persistCache = &gPersist;
    Redirect r1(ctx, 0xcf18b0, reinterpret_cast<const void *>(&F_cf18b0));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        static const uint32_t kKinds[] = {0, 1, 2, 3, 7, 0x7f, 0x80, 0xff};
        const uint32_t kind = rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : kKinds[rng() % 8];
        const uint32_t key = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        const uint32_t flag = rng() % 2 ? 0 : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 3);
        // existing cache entries: keys near the one asked for, values null or resources
        std::vector<std::pair<uint32_t, int>> cached;
        const uint32_t k0 = ((((flag & 0xff) << 7) | ((kind == 7 ? 3 : kind) & 0x7f)) * 2) | ((flag == 0 ? key : 0) & 1);
        const int n = static_cast<int>(rng() % 5);
        for (int k = 0; k < n; ++k) cached.push_back({rng() % 2 ? k0 + static_cast<uint32_t>(rng() % 3) - 1 : static_cast<uint32_t>(rng() % 0x400), static_cast<int>(rng() % 7) - 1});
        Compare(ctx, "GetPartResource", sc, vt, [&](World &w, bool original) -> uint64_t {
            gSide = original ? 0 : 1;
            auto *map = new (w.full + 0x4c) ResMap();
            for (auto &[k, v] : cached) map->try_emplace(k, v < 0 ? nullptr : &gRes[gSide][v]);
            void *r = original ? orig(w.full, kind, key, flag) : nTSCASCore::cTSCASBodyPartSystemAdapters::GetPartResource(reinterpret_cast<System *>(w.full), nullptr, kind, key, flag);
            uint32_t h = 2166136261u;
            for (auto &[k, v] : *map) {
                const uint32_t idx = ResIndex(v);
                h = Hash(h, &k, 4);
                h = Hash(h, &idx, 4);
            }
            const uint32_t size = static_cast<uint32_t>(map->size());
            std::memset(w.full + 0x4c, 0, 8);
            return (static_cast<uint64_t>(h) << 32) | (size << 16) | ResIndex(r);
        });
    }
    *persistCache = savedPersist;
}
