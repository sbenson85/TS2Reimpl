// nTSCASCore::cTSCASBodyPartSystem (lane JZ): the point exports (slots 76, 77, 78). For each id a source lists, the
// vertices of that id's part entry are combined with the source's points into a new point vector handed to the target.
// The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

#include <cmath>

namespace nTSCASCore {

namespace {

void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

// The snapshot id the exports park the current state under while they run at age 1.
constexpr int32_t kExportSnapshot = static_cast<int32_t>(0x91e37c4f);

// A 12-byte point that default construction leaves uninitialised: slot 76's vector is resized with 0x1953a0, which
// allocates without filling.
struct RawPoint {
    float mXyz[3];
    RawPoint() {}
};
static_assert(sizeof(RawPoint) == 12, "RawPoint");

using PointVector = std::vector<PartPoint, CasAlloc<PartPoint>>;
using RawPointVector = std::vector<RawPoint, CasAlloc<RawPoint>>;

[[noreturn]] void ThrowSubscript()
{
    OriginalFunction<void(__cdecl *)(), 0x1ef450>()(); // std::vector subscript out of range: throws
    __assume(0);
}

// The source's interface: slot 14 its ids, slot 13 an id's points, slot 16 an id's part entry key.
const IdVector *SourceIds(void *source)
{
    return VirtualCall<const IdVector *(__thiscall *)(void *), 14>(source);
}
const PartPoints *SourcePoints(void *source, uint32_t id)
{
    return VirtualCall<const PartPoints *(__thiscall *)(void *, uint32_t), 13>(source, id);
}
struct PartKey {
    uint32_t mKind, mKey, mPartId, mId;
};
PartKey SourceKey(void *source, uint32_t id)
{
    PartKey k;
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t *, uint32_t *, uint32_t *, uint32_t *), 16>(source, id, &k.mKind, &k.mKey,
                                                                                                        &k.mPartId, &k.mId);
    return k;
}

uint32_t IdAt(const IdVector *ids, uint32_t index)
{
    if (index >= ids->size()) ThrowSubscript();
    return (*ids)[index];
}

uint32_t PointCount(const PartPoints *points)
{
    return static_cast<uint32_t>(points->mLast - points->mFirst);
}

const PartMeshEntry *EntryOf(BodyPartSystemIface *self, const PartKey &k)
{
    return reinterpret_cast<const PartMeshEntry *>(
        OriginalFunction<uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(SystemOf(self), k.mKind, k.mKey,
                                                                                                        k.mId, k.mPartId));
}

// Runs at age 1: when slot 72 gives another age mask, the state is saved (slot 12) and the age set (slot 71); the
// caller restores it (slot 13) when this returns true.
bool EnterAgeOne(BodyPartSystemIface *self)
{
    if (VirtualCall<uint32_t(__thiscall *)(void *), 72>(self) == 1) return false;
    VirtualCall<bool(__thiscall *)(void *, int32_t), 12>(self, kExportSnapshot);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 71>(self, 1);
    return true;
}
void LeaveAgeOne(BodyPartSystemIface *self)
{
    VirtualCall<bool(__thiscall *)(void *, int32_t), 13>(self, kExportSnapshot);
}

void TargetClear(void *target)
{
    VirtualCall<void(__thiscall *)(void *), 18>(target);
}
template <typename Vector> void TargetSet(void *target, uint32_t id, Vector *points)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, Vector *), 12>(target, id, points);
}

// Slots 76 and 77: the ids walked with the age switched (only when the source has ids), each id with points built
// into a vector by `build(id, points)` and handed to the target.
template <typename Build> void ExportPerId(BodyPartSystemIface *self, void *source, void *target, Build build)
{
    TargetClear(target);
    const IdVector *const ids = SourceIds(source);
    if (ids == nullptr) return;
    const uint32_t count = static_cast<uint32_t>(ids->size());
    if (count == 0) return;
    const bool restore = EnterAgeOne(self);
    for (uint32_t i = 0; i < count; ++i) {
        const PartPoints *const points = SourcePoints(source, IdAt(ids, i));
        if (points == nullptr || PointCount(points) == 0) continue;
        build(i, ids, points);
    }
    if (restore) LeaveAgeOne(self);
}

} // namespace

void BodyPartSystemIface::ExportPointDeltas(void *source, void *target)
{
    ExportPerId(this, source, target, [&](uint32_t i, const IdVector *ids, const PartPoints *points) {
        RawPointVector out;
        out.resize(PointCount(points));
        const PartMeshEntry *const entry = EntryOf(this, SourceKey(source, IdAt(ids, i)));
        // ORIGINAL-BUG: the index steps by 2 per vertex, both for the point read and the two points written, so only
        // the even points are read, and with an odd point count the last vertex writes one point past the vector's end.
        uint32_t index = 0;
        for (const PartMeshGroup *g = entry->mGroupsFirst; g != entry->mGroupsLast; ++g) {
            for (const PartMeshVertex *v = g->mFirst; v != g->mLast; ++v) {
                if (index >= PointCount(points)) ThrowSubscript();
                const PartPoint &p = points->mFirst[index];
                RawPoint *const o = out.data() + index;
                o[0].mXyz[0] = p.mXyz[0] - v->mOrigin[0];
                o[0].mXyz[1] = p.mXyz[1] - v->mOrigin[1];
                o[0].mXyz[2] = p.mXyz[2] - v->mOrigin[2];
                std::memcpy(&o[1].mXyz[0], &v->mUv[0], 4);
                std::memcpy(&o[1].mXyz[1], &v->mUv[1], 4);
                o[1].mXyz[2] = 0.0f;
                index += 2;
            }
        }
        TargetSet(target, IdAt(ids, i), &out);
    });
}

