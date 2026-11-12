// Differential tests of the dynamic render strategy's frame dirty rectangles (lane 261108p, scenegraph row TS1f;
// src/sg/cDisplayListDynamicRenderStrategy.h). Raw blocks and hand-made vtables, as sg_display_list_dynamic_paths.cpp.
//
//   SG_DynamicTableUpdate  the frame state table's Update 0x372430 (Collect 0x36fc40, Push 0x3703f0, PushVanished
//       0x370980 and DrawItemPixelRect 0x372270 inside it) on our table, over several frames per seed (the table's
//       FrameStep / Reset, ours and verified, between them: the next frame, the same one, a jump, a reset). Each frame:
//       a display list slot count (the owner's slot 41: a few, or more than the 1000 records), up to 24 items over a
//       small pool of record indices (so last frame's records stand for unchanged items), random sort keys, the item
//       byte +0x16 (changed, whole pass, other bits), state word 2's bits (node record or geometry subset bounds), three
//       fake geometries (slot 5) and node records and placements, a pixel rectangle, gather / useRecords as bytes with
//       random upper bytes, clear mode 0..2 (Prepare sets no other: a fourth would store the game's stack garbage in the
//       first record's side), mDebug any byte. Noted after each call: the answer (mDirtyRects or not), the table
//       (sides' bits, rectangles, every record, both bitsets with sizes and capacities, the frame, both area sums, the
//       bytes at +0x4c), and the projection's calls; at the end the heap.
//   SG_DynamicAddRect  AddRect 0x370470 on a list (some elements to start with) and sequences of rectangles (ordered,
//       inverted, of no area, huge), with mMergeThreshold 0, small, large, 0x80000000 (the sentinel write) or random;
//       a third of the seeds on a small grid with repeated start elements, so that scores tie (the first best wins).
//       Noted after each call: the list and the sentinel's value storage; at the end the heap.
//   SG_DynamicHelperCollect  the helper's Collect 0x372480 (CollectWindows 0x3702c0, Add 0x370430, Window 0x370cd0,
//       Record 0x375a20, the callback 0x370e40, WindowScreenRect 0x372600, WindowId 0x3726f0, WindowOpaque 0x3738b0 and
//       the window property read 0x372670, ours and verified, inside it) on a tree of fake windows over several frames
//       (the helper's FrameStep / Reset, ours, between): rectangles, parent offsets, ids (some shared), flags (any
//       byte answered for a set one), fill colours, the property 0x300d422 (present, absent, failed with a variant
//       left), walks that refuse now and then; mFlagC (renderOpaqueUI) and mDebug any byte. Noted after each call: the
//       answer, the helper's words, its rectangles, both maps (entries in order; the value's pad bytes masked: the game
//       stores stack bytes there), the fakes' calls; at the end the fakes' counts and the heap.
//   SG_DynamicUpdateDirtyRects  UpdateDirtyRects 0x3706a0 (hooked; through its adapter) on a strategy-shaped block
//       (random bytes, the lists, helper, frame state and censor set by the test) with the two worlds above, the
//       frame state's mode and dirty-rect frames, mDirtyRects, renderOpaqueUI, the merge threshold, the pixel rectangle
//       and the clear mode changing between calls; the censor's rectangles (0x377f50, the original's on both sides)
//       from a censor-shaped block's boxes and a fake viewer's frame state (slot 116, counted). Noted after each call:
//       both lists (with their sentinels), the block's other words, the table, the helper, the fakes' calls.
//
// Stood in on both sides: the box projection 0xaf22d0 (nGZSceneGraph::ProjectBox, a StandIn; it notes which box and the matrix's words, and
// answers the box's corners), the game's operator new / delete (0x824632 / 0x824624) and RZMemory's Malloc / Free
// (the helper's maps), to ours, so both sides' blocks are counted.
#include "code_patch.h"
#include "difftest.h"
#include "heap_poison.h"

#include "compgeom/CompGeom.h"
#include "gz/cGZWinMgrW32.h"
#include "rz/RZMemory.h"
#include "sg/SGUtilities.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <list>
#include <new>
#include <random>
#include <vector>

using nTSSG::DynamicRectRecord;
using nTSSG::DynamicStrategyHelper;
using nTSSG::DynamicStrategyState;
using nTSSG::DynamicStrategyTable;
using nTSSG::DynamicWords16;
using Self = nTSSG::cDisplayListDynamicRenderStrategy;
using A = nTSSG::cDisplayListDynamicRenderStrategyAdapters;

namespace {

using Log = std::vector<uint32_t>;
Log *gLog = nullptr;
void Note(std::initializer_list<uint32_t> words) { gLog->insert(gLog->end(), words); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl StandMalloc(uint32_t n) { return ::operator new(n); }
void __cdecl StandFree(void *p) { ::operator delete(p); }

constexpr uint8_t kPoison = 0xc5;
constexpr uint32_t kFrameworkCell = 0x10c9378, kWinMgrId = 0xa417445e, kOpaqueUIWindowId = 0x22ba0121;
constexpr uint32_t kProperty = 0x300d422;
constexpr uint32_t kOffMatrix3 = 0x204, kOffViewer = 0xc, kPassSize = 0x350;

bool CompareLogs(difftest::Context &ctx, const char *what, uint32_t seed, const Log &original, const Log &ours)
{
    if (original == ours) return true;
    size_t at = 0;
    while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
    ctx.Fail("%s seed %u: logs differ at word %u of %u / %u: original 0x%08x, ours 0x%08x", what, seed,
             static_cast<unsigned>(at), static_cast<unsigned>(original.size()), static_cast<unsigned>(ours.size()),
             at < original.size() ? original[at] : 0u, at < ours.size() ? ours[at] : 0u);
    return false;
}

void FillRandom(std::mt19937 &rng, void *p, size_t n)
{
    auto *const b = static_cast<uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
}

template <typename T> void Put(void *block, uint32_t at, T value)
{
    std::memcpy(static_cast<uint8_t *>(block) + at, &value, sizeof value);
}

uint32_t W(const void *p, uint32_t at)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + at, 4);
    return w;
}

