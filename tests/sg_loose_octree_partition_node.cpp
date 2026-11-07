// Differential tests of nGZSceneGraph::cLooseOctreePartitionNode (lane OI, scenegraph N1 step 2): the game's octree (its
// factory 0xa51ab0, its vtables 0xe6e1b4 / 0xe6e320 and the bodies behind them) against ours (Create), each side building
// its own octrees with its own factory.
//  - The pilot (sg_transform_pilot.h, level 3): trees of octrees through the composition, transform and bound slots
//    (66-72), the +8 persist face (Read 0xa526b0, GetTypeInfo 0xa535f0, Write 0xa53790), Clone (shared, empty) and events.
//  - Content: one octree with content nodes (cIRenderableNode fakes) and sub-partitions (cIPartitionNode fakes), driven
//    through slots 60 (0xa536d0), 71-75 (0xa51fc0, 0xa52080, 0xa51bc0, 0xa51c40, 0xa51f10), 77-88 (Clear 0xa51790,
//    RemovePartition 0xa52840, RemoveNode 0xa527a0, AddPartition 0xa522a0, UpdateNode 0xa524b0, InsertNode 0xa520e0 and
//    InsertInto 0xa51cc0, QueryShape 0xa52a80 / 0xa52c70, QueryLinear 0xa52b80 / 0xa52dd0, RayCast 0xa52f30 / 0xa53220,
//    Cull 0xa53500 / 0xa51960, SetSize 0xa52a10) and the destructor 0xa51170 (DeleteChild 0xa51730, HasChildren
//    0xa51ba0, the pool helpers 0xa51280 / 0xa512b0 / 0xa512e0 / 0xa51360, the cell 0xa50e80), with the octree's words,
//    its cells, its item list and both pools compared after every step.
//    The content boxes gather around anchors (shared and sibling cells), some sit exactly on the fit test's boundary
//    (BoundaryBox), and the line is now and then axis-aligned (ties on entry) and aimed through an anchor.
//  - ClassId: the class-id static 0xa51b10 against GetClassId.
// Also reached through the factory, in effect through callers (db/status/cLooseOctreePartitionNode.jsonl): 0xa514a0
// 0xa52670 0xa535d0 0xa50cd0 0xa50d60 0xa50d40 0xa50e20 0xa50ce0 0xa50dc0 0xa51130 0xa51090 0xa51150 0xa510b0 0xa510f0
// 0xa51080 0xa513e0 0xa51440 0xa51420 0xa51480 0xa51400 0xa51460 0xa52720 0xa51b20 0xa51b60 0xa528a0 0xa529e0 0xa52990
// 0xa51800 0xa51920 0xa51690 0xa51770 0xa516b0 0xa514d0 0xa515b0, and the adjustor thunks 0xa526a2 0xa52785 0xa52775.
// Floats are compared bit-exact; the geometry code (nGZCompGeom) is original code on both sides, on our arguments.
//
// The octree's pools (cells: 0x10f620c, 0x3c-byte blocks on 4 KB pages; items: 0x10f6218, 0x10-byte blocks on 2 KB pages)
// point at regions of the test's own whose NewPage / FreePage slots are fakes; each side starts each seed from the same
// bytes, so blocks (and the cell and item pointers the nodes hold) compare as they are.
//
// Left out of the comparison: the vtable words 0 and 2 (each side's own); in the pilot, mHalf (words 40-42), which the
// constructor 0xa50ef0 never writes (slot 88 sets it), so a fresh node holds what its allocator left; in the content
// tests, the vector words (43-48: the free slots' and the sub-partitions' begin / end / capacity, heap addresses), whose
// sizes, capacities and elements are noted instead.
#include "sg_transform_pilot.h"

#include "misc/UtilitiesC.h"
#include "rz/RZFixedAllocator.h"
#include "sg/cLooseOctreePartitionNode.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

