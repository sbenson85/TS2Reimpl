// Differential tests of nTSSG::cOverlayNode (lane 261108d, scenegraph row LT1; src/sg/cOverlayNode.h). Each side builds its
// own nodes in the same 0xcd-filled blocks: the game's constructor 0x3ae470, or our Construct (the hook's adapter), so the
// bytes neither constructor writes compare as well.
//
//   Overlay_Node_Construct  0x3ae470 in place; then 0x3ae890 and delete, the deleting 0x3aeb80 (with and without bit 0) or
//                           the last Release, on nodes with every field filled: every call in its order, the blocks freed
//   Overlay_Node_Draw       71 0x3af3a0, 72 0x3af3c0, 82 0x3aed00, 83 0x3aec90, 60, 87-90 and the face's slots (3 0x3af1b0,
//                           4 0x3b16f0, 5 0x3afdf0, 6 0x3afdb0, 7, 8 0x3b1820, 9 0x3afd50, 10 0x3af420, 11 0x3af440, 12, 13,
//                           14) in random order on nodes in any state, the display list's frame and the view changing
//   Overlay_Node_Pilot_*    the template levels' copies through the pilot (tests/sg_transform_pilot.h, level 4): slots,
//                           events, persist and clone (the shared 15 makes a plain transform node)
//
// The world: the allocator service (cache 0x10c95d4, the guard as after its first use) the entries and the list allocate
// through, handing out heap blocks (they normalise by first reach); two generators that rewrite the entries vector the way
// a game generator would (the node never grows it itself); two geometries with an info block (the bound at +0x14); the
// display list (its frame, DrawNode, AddGeometrySubsets); the detail object's item slots; the material manager in 0x10f8fcc.
// SetGeometry is never given null: the original dereferences it (ORIGINAL-BUG, see the header).
#include "difftest.h"
#include "game/vtable.h"
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sgfx.h"

#include "sg/cOverlayNode.h"
#include "sg/cRenderableNodeImpl.h"
#include "sg/cTransformNode.h"
#include "sg/cViewerRefNodeBaseImpl.h"

#include <array>
#include <bit>
#include <cfloat>
#include <cstring>
#include <limits>
#include <new>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;
using nTSSG::cIOverlayNode;
using nTSSG::OverlayEntry;

using Node = nTSSG::cOverlayNode;
constexpr uint32_t kCtor = 0x3ae470, kDtor = 0x3ae890, kDeleting = 0x3aeb80;
constexpr uint32_t kWords = Node::kSize / 4;
constexpr uint32_t kCompared = cTransformNode::kSize; // the words the pilot compares: all a clone has; NoteTail the rest
constexpr uint32_t kEventCell = 0x10fc820;
constexpr uint32_t kMaterialCell = 0x10f8fcc;
constexpr uint32_t kServiceCell = 0x10c95d4, kServiceGuard = 0x10c95d8;
constexpr uint32_t kViewTag = 0x79000000, kTailTag = 0x7a000000, kInfoTag = 0x7b400000;
constexpr uint32_t kFaceOffset = 0x7c;
constexpr uint32_t kFaceWord = kFaceOffset / 4, kInitWord = 0x80 / 4, kWord84 = 0x84 / 4, kGeneratorWord = 0x88 / 4,
                   kGeometryWord = 0x8c / 4, kHandleWord = 0x90 / 4, kEntriesWord = 0xb0 / 4, kListWord = 0xbc / 4,
                   kBuiltWord = 0x17c / 4, kMaterialsWord = 0x180 / 4, kFrameWord = 0x18c / 4, kSlotsWord = 0x190 / 4,
                   kModeWord = 0x198 / 4;
constexpr uint32_t kEntrySize = sizeof(OverlayEntry), kEntryWords = kEntrySize / 4;

template <unsigned N> constexpr std::array<Slot, N> BaseSlots()
{
    std::array<Slot, N> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    return a;
}