// A byte argument as the callers push it: 0 or 1, now and then another low byte or random upper bytes.
uint32_t ByteArg(std::mt19937 &rng)
{
    uint32_t v = rng() % 2;
    if (rng() % 6 == 0) v = rng() & 0xff;
    if (rng() % 4 == 0) v |= rng() & 0xffffff00u;
    return v;
}

uint8_t FlagByte(std::mt19937 &rng)
{
    const uint32_t k = rng() % 8;
    return k < 3 ? 0 : k < 7 ? 1 : static_cast<uint8_t>(rng());
}

const int32_t kOddRects[][4] = {{0, 0, 640, 480},  {10, 20, 300, 200}, {0, 0, 1, 1},       {100, 100, 50, 50},
                                {5, 5, 5, 300},    {-100, -50, 100, 50}, {320, 0, 640, 480},
                                {INT_MIN, INT_MIN, INT_MAX, INT_MAX},    {0, 0, 0, 0},
                                {2147483600, 0, -2147483600, 480},       {-2000000000, -5, 2000000000, 7}};

int32_t Coordinate(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return -static_cast<int32_t>(rng() % 50);
    case 1: return static_cast<int32_t>(rng() % 700);
    default: return static_cast<int32_t>(rng() % 200);
    }
}

// A rectangle: ordered most times, else inverted, of no area on one axis, or one of the odd ones.
void RandomRect(std::mt19937 &rng, int32_t *r)
{
    if (rng() % 16 == 0) {
        std::memcpy(r, kOddRects[rng() % std::size(kOddRects)], 16);
        return;
    }
    for (int i = 0; i < 4; ++i) r[i] = Coordinate(rng);
    const uint32_t kind = rng() % 8;
    if (kind < 6) {
        if (r[0] > r[2]) std::swap(r[0], r[2]);
        if (r[1] > r[3]) std::swap(r[1], r[3]);
    }
    if (kind == 6) r[2] = r[0];
}

DynamicWords16 AsWords(const int32_t *r)
{
    DynamicWords16 w;
    std::memcpy(w.w, r, 16);
    return w;
}

void NoteWords16(Log &log, const std::vector<DynamicWords16> &v)
{
    log.push_back(static_cast<uint32_t>(v.size()));
    log.push_back(static_cast<uint32_t>(v.capacity()));
    for (const DynamicWords16 &w : v) log.insert(log.end(), w.w, w.w + 4);
}

// MSVC's list node, for the sentinel's value storage (allocated with the list, never constructed).
struct RawNode {
    RawNode *mNext;
    RawNode *mPrev;
    uint32_t mValue[4];
};

void NoteList(Log &log, const std::list<DynamicWords16> &list)
{
    log.push_back(0x1157);
    log.push_back(static_cast<uint32_t>(list.size()));
    for (const DynamicWords16 &w : list) log.insert(log.end(), w.w, w.w + 4);
    const RawNode *const head = *reinterpret_cast<const RawNode *const *>(&list);
    log.insert(log.end(), head->mValue, head->mValue + 4);
}

// A game cell set for the test and restored after it.
struct CellWord {
    uint32_t *mCell;
    uint32_t mSaved;
    explicit CellWord(uint32_t rva) : mCell(GameAddress<uint32_t>(rva)), mSaved(*mCell) {}
    ~CellWord() { *mCell = mSaved; }
};

// ---- the scene: the pass, its owner, geometries, node records, placements and items ----

constexpr unsigned kGeoms = 3, kSubsets = 4, kNodeRecords = 3, kPlacements = 3, kMaxItems = 24;
constexpr uint32_t kSubsetSize = 0x40, kSubsetBounds = 0x1c, kNodeRecordSize = 0x5c, kNodeBounds = 0x44,
                   kNodePlacement = 0x10;

struct FakeObject {
    void **mVTable;
    unsigned mIndex;
    uint32_t mRefs;
};

struct Scene {
    FakeObject owner, geoms[kGeoms];
    alignas(16) uint8_t subsets[kGeoms][kSubsets][kSubsetSize];
    alignas(16) uint8_t records[kNodeRecords][kNodeRecordSize];
    AffineTransform3 placements[kPlacements];
    alignas(16) uint8_t pass[kPassSize];
    alignas(16) uint8_t items[kMaxItems][sizeof(nGZSceneGraph::DrawItem)];
    uint32_t slots;
    unsigned count;
    int32_t pixelRect[4];
};
Scene gs;
void *gOwnerVt[48], *gGeomVt[8];

nGZSceneGraph::DrawItem *Item(unsigned i) { return reinterpret_cast<nGZSceneGraph::DrawItem *>(gs.items[i]); }
nGZSceneGraph::Pass *ScenePass() { return reinterpret_cast<nGZSceneGraph::Pass *>(gs.pass); }

uint32_t __fastcall OwnerSlotCount(FakeObject *, void *)
{
    Note({0x41});
    return gs.slots;
}

uint32_t *__fastcall GeomDrawInfo(FakeObject *self, void *, uint32_t what)
{
    Note({0x6e05, self->mIndex, what});
    return reinterpret_cast<uint32_t *>(gs.subsets[self->mIndex][what % kSubsets]);
}

