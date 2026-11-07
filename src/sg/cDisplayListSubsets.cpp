// cDisplayList's slot 28 (DLb in docs/subsystems/scenegraph.md): a geometry's subsets as draw items in the current
// pass, with their bone frames (mBoneFrames), light state, material variants and depths; and its helpers, the bone
// frames' growth and the two depth measures.
#include "sg/cDisplayList.h"

#include "compgeom/Frames.h"  // AsFrame(...).ToMatrix4, FrameMath::SetRotation
#include "compgeom/Matrix4.h" // Matrix4Multiply
#include "game/game.h"
#include "sg/DisplayListSort.h"
#include "sg/cIGeometryRender.h"
#include "sg/cMaterialManager.h"
#include "sg/cShader.h" // cShader::GetInfo, ShaderInfo

#include <cstring>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::BoundBox;
using nGZCompGeom::FramePlacement;
using nGZCompGeom::RigidTransform3;
using nGZCompGeom::Vec3;

constexpr float kMinDepthW = 1e-5f;                // the least w a depth is divided by (0xda7658)
constexpr float kDepthStep = 1e-5f;                // the depth sort's step per item (the same constant)
constexpr float kFarDepth = 1e6f;                  // the nearest corner's start
constexpr uint8_t kAllPlanes = 0x3f;               // a node inside every frustum plane
constexpr uint32_t kFrameFlags = 1;                // a bone's bind frame before its rotation

// Slot 28's flags.
constexpr uint32_t kNoLights = 0x1;     // the items have no lights
constexpr uint32_t kOwnLights = 0x2;    // each subset selects its lights (also the sub key's bit 19)
constexpr uint32_t kBones = 0x10;       // bone frames from the poses
constexpr uint32_t kBlend = 0x20;       // the blend count in the state
constexpr uint32_t kCentreDepth = 0x40; // the box's centre for the depth (else the nearest corner)
constexpr uint32_t kCull = 0x80;        // subsets outside the frustum skipped
constexpr uint32_t kSubKeyBit2 = 0x100; // the sub key's bit 18

// The state's third word: the format's bits at 4, the render type at 11, the bone count at 15 (flag 2), the blend count
// at 22 (flag 4), the light state's bit 3 copied.
constexpr uint32_t kFormatMask = 0x7f0, kRenderTypeMask = 0x7800, kBoneCountMask = 0x3f8000, kBlendMask = 0xfc00000;
constexpr uint32_t kStateBones = 2, kStateBlend = 4, kStateLight = 8;

// A point's depth through a 4x4: its third row over its fourth, 0 when that is not above kMinDepthW.
float ProjectDepth(const Vec3 &p, const float *m)
{
    const float z = ((p.y * m[9] + p.x * m[8]) + p.z * m[10]) + m[11];
    const float w = ((p.y * m[13] + p.x * m[12]) + p.z * m[14]) + m[15];
    return w > kMinDepthW ? z / w : 0.0f;
}

} // namespace

// ---- the helpers --------------------------------------------------------------------------------------------------------

void cDisplayList::GrowBoneFrames(uint32_t count)
{
    // The frames the items may point into (none while none is in use).
    const uint32_t inUse = mWords84[3];
    const uintptr_t oldData = inUse ? reinterpret_cast<uintptr_t>(mBoneFrames.data()) : 0;
    const uintptr_t oldEnd = oldData + inUse * sizeof(FramePlacement);
    mBoneFrames.resize(count, reinterpret_cast<const FramePlacement &>(ts2_rva_106e3d8));
    const uintptr_t newData = reinterpret_cast<uintptr_t>(mBoneFrames.data());
    if (newData == oldData) return;
    DrawItem *const items = mItems.data();
    for (int32_t i = 0; i < static_cast<int32_t>(mSortedCount); ++i) {
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(items[i].mPlacement);
        if (pointer < oldData || pointer >= oldEnd) continue;
        items[i].mPlacement =
            reinterpret_cast<const void *>((pointer - oldData) / sizeof(FramePlacement) * sizeof(FramePlacement) + newData);
    }
}

float *__cdecl cDisplayList::CentreDepth(float *out, const BoundBox *box, const float *matrix)
{
    Vec3 centre;
    centre.x = (box->max.x - box->min.x) * 0.5f + box->min.x;
    centre.y = (box->max.y - box->min.y) * 0.5f + box->min.y;
    centre.z = (box->max.z - box->min.z) * 0.5f + box->min.z;
    *out = ProjectDepth(centre, matrix);
    return out;
}

