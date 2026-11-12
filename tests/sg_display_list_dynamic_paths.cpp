// Differential tests of the dynamic render strategy's per-frame helpers the drawing paths call (lane 261108o,
// scenegraph row TS1e; src/sg/cDisplayListDynamicRenderStrategy.h). Each runs on a strategy-shaped block (random bytes
// from the seed, alike on both sides, with the members the function reads constructed or set by the test); ours is
// called through its hook adapter, with the byte arguments as dwords whose upper bytes are random now and then.
//
//   SG_DynamicMarkRecords  MarkRecords 0x3724a0: per seed a block with the record vectors (+0x30's count, +0x3c's record
//       pointers, sometimes more of them than the count), records of random rectangles and flags (the drawn and side
//       bytes 0, 1 or another value), both bounds, dirtyRectsDisableIntervalTrees either way and the trees member (our
//       constructor, verified against 0x36e9c0), then several calls with random rectangle lists (empty to 30, some
//       inverted), turbo / test / which, the records' flags changed between calls. Noted after each call: the block's
//       words (the vectors' and trees' words skipped), every record, and the trees (node count, the node vector's size
//       and capacity, every node with links as numbers, both roots, both hit sets); at the end the heap. The rectangle
//       intersection 0x373520 is the game's on its side, ours (SG_GZRectIntersect) on ours; the trees' Build / Overlaps are the original's on its side.
//   SG_DynamicComputeBounds  ComputeBounds 0x370eb0 on random rectangle lists (empty, inverted, empty-area, over or
//       outside the pass's pixels), either side, with the start and empty rectangle cells (0x10091cc / 0x10091b4) set
//       to the usual extremes or random rectangles. Noted: the block's words.
//   SG_DynamicSetUpScissor  SetUpScissor 0x3719a0: useScissoring 0, 1 or another byte, dirtyRectsDebug 0 to 4 or random,
//       either side's bounds empty or not, pass sizes small, zero or above 2^31 (the unsigned conversion), and a fake
//       device state whose ValidateCommand (slot 29) answers from the seed (any other slot is a null entry). Noted: the
//       answer's low byte, the block's words, the fake's calls (the command as its offset in the block, the flags).
//
// The game's operator new / delete (0x824632 / 0x824624) go to ours on both sides, so the trees' blocks are counted.
#include "code_patch.h"
#include "difftest.h"
#include "heap_poison.h"

#include "sg/cDisplayList.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"
#include "sg/cIntervalTree.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <vector>

using nTSSG::cIntervalTree;
using nTSSG::DynamicRectRecord;
using nTSSG::DynamicStrategyTrees;
using nTSSG::DynamicWords16;
using Self = nTSSG::cDisplayListDynamicRenderStrategy;
using A = nTSSG::cDisplayListDynamicRenderStrategyAdapters;
using Node = cIntervalTree::Node;
using NodeSet = cIntervalTree::NodeSet;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct HeapPatches {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

using MarkFn = void(__thiscall *)(void *, const std::list<DynamicWords16> *, void *, uint32_t, uint32_t, uint32_t);
using BoundsFn = void(__thiscall *)(void *, const std::list<DynamicWords16> *, uint32_t);
using ScissorFn = bool(__thiscall *)(void *, void *, uint32_t);

constexpr uint8_t kPoison = 0xc5;
constexpr size_t kBlock = Self::kSize;
constexpr uint32_t kOffVectors = 0x30, kOffVectorsEnd = 0x48, kOffTrees = 0x98, kOffTreesEnd = 0xc0;
constexpr uint32_t kOffBounds = 0x58, kOffTurboBounds = 0x68, kOffUseScissoring = 0x78, kOffDebugMode = 0xc0;
constexpr uint32_t kOffDisableTrees = 0xc5, kOffPixelRect = 0xdc;
constexpr uint32_t kUnboundedRectCell = 0x10091cc, kEmptyRectCell = 0x10091b4;

using Log = std::vector<uint32_t>;

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

// A block's words, those in [skipFrom, skipTo) and [skip2From, skip2To) left out.
void NoteWords(Log &log, const uint8_t *block, size_t size, uint32_t skipFrom = 0, uint32_t skipTo = 0,
               uint32_t skip2From = 0, uint32_t skip2To = 0)
{
    log.push_back(0xb10c);
    for (uint32_t at = 0; at + 4 <= size; at += 4) {
        if ((at >= skipFrom && at < skipTo) || (at >= skip2From && at < skip2To)) continue;
        uint32_t w;
        std::memcpy(&w, block + at, 4);
        log.push_back(w);
    }
}

int32_t Coordinate(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return -static_cast<int32_t>(rng() % 50);
    case 1: return static_cast<int32_t>(rng() % 2000);
    default: return static_cast<int32_t>(rng() % 200);
    }
}

