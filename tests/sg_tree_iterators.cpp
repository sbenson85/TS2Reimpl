// Differential test of the composition Impl's slot-43 / 44 builders and the pre- / postorder tree iterators (lane OP,
// scenegraph N0 part 3 step 4; src/sg/cCompositionTreeIterators.h): the game's builders 0xa37fd0 (const) and 0xa37e40
// (their iterators carry the game's eight vtables) against ours, on random trees of node fakes.
//
//   SG_TreeIterators  per seed a tree of 1-40 node fakes (random, a deep chain past the 16 inline stack entries, or
//       wide), each node's HasChildren (slot 52) its first child, now and then false for a node with children; a filter
//       fake (or none: the default filter) answering 0 / 1 / 2 per node, per seed fixed or varying per question. Then
//       1-3 builder calls (const or not, order 0 / 1 / random, on the root or another node), each range noted, and
//       8-60 random operations on the held iterators through their slots: Get, IsEqual, Next (only off the end: Next
//       at the end is not defined), whole walks to the end (each node noted), Clone, QueryInterface (iid 1, another, a
//       null out), AddRef, Release while more than one reference is held. After each operation the touched
//       iterator's words are noted (count, filter, depth, the 16 inline entries, the vector's size, capacity and
//       entries; the vtable word skipped: the classes differ by design). Every slot call on the fakes is in the log in
//       order (the nodes' AddRef / Release, GetNextSibling 38, GetFirstChild 40, HasChildren 52, the filter's
//       Classify). At the end every reference is released (the heap blocks freed noted by size) and the four pools'
//       pages are noted word by word.
//
// What it covers (all in effect through the two builders' hooks): the builders 0xa37fd0 / 0xa37e40; per template class
// (non-const pre, const pre, non-const post, const post) the end constructors 0xa362f0 / 0xa36780 / 0xa358a0 /
// 0xa35e60, the begin constructors 0xa36100 / 0xa36590 / 0xa35580 / 0xa35b40, Clone 0xa37570 / 0xa375d0 / 0xa374b0 /
// 0xa37510 with the copy constructors 0xa35f50 / 0xa363e0 / 0xa353d0 / 0xa35990, the destructors 0xa36ab0 / 0xa36ae0 /
// 0xa36a50 / 0xa36a80 and the deleting destructors 0xa37000 / 0xa37030 / 0xa36fa0 / 0xa36fd0 (the final releases of
// clones); the pooled classes' deleting destructors 0xa37110 / 0xa37190 / 0xa370d0 / 0xa37150, their pool
// allocations 0xa36c60 / 0xa36cc0 / 0xa36c30 / 0xa36c90 and frees 0xa36d70 / 0xa36e70 / 0xa36cf0 / 0xa36df0 (the pool
// notes); the shared slots IsEqual 0xa37760, Get 0xa37700, Next 0xa38a80 (preorder) and 0xa38840 (postorder); the
// entry vector's helpers 0xa36ef0 (an entry's assignment), 0xa39250 (push_back), 0xa35110 (the reallocating insert)
// and the entry's constructor 0xa36990 (the deep trees reach them).
#include "sgfx.h"

#include "game/game.h"
#include "rz/RZFixedAllocator.h"
#include "sg/cCompositionTreeIterators.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

