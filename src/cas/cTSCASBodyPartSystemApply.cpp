// nTSCASCore::cTSCASBodyPartSystem: slot 24 of the cITSCASBodyPartSystem face (lane JA), which pushes a part's entries
// through the system's entry helpers. The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

namespace nTSCASCore {

namespace {

void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

using EntryOf = uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
using EntryPass = void(__thiscall *)(void *, uint8_t *, uint32_t, uint32_t);

} // namespace

void BodyPartSystemIface::ApplyPart(uint32_t kind, uint32_t key, uint32_t id, uint8_t allKinds, uint8_t allKeys, uint8_t allIds,
                                    uint8_t rebuild, uint8_t refresh, uint8_t firstPass, uint8_t resetIndex)
{
    void *const part = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 52>(this, kind, key, id);
    if (part == nullptr) return;
    const uint32_t partKind = kind == 7 ? 3 : kind; // kind 7 shares kind 3's entries
    const uint32_t partKey = id == 0 ? key : 0;
    IdVector partIds;
    IdVector kinds;
    if (allKinds) {
        kinds.assign(mListAFirst, mListALast);
    } else {
        kinds.push_back(partKind);
    }
    IdVector keys;
    if (allKeys) {
        keys.assign(mListBFirst, mListBLast);
    } else {
        keys.push_back(partKey);
    }
    if (allIds) {
        VirtualCall<void(__thiscall *)(void *, IdVector *), 56>(part, &partIds);
    } else {
        partIds.push_back(VirtualCall<uint32_t(__thiscall *)(void *), 57>(part));
    }
    for (const uint32_t partId : partIds) {
        if (VirtualCall<void *(__thiscall *)(void *, uint32_t), 48>(part, partId) == nullptr) continue;
        for (const uint32_t k : kinds) {
            for (const uint32_t b : keys) {
                uint8_t *const entry = OriginalFunction<EntryOf, 0xcf04b0>()(SystemOf(this), k, b, id, partId);
                if (rebuild) {
                    if (resetIndex) *reinterpret_cast<int32_t *>(entry + 0x50) = -1;
                    OriginalFunction<void(__thiscall *)(void *, uint8_t *), 0xcfbbb0>()(SystemOf(this), entry);
                    OriginalFunction<void(__thiscall *)(void *, uint8_t *), 0xce7430>()(SystemOf(this), entry);
                }
                if (firstPass) OriginalFunction<EntryPass, 0xcfb180>()(SystemOf(this), entry, 1, 0);
                if (refresh) {
                    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 31>(this, k, b, id, partId);
                    OriginalFunction<EntryPass, 0xcfb180>()(SystemOf(this), entry, 0, 1);
                }
            }
        }
        mLastKind = partKind;
        mLastKey = partKey;
        mLastPartId = partId;
    }
}

namespace {

// A mesh vertex of an entry's groups: 0x68 bytes, the morphed position first and the base position at +0x10.
struct MeshVertex {
    float mPosition[3];     // +0x00
    float mPad0C;
    float mBasePosition[3]; // +0x10
    uint8_t mRest[0x68 - 0x1c];
};
static_assert(sizeof(MeshVertex) == 0x68, "MeshVertex layout");

struct MeshGroup {
    MeshVertex *mFirst;
    MeshVertex *mLast;
    MeshVertex *mEnd;
};

struct MeshEntry {
    MeshGroup *mGroupsFirst; // +0x00
    MeshGroup *mGroupsLast;  // +0x04
    uint8_t mPad08[0x58 - 0x08];
    uint32_t mMeshA;         // +0x58  what 0xcf29b0 is asked with
    uint32_t mMeshB;         // +0x5c
};

// What 0xcf29b0 returns: per-vertex weights at +0x44.
struct VertexWeights {
    uint8_t mPad[0x44];
    float *mFirst; // +0x44
    float *mLast;  // +0x48
};

} // namespace

bool BodyPartSystemIface::AverageWeightedOffset(uint32_t kind, uint32_t key, uint32_t id, uint32_t morph, float *out)
{
    if (morph == 0) return false;
    const auto *const entry = reinterpret_cast<const MeshEntry *>(OriginalFunction<EntryOf, 0xcf04b0>()(SystemOf(this), kind, key, id, 0));
    const auto *const weights = OriginalFunction<const VertexWeights *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf29b0>()(
        SystemOf(this), entry->mMeshA, entry->mMeshB, id, 0, morph);
    if (weights->mFirst == weights->mLast) return false;
    float sum[3] = {0.0f, 0.0f, 0.0f};
    uint32_t count = 0;
    uint32_t index = 0;
    for (const MeshGroup *group = entry->mGroupsFirst; group != entry->mGroupsLast; ++group) {
        for (const MeshVertex *v = group->mFirst; v != group->mLast; ++v, ++index) {
            if (index >= static_cast<uint32_t>(weights->mLast - weights->mFirst)) {
                OriginalFunction<void(__cdecl *)(), 0x1ef450>()(); // std::vector subscript out of range: throws
            }
            if (weights->mFirst[index] == 1.0f) {
                ++count;
                for (int k = 0; k < 3; ++k) sum[k] = (v->mPosition[k] - v->mBasePosition[k]) + sum[k];
            }
        }
    }
    if (count == 0) return false;
    const float n = static_cast<float>(static_cast<double>(count));
    for (int k = 0; k < 3; ++k) out[k] = sum[k] / n;
    return true;
}

} // namespace nTSCASCore
