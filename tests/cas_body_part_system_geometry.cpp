// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane KA: slot 31 (RefreshGeometry, 0xce7ee0), a part entry's
// vertices written back into the part's geometry mesh. Every object it talks to is a recording fake: the part (iface
// slot 52), its geometry, the mesh, the iterators and index maps the mesh hands out (counted references whose AddRef /
// Release / slot 3 copies are recorded by object number), the entry (0xcf04b0) and the element lookup (0xaeda40).
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "sg/SGUtilities.h"

#include <array>
#include <cstring>
#include <deque>

namespace bpt_geometry {

using namespace bpt;
using Vt = std::array<const void *, 64>;

// One fake object: what it is, its number (events name objects by it), and for iterators / maps their data.
struct FObj {
    const void *const *vt;
    uint32_t id;
    std::vector<uint16_t> indices;              // index / vertex iterators
    std::vector<std::array<uint32_t, 3>> points; // point iterators
    size_t pos = 0;
    uint32_t mapMul = 1, mapAdd = 0, mapSize = 1; // index maps
};
std::deque<FObj> gPool;
Vt gPartVt, gGeoVt, gMeshVt, gIndexVt, gPointVt, gMapVt, gAuxVt;

FObj *NewObj(const Vt &vt)
{
    gPool.push_back(FObj{vt.data(), static_cast<uint32_t>(gPool.size() + 1)});
    return &gPool.back();
}

// One group of the case: whether the mesh says it has vertices, its elements (-1 = missing), their index and point
// lists, the vertex order of the group and the index maps' arithmetic; the entry's vertex count for it.
struct GroupCase {
    uint32_t hasVertices = 1;
    int32_t posElement = 0, normElement = 0;
    std::vector<uint16_t> posIndices, normIndices, vertices;
    std::vector<std::array<uint32_t, 3>> posPoints, normPoints;
    uint32_t posMul = 1, posAdd = 0, normMul = 1, normAdd = 0;
    std::vector<uint8_t> entryVertices; // 0x68 bytes each
};
struct Case {
    bool partNull = false, geometryNull = false, meshNull = false;
    uint32_t countDelta = 0; // the mesh's group count minus the entry's
    std::vector<GroupCase> groups;
} gCase;

// The entry 0xcf04b0 hands out: its group vectors.
struct EntryBlock {
    nTSCASCore::PartMeshGroup *first, *last;
    uint8_t rest[0x60];
} gEntry;
std::vector<nTSCASCore::PartMeshGroup> gEntryGroups;

FObj *gPart, *gGeometry;

const GroupCase *GroupOfElement(int32_t element)
{
    for (const GroupCase &g : gCase.groups)
        if (g.posElement == element || g.normElement == element) return &g;
    return nullptr;
}

uint32_t __fastcall AddRef(FObj *self, void *) { gEvents.push_back({0x7001, self->id}); return 2; }
uint32_t __fastcall Release(FObj *self, void *) { gEvents.push_back({0x7002, self->id}); return 1; }

void *__fastcall Slot52(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    gEvents.push_back({52, Rel(self), a, b, c});
    return gCase.partNull ? nullptr : gPart;
}
EntryBlock *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, c, d});
    return &gEntry;
}
void *__fastcall PartGeometry(FObj *self, void *, uint32_t partId)
{
    gEvents.push_back({0x8048, self->id, partId});
    return gCase.geometryNull ? nullptr : gGeometry;
}
void __fastcall GeoMesh(FObj *self, void *, void **out)
{
    gEvents.push_back({0x8139, self->id});
    *out = gCase.meshNull ? nullptr : NewObj(gMeshVt);
}
void __fastcall GeoSetMesh(FObj *self, void *, FObj *mesh) { gEvents.push_back({0x8138, self->id, mesh ? mesh->id : 0}); }

