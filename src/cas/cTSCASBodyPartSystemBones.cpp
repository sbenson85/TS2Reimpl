// nTSCASCore::cTSCASBodyPartSystem (lane JZ): slot 82, which writes a part's bone adjustments into a scene-graph target
// (the bone nodes and one "bone_adjust" extension per bone). The face's `this` is the subobject at +0x1c; see
// cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/SGUtilities.h"   // FindNodeByName, FindDataList

#include <string>

namespace nTSCASCore {

namespace {

void *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<uint8_t *>(iface) - 0x1c;
}

// The image's texts.
constexpr uint32_t kRvaBoneAdjust = 0xe7ced4;            // "bone_adjust": the extension's type name
constexpr uint32_t kRvaBoneTag = 0xe7cee0;               // "bone_adjust_bone_tag"
constexpr uint32_t kRvaBoneTranslation = 0xe7cef8;       // "bone_adjust_translation"
constexpr uint32_t kRvaBoneRotation = 0xe7cf10;          // "bone_adjust_rotation"

// The extension object's class and interface ids (the framework's slot 3 creates one).
constexpr uint32_t kExtensionClass = 0x6a836d56;
constexpr uint32_t kExtensionIid = 0x6a836d51;

// One 0x3c-byte bone adjustment of a part entry (the vector at entry +0x18). Polymorphic: slot 5 gives the bone name.
struct BoneAdjust {
    const void *mVTable;     // +0x00
    std::string mBoneName;   // +0x04
    uint32_t mField1C;       // +0x1c
    float mTranslation[3];   // +0x20
    float mRotation[4];      // +0x2c
};
static_assert(sizeof(BoneAdjust) == 0x3c && offsetof(BoneAdjust, mTranslation) == 0x20, "BoneAdjust layout");

const char *BoneName(const BoneAdjust *b)
{
    return VirtualCall<const char *(__thiscall *)(const void *), 5>(b);
}

nGZSceneGraph::cICompositionTreeNode *TreeNode(void *node)
{
    return static_cast<nGZSceneGraph::cICompositionTreeNode *>(node);
}

// The next extension of the "bone_adjust" type on the target after `after` (null for the first; 0xaeb5b0, ours).
void *NextExtension(void *target, void *after)
{
    return nGZSceneGraph::FindDataList(TreeNode(target), GameAddress<const char>(kRvaBoneAdjust),
                                       static_cast<nGZSceneGraph::cIDataListExtension *>(after));
}

// The extension interface's virtual base (its cIGZUnknown): the vbptr at +4, the base's offset its second entry.
void *VirtualBase(void *p)
{
    uint8_t *const vbptr = static_cast<uint8_t *>(p) + 4;
    return vbptr + (*reinterpret_cast<const int32_t *const *>(vbptr))[1];
}

} // namespace

bool BodyPartSystemIface::ApplyBoneAdjusts(uint32_t kind, uint32_t key, uint32_t id, void *target)
{
    if (target == nullptr) return true;
    const uint8_t *const entry = OriginalFunction<uint8_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
        SystemOf(this), kind == 7 ? 3 : kind, id == 0 ? key : 0, id, 0);
    BoneAdjust *const *const bones = reinterpret_cast<BoneAdjust *const *>(entry + 0x18); // first, last
    for (BoneAdjust *b = bones[0]; b != bones[1]; ++b) {
        void *const node = nGZSceneGraph::FindNodeByName(BoneName(b), TreeNode(target), 0xf);
        if (node == nullptr) continue;
        void *adjust = NextExtension(target, nullptr);
        while (adjust != nullptr) {
            LocalRZString tag;
            if (VirtualCall<bool(__thiscall *)(void *, const char *, cRZString *), 18>(adjust, GameAddress<const char>(kRvaBoneTag), &tag.mText) &&
                tag.mText.mString == b->mBoneName) {
                break;
            }
            adjust = NextExtension(target, adjust);
        }
        if (adjust == nullptr) {
            void *created = nullptr;
            void *const framework = GetDirectorFrameworkObject();
            VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kExtensionClass, kExtensionIid, &created);
            VirtualCall<void(__thiscall *)(void *, const char *), 0>(created, GameAddress<const char>(kRvaBoneAdjust));
            VirtualCall<void(__thiscall *)(void *, const char *, const char *), 6>(created, GameAddress<const char>(kRvaBoneTag), BoneName(b));
            VirtualCall<void(__thiscall *)(void *, void *), 22>(target, VirtualBase(created));
            // The target holds it now; our reference goes (the extension stays in use below).
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(VirtualBase(created));
            adjust = created;
        }
        VirtualCall<void(__thiscall *)(void *, const float *), 64>(node, b->mTranslation);
        VirtualCall<void(__thiscall *)(void *, const float *), 65>(node, b->mRotation);
        VirtualCall<void(__thiscall *)(void *, const char *, const float *), 5>(adjust, GameAddress<const char>(kRvaBoneTranslation), b->mTranslation);
        VirtualCall<void(__thiscall *)(void *, const char *, const float *), 7>(adjust, GameAddress<const char>(kRvaBoneRotation), b->mRotation);
    }
    return true;
}

} // namespace nTSCASCore
