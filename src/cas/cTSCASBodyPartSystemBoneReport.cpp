// nTSCASCore::cTSCASBodyPartSystem (lane KB): slot 88, the bone report. The face's `this` is the subobject at +0x1c; see
// cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cITSSkinCatalog.h"
#include "rz/ServiceGetters.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <map>
#include <vector>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// The report's texts, the image's own.
constexpr uint32_t kRvaModifiersTitle = 0xeba868;  // 0x26 characters
constexpr uint32_t kRvaModifierLine = 0xeba890;    // sign, item, bone, part, delta, base position
constexpr uint32_t kRvaArchetypesTitle = 0xeba8d0; // 0x27 characters
constexpr uint32_t kRvaArchetypeLine = 0xeba8f8;   // item, bone, part, the two positions
constexpr uint32_t kRvaGuidFormat = 0xdce3cc;      // a GUID as text
constexpr uint32_t kRvaUnknown = 0xdc4c48;         // the item name when no item matched

// One 0x3c-byte bone record of a part entry: its name and position (the rest is not read here).
struct BoneRecord {
    cRZString mName;     // +0x00
    float mPosition[3];  // +0x20
    uint8_t mPad2C[0x3c - 0x2c];
};
static_assert(sizeof(BoneRecord) == 0x3c, "BoneRecord layout");
struct BoneRecords {
    BoneRecord *mFirst, *mLast, *mEnd;
};

// The part entries as far as this report reads them (each a vector of BoneRecords or its emptiness).
const BoneRecords &RecordsAt(const uint8_t *entry, uint32_t offset)
{
    return *reinterpret_cast<const BoneRecords *>(entry + offset);
}
bool IsEmptyAt(const uint8_t *entry, uint32_t offset)
{
    const auto *v = reinterpret_cast<void *const *>(entry + offset);
    return v[0] == v[1];
}
constexpr uint32_t kBaseBones = 0x24; // 0xcf04b0's entry: the base bones

// The catalog's items grouped by their GUID (16 bytes in memcmp order), std::allocator nodes (0x2c bytes), vectors on
// the allocator service (growth 0x38c800); erased with 0xcdb350.
struct GuidMemLess {
    bool operator()(const CasGuid &a, const CasGuid &b) const { return std::memcmp(&a, &b, sizeof(CasGuid)) < 0; }
};
using ItemVector = std::vector<void *, CasAlloc<void *>>;
using ItemsByGuid = std::map<CasGuid, ItemVector, GuidMemLess>;

// The catalog's items into `out`: a vector the service fills, freed at the end.
struct CatalogItems : ServiceBlockVector {
    CatalogItems() : ServiceBlockVector{} {}
    CatalogItems(const CatalogItems &) = delete;
    ~CatalogItems() { FreeBlock(); }
    void *const *begin() const { return static_cast<void *const *>(mFirst); }
    void *const *end() const { return static_cast<void *const *>(mLast); }
};

void *Catalog() { return CachedServiceGetter<0x10faf74, 0x4bcf1bb0, 0x0bcf1b58>(); }

void Append(void *out, LocalRZString &line) { VirtualCall<void(__thiscall *)(void *, cRZString *), 19>(out, &line.mText); }

const char *IdName(uint32_t id) { return OriginalFunction<const char *(__cdecl *)(uint32_t), 0xb5fd70>()(id); }
uint32_t IdCategories(uint32_t id) { return OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0xb5fd30>()(id); }

// An item's description (its slot 3): its category mask (slot 22) and name (slot 13).
void *Description(void *item) { return VirtualCall<void *(__thiscall *)(void *), 3>(item); }

// Whether the item is in one of the id's categories that +0x11c allows.
bool InCategories(void *item, uint32_t id, uint32_t allowed)
{
    const uint32_t mask = VirtualCall<uint32_t(__thiscall *)(void *), 22>(Description(item));
    return (allowed & IdCategories(id) & mask) != 0;
}

const char *ItemName(void *item)
{
    if (item == nullptr) return GameAddress<const char>(kRvaUnknown);
    return VirtualCall<const char *(__thiscall *)(void *), 13>(Description(item));
}

const char *Text(const BoneRecord &r) { return VirtualCall<const char *(__thiscall *)(const void *), 5>(&r.mName); }

void GuidText(LocalRZString &text, const CasGuid &g)
{
    const uint32_t *w = g.mWords;
    text.mText.Sprintf(GameAddress<const char>(kRvaGuidFormat), w[0], w[1] >> 16, w[1] & 0xffff, w[2] >> 16, w[2] & 0xffff, w[3]);
}

// For each bone of `bones` whose name the base part entry's bones also have (the first such), `print(bone, base)`.
template <typename Print> void MatchBones(const BoneRecords &bones, const BoneRecords &base, Print print)
{
    for (const BoneRecord *p = bones.mFirst; p != bones.mLast; ++p) {
        for (const BoneRecord *q = base.mFirst; q != base.mLast; ++q) {
            if (p->mName.mString == q->mName.mString) {
                print(*p, *q);
                break;
            }
        }
    }
}

// The skin base filter's +0x54 face, the predicate the catalog's queries take (null without a filter).
void *FilterFace(void *filter) { return filter == nullptr ? nullptr : static_cast<uint8_t *>(filter) + 0x54; }

} // namespace

