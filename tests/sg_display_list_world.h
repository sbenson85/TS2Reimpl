#pragma once
// The display list tests' shared world (tests/sg_display_list.cpp, tests/sg_display_list_draw.cpp): the object buffer
// both sides run on, the svcfx fakes the constructor and buffers ask, each side's vtable, the capture of the object and
// its passes, and the comparison. Each test file gets its own copy (an anonymous namespace).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cDefaultDisplayListRenderStrategy.h"
#include "sg/cDisplayList.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

using nGZSceneGraph::cDisplayList;
using nGZSceneGraph::cDisplayListAdapters;
using nGZSceneGraph::DrawItem;
using nGZSceneGraph::DrawItemOrder;
using nGZSceneGraph::NodeRecord;
using nGZSceneGraph::Pass;
using nGZSceneGraph::PassEntry;

namespace {

using Blob = std::vector<uint32_t>;

constexpr uint32_t kVTable = 0xe75404;
constexpr unsigned kSlots = 49;
constexpr uint32_t kObjectSize = cDisplayList::kSize;

alignas(16) uint8_t gObject[kObjectSize];
uint8_t *gAt = gObject;          // the object the captures read: gObject, or a constructed one (the lifecycle test)
bool gConstructed = false;  // +0xe0 captured as its object's vtable word (a constructed object's default strategy)
void **gOurVTable;               // our class's vtable, read from an object our constructor built

cDisplayList *Self() { return reinterpret_cast<cDisplayList *>(gAt); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- the services the constructor and the buffers ask (svcfx fakes) -------------------------------------------------
//
// F(0) the framework (cell 0x10c9378): GetSystemService (slot 5) answers the graphics system F(3) for the constructor's
// device event (Connect, slot 8) and nothing for others. F(1) the cached graphic system (cell 0x10ca87c): the current
// device index (slot 21) and the device F(2) (slot 24), whose IsDeviceReady (slot 35) is drawn. F(5) the COM director's
// framework object: GetClassObject (slot 3) hands out the arrays F(6)-F(9) in turn; their ReleaseResource (3) and Create
// (23, answers drawn per array).
constexpr unsigned kFramework = 0, kSystem = 1, kDevice = 2, kGraphics = 3, kCom = 5, kArray0 = 6;

uint32_t SystemService(svcfx::Obj *, uint32_t *args)
{
    if (args[0] != cDisplayList::kGraphicSystemId) return 0;
    svcfx::Obj &graphics = svcfx::F(kGraphics);
    ++graphics.refs;
    *reinterpret_cast<void **>(args[2]) = &graphics;
    return 1;
}

uint32_t ClassObject(svcfx::Obj *self, uint32_t *args)
{
    if (self->value[0] >= 4) return 0;
    *reinterpret_cast<void **>(args[2]) = &svcfx::F(kArray0 + self->value[0]++);
    return 1;
}

// The world for a seed: device index (0, or -1: no device), readiness and the arrays' Create answers.
struct World {
    svcfx::Globals cells;
    explicit World(std::mt19937 &gen, bool device = true)
    {
        svcfx::ResetAll();
        svcfx::gFrameworkIndex = kCom;
        cells.Set(0x10c9378, &svcfx::F(kFramework));
        cells.Set(0x10ca87c, &svcfx::F(kSystem));
        svcfx::F(kFramework).Set(5, "ddp", 0, &SystemService);
        svcfx::F(kSystem).Set(21, "", device ? 0u : 0xffffffffu);
        svcfx::F(kSystem).Set(24, "d", &svcfx::F(kDevice));
        svcfx::F(kGraphics).Set(8, "dpd", 1);
        svcfx::F(kCom).Set(3, "ddp", 0, &ClassObject);
        Draw(gen);
        for (unsigned i = 0; i < 4; ++i) svcfx::F(kArray0 + i).Set(3, "");
    }
    // Readiness and the arrays' answers, drawn again before each step.
    static void Draw(std::mt19937 &gen)
    {
        svcfx::F(kDevice).Set(35, "", gen() % 4 != 0);
        for (unsigned i = 0; i < 4; ++i) svcfx::F(kArray0 + i).Set(23, "pdd", gen() % 5 != 0);
    }
    static void *Array(unsigned i) { return &svcfx::F(kArray0 + i); }
};

constexpr uint32_t kDefaultStrategyVTable = 0xe754e0;

// Our default render strategy's vtable (what the display list's slot 32 builds when given none).
void **DefaultStrategyVTable()
{
    static void **const vtable = [] {
        nGZSceneGraph::cDefaultDisplayListRenderStrategy strategy;
        return shape::VTableAt(&strategy, 0);
    }();
    return vtable;
}

void **ReadOurVTable()
{
    std::mt19937 gen(1);
    World world(gen);
    cDisplayList *const object = new cDisplayList;
    void **const vtable = shape::VTableAt(object, 0);
    delete object;
    return vtable;
}

struct Fixture {
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
    Fixture()
    {
        gOurVTable = ReadOurVTable();
        gAt = gObject;
        gConstructed = false;
    }
};

// Slot `slot` of the object's vtable (the game's on the original's side, ours on ours), as `Fn`.
template <class Fn> Fn Slot(unsigned slot) { return reinterpret_cast<Fn>(shape::VTableAt(gAt, 0)[slot]); }

// A fresh object for `side`: zero, the free lists empty, the vtable of the side.
void Fresh(int side)
{
    std::memset(gObject, 0, kObjectSize);
    Self()->mFreedThisFrame = cDisplayList::kSlotEnd;
    Self()->mFreedLastFrame = cDisplayList::kSlotEnd;
    shape::VTableAt(gObject, 0) = side == 0 ? shape::GameVTable(kVTable) : gOurVTable;
}

// The object's vectors are freed by the test (both sides' buffers come from our heap).
template <class T> void Free(std::vector<T> &v) { std::vector<T>().swap(v); }
void FreeAll()
{
    cDisplayList *const self = Self();
    Free(self->mPasses);
    Free(self->mPassStack);
    Free(self->mItems);
    Free(self->mSlots);
}

template <class T> void VectorShape(Blob &o, const std::vector<T> &v)
{
    o.push_back(static_cast<uint32_t>(v.size()));
    o.push_back(static_cast<uint32_t>(v.capacity()));
}

uint32_t Index(const Pass *pass)
{
    const std::vector<Pass> &passes = Self()->mPasses;
    if (!pass) return 0xffffffffu;
    if (passes.empty() || pass < passes.data() || pass >= passes.data() + passes.size()) return 0xdead0000u;
    return static_cast<uint32_t>(pass - passes.data());
}

void CapturePass(Blob &o, const Pass &pass)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&pass);
    for (uint32_t off = 0; off < sizeof(Pass); off += 4) {
        if (off == offsetof(Pass, mPrevious) || off == offsetof(Pass, mRecord)) continue;
        if ((off >= offsetof(Pass, mEntriesA) && off < offsetof(Pass, mEntriesA) + 12) ||
            (off >= offsetof(Pass, mEntriesB) && off < offsetof(Pass, mEntriesB) + 12) ||
            (off >= offsetof(Pass, mNodes) && off < offsetof(Pass, mNodes) + 12))
            continue;
        uint32_t word;
        std::memcpy(&word, bytes + off, 4);
        o.push_back(word);
    }
    o.push_back(Index(pass.mPrevious));
    o.push_back(static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(pass.mRecord) - bytes));
    for (const std::vector<PassEntry> *entries : {&pass.mEntriesA, &pass.mEntriesB}) {
        VectorShape(o, *entries);
        for (const PassEntry &entry : *entries) {
            uint32_t words[2];
            std::memcpy(words, &entry, 8);
            o.push_back(words[0]);
            o.push_back(words[1]);
        }
    }
    VectorShape(o, pass.mNodes);
    for (const NodeRecord &record : pass.mNodes) {
        uint32_t words[sizeof(NodeRecord) / 4];
        std::memcpy(words, &record, sizeof(record));
        // The two pad bytes after the plane mask: the original's growth constructs new records with only the placement's
        // first word set (0xa9d420: 0x1f), so the pad keeps what the heap had; DrawNode writes every other byte.
        words[offsetof(NodeRecord, mPad0e) / 4] &= 0xffffu;
        o.insert(o.end(), std::begin(words), std::end(words));
    }
}