uint32_t __fastcall MeshCount(FObj *self, void *)
{
    gEvents.push_back({0x9044, self->id});
    return static_cast<uint32_t>(gCase.groups.size()) + gCase.countDelta;
}
void __fastcall MeshInfo(FObj *self, void *, uint32_t *out, uint32_t i)
{
    gEvents.push_back({0x9045, self->id, i});
    for (int k = 0; k < 7; ++k) out[k] = 0xabc00000u + k;
    out[2] = i < gCase.groups.size() ? gCase.groups[i].hasVertices : 0;
    out[3] = 100 + i;
}
// 0xaeda40 (out, mesh, block, type, 0, 2): the group's element of that type.
uint32_t *__cdecl F_aeda40(uint32_t *out, FObj *mesh, uint32_t block, uint32_t type, uint32_t a, uint32_t b)
{
    gEvents.push_back({0xaeda40, mesh->id, block, type, a, b});
    const GroupCase &g = gCase.groups[block - 100];
    const int32_t element = type == 0x5b830781u ? g.posElement : type == 0x3b83078bu ? g.normElement : -1;
    out[0] = static_cast<uint32_t>(element);
    out[1] = 0x11;
    out[2] = type;
    out[3] = a;
    out[4] = b;
    out[5] = static_cast<uint32_t>(element) * 7u + 3u; // the stream
    return out;
}

void FillTriple(FObj **out, FObj *it)
{
    out[0] = it;
    out[1] = NewObj(gAuxVt);
    out[2] = NewObj(gAuxVt);
}
FObj **__fastcall MeshIndices(FObj *self, void *, FObj **out, int32_t element)
{
    gEvents.push_back({0x9038, self->id, static_cast<uint32_t>(element)});
    FObj *it = NewObj(gIndexVt);
    if (const GroupCase *g = GroupOfElement(element)) it->indices = element == g->posElement ? g->posIndices : g->normIndices;
    FillTriple(out, it);
    return out;
}
FObj **__fastcall MeshPoints(FObj *self, void *, FObj **out, int32_t element)
{
    gEvents.push_back({0x9035, self->id, static_cast<uint32_t>(element)});
    FObj *it = NewObj(gPointVt);
    if (const GroupCase *g = GroupOfElement(element)) it->points = element == g->posElement ? g->posPoints : g->normPoints;
    FillTriple(out, it);
    return out;
}
FObj **__fastcall MeshVertices(FObj *self, void *, FObj **out, uint32_t i)
{
    gEvents.push_back({0x9046, self->id, i});
    FObj *it = NewObj(gIndexVt);
    it->indices = gCase.groups[i].vertices;
    FillTriple(out, it);
    return out;
}
FObj **__fastcall MeshMap(FObj *self, void *, FObj **out, uint32_t block, uint32_t stream)
{
    gEvents.push_back({0x9042, self->id, block, stream});
    FObj *map = NewObj(gMapVt);
    const GroupCase &g = gCase.groups[block - 100];
    if (stream == static_cast<uint32_t>(g.posElement) * 7u + 3u) {
        map->mapMul = g.posMul, map->mapAdd = g.posAdd, map->mapSize = static_cast<uint32_t>(g.posPoints.size());
    } else {
        map->mapMul = g.normMul, map->mapAdd = g.normAdd, map->mapSize = static_cast<uint32_t>(g.normPoints.size());
    }
    FillTriple(out, map);
    return out;
}
uint32_t Fnv(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < n; ++k) h = (h ^ static_cast<const uint8_t *>(p)[k]) * 16777619u;
    return h;
}
void __fastcall MeshUpload(FObj *self, void *, int32_t element, uint32_t count, const void *data, uint32_t bytes, const uint16_t *indices,
                           uint32_t indexCount)
{
    gEvents.push_back({0x900c, self->id, static_cast<uint32_t>(element), count, bytes, data ? Fnv(data, count * 12u) : 0u,
                       indices ? 1u : 0u, indexCount, indices ? Fnv(indices, indexCount * 2u) : 0u});
}