void BodyPartSystemIface::ReportBones(void *out)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 26>(out, 0, 0xffffffffu);
    if (mAge == 0) return;
    LocalRZString line;
    line.mText.mString.assign(GameAddress<const char>(kRvaModifiersTitle), 0x26);
    Append(out, line);

    VirtualCall<void(__thiscall *)(void *), 7>(mSkinBaseFilter);
    using SetWord = void(__thiscall *)(void *, uint32_t);
    void *const filter = mSkinBaseFilter;
    OriginalFunction<SetWord, 0xb65750>()(filter, OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0x267350>()(mAge));
    OriginalFunction<SetWord, 0xb654f0>()(mSkinBaseFilter, 8);
    OriginalFunction<SetWord, 0xb655f0>()(mSkinBaseFilter, 2);
    void *const catalog = Catalog();
    cTSCASBodyPartSystem *const system = SystemOf(this);

    // Modifiers: per GUID (the item's slot 10) and id, the bones of each of the two groups the entry has.
    CatalogItems modifiers;
    As<ifaces::cITSSkinCatalog>(catalog)->FindType9Matching(&modifiers, FilterFace(mSkinBaseFilter), false, -1);
    ItemsByGuid modifiersByGuid;
    for (void *item : modifiers) {
        modifiersByGuid[*VirtualCall<const CasGuid *(__thiscall *)(void *), 10>(item)].push_back(item);
    }
    for (const auto &[guid, items] : modifiersByGuid) {
        LocalRZString guidText;
        GuidText(guidText, guid);
        for (const uint32_t *p = mIdsFirst; p != mIdsLast; ++p) {
            const uint32_t id = *p;
            const uint8_t *const entry = OriginalFunction<const uint8_t *(__thiscall *)(void *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf07a0>()(
                system, guidText.mText.ToChar(), 3, 0, id, 0, 1);
            if (IsEmptyAt(entry, 0x60) && IsEmptyAt(entry, 0x6c)) continue;
            const uint8_t *const base = OriginalFunction<const uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
                system, 3, 0, id, 0);
            for (uint32_t group = 0; group < 2; ++group) {
                if (IsEmptyAt(entry, 0x60 + group * 0xc)) continue;
                const uint32_t sign = entry[0x38] == 0 ? ' ' : (group != 0 ? '-' : '+');
                // The first item of the GUID in the id's categories whose flags (slot 9) have the group's bit.
                void *chosen = nullptr;
                for (void *item : items) {
                    if (!InCategories(item, id, mCategoryMask)) continue;
                    const uint32_t bit = group == 0 ? 1 : 2;
                    if ((VirtualCall<uint8_t(__thiscall *)(void *), 9>(item) & bit) != 0) {
                        chosen = item;
                        break;
                    }
                }
                MatchBones(RecordsAt(entry, 0x78 + group * 0xc), RecordsAt(base, kBaseBones), [&](const BoneRecord &bone, const BoneRecord &b) {
                    const char *const name = ItemName(chosen);
                    const char *const part = IdName(id);
                    const char *const boneName = Text(bone);
                    line.mText.Sprintf(GameAddress<const char>(kRvaModifierLine), sign, name, boneName, part,
                                       static_cast<double>(b.mPosition[0] - bone.mPosition[0]),
                                       static_cast<double>(b.mPosition[1] - bone.mPosition[1]),
                                       static_cast<double>(b.mPosition[2] - bone.mPosition[2]),
                                       static_cast<double>(b.mPosition[0]), static_cast<double>(b.mPosition[1]),
                                       static_cast<double>(b.mPosition[2]));
                    Append(out, line);
                });
            }
        }
    }

    // Archetypes: per GUID (the item's slot 6) and id, the bones of the archetype entry.
    line.mText.mString.assign(GameAddress<const char>(kRvaArchetypesTitle), 0x27);
    Append(out, line);
    CatalogItems archetypes;
    As<ifaces::cITSSkinCatalog>(catalog)->FindType8Matching(&archetypes, FilterFace(mSkinBaseFilter), false, -1);
    ItemsByGuid archetypesByGuid;
    for (void *item : archetypes) {
        archetypesByGuid[*VirtualCall<const CasGuid *(__thiscall *)(void *), 6>(item)].push_back(item);
    }
    for (const auto &[guid, items] : archetypesByGuid) {
        LocalRZString guidText;
        GuidText(guidText, guid);
        for (const uint32_t *p = mIdsFirst; p != mIdsLast; ++p) {
            const uint32_t id = *p;
            const uint8_t *const entry = OriginalFunction<const uint8_t *(__thiscall *)(void *, const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 0xcec500>()(
                system, guidText.mText.ToChar(), 3, 0, id, 0, 1, 0);
            if (IsEmptyAt(entry, 0x40)) continue;
            const uint8_t *const base = OriginalFunction<const uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
                system, 3, 0, id, 0);
            void *chosen = nullptr;
            for (void *item : items) {
                if (InCategories(item, id, mCategoryMask)) {
                    chosen = item;
                    break;
                }
            }
            MatchBones(RecordsAt(entry, 0x4c), RecordsAt(base, kBaseBones), [&](const BoneRecord &bone, const BoneRecord &b) {
                const char *const name = ItemName(chosen);
                const char *const part = IdName(id);
                const char *const boneName = Text(bone);
                line.mText.Sprintf(GameAddress<const char>(kRvaArchetypeLine), name, boneName, part,
                                   static_cast<double>(bone.mPosition[0]), static_cast<double>(bone.mPosition[1]),
                                   static_cast<double>(bone.mPosition[2]), static_cast<double>(b.mPosition[0]),
                                   static_cast<double>(b.mPosition[1]), static_cast<double>(b.mPosition[2]));
                Append(out, line);
            });
        }
    }
}

} // namespace nTSCASCore
