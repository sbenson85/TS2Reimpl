// nTSCASCore::cTSCASBodyPartSystem (lane KB): slot 63, the region pair check. The face's `this` is the subobject at +0x1c;
// see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

constexpr uint32_t kRvaListDelimiter = 0xdaa6c0; // ","
constexpr uint32_t kRvaLineFormat = 0xeba5ac;    // kind, key, the two regions, the verdict, the offset
constexpr float kNoLimitLow = -9999.0f;          // 0xeba940
constexpr float kNoLimitHigh = 9999.0f;          // 0xeba93c
constexpr float kCentreWeight = 0.9f;            // 0x3f666666, handed to 0xce9710

// The game's case-insensitive order of names (_stricmp).
struct NoCaseLess {
    bool operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
};
// Names to a flag, on the allocator service (0x2c-byte nodes from 0x7f0da0, erase 0xcdc120, the head freed with
// 0x197100); looked up with 0x2eb8c0.
using NameFlags = std::map<std::string, bool, NoCaseLess, CasAlloc<std::pair<const std::string, bool>>>;

// SplitOff's out: raw memory until the callee constructs it.
struct SplitText {
    union {
        cRZString mText;
    };
    SplitText() {}
    SplitText(const SplitText &) = delete;
    ~SplitText() { mText.Destruct(); }
};

// The limits of a region pair's offset the system keeps at iface +4 (system +0x20): region name -> region name -> part
// table key -> {low, high}, the names without case. Only read here.
struct OffsetLimits {
    float mLow, mHigh;
};
struct LimitsByKey {
    int32_t mKey;
    const OffsetLimits *mLimits;
};
struct LimitsByRegion {
    std::string mName;
    GameTree<LimitsByKey> mByKey;
};
struct LimitsByFirstRegion {
    std::string mName;
    GameTree<LimitsByRegion> mBySecond;
};

// The node of `tree` whose key equals `key` under `less` (std::map::find over a game-built tree), or null.
template <typename T, typename Key, typename Less> const GameTreeNode<T> *Find(const GameTree<T> &tree, const Key &key, Less less)
{
    const GameTreeNode<T> *result = tree.mHead;
    for (const GameTreeNode<T> *node = tree.mHead->mParent; !node->mIsNil;) {
        if (less(node->mValue, key)) {
            node = node->mRight;
        } else {
            result = node;
            node = node->mLeft;
        }
    }
    if (result == tree.mHead || less(key, result->mValue)) return nullptr;
    return result;
}
struct NameLess {
    template <typename T> bool operator()(const T &node, const std::string &key) const { return _stricmp(node.mName.c_str(), key.c_str()) < 0; }
    template <typename T> bool operator()(const std::string &key, const T &node) const { return _stricmp(key.c_str(), node.mName.c_str()) < 0; }
};
struct KeyLess {
    bool operator()(const LimitsByKey &node, int32_t key) const { return node.mKey < key; }
    bool operator()(int32_t key, const LimitsByKey &node) const { return key < node.mKey; }
};

using WeightsFn = const uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, const char *);
const uint8_t *RegionWeights(cTSCASBodyPartSystem *system, uint32_t kind, uint32_t key, uint32_t id, uint32_t partId, const std::string &region)
{
    return OriginalFunction<WeightsFn, 0xcf29b0>()(system, kind, key, id, partId, region.c_str());
}
bool HasWeights(const uint8_t *weights)
{
    const auto *v = reinterpret_cast<void *const *>(weights + 0x44);
    return v[0] != v[1];
}

// The part a pair is checked on: its entry (0xcf04b0) and the keys the region lookups take.
struct PartKey {
    cTSCASBodyPartSystem *system;
    uint8_t *entry;
    uint32_t kind, key, id, partId;
    const uint8_t *Weights(const std::string &region) const { return RegionWeights(system, kind, key, id, partId, region); }
};

// Fits a region to the outfit its flag picks (A when the name has no flag) once per part (0xce3010); false when that
// fails.
bool Fit(const PartKey &at, const uint8_t *weights, const std::string &region, void *outfitA, void *outfitB,
         const NameFlags &useA, NameFlags &fitted)
{
    if (fitted.find(region) != fitted.end()) return true;
    bool a = true;
    const auto flag = useA.find(region);
    if (flag != useA.end()) a = flag->second;
    using FitFn = uint8_t(__thiscall *)(void *, const uint8_t *, uint8_t *, void *);
    if (OriginalFunction<FitFn, 0xce3010>()(at.system, weights, at.entry, a ? outfitA : outfitB) == 0) return false;
    fitted[region] = true;
    return true;
}

// The height of a region's weighted centre on the part (0xce9710 at weight 0.9 fills a point; 0 when it fails).
float CentreHeight(const PartKey &at, const uint8_t *weights)
{
    float centre[3]; // all three written on success
    centre[2] = 0.0f;
    using CentreFn = uint8_t(__thiscall *)(void *, const uint8_t *, float, uint8_t *, float *);
    if (OriginalFunction<CentreFn, 0xce9710>()(at.system, weights, kCentreWeight, at.entry, centre) == 0) return 0.0f;
    return centre[2];
}