FObj *__fastcall IterClone(FObj *self, void *)
{
    FObj *copy = NewObj(gIndexVt);
    copy->vt = self->vt; // the same kind of iterator
    copy->indices = self->indices;
    copy->points = self->points;
    copy->pos = self->pos;
    gEvents.push_back({0x7003, self->id, copy->id});
    return copy;
}
uint8_t __fastcall IterAtEnd(FObj *self, void *, FObj *end)
{
    gEvents.push_back({0x7004, self->id, end ? end->id : 0});
    return self->pos >= (self->points.empty() ? self->indices.size() : self->points.size()) ? 1 : 0;
}
void __fastcall IterNext(FObj *self, void *)
{
    gEvents.push_back({0x7006, self->id});
    ++self->pos;
}
uint16_t __fastcall IterIndex(FObj *self, void *)
{
    gEvents.push_back({0x7005, self->id});
    return self->pos < self->indices.size() ? self->indices[self->pos] : 0xffff;
}
void __fastcall IterPoint(FObj *self, void *, uint32_t *out)
{
    gEvents.push_back({0x7015, self->id});
    const std::array<uint32_t, 3> p = self->pos < self->points.size() ? self->points[self->pos] : std::array<uint32_t, 3>{};
    std::memcpy(out, p.data(), 12);
}
uint16_t __fastcall MapIndex(FObj *self, void *, uint32_t index)
{
    gEvents.push_back({0x7009, self->id, index});
    return static_cast<uint16_t>((index * self->mapMul + self->mapAdd) % self->mapSize);
}

uint32_t RandomBits(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0x7fc00000u | (rng() & 0x3fffff); // NaNs with payloads (quiet)
    case 1: return 0x7f800001u + (rng() & 0x3fff);   // signalling NaNs
    default: {
        const float f = static_cast<float>(static_cast<int>(rng() % 2000) - 1000) / 37.0f;
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    }
    }
}

void BuildCase(std::mt19937 &rng)
{
    gCase = Case{};
    gCase.partNull = rng() % 12 == 0;
    gCase.geometryNull = rng() % 12 == 0;
    gCase.meshNull = rng() % 12 == 0;
    gCase.countDelta = rng() % 10 == 0 ? (rng() % 2 ? 1u : 0xffffffffu) : 0u;
    const int n = static_cast<int>(rng() % 4);
    for (int g = 0; g < n; ++g) {
        GroupCase gc;
        gc.hasVertices = rng() % 6 == 0 ? 0u : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) | 1u : 1u);
        gc.posElement = rng() % 8 == 0 ? -1 : 10 * g + 1;
        gc.normElement = rng() % 8 == 0 ? -1 : 10 * g + 2;
        for (auto *list : {&gc.posIndices, &gc.normIndices}) {
            list->resize(rng() % 5);
            for (auto &x : *list) x = static_cast<uint16_t>(rng());
        }
        for (auto *pts : {&gc.posPoints, &gc.normPoints}) {
            pts->resize(1 + rng() % 5);
            for (auto &p : *pts)
                for (auto &w : p) w = RandomBits(rng);
        }
        gc.vertices.resize(rng() % 7);
        for (auto &v : gc.vertices) v = static_cast<uint16_t>(rng() % 50);
        gc.posMul = 1 + rng() % 7, gc.posAdd = rng() % 5, gc.normMul = 1 + rng() % 7, gc.normAdd = rng() % 5;
        // The entry's vertex count around the iterator's: fewer, as many, more, none.
        const size_t v = gc.vertices.size();
        size_t count = v;
        switch (rng() % 4) {
        case 0: count = v > 0 ? v - 1 : 0; break;
        case 1: count = v + 1; break;
        case 2: count = rng() % 5 == 0 ? 0 : v; break;
        default: break;
        }
        gc.entryVertices.resize(count * 0x68);
        for (size_t k = 0; k < gc.entryVertices.size(); k += 4) {
            const uint32_t w = RandomBits(rng);
            std::memcpy(&gc.entryVertices[k], &w, 4);
        }
        gCase.groups.push_back(std::move(gc));
    }
    gEntryGroups.clear();
    for (GroupCase &gc : gCase.groups) {
        auto *first = reinterpret_cast<nTSCASCore::PartMeshVertex *>(gc.entryVertices.data());
        const size_t count = gc.entryVertices.size() / 0x68;
        gEntryGroups.push_back({first, first + count, first + count});
    }
    gEntry.first = gEntryGroups.data();
    gEntry.last = gEntryGroups.data() + gEntryGroups.size();
}

} // namespace bpt_geometry