// 0xaf22d0 stood in: notes which box and the matrix's words, answers the box's corners.
void __cdecl StandProject(float *lo, float *hi, const nGZCompGeom::BoundBox *box, const float *matrix)
{
    uint32_t tag = 0xdead;
    const auto *const b = reinterpret_cast<const uint8_t *>(box);
    for (unsigned g = 0; g < kGeoms; ++g)
        for (unsigned s = 0; s < kSubsets; ++s)
            if (b == gs.subsets[g][s] + kSubsetBounds) tag = 0x100 + g * 16 + s;
    for (unsigned r = 0; r < kNodeRecords; ++r)
        if (b == gs.records[r] + kNodeBounds) tag = 0x200 + r;
    gLog->push_back(0x9a0);
    gLog->push_back(tag);
    for (int i = 0; i < 16; ++i) gLog->push_back(std::bit_cast<uint32_t>(matrix[i]));
    lo[0] = box->min.x, lo[1] = box->min.y, lo[2] = box->min.z;
    hi[0] = box->max.x, hi[1] = box->max.y, hi[2] = box->max.z;
}

const float kNan = std::bit_cast<float>(0xffc00000u);
const float kInf = std::bit_cast<float>(0x7f800000u);
const float kOddFloats[] = {0.0f,  -0.0f,      0.5f,        -0.5f, 1.5f,  2.5f,  0.49999997f, 0.50000006f, 99.99999f,
                            1e10f, -1e10f,     kInf,        -kInf, kNan,  639.5f, 640.0f,     479.99997f,  -1.0f,
                            1e-30f, 4194304.5f, 8388608.5f, -4194305.5f};

float BoxCoordinate(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return kOddFloats[rng() % std::size(kOddFloats)];
    case 1: return -static_cast<float>(rng() % 60) - 0.25f * static_cast<float>(rng() % 4);
    default: return static_cast<float>(rng() % 400) + 0.25f * static_cast<float>(rng() % 4);
    }
}

void RandomBox(std::mt19937 &rng, uint8_t *at)
{
    float f[6];
    for (float &v : f) v = BoxCoordinate(rng);
    if (rng() % 6) {
        for (int i = 0; i < 3; ++i)
            if (f[i] > f[i + 3]) std::swap(f[i], f[i + 3]);
    }
    const uint32_t depth = rng() % 8;
    if (depth == 0) f[5] = -1.0f - static_cast<float>(rng() % 10); // behind the eye
    else if (depth == 1) f[5] = rng() % 2 ? kNan : -0.0f;
    std::memcpy(at, f, sizeof f);
}

float MatrixValue(std::mt19937 &rng) { return static_cast<float>(static_cast<int>(rng() % 9) - 4) * 0.5f; }

void RandomPlacement(std::mt19937 &rng, AffineTransform3 &p)
{
    p.mUnknown0 = rng() % 4 == 0 ? rng() : rng() & 0x1f;
    for (float &m : p.mMatrix) m = MatrixValue(rng);
    for (float &o : p.mOffset) o = static_cast<float>(rng() % 100) - 20.0f;
}

void BuildScene(std::mt19937 &rng)
{
    for (unsigned i = 0; i < 48; ++i) gOwnerVt[i] = nullptr;
    for (unsigned i = 0; i < 8; ++i) gGeomVt[i] = nullptr;
    gOwnerVt[41] = reinterpret_cast<void *>(&OwnerSlotCount);
    gGeomVt[5] = reinterpret_cast<void *>(&GeomDrawInfo);
    gs.owner = {gOwnerVt, 0, 0};
    for (unsigned g = 0; g < kGeoms; ++g) {
        gs.geoms[g] = {gGeomVt, g, 0};
        for (unsigned s = 0; s < kSubsets; ++s) {
            FillRandom(rng, gs.subsets[g][s], kSubsetSize);
            RandomBox(rng, gs.subsets[g][s] + kSubsetBounds);
        }
    }
    for (unsigned r = 0; r < kNodeRecords; ++r) {
        FillRandom(rng, gs.records[r], kNodeRecordSize);
        AffineTransform3 p;
        RandomPlacement(rng, p);
        std::memcpy(gs.records[r] + kNodePlacement, &p, sizeof p);
        RandomBox(rng, gs.records[r] + kNodeBounds);
    }
    for (AffineTransform3 &p : gs.placements) RandomPlacement(rng, p);
    FillRandom(rng, gs.pass, kPassSize);
    Put(gs.pass, 0, &gs.owner);
    for (uint32_t i = 0; i < 16; ++i) Put(gs.pass, kOffMatrix3 + 4 * i, MatrixValue(rng));
}

// A frame's items over a small pool of record indices (so records carry over), sometimes any index below the count.
void MakeItems(std::mt19937 &rng, uint32_t pool)
{
    const uint32_t k = rng() % 10;
    gs.slots = k == 0 ? 1000 + rng() % 100 : k == 1 ? 1 + rng() % 4 : 33 + rng() % 40;
    gs.count = rng() % 6 == 0 ? 0 : rng() % (kMaxItems + 1);
    for (unsigned i = 0; i < gs.count; ++i) {
        FillRandom(rng, gs.items[i], sizeof gs.items[i]);
        nGZSceneGraph::DrawItem *const item = Item(i);
        const uint32_t limit = (std::min)(gs.slots, pool);
        item->mUserData = rng() % 5 == 0 ? rng() % gs.slots : rng() % limit;
        const uint32_t key = rng() % 6;
        item->mSortKey = key == 0 ? static_cast<int32_t>(rng()) : static_cast<int32_t>(rng() % 7) - 3;
        uint32_t flags = rng() % 2;                     // changed
        if (rng() % 6 == 0) flags |= 4;                 // the whole pass
        if (rng() % 5 == 0) flags |= rng() & 0xfa;      // other bits
        item->mSubKey = (rng() % 3 == 0 ? rng() & 0xffff : rng() % kSubsets) | (flags << 16) | (rng() & 0xff000000u);
        item->mGeometry = &gs.geoms[rng() % kGeoms];
        item->mRecord = gs.records[rng() % kNodeRecords];
        item->mPlacement = &gs.placements[rng() % kPlacements];
        const uint32_t s = rng() % 4;
        item->mState[2] = s == 0 ? rng() : s == 1 ? (rng() % 2 ? 2u : 4u) : rng() & ~6u;
    }
    if (rng() % 4 == 0) std::memcpy(gs.pixelRect, kOddRects[rng() % 3], 16);
    else RandomRect(rng, gs.pixelRect);
}