constexpr std::array<Slot, 44> MakeListSlots()
{
    std::array<Slot, 44> a = BaseSlots<44>();
    a[25] = {4, Ret::Int, true, "DrawNode", "aF13F6u"};
    a[28] = {8, Ret::Int, true, "AddGeometrySubsets", "auF1F1F1aau"};
    a[43] = {0, Ret::Int, true, "GetFrame", ""};
    return a;
}
constexpr auto kListSlots = MakeListSlots();
constexpr drawfx::Iface kList{"DisplayList", 44, kListSlots.data()};

constexpr std::array<Slot, 44> MakeDetailSlots()
{
    std::array<Slot, 44> a = BaseSlots<44>();
    a[18] = {1, Ret::Int, true, "AllocSlots", "u"};
    a[19] = {3, Ret::Int, true, "ReallocSlots", "uuu"};
    a[20] = {2, Ret::Int, true, "FreeSlots", "uu"};
    a[43] = {0, Ret::Int, true, "Limit", ""};
    return a;
}
constexpr auto kDetailSlots = MakeDetailSlots();
constexpr drawfx::Iface kDetail{"Detail", 44, kDetailSlots.data()};

constexpr std::array<Slot, 22> MakeMaterialsSlots()
{
    std::array<Slot, 22> a = BaseSlots<22>();
    a[19] = {1, Ret::Int, true, "FindOrLoad", "s"};
    a[20] = {1, Ret::Int, true, "AddRefIndex", "u"};
    a[21] = {1, Ret::Int, true, "ReleaseIndex", "u"};
    return a;
}
constexpr auto kMaterialsSlots = MakeMaterialsSlots();
constexpr drawfx::Iface kMaterials{"MaterialManager", 22, kMaterialsSlots.data()};

constexpr std::array<Slot, 5> MakeServiceSlots()
{
    std::array<Slot, 5> a = BaseSlots<5>();
    a[3] = {1, Ret::Int, true, "Allocate", "u"};
    a[4] = {1, Ret::Int, true, "Free", "a"};
    return a;
}
constexpr auto kServiceSlots = MakeServiceSlots();
constexpr drawfx::Iface kService{"AllocatorService", 5, kServiceSlots.data()};

constexpr std::array<Slot, 12> MakeGeneratorSlots()
{
    std::array<Slot, 12> a = BaseSlots<12>();
    a[11] = {1, Ret::Int, true, "Generate", "a"};
    return a;
}
constexpr auto kGeneratorSlots = MakeGeneratorSlots();
constexpr drawfx::Iface kGenerator{"OverlayGenerator", 12, kGeneratorSlots.data()};

constexpr std::array<Slot, 12> MakeGeometrySlots()
{
    std::array<Slot, 12> a = BaseSlots<12>();
    a[4] = {0, Ret::Int, true, "GetFlags", ""};
    a[9] = {1, Ret::Int, true, "Attach", "a"};
    a[10] = {0, Ret::Int, true, "Detach", ""};
    a[11] = {1, Ret::Int, true, "Build", "a"};
    return a;
}
constexpr auto kGeometrySlots = MakeGeometrySlots();
constexpr drawfx::Iface kGeometry{"OverlayGeometry", 12, kGeometrySlots.data()};

struct World {
    Fake list, materials, service, generators[2], geometries[2];
    unsigned nextSlot = 0;
};
World w;
alignas(16) cViewerView gView;
alignas(16) uint32_t gInfo[2][0x2c / 4]; // the geometries' info blocks: the bound at +0x14
std::mt19937 gStream;                     // the generators' choices (both sides make the same calls in the same order)
difftest::Context *gCtx;
uint32_t gNodeVtable; // this side's overlay node's vtable word (a clone, and what a freed node's block becomes, have others)