void BodyPartSystemIface::ExportPointPositions(void *source, void *target)
{
    ExportPerId(this, source, target, [&](uint32_t i, const IdVector *ids, const PartPoints *points) {
        PointVector out;
        const uint32_t size = PointCount(points) * 2;
        if (size != 0) out.resize(size);
        const PartMeshEntry *const entry = EntryOf(this, SourceKey(source, IdAt(ids, i)));
        uint32_t index = 0;
        for (const PartMeshGroup *g = entry->mGroupsFirst; g != entry->mGroupsLast; ++g) {
            for (const PartMeshVertex *v = g->mFirst; v != g->mLast; ++v, ++index) {
                if (index >= PointCount(points)) ThrowSubscript();
                const PartPoint &p = points->mFirst[index];
                PartPoint &o = out.data()[index * 2];
                o.mXyz[0] = v->mOrigin[0] + p.mXyz[0];
                o.mXyz[1] = v->mOrigin[1] + p.mXyz[1];
                o.mXyz[2] = v->mOrigin[2] + p.mXyz[2];
            }
        }
        TargetSet(target, IdAt(ids, i), &out);
    });
}

void BodyPartSystemIface::ExportPositionsByUv(void *source, void *target, uint32_t mode)
{
    TargetClear(target);
    // Unlike slots 76 and 77 the age is switched before the ids are looked at.
    const bool restore = EnterAgeOne(this);
    const IdVector *const ids = SourceIds(source);
    // ORIGINAL-BUG: with no ids the saved state is never restored, so the system stays at age 1.
    if (ids == nullptr) return;
    const uint32_t count = static_cast<uint32_t>(ids->size());
    if (count == 0) return;
    for (uint32_t i = 0; i < count; ++i) {
        const PartPoints *const points = SourcePoints(source, IdAt(ids, i));
        if (points == nullptr) continue;
        const uint32_t pointCount = PointCount(points);
        if (pointCount == 0) continue;
        PointVector out;
        out.reserve(pointCount);
        const PartKey key = SourceKey(source, IdAt(ids, i));
        const PartMeshEntry *const entry = EntryOf(this, key);
        // Kinds left as they are: mode 3 skips kind 5 and kind 4 key 1 part 0xf, mode 4 skips kind 5.
        bool keep = false;
        if (mode == 3) {
            keep = key.mKind == 4 ? key.mKey == 1 && key.mPartId == 0xf : key.mKind == 5;
        } else if (mode == 4) {
            keep = key.mKind == 5;
        }
        for (const PartMeshGroup *g = entry->mGroupsFirst; g != entry->mGroupsLast; ++g) {
            for (const PartMeshVertex *v = g->mFirst; v != g->mLast; ++v) {
                out.push_back(PartPoint{{v->mOrigin[0], v->mOrigin[1], v->mOrigin[2]}});
                // ORIGINAL-BUG: the position is addressed before the uv is appended; if that append grows the
                // vector, the match below writes into the freed buffer.
                PartPoint *const position = out.data() + out.size() - 1;
                PartPoint uv;
                std::memcpy(&uv.mXyz[0], &v->mUv[0], 4);
                std::memcpy(&uv.mXyz[1], &v->mUv[1], 4);
                uv.mXyz[2] = 0.0f;
                out.push_back(uv);
                if (keep) continue;
                const float u = out.back().mXyz[0];
                const float w = out.back().mXyz[1];
                for (uint32_t j = 0; j < pointCount; j += 2) {
                    const uint32_t size = PointCount(points);
                    if (j >= size || j + 1 >= size) ThrowSubscript();
                    const PartPoint &delta = points->mFirst[j];
                    const PartPoint &pointUv = points->mFirst[j + 1];
                    const auto sameSign = [](float a, float b) {
                        return (a < 0.0f && b < 0.0f) || (a > 0.0f && b > 0.0f) || (a == 0.0f && b == 0.0f);
                    };
                    if (!sameSign(u, pointUv.mXyz[0]) || !sameSign(w, pointUv.mXyz[1])) continue;
                    if (!(std::fabs(u - pointUv.mXyz[0]) < 0.0001f) || !(std::fabs(w - pointUv.mXyz[1]) < 0.0001f)) continue;
                    position->mXyz[0] = position->mXyz[0] + delta.mXyz[0];
                    position->mXyz[1] = position->mXyz[1] + delta.mXyz[1];
                    position->mXyz[2] = position->mXyz[2] + delta.mXyz[2];
                    break;
                }
            }
        }
        TargetSet(target, IdAt(ids, i), &out);
    }
    if (restore) LeaveAgeOne(this);
}

} // namespace nTSCASCore