// A rectangle: ordered most times, else inverted or of no area on one axis.
void RandomRect(std::mt19937 &rng, int32_t *r)
{
    for (int i = 0; i < 4; ++i) r[i] = Coordinate(rng);
    const uint32_t kind = rng() % 8;
    if (kind < 6) {
        if (r[0] > r[2]) std::swap(r[0], r[2]);
        if (r[1] > r[3]) std::swap(r[1], r[3]);
    }
    if (kind == 6) r[2] = r[0];
}

std::list<DynamicWords16> RandomRects(std::mt19937 &rng, uint32_t most)
{
    std::list<DynamicWords16> rects;
    const uint32_t n = rng() % 4 == 0 ? 0 : rng() % (most + 1);
    for (uint32_t i = 0; i < n; ++i) {
        DynamicWords16 w;
        int32_t r[4];
        RandomRect(rng, r);
        std::memcpy(w.w, r, sizeof r);
        rects.push_back(w);
    }
    return rects;
}

// A byte argument as the callers push it: 0 or 1, now and then another low byte or random upper bytes.
uint32_t ByteArg(std::mt19937 &rng, bool zeroOrOne = false)
{
    uint32_t v = rng() % 2;
    if (!zeroOrOne && rng() % 6 == 0) v = rng() & 0xff;
    if (rng() % 4 == 0) v |= rng() & 0xffffff00u;
    return v;
}

uint8_t FlagByte(std::mt19937 &rng)
{
    const uint32_t k = rng() % 8;
    return k < 3 ? 0 : k < 7 ? 1 : static_cast<uint8_t>(rng());
}

void FillRandom(std::mt19937 &rng, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(rng());
}

template <typename T> void Put(uint8_t *block, uint32_t at, T value) { std::memcpy(block + at, &value, sizeof value); }

// A rectangle cell set for the test and restored after it.
struct CellRect {
    int32_t *mCell;
    int32_t mSaved[4];
    explicit CellRect(uint32_t rva) : mCell(GameAddress<int32_t>(rva)) { std::memcpy(mSaved, mCell, sizeof mSaved); }
    ~CellRect() { std::memcpy(mCell, mSaved, sizeof mSaved); }
    void Set(int32_t a, int32_t b, int32_t c, int32_t d)
    {
        const int32_t r[4] = {a, b, c, d};
        std::memcpy(mCell, r, sizeof r);
    }
};

// ---- SG_DynamicMarkRecords ----

uint32_t NodeNumber(const DynamicStrategyTrees &t, const Node *p)
{
    if (!p) return 0;
    for (size_t i = 0; i < t.mNodes.size(); ++i)
        if (&t.mNodes[i] == p) return static_cast<uint32_t>(i + 1);
    return 0xeeeeeeeeu;
}

void NoteTrees(Log &log, const DynamicStrategyTrees &t)
{
    log.push_back(0x7eee5);
    log.push_back(t.mNodeCount);
    log.push_back(static_cast<uint32_t>(t.mNodes.size()));
    log.push_back(static_cast<uint32_t>(t.mNodes.capacity()));
    for (const Node &n : t.mNodes) {
        log.push_back(NodeNumber(t, n.mParent));
        log.push_back(NodeNumber(t, n.mLeft));
        log.push_back(NodeNumber(t, n.mRight));
        log.push_back(static_cast<uint32_t>(n.mData));
        log.push_back(n.mColour);
        log.push_back(static_cast<uint32_t>(n.mLow));
        log.push_back(static_cast<uint32_t>(n.mHigh));
        log.push_back(static_cast<uint32_t>(n.mMax));
    }
    log.push_back(NodeNumber(t, t.mXTree->Root()));
    log.push_back(NodeNumber(t, t.mYTree->Root()));
    for (const NodeSet *s : {&t.mXHits, &t.mYHits}) {
        log.push_back(0x5e7);
        log.push_back(static_cast<uint32_t>(s->size()));
        for (const Node *n : *s) log.push_back(NodeNumber(t, n));
    }
}

void NoteRecords(Log &log, const std::vector<DynamicRectRecord> &records)
{
    log.push_back(0x4ec0);
    for (const DynamicRectRecord &r : records) {
        uint32_t w[9];
        std::memcpy(w, &r, sizeof w);
        log.insert(log.end(), w, w + 9);
    }
}