uint32_t *Words(cICompositionTreeNode *n) { return reinterpret_cast<uint32_t *>(n); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
cITSParticlesRenderable *As(cICompositionTreeNode *n)
{
    return static_cast<cITSParticlesRenderable *>(static_cast<cIRenderableNode *>(n));
}
cIOverlayNode *Face(cICompositionTreeNode *n)
{
    return reinterpret_cast<cIOverlayNode *>(reinterpret_cast<uint8_t *>(n) + kFaceOffset);
}

void *Filled()
{
    void *const memory = ::operator new(Node::kSize);
    std::memset(memory, 0xcd, Node::kSize);
    return memory;
}
void *MakeOriginalAt(void *memory)
{
    void *const made = gCtx->Original<void *(__thiscall *)(void *)>(kCtor)(memory);
    gNodeVtable = *static_cast<uint32_t *>(memory);
    return made;
}
void *MakeOurAt(void *memory)
{
    void *const made = nTSSG::cOverlayNodeAdapters::Construct(static_cast<Node *>(memory), nullptr);
    gNodeVtable = *static_cast<uint32_t *>(memory);
    return made;
}
cICompositionTreeNode *__cdecl MakeOriginal() { return reinterpret_cast<cICompositionTreeNode *>(MakeOriginalAt(Filled())); }
cICompositionTreeNode *__cdecl MakeOurs() { return reinterpret_cast<cICompositionTreeNode *>(MakeOurAt(Filled())); }

// The node's words past the pilot's (the face's vtable and the overlay's own fields), as a range of their own.
void AddTail(cICompositionTreeNode *n, unsigned index)
{
    drawfx::AddRange(reinterpret_cast<uint8_t *>(n) + kCompared, Node::kSize - kCompared, kTailTag | index << 12);
}

// ---- the fakes' handlers ----

uint32_t ListSlots(Fake &, const uint32_t *) { return 0x40 + w.nextSlot++ * 0x10; }
uint32_t GeometryInfo(Fake &self, const uint32_t *) { return self.value[0]; }

uint32_t ServiceAllocate(Fake &, const uint32_t *a)
{
    void *const block = ::operator new(a[0] ? a[0] : 1);
    std::memset(block, 0xcd, a[0]);
    return P(block);
}
uint32_t ServiceFree(Fake &, const uint32_t *a)
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(a[0])));
    return 0;
}

uint32_t TextHash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}
uint32_t FindOrLoad(Fake &, const uint32_t *a)
{
    const uint32_t h = TextHash(reinterpret_cast<const char *>(uintptr_t(a[0])));
    return h % 5 == 0 ? 0xffffffffu : h % 50;
}

void RandomEntry(OverlayEntry &e, std::mt19937 &rng)
{
    for (uint32_t &word : e.mWords) word = uint32_t(rng());
    e.mWordA4 = rng() % 2 ? -1 : int32_t(rng());
    for (uint8_t &b : e.mPadA9) b = uint8_t(rng());
    for (uint8_t &b : e.mPadB5) b = uint8_t(rng());
    e.mMaterial = uint8_t(rng() % Node::kMaterialCount);
    const uint8_t enabled[] = {0, 1, 0x80, 0};
    e.mEnabled = enabled[rng() % 4];
}

// An entries block of `capacity` entries (the vector's words: first, last, end), `count` of them filled.
void FillEntries(uint32_t *vector, unsigned count, unsigned capacity, std::mt19937 &rng)
{
    auto *const block = capacity ? static_cast<OverlayEntry *>(::operator new(capacity * kEntrySize)) : nullptr;
    if (block) std::memset(static_cast<void *>(block), 0xcd, capacity * kEntrySize);
    for (unsigned i = 0; i < count; ++i) RandomEntry(block[i], rng);
    vector[0] = P(block);
    vector[1] = P(block + count);
    vector[2] = P(block + capacity);
}

// A generator: rewrites the entries now and then (reallocating as a growing vector would), and answers a change or none
// (only the low byte is the answer).
uint32_t Generate(Fake &, const uint32_t *a)
{
    auto *const vector = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    static const uint32_t kAnswers[] = {0, 1, 0x100, 0xffffff01u, 1, 1};
    const uint32_t answer = kAnswers[gStream() % 6];
    if (gStream() % 3) {
        const unsigned count = gStream() % 7;
        const unsigned capacity = vector[0] ? (vector[2] - vector[0]) / kEntrySize : 0;
        if (count > capacity) {
            void *const old = reinterpret_cast<void *>(uintptr_t(vector[0]));
            FillEntries(vector, count, count + gStream() % 2, gStream);
            ::operator delete(old);
        } else {
            auto *const first = reinterpret_cast<OverlayEntry *>(uintptr_t(vector[0]));
            for (unsigned i = 0; i < count; ++i) RandomEntry(first[i], gStream);
            vector[1] = P(first + count);
        }
    }
    return answer;
}