// ---- the pools ----
template <uint32_t Rva, uint32_t Log2, uint32_t BlockBytes> struct Pool {
    static constexpr uint32_t kPage = 1u << Log2;
    static constexpr uint32_t kBlocks = (kPage - 16) / BlockBytes;
    static constexpr int kPages = 16;
    static inline uint8_t *region = nullptr;
    static inline int next = 0;
    static inline std::vector<uint32_t> freed;
    static inline void *vt[4] = {};
    static inline uint32_t saved[3] = {};
    static inline std::vector<uint32_t> fill; // the region's words as Reset left them

    static uint32_t *Words() { return GameAddress<uint32_t>(Rva); }
    static RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(region + uint32_t(i) * kPage); }
    // A page as its index (0xff: null, 0xfe: not one of ours).
    static uint32_t IndexOf(uint32_t p)
    {
        if (!p) return 0xff;
        const uint32_t d = p - P(region);
        return d < kPages * kPage && d % kPage == 0 ? d / kPage : 0xfe;
    }
    static void InitPage(RZFixedPage *p)
    {
        p->next = p;
        p->prev = p;
        p->used = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p);
        for (uint32_t k = 0; k < kBlocks; ++k) {
            void **block = reinterpret_cast<void **>(base + 16 + k * BlockBytes);
            *block = k + 1 < kBlocks ? base + 16 + (k + 1) * BlockBytes : nullptr;
        }
        p->freeList = base + 16;
    }
    // Slot 2: the next page of the region, its blocks chained from the front; slot 3: the page noted.
    static RZFixedPage *__fastcall NewPage(void *, void *)
    {
        if (next >= kPages) {
            drawfx::gErrors.emplace_back("octree pool: out of pages");
            return nullptr;
        }
        RZFixedPage *p = Page(next++);
        InitPage(p);
        return p;
    }
    static void __fastcall FreePage(void *, void *, RZFixedPage *p) { freed.push_back(IndexOf(P(p))); }

    static void Open()
    {
        std::memcpy(saved, Words(), sizeof(saved));
        region = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPages * kPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        fill.assign(kPages * kPage / 4, 0);
        vt[2] = reinterpret_cast<void *>(&NewPage);
        vt[3] = reinterpret_cast<void *>(&FreePage);
    }
    static void Close()
    {
        std::memcpy(Words(), saved, sizeof(saved));
        VirtualFree(region, 0, MEM_RELEASE);
        region = nullptr;
    }
    // Random bytes everywhere, then page 0 the current ring of one (its blocks chained), no full ring.
    static void Reset(uint32_t seed)
    {
        std::mt19937 rng(seed ^ Rva);
        for (uint32_t i = 0; i < kPages * kPage; i += 4) *reinterpret_cast<uint32_t *>(region + i) = fill[i / 4] = rng();
        next = 1;
        freed.clear();
        InitPage(Page(0));
        Words()[0] = P(vt);
        Words()[1] = P(Page(0));
        Words()[2] = 0;
    }
    // The rings, the pages handed out and freed, and every word of the pages handed out (pointers normalised). A word
    // still holding Reset's random fill (never written, or a freed block's stale bytes) is the same data on both sides
    // and is logged as it is: through Norm it would read as a pointer on the one side where it happens to fall inside a
    // registered block (OP, 2026-10-28: content long seed 18, ours 0x09d1eb3f, the original's 0x7c0000bf).
    static void Note(const char *label)
    {
        drawfx::Call c;
        c.note = label;
        c.words = {IndexOf(Words()[1]), IndexOf(Words()[2]), uint32_t(next), uint32_t(freed.size())};
        for (uint32_t f : freed) c.words.push_back(f);
        for (int i = 0; i < next; ++i) {
            const uint32_t *page = reinterpret_cast<const uint32_t *>(Page(i));
            c.words.push_back(IndexOf(page[0]));
            c.words.push_back(IndexOf(page[1]));
            c.words.push_back(page[2] ? page[2] - P(region) : 0xffffffffu);
            c.words.push_back(page[3]);
            const uint32_t *const filled = fill.data() + uint32_t(i) * (kPage / 4);
            for (uint32_t k = 4; k < kPage / 4; ++k)
                c.words.push_back(page[k] == filled[k] ? page[k] : drawfx::Norm(page[k]));
        }
        drawfx::gLog.push_back(std::move(c));
    }
};
using CellPool = Pool<cLooseOctreePartitionNode::kCellPool, 12, 0x3c>;
using ItemPool = Pool<cLooseOctreePartitionNode::kItemPool, 11, 0x10>;
static_assert(CellPool::kBlocks == 68 && ItemPool::kBlocks == 127);

struct Pools {
    Pools()
    {
        CellPool::Open();
        ItemPool::Open();
    }
    ~Pools()
    {
        ItemPool::Close();
        CellPool::Close();
    }
    Pools(const Pools &) = delete;
    Pools &operator=(const Pools &) = delete;
    static void Reset(uint32_t seed)
    {
        CellPool::Reset(seed);
        ItemPool::Reset(seed);
    }
};

constexpr float kSizes[] = {64.0f, 100.0f, 37.5f, 256.0f};

// ---- the pilot ----
struct Cfg {
    using Interface = cILooseOctreePartitionNode;
    static constexpr uint32_t kFactory = 0xa51ab0;
    static constexpr uint32_t kSize = cLooseOctreePartitionNode::kSize;
    static constexpr int kLevel = 3;
    static constexpr uint32_t kIid = cILooseOctreePartitionNode::kIid;
    static constexpr uint32_t kEventCell = cLooseOctreePartitionNodeBehaviorTraits::kEventManagerCell;
    static constexpr uint64_t kSkipWords = 0x7ull << 40; // mHalf: see the top
    using Scope = Pools;
    static void Setup(uint32_t seed) { Pools::Reset(seed); }
    static void Prepare(cICompositionTreeNode *n, std::mt19937 &rng)
    {
        static_cast<cILooseOctreePartitionNode *>(n)->SetSize(kSizes[rng() % 4]);
    }
    static cICompositionTreeNode *Create() { return cLooseOctreePartitionNode::Create(); }
};
using PilotP = sgtp::Pilot<Cfg>;