Log MarkSide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    difftest::AllocationCounter counter;
    alignas(16) uint8_t block[kBlock];
    FillRandom(rng, block, kBlock);
    const uint32_t count = rng() % 41;
    std::vector<DynamicRectRecord> records(count + 3);
    for (DynamicRectRecord &r : records) {
        FillRandom(rng, reinterpret_cast<uint8_t *>(&r), sizeof r);
        RandomRect(rng, r.mRect);
        r.mDrawn = FlagByte(rng);
        r.mSecond = FlagByte(rng);
    }
    auto *const counted = new (block + kOffVectors) std::vector<uint32_t>(count);
    auto *const pointers = new (block + kOffVectors + 0xc) std::vector<uint32_t>();
    const uint32_t extra = rng() % 3;
    for (uint32_t i = 0; i < count + extra; ++i)
        pointers->push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&records[i])));
    for (uint32_t at : {kOffBounds, kOffTurboBounds}) {
        int32_t r[4];
        RandomRect(rng, r);
        std::memcpy(block + at, r, sizeof r);
    }
    block[kOffDisableTrees] = static_cast<uint8_t>(rng() % 3 == 0 ? (rng() % 2 ? 1 : rng() | 1) : 0);
    auto *const trees = new (block + kOffTrees) DynamicStrategyTrees;
    uint8_t passDummy[16] = {};
    const uint32_t calls = 1 + rng() % 6;
    for (uint32_t c = 0; c < calls; ++c) {
        const std::list<DynamicWords16> rects = RandomRects(rng, 30);
        const uint32_t turbo = ByteArg(rng), test = rng() % 5 == 0 ? (rng() & 0xffffff00u) : ByteArg(rng) | 1;
        const uint32_t which = ByteArg(rng, true);
        if (original)
            ctx.Original<MarkFn>(0x3724a0)(block, &rects, passDummy, turbo, test, which);
        else
            A::MarkRecords(reinterpret_cast<Self *>(block), nullptr, &rects,
                           reinterpret_cast<nGZSceneGraph::Pass *>(passDummy), turbo, test, which);
        log.push_back(0xca11);
        NoteWords(log, block, kBlock, kOffVectors, kOffVectorsEnd, kOffTrees, kOffTreesEnd);
        NoteRecords(log, records);
        NoteTrees(log, *trees);
        for (DynamicRectRecord &r : records) {
            if (rng() % 4 == 0) r.mDrawn = FlagByte(rng);
            if (rng() % 6 == 0) r.mSecond = FlagByte(rng);
            if (rng() % 6 == 0) RandomRect(rng, r.mRect);
        }
        if (rng() % 5 == 0) block[kOffDisableTrees] ^= 1;
    }
    trees->~DynamicStrategyTrees();
    counted->~vector();
    pointers->~vector();
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
    return log;
}

// ---- SG_DynamicComputeBounds ----

void SetCells(std::mt19937 &rng, CellRect &unbounded, CellRect &empty)
{
    if (rng() % 3) {
        unbounded.Set(INT_MAX, INT_MAX, INT_MIN, INT_MIN);
        empty.Set(0, 0, 0, 0);
    } else {
        int32_t a[4], b[4];
        RandomRect(rng, a);
        RandomRect(rng, b);
        unbounded.Set(a[0], a[1], a[2], a[3]);
        empty.Set(b[0], b[1], b[2], b[3]);
    }
}

Log BoundsSide(difftest::Context &ctx, uint32_t seed, bool original, CellRect &unbounded, CellRect &empty)
{
    std::mt19937 rng(seed);
    Log log;
    alignas(16) uint8_t block[kBlock];
    FillRandom(rng, block, kBlock);
    SetCells(rng, unbounded, empty);
    int32_t pixels[4];
    RandomRect(rng, pixels);
    if (rng() % 3 == 0) {
        pixels[0] = 0;
        pixels[1] = 0;
        pixels[2] = static_cast<int32_t>(rng() % 300);
        pixels[3] = static_cast<int32_t>(rng() % 300);
    }
    std::memcpy(block + kOffPixelRect, pixels, sizeof pixels);
    const uint32_t calls = 1 + rng() % 4;
    for (uint32_t c = 0; c < calls; ++c) {
        const std::list<DynamicWords16> rects = RandomRects(rng, 12);
        const uint32_t which = ByteArg(rng, true);
        if (original)
            ctx.Original<BoundsFn>(0x370eb0)(block, &rects, which);
        else
            A::ComputeBounds(reinterpret_cast<Self *>(block), nullptr, &rects, which);
        log.push_back(0xca11);
        NoteWords(log, block, kBlock);
    }
    return log;
}

