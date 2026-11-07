// nTSCASCore::cTSCASBodyPartSystem (lane KA): slot 25, which rebuilds a part's materials and hands their new names to
// the parts whose subsets use them. The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"

#include <cstring>

namespace nTSCASCore {

namespace {

void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

const char *TextOf(const cRZString &s)
{
    return reinterpret_cast<const cIGZString *>(&s)->ToChar();
}

// One link of a part entry (the vector at +0x68 of what 0xcf04b0 returns): a linked object, when enabled.
struct MaterialLink {
    uint8_t mPad0[8];
    uint8_t mEnabled; // +0x08
    uint8_t mPad9[3];
    void *mObject;    // +0x0c
};
static_assert(offsetof(MaterialLink, mObject) == 0xc, "MaterialLink layout");
struct LinkedPartEntry {
    uint8_t mPad0[0x68];
    MaterialLink **mLinksFirst; // +0x68
    MaterialLink **mLinksLast;  // +0x6c
};

const LinkedPartEntry *PartEntry(BodyPartSystemIface *iface, uint32_t kind, uint32_t key, uint32_t id, uint32_t part)
{
    return OriginalFunction<const LinkedPartEntry *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
        SystemOf(iface), kind, key, id, part);
}

// The part object's slot 34: a material name for one of its subsets.
void SetSubsetMaterial(void *part, const PartMaterial &material, const PartMaterialUser &user)
{
    const char *const subset = TextOf(user.mSubset);
    VirtualCall<void(__thiscall *)(void *, const char *, const char *), 34>(part, TextOf(material.mName), subset);
}

} // namespace

PartMaterial::~PartMaterial()
{
    mText34.mString.~basic_string();
    using Erase = void(__thiscall *)(GameTree<PartMaterialUser> *, GameTree<PartMaterialUser> *, GameTreeNode<PartMaterialUser> *);
    OriginalFunction<Erase, 0xcdc480>()(&mUsers, &mUsers, mUsers.mHead->mParent);
    CasAlloc<GameTreeNode<PartMaterialUser>>().deallocate(mUsers.mHead, 1);
    mName.mString.~basic_string();
}

bool BodyPartSystemIface::RebuildPartMaterials(uint32_t kind, uint32_t key, uint32_t materialArg, uint8_t applyToParts)
{
    bool renamed = false;
    for (const uint32_t *id = mIdsFirst; id != mIdsLast; ++id) PartEntry(this, kind, key, *id, 0);
    PartMaterialMap &materials = (*mMaterials)[PartTableKey(kind, key)];
    for (auto &[unused, material] : materials) {
        if (material.mUsers.mSize == 0 || material.mRebuildable == 0) continue;
        const char *const name = OriginalFunction<const char *(__thiscall *)(void *, PartMaterial *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcfb1d0>()(
            SystemOf(this), &material, kind, key, mAge, materialArg);
        if (name == nullptr) continue;
        material.mName.mString.assign(name, std::strlen(name));
        renamed = true;
        if (!applyToParts) continue;
        for (auto *node = material.mUsers.First(); node != material.mUsers.End(); node = node->Next()) {
            const PartMaterialUser &user = node->mValue;
            void *const part = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 52>(this, user.mKind, user.mKey, user.mId);
            if (part == nullptr) continue;
            const CasGuid *const zero = OriginalFunction<const CasGuid *(__cdecl *)(), 0x4f8e50>()();
            if (std::memcmp(&user.mGuid, zero, sizeof(CasGuid)) == 0) {
                SetSubsetMaterial(part, material, user);
                continue;
            }
            IdVector ids;
            VirtualCall<void(__thiscall *)(void *, IdVector *), 56>(part, &ids);
            for (const uint32_t partId : ids) {
                const LinkedPartEntry *const entry = PartEntry(this, user.mKind, user.mKey, user.mId, partId);
                for (MaterialLink *const *at = entry->mLinksFirst; at != entry->mLinksLast; ++at) {
                    const MaterialLink *const link = *at;
                    if (link == nullptr || !link->mEnabled || link->mObject == nullptr) continue;
                    const CasGuid *const guid = OriginalFunction<const CasGuid *(__thiscall *)(const MaterialLink *), 0xcff0c0>()(link);
                    if (std::memcmp(guid, &user.mGuid, sizeof(CasGuid)) == 0) SetSubsetMaterial(link->mObject, material, user);
                }
            }
        }
    }
    return renamed;
}

} // namespace nTSCASCore
