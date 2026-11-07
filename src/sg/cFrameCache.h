#pragma once
// nGZSceneGraph::cFrameCache (0x50 bytes, vtable 0xe75328; interface id 0x4bcf4f66): a view's per-frame cache. Each
// viewer node holds one (its slot 121's holder answers the query); the display list's DrawViewer (slot 23) takes it into
// the pass, and the view draw (slot 24) flips it once per frame. Two pairs of lists: two 32-byte blob lists that swap
// each flip and grow to the display list's slot count (one per draw slot), and two lists of counted objects with a word,
// this frame's and last frame's; a flip releases last frame's objects and makes this frame's last frame's.
//
// The objects are the game's (built by 0xa9d8b0 with the game's vtable), so this is the layout with the members as
// plain functions: the deleting destructor (slot 3) is hooked; the destructor and Flip are in effect through their
// callers (the hooked slot 3 and the display list's slots 23 / 24, ours), CopyLastFrame and StoreLights through the
// display list's light selection (ours).
#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZUnknown;

namespace nGZSceneGraph {

struct LightRecord;

// A light the selection chose for an item, with its strength over the item's box (cDisplayListLights.cpp).
struct LightChoice {
    LightRecord *mRecord;
    float mStrength;
};
// A short list of choices on the selection's stack, strongest first (8 for the directional, point and spot lights, 4
// for the others); a full list drops the weakest.
struct LightChoices {
    LightChoice *mData;
    int32_t mCapacity;
    int32_t mCount;
};

struct cFrameCache {
    static constexpr uint32_t kIid = 0x4bcf4f66;
    static constexpr uint32_t kVTable = 0xe75328;

    // A draw slot's blob (what is in it is the renderers'; the flip's new ones have words 0 and 5-7 zero, the rest are
    // whatever the original's stack held, so ours zeroes them all).
    struct Blob {
        uint32_t mWords[8];
    };
    // A counted object with a word (released by the flip after next).
    struct Held {
        cIGZUnknown *mObject;
        uint32_t mWord;
    };

    bool QueryInterface(uint32_t iid, void **out);   // slot 0, 0xaa21a0 (ret 8): 1 and kIid, null out refused
    cFrameCache *DeletingDestructor(uint32_t flags); // slot 3, 0xa9e5b0
    // 0xa9e3f0: the vtable stored, two flips (every held object released), the four lists freed (D, C, B, A).
    void Destruct();
    // 0xaa2e10 (ret 8): the blob lists swapped and each grown to `slots` (never shrunk); last frame's objects released
    // and cleared, this frame's list becomes last frame's (its count too, this frame's count 0); the flip count up; the
    // frame stored.
    void Flip(uint32_t slots, uint32_t frame);
    // 0xa9e5d0 (ret 0xc): this frame's blob for `slot` (the list grown to it) as last frame's `last` with `ambient`: the
    // flip count, where its objects start in this frame's list, the ambient colour, last's light counts; last's objects
    // and words copied to the end of this frame's list (each AddRef'd).
    Blob *CopyLastFrame(uint32_t slot, const float *ambient, const Blob *last);
    // 0xa9ef10 (ret 0x10): this frame's blob for `slot` from the chosen lights: the flip count, where the objects start,
    // the ambient colour, the cumulative counts by light type (types 1-12 as nibbles 1-12 of words 5-6); the lights'
    // nodes (AddRef'd) and strengths stored grouped by type, each type's in list order (`a`, then `b`).
    Blob *StoreLights(uint32_t slot, const float *ambient, const LightChoices *a, const LightChoices *b);
    // The lights a blob holds (its nibble 12: the count through type 12).
    static uint32_t LightCount(const Blob &blob) { return blob.mWords[6] >> 16 & 0xf; }

    const void *mVTable;
    uint32_t mRefCount;           // +0x04
    uint32_t mFrame;              // +0x08 the frame of the last flip
    uint32_t mFlips;              // +0x0c (2 from the constructor)
    std::vector<Blob> mBlobsA;    // +0x10
    uint32_t mBlobCountA;         // +0x1c
    std::vector<Blob> mBlobsB;    // +0x20
    uint32_t mBlobCountB;         // +0x2c
    uint32_t mHeldCount;          // +0x30 this frame's objects in use
    std::vector<Held> mHeld;      // +0x34
    uint32_t mHeldLastCount;      // +0x40 last frame's
    std::vector<Held> mHeldLast;  // +0x44
};
static_assert(sizeof(cFrameCache::Blob) == 0x20 && sizeof(cFrameCache::Held) == 8, "the original's elements");
static_assert(offsetof(cFrameCache, mBlobsA) == 0x10 && offsetof(cFrameCache, mBlobsB) == 0x20 &&
                  offsetof(cFrameCache, mHeld) == 0x34 && offsetof(cFrameCache, mHeldLast) == 0x44,
              "cFrameCache layout");
static_assert(sizeof(cFrameCache) == 0x50, "layout: the size 0xa9e5b0 frees");

} // namespace nGZSceneGraph
