// cDisplayList's light selection for slot 28's items (DLb in docs/subsystems/scenegraph.md): which of the scene's lights
// (slot 30's records) light an item, strongest first, kept per draw slot in the view's frame cache and reused next frame
// while the lights stay; the item's light block in mBytesA4.
#include "sg/cDisplayList.h"

#include "compgeom/Frames.h" // AddInOrder
#include "game/game.h"
#include "gz/cVector4fEvent.h" // ts2_rva_db07d4
#include "sg/SGLightInterfaces.h"
#include "sg/SGShaderAttributes.h" // cIShaderEventSource
#include "sg/cFrameCache.h"
#include "sg/cLightRefNode.h"

#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::AddInOrder;
using nGZCompGeom::BoundBox;
using nGZCompGeom::FramePlacement;

constexpr int32_t kStrongLights = 8;      // the list for the directional, point and spot lights
constexpr int32_t kOtherLights = 4;       // the list for the rest
constexpr uint32_t kFirstOtherType = 4;   // cILight::kEnvironmentCube and above go to the other list
constexpr float kSameStrength = 0.01f;    // strengths closer than this are ordered by node (0xda74e8)
constexpr uint32_t kColorEvent = 0x8c798726;
// The colour event: nGZEvent::cVectorEvent<cVector<4, float>> (its table owned: gz/cVector4fEvent.h).
constexpr uint32_t kTranslucent = 0x8;    // state word 2's bit for an alpha away from 1
constexpr uint32_t kLightBlockHeader = 12; // a light block's ambient colour before its records

// A light's layer state against the item's: the words equal (or, with flag 1, differ; flag 2: either) and the masks
// meet.
bool Shows(const uint32_t *item, const uint32_t *light, uint8_t flag)
{
    const bool word = flag == 2 || ((item[1] == light[1]) != (flag == 1));
    return word && (light[0] & item[0]) != 0;
}

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// The float's bits as an integer ordered like the floats (negative ones below zero), less `centre`'s.
int32_t UlpsFrom(float f, uint32_t centre)
{
    const uint32_t bits = Bits(f), sign = bits >> 31;
    return static_cast<int32_t>((bits ^ (0u - sign) >> 1) + sign - centre);
}

// Within 32 ulps of zero.
bool NearZero(float f)
{
    const int32_t v = UlpsFrom(f, 0);
    return v >> 5 == v >> 31;
}

// The alpha test: off by more than 31 ulps from 1 (the original's form of it).
bool AwayFromOne(float f)
{
    const int32_t v = UlpsFrom(f, 0x3f800000u);
    return ((static_cast<uint32_t>(v >> 26) ^ static_cast<uint32_t>(v)) & ~0x1fu) != 0;
}

// The light's strength over the box, from the item's placement into the light's space.
float StrengthOver(const LightRecord &record, const BoundBox *box, const AffineTransform3 *placement)
{
    FramePlacement frame;
    std::memcpy(&frame, placement, sizeof frame);
    AffineTransform3 toLight = *frame.Compose(reinterpret_cast<const FramePlacement *>(&record.mInverse));
    return record.mLight->StrengthOverBox(&box->min.x, &toLight);
}

// The two lists on the caller's stack.
struct ChoiceLists {
    LightChoice strong[kStrongLights];
    LightChoice other[kOtherLights];
    LightChoices a{strong, kStrongLights, 0};
    LightChoices b{other, kOtherLights, 0};
    LightChoices *For(const LightRecord *record) { return record->mType < kFirstOtherType ? &a : &b; }
};

cILightRefNode *AsLightNode(cIGZUnknown *object) { return static_cast<cILightRefNode *>(object); }

} // namespace