// What the original's constructor leaves uninitialised (ours zeroes it): the pad byte +0x33, +0xe4 and the two vectors
// +0x140 / +0x14c. Masked on constructed objects; the lifecycle test checks the original leaves exactly these.
bool LeftByConstructor(uint32_t off) { return off == 0xe4 || (off >= 0x140 && off < 0x158); }
constexpr uint32_t kPadMask = 0x00ffffffu; // the word at +0x30 without +0x33

// The object: its words outside the vectors (but the two vtable words, +0 and +8, which differ per side; on a
// constructed object the strategy at +0xe0 by its vtable word and the words above masked), then the vectors.
void Capture(Blob &o)
{
    static const uint32_t kVectors[] = {0x3c, 0x48, 0x58, 0x68, 0x78, 0x94, 0xa4, 0xb4, 0xc0, 0xcc};
    o.push_back(0xeeee);
    for (uint32_t off = 4; off < kObjectSize; off += 4) {
        bool inVector = false;
        for (uint32_t v : kVectors) inVector = inVector || (off >= v && off < v + 12);
        if (inVector || off == 8) continue;
        uint32_t word;
        std::memcpy(&word, gAt + off, 4);
        if (gConstructed && LeftByConstructor(off)) continue;
        if (gConstructed && off == 0x30) word &= kPadMask;
        if (off == 0xe0 && gConstructed && word) {
            bool fake = false;
            for (unsigned i = 0; i < svcfx::kFakes; ++i) fake = fake || word == svcfx::P(&svcfx::F(i));
            if (!fake) {
                // A default strategy: the game's (vtable 0xe754e0) or ours, then its reference count.
                const auto *strategy = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(word));
                const bool known = strategy[0] == svcfx::P(shape::GameVTable(kDefaultStrategyVTable)) ||
                                   strategy[0] == svcfx::P(DefaultStrategyVTable());
                o.push_back(known ? 0xdef0u : strategy[0]);
                word = strategy[1];
            }
        }
        o.push_back(word);
    }
    cDisplayList *const self = Self();
    for (uint32_t v : kVectors) {
        if (v == 0x3c || v == 0x48 || v == 0xb4 || v == 0xcc) continue;
        uint32_t words[3];
        std::memcpy(words, gAt + v, 12);
        o.push_back(words[0] | words[1] | words[2]); // never touched: still empty
    }
    VectorShape(o, self->mSlots);
    for (int32_t slot : self->mSlots) o.push_back(static_cast<uint32_t>(slot));
    VectorShape(o, self->mPasses);
    for (const Pass &pass : self->mPasses) CapturePass(o, pass);
    VectorShape(o, self->mPassStack);
    for (const Pass *pass : self->mPassStack) o.push_back(Index(pass));
    VectorShape(o, self->mItems);
    for (const DrawItem &item : self->mItems) {
        uint32_t words[sizeof(DrawItem) / 4];
        std::memcpy(words, &item, sizeof(item));
        o.insert(o.end(), std::begin(words), std::end(words));
    }
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return true;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: outcomes differ at word %zu of %zu / %zu (%08x vs %08x)", what, seed, i, a.size(), b.size(),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    return false;
}

} // namespace