// What the geometry is built from: the entries' count, materials and flags.
uint32_t Build(Fake &, const uint32_t *a)
{
    const auto *const vector = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    drawfx::Call c;
    c.note = "build from";
    const auto *const first = reinterpret_cast<const OverlayEntry *>(uintptr_t(vector[0]));
    const auto *const last = reinterpret_cast<const OverlayEntry *>(uintptr_t(vector[1]));
    c.words.push_back(uint32_t(last - first));
    for (const OverlayEntry *e = first; e != last; ++e) c.words.push_back(e->mMaterial | e->mEnabled << 8 | e->mWords[0] << 16);
    drawfx::gLog.push_back(std::move(c));
    return 0;
}

float RandomBoundWord(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return -0.0f;
    case 1: return 0.0f;
    case 2: return std::bit_cast<float>(0xffc12345u);
    case 3: return std::numeric_limits<float>::infinity();
    case 4: return std::bit_cast<float>(0x00000001u);
    default: return sgtx::RandomFloat(rng);
    }
}

void RandomInfo(unsigned i, std::mt19937 &rng)
{
    uint32_t *const info = gInfo[i];
    for (unsigned k = 0; k < 5; ++k) info[k] = uint32_t(rng());
    float *const bound = reinterpret_cast<float *>(info + 5);
    for (unsigned k = 0; k < 6; ++k) bound[k] = RandomBoundWord(rng);
    if (rng() % 3) { // a box the frustum can sort
        for (unsigned k = 0; k < 3; ++k) {
            bound[k] = sgtx::RandomFloat(rng);
            bound[k + 3] = bound[k] + float(rng() % 5);
        }
    }
}

// The view: its frame and six planes, each an axis (either way) at a distance.
void RandomView(std::mt19937 &rng)
{
    const cTransform34 frame = sgtx::RandomFrame(rng);
    std::memcpy(&gView.mFrame, &frame, sizeof(frame));
    for (auto &plane : gView.mFrustum.plane) {
        for (float &c : plane) c = 0.0f;
        plane[rng() % 3] = rng() % 2 ? 1.0f : -1.0f;
        plane[3] = float(int(rng() % 13) - 4);
    }
}

uint32_t RandomMode(std::mt19937 &rng)
{
    const uint32_t modes[] = {0, 1, 2, 0x80000000u, 0xffffffffu, uint32_t(rng())};
    return modes[rng() % 6];
}

// Fills the node's own fields as the game's use leaves them: the references are counted, as a holder would.
void Plant(cICompositionTreeNode *n, std::mt19937 &rng)
{
    uint32_t *const words = Words(n);
    auto *const bytes = reinterpret_cast<uint8_t *>(n);
    if (rng() % 2) bytes[0x80] = uint8_t(rng() % 2);
    if (rng() % 2) words[kWord84] = uint32_t(rng());
    if (rng() % 3) {
        Fake &generator = w.generators[rng() % 2];
        ++generator.refs;
        words[kGeneratorWord] = P(&generator);
    }
    if (rng() % 3) {
        Fake &geometry = w.geometries[rng() % 2];
        ++geometry.refs;
        words[kGeometryWord] = P(&geometry);
    }
    for (unsigned i = 0; i < Node::kMaterialCount; ++i)
        if (rng() % 2) words[kHandleWord + i] = rng() % 4 ? rng() % 50 : 0xffffffffu;
    if (rng() % 2) {
        const unsigned count = rng() % 6, capacity = count + rng() % 2;
        if (capacity) FillEntries(words + kEntriesWord, count, capacity, rng);
    }
    if (rng() % 3 == 0) { // list nodes: next, prev, value
        auto *const head = reinterpret_cast<uint32_t *>(uintptr_t(words[kListWord]));
        const unsigned count = 1 + rng() % 3;
        for (unsigned i = 0; i < count; ++i) {
            auto *const node = static_cast<uint32_t *>(::operator new(12));
            auto *const last = reinterpret_cast<uint32_t *>(uintptr_t(head[1]));
            node[0] = P(head);
            node[1] = P(last);
            node[2] = uint32_t(rng());
            last[0] = P(node);
            head[1] = P(node);
        }
        words[kListWord + 1] += count;
    }
    if (rng() % 2) bytes[0x17c] = uint8_t(rng() % 2);
    if (rng() % 2) {
        const unsigned count = rng() % 6, capacity = count + rng() % 2;
        if (capacity) {
            auto *const block = static_cast<uint32_t *>(::operator new(capacity * 4));
            for (unsigned i = 0; i < capacity; ++i) block[i] = i < count ? uint32_t(rng() % 50) : 0xcdcdcdcdu;
            words[kMaterialsWord] = P(block);
            words[kMaterialsWord + 1] = P(block + count);
            words[kMaterialsWord + 2] = P(block + capacity);
        }
    }
    if (rng() % 2) words[kFrameWord] = rng() % 4;
    if (rng() % 2) {
        words[kSlotsWord] = rng() % 300;
        words[kSlotsWord + 1] = rng() % 8;
    }
    if (rng() % 2) words[kModeWord] = RandomMode(rng);
}