bool __cdecl cDisplayList::InsertLight(LightChoices *list, const LightChoice *choice)
{
    const float strength = choice->mStrength;
    if (0.0f >= strength) return false;
    // Whether `other` goes before the new choice.
    const auto greater = [&](const LightChoice &other) {
        if (!(std::fabs(other.mStrength - strength) >= kSameStrength))
            return reinterpret_cast<uintptr_t>(other.mRecord->mNode) > reinterpret_cast<uintptr_t>(choice->mRecord->mNode);
        return other.mStrength > strength;
    };
    int32_t count = list->mCount;
    if (count == list->mCapacity && greater(list->mData[count - 1])) return false;
    int32_t at = 0;
    for (int32_t n = count; n != 0;) {
        const int32_t half = n >> 1;
        if (greater(list->mData[at + half])) {
            at += half + 1;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    count = list->mCount;
    if (count < list->mCapacity) {
        list->mCount = count + 1;
        for (int32_t i = count; i > at; --i) list->mData[i] = list->mData[i - 1];
    }
    list->mData[at] = *choice;
    return true;
}

void cDisplayList::GrowLightBytes(uint32_t bytes)
{
    const uintptr_t old = mWordA0 ? reinterpret_cast<uintptr_t>(mBytesA4.data()) : 0;
    mBytesA4.resize(bytes);
    const uintptr_t now = reinterpret_cast<uintptr_t>(mBytesA4.data());
    if (now == old) return;
    for (int32_t i = 0; i < static_cast<int32_t>(mSortedCount); ++i) {
        DrawItem &item = mItems.data()[i];
        if (item.mWord3c) item.mWord3c = static_cast<uint32_t>(item.mWord3c - old + now);
    }
}

cFrameCache::Blob *cDisplayList::FindCachedLights(uint32_t slot, DrawItem *item, const BoundBox *box,
                                                  const AffineTransform3 *placement, const uint32_t *layerState,
                                                  bool *stored)
{
    cFrameCache *const cache = mPassStack.back()->mFrameCache;
    if (slot >= cache->mBlobCountA) return nullptr;
    cFrameCache::Blob *const last = &cache->mBlobsA[slot];
    if (last->mWords[0] != cache->mFlips - 1) return nullptr;

    float ambient[3];
    std::memcpy(ambient, &last->mWords[2], sizeof ambient);
    if (mWords84[0]) {
        ambient[0] = ambient[1] = ambient[2] = 0.0f;
        for (int32_t i = 0; i < static_cast<int32_t>(mCount74); ++i) {
            const AmbientRecord &record = mRecords78[i];
            if (!Shows(layerState, record.mLayerState, record.mFlag)) continue;
            float params[8];
            record.mLight->GetStandardLightConst()->GetParams(params, static_cast<uint32_t>(
                                                                          reinterpret_cast<uintptr_t>(item)));
            ambient[0] = AddInOrder(params[0], ambient[0]);
            ambient[1] = AddInOrder(params[1], ambient[1]);
            ambient[2] = AddInOrder(params[2], ambient[2]);
        }
    }

    const uint32_t count = cFrameCache::LightCount(*last);
    cFrameCache::Held *const held = cache->mHeldLast.data() + last->mWords[1];
    if (mCount64 == 0) {
        // No light is current: last frame's choice holds while its lights are all still lights.
        uint32_t i = 0;
        while (i < count && AsLightNode(held[i].mObject)->GetWord78() != -1) ++i;
        if (i == count) {
            mStats[2] += count;
            *stored = false;
            return cache->CopyLastFrame(slot, ambient, last);
        }
    }

    ChoiceLists lists;
    bool reusable = true;
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t index = AsLightNode(held[i].mObject)->GetWord78();
        if (index == -1) {
            reusable = false;
            continue;
        }
        LightRecord *const record = reinterpret_cast<LightRecord *>(
            reinterpret_cast<uintptr_t>(mRecords58.data()) + (static_cast<uint32_t>(index) << 7));
        if (record->mCurrent) {
            reusable = false;
            continue;
        }
        LightChoice choice{record, 0.0f};
        std::memcpy(&choice.mStrength, &held[i].mWord, 4);
        InsertLight(lists.For(record), &choice);
    }
    bool changed = false;
    for (int32_t i = 0; i < static_cast<int32_t>(mCount64); ++i) {
        LightRecord *const record = mWords68[i];
        if (!Shows(layerState, record->mLayerState, record->mFlag)) continue;
        const LightChoice choice{record, StrengthOver(*record, box, placement)};
        changed |= InsertLight(lists.For(record), &choice);
    }
    if (reusable && !changed) {
        mStats[2] += count;
        *stored = false;
        return cache->CopyLastFrame(slot, ambient, last);
    }
    mStats[3] += lists.a.mCount + lists.b.mCount;
    *stored = true;
    return cache->StoreLights(slot, ambient, &lists.a, &lists.b);
}

bool cDisplayList::SelectLights(DrawItem *item, int32_t, const BoundBox *box, const AffineTransform3 *placement,
                                const uint32_t *layerState)
{
    bool stored = true;
    Pass *const pass = mPassStack.back();
    const uint32_t slot = item->mUserData;
    cFrameCache::Blob *blob = nullptr;
    if (mSwitches[2] && !(item->mSubKey & 0x10000))
        blob = FindCachedLights(slot, item, box, placement, layerState, &stored);
    if (!blob) {
        float ambient[3] = {0.0f, 0.0f, 0.0f};
        for (int32_t i = 0; i < static_cast<int32_t>(mCount74); ++i) {
            const AmbientRecord &record = mRecords78[i];
            if (!Shows(layerState, record.mLayerState, record.mFlag)) continue;
            float params[8];
            record.mLight->GetStandardLightConst()->GetParams(params, static_cast<uint32_t>(
                                                                          reinterpret_cast<uintptr_t>(item)));
            ambient[0] = AddInOrder(ambient[0], params[0]);
            ambient[1] = AddInOrder(params[1], ambient[1]);
            ambient[2] = AddInOrder(params[2], ambient[2]);
        }
        ChoiceLists lists;
        for (int32_t i = 0; i < static_cast<int32_t>(mCount54); ++i) {
            LightRecord *const record = &mRecords58[i];
            if (!Shows(layerState, record->mLayerState, record->mFlag)) continue;
            const LightChoice choice{record, StrengthOver(*record, box, placement)};
            InsertLight(lists.For(record), &choice);
        }
        mStats[3] += lists.a.mCount + lists.b.mCount;
        blob = pass->mFrameCache->StoreLights(slot, ambient, &lists.a, &lists.b);
    }

    item->mState[0] = blob->mWords[5];
    item->mState[1] = blob->mWords[6];
    const uint32_t count = cFrameCache::LightCount(*blob);
    float ambient[3];
    std::memcpy(ambient, &blob->mWords[2], sizeof ambient);
    if (count == 0 && NearZero(ambient[0]) && NearZero(ambient[1]) && NearZero(ambient[2])) {
        item->mWord3c = 0;
        return stored;
    }
    const uint32_t bytes = count * 4 + kLightBlockHeader;
    if (mBytesA4.size() < bytes + mWordA0) GrowLightBytes(bytes + mWordA0);
    item->mWord3c = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mBytesA4.data())) + kLightBlockHeader + mWordA0;
    mWordA0 += bytes;
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(item->mWord3c - kLightBlockHeader)), ambient,
                sizeof ambient);
    if (count) {
        const cFrameCache::Held *const held = pass->mFrameCache->mHeld.data() + blob->mWords[1];
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t index = static_cast<uint32_t>(AsLightNode(held[i].mObject)->GetWord78());
            reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(item->mWord3c))[i] =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mRecords58.data())) + (index << 7);
        }
    }

    // Colour lights (type 7: nibble 7 above nibble 6): the record's node gives the colour event; its alpha away from 1
    // marks the item translucent.
    if (item->mState[0] >> 28 != (item->mState[0] >> 24 & 0xf)) {
        struct {
            const void *mVTable;
            float mColor[4];
        } event{&ts2_rva_db07d4, {1.0f, 1.0f, 1.0f, 1.0f}};
        (*static_cast<cIShaderEventSource *const *>(item->mRecord))->GetEvent(&event, kColorEvent, 0);
        if (AwayFromOne(event.mColor[0])) item->mState[2] |= kTranslucent;
    }
    return stored;
}

} // namespace nGZSceneGraph