void MutateScene(std::mt19937 &rng)
{
    for (unsigned g = 0; g < kGeoms; ++g)
        for (unsigned s = 0; s < kSubsets; ++s)
            if (rng() % 5 == 0) RandomBox(rng, gs.subsets[g][s] + kSubsetBounds);
    for (unsigned r = 0; r < kNodeRecords; ++r)
        if (rng() % 5 == 0) RandomBox(rng, gs.records[r] + kNodeBounds);
    if (rng() % 6 == 0) Put(gs.pass, kOffMatrix3 + 4 * (rng() % 16), MatrixValue(rng));
}

void NoteTable(Log &log, const DynamicStrategyTable &t)
{
    log.push_back(0x7ab1e);
    log.push_back(static_cast<uint32_t>(t.mDirtyTurbo.size()));
    uint32_t bits = 0;
    for (size_t i = 0; i < t.mDirtyTurbo.size(); ++i) {
        bits = bits << 1 | (t.mDirtyTurbo[i] ? 1u : 0u);
        if (i % 32 == 31) log.push_back(bits), bits = 0;
    }
    log.push_back(bits);
    NoteWords16(log, t.mDirtyRects);
    log.push_back(static_cast<uint32_t>(t.mRecords.size()));
    log.push_back(static_cast<uint32_t>(t.mRecords.capacity()));
    for (const DynamicRectRecord &r : t.mRecords) {
        uint32_t w[9];
        std::memcpy(w, &r, sizeof w);
        log.insert(log.end(), w, w + 9);
    }
    for (const std::vector<uint32_t> *v : {&t.mLastVisible, &t.mVisible}) {
        log.push_back(static_cast<uint32_t>(v->size()));
        log.push_back(static_cast<uint32_t>(v->capacity()));
        log.insert(log.end(), v->begin(), v->end());
    }
    log.push_back(t.mFrame);
    log.push_back(t.mRecordArea);
    log.push_back(t.mDirtyArea);
    log.push_back(W(&t, 0x4c));
}

// The table's next frame: the next one most times, else the same, a jump (FrameStep resets) or a reset.
void StepTable(std::mt19937 &rng, DynamicStrategyTable &table, uint32_t &frame)
{
    switch (rng() % 10) {
    case 0: break;
    case 1: frame += 2 + rng() % 3; break;
    case 2: table.Reset(); ++frame; break;
    default: ++frame; break;
    }
    table.FrameStep(frame);
}

// ---- SG_DynamicTableUpdate ----

using UpdateFn = void *(__thiscall *)(void *, void *, const void *, uint32_t, uint32_t, uint32_t, const int32_t *,
                                      uint32_t);

struct TableStands {
    shape::StandIn project{0xaf22d0u, &nGZSceneGraph::ProjectBox, &StandProject};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

Log TableSide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    log.reserve(1u << 16);
    gLog = &log;
    difftest::AllocationCounter counter;
    BuildScene(rng);
    auto *const table = new DynamicStrategyTable;
    table->mRecordArea = rng();
    table->mDirtyArea = rng();
    uint32_t frame = 1 + rng() % 3;
    table->FrameStep(frame);
    const uint32_t pool = 4 + rng() % 40;
    const uint32_t calls = 1 + rng() % 6;
    for (uint32_t c = 0; c < calls; ++c) {
        MakeItems(rng, pool);
        const uint32_t gather = rng() % 4 ? ByteArg(rng) | (rng() % 3 ? 1 : 0) : ByteArg(rng);
        const uint32_t clearMode = rng() % 3, useRecords = rng() % 4 ? 1 | (rng() % 4 ? 0 : rng() & ~0xffu) : ByteArg(rng);
        table->mDebug = FlagByte(rng);
        void *answer;
        if (original)
            answer = ctx.Original<UpdateFn>(0x372430)(table, gs.pass, gs.items, gs.count, gather, clearMode,
                                                      gs.pixelRect, useRecords);
        else
            answer = table->Update(ScenePass(), Item(0), gs.count, (gather & 0xff) != 0, clearMode, gs.pixelRect,
                                   (useRecords & 0xff) != 0);
        log.push_back(0xca11);
        log.push_back(answer == &table->mDirtyRects ? 1u : 0u);
        NoteTable(log, *table);
        MutateScene(rng);
        StepTable(rng, *table, frame);
    }
    delete table;
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
    gLog = nullptr;
    return log;
}

// ---- SG_DynamicAddRect ----

using AddRectFn = void(__thiscall *)(void *, void *, const int32_t *);

uint32_t Threshold(std::mt19937 &rng)
{
    switch (rng() % 7) {
    case 0: return 0;
    case 1: return rng() % 50;
    case 2: return 1000 + rng() % 20000;
    case 3: return 0x80000000u;
    case 4: return rng();
    case 5: return 0x7fffffffu;
    default: return rng() % 3000;
    }
}

