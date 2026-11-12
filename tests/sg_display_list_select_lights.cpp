// cDisplayList's light selection (src/sg/cDisplayListLights.cpp) and the frame cache's two stores
// (src/sg/cFrameCache.cpp), original vs ours on the light world (tests/sg_display_list_lights.h): the selection 0xaa0130
// with last frame's reuse 0xaa2f40, the frame cache's 0xa9e5d0 (last frame's lights again) and 0xa9ef10 (new ones),
// the list insertion 0xaa1430 and the light bytes' growth 0xaa27e0.
#include "sg_display_list_world.h"

#include "sg_display_list_lights.h"

#include "compgeom/CompGeom.h"

using nGZCompGeom::BoundBox;
using nGZSceneGraph::LightChoice;
using nGZSceneGraph::LightChoices;

namespace {

using SelectFn = uint32_t(__thiscall *)(void *, DrawItem *, int32_t, const BoundBox *, const AffineTransform3 *,
                                        const uint32_t *);
using InsertFn = uint32_t(__cdecl *)(LightChoices *, const LightChoice *);
using GrowFn = void(__thiscall *)(void *, uint32_t);

DrawItem gItem; // the lit item when it is not one of the list's

void RandomItem(std::mt19937 &gen, DrawItem &item)
{
    uint32_t words[sizeof(DrawItem) / 4];
    for (uint32_t &w : words) w = gen();
    std::memcpy(&item, words, sizeof item);
    item.mMaterial = nullptr;
    item.mGeometry = nullptr;
    item.mPlacement = nullptr;
    item.mRecord = &gSourceRecord;
}

// The object: one or two passes on the stack over the frame cache, the switches, the counters, 0-4 items (some
// sorted), slot 30's records, the light bytes.
void LightObject(std::mt19937 &gen, int side, uint32_t maxType)
{
    Fresh(side);
    cDisplayList *const self = Self();
    self->mPasses.resize(1 + gen() % 2);
    for (Pass &pass : self->mPasses) {
        pass.mRecord = &pass.mViewerRecord;
        pass.mFrameCache = &gCache;
        self->mPassStack.push_back(&pass);
    }
    for (uint8_t &s : self->mSwitches) s = gen() % 3 != 0;
    for (uint32_t &w : self->mStats) w = gen() % 100;
    const uint32_t n = gen() % 5;
    self->mItems.resize(n);
    for (DrawItem &item : self->mItems) RandomItem(gen, item);
    self->mSortedCount = n ? gen() % (n + 1) : 0;
    LightRecords(gen, self, maxType);
    LightBytes(gen, self);
    LightCache(gen, maxType);
}

void Box(std::mt19937 &gen, BoundBox &box)
{
    float *const lo = &box.min.x, *const hi = &box.max.x;
    for (unsigned a = 0; a < 3; ++a) {
        lo[a] = LightReal(gen, 20.0f);
        hi[a] = gen() % 6 == 0 ? lo[a] : lo[a] + std::abs(LightReal(gen, 10.0f));
    }
}

// An item's words with its light block normalised.
void CaptureItem(Blob &o, const DrawItem &item, const LightPointers &pointers)
{
    uint32_t words[sizeof(DrawItem) / 4];
    std::memcpy(words, &item, sizeof item);
    words[offsetof(DrawItem, mWord3c) / 4] = pointers.Item(Self(), item);
    o.insert(o.end(), std::begin(words), std::end(words));
}

// After a call: the light state, the lit item, then the object (Capture) with the light vectors set aside and the
// items' light blocks (captured above) cleared.
void CaptureSelect(Blob &o, const LightPointers &pointers)
{
    cDisplayList *const self = Self();
    CaptureLights(o, pointers);
    CaptureItem(o, gItem, pointers);
    std::vector<uint32_t> blocks;
    for (DrawItem &item : self->mItems) {
        blocks.push_back(item.mWord3c);
        item.mWord3c = 0;
    }
    LightVectors aside;
    aside.Swap(self);
    Capture(o);
    aside.Swap(self);
    for (size_t i = 0; i < blocks.size(); ++i) self->mItems[i].mWord3c = blocks[i];
}

} // namespace