// ---- the content fakes ----
#define OCT_UNKNOWN                                                                                                    \
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define OCT_TRANSFORM_BOUNDED                                                                                          \
    {1, Ret::Int, true, "SetLimit", "u"}, {2, Ret::Int, true, "SetTransformParts", "F3F4"},                         \
        {1, Ret::Int, true, "SetTransform", "F7"}, {1, Ret::Int, true, "SetTranslation", "F3"},                     \
        {1, Ret::Int, true, "SetRotation", "F4"}, {1, Ret::Int, true, "SetBoundFlag", "b"},                         \
        {2, Ret::Int, true, "SetHolder", "iu"}, {0, Ret::Int, true, "GetHolderConst", ""},                          \
        {0, Ret::Int, true, "GetHolder", ""}, {0, Ret::Int, true, "Slot70", ""},                                    \
        {2, Ret::Int, true, "GetBoundBoxIn", "iF13"}, {1, Ret::Int, true, "GetBoundBox", "i"},                      \
        {2, Ret::Int, true, "Slot73", "iu"}, {1, Ret::Int, true, "IntersectsLinear", "i"},                          \
        {2, Ret::Int, true, "FirstHitLinear", "F1i"}, {0, Ret::Int, true, "GetHolderData", ""}
inline constexpr Slot kContentTail[] = {
    OCT_TRANSFORM_BOUNDED,                                                             // 61-76
    {1, Ret::Int, true, "AddLayer", "u"},           {1, Ret::Int, true, "AddLayerBits", "u"},    // 77, 78
    {1, Ret::Int, true, "RemoveLayer", "u"},        {1, Ret::Int, true, "RemoveLayerBits", "u"}, // 79, 80
    {1, Ret::Int, true, "SetLayerTag", "u"},             {2, Ret::Int, true, "Draw", "iu"},           // 81, 82
    {1, Ret::Int, true, "DeletingDestructor", "u"},                                               // 83
};
inline constexpr Slot kPartitionTail[] = {
    OCT_TRANSFORM_BOUNDED,                                                                  // 61-76
    {0, Ret::Int, true, "Clear", ""},              {1, Ret::Int, true, "RemovePartition", "i"},      // 77, 78
    {1, Ret::Int, true, "RemoveNode", "i"},        {1, Ret::Int, true, "RemovePartitionFalse", "i"}, // 79, 80
    {1, Ret::Int, true, "AddPartition", "i"},      {1, Ret::Int, true, "UpdateNode", "i"},           // 81, 82
    {1, Ret::Int, true, "InsertNode", "i"},        {3, Ret::Int, true, "QueryShape", "iiu"},         // 83, 84
    {2, Ret::Int, true, "QueryLinear", "ii"},      {4, Ret::Int, true, "RayCast", "F1iii"},          // 85, 86
    {1, Ret::Int, true, "Cull", "i"},              {1, Ret::Int, true, "DeletingDestructor", "u"},   // 87, 88
};
inline constexpr auto kContentSlots = sgfx::detail::ExtendNode<84>(kContentTail);
inline constexpr auto kPartitionSlots = sgfx::detail::ExtendNode<89>(kPartitionTail);
inline constexpr drawfx::Iface kContent{"RenderableNode", 84, kContentSlots.data()};
inline constexpr drawfx::Iface kPartition{"PartitionNode", 89, kPartitionSlots.data()};

constexpr std::array<Slot, 20> MakeShapeSlots()
{
    std::array<Slot, 20> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[19] = {3, Ret::Int, true, "Slot19", "uF3F13"}; // (arg, the cuboid's half extents, the frame)
    return a;
}
inline constexpr auto kShapeSlots = MakeShapeSlots();
inline constexpr drawfx::Iface kShape{"Bound", 20, kShapeSlots.data()};
constexpr std::array<Slot, 32> MakeLinearSlots()
{
    std::array<Slot, 32> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[4] = {0, Ret::Int, true, "GetClassID", ""};
    a[17] = {2, Ret::Int, true, "IntersectsCuboidIn", "F3F13"};
    a[18] = {2, Ret::Int, true, "IntersectsBoundIn", "G7F13"}; // a cTranslatedCuboidShape: (vtable skipped) half, centre, 0
    // (t, ...): t an out only (0xa51f10 hands it an unset local), so not read.
    a[28] = {3, Ret::Int, true, "FirstHitCuboidIn", "iF3F13"};
    a[30] = {1, Ret::Int, true, "GetDirection", "i"}; // hidden out
    a[31] = {1, Ret::Int, true, "GetOrigin", "i"};    // hidden out
    return a;
}
inline constexpr auto kLinearSlots = MakeLinearSlots();
inline constexpr drawfx::Iface kLinear{"Linear", 32, kLinearSlots.data()};
inline constexpr Slot kStrategySlots[] = {OCT_UNKNOWN, {3, Ret::Int, true, "VisitShape", "iiu"},
                                          {2, Ret::Int, true, "VisitLinear", "ii"}};
inline constexpr drawfx::Iface kStrategy{"QueryStrategy", 5, kStrategySlots};
inline constexpr Slot kNearerSlots[] = {OCT_UNKNOWN, {4, Ret::Int, true, "Visit", "iF1ii"}};
inline constexpr drawfx::Iface kNearer{"NearerStrategy", 4, kNearerSlots};
inline constexpr Slot kFilterSlots[] = {OCT_UNKNOWN, {2, Ret::Int, true, "Accept", "ii"}};
inline constexpr drawfx::Iface kFilter{"VisibilityFilter", 4, kFilterSlots};
#undef OCT_UNKNOWN
#undef OCT_TRANSFORM_BOUNDED

