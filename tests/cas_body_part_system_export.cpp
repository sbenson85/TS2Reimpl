// Differential tests for nTSCASCore::cTSCASBodyPartSystem, lane JZ: the point exports, slots 76 (0xce9840), 77
// (0xce9b30) and 78 (0xce9e20). The source and target objects are fakes (source slots 13, 14, 16; target slots 12, 18),
// the system's entry helper 0xcf04b0 returns mesh entries built per case, and the face's own slots 12, 13, 71 and 72
// are recording fakes in the copied vtable. The target's fake records each point vector it is handed (size, capacity
// and the points' bits). See cas_body_part_system.h for the fixture.
#include "cas_body_part_system.h"

namespace bpt_export {

using namespace bpt;
using nTSCASCore::PartMeshEntry;
using nTSCASCore::PartMeshGroup;
using nTSCASCore::PartMeshVertex;
using nTSCASCore::PartPoint;
using nTSCASCore::PartPoints;

// ---- The source ----------------------------------------------------------------------------------------------------

struct IdList {
    uint32_t *first, *last, *end;
};
uint32_t gIdStore[6];
IdList gIds;
bool gIdsNull;

constexpr int kPointSets = 4;
PartPoint gPointStore[kPointSets][40];
PartPoints gPoints[kPointSets];
int gPointsFor[8]; // per id (id % 8): which point set, -1 for none

struct PartKeyWords {
    uint32_t w[4];
} gKeys[8];

struct Fake {
    const void *const *vtable;
};
std::array<const void *, 40> gSourceVt, gTargetVt;
Fake gSource, gTarget;

const IdList *__fastcall SourceIds(Fake *, void *)
{
    gEvents.push_back({0x5014});
    return gIdsNull ? nullptr : &gIds;
}
const PartPoints *__fastcall SourcePoints(Fake *, void *, uint32_t id)
{
    gEvents.push_back({0x5013, id});
    const int set = gPointsFor[id % 8];
    return set < 0 ? nullptr : &gPoints[set];
}
void __fastcall SourceKey(Fake *, void *, uint32_t id, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    gEvents.push_back({0x5016, id});
    const PartKeyWords &k = gKeys[id % 8];
    *a = k.w[0];
    *b = k.w[1];
    *c = k.w[2];
    *d = k.w[3];
}

uint32_t Hash(uint32_t h, const void *p, size_t n)
{
    const auto *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

size_t gHashLimit; // how many leading points of a handed vector the target hashes (slot 76 leaves the rest unfilled)

struct PointVec {
    PartPoint *first, *last, *end;
};
void __fastcall TargetClear(Fake *, void *) { gEvents.push_back({0x6018}); }
void __fastcall TargetSet(Fake *, void *, uint32_t id, const PointVec *v)
{
    const size_t size = static_cast<size_t>(v->last - v->first);
    const size_t n = size < gHashLimit ? size : gHashLimit;
    gEvents.push_back({0x6012, id, static_cast<uint32_t>(size), static_cast<uint32_t>(v->end - v->first),
                       Hash(2166136261u, v->first, n * sizeof(PartPoint))});
}

// ---- The face's own slots ------------------------------------------------------------------------------------------

uint32_t gMask;
uint32_t __fastcall Slot72(void *self, void *)
{
    gEvents.push_back({72, Rel(self)});
    return gMask;
}
uint8_t __fastcall Slot12(void *self, void *, int32_t id)
{
    gEvents.push_back({12, Rel(self), static_cast<uint32_t>(id)});
    return 1;
}
uint8_t __fastcall Slot13(void *self, void *, int32_t id)
{
    gEvents.push_back({13, Rel(self), static_cast<uint32_t>(id)});
    return 1;
}
void __fastcall Slot71(void *self, void *, uint32_t code) { gEvents.push_back({71, Rel(self), code}); }

// ---- The mesh entries ----------------------------------------------------------------------------------------------

struct Mesh {
    PartMeshVertex vertices[4][5];
    PartMeshGroup groups[4];
    PartMeshEntry entry;
    int count; // vertices over all groups
} gMesh[2];

PartMeshEntry *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, partId});
    return &gMesh[(a ^ b ^ id ^ partId) & 1].entry;
}

float RandomFloat(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return static_cast<float>(static_cast<int>(rng() % 21) - 10);
    case 1: return 0.0f;
    case 2: return -0.0f;
    default: return (static_cast<float>(rng() % 200001) - 100000.0f) / 997.0f;
    }
}