void __cdecl cDisplayList::NearestCornerDepth(float *out, const BoundBox *box, const float *matrix)
{
    *out = kFarDepth;
    for (uint32_t i = 0; i < 8; ++i) {
        Vec3 corner;
        const float depth = ProjectDepth(*box->Corner(&corner, i), matrix);
        // The depth only when it is less (a NaN keeps the least so far).
        *out = depth < *out ? depth : *out;
    }
}

// ---- slot 28 ------------------------------------------------------------------------------------------------------------

void cDisplayList::AddGeometrySubsets(cIGeometryRender *geometry, uint32_t flags, const int32_t *materials,
                                      const uint32_t *const *subsetWords, const uint32_t *word24,
                                      const uint32_t (*layerStates)[2], const RigidTransform3 *bonePoses,
                                      uint32_t userDataBase)
{
    const auto *const info = reinterpret_cast<const GeometryInfo *>(geometry->GetFlags());
    if (mItems.size() < mSortedCount + info->mSubsets) mItems.resize(mSortedCount + info->mSubsets);
    Pass *const pass = mPassStack.back();
    NodeRecord *const record = pass->mNodes.data() + mWords84[2];
    const bool bones = (flags & kBones) && bonePoses && mSwitches[3];
    const bool blend = (flags & kBlend) && *word24 && mSwitches[4];

    // The bone frames: each bind frame with its pose, in view space, after the frames in use.
    const FramePlacement *boneFrames = nullptr;
    if (bones) {
        if (mBoneFrames.size() < mWords84[3] + info->mBones) GrowBoneFrames(mWords84[3] + info->mBones);
        FramePlacement *const block = mBoneFrames.data() + mWords84[3];
        boneFrames = block;
        nGZCompGeom::FrameMath frame;
        for (uint32_t k = 0; k < info->mBones; ++k) {
            const RigidTransform3 &bind = info->mBoneFrames[k];
            frame.mUnknown0 = kFrameFlags;
            std::memcpy(frame.mOffset, &bind.translation, sizeof frame.mOffset);
            frame.SetRotation(bind.rotation);
            static_cast<AffineTransform3 &>(block[k]) = frame;
            block[k].ComposeRigid(&bonePoses[k]);
            block[k].Compose(&pass->mViewInverse);
        }
        mWords84[3] += info->mBones;
    }

    // One item, written field by field per subset (the light selection sees the previous subset's material, sort key
    // and depth).
    DrawItem item{};
    item.mGeometry = geometry;
    item.mWords20[1] = *word24;
    item.mRecord = record;
    bool anyDepthSorted = false;
    bool lit = false; // kept from the subset before when the light state is copied
    const uint32_t start = mSortedCount;
    DrawItem *out = mItems.data() + start;
    const uint32_t current = record->mCurrent != 0 ? 1u : 0u;

    for (uint32_t i = 0; i < info->mSubsets; ++i) {
        const int32_t material = materials[i];
        if (material == -1) continue;
        uint32_t layerState[2] = {record->mLayerState[0], record->mLayerState[1]};
        if (layerStates) {
            layerState[0] = layerStates[i][0];
            layerState[1] = layerStates[i][1];
        }
        if (!pass->Shows(layerState)) continue;
        const auto *const subset = reinterpret_cast<const GeometrySubset *>(geometry->GetDrawInfo(i));
        uint8_t planeMask = record->mPlaneMask;
        if (planeMask != kAllPlanes && (flags & kCull) &&
            pass->mFrustum.ClassifyBox(&planeMask, &subset->mBounds, &record->mPlacement) > 0)
            continue;

        item.mWord00 = mMaterialManager->GetInfo(material).nameHash;
        item.mUserData = userDataBase + i;
        uint32_t keyHigh = planeMask != kAllPlanes ? 2u : 0u;
        keyHigh |= (subset->mSubKeyFlag != 0 ? 1u : 0u) | ((flags & kOwnLights) << 2) | current;
        item.mLayer = subset->mLayer;
        item.mWords20[0] = *subsetWords ? (*subsetWords)[i] : 0;
        item.mPlacement = &record->mPlacement;
        item.mState[0] = item.mState[1] = item.mState[2] = 0;
        item.mWord3c = 0;
        item.mMaterialIndex = material;
        uint32_t state = (subset->mFormat->GetFormatBits() << 4) & kFormatMask;
        state |= (pass->mRenderType << 11) & kRenderTypeMask;
        state |= subset->mFlag != 0 ? 1u : 0u;
        bool boneFramed = false;
        if (bones && subset->mBoneCount) {
            item.mPlacement = boneFrames;
            boneFramed = true;
            state = (state & ~kBoneCountMask) | ((subset->mBoneCount << 15) & kBoneCountMask) | kStateBones;
        }
        if (blend && subset->mBlendCount)
            state = (state & ~kBlendMask) | ((subset->mBlendCount << 22) & kBlendMask) | kStateBlend;
        item.mState[2] = state;
        item.mSubKey = (i & 0xffffu) | (keyHigh << 16);

        if (flags & kNoLights) {
            lit = false;
        } else if (flags & kOwnLights) {
            lit = SelectLights(&item, material, &subset->mBounds, &record->mPlacement, layerState);
        } else if (mWordE4 != mSortedCount) {
            // The light state of the item the node's draw began at.
            const DrawItem &first = mItems.data()[mWordE4];
            item.mState[0] = first.mState[0];
            item.mState[1] = first.mState[1];
            item.mWord3c = first.mWord3c;
            item.mState[2] |= first.mState[2] & kStateLight;
        } else {
            lit = SelectLights(&item, material, &record->mBounds, &record->mPlacement, layerState);
        }

        cIGZUnknown *const variant =
            mMaterialManager->GetVariant(static_cast<uint32_t>(material), item.mState, 1, subset->mBoneCount,
                                         subset->mBlendCount);
        const ShaderInfo *const shader = reinterpret_cast<cShader *>(variant)->GetInfo();
        if (!shader->mFirstPass || !geometry->IsSupported(shader->mFlags[0])) continue;
        item.mMaterial = variant;
        item.mSortKey = static_cast<int32_t>(shader->m84);
        item.mDepth = 0.0f;
        if (shader->m84 & 1) {
            anyDepthSorted = true;
            const int32_t frameIndex = boneFramed ? subset->mBoneIndices[0] : 0;
            const auto *const frame = static_cast<const AffineTransform3 *>(item.mPlacement) + frameIndex;
            float frameMatrix[16], matrix[16];
            nGZCompGeom::Matrix4Multiply(matrix, nGZCompGeom::AsFrame(*frame).ToMatrix4(frameMatrix), pass->mMatrices[3]);
            if (flags & kCentreDepth)
                CentreDepth(&item.mDepth, &subset->mBounds, matrix);
            else
                NearestCornerDepth(&item.mDepth, &subset->mBounds, matrix);
        }
        if (shader->mFlags[2] || lit) item.mSubKey |= 1u << 16;
        if (shader->mFlags[3] || (flags & kSubKeyBit2)) item.mSubKey |= 4u << 16;
        *out++ = item;
        ++mSortedCount;
    }
    if (!anyDepthSorted) return;

    // The new items in layer order; the depth-sorted ones then get their depths (each nudged by its place) in ascending
    // order, so their order stays the layer order's.
    DrawItem *const first = mItems.data() + start;
    DrawItem *const last = mItems.data() + mSortedCount;
    DisplayListSort::SortLayered(first, last, last - first, DrawItemLayerOrder{});
    const uint32_t count = mSortedCount - start;
    if (mDepths.size() < count) mDepths.resize(count);
    float *depth = mDepths.data();
    for (uint32_t j = 0; j < count; ++j) {
        const DrawItem &added = mItems.data()[start + j];
        if (added.mSortKey & 1) *depth++ = static_cast<float>(j) * kDepthStep + added.mDepth;
    }
    DisplayListSort::SortFloats(mDepths.data(), depth, depth - mDepths.data(), DisplayListSort::FloatLess{});
    const float *sorted = mDepths.data();
    for (uint32_t j = 0; j < count; ++j) {
        DrawItem &added = mItems.data()[start + j];
        if (added.mSortKey & 1) added.mDepth = *sorted++;
    }
}

} // namespace nGZSceneGraph