// A fake's state (Fake::owner): the holder words, a box (centre, half extent), a line (origin = centre, direction), and
// the answers its yes / no slots give, one bit per call. A raw box (min, max) is answered as is, whatever the frame.
struct State {
    uint32_t holder, data;
    float center[3], extent, dir[3];
    uint32_t bits;
    float box[6];
    bool raw;
};
State &St(Fake &f) { return *static_cast<State *>(f.owner); }
uint32_t Answer(Fake &f)
{
    State &s = St(f);
    const uint32_t b = s.bits & 1;
    s.bits = (s.bits >> 1) | (b << 31);
    return b;
}
uint32_t AnswerSlot(Fake &f, const uint32_t *) { return Answer(f); }
uint32_t SetHolder(Fake &f, const uint32_t *a)
{
    St(f).holder = a[0];
    St(f).data = a[1];
    return 0;
}
uint32_t GetHolder(Fake &f, const uint32_t *) { return St(f).holder; }
uint32_t GetData(Fake &f, const uint32_t *) { return St(f).data; }
// 71 (out, frame): the box around the centre placed by the frame (matrix rows, then the offset), every float written.
uint32_t BoxIn(Fake &f, const uint32_t *a)
{
    const State &s = St(f);
    const auto *frame = reinterpret_cast<const AffineTransform3 *>(uintptr_t(a[1]));
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    if (s.raw) {
        std::memcpy(out, s.box, sizeof s.box);
        return a[0];
    }
    for (int i = 0; i < 3; ++i) {
        const float p = frame->mMatrix[i * 3] * s.center[0] + frame->mMatrix[i * 3 + 1] * s.center[1] +
                        frame->mMatrix[i * 3 + 2] * s.center[2] + frame->mOffset[i];
        out[i] = p - s.extent;
        out[3 + i] = p + s.extent;
    }
    return a[0];
}
// The line: its class id (data), the origin (centre) and direction, and a hit at `extent`.
uint32_t LinearClass(Fake &f, const uint32_t *) { return St(f).data; }
uint32_t LinearOrigin(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), St(f).center, 12);
    return a[0];
}
uint32_t LinearDirection(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), St(f).dir, 12);
    return a[0];
}
uint32_t LinearFirstHit(Fake &f, const uint32_t *a)
{
    *reinterpret_cast<float *>(uintptr_t(a[0])) = St(f).extent;
    return Answer(f);
}
constexpr unsigned kContents = 16, kParts = 3, kAnchors = 3;
constexpr uint32_t kViewerTag = 0x7c000000;
constexpr uint32_t kDrawn = 0x420, kHidden = 0x4000;

// Anchors (in units of the octree's size) gather boxes into shared cells and sibling cells: a0 anywhere, a1 and a2
// a fraction of the size away across cell boundaries; each has a depth class for its boxes.
struct World {
    Fake content[kContents], parts[kParts], shape, linear, strategy, nearer, filter;
    State states[kContents + kParts + 5];
    alignas(16) uint8_t viewer[0x1d0];
    float size;
    float anchor[kAnchors][3];
    uint32_t anchorDepth[kAnchors];
};
World gw;

// The nearer strategy (context, t, node, linear): when its answer says so the node is nearer: t halved, a hit at the
// line's origin (t 0: the walk stops after a child that starts past it), or t cut to where the line passes the node's
// centre.
uint32_t NearerVisit(Fake &f, const uint32_t *a)
{
    float *t = reinterpret_cast<float *>(uintptr_t(a[1]));
    if (!Answer(f)) return 0;
    if (Answer(f)) {
        *t *= 0.5f;
        return 0;
    }
    if (Answer(f)) {
        *t = 0.0f;
        return 0;
    }
    const auto *node = reinterpret_cast<Fake *>(uintptr_t(a[2]));
    if (node < gw.content || node >= gw.content + kContents) return 0;
    const State &n = St(*const_cast<Fake *>(node)), &l = St(gw.linear);
    float along = 0.0f, length = 0.0f;
    for (int i = 0; i < 3; ++i) {
        along += (n.center[i] - l.center[i]) * l.dir[i];
        length += l.dir[i] * l.dir[i];
    }
    const float at = along / length;
    if (at < *t) *t = at;
    return 0;
}

cILooseOctreePartitionNode *Octree() { return static_cast<cILooseOctreePartitionNode *>(PilotP::w.nodes[0]); }

float Coord(std::mt19937 &rng, float size) { return float(int(rng() % 2001) - 1000) / 1000.0f * size * 0.5f; }

