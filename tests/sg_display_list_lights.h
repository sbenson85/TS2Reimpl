#pragma once
// The display list's light world (tests/sg_display_list_select_lights.cpp, tests/sg_display_list_subsets.cpp), after
// tests/sg_display_list_world.h: slot 30's light records (mRecords58, the current list mWords68, the ambient records
// mRecords78), the pass's frame cache with last frame's blobs and objects, and the light block bytes (mBytesA4) with
// items pointing into them; hand-made fakes (svcfx's have neither slot 97 nor float returns) logging every call.
//
// Fakes: the lights (GetStandardLightConst 34 answers the light itself; GetParams 50 the light's colour, StrengthOverBox
// 38 a strength drawn from the light and every bit of the box and the transform: zero, negative, NaN, infinite, close
// pairs within 0.01), the light nodes (AddRef 1, Release 2, GetWord78 97: the record's index or -1), the item's record
// node (GetEvent 10: the colour event's words, then a colour).
#include "sg/cFrameCache.h"
#include "sg/cLightRefNode.h"
#include "sg/SGLightInterfaces.h"

#include <cmath>
#include <tuple>
#include <utility>

using nGZSceneGraph::AmbientRecord;
using nGZSceneGraph::cFrameCache;
using nGZSceneGraph::LightRecord;

namespace {

constexpr uint32_t evStandard = 0x1a0034, evParams = 0x1a0050, evStrength = 0x1a0038, evAddRef = 0x1a0001,
                   evRelease = 0x1a0002, evWord78 = 0x1a0097, evEvent = 0x1a000a;
constexpr unsigned kLightRecords = 12, kAmbients = 4, kLightNodes = kLightRecords + 2;
constexpr uint32_t kColorEventVTable = 0xdb07d4;

struct FakeLight {
    void *const *vtable;
    uint32_t id;
    float colour[8];
    uint32_t salt;
};
struct FakeNode {
    void *const *vtable;
    uint32_t id;
    int32_t word78;
    uint32_t refs;
};
struct FakeSource {
    void *const *vtable;
    uint32_t id;
    float colour[4];
    bool write;
};

FakeLight gLights[kLightRecords + kAmbients];
FakeNode gNodes[kLightNodes];
FakeSource gSource;
NodeRecord gSourceRecord; // the item's record: its node is gSource
cFrameCache gCache;

uint32_t LightMix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ b * 0x85ebca77u;
    h ^= h >> 15;
    return h * 0xc2b2ae3du ^ h >> 13;
}

uint32_t FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

float LightReal(std::mt19937 &gen, float range)
{
    return (static_cast<float>(gen() % 20001) / 10000.0f - 1.0f) * range;
}

// A light's strength over a box: picked by the hash of everything it was given, from values that test the list's order
// (equal, within 0.01 of each other, just outside it) and its refusals (zero, negative, NaN).
float PickStrength(uint32_t h)
{
    static const float kValues[] = {0.0f, -0.0f, -1.0f, NAN, 0.3f, 0.305f, 0.3101f, 0.5f, 0.5f, 0.9f, 2.0f, 1e-6f,
                                    INFINITY, 0.299f, 0.75f, 1.2f};
    return kValues[h % (sizeof kValues / sizeof kValues[0])];
}

void *__fastcall LightStandard(FakeLight *self, void *)
{
    svcfx::Ev(evStandard);
    svcfx::Ev(self->id);
    return self;
}

void __fastcall LightParams(FakeLight *self, void *, float *out, uint32_t)
{
    svcfx::Ev(evParams);
    svcfx::Ev(self->id);
    for (unsigned i = 0; i < 8; ++i) out[i] = self->colour[i];
}

float __fastcall LightStrength(FakeLight *self, void *, const float *box, const AffineTransform3 *toLight)
{
    svcfx::Ev(evStrength);
    svcfx::Ev(self->id);
    uint32_t h = self->salt;
    for (unsigned i = 0; i < 6; ++i) {
        svcfx::Ev(FloatBits(box[i]));
        h = LightMix(h, FloatBits(box[i]));
    }
    uint32_t words[sizeof(AffineTransform3) / 4];
    std::memcpy(words, toLight, sizeof words);
    for (uint32_t w : words) {
        svcfx::Ev(w);
        h = LightMix(h, w);
    }
    return PickStrength(h);
}

uint32_t __fastcall NodeAddRef(FakeNode *self, void *)
{
    svcfx::Ev(evAddRef);
    svcfx::Ev(self->id);
    return ++self->refs;
}