// A vector's words as differences from its first (each side's blocks are its own), then its elements' words.
void PushVector(drawfx::Call &c, const uint32_t *vector)
{
    for (unsigned k = 0; k < 3; ++k) c.words.push_back(vector[0] ? vector[k] - vector[0] : vector[k]);
}

void NoteTail(const char *label, cICompositionTreeNode *n)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *const words = Words(n);
    auto flush = [&] {
        drawfx::gLog.push_back(c);
        c.words.clear();
    };
    for (uint32_t k = kCompared / 4; k < kWords; ++k) {
        if (k == kFaceWord) continue; // (each side's face vtable)
        if (k == kEntriesWord) PushVector(c, words + k), k += 2;
        else if (k == kMaterialsWord) PushVector(c, words + k), k += 2;
        else if (k == kListWord) c.words.push_back(words[k] != 0); // (the head is each side's block)
        else c.words.push_back(drawfx::Norm(words[k]));
        if (c.words.size() >= 8) flush(); // (a failure prints this much of a note)
    }
    if (!c.words.empty()) flush();
    c.note = "entries";
    if (words[kEntriesWord])
        for (const uint32_t *at = reinterpret_cast<const uint32_t *>(uintptr_t(words[kEntriesWord]));
             P(at) != words[kEntriesWord + 1]; ++at)
            c.words.push_back(*at);
    flush();
    c.note = "materials";
    if (words[kMaterialsWord])
        for (const uint32_t *at = reinterpret_cast<const uint32_t *>(uintptr_t(words[kMaterialsWord]));
             P(at) != words[kMaterialsWord + 1]; ++at)
            c.words.push_back(*at);
    flush();
    c.note = "list";
    if (const auto *const head = reinterpret_cast<const uint32_t *>(uintptr_t(words[kListWord]))) {
        const uint32_t *at = reinterpret_cast<const uint32_t *>(uintptr_t(head[0]));
        for (unsigned i = 0; at != head && i <= words[kListWord + 1]; ++i) {
            c.words.push_back(at[2]);
            at = reinterpret_cast<const uint32_t *>(uintptr_t(at[0]));
        }
    }
    flush();
}

void NoteBlocks()
{
    drawfx::Note("refs", {uint32_t(w.generators[0].refs), uint32_t(w.generators[1].refs), uint32_t(w.geometries[0].refs),
                          uint32_t(w.geometries[1].refs), uint32_t(w.list.refs), uint32_t(w.service.refs)});
}