// A box whose bounding sphere meets the root's (+, +, +) child's loose box exactly on x (the fit test's >=), with
// diagonals for which the radius's sum order rounds differently where one is found: computed as the octree does
// (edge = size / 2, centre = d * 0.5 + min, radius = sqrt of the squared diagonal, halved).
bool BoundaryBox(State &s, std::mt19937 &rng, float size)
{
    const float edge = size / 2.0f, half = edge * 0.5f, extent = (edge + edge) * 0.5f;
    const float rhs = half - extent;
    float fallback[6];
    bool found = false;
    for (int attempt = 0; attempt < 48; ++attempt) {
        float box[6];
        float d[3];
        for (int i = 1; i < 3; ++i) {
            const float want = half * (1.2f + float(rng() % 1000) / 1000.0f * 0.7f);
            box[i] = half - want * 0.5f;
            box[3 + i] = box[i] + want;
            d[i] = box[3 + i] - box[i];
        }
        const float want0 = half * (1.2f + float(rng() % 1000) / 1000.0f * 0.7f);
        const float guess = float(std::sqrt(double((want0 * want0 + d[1] * d[1]) + d[2] * d[2]))) * 0.5f;
        float lo = rhs + guess - want0 * 0.5f;
        for (int step = 0; step < 256; ++step) {
            box[0] = lo;
            box[3] = lo + want0;
            d[0] = box[3] - box[0];
            const float c0 = d[0] * 0.5f + box[0];
            const float r = float(std::sqrt(double((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]))) * 0.5f;
            const float other = float(std::sqrt(double(d[0] * d[0] + (d[1] * d[1] + d[2] * d[2])))) * 0.5f;
            const float gap = c0 - r;
            if (gap == rhs) {
                if (other > r) {
                    found = true;
                    std::memcpy(fallback, box, sizeof box);
                    attempt = 48;
                } else if (!found) {
                    found = true;
                    std::memcpy(fallback, box, sizeof box);
                }
                break;
            }
            lo = std::nextafter(lo, gap < rhs ? 1.0e9f : -1.0e9f);
        }
    }
    if (!found) return false;
    std::memcpy(s.box, fallback, sizeof fallback);
    for (int i = 0; i < 3; ++i) s.center[i] = (fallback[3 + i] - fallback[i]) * 0.5f + fallback[i];
    s.extent = 0.0f;
    s.raw = true;
    return true;
}

// A box somewhere in (or now and then partly out of) the octree, of a size that lands it at some depth; now and then
// near an anchor (in a cell with others), or on the fit test's boundary.
void PlaceBox(State &s, std::mt19937 &rng, float size)
{
    s.raw = false;
    const uint32_t pick = rng() % 15;
    if (pick < 3 && BoundaryBox(s, rng, size)) return;
    if (pick >= 10) {
        const unsigned a = pick % kAnchors;
        for (int i = 0; i < 3; ++i) s.center[i] = gw.anchor[a][i] * size + float(int(rng() % 9) - 4) * size / 4096.0f;
        // at the anchor's depth (a cell shared with others), or at any depth on the anchor's path
        const uint32_t depth = rng() % 2 ? gw.anchorDepth[a] : rng() % 8;
        s.extent = size / float(4u << depth) * float(1 + rng() % 2) * 0.5f;
        return;
    }
    for (float &c : s.center) c = Coord(rng, size * (rng() % 8 ? 1.0f : 1.25f));
    s.extent = size / float(4u << (rng() % 10)) * float(1 + rng() % 3);
}
// The line's direction (now and then along an axis, where sibling cells tie on entry) and origin (now and then aimed
// through an anchor).
void AimLine(State &l, std::mt19937 &rng, float size)
{
    for (float &d : l.dir) d = float(int(rng() % 2001) - 1000) / 250.0f;
    if (rng() % 3 == 0) {
        const unsigned axis = rng() % 3;
        for (unsigned i = 0; i < 3; ++i) l.dir[i] = i == axis ? float(1 + rng() % 4) * (rng() % 2 ? 1.0f : -1.0f) : 0.0f;
    }
    // Through two anchors (cells in sibling subtrees on one line), from outside the octree.
    const unsigned from = rng() % kAnchors, to = (from + 1 + rng() % (kAnchors - 1)) % kAnchors;
    float across[3], span = 0.0f;
    for (int i = 0; i < 3; ++i) {
        across[i] = (gw.anchor[to][i] - gw.anchor[from][i]) * 8.0f;
        span += across[i] * across[i];
    }
    if (rng() % 2 && span > 0.0f) {
        const float back = 2.0f * size / std::sqrt(span);
        for (int i = 0; i < 3; ++i) {
            l.dir[i] = across[i];
            l.center[i] = gw.anchor[from][i] * size - across[i] * back;
        }
    } else if (rng() % 2) {
        const unsigned a = rng() % kAnchors;
        float back = float(1 + rng() % 4) * 0.5f;
        if (rng() % 2) { // from outside the octree: every child starts past 0
            float length = 0.0f;
            for (float d : l.dir) length += d * d;
            if (length > 0.0f) back = 2.0f * size / std::sqrt(length);
        }
        for (int i = 0; i < 3; ++i) l.center[i] = gw.anchor[a][i] * size - l.dir[i] * back;
    } else {
        for (float &p : l.center) p = Coord(rng, size);
    }
}
void MakeAnchors(std::mt19937 &rng)
{
    for (int i = 0; i < 3; ++i) gw.anchor[0][i] = float(int(rng() % 801) - 400) / 1000.0f;
    for (unsigned a = 1; a < kAnchors; ++a) {
        for (int i = 0; i < 3; ++i) {
            const float step = float(rng() % 2 ? 1 : -1) / float(4u << (rng() % 4));
            gw.anchor[a][i] = std::clamp(gw.anchor[0][i] + (rng() % 2 ? step : 0.0f), -0.45f, 0.45f);
        }
    }
    for (uint32_t &d : gw.anchorDepth) d = rng() % 6;
}
void InitState(Fake &f, State &s, std::mt19937 &rng, float size)
{
    f.owner = &s;
    s = State{};
    PlaceBox(s, rng, size);
    for (float &d : s.dir) d = float(int(rng() % 2001) - 1000) / 250.0f;
    s.bits = uint32_t(rng());
}
template <const drawfx::Iface &I> void MakeBounded(Fake &f, State &s, uint32_t id, std::mt19937 &rng, float size)
{
    sgfx::MakeFlagNode<I>(f, id);
    InitState(f, s, rng, size);
    f.value[sgfx::kFlagsWord] = (rng() % 4 ? kDrawn : 0x20) | (rng() % 5 ? 0 : kHidden) | (rng() % 2 << 13);
    f.On(67, &SetHolder);
    f.On(68, &GetHolder);
    f.On(69, &GetHolder);
    f.On(71, &BoxIn);
    f.On(76, &GetData);
}