// ---- the pools (the octree test's pattern: a test region, page fakes for slots 2 / 3) ----
constexpr uint32_t kPoolTag = 0x76000000;
constexpr uint32_t kDefaultFilter = 0xdef0f117; // either side's default filter, noted alike
template <uint32_t Rva, uint32_t Index> struct Pool {
    static constexpr uint32_t kLog2 = 14;
    static constexpr uint32_t kBlockBytes = 0x9c;
    static constexpr uint32_t kPage = 1u << kLog2;
    static constexpr uint32_t kBlocks = (kPage - 16) / kBlockBytes;
    static constexpr int kPages = 3;
    static inline uint8_t *region = nullptr;
    static inline int next = 0;
    static inline std::vector<uint32_t> freed;
    static inline void *vt[4] = {};
    static inline uint32_t saved[3] = {};

    static uint32_t *Words() { return GameAddress<uint32_t>(Rva); }
    static RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(region + uint32_t(i) * kPage); }
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
            void **block = reinterpret_cast<void **>(base + 16 + k * kBlockBytes);
            *block = k + 1 < kBlocks ? base + 16 + (k + 1) * kBlockBytes : nullptr;
        }
        p->freeList = base + 16;
    }
    static RZFixedPage *__fastcall NewPage(void *, void *)
    {
        if (next >= kPages) {
            drawfx::gErrors.emplace_back("iterator pool: out of pages");
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
        vt[2] = reinterpret_cast<void *>(&NewPage);
        vt[3] = reinterpret_cast<void *>(&FreePage);
    }
    static void Close()
    {
        std::memcpy(Words(), saved, sizeof(saved));
        VirtualFree(region, 0, MEM_RELEASE);
        region = nullptr;
    }
    // Random bytes everywhere, then page 0 the current ring of one (its blocks chained), no full ring; the region named.
    static void Reset(uint32_t seed)
    {
        std::mt19937 rng(seed ^ Rva);
        for (uint32_t i = 0; i < kPages * kPage; i += 4) *reinterpret_cast<uint32_t *>(region + i) = rng();
        next = 1;
        freed.clear();
        InitPage(Page(0));
        Words()[0] = P(vt);
        Words()[1] = P(Page(0));
        Words()[2] = 0;
        drawfx::AddRange(region, kPages * kPage, kPoolTag | (Index << 20));
    }
    // The rings, the pages handed out and freed, and every word of the pages handed out (pointers normalised; a freed
    // block keeps its iterator's words past the link, so this side's default filter is noted as kDefaultFilter).
    static void Note(const char *label, uint32_t defaultFilter)
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
            for (uint32_t k = 4; k < kPage / 4; ++k)
                c.words.push_back(page[k] == defaultFilter ? kDefaultFilter : drawfx::Norm(page[k]));
        }
        drawfx::gLog.push_back(std::move(c));
    }
};
using PrePool = Pool<cCompositionTreePreorderIterator::kPool, 0>;
using ConstPrePool = Pool<cConstCompositionTreePreorderIterator::kPool, 1>;
using PostPool = Pool<cCompositionTreePostorderIterator::kPool, 2>;
using ConstPostPool = Pool<cConstCompositionTreePostorderIterator::kPool, 3>;
static_assert(PrePool::kBlocks == 104);

struct Pools {
    Pools()
    {
        PrePool::Open();
        ConstPrePool::Open();
        PostPool::Open();
        ConstPostPool::Open();
    }
    ~Pools()
    {
        ConstPostPool::Close();
        PostPool::Close();
        ConstPrePool::Close();
        PrePool::Close();
    }
    Pools(const Pools &) = delete;
    Pools &operator=(const Pools &) = delete;
    static void Reset(uint32_t seed)
    {
        PrePool::Reset(seed);
        ConstPrePool::Reset(seed);
        PostPool::Reset(seed);
        ConstPostPool::Reset(seed);
    }
    static void Note(uint32_t defaultFilter)
    {
        PrePool::Note("pre pool", defaultFilter);
        ConstPrePool::Note("const pre pool", defaultFilter);
        PostPool::Note("post pool", defaultFilter);
        ConstPostPool::Note("const post pool", defaultFilter);
    }
};

// ---- the world ----
constexpr unsigned kMaxNodes = 40;
constexpr unsigned kHasChildrenWord = 7; // slot 52's answer (MakeNode leaves words 6 and 7 free)
constexpr uint32_t kFilterId = 700;

inline constexpr Slot kFilterSlots[] = {
    {1, Ret::Int, true, "Classify", "i"},
    {1, Ret::Int, false, "DeletingDestructor", "u"},
};
inline constexpr drawfx::Iface kFilter{"BranchFilter", 2, kFilterSlots};

struct World {
    Fake nodes[kMaxNodes];
    Fake filter;
    unsigned count = 0;
    uint8_t answer[kMaxNodes] = {}; // the filter's answer per node
    bool varying = false;           // answers change with each question
    uint32_t questions = 0;
} gw;

uint32_t Classify(Fake &, const uint32_t *a)
{
    const uint32_t q = gw.questions++;
    for (unsigned i = 0; i < gw.count; ++i) {
        if (a[0] != P(&gw.nodes[i])) continue;
        if (!gw.varying) return gw.answer[i];
        const uint32_t h = (i * 2654435761u) ^ (q * 40503u);
        const uint32_t r = (h >> 7) % 8;
        return r < 5 ? 0 : r < 7 ? 1 : 2;
    }
    return 0;
}

void Link(unsigned child, unsigned parent, std::array<unsigned, kMaxNodes> &lastChild)
{
    Fake &c = gw.nodes[child];
    Fake &p = gw.nodes[parent];
    sgfx::SetWord(c, sgfx::kParentWord, &p);
    if (lastChild[parent] != ~0u)
        sgfx::SetWord(gw.nodes[lastChild[parent]], sgfx::kNextWord, &c);
    else
        sgfx::SetWord(p, sgfx::kFirstChildWord, &c);
    lastChild[parent] = child;
}