// The world of every test (the pilot's Build calls it per seed).
void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed ^ 0x0ae470u);
    gStream.seed(seed * 31 + 7);
    sgrx::Setup(rng);
    Fake &detail = sgrx::Registry::Get().detail;
    detail.Reset<kDetail>(702);
    detail.Returns(43, uint32_t(rng() % 1000));
    detail.On(18, &ListSlots);
    detail.On(19, &ListSlots);
    w.nextSlot = 0;
    w.materials.Reset<kMaterials>(760);
    w.materials.On(19, &FindOrLoad);
    cells.Set(kMaterialCell, static_cast<const void *>(&w.materials));
    w.service.Reset<kService>(761);
    w.service.On(3, &ServiceAllocate);
    w.service.On(4, &ServiceFree);
    cells.Set(kServiceCell, static_cast<const void *>(&w.service));
    cells.Set(kServiceGuard, 0x80000000u); // as after the first fetch
    w.list.Reset<kList>(762);
    w.list.Returns(43, uint32_t(rng() % 4));
    for (unsigned i = 0; i < 2; ++i) {
        w.generators[i].Reset<kGenerator>(763 + i);
        w.generators[i].On(11, &Generate);
        Fake &geometry = w.geometries[i];
        geometry.Reset<kGeometry>(765 + i);
        geometry.On(4, &GeometryInfo);
        geometry.On(11, &Build);
        geometry.value[0] = P(gInfo[i]);
        RandomInfo(i, rng);
        drawfx::AddRange(gInfo[i], sizeof(gInfo[i]), kInfoTag | i << 8);
    }
    std::memset(&gView, 0, sizeof(gView));
    gView.mDrawer = &w.list;
    RandomView(rng);
    drawfx::AddRange(&gView, sizeof(gView), kViewTag);
}

struct Scope {
    sgrx::Services services;
};

// ---- the pilot (level 4: a bounded node of another family; see sg_transform_pilot.h) ----

struct Cfg {
    using Interface = cITSParticlesRenderable;
    static constexpr uint32_t kSize = kCompared; // (a clone is a plain transform node: the rest is noted by NoteNode)
    static constexpr int kLevel = 4;
    static constexpr uint32_t kIid = cRenderableNodeImpl::kIid;
    static constexpr uint32_t kEventCell = ::kEventCell;
    using Scope = ::Scope;
    static sgfx::Factory OriginalFactory(difftest::Context &ctx)
    {
        gCtx = &ctx;
        return &MakeOriginal;
    }
    static cICompositionTreeNode *Create() { return MakeOurs(); }
    static void SetupCells(uint32_t seed, drawfx::Cells &cells);
    static void Prepare(cICompositionTreeNode *n, std::mt19937 &rng);
    // A leaf, as the game's overlay nodes are: a copy is a plain transform node that keeps the source's "bounded" flag
    // (as the particles node's; see Clone there).
    static cICompositionTreeNode *CloneSource(cICompositionTreeNode *n)
    {
        while (cICompositionTreeNode *const child = n->GetFirstChild()) n = child;
        return n;
    }
    static void NoteNode(const char *label, cICompositionTreeNode *n)
    {
        if (Words(n)[0] == gNodeVtable) NoteTail(label, n); // (a clone is a plain transform node)
    }
};
using Pilot = sgtp::Pilot<Cfg>;

void Cfg::SetupCells(uint32_t seed, drawfx::Cells &cells)
{
    BuildWorld(seed, cells);
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, Pilot::w.manager.manager);
}

void Cfg::Prepare(cICompositionTreeNode *n, std::mt19937 &rng)
{
    const sgfx::Nodes &ns = Pilot::w.nodes;
    unsigned index = 0;
    while (index < ns.count && ns.at[index] != n) ++index;
    AddTail(n, index);
    Plant(n, rng);
}

void Mark(difftest::Context &ctx, int k, unsigned op)
{
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
}

void NoteBox(const char *label, const cBoundBox &b) { sgtx::NoteFloats(label, b.mMin, 6); }

void NoteEntry(cICompositionTreeNode *n, const OverlayEntry *entry)
{
    drawfx::Call c;
    c.note = "entry";
    const uint32_t first = Words(n)[kEntriesWord];
    if (P(entry) == P(n) + offsetof(Node, mDefault)) c.words.push_back(0xdefau);
    else c.words.push_back((P(entry) - first) / kEntrySize);
    const auto *const at = reinterpret_cast<const uint32_t *>(entry);
    c.words.insert(c.words.end(), at, at + kEntryWords);
    drawfx::gLog.push_back(std::move(c));
}