Log AddRectSide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    log.reserve(1u << 12);
    difftest::AllocationCounter counter;
    alignas(16) uint8_t block[Self::kSize];
    FillRandom(rng, block, sizeof block);
    {
        // A small grid (coordinates 0..5) makes equal scores common, and repeated start elements tie exactly: the
        // first best is the one merged into or erased.
        const bool grid = rng() % 3 == 0;
        const auto rect = [&](int32_t *r) {
            if (!grid) {
                RandomRect(rng, r);
                return;
            }
            for (int i = 0; i < 4; ++i) r[i] = static_cast<int32_t>(rng() % 6);
            if (r[0] > r[2]) std::swap(r[0], r[2]);
            if (r[1] > r[3]) std::swap(r[1], r[3]);
            if (r[2] == r[0]) ++r[2];
            if (r[3] == r[1]) ++r[3];
        };
        std::list<DynamicWords16> list;
        const uint32_t start = grid ? 2 + rng() % 4 : rng() % 3 == 0 ? rng() % 5 : 0;
        for (uint32_t i = 0; i < start; ++i) {
            int32_t r[4];
            if (grid && i > 0 && rng() % 2 == 0) std::memcpy(r, list.back().w, 16);
            else rect(r);
            list.push_back(AsWords(r));
        }
        const uint32_t calls = 1 + rng() % 16;
        for (uint32_t c = 0; c < calls; ++c) {
            if (c == 0 || rng() % 4 == 0) Put(block, 0xc8, grid && rng() % 2 ? rng() % 8 : Threshold(rng));
            int32_t r[4];
            rect(r);
            if (original) ctx.Original<AddRectFn>(0x370470)(block, &list, r);
            else reinterpret_cast<Self *>(block)->AddRect(&list, r);
            log.push_back(0xca11);
            NoteList(log, list);
        }
    }
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
    return log;
}

// ---- the window world ----

constexpr unsigned kWindows = 12, kMaxChildren = 4;
enum PropertyMode : uint32_t { kNoProperty, kPropertyPresent, kFailedLeft };