uint32_t __fastcall NodeRelease(FakeNode *self, void *)
{
    svcfx::Ev(evRelease);
    svcfx::Ev(self->id);
    return --self->refs;
}

int32_t __fastcall NodeWord78(FakeNode *self, void *)
{
    svcfx::Ev(evWord78);
    svcfx::Ev(self->id);
    return self->word78;
}

// The colour event: its vtable (the game's cVectorEvent<cVector<4, float>>), the colour it starts with, the type and
// index; then the source's colour written (or the event left).
void __fastcall SourceEvent(FakeSource *self, void *, void *event, uint32_t type, int32_t index)
{
    svcfx::Ev(evEvent);
    svcfx::Ev(self->id);
    uint32_t words[5];
    std::memcpy(words, event, sizeof words);
    svcfx::Ev(words[0] == svcfx::P(GameAddress<void>(kColorEventVTable)));
    for (unsigned i = 1; i < 5; ++i) svcfx::Ev(words[i]);
    svcfx::Ev(type);
    svcfx::Ev(static_cast<uint32_t>(index));
    if (self->write) std::memcpy(static_cast<uint8_t *>(event) + 4, self->colour, sizeof self->colour);
}

void *const *LightVTable()
{
    static void *table[64] = {};
    table[34] = reinterpret_cast<void *>(&LightStandard);
    table[38] = reinterpret_cast<void *>(&LightStrength);
    table[50] = reinterpret_cast<void *>(&LightParams);
    return table;
}

void *const *NodeVTable()
{
    static void *table[100] = {};
    table[1] = reinterpret_cast<void *>(&NodeAddRef);
    table[2] = reinterpret_cast<void *>(&NodeRelease);
    table[97] = reinterpret_cast<void *>(&NodeWord78);
    return table;
}

void *const *SourceVTable()
{
    static void *table[16] = {};
    table[10] = reinterpret_cast<void *>(&SourceEvent);
    return table;
}

// A frame for a light's inverse or an item's placement: flags, a matrix (sometimes the identity's entries), an offset.
void LightFrame(std::mt19937 &gen, AffineTransform3 &frame)
{
    uint32_t words[13];
    words[0] = gen() % 8;
    for (unsigned i = 1; i < 13; ++i) {
        const float f =
            i < 10 && gen() % 3 == 0 ? ((i - 1) % 4 == 0 ? 1.0f : 0.0f) : LightReal(gen, i < 10 ? 2.0f : 20.0f);
        words[i] = FloatBits(f);
    }
    std::memcpy(&frame, words, sizeof words);
}