// 0-3 groups: vertex blocks missing, either element missing (-1), 0-4 indices and 1-5 points per element (NaN payloads,
// signalling NaNs), 0-6 vertices against an entry with one fewer / as many / one more / none; a null part, geometry or
// mesh, and a mesh whose group count differs by one either way.
DIFF_TEST(cTSCASBodyPartSystem_RefreshGeometry, 0xce7ee0)
{
    using namespace bpt_geometry;
    std::mt19937 rng(0xce7ee0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0xce7ee0);
    VTable vt = CopyVTable(ctx);
    vt.slots[52] = reinterpret_cast<const void *>(&Slot52);
    Env env(ctx);
    Redirect r1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    shape::StandIn r2(0xaeda40, &nGZSceneGraph::FindLinkedElement, &F_aeda40); // the image's and ours
    for (Vt *v : {&gPartVt, &gGeoVt, &gMeshVt, &gIndexVt, &gPointVt, &gMapVt, &gAuxVt}) {
        *v = {};
        (*v)[1] = reinterpret_cast<const void *>(&AddRef);
        (*v)[2] = reinterpret_cast<const void *>(&Release);
    }
    gPartVt[48] = reinterpret_cast<const void *>(&PartGeometry);
    gGeoVt[39] = reinterpret_cast<const void *>(&GeoMesh);
    gGeoVt[38] = reinterpret_cast<const void *>(&GeoSetMesh);
    gMeshVt[44] = reinterpret_cast<const void *>(&MeshCount);
    gMeshVt[45] = reinterpret_cast<const void *>(&MeshInfo);
    gMeshVt[38] = reinterpret_cast<const void *>(&MeshIndices);
    gMeshVt[35] = reinterpret_cast<const void *>(&MeshPoints);
    gMeshVt[46] = reinterpret_cast<const void *>(&MeshVertices);
    gMeshVt[42] = reinterpret_cast<const void *>(&MeshMap);
    gMeshVt[12] = reinterpret_cast<const void *>(&MeshUpload);
    for (Vt *v : {&gIndexVt, &gPointVt}) {
        (*v)[3] = reinterpret_cast<const void *>(&IterClone);
        (*v)[4] = reinterpret_cast<const void *>(&IterAtEnd);
        (*v)[6] = reinterpret_cast<const void *>(&IterNext);
    }
    gIndexVt[5] = reinterpret_cast<const void *>(&IterIndex);
    gPointVt[5] = reinterpret_cast<const void *>(&IterPoint);
    gMapVt[9] = reinterpret_cast<const void *>(&MapIndex);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        BuildCase(rng);
        const uint32_t kind = rng() % 9, key = rng() % 3, id = rng() % 6, partId = rng() % 4;
        Compare(ctx, "RefreshGeometry", sc, vt, [&](World &w, bool original) -> uint64_t {
            gPool.clear();
            gPart = NewObj(gPartVt);
            gGeometry = NewObj(gGeoVt);
            if (original) orig(w.S(), kind, key, id, partId);
            else w.S()->RefreshGeometry(kind, key, id, partId);
            return gPool.size();
        });
    }
}