struct WinWorld {
    FakeObject framework, manager, windows[kWindows], variants[kWindows];
    int parent[kWindows];
    unsigned children[kWindows][kMaxChildren], childCount[kWindows];
    int32_t rect[kWindows][4], x[kWindows], y[kWindows];
    uint32_t id[kWindows], flags[kWindows], colour[kWindows], value[kWindows];
    bool writesColour[kWindows], refuses[kWindows];
    PropertyMode mode[kWindows];
    uint32_t lcg;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
WinWorld gw;
void *gFrameworkVt[8], *gManagerVt[8], *gWindowVt[128], *gVariantVt[32];

uint32_t Garbage() { return gw.Next() & 0xffffff00u; }
unsigned WindowIndex(const FakeObject *w) { return static_cast<unsigned>(w - gw.windows); }

bool __fastcall FrameworkService(FakeObject *, void *, uint32_t id, uint32_t iid, void **out)
{
    Note({0xf5, id, iid});
    *out = &gw.manager;
    ++gw.manager.mRefs;
    return true;
}
uint32_t __fastcall ManagerRelease(FakeObject *self, void *)
{
    Note({0xa2});
    return --self->mRefs;
}
void *__fastcall ManagerMainWindow(FakeObject *, void *)
{
    Note({0xa3});
    return &gw.windows[0];
}

void *__fastcall WinParent(FakeObject *self, void *)
{
    const unsigned i = WindowIndex(self);
    Note({0x11, i});
    return gw.parent[i] < 0 ? nullptr : &gw.windows[gw.parent[i]];
}
uint32_t __fastcall WinChildCount(FakeObject *self, void *)
{
    const unsigned i = WindowIndex(self);
    Note({0x15, i});
    return gw.childCount[i];
}
using WalkCallback = uint8_t(__cdecl *)(void *, uint32_t, void *, void *);
uint32_t __fastcall WinWalk(FakeObject *self, void *, uint32_t filter, void *callback, void *data)
{
    const unsigned i = WindowIndex(self);
    Note({0x37, i, filter});
    const auto cb = reinterpret_cast<WalkCallback>(callback);
    for (unsigned k = 0; k < gw.childCount[i]; ++k) {
        const unsigned child = gw.children[i][k];
        const uint8_t r = cb(self, 0x78000000u | child, &gw.windows[child], data);
        Note({0x37a, child, r});
        if (!r) return Garbage();
    }
    // A tree of callbacks alone never answers false (a leaf answers true): the walk refuses by itself now and then.
    if (gw.refuses[i]) return Garbage();
    return 1 | Garbage();
}
int32_t __fastcall WinX(FakeObject *self, void *)
{
    const unsigned i = WindowIndex(self);
    Note({0x51, i});
    return gw.x[i];
}
int32_t __fastcall WinY(FakeObject *self, void *)
{
    const unsigned i = WindowIndex(self);
    Note({0x52, i});
    return gw.y[i];
}
uint32_t __fastcall WinRect(FakeObject *self, void *, int32_t *out)
{
    const unsigned i = WindowIndex(self);
    Note({0x55, i});
    std::memcpy(out, gw.rect[i], 16);
    return Garbage();
}
uint32_t __fastcall WinId(FakeObject *self, void *)
{
    const unsigned i = WindowIndex(self);
    Note({0x80, i});
    return gw.id[i];
}
// A set flag answers any non-zero byte now and then.
uint32_t __fastcall WinFlag(FakeObject *self, void *, uint32_t mask)
{
    const unsigned i = WindowIndex(self);
    Note({0x82, i, mask});
    const bool set = (gw.flags[i] & mask) != 0;
    const uint32_t byte = !set ? 0 : gw.Next() % 4 == 0 ? 1 + gw.Next() % 255 : 1;
    return byte | Garbage();
}
uint32_t __fastcall WinColour(FakeObject *self, void *, uint32_t *colour)
{
    const unsigned i = WindowIndex(self);
    Note({0x125, i, *colour});
    if (gw.writesColour[i]) *colour = gw.colour[i];
    return Garbage();
}
uint32_t __fastcall WinGetProperty(FakeObject *self, void *, uint32_t id, void **out)
{
    const unsigned i = WindowIndex(self);
    Note({0x126, i, id});
    const PropertyMode mode = id == kProperty ? gw.mode[i] : kNoProperty;
    if (mode == kNoProperty) return Garbage();
    *out = &gw.variants[i];
    ++gw.variants[i].mRefs;
    return (mode == kPropertyPresent ? 1u : 0u) | Garbage();
}
uint32_t __fastcall VariantRelease(FakeObject *self, void *)
{
    Note({0x2a, self->mIndex});
    return --self->mRefs;
}
uint32_t __fastcall VariantGet(FakeObject *self, void *, uint32_t *out)
{
    Note({0x26, self->mIndex});
    *out = gw.value[self->mIndex];
    return (gw.Next() % 2) | Garbage();
}

int32_t SmallOffset(std::mt19937 &rng) { return rng() % 8 == 0 ? static_cast<int32_t>(rng()) : static_cast<int32_t>(rng() % 60) - 10; }

uint32_t WindowFlags(std::mt19937 &rng)
{
    uint32_t f = rng() % 8 == 0 ? rng() : rng() & (4u | 0x20u | 0x80u | 0x100000u);
    if (rng() % 5) f |= 1; // visible
    return f;
}

void RandomWindow(std::mt19937 &rng, unsigned i)
{
    RandomRect(rng, gw.rect[i]);
    gw.x[i] = SmallOffset(rng);
    gw.y[i] = SmallOffset(rng);
    gw.flags[i] = WindowFlags(rng);
    gw.colour[i] = rng() % 3 == 0 ? rng() : (rng() % 2 ? 0xff000000u : 0x80000000u) | (rng() & 0xffffff);
    gw.writesColour[i] = rng() % 5 != 0;
    gw.value[i] = rng() % 5 == 0 ? rng() : rng() % 4;
    const uint32_t m = rng() % 6;
    gw.mode[i] = m < 3 ? kPropertyPresent : m < 5 ? kNoProperty : kFailedLeft;
    gw.refuses[i] = rng() % 10 == 0;
}

void BuildWindows(std::mt19937 &rng)
{
    for (void *&s : gFrameworkVt) s = nullptr;
    for (void *&s : gManagerVt) s = nullptr;
    for (void *&s : gWindowVt) s = nullptr;
    for (void *&s : gVariantVt) s = nullptr;
    gFrameworkVt[5] = reinterpret_cast<void *>(&FrameworkService);
    gManagerVt[2] = reinterpret_cast<void *>(&ManagerRelease);
    gManagerVt[3] = reinterpret_cast<void *>(&ManagerMainWindow);
    gWindowVt[11] = reinterpret_cast<void *>(&WinParent);
    gWindowVt[15] = reinterpret_cast<void *>(&WinChildCount);
    gWindowVt[37] = reinterpret_cast<void *>(&WinWalk);
    gWindowVt[51] = reinterpret_cast<void *>(&WinX);
    gWindowVt[52] = reinterpret_cast<void *>(&WinY);
    gWindowVt[55] = reinterpret_cast<void *>(&WinRect);
    gWindowVt[80] = reinterpret_cast<void *>(&WinId);
    gWindowVt[82] = reinterpret_cast<void *>(&WinFlag);
    gWindowVt[125] = reinterpret_cast<void *>(&WinColour);
    gWindowVt[126] = reinterpret_cast<void *>(&WinGetProperty);
    gVariantVt[2] = reinterpret_cast<void *>(&VariantRelease);
    gVariantVt[26] = reinterpret_cast<void *>(&VariantGet);
    gw.framework = {gFrameworkVt, 0, 0};
    gw.manager = {gManagerVt, 0, 0};
    gw.lcg = static_cast<uint32_t>(rng());
    const unsigned windows = 1 + rng() % kWindows;
    for (unsigned i = 0; i < kWindows; ++i) {
        gw.windows[i] = {gWindowVt, i, 0};
        gw.variants[i] = {gVariantVt, i, 0};
        gw.childCount[i] = 0;
        gw.parent[i] = -1;
        gw.id[i] = rng() % 6 == 0 ? rng() % 3 : 0x1000 + i; // some shared
        RandomWindow(rng, i);
    }
    for (unsigned i = 1; i < windows; ++i) {
        unsigned p = rng() % i;
        while (gw.childCount[p] == kMaxChildren) p = (p + 1) % i;
        gw.parent[i] = static_cast<int>(p);
        gw.children[p][gw.childCount[p]++] = i;
    }
}

void MutateWindows(std::mt19937 &rng)
{
    for (unsigned i = 0; i < kWindows; ++i) {
        const uint32_t k = rng() % 12;
        if (k == 0) RandomWindow(rng, i);
        else if (k == 1) gw.rect[i][rng() % 4] += static_cast<int32_t>(rng() % 9) - 4;
        else if (k == 2) gw.flags[i] = WindowFlags(rng);
        else if (k == 3) gw.value[i] = rng() % 4;
        else if (k == 4) gw.colour[i] ^= 0x7f000000u;
        else if (k == 5) gw.x[i] = SmallOffset(rng);
    }
}

void NoteMap(Log &log, const DynamicStrategyHelper::Map &map)
{
    log.push_back(0x3a9);
    log.push_back(static_cast<uint32_t>(map.size()));
    log.push_back(static_cast<uint32_t>(map.bucket_count()));
    for (const auto &e : map) {
        log.push_back(e.first);
        log.insert(log.end(), e.second.w, e.second.w + 5);
        log.push_back(e.second.w[5] & 0xffffu); // the pad bytes: the game's stack
    }
}

void NoteHelper(Log &log, const DynamicStrategyHelper &h)
{
    log.push_back(0x4e1);
    NoteWords16(log, h.mItems);
    for (uint32_t at : {0xcu, 0x18u, 0x1cu, 0x20u, 0x24u}) log.push_back(W(&h, at));
    NoteMap(log, *h.mMap10);
    NoteMap(log, *h.mMap14);
}

void NoteWindowCounts(Log &log)
{
    log.push_back(0xc0);
    log.push_back(gw.manager.mRefs);
    for (unsigned i = 0; i < kWindows; ++i) log.push_back(gw.variants[i].mRefs);
}

void StepHelper(std::mt19937 &rng, DynamicStrategyHelper &helper, uint32_t &frame)
{
    switch (rng() % 10) {
    case 0: break;
    case 1: frame += 2 + rng() % 3; break;
    case 2: helper.Reset(); ++frame; break;
    default: ++frame; break;
    }
    helper.FrameStep(frame);
}

struct WindowStands {
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &StandMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &StandFree};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// ---- SG_DynamicHelperCollect ----

using CollectFn = void *(__thiscall *)(void *);

Log HelperSide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    log.reserve(1u << 14);
    gLog = &log;
    difftest::AllocationCounter counter;
    BuildWindows(rng);
    *GameAddress<void *>(kFrameworkCell) = &gw.framework;
    auto *const helper = new DynamicStrategyHelper;
    helper->mRecordedArea = rng();
    helper->mAddedArea = rng();
    uint32_t frame = 1 + rng() % 3;
    helper->FrameStep(frame);
    const uint32_t calls = 1 + rng() % 6;
    for (uint32_t c = 0; c < calls; ++c) {
        helper->mFlagC = FlagByte(rng);
        helper->mDebug = FlagByte(rng);
        void *answer;
        if (original) answer = ctx.Original<CollectFn>(0x372480)(helper);
        else answer = helper->Collect();
        log.push_back(0xca11);
        log.push_back(answer == &helper->mItems ? 1u : 0u);
        NoteHelper(log, *helper);
        NoteWindowCounts(log);
        MutateWindows(rng);
        StepHelper(rng, *helper, frame);
    }
    delete helper;
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
    gLog = nullptr;
    return log;
}