const char *RandomName(std::mt19937 &rng)
{
    static const char *const kNames[] = {"OverlayDecalMaterial", "OverlayShadowMaterial", "floorDecal", "skirtEdge", "", "x"};
    return kNames[rng() % 6];
}

void OperateDraw(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = Pilot::w.nodes;
    cICompositionTreeNode *const n = ns[rng() % ns.count];
    cITSParticlesRenderable *const s = As(n);
    cIOverlayNode *const face = Face(n);
    const unsigned op = rng() % 24;
    Mark(ctx, k, op);
    switch (op) {
    case 0: // 72
        NoteBox("box", s->GetBoundBox());
        break;
    case 1: { // 71
        const cTransform34 frame = sgtx::RandomFrame(rng);
        NoteBox("box in", s->GetBoundBoxIn(&frame));
        break;
    }
    case 2:
    case 3:
    case 4: // 82
        s->Draw(&gView, rng() % 2 ? 0x3fu : uint32_t(rng()));
        break;
    case 5: // 83
        s->Slot83(&gView, reinterpret_cast<void *>(uintptr_t(rng())));
        break;
    case 6:
        RandomView(rng);
        break;
    case 7: // the display list's next frame
        w.list.Returns(43, uint32_t(rng() % 4));
        break;
    case 8:
        RandomInfo(rng() % 2, rng);
        break;
    case 9: // 87-90
        if (rng() % 2) s->SetWord6c(rng() % 2 ? 0 : uint32_t(rng()));
        drawfx::Note("words", {s->GetRenderableKind(), s->GetDetailLimit(), s->GetWord6c()});
        break;
    case 10: { // 60
        const cRigidTransform t = sgtx::RandomRigid(rng);
        n->OnContentChanged(P(&t), 0, 0);
        const cRigidTransform world = n->GetWorldTransform();
        sgtx::NoteFloats("world", world.mValues, 7);
        break;
    }
    case 11: // face 3
        drawfx::Note("init", uint8_t(face->Init()));
        break;
    case 12: // face 4
        drawfx::Note("shutdown", uint8_t(face->Shutdown()));
        break;
    case 13: { // face 5
        const unsigned which = rng() % 3;
        face->SetGenerator(which == 2 ? nullptr : reinterpret_cast<nTSSG::cIOverlayGenerator *>(&w.generators[which]));
        break;
    }
    case 14: // face 6 (never null: ORIGINAL-BUG)
        face->SetGeometry(reinterpret_cast<nTSSG::cIOverlayGeometry *>(&w.geometries[rng() % 2]));
        break;
    case 15: // face 7, 12
        if (rng() % 2) face->SetWord84(uint32_t(rng()));
        drawfx::Note("word84", face->GetWord84());
        break;
    case 16: // face 8
        drawfx::Note("update", uint8_t(face->Update()));
        break;
    case 17: { // face 9
        const uint32_t indices[] = {0, 3, 6, 7, 8, 0xffffffffu, 0x80000000u, uint32_t(rng() % 10)};
        face->SetMaterial(indices[rng() % 8], RandomName(rng));
        break;
    }
    case 18: { // face 10, 11
        drawfx::Note("count", uint32_t(face->GetEntryCount()));
        const int32_t indices[] = {0, 1, 2, 5, -1, INT32_MIN, int32_t(rng() % 8), 0};
        uint8_t built = 0xcc;
        const OverlayEntry *const entry = face->GetEntry(indices[rng() % 8], rng() % 3 ? &built : nullptr);
        drawfx::Note("built", built);
        NoteEntry(n, entry);
        break;
    }
    case 19: // face 13, 14 (GetMode is the folded body 0x1cc660, shared_vf_1cc660)
        if (rng() % 2) Words(n)[kModeWord] = RandomMode(rng);
        drawfx::Note("node, mode", {uint32_t(face->GetNode() == n), face->GetMode()});
        break;
    case 20: // face 8 then 82: entries that changed this frame
        face->Update();
        s->Draw(&gView, 0x3fu);
        break;
    case 21: // a generator's next answers
        gStream.discard(rng() % 3);
        break;
    case 22:
    case 23: { // 0 / 1 / 2 through the second interface (+8: thunks 0x3af645, 0x3af695) or the face (0x3af64d, 0x3aecf5,
               // 0x3af69d)
        void *const at = reinterpret_cast<uint8_t *>(n) + (op == 22 ? 8 : kFaceOffset);
        const uint32_t added = VirtualCall<uint32_t(__thiscall *)(void *), 1>(at);
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cRenderableNodeImpl::kIid, 0x12345678u, uint32_t(rng())};
        void *out = reinterpret_cast<void *>(uintptr_t(0xcccccccc));
        const bool found = VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(at, iids[rng() % 5], &out);
        drawfx::Note("faces", {added, uint32_t(found), drawfx::Norm(P(out))});
        if (found && out) static_cast<cIGZUnknown *>(out)->Release();
        drawfx::Note("released", VirtualCall<uint32_t(__thiscall *)(void *), 2>(at));
        break;
    }
    }
}

} // namespace

