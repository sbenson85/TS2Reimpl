#include "sg/cFrameCache.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "sg/cDisplayList.h" // LightRecord
#include "sg/cLightRefNode.h"

#include <cstring>
#include <new>
#include <utility>

namespace nGZSceneGraph {

bool cFrameCache::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    switch (iid) {
    case 1:
    case kIid:
        *out = this;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
        return true;
    }
    return false;
}

cFrameCache *cFrameCache::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cFrameCache));
    return this;
}

void cFrameCache::Destruct()
{
    mVTable = GameAddress<const void>(kVTable);
    Flip(0, 0);
    Flip(0, 0);
    std::vector<Held>().swap(mHeldLast);
    std::vector<Held>().swap(mHeld);
    std::vector<Blob>().swap(mBlobsB);
    std::vector<Blob>().swap(mBlobsA);
}

void cFrameCache::Flip(uint32_t slots, uint32_t frame)
{
    mBlobsA.swap(mBlobsB);
    std::swap(mBlobCountA, mBlobCountB);
    if (mBlobsA.size() < slots) {
        mBlobsA.resize(slots, Blob{});
        mBlobCountA = slots;
    }
    if (mBlobsB.size() < slots) {
        mBlobsB.resize(slots, Blob{});
        mBlobCountB = slots;
    }
    // The end is read again after each release.
    for (Held *held = mHeldLast.data(); held != mHeldLast.data() + mHeldLastCount; ++held) {
        held->mObject->Release();
        held->mObject = nullptr;
        held->mWord = 0;
    }
    mHeldLast.swap(mHeld);
    ++mFlips;
    mHeldLastCount = mHeldCount;
    mHeldCount = 0;
    mFrame = frame;
}

namespace {

constexpr uint32_t kLightTypes = 13; // the counters StoreLights keeps (types 0-12; slot 30's lights are 1-6)

// This frame's blob for `slot`: the list grown (or cut) to slot + 1 when the slot is past its count.
cFrameCache::Blob &ThisFrameBlob(cFrameCache &cache, uint32_t slot)
{
    if (slot >= cache.mBlobCountB) {
        cache.mBlobCountB = slot + 1;
        cache.mBlobsB.resize(slot + 1, cFrameCache::Blob{});
    }
    return cache.mBlobsB[slot];
}

// A blob's cumulative count through light type `type` (nibble `type` of words 5-7).
uint32_t CountThrough(const cFrameCache::Blob &blob, uint32_t type)
{
    return blob.mWords[5 + (type >> 3)] >> ((type & 7) * 4) & 0xf;
}

} // namespace

cFrameCache::Blob *cFrameCache::CopyLastFrame(uint32_t slot, const float *ambient, const Blob *last)
{
    Blob &blob = ThisFrameBlob(*this, slot);
    blob.mWords[0] = mFlips;
    blob.mWords[1] = mHeldCount;
    std::memcpy(&blob.mWords[2], ambient, 3 * sizeof(float));
    blob.mWords[5] = last->mWords[5];
    blob.mWords[6] = last->mWords[6];
    blob.mWords[7] = last->mWords[7];
    const uint32_t count = LightCount(*last);
    mHeldCount += count;
    if (mHeld.size() < mHeldCount) mHeld.resize(mHeldCount, Held{});
    const Held *from = mHeldLast.data() + last->mWords[1];
    Held *to = mHeld.data() + blob.mWords[1];
    for (uint32_t i = 0; i < count; ++i) {
        to[i] = from[i];
        to[i].mObject->AddRef();
    }
    return &blob;
}

cFrameCache::Blob *cFrameCache::StoreLights(uint32_t slot, const float *ambient, const LightChoices *a,
                                            const LightChoices *b)
{
    Blob &blob = ThisFrameBlob(*this, slot);
    blob.mWords[0] = mFlips;
    blob.mWords[1] = mHeldCount;
    std::memcpy(&blob.mWords[2], ambient, 3 * sizeof(float));
    blob.mWords[5] = blob.mWords[6] = blob.mWords[7] = 0;
    const int32_t countA = a->mCount, total = a->mCount + b->mCount;
    mHeldCount += total;
    if (mHeld.size() < mHeldCount) mHeld.resize(mHeldCount, Held{});

    uint32_t counts[kLightTypes] = {};
    for (int32_t i = 0; i < a->mCount; ++i) ++counts[a->mData[i].mRecord->mType];
    for (int32_t i = 0; i < b->mCount; ++i) ++counts[b->mData[i].mRecord->mType];
    // Type 0 (ambient) is not in the counts: slot 30 keeps ambient lights apart, so no choice has it (the original would
    // read far outside the blob for its start; ours starts it at 0).
    uint32_t through = 0;
    for (uint32_t type = 1; type < kLightTypes; ++type) {
        through += counts[type];
        counts[type] = 0;
        blob.mWords[5 + (type >> 3)] |= through << ((type & 7) * 4);
    }
    if (mHeld.empty() || blob.mWords[1] >= mHeld.size()) return &blob;
    Held *const to = mHeld.data() + blob.mWords[1];
    for (int32_t j = 0; j < total; ++j) {
        const LightChoice &choice = j < countA ? a->mData[j] : b->mData[j - countA];
        const LightRecord *const record = choice.mRecord;
        const uint32_t type = record->mType;
        const uint32_t at = counts[type]++ + (type ? CountThrough(blob, type - 1) : 0);
        cIGZUnknown *const node = record->mNode;
        to[at].mObject = node;
        node->AddRef();
        std::memcpy(&to[at].mWord, &choice.mStrength, 4);
    }
    return &blob;
}

} // namespace nGZSceneGraph