// ---- SG_DynamicUpdateDirtyRects ----

using UpdateDirtyRectsFn = void(__thiscall *)(void *, void *, const void *, uint32_t, uint32_t);

// The censor strategy's drawn boxes (0x30 bytes: the rectangle at +0x18) in a censor-shaped block's vector at +0x18,
// and the frame state the viewer's slot 116 hands out (counted; its rectangles at +0x54).
struct Box48 {
    uint32_t w[12];
};
constexpr uint32_t kCensorSize = 0x40, kCensorBoxes = 0x18, kStateRects = 0x54, kFakeStateSize = 0x64;
struct CensorWorld {
    FakeObject viewer;
    uint8_t censor[kCensorSize];
    uint8_t state[kFakeStateSize];
    uint32_t stateRefs;
};
CensorWorld gc;
void *gViewerVt[120], *gFakeStateVt[4];

void *__fastcall ViewerState(FakeObject *, void *)
{
    Note({0x116});
    return gc.state;
}
uint32_t __fastcall FakeStateAddRef(void *, void *)
{
    Note({0x5a1});
    return ++gc.stateRefs;
}
uint32_t __fastcall FakeStateRelease(void *, void *)
{
    Note({0x5a2});
    return --gc.stateRefs;
}

std::vector<Box48> &CensorBoxes() { return *reinterpret_cast<std::vector<Box48> *>(gc.censor + kCensorBoxes); }
std::vector<DynamicWords16> &StateRects() { return *reinterpret_cast<std::vector<DynamicWords16> *>(gc.state + kStateRects); }

void FillCensor(std::mt19937 &rng)
{
    std::vector<Box48> &boxes = CensorBoxes();
    boxes.clear();
    for (uint32_t n = rng() % 3 == 0 ? 0 : rng() % 5; n; --n) {
        Box48 b;
        FillRandom(rng, &b, sizeof b);
        int32_t r[4];
        RandomRect(rng, r);
        std::memcpy(&b.w[6], r, 16);
        boxes.push_back(b);
    }
    std::vector<DynamicWords16> &rects = StateRects();
    rects.clear();
    for (uint32_t n = rng() % 3 == 0 ? 0 : rng() % 4; n; --n) {
        int32_t r[4];
        RandomRect(rng, r);
        rects.push_back(AsWords(r));
    }
}

void BuildCensor(std::mt19937 &rng)
{
    for (void *&s : gViewerVt) s = nullptr;
    gViewerVt[116] = reinterpret_cast<void *>(&ViewerState);
    gFakeStateVt[0] = nullptr;
    gFakeStateVt[1] = reinterpret_cast<void *>(&FakeStateAddRef);
    gFakeStateVt[2] = reinterpret_cast<void *>(&FakeStateRelease);
    gFakeStateVt[3] = nullptr;
    gc.viewer = {gViewerVt, 0, 0};
    gc.stateRefs = 1;
    FillRandom(rng, gc.censor, kCensorSize);
    FillRandom(rng, gc.state, kFakeStateSize);
    new (gc.censor + kCensorBoxes) std::vector<Box48>();
    new (gc.state + kStateRects) std::vector<DynamicWords16>();
    Put(gc.state, 0, gFakeStateVt);
    FillCensor(rng);
    Put(gs.pass, kOffViewer, &gc.viewer);
}

void FreeCensor()
{
    using Boxes = std::vector<Box48>;
    using Rects = std::vector<DynamicWords16>;
    CensorBoxes().~Boxes();
    StateRects().~Rects();
}

// The block's words the test owns (pointers that differ between the sides, the lists) are left out of its notes.
constexpr uint32_t kOffCensor = 0xc, kOffHelper = 0x18, kOffRects = 0x48, kOffTurboRects = 0x50, kOffState = 0xd4;
constexpr uint32_t kOffRenderOpaqueUI = 0xc4, kOffMergeThreshold = 0xc8, kOffDirtyRects = 0xce, kOffPixelRect = 0xdc,
                   kOffClearMode = 0x144;

void NoteBlock(Log &log, const uint8_t *block)
{
    log.push_back(0xb10c);
    for (uint32_t at = 0; at + 4 <= Self::kSize; at += 4) {
        if (at == kOffCensor || at == kOffHelper || at == kOffState || (at >= kOffRects && at < kOffTurboRects + 8))
            continue;
        log.push_back(W(block, at));
    }
}