DIFF_TEST(Overlay_Node_Construct, kCtor)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    sgfx::CheckSides(ctx, "construct", 128, Pilot::Build, [&](bool original, std::mt19937 &rng) {
        void *const memory = Filled();
        void *const made = original ? MakeOriginalAt(memory) : MakeOurAt(memory);
        drawfx::Note("in place", uint8_t(made == memory));
        auto *const n = reinterpret_cast<cICompositionTreeNode *>(made);
        drawfx::Note("count", CountOf(n));
        n->AddRef();
        sgfx::Nodes &ns = Pilot::w.nodes;
        ns.at[0] = n;
        ns.count = 1;
        ns.size = kCompared;
        drawfx::AddRange(n, kCompared, sgfx::kNodeTag);
        AddTail(n, 0);
        ns.NoteWords("new node", 0b101);
        ns.NoteShape("new node");
        NoteTail("new node", n);
        Plant(n, rng);
        NoteTail("planted", n);
        ns.count = 0;
        const unsigned end = rng() % 4;
        drawfx::Note("end", end);
        {
            difftest::FreeRecorder freed;
            switch (end) {
            case 0:
                n->Release();
                break;
            case 1:
                if (original) gCtx->Original<void(__thiscall *)(void *)>(kDtor)(n);
                else nTSSG::cOverlayNodeAdapters::Destruct(reinterpret_cast<Node *>(n), nullptr);
                ::operator delete(n);
                break;
            default: {
                const uint32_t flags = end == 2 ? 1 : 0;
                void *const back = original ? gCtx->Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(n, flags)
                                            : VirtualCall<void *(__thiscall *)(void *, uint32_t), 91>(n, flags);
                drawfx::Note("returns this", uint8_t(back == n));
                if (!flags) ::operator delete(n);
                break;
            }
            }
            freed.Stop();
            size_t sizes[16];
            const size_t count = freed.SortedSizes(sizes, 16);
            drawfx::Call c;
            c.note = "freed";
            for (size_t i = 0; i < count; ++i) c.words.push_back(uint32_t(sizes[i]));
            drawfx::gLog.push_back(std::move(c));
        }
        NoteBlocks();
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Overlay_Node_Draw, 0x3aed00)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    sgfx::CheckSides(ctx, "draw", 256, Pilot::Build, [&](bool original, std::mt19937 &rng) {
        Pilot::MakeTree(Pilot::Maker(ctx, original), rng);
        Pilot::NoteNodes("built");
        for (int k = 0; k < 48; ++k) OperateDraw(ctx, k, rng);
        Pilot::NoteNodes("after");
        Pilot::ReleaseAll();
        NoteBlocks();
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Overlay_Node_Pilot_Factory, kCtor) { Pilot::Factory(ctx); }
DIFF_TEST(Overlay_Node_Pilot_Slots, 0x3af3c0) { Pilot::Slots(ctx); }
DIFF_TEST(Overlay_Node_Pilot_Events, 0xbbd020) { Pilot::Events(ctx); }
DIFF_TEST(Overlay_Node_Pilot_Persist, 0xbb9270) { Pilot::Persist(ctx); }
DIFF_TEST(Overlay_Node_Pilot_Clone, 0xa3a3b0) { Pilot::Clone(ctx); }