// The limits the system's table holds for the record's two regions and the part, or none.
OffsetLimits Limits(const BodyPartSystemIface *self, const PartKey &at, const SurfaceRecord &record)
{
    const OffsetLimits none{kNoLimitLow, kNoLimitHigh};
    if (self->mByte129 == 0) return none;
    const auto *table = static_cast<const GameTree<LimitsByFirstRegion> *>(self->mOffsetLimits);
    const auto *first = Find(*table, record.mText[0], NameLess{});
    if (first == nullptr) return none;
    const auto *second = Find(first->mValue.mBySecond, record.mText[1], NameLess{});
    if (second == nullptr) return none;
    const int32_t key = static_cast<int32_t>(((at.id & 0xff) << 7 | (at.kind & 0x7f)) << 1 | (at.key & 1));
    const auto *limits = Find(second->mValue.mByKey, key, KeyLess{});
    if (limits == nullptr) return none;
    return *limits->mValue.mLimits;
}

// Moves the region by `amount` (0xce1600 on its weights); false when that fails.
bool Correct(const PartKey &at, const std::string &region, float amount)
{
    using MoveFn = uint8_t(__thiscall *)(void *, const uint8_t *, uint8_t *, float);
    return OriginalFunction<MoveFn, 0xce1600>()(at.system, at.Weights(region), at.entry, amount) != 0;
}

// One record's pair (regions 0 and 1; 2 and 3 the regions moved when the offset is above / below the limits).
void CheckPair(const BodyPartSystemIface *self, const PartKey &at, const SurfaceRecord &record, void *outfitA, void *outfitB,
               const NameFlags &useA, NameFlags &fitted, ServiceStringVector *out)
{
    const std::string &first = record.mText[0], &second = record.mText[1];
    const uint8_t *const weightsA = at.Weights(first);
    if (!HasWeights(weightsA)) return;
    const uint8_t *const weightsB = at.Weights(second);
    if (!HasWeights(weightsB)) return;
    if (!Fit(at, weightsA, first, outfitA, outfitB, useA, fitted)) return;
    if (!Fit(at, weightsB, second, outfitA, outfitB, useA, fitted)) return;
    const float heightA = CentreHeight(at, weightsA);
    const float heightB = CentreHeight(at, weightsB);
    const float offset = heightA - heightB;
    const OffsetLimits limits = Limits(self, at, record);
    uint32_t verdict;
    if (limits.mLow > offset) {
        if (!Correct(at, record.mText[3], offset - limits.mLow)) return;
        verdict = 0;
    } else if (offset > limits.mHigh) {
        if (!Correct(at, record.mText[2], offset - limits.mHigh)) return;
        verdict = 1;
    } else {
        verdict = 2;
    }
    LocalRZString line;
    line.mText.Sprintf(GameAddress<const char>(kRvaLineFormat), at.kind, at.key, first.c_str(), second.c_str(), verdict,
                       static_cast<double>(offset));
    out->push_back(ServiceString(line.mText.ToChar()));
}

} // namespace

void BodyPartSystemIface::CheckRegionPairs(void *outfitA, void *outfitB, const ServiceStringVector *flags, ServiceStringVector *out)
{
    out->clear();
    CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();

    // "name,flag" texts: a flag of 1 picks outfit A for the name's region, anything else outfit B.
    NameFlags useA;
    for (const ServiceString &text : *flags) {
        LocalRZString rest;
        rest.mText.mString.assign(text.c_str(), text.size());
        SplitText name;
        reinterpret_cast<cRZIniStringHelpers *>(&rest.mText)->SplitOff(&name.mText, GameAddress<const char>(kRvaListDelimiter));
        const std::string key = std::move(name.mText.mString);
        useA[key] = std::atoi(rest.mText.mString.c_str()) == 1;
    }

    cTSCASBodyPartSystem *const system = SystemOf(this);
    for (const uint32_t *k = mListAFirst; k != mListALast; ++k) {
        for (const uint32_t *y = mListBFirst; y != mListBLast; ++y) {
            for (const uint32_t *i = mIdsFirst; i != mIdsLast; ++i) {
                const uint32_t kind = *k, key = *y, id = *i;
                void *const part = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 52>(this, kind, key, id);
                if (part == nullptr) continue;
                IdVector partIds;
                VirtualCall<void(__thiscall *)(void *, IdVector *), 56>(part, &partIds);
                for (const uint32_t partId : partIds) {
                    uint8_t *const entry = OriginalFunction<uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
                        system, kind, key, id, partId);
                    OriginalFunction<void(__thiscall *)(void *, void *), 0xce4590>()(system, entry);
                    PartMeshEntry *const mesh = reinterpret_cast<PartMeshEntry *>(entry);
                    mesh->mStackIndex = 0;
                    if (mesh->mGroupsFirst == mesh->mGroupsLast) continue;
                    // Both outfits must have the part (their slot 15, then slot 13 on what it gives).
                    const auto hasPart = [&](void *outfit) {
                        const uint32_t ref = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 15>(
                            outfit, kind, key, partId, id, 1);
                        return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 13>(outfit, ref) != 0;
                    };
                    if (!hasPart(outfitA) || !hasPart(outfitB)) continue;
                    NameFlags fitted; // the regions fitted to an outfit (0xce3010) for this part
                    const PartKey at{system, entry, kind, key, id, partId};
                    for (const SurfaceRecord *record = mRecords->data(); record != mRecords->data() + mRecords->size(); ++record) {
                        CheckPair(this, at, *record, outfitA, outfitB, useA, fitted, out);
                    }
                    OriginalFunction<void(__thiscall *)(void *, void *), 0xce7430>()(system, entry);
                    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 31>(this, kind, key, id, partId);
                }
            }
        }
    }
}

} // namespace nTSCASCore