// A colour: mostly small, sometimes exactly zero or within a few ulps of it (the selection's near-zero test), sometimes
// negative, sometimes a NaN with its own payload (which of two NaNs a sum keeps shows the order of its operands).
float LightColour(std::mt19937 &gen)
{
    switch (gen() % 7) {
    case 0: return 0.0f;
    case 1: {
        const uint32_t bits = gen() % 70 | (gen() % 2 ? 0x80000000u : 0u);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    case 3: {
        const uint32_t bits = 0x7fc00000u | (gen() & 0x3fffffu) | (gen() % 2 ? 0x80000000u : 0u);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
    case 2: return -LightReal(gen, 0.5f);
    default: return LightReal(gen, 1.0f);
    }
}

// A layer state the item's will meet sometimes: one or two of four bits, the word 0-2.
void LightLayer(std::mt19937 &gen, uint32_t state[2])
{
    state[0] = gen() % 5 == 0 ? 0 : 1u << (gen() % 4) | (gen() % 2 ? 1u << (gen() % 4) : 0u);
    state[1] = gen() % 3;
}

// The fakes for a seed; `maxType` the highest light type the records get (6 keeps the colour event out: slot 28's test
// has no record node for it).
void LightFakes(std::mt19937 &gen)
{
    for (unsigned i = 0; i < kLightRecords + kAmbients; ++i) {
        FakeLight &light = gLights[i];
        light.vtable = LightVTable();
        light.id = 0x500 + i;
        for (float &c : light.colour) c = LightColour(gen);
        light.salt = gen();
    }
    for (unsigned i = 0; i < kLightNodes; ++i) {
        FakeNode &node = gNodes[i];
        node.vtable = NodeVTable();
        node.id = 0x600 + i;
        node.word78 = -1;
        node.refs = 100;
    }
    gSource.vtable = SourceVTable();
    gSource.id = 0x700;
    gSource.write = gen() % 4 != 0;
    for (float &c : gSource.colour) c = gen() % 2 ? 1.0f : LightColour(gen);
    if (gen() % 3 == 0) {
        const uint32_t bits = 0x3f800000u + gen() % 80 - 40; // within and past 31 ulps of 1
        std::memcpy(&gSource.colour[0], &bits, 4);
    }
    std::memset(&gSourceRecord, 0, sizeof gSourceRecord);
    gSourceRecord.mNode = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(&gSource);
}

// The current list from the records in use (slot 30's: those with mCurrent set).
void CurrentLights(cDisplayList *self)
{
    self->mWords68.clear();
    for (uint32_t i = 0; i < self->mCount54; ++i)
        if (self->mRecords58[i].mCurrent) self->mWords68.push_back(&self->mRecords58[i]);
    self->mCount64 = static_cast<uint32_t>(self->mWords68.size());
}

// Slot 30's records on the object: the lights in use, each node told its index, and some past the count (their nodes
// told -1 mostly: removed since), the current list, the ambient records; the ambient-current count.
void LightRecords(std::mt19937 &gen, cDisplayList *self, uint32_t maxType)
{
    const uint32_t records = gen() % 5 == 0 ? 0 : 1 + gen() % kLightRecords;
    self->mRecords58.resize(records);
    for (uint32_t i = 0; i < records; ++i) {
        LightRecord &record = self->mRecords58[i];
        std::memset(&record, 0, sizeof record);
        record.mLight = reinterpret_cast<nGZSceneGraph::cILight *>(&gLights[i]);
        record.mNode = reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[i]);
        record.mType = 1 + gen() % maxType;
        LightLayer(gen, record.mLayerState);
        record.mFlag = static_cast<uint8_t>(gen() % 7 == 0 ? 3 + gen() % 200 : gen() % 3);
        record.mCurrent = static_cast<uint8_t>(gen() % 3 == 0 ? (gen() % 4 ? 1 : gen()) : 0);
        LightFrame(gen, record.mFrame);
        LightFrame(gen, record.mInverse);
    }
    self->mCount54 = records ? records - gen() % 2 * (gen() % (records + 1)) : 0;
    for (uint32_t i = 0; i < records; ++i)
        gNodes[i].word78 = i < self->mCount54 || gen() % 4 == 0 ? static_cast<int32_t>(i) : -1;
    CurrentLights(self);
    if (gen() % 4 == 0 && records) self->mWords68.push_back(&self->mRecords58[gen() % records]); // past the count
    const uint32_t ambients = gen() % (kAmbients + 1);
    self->mRecords78.resize(ambients);
    for (uint32_t i = 0; i < ambients; ++i) {
        AmbientRecord &record = self->mRecords78[i];
        std::memset(&record, 0, sizeof record);
        record.mLight = reinterpret_cast<nGZSceneGraph::cILight *>(&gLights[kLightRecords + i]);
        LightLayer(gen, record.mLayerState);
        record.mFlag = static_cast<uint8_t>(gen() % 3);
        record.mCurrent = static_cast<uint8_t>(gen() % 3 == 0);
    }
    self->mCount74 = ambients ? ambients - gen() % 2 : 0;
    self->mWords84[0] = gen() % 2 ? 0 : 1 + gen() % 3;
}

// A blob's light counts for a frame-cache blob: `count` lights spread over types 1-12, as cumulative nibbles.
void LightNibbles(std::mt19937 &gen, cFrameCache::Blob &blob, uint32_t count, uint32_t maxType)
{
    uint32_t per[13] = {};
    for (uint32_t i = 0; i < count; ++i) ++per[1 + gen() % maxType];
    uint32_t through = 0;
    blob.mWords[5] = blob.mWords[6] = blob.mWords[7] = 0;
    for (uint32_t type = 1; type < 13; ++type) {
        through += per[type];
        blob.mWords[5 + (type >> 3)] |= through << ((type & 7) * 4);
    }
    if (gen() % 6 == 0) blob.mWords[7] = gen(); // words the selection copies but never reads
}

// The pass's frame cache: the flip count, last frame's blobs (most of the frame before, some older) over last frame's
// objects (the records' nodes and two nodes no record has), this frame's blobs and objects so far.
void LightCache(std::mt19937 &gen, uint32_t maxType)
{
    cFrameCache &cache = gCache;
    std::memset(static_cast<void *>(&cache), 0, offsetof(cFrameCache, mBlobsA));
    cache.mVTable = GameAddress<void>(cFrameCache::kVTable);
    cache.mRefCount = 1;
    cache.mFrame = gen() % 50;
    cache.mFlips = 2 + gen() % 6;
    const uint32_t lastBlobs = gen() % 7;
    cache.mBlobsA.resize(lastBlobs);
    cache.mBlobCountA = lastBlobs ? lastBlobs - gen() % 2 : 0;
    uint32_t heldLast = 0;
    for (cFrameCache::Blob &blob : cache.mBlobsA) {
        blob.mWords[0] = gen() % 5 ? cache.mFlips - 1 : cache.mFlips - 2 - gen() % 2;
        const uint32_t count = gen() % 6 == 0 ? 0 : gen() % 4 == 0 ? gen() % 13 : 1 + gen() % 5;
        blob.mWords[1] = heldLast;
        heldLast += count + gen() % 2;
        for (unsigned k = 2; k < 5; ++k) blob.mWords[k] = FloatBits(LightColour(gen));
        LightNibbles(gen, blob, count, maxType);
    }
    cache.mHeldLast.resize(heldLast + gen() % 3);
    for (cFrameCache::Held &held : cache.mHeldLast) {
        held.mObject = reinterpret_cast<cIGZUnknown *>(&gNodes[gen() % kLightNodes]);
        held.mWord = FloatBits(PickStrength(gen()));
    }
    cache.mHeldLastCount = heldLast;
    const uint32_t thisBlobs = gen() % 5;
    cache.mBlobsB.resize(thisBlobs);
    cache.mBlobCountB = thisBlobs ? thisBlobs - gen() % 2 * (gen() % (thisBlobs + 1)) : 0;
    for (cFrameCache::Blob &blob : cache.mBlobsB)
        for (uint32_t &w : blob.mWords) w = gen() | 1u;
    cache.mHeldCount = gen() % 4;
    cache.mHeld.resize(gen() % 3 == 0 ? 0 : cache.mHeldCount + gen() % 4);
    for (cFrameCache::Held &held : cache.mHeld) {
        held.mObject = reinterpret_cast<cIGZUnknown *>(&gNodes[gen() % kLightNodes]);
        held.mWord = gen();
    }
    if (gen() % 3 == 0) cache.mHeld.reserve(cache.mHeld.size() + gen() % 8);
    if (gen() % 3 == 0) cache.mBlobsB.reserve(cache.mBlobsB.size() + gen() % 4);
}

// The light block bytes: some in use (whole words), the items' blocks pointing into them or none. With `elsewhere`
// (the growth's own test) some items' blocks are small values, and all are when no bytes are in use: a growth adds the
// block's move to them (from no block at all when none are in use), which the capture can still read.
void LightBytes(std::mt19937 &gen, cDisplayList *self, bool elsewhere = false)
{
    const uint32_t size = gen() % 3 == 0 ? 0 : 4 * (gen() % 40);
    self->mBytesA4.resize(size);
    for (char &c : self->mBytesA4) c = static_cast<char>(gen());
    if (gen() % 3 == 0) self->mBytesA4.reserve(size + gen() % 64);
    self->mWordA0 = size ? 4 * (gen() % (size / 4 + 1)) : 0;
    const uint32_t data = svcfx::P(self->mBytesA4.data());
    for (DrawItem &item : self->mItems) {
        const uint32_t kind = gen() % 4;
        const bool plain = kind == 1 || !size || !self->mWordA0;
        item.mWord3c = kind == 0 ? 0 : plain ? (elsewhere ? 0x4000u + gen() % 64 : 0) : data + gen() % size;
    }
}

// The cache's state: counts, last frame's and this frame's blobs (a new blob's words 1-4 are what the original's stack
// held, so they are masked on blobs with word 0 still zero), the objects as node indexes, the nodes' counts.
uint32_t NodeIndex(const void *p)
{
    for (unsigned i = 0; i < kLightNodes; ++i)
        if (p == &gNodes[i]) return 0x06000000u | i;
    return svcfx::P(p);
}

void CaptureCache(Blob &o)
{
    const cFrameCache &cache = gCache;
    o.push_back(0xcace);
    o.push_back(cache.mFlips);
    o.push_back(cache.mFrame);
    o.push_back(cache.mBlobCountA);
    o.push_back(cache.mBlobCountB);
    o.push_back(cache.mHeldCount);
    o.push_back(cache.mHeldLastCount);
    for (const std::vector<cFrameCache::Blob> *blobs : {&cache.mBlobsA, &cache.mBlobsB}) {
        VectorShape(o, *blobs);
        for (const cFrameCache::Blob &blob : *blobs)
            for (unsigned k = 0; k < 8; ++k) o.push_back(blob.mWords[0] == 0 && k >= 1 && k <= 4 ? 0 : blob.mWords[k]);
    }
    for (const std::vector<cFrameCache::Held> *held : {&cache.mHeld, &cache.mHeldLast}) {
        VectorShape(o, *held);
        for (const cFrameCache::Held &h : *held) {
            o.push_back(NodeIndex(h.mObject));
            o.push_back(h.mWord);
        }
    }
    for (const FakeNode &node : gNodes) o.push_back(node.refs);
}

void FreeCache()
{
    Free(gCache.mBlobsA);
    Free(gCache.mBlobsB);
    Free(gCache.mHeld);
    Free(gCache.mHeldLast);
}

// The light vectors set aside (Capture expects them empty) and back.
struct LightVectors {
    std::vector<LightRecord> records;
    std::vector<LightRecord *> current;
    std::vector<AmbientRecord> ambients;
    std::vector<char> bytes;
    void Swap(cDisplayList *self)
    {
        records.swap(self->mRecords58);
        current.swap(self->mWords68);
        ambients.swap(self->mRecords78);
        bytes.swap(self->mBytesA4);
    }
};

// While alive, this thread's process-heap frees through our own HeapFree import are held and done when it ends, so no
// address is handed out twice within a side's case. Both sides' vectors free there: ours through our CRT, the
// original's through the test's GameDelete stand-in (sg_display_list_world.h). Without it the heap may give a new block
// an old block's address on one side only, and the comparisons that identify blocks by address (LightPointers' block
// numbers and its items, "the block moved") then differ between the sides though the code did the same (TY, backlog
// 2026-11-02: subsets seed 312). One alive at a time; past kMax held frees, frees go through and Overflowed() is set
// for the rest of the run (the tests fail on it).
class HeldFrees {
public:
    HeldFrees()
    {
        owner_ = GetCurrentThreadId();
        count_ = 0;
        previous_ = reinterpret_cast<FreeFn>(difftest::SwapOwnImport("HeapFree", reinterpret_cast<void *>(&Hold)));
    }
    ~HeldFrees()
    {
        difftest::SwapOwnImport("HeapFree", reinterpret_cast<void *>(previous_));
        for (unsigned i = 0; i < count_; ++i) previous_(held_[i].heap, held_[i].flags, held_[i].p);
        count_ = 0;
    }
    HeldFrees(const HeldFrees &) = delete;
    HeldFrees &operator=(const HeldFrees &) = delete;
    static bool Overflowed() { return overflowed_; }

private:
    using FreeFn = BOOL(WINAPI *)(HANDLE, DWORD, LPVOID);
    struct Held {
        HANDLE heap;
        DWORD flags;
        LPVOID p;
    };
    static constexpr unsigned kMax = 1u << 14;
    static BOOL WINAPI Hold(HANDLE heap, DWORD flags, LPVOID p)
    {
        if (!p || GetCurrentThreadId() != owner_ || count_ == kMax) {
            if (p && GetCurrentThreadId() == owner_) overflowed_ = true;
            return previous_(heap, flags, p);
        }
        held_[count_++] = {heap, flags, p};
        return TRUE;
    }
    static inline FreeFn previous_ = nullptr;
    static inline DWORD owner_ = 0;
    static inline unsigned count_ = 0;
    static inline bool overflowed_ = false;
    static inline Held held_[kMax];
};

// A light block pointer (an item's mWord3c, a word in the bytes): into the bytes as they are now (0x0a), into a block
// they had before (0x0b, with the block's number: items past the sorted count keep pointing into old blocks), into the
// light records (0x05), or as it is. Note before each call.
//
// An item's block (Item) the call left as it was keeps the record it had when noted: the heap may hand a new block the
// address of an old one on one side only, and a stale pointer into the old block would then read as into the new one.
struct LightPointers {
    std::vector<std::pair<uint32_t, uint32_t>> blocks; // the blocks seen, first address and capacity
    std::vector<std::tuple<const DrawItem *, uint32_t, uint32_t>> items; // an item, its block word and that's record
    void Note(const cDisplayList *self, const DrawItem *extra = nullptr)
    {
        difftest::NotCounted bookkeeping;
        const std::pair<uint32_t, uint32_t> block{svcfx::P(self->mBytesA4.data()),
                                                  static_cast<uint32_t>(self->mBytesA4.capacity())};
        if (block.second && (blocks.empty() || blocks.back() != block)) blocks.push_back(block);
        std::vector<std::tuple<const DrawItem *, uint32_t, uint32_t>> noted;
        for (const DrawItem &item : self->mItems) noted.emplace_back(&item, item.mWord3c, Item(self, item));
        if (extra) noted.emplace_back(extra, extra->mWord3c, Item(self, *extra));
        items.swap(noted);
    }
    uint32_t Item(const cDisplayList *self, const DrawItem &item) const
    {
        for (const auto &[at, word, record] : items)
            if (at == &item && word == item.mWord3c) return record;
        // A small value (no block's) the call moved with the bytes reads as Norm's 0x0c/0x0d without a stale block's
        // address in the way: the new block's address plus the value can fall inside an old block on one side only
        // (VH: grow light bytes seeds 1432 and 772, 0x0c00400e against 0x0b000006 and 0x0b00002b against 0x0c004003).
        // The value was noted small whatever block the bytes had then: as itself with no block, as Norm's 0x0d with one
        // (Before() is that block, so 0x0d keeps the value; 261111a: seed 3649 noted 0x4024 as 0x0d004024 beside block 0d1907e8; the original's new block 0d18c7e8 put
        // the moved 0d19080c inside it, 0x0b000024 against ours 0x0c004024).
        const uint32_t now = svcfx::P(self->mBytesA4.data());
        for (const auto &[at, word, record] : items) {
            const bool notedSmall = record == word || record == (0x0d000000u | word);
            const bool smallNoted = at == &item && notedSmall && word - 0x4000u < 0x40u && self->mBytesA4.capacity();
            if (smallNoted && item.mWord3c - now == word) return 0x0c000000u | word;
            if (smallNoted && item.mWord3c - now + Before() == word) return 0x0d000000u | word;
        }
        return Norm(self, item.mWord3c);
    }
    uint32_t Before() const { return blocks.empty() ? 0 : blocks.back().first; }
    uint32_t Norm(const cDisplayList *self, uint32_t p) const
    {
        if (!p) return 0;
        const uint32_t now = svcfx::P(self->mBytesA4.data());
        const uint32_t nowSize = static_cast<uint32_t>(self->mBytesA4.capacity());
        if (nowSize && p >= now && p <= now + nowSize) return 0x0a000000u | (p - now);
        for (size_t i = blocks.size(); i-- > 0;) {
            const uint32_t base = blocks[i].first;
            if (p >= base && p <= base + blocks[i].second)
                return 0x0b000000u | static_cast<uint32_t>(i) << 16 | (p - base);
        }
        const uint32_t records = svcfx::P(self->mRecords58.data());
        const uint32_t recordBytes = static_cast<uint32_t>(self->mRecords58.size() * sizeof(LightRecord));
        if (recordBytes && p >= records && p < records + recordBytes) return 0x05000000u | (p - records);
        // A small value (an item's block that was none of the bytes') moved with the bytes: from no block at all, or
        // from the block before.
        if (nowSize && p - now - 0x4000u < 0x40u) return 0x0c000000u | (p - now);
        if (nowSize && p - now + Before() - 0x4000u < 0x40u) return 0x0d000000u | (p - now + Before());
        return p;
    }
};

// The light state: the bytes (shape, the words in use with pointers normalised), every item's light block, the records'
// current list as indexes, the cache.
void CaptureLights(Blob &o, const LightPointers &pointers)
{
    const cDisplayList *const self = Self();
    o.push_back(0x119b);
    VectorShape(o, self->mBytesA4);
    const uint32_t used = self->mWordA0 < self->mBytesA4.size() ? self->mWordA0 : 0;
    for (uint32_t off = 0; off + 4 <= used; off += 4) {
        uint32_t word;
        std::memcpy(&word, self->mBytesA4.data() + off, 4);
        o.push_back(pointers.Norm(self, word));
    }
    for (const DrawItem &item : self->mItems) o.push_back(pointers.Item(self, item));
    VectorShape(o, self->mRecords58);
    VectorShape(o, self->mWords68);
    VectorShape(o, self->mRecords78);
    for (const LightRecord *record : self->mWords68) o.push_back(pointers.Norm(self, svcfx::P(record)));
    CaptureCache(o);
}

void FreeLights()
{
    cDisplayList *const self = Self();
    Free(self->mRecords58);
    Free(self->mWords68);
    Free(self->mRecords78);
    Free(self->mBytesA4);
    FreeCache();
}

} // namespace