// A rotation from a few unit quaternions (x, y, z, w) and a translation within the octree.
cRigidTransform PlacedRigid(std::mt19937 &rng, float size)
{
    static constexpr float kRotations[][4] = {{0, 0, 0, 1},
                                              {0, 0, 0.70710677f, 0.70710677f},
                                              {0.5f, 0.5f, 0.5f, 0.5f},
                                              {0, 0.38268343f, 0, 0.92387953f}};
    cRigidTransform t;
    for (int i = 0; i < 3; ++i) t.mValues[i] = Coord(rng, size) * 0.25f;
    const float *q = kRotations[rng() % 4];
    for (int i = 0; i < 4; ++i) t.mValues[3 + i] = q[i];
    return t;
}

void BuildContent(uint32_t seed, drawfx::Cells &cells)
{
    PilotP::Build(seed, cells);
    std::mt19937 rng(seed * 31u + 7u);
    gw.size = kSizes[rng() % 4];
    MakeAnchors(rng);
    unsigned k = 0;
    for (unsigned i = 0; i < kContents; ++i) MakeBounded<kContent>(gw.content[i], gw.states[k++], 120 + i, rng, gw.size);
    for (unsigned i = 0; i < kParts; ++i) {
        MakeBounded<kPartition>(gw.parts[i], gw.states[k++], 140 + i, rng, gw.size);
        for (unsigned s : {73u, 74u, 81u, 83u, 84u, 85u}) gw.parts[i].On(s, &AnswerSlot);
    }
    gw.shape.Reset<kShape>(150);
    InitState(gw.shape, gw.states[k++], rng, gw.size);
    gw.shape.On(19, &AnswerSlot);
    gw.linear.Reset<kLinear>(151);
    InitState(gw.linear, gw.states[k++], rng, gw.size);
    const uint32_t classes[] = {0x3ce57cc0, 0x3ce57f31, 0x3ce57d17};
    St(gw.linear).data = classes[rng() % 3];
    St(gw.linear).extent = float(rng() % 64) / 8.0f;
    AimLine(St(gw.linear), rng, gw.size);
    gw.linear.On(4, &LinearClass);
    gw.linear.On(17, &AnswerSlot);
    gw.linear.On(18, &AnswerSlot);
    gw.linear.On(28, &LinearFirstHit);
    gw.linear.On(30, &LinearDirection);
    gw.linear.On(31, &LinearOrigin);
    gw.strategy.Reset<kStrategy>(152);
    InitState(gw.strategy, gw.states[k++], rng, gw.size);
    gw.strategy.On(3, &AnswerSlot);
    gw.strategy.On(4, &AnswerSlot);
    gw.nearer.Reset<kNearer>(153);
    InitState(gw.nearer, gw.states[k++], rng, gw.size);
    gw.nearer.On(3, &NearerVisit);
    gw.filter.Reset<kFilter>(154);
    InitState(gw.filter, gw.states[k++], rng, gw.size);
    gw.filter.On(3, &AnswerSlot);
    // The viewer: the filter at +0x20 (or none), the view at +0x184: a scale and an offset, a box of a few cells.
    std::memset(gw.viewer, 0, sizeof(gw.viewer));
    *reinterpret_cast<uint32_t *>(gw.viewer + cLooseOctreePartitionNode::kViewerFilterOffset) = rng() % 3 ? P(&gw.filter) : 0;
    float *view = reinterpret_cast<float *>(gw.viewer + cLooseOctreePartitionNode::kViewerViewOffset);
    const float scale = 8.0f / gw.size * float(1 + rng() % 4);
    view[0] = view[5] = view[10] = scale;
    view[15] = 1.0f;
    for (int i : {3, 7, 11}) view[i] = float(int(rng() % 201) - 100) / 100.0f;
    drawfx::AddRange(gw.viewer, sizeof(gw.viewer), kViewerTag);
}