// 0xaa0130: 1-4 calls per seed on the light world (types 1-12, so the colour event too), the item one of the list's
// or apart, its slot 0-7 (in and past both blob lists), the cache switch and the item's no-cache bit drawn, so both
// the fresh choice and last frame's (reused, reused with new ambient, or rebuilt) are taken; between calls the frame's
// lights may change (a record made current or not, the last one removed). The return in al (the original sets only al);
// after each call the bytes, every item's light block, the cache, the object; at the end every call into the fakes.
DIFF_TEST(sg_display_list_select_lights, 0xaa0130)
{
    Fixture fixture;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            HeldFrees held; // no address handed out twice in the side (LightPointers)
            std::mt19937 gen(seed * 7919u + 0xaa0130u);
            svcfx::ResetAll();
            LightFakes(gen);
            LightObject(gen, side, 12);
            cDisplayList *const self = Self();
            RandomItem(gen, gItem);
            ctx.Mark(side ? "ours" : "original");
            Blob &o = outcome[side];
            const unsigned calls = 1 + gen() % 4;
            LightPointers pointers;
            for (unsigned call = 0; call < calls; ++call) {
                if (call && gen() % 3 == 0 && self->mCount54) {
                    // A light made current or not, or the last in use removed (its node told -1).
                    if (gen() % 2) gNodes[--self->mCount54].word78 = -1;
                    else self->mRecords58[gen() % self->mCount54].mCurrent ^= 1;
                    CurrentLights(self);
                }
                DrawItem *item = &gItem;
                if (!self->mItems.empty() && gen() % 2) item = &self->mItems[gen() % self->mItems.size()];
                item->mUserData = gen() % 8;
                item->mSubKey = gen() % 4 == 0 ? gen() | 0x10000u : gen() & ~0x10000u;
                item->mRecord = &gSourceRecord;
                BoundBox box;
                Box(gen, box);
                AffineTransform3 placement;
                LightFrame(gen, placement);
                uint32_t layer[2];
                LightLayer(gen, layer);
                const int32_t material = static_cast<int32_t>(gen() % 5) - 1;
                pointers.Note(self, &gItem);
                uint32_t result;
                if (side == 0)
                    result = ctx.Original<SelectFn>(0xaa0130)(gObject, item, material, &box, &placement, layer);
                else
                    result = self->SelectLights(item, material, &box, &placement, layer);
                o.push_back(0x5e1e0000u + call);
                o.push_back(result & 0xffu);
                CaptureSelect(o, pointers);
            }
            o.insert(o.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            FreeLights();
            FreeAll();
        }
        Same(ctx, outcome[0], outcome[1], "select lights", seed);
        if (HeldFrees::Overflowed()) ctx.Fail("%s seed %u: more frees than HeldFrees holds", "select lights", seed);
    }
}

// 0xaa1430 on its own: lists of capacity 1-8, empty to full, mostly in order (as the selection keeps them) and
// sometimes not; strengths equal, within 0.01, apart, zero, negative, NaN, infinite; ties ordered by node. The return
// in al; the list.
DIFF_TEST(sg_display_list_insert_light, 0xaa1430)
{
    static LightRecord records[kLightNodes];
    for (unsigned i = 0; i < kLightNodes; ++i)
        records[i].mNode = reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[i]);
    for (uint32_t seed = 0; seed < 30000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa1430u);
            LightChoice data[8];
            LightChoices list{data, static_cast<int32_t>(1 + gen() % 8), 0};
            list.mCount = static_cast<int32_t>(gen() % (list.mCapacity + 1));
            for (int32_t i = 0; i < list.mCount; ++i)
                data[i] = {&records[gen() % kLightNodes], PickStrength(gen())};
            if (gen() % 4 != 0) { // in order: strongest first, ties by node (larger first)
                for (int32_t i = 1; i < list.mCount; ++i)
                    for (int32_t j = i; j > 0; --j) {
                        const LightChoice &a = data[j - 1], &b = data[j];
                        const bool swap = a.mStrength < b.mStrength ||
                                          (a.mStrength == b.mStrength && a.mRecord->mNode < b.mRecord->mNode);
                        if (swap) std::swap(data[j - 1], data[j]);
                    }
            }
            const LightChoice choice{&records[gen() % kLightNodes], PickStrength(gen())};
            ctx.Mark(side ? "ours" : "original");
            const uint32_t result = side == 0 ? ctx.Original<InsertFn>(0xaa1430)(&list, &choice)
                                              : cDisplayList::InsertLight(&list, &choice);
            Blob &o = outcome[side];
            o.push_back(result & 0xffu);
            o.push_back(static_cast<uint32_t>(list.mCount));
            for (int32_t i = 0; i < list.mCapacity; ++i) {
                o.push_back(static_cast<uint32_t>(i < list.mCount ? data[i].mRecord - records : 0xff));
                o.push_back(i < list.mCount ? FloatBits(data[i].mStrength) : 0);
            }
        }
        Same(ctx, outcome[0], outcome[1], "insert light", seed);
    }
}