// ---- SG_DynamicSetUpScissor ----

struct FakeState {
    void **mVTable;
    Log *mLog;
    const uint8_t *mBlock;
    uint8_t mAnswer;
};

bool __fastcall FakeValidate(FakeState *self, void *, const void *command, uint32_t hardware, uint32_t logIt)
{
    self->mLog->push_back(0x29);
    self->mLog->push_back(static_cast<uint32_t>(static_cast<const uint8_t *>(command) - self->mBlock));
    self->mLog->push_back(hardware & 0xff);
    self->mLog->push_back(logIt & 0xff);
    return self->mAnswer != 0;
}

void __fastcall FakeApply(FakeState *self, void *, uint32_t command)
{
    self->mLog->push_back(0x12);
    self->mLog->push_back(command - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self->mBlock)));
}

uint32_t PassSize(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 0x80000000u + rng() % 5000;
    case 2: return rng();
    default: return 1 + rng() % 2000;
    }
}

Log ScissorSide(difftest::Context &ctx, uint32_t seed, bool original)
{
    std::mt19937 rng(seed);
    Log log;
    alignas(16) uint8_t block[kBlock];
    FillRandom(rng, block, kBlock);
    alignas(16) uint8_t pass[0x300];
    FillRandom(rng, pass, sizeof pass);
    void *vtable[32] = {};
    vtable[29] = reinterpret_cast<void *>(&FakeValidate);
    vtable[12] = reinterpret_cast<void *>(&FakeApply);
    FakeState fake{vtable, &log, block, 0};
    Put(pass, 0x2ac, &fake);
    const uint32_t calls = 1 + rng() % 4;
    for (uint32_t c = 0; c < calls; ++c) {
        const uint32_t k = rng() % 6;
        block[kOffUseScissoring] = static_cast<uint8_t>(k < 1 ? 0 : k < 5 ? 1 : rng());
        Put<uint32_t>(block, kOffDebugMode, rng() % 6 == 0 ? rng() : rng() % 5);
        for (uint32_t at : {kOffBounds, kOffTurboBounds}) {
            int32_t r[4];
            RandomRect(rng, r);
            std::memcpy(block + at, r, sizeof r);
        }
        Put<uint32_t>(pass, 0xc0, PassSize(rng));
        Put<uint32_t>(pass, 0xc4, PassSize(rng));
        fake.mAnswer = static_cast<uint8_t>(rng() % 3 ? 1 : 0);
        const uint32_t which = ByteArg(rng, true);
        bool answer;
        if (original)
            answer = ctx.Original<ScissorFn>(0x3719a0)(block, pass, which);
        else
            answer = A::SetUpScissor(reinterpret_cast<Self *>(block), nullptr, reinterpret_cast<nGZSceneGraph::Pass *>(pass),
                                     which);
        log.push_back(0xca11);
        log.push_back(answer ? 1u : 0u);
        NoteWords(log, block, kBlock);
    }
    return log;
}

} // namespace

// Verifies: MarkRecords 0x3724a0 (with the trees' Build 0x370b70 / Overlaps 0x373750 inside it on our side).
DIFF_TEST(SG_DynamicMarkRecords, 0x3724a0)
{
    heappoison::Scope poison(ctx, kPoison);
    HeapPatches patches;
    (void)MarkSide(ctx, 1, true); // warm-up: the original's first run
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const Log original = MarkSide(ctx, seed, true);
        const Log ours = MarkSide(ctx, seed, false);
        if (!CompareLogs(ctx, "mark records", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: ComputeBounds 0x370eb0.
DIFF_TEST(SG_DynamicComputeBounds, 0x370eb0)
{
    CellRect unbounded(kUnboundedRectCell), empty(kEmptyRectCell);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const Log original = BoundsSide(ctx, seed, true, unbounded, empty);
        const Log ours = BoundsSide(ctx, seed, false, unbounded, empty);
        if (!CompareLogs(ctx, "compute bounds", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: SetUpScissor 0x3719a0.
DIFF_TEST(SG_DynamicSetUpScissor, 0x3719a0)
{
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        const Log original = ScissorSide(ctx, seed, true);
        const Log ours = ScissorSide(ctx, seed, false);
        if (!CompareLogs(ctx, "set up scissor", seed, original, ours)) return;
        ++ctx.cases;
    }
}