// ---- notes ----
uint32_t W(const void *p, unsigned word) { return reinterpret_cast<const uint32_t *>(p)[word]; }
void NoteVector(const char *label, const void *node, unsigned word)
{
    const uint32_t begin = W(node, word), end = W(node, word + 1), capacity = W(node, word + 2);
    drawfx::Call c;
    c.note = label;
    c.words = {(end - begin) / 4, (capacity - begin) / 4};
    for (uint32_t p = begin; p != end; p += 4) c.words.push_back(drawfx::Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(p))));
    drawfx::gLog.push_back(std::move(c));
}
void NoteCells(const cLooseOctreePartitionNode::Cell *cell, unsigned depth)
{
    drawfx::Call c;
    c.note = "cell";
    c.words.push_back(depth);
    const uint32_t *w = reinterpret_cast<const uint32_t *>(cell);
    for (unsigned i = 0; i < 15; ++i) c.words.push_back(drawfx::Norm(w[i]));
    drawfx::gLog.push_back(std::move(c));
    if (depth > 12) return;
    for (const auto *child : cell->mChildren)
        if (child) NoteCells(child, depth + 1);
}
// The octree's words (but the vtables and vectors), its vectors, its cells from the root and its item list.
void NoteOctree(const char *label)
{
    const void *n = Octree();
    PilotP::w.nodes.NoteWords(label, 0b101ull | (0x3full << 43));
    NoteVector("free slots", n, 43);
    NoteVector("partitions", n, 46);
    if (const auto *root = reinterpret_cast<const cLooseOctreePartitionNode::Cell *>(uintptr_t(W(n, 49))))
        NoteCells(root, 0);
    unsigned guard = 0;
    for (uint32_t item = W(n, 0xcc / 4 + 2); item && guard < 64; ++guard) {
        const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(item));
        drawfx::Note("item", {drawfx::Norm(item), drawfx::Norm(w[0]), drawfx::Norm(w[1]), drawfx::Norm(w[2]), drawfx::Norm(w[3])});
        item = w[2];
    }
}
void NoteFakes()
{
    for (unsigned i = 0; i < kContents + kParts; ++i) {
        Fake &f = i < kContents ? gw.content[i] : gw.parts[i - kContents];
        drawfx::Note("fake", {f.id, uint32_t(f.refs), drawfx::Norm(St(f).holder), drawfx::Norm(St(f).data),
                              f.value[sgfx::kFlagsWord]});
    }
}

bool Holds(Fake &f) { return St(f).holder == P(static_cast<cIBoundedNode *>(Octree())); }

// ---- the steps ----
void Step(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    cILooseOctreePartitionNode *o = Octree();
    const unsigned op = rng() % 18;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    Fake &c = gw.content[rng() % kContents];
    auto *cn = reinterpret_cast<cIBoundedNode *>(&c);
    Fake &s = gw.parts[rng() % kParts];
    auto *sp = reinterpret_cast<cIPartitionNode *>(&s);
    auto *shape = reinterpret_cast<nGZCompGeom::cIBound *>(&gw.shape);
    auto *linear = reinterpret_cast<nGZCompGeom::cILinear3 *>(&gw.linear);
    switch (op) {
    case 0:
    case 1:
    case 2: // insert a node not held (the game never inserts one twice), or move one and update it
        if (!St(c).holder) {
            drawfx::Note("insert", uint8_t(o->InsertNode(cn)));
        } else if (Holds(c)) {
            if (rng() % 2) PlaceBox(St(c), rng, gw.size);
            drawfx::Note("update", uint8_t(o->UpdateNode(cn)));
        }
        break;
    case 3: // remove
        if (Holds(c)) drawfx::Note("remove", uint8_t(o->RemoveNode(cn)));
        break;
    case 4: // a sub-partition in or out (80: 78, then false)
        if (!St(s).holder) {
            drawfx::Note("add partition", uint8_t(o->AddPartition(sp)));
        } else if (Holds(s)) {
            if (rng() % 2)
                o->RemovePartition(sp);
            else
                drawfx::Note("remove partition false", uint8_t(o->RemovePartitionFalse(sp)));
        }
        break;
    case 5:
        if (rng() % 3 == 0) o->Clear();
        break;
    case 6:
    case 7:
        drawfx::Note("query shape", o->QueryShape(reinterpret_cast<cIIntersectionQueryStrategy *>(&gw.strategy), shape,
                                                  rng() % 4));
        break;
    case 8:
        drawfx::Note("query linear",
                     o->QueryLinear(reinterpret_cast<cIIntersectionQueryStrategy *>(&gw.strategy), linear));
        break;
    case 9:
    case 10: // a few rays, the line re-aimed between them
        for (int ray = 0; ray < 4; ++ray) {
            const float starts[] = {1.0e30f, 1.0e30f, 0.5f, 2.0f, float(rng() % 100) / 3.0f};
            float t = starts[rng() % 5];
            o->RayCast(&t, &gw.viewer[0x40], reinterpret_cast<cINearerIntersectionQueryStrategy *>(&gw.nearer), linear);
            drawfx::Note("ray", std::bit_cast<uint32_t>(t));
            AimLine(St(gw.linear), rng, gw.size);
        }
        break;
    case 11:
    case 12: o->Cull(gw.viewer); break;
    case 13: // the bound slots
    {
        sgtx::NoteFloats("box", o->GetBoundBox().mMin, 6);
        const cTransform34 frame = sgtx::RandomFrame(rng);
        sgtx::NoteFloats("box in", o->GetBoundBoxIn(&frame).mMin, 6);
        drawfx::Note("73", uint8_t(o->IntersectsBound(P(shape), rng() % 4)));
        drawfx::Note("74", uint8_t(o->IntersectsLinear(linear)));
        float t = float(rng() % 64) / 4.0f;
        const bool hit = o->FirstHitLinear(&t, linear);
        drawfx::Note("75", {uint8_t(hit), std::bit_cast<uint32_t>(t)});
        break;
    }
    case 14: // a new transform (the octree's frame follows through slot 60 when the cache is refreshed)
    {
        const cRigidTransform t = PlacedRigid(rng, gw.size);
        o->SetTransform(&t);
        if (rng() % 2) {
            cRigidTransform u = PlacedRigid(rng, gw.size);
            o->OnContentChanged(P(&u), rng(), rng());
        }
        break;
    }
    case 15:
        if (rng() % 4 == 0) {
            gw.size = kSizes[rng() % 4];
            o->SetSize(gw.size);
        }
        break;
    case 16: { // the fakes change: flags, the line, the answers
        c.value[sgfx::kFlagsWord] ^= (rng() % 2 ? kHidden : 0) | (rng() % 2 ? (1u << 13) : 0) | (rng() % 3 ? 0 : 0x400);
        s.value[sgfx::kFlagsWord] &= ~(1u << 13);
        State &l = St(gw.linear);
        AimLine(l, rng, gw.size);
        const uint32_t classes[] = {0x3ce57cc0, 0x3ce57f31, 0x3ce57d17};
        l.data = classes[rng() % 3];
        St(gw.strategy).bits ^= rng();
        break;
    }
    case 17: { // the kinds and the interfaces
        for (uint32_t iid : {cIPartitionNode::kIid, cILooseOctreePartitionNode::kIid, cIBoundedNode::kIid})
            drawfx::Note("kind", {iid, uint8_t(o->IsKindOf(iid))});
        void *out = nullptr;
        drawfx::Note("query", {uint8_t(o->QueryInterface(rng() % 2 ? cIPartitionNode::kIid : cILooseOctreePartitionNode::kIid, &out)),
                               drawfx::Norm(P(out))});
        if (out) o->Release();
        break;
    }
    }
    NoteOctree("after");
}