// 0xaa27e0 on its own: the bytes shrunk, kept, grown within and past the capacity, with and without bytes in use; the
// sorted items' blocks rebased when they move (those unset, outside, past the sorted count left).
DIFF_TEST(sg_display_list_grow_light_bytes, 0xaa27e0)
{
    Fixture fixture;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob outcome[2];
        std::string notes[2]; // each side's addresses and item words before and after, printed when the sides differ
        for (std::string &n : notes) n.reserve(400); // no allocation inside a side
        for (int side = 0; side < 2; ++side) {
            HeldFrees held; // no address handed out twice in the side (LightPointers, "the block moved")
            std::mt19937 gen(seed * 7919u + 0xaa27e0u);
            svcfx::ResetAll();
            Fresh(side);
            cDisplayList *const self = Self();
            const uint32_t n = gen() % 6;
            self->mItems.resize(n);
            for (DrawItem &item : self->mItems) RandomItem(gen, item);
            self->mSortedCount = n ? gen() % (n + 1) : 0;
            LightBytes(gen, self, true);
            const uint32_t size = static_cast<uint32_t>(self->mBytesA4.size());
            const uint32_t bytes = gen() % 4 == 0 ? size : gen() % 3 == 0 ? gen() % (size + 1) : size + gen() % 200;
            LightPointers pointers;
            pointers.Note(self);
            std::string &note = notes[side];
            {
                char b[160];
                std::snprintf(b, sizeof(b), "bytes %08x size %u cap %u used %u -> %u;", svcfx::P(self->mBytesA4.data()), size,
                              static_cast<uint32_t>(self->mBytesA4.capacity()), self->mWordA0, bytes);
                note = b;
                for (const DrawItem &item : self->mItems) {
                    std::snprintf(b, sizeof(b), " %08x", item.mWord3c);
                    note += b;
                }
            }
            ctx.Mark(side ? "ours" : "original");
            if (side == 0)
                ctx.Original<GrowFn>(0xaa27e0)(gObject, bytes);
            else
                self->GrowLightBytes(bytes);
            {
                char b[64];
                std::snprintf(b, sizeof(b), "; now %08x cap %u;", svcfx::P(self->mBytesA4.data()),
                              static_cast<uint32_t>(self->mBytesA4.capacity()));
                note += b;
                for (const DrawItem &item : self->mItems) {
                    std::snprintf(b, sizeof(b), " %08x", item.mWord3c);
                    note += b;
                }
            }
            Blob &o = outcome[side];
            o.push_back(svcfx::P(self->mBytesA4.data()) == pointers.Before());
            VectorShape(o, self->mBytesA4);
            for (char c : self->mBytesA4) o.push_back(static_cast<uint8_t>(c));
            for (const DrawItem &item : self->mItems) o.push_back(pointers.Item(self, item));
            std::vector<char> bytesAside;
            bytesAside.swap(self->mBytesA4);
            for (DrawItem &item : self->mItems) item.mWord3c = 0;
            Capture(o);
            bytesAside.swap(self->mBytesA4);
            Free(self->mBytesA4);
            FreeAll();
        }
        if (!Same(ctx, outcome[0], outcome[1], "grow light bytes", seed))
            for (int side = 0; side < 2; ++side) ctx.Fail("  %s: %s", side ? "ours" : "original", notes[side].c_str());
        if (HeldFrees::Overflowed()) ctx.Fail("%s seed %u: more frees than HeldFrees holds", "grow light bytes", seed);
    }
}