Log DirtySide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    log.reserve(1u << 16);
    gLog = &log;
    difftest::AllocationCounter counter;
    BuildScene(rng);
    BuildWindows(rng);
    BuildCensor(rng);
    *GameAddress<void *>(kFrameworkCell) = &gw.framework;
    alignas(16) uint8_t block[Self::kSize];
    FillRandom(rng, block, sizeof block);
    auto *const rects = new (block + kOffRects) std::list<DynamicWords16>();
    auto *const turboRects = new (block + kOffTurboRects) std::list<DynamicWords16>();
    auto *const helper = new DynamicStrategyHelper;
    helper->mRecordedArea = rng();
    helper->mAddedArea = rng();
    auto *const state = new DynamicStrategyState;
    state->mTable->mRecordArea = rng();
    state->mTable->mDirtyArea = rng();
    Put(block, kOffCensor, gc.censor);
    Put(block, kOffHelper, helper);
    Put(block, kOffState, state);
    uint32_t tableFrame = 1 + rng() % 3, helperFrame = 1 + rng() % 3;
    state->mTable->FrameStep(tableFrame);
    helper->FrameStep(helperFrame);
    const uint32_t pool = 4 + rng() % 40;
    const uint32_t calls = 1 + rng() % 6;
    for (uint32_t c = 0; c < calls; ++c) {
        MakeItems(rng, pool);
        state->mMode = rng() % 4 ? 1 : rng() % 6 == 0 ? rng() : rng() % 3;
        state->mFrames = rng() % 5 == 0 ? rng() % 2 : 2 + rng() % 3;
        state->mTable->mDebug = FlagByte(rng);
        helper->mDebug = FlagByte(rng);
        block[kOffDirtyRects] = FlagByte(rng);
        block[kOffRenderOpaqueUI] = FlagByte(rng);
        if (c == 0 || rng() % 3 == 0) Put(block, kOffMergeThreshold, Threshold(rng));
        int32_t pixels[4];
        if (rng() % 3) {
            pixels[0] = pixels[1] = 0;
            pixels[2] = static_cast<int32_t>(rng() % 500);
            pixels[3] = static_cast<int32_t>(rng() % 500);
        } else {
            RandomRect(rng, pixels);
        }
        std::memcpy(block + kOffPixelRect, pixels, 16);
        Put<uint32_t>(block, kOffClearMode, rng() % 3); // Prepare sets 0..2 only (see the table test)
        if (rng() % 3 == 0) rects->clear();
        if (rng() % 3 == 0) turboRects->clear();
        if (rng() % 3 == 0) FillCensor(rng);
        const uint32_t useRecords = rng() % 4 ? 1 | (rng() % 4 ? 0 : rng() & ~0xffu) : ByteArg(rng);
        if (original)
            ctx.Original<UpdateDirtyRectsFn>(0x3706a0)(block, gs.pass, gs.items, gs.count, useRecords);
        else
            A::UpdateDirtyRects(reinterpret_cast<Self *>(block), nullptr, ScenePass(), Item(0), gs.count, useRecords);
        log.push_back(0xca11);
        NoteList(log, *rects);
        NoteList(log, *turboRects);
        NoteBlock(log, block);
        NoteTable(log, *state->mTable);
        NoteHelper(log, *helper);
        NoteWindowCounts(log);
        log.push_back(gc.stateRefs);
        MutateScene(rng);
        MutateWindows(rng);
        StepTable(rng, *state->mTable, tableFrame);
        StepHelper(rng, *helper, helperFrame);
    }
    delete state;
    delete helper;
    using List = std::list<DynamicWords16>;
    rects->~List();
    turboRects->~List();
    FreeCensor();
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
    gLog = nullptr;
    return log;
}

} // namespace

// Verifies: the table's Update 0x372430, Collect 0x36fc40, Push 0x3703f0, PushVanished 0x370980, DrawItemPixelRect
// 0x372270.
DIFF_TEST(SG_DynamicTableUpdate, 0x372430)
{
    heappoison::Scope poison(ctx, kPoison);
    TableStands stands;
    (void)TableSide(ctx, 1, true); // warm-up: the original's first run
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        const Log original = TableSide(ctx, seed, true);
        const Log ours = TableSide(ctx, seed, false);
        if (!CompareLogs(ctx, "table update", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: AddRect 0x370470.
DIFF_TEST(SG_DynamicAddRect, 0x370470)
{
    heappoison::Scope poison(ctx, kPoison);
    TableStands stands;
    (void)AddRectSide(ctx, 1, true);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        const Log original = AddRectSide(ctx, seed, true);
        const Log ours = AddRectSide(ctx, seed, false);
        if (!CompareLogs(ctx, "add rect", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: the helper's Collect 0x372480, CollectWindows 0x3702c0, Add 0x370430, Window 0x370cd0, Record 0x375a20,
// WindowCallback 0x370e40, WindowScreenRect 0x372600, WindowId 0x3726f0, WindowOpaque 0x3738b0.
DIFF_TEST(SG_DynamicHelperCollect, 0x372480)
{
    heappoison::Scope poison(ctx, kPoison);
    WindowStands stands;
    CellWord framework(kFrameworkCell);
    (void)HelperSide(ctx, 1, true);
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        const Log original = HelperSide(ctx, seed, true);
        const Log ours = HelperSide(ctx, seed, false);
        if (!CompareLogs(ctx, "helper collect", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: UpdateDirtyRects 0x3706a0.
DIFF_TEST(SG_DynamicUpdateDirtyRects, 0x3706a0)
{
    heappoison::Scope poison(ctx, kPoison);
    WindowStands stands;
    shape::StandIn project{0xaf22d0u, &nGZSceneGraph::ProjectBox, &StandProject};
    CellWord framework(kFrameworkCell);
    (void)DirtySide(ctx, 1, true);
    for (uint32_t seed = 1; seed <= 1000; ++seed) {
        const Log original = DirtySide(ctx, seed, true);
        const Log ours = DirtySide(ctx, seed, false);
        if (!CompareLogs(ctx, "update dirty rects", seed, original, ours)) return;
        ++ctx.cases;
    }
}