// Returns the filter to hand the builders (null: the default).
void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11u);
    const unsigned shape = rng() % 3;
    gw.count = shape == 1 ? 17 + rng() % (kMaxNodes - 16) : 1 + rng() % kMaxNodes;
    std::array<unsigned, kMaxNodes> lastChild;
    lastChild.fill(~0u);
    for (unsigned i = 0; i < gw.count; ++i) {
        sgfx::MakeNode(gw.nodes[i], 100 + i);
        gw.nodes[i].On(52, &sgfx::detail::GetWord<kHasChildrenWord>);
        if (i == 0) continue;
        unsigned parent;
        switch (shape) {
        case 1: parent = rng() % 4 ? i - 1 : rng() % i; break; // deep: mostly a chain
        case 2: parent = rng() % (i < 2 ? i : 2); break;        // wide
        default: parent = rng() % i; break;
        }
        Link(i, parent, lastChild);
    }
    // HasChildren: the first child, now and then false for a node that has children (never true without one: the
    // postorder walk would then ask a null node).
    for (unsigned i = 0; i < gw.count; ++i) {
        Fake &n = gw.nodes[i];
        n.value[kHasChildrenWord] = n.value[sgfx::kFirstChildWord] != 0 && rng() % 8 != 0;
    }
    gw.filter.Reset<kFilter>(kFilterId);
    gw.filter.On(0, &Classify);
    gw.varying = rng() % 3 == 0;
    gw.questions = 0;
    const unsigned mix = rng() % 4; // 0: all visited; else 0 / 1 / 2 in proportions
    for (unsigned i = 0; i < gw.count; ++i) {
        const uint32_t r = rng() % 8;
        gw.answer[i] = mix == 0 ? 0 : r < 5 ? 0 : r < 7 ? 1 : 2;
    }
}

uint32_t Count(const cICompositionTreeNodeIterator *it) { return reinterpret_cast<const uint32_t *>(it)[1]; }

// The iterator's words: count, filter, depth, the 16 inline entries, the vector's size, capacity and entries.
void NoteIterator(const char *label, const cICompositionTreeNodeIterator *it, bool original)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(it);
    const uint32_t defaultFilter = original ? P(GameAddress<void>(0x1069ffc)) : P(DefaultBranchFilter());
    drawfx::Call c;
    c.note = label;
    c.words.push_back(w[1]);
    c.words.push_back(w[2] == defaultFilter ? kDefaultFilter : drawfx::Norm(w[2]));
    c.words.push_back(w[3]);
    for (unsigned k = 4; k < 0x24; ++k) c.words.push_back(drawfx::Norm(w[k]));
    const uint32_t first = w[0x24], last = w[0x25], end = w[0x26];
    c.words.push_back((last - first) / 8);
    c.words.push_back((end - first) / 8);
    for (uint32_t p = first; p < last; p += 4) c.words.push_back(drawfx::Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(p))));
    drawfx::gLog.push_back(std::move(c));
}

void NoteRange(const cCompositionTreeNodeRange &r, const cCompositionTreeNodeRange *result)
{
    drawfx::Note("range", {uint32_t(result == &r), Count(r.mCurrent), Count(r.mBegin), Count(r.mEnd),
                           uint32_t(r.mCurrent == r.mBegin), uint32_t(r.mBegin == r.mEnd)});
}

using BuildFn = cCompositionTreeNodeRange *(__thiscall *)(void *, cCompositionTreeNodeRange *, cICompositionTreeNode *,
                                                          uint32_t, cIBranchNodesStrategy *);

} // namespace