void BuildMesh(std::mt19937 &rng, Mesh &m, int maxVertices)
{
    const int groups = static_cast<int>(rng() % 4);
    m.count = 0;
    for (int g = 0; g < 4; ++g) {
        int n = static_cast<int>(rng() % 5);
        if (g >= groups) n = 0;
        if (m.count + n > maxVertices) n = maxVertices - m.count;
        m.groups[g] = {m.vertices[g], m.vertices[g] + n, m.vertices[g] + 5};
        m.count += n;
        for (auto &v : m.vertices[g]) {
            for (size_t b = 0; b < sizeof(v); ++b) reinterpret_cast<uint8_t *>(&v)[b] = static_cast<uint8_t>(rng());
            for (int k = 0; k < 3; ++k) v.mOrigin[k] = RandomFloat(rng);
            for (int k = 0; k < 2; ++k) {
                const int pick = static_cast<int>(rng() % 4);
                v.mUv[k] = pick == 0 ? 0.0f : pick == 1 ? -static_cast<float>(rng() % 100) / 64.0f : static_cast<float>(rng() % 100) / 64.0f;
            }
        }
    }
    for (size_t b = 0; b < sizeof(m.entry); ++b) reinterpret_cast<uint8_t *>(&m.entry)[b] = static_cast<uint8_t>(rng());
    m.entry.mGroupsFirst = m.groups;
    m.entry.mGroupsLast = m.groups + groups;
}

// A point uv near one of the meshes' vertex uvs (equal, within or past the 1e-4 tolerance, or of the other sign), or
// unrelated.
float NearUv(std::mt19937 &rng, float uv)
{
    switch (rng() % 7) {
    case 0: return uv;
    case 1: return uv + 0.00005f;
    case 2: return uv - 0.00009f;
    case 3: return uv + 0.0002f;
    case 4: return -uv;
    case 5: return 0.0f;
    default: return 7.0f + static_cast<float>(rng() % 8);
    }
}
const PartMeshVertex &AnyVertex(std::mt19937 &rng)
{
    const Mesh &m = gMesh[rng() % 2];
    return m.vertices[rng() % 4][rng() % 5];
}

// Fills the source: ids, their point sets of `sizes[set]` points, their keys.
void BuildSource(std::mt19937 &rng, const int *sizes, bool uvPairs)
{
    const int n = static_cast<int>(rng() % 5);
    for (int k = 0; k < n; ++k) gIdStore[k] = rng() % 8;
    gIds = {gIdStore, gIdStore + n, gIdStore + 6};
    gIdsNull = rng() % 10 == 0;
    for (int s = 0; s < kPointSets; ++s) {
        for (int p = 0; p < 40; ++p) {
            PartPoint &pt = gPointStore[s][p];
            if (uvPairs && p % 2 == 1) {
                const PartMeshVertex &v = AnyVertex(rng);
                pt.mXyz[0] = NearUv(rng, v.mUv[0]);
                pt.mXyz[1] = NearUv(rng, v.mUv[1]);
                pt.mXyz[2] = RandomFloat(rng);
            } else {
                for (int k = 0; k < 3; ++k) pt.mXyz[k] = RandomFloat(rng);
            }
        }
        gPoints[s] = {gPointStore[s], gPointStore[s] + sizes[s], gPointStore[s] + 40};
    }
    for (int &p : gPointsFor) p = rng() % 6 == 0 ? -1 : static_cast<int>(rng() % kPointSets);
    for (auto &k : gKeys) {
        k.w[0] = rng() % 3 == 0 ? 3 + rng() % 4 : static_cast<uint32_t>(rng() % 8);
        k.w[1] = rng() % 2 ? 1 : static_cast<uint32_t>(rng() % 4);
        k.w[2] = rng() % 3 == 0 ? 0xf : static_cast<uint32_t>(rng() % 20);
        k.w[3] = static_cast<uint32_t>(rng() % 4);
    }
}

struct Fixture {
    VTable vt;
    Env env;
    Redirect entry;
    explicit Fixture(difftest::Context &ctx)
        : vt(CopyVTable(ctx)), env(ctx), entry(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0))
    {
        vt.slots[72] = reinterpret_cast<const void *>(&Slot72);
        vt.slots[12] = reinterpret_cast<const void *>(&Slot12);
        vt.slots[13] = reinterpret_cast<const void *>(&Slot13);
        vt.slots[71] = reinterpret_cast<const void *>(&Slot71);
        gSourceVt = {};
        gSourceVt[13] = reinterpret_cast<const void *>(&SourcePoints);
        gSourceVt[14] = reinterpret_cast<const void *>(&SourceIds);
        gSourceVt[16] = reinterpret_cast<const void *>(&SourceKey);
        gSource.vtable = gSourceVt.data();
        gTargetVt = {};
        gTargetVt[18] = reinterpret_cast<const void *>(&TargetClear);
        gTargetVt[12] = reinterpret_cast<const void *>(&TargetSet);
        gTarget.vtable = gTargetVt.data();
        gEvents.reserve(1024);
    }
};

