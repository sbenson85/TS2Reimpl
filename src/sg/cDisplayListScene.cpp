// cDisplayList's scene slots: AddLight (slot 30) records the lights a pass shows, with the growth of its light list.
#include "sg/cDisplayList.h"

#include "compgeom/SeparatingAxes.h" // FramePlacement::Invert
#include "sg/SGLightInterfaces.h"    // cILight
#include "sg/cLightRefNode.h"        // cILightRefNode

#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kAmbientLight = 0; // cILight::GetLightType of an ambient light
constexpr uint32_t kNewFrameFlags = 0x1f;
constexpr uintptr_t kLightRecordMask = ~static_cast<uintptr_t>(sizeof(LightRecord) - 1);

} // namespace

void cDisplayList::GrowLights(uint32_t count)
{
    // The block the current list points into (none while no light is in use).
    const uintptr_t oldData = mCount54 ? reinterpret_cast<uintptr_t>(mRecords58.data()) : 0;
    const size_t oldSize = mRecords58.size();
    mRecords58.resize(count);
    // A new record's frames start with every flag (the original's record constructor sets only these two words).
    for (size_t i = oldSize; i < mRecords58.size(); ++i)
        mRecords58[i].mFrame.mUnknown0 = mRecords58[i].mInverse.mUnknown0 = kNewFrameFlags;
    const uintptr_t newData = reinterpret_cast<uintptr_t>(mRecords58.data());
    if (newData == oldData) return;
    // Each pointer keeps its record's offset, rounded down to a whole record.
    for (int32_t i = 0; i < static_cast<int32_t>(mCount64); ++i) {
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(mWords68[i]);
        mWords68[i] = reinterpret_cast<LightRecord *>(((pointer - oldData) & kLightRecordMask) + newData);
    }
}

void cDisplayList::AddLight(cILight *light, const AffineTransform3 *frame)
{
    Pass *const pass = mPassStack.back();
    cILightRefNode *const node = static_cast<cILightRefNode *>(pass->mEntriesB[mWords84[1]].mObject);
    uint32_t layerState[2];
    node->GetLayerState(layerState);
    if (!pass->Shows(layerState)) return;

    const bool current = node->GetDetailLimit() == mFrame;
    const uint8_t flag = node->GetLightFlag();
    const uint32_t type = light->GetLightType();

    if (type == kAmbientLight) {
        // The list only grows: records past the count are kept for reuse.
        if (mCount74 == mRecords78.size()) mRecords78.resize(mCount74 + 1);
        AmbientRecord &record = mRecords78[mCount74];
        record.mLight = light;
        record.mLayerState[0] = layerState[0];
        record.mLayerState[1] = layerState[1];
        record.mFlag = flag;
        record.mCurrent = current;
        ++mCount74;
        if (current) ++mWords84[0];
        return;
    }

    if (mRecords58.size() < mCount54 + 1) GrowLights(mCount54 + 1);
    node->SetWord78(static_cast<int32_t>(mCount54));
    LightRecord &record = mRecords58[mCount54];
    record.mLight = light;
    record.mNode = node;
    record.mType = type;
    record.mLayerState[0] = layerState[0];
    record.mLayerState[1] = layerState[1];
    record.mFlag = flag;
    record.mCurrent = current;
    record.mFrame = *frame;
    nGZCompGeom::FramePlacement inverse;
    std::memcpy(&inverse, frame, sizeof inverse);
    record.mInverse = *inverse.Invert();
    ++mCount54;
    if (!record.mCurrent) return;
    if (mWords68.size() < mCount64 + 1) mWords68.resize(mCount64 + 1);
    mWords68[mCount64++] = &record;
}

} // namespace nGZSceneGraph