DIFF_TEST(SG_TreeIterators, 0xa37fd0)
{
    const BuildFn originalConst = ctx.Original<BuildFn>(0xa37fd0);
    const BuildFn original = ctx.Original<BuildFn>(0xa37e40);
    Pools pools;
    sgfx::CheckSides(
        ctx, "TreeIterators", 96,
        [](uint32_t seed, drawfx::Cells &) {
            Build(seed);
            Pools::Reset(seed);
        },
        [&](bool isOriginal, std::mt19937 &rng) {
            std::vector<cICompositionTreeNodeIterator *> held; // one entry per reference the test holds
            const bool useFilter = rng() % 4 != 0;
            cIBranchNodesStrategy *const filter = useFilter ? reinterpret_cast<cIBranchNodesStrategy *>(&gw.filter) : nullptr;
            const unsigned builds = 1 + rng() % 3;
            for (unsigned b = 0; b < builds; ++b) {
                const bool isConst = rng() % 2;
                const uint32_t orders[] = {0, 1, rng()};
                const uint32_t order = orders[rng() % 3];
                const unsigned at = rng() % 3 == 0 ? rng() % gw.count : 0;
                auto *const node = reinterpret_cast<cICompositionTreeNode *>(&gw.nodes[at]);
                void *const impl = reinterpret_cast<void *>(uintptr_t(0x5a5a0000u | (rng() & 0xfff0)));
                drawfx::Note("build", {uint32_t(isConst), order, at});
                cCompositionTreeNodeRange range{};
                cCompositionTreeNodeRange *result;
                if (isOriginal)
                    result = (isConst ? originalConst : original)(impl, &range, node, order, filter);
                else
                    result = isConst ? MakeConstTreeIterators(impl, nullptr, &range, node, order, filter)
                                     : MakeTreeIterators(impl, nullptr, &range, node, order, filter);
                NoteRange(range, result);
                NoteIterator("current", range.mCurrent, isOriginal);
                NoteIterator("begin", range.mBegin, isOriginal);
                NoteIterator("end", range.mEnd, isOriginal);
                held.push_back(range.mCurrent);
                held.push_back(range.mBegin);
                held.push_back(range.mEnd);
            }
            const unsigned ops = 8 + rng() % 53;
            for (unsigned k = 0; k < ops; ++k) {
                char step[32];
                std::snprintf(step, sizeof(step), "op %u", k);
                sgfx::MarkStep(ctx, step);
                const unsigned i = rng() % held.size();
                cICompositionTreeNodeIterator *const it = held[i];
                switch (rng() % 12) {
                case 0: drawfx::Note("get", drawfx::Norm(P(it->Get()))); break;
                case 1: {
                    const unsigned j = rng() % held.size();
                    drawfx::Note("equal", {uint32_t(j), uint32_t(it->IsEqual(held[j]))});
                    break;
                }
                case 2:
                case 3:
                case 4:
                    if (it->Get()) it->Next();
                    drawfx::Note("next", drawfx::Norm(P(it->Get())));
                    break;
                case 5: {
                    unsigned steps = 0;
                    while (it->Get() && steps++ < 200) {
                        drawfx::Note("walk", drawfx::Norm(P(it->Get())));
                        it->Next();
                    }
                    break;
                }
                case 6:
                case 7: {
                    cICompositionTreeNodeIterator *const clone = it->Clone();
                    drawfx::Note("clone", {uint32_t(clone != nullptr), Count(clone), uint32_t(clone != it)});
                    NoteIterator("clone words", clone, isOriginal);
                    drawfx::Note("addref", clone->AddRef());
                    held.push_back(clone);
                    break;
                }
                case 8: {
                    const uint32_t kinds[] = {1, 2, rng()};
                    const uint32_t riid = kinds[rng() % 3];
                    void *out = reinterpret_cast<void *>(uintptr_t(0x77));
                    const bool nullOut = rng() % 4 == 0;
                    const bool ok = it->QueryInterface(riid, nullOut ? nullptr : &out);
                    drawfx::Note("query", {riid, uint32_t(nullOut), uint32_t(ok), uint32_t(out == it),
                                           uint32_t(out == reinterpret_cast<void *>(uintptr_t(0x77)))});
                    if (ok) held.push_back(it);
                    break;
                }
                case 9:
                    drawfx::Note("addref", it->AddRef());
                    held.push_back(it);
                    break;
                default:
                    if (Count(it) < 2) break;
                    drawfx::Note("release", it->Release());
                    held.erase(held.begin() + i);
                    break;
                }
                NoteIterator("words", it, isOriginal);
            }
            sgfx::MarkStep(ctx, "final releases");
            difftest::FreeRecorder rec;
            for (cICompositionTreeNodeIterator *it : held) drawfx::Note("final release", it->Release());
            rec.Stop();
            size_t sizes[256];
            const size_t n = rec.SortedSizes(sizes, 256);
            drawfx::Call freed;
            freed.note = "freed";
            freed.words.push_back(uint32_t(n));
            for (size_t j = 0; j < n && j < 256; ++j) freed.words.push_back(uint32_t(sizes[j]));
            drawfx::gLog.push_back(std::move(freed));
            Pools::Note(isOriginal ? P(GameAddress<void>(0x1069ffc)) : P(DefaultBranchFilter()));
            for (unsigned j = 0; j < gw.count; ++j) drawfx::Note("node refs", {j, uint32_t(gw.nodes[j].refs)});
        });
}