Scenario PlainScenario(std::mt19937 &rng)
{
    Scenario sc;
    for (auto &b : sc.bytes) b = static_cast<uint8_t>(rng());
    return sc;
}

} // namespace bpt_export

// Slot 76: every point set holds at least two points per vertex of the larger mesh (fewer throws, and an odd count
// equal to 2V-1 overruns the vector in the original), from exactly 2V and 2V+1 up.
DIFF_TEST(cTSCASBodyPartSystem_ExportPointDeltas, 0xce9840)
{
    using namespace bpt_export;
    std::mt19937 rng(0xce9840);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xce9840);
    Fixture fx(ctx);
    for (int i = 0; i < 3000; ++i) {
        BuildMesh(rng, gMesh[0], 20);
        BuildMesh(rng, gMesh[1], 20);
        const int v = gMesh[0].count > gMesh[1].count ? gMesh[0].count : gMesh[1].count;
        int sizes[kPointSets];
        for (int &s : sizes) {
            const int extra = rng() % 3 == 0 ? 0 : rng() % 2 == 0 ? 1 : static_cast<int>(rng() % 3);
            s = 2 * v + extra; // 0 for meshes without vertices: an empty set is skipped
        }
        BuildSource(rng, sizes, false);
        gMask = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 8);
        // Only the points written are hashed (0x1953a0 leaves the rest unfilled): what both meshes write.
        gHashLimit = 2 * static_cast<size_t>(gMesh[0].count < gMesh[1].count ? gMesh[0].count : gMesh[1].count);
        Scenario sc = PlainScenario(rng);
        Compare(ctx, "ExportPointDeltas", sc, fx.vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &gSource, &gTarget);
            else w.S()->ExportPointDeltas(&gSource, &gTarget);
            return 0;
        });
    }
}

// Slot 77: point sets of at least V points (fewer throws), V exactly included.
DIFF_TEST(cTSCASBodyPartSystem_ExportPointPositions, 0xce9b30)
{
    using namespace bpt_export;
    std::mt19937 rng(0xce9b30);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xce9b30);
    Fixture fx(ctx);
    for (int i = 0; i < 3000; ++i) {
        BuildMesh(rng, gMesh[0], 20);
        BuildMesh(rng, gMesh[1], 20);
        const int v = gMesh[0].count > gMesh[1].count ? gMesh[0].count : gMesh[1].count;
        int sizes[kPointSets];
        for (int &s : sizes) s = v + (rng() % 3 == 0 ? 0 : static_cast<int>(rng() % 4));
        BuildSource(rng, sizes, false);
        gMask = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 8);
        gHashLimit = 1000;
        Scenario sc = PlainScenario(rng);
        Compare(ctx, "ExportPointPositions", sc, fx.vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &gSource, &gTarget);
            else w.S()->ExportPointPositions(&gSource, &gTarget);
            return 0;
        });
    }
}

// Slot 78: (delta, uv) pairs, an even count (an odd one throws when nothing matches); at least two per vertex while
// matches can happen (the original writes a match through a pointer the growing append frees), fewer only with uvs no
// vertex has, so the out vector grows. Modes 3 and 4 with kinds 4 and 5 (key 1, part 0xf) skip the matching.
DIFF_TEST(cTSCASBodyPartSystem_ExportPositionsByUv, 0xce9e20)
{
    using namespace bpt_export;
    std::mt19937 rng(0xce9e20);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0xce9e20);
    Fixture fx(ctx);
    for (int i = 0; i < 4000; ++i) {
        BuildMesh(rng, gMesh[0], 18);
        BuildMesh(rng, gMesh[1], 18);
        const int v = gMesh[0].count > gMesh[1].count ? gMesh[0].count : gMesh[1].count;
        const bool grow = rng() % 5 == 0;
        int sizes[kPointSets];
        for (int &s : sizes) s = grow ? 2 * static_cast<int>(rng() % (v + 1)) : 2 * v + 2 * static_cast<int>(rng() % 2);
        BuildSource(rng, sizes, true);
        if (grow) {
            for (auto &set : gPointStore) {
                for (int p = 1; p < 40; p += 2) set[p].mXyz[0] = 9.0f + static_cast<float>(p);
            }
        }
        gMask = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 8);
        gHashLimit = 1000;
        const uint32_t mode = rng() % 3 == 0 ? static_cast<uint32_t>(rng() % 6) : 3 + rng() % 2;
        Scenario sc = PlainScenario(rng);
        Compare(ctx, "ExportPositionsByUv", sc, fx.vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), &gSource, &gTarget, mode);
            else w.S()->ExportPositionsByUv(&gSource, &gTarget, mode);
            return 0;
        });
    }
}