void Content(difftest::Context &ctx, const char *what, unsigned seeds, int steps)
{
    PilotP::Registries registries;
    sgfx::CheckSides(ctx, what, seeds, BuildContent, [&](bool original, std::mt19937 &rng) {
        PilotP::w.nodes.Make(PilotP::Maker(ctx, original), 1, Cfg::kSize);
        Octree()->SetSize(gw.size);
        if (rng() % 2) {
            const cRigidTransform t = PlacedRigid(rng, gw.size);
            Octree()->SetTransform(&t);
        }
        NoteOctree("built");
        for (int k = 0; k < steps; ++k) Step(ctx, k, rng);
        NoteFakes();
        CellPool::Note("cells");
        ItemPool::Note("items");
        PilotP::ReleaseAll(); // the destructor: Clear, then the root cell
        NoteFakes();
        CellPool::Note("cells released");
        ItemPool::Note("items released");
    });
    PilotP::w.manager.Drop();
}

// The record the octree's behaviour slot 14 copies for an unknown event (0x106b418, this instantiation's) is all zeros
// in the unstarted image, so a wrong record address would read zeros as well: EventRecord fills it with a pattern
// (restored after), as sg_resource_node.cpp does for its instantiation's.
struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106b418;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x0c7ee000 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x0c7ee000 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

} // namespace

DIFF_TEST(SG_LooseOctree_Factory, Cfg::kFactory) { PilotP::Factory(ctx); }
// The event registry's slots on an octree: the count, and the records of a registered event and of one that is not
// (0x5678: the missing record, patterned).
DIFF_TEST(SG_LooseOctree_EventRecord, Cfg::kFactory)
{
    PilotP::Registries registries;
    MissingRecordScope missing;
    sgfx::CheckSides(ctx, "event record", 8, PilotP::Build, [&](bool original, std::mt19937 &rng) {
        PilotP::w.nodes.Make(PilotP::Maker(ctx, original), 1, Cfg::kSize);
        cICompositionTreeNode *n = PilotP::w.nodes[0];
        drawfx::Note("event count", n->GetEventCount());
        for (int k = 0; k < 4; ++k) {
            const nGZEvent::cEventAttribute a = n->GetEventAttribute(rng() % 2 ? cGraphConnChanged::kEventId : 0x5678u);
            drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        }
        PilotP::ReleaseAll();
    });
    PilotP::w.manager.Drop();
}
// The class-id static 0xa51b10 (cdecl, no arguments, no side effects: one register load and a return).
DIFF_TEST(SG_LooseOctree_ClassId, 0xa51b10)
{
    const uint32_t original = ctx.Original<uint32_t(__cdecl *)()>(0xa51b10)();
    const uint32_t ours = cLooseOctreePartitionNode::GetClassId();
    ctx.cases++;
    if (original != ours) ctx.Fail("class id: original %08x, ours %08x", original, ours);
}
DIFF_TEST(SG_LooseOctree_Slots, Cfg::kFactory) { PilotP::Slots(ctx); }
DIFF_TEST(SG_LooseOctree_Persist, 0xa526b0) { PilotP::Persist(ctx); }
DIFF_TEST(SG_LooseOctree_Clone, Cfg::kFactory) { PilotP::Clone(ctx); }
DIFF_TEST(SG_LooseOctree_Events, Cfg::kFactory) { PilotP::Events(ctx); }
DIFF_TEST(SG_LooseOctree_Content, 0xa520e0) { Content(ctx, "content", 96, 40); }
DIFF_TEST(SG_LooseOctree_ContentLong, 0xa51cc0) { Content(ctx, "content long", 24, 120); }
