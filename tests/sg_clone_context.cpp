// Differential test of the composition Impl's CloneInto 0xa384f0 and the clone context it builds (lane OR, scenegraph N0
// part 3 step 4; src/sg/cNodeCloneContext.h): the game's function against CloneCompositionInto on a raw Impl block whose
// children, outer nodes and extensions are fakes.
//
//   SG_CloneContext  CloneInto 0xa384f0 in every mode, with no context (it builds its own cNodeCloneContext) or a caller's
//                    context fake. The children's Clone (slot 15) drives the context it is handed as a node would: random
//                    FindClone calls on the source, the subtree (direct children and deeper nodes, whose parents lead to the
//                    source), outsiders and the clones, asked again and again (the second answer comes from the map), in
//                    three interface ids (two answered, one not); QueryInterface for cINodeCloneContext (uncounted), id 1
//                    (counted) and an unknown id; AddRef / Release pairs (their counts noted); then FindClone on itself,
//                    whose answer it hands back. A shallow clone (mode 2, what the context asks for a subtree node seen
//                    for the first time) hands out a fresh fake. More than 8 nodes are asked about in most seeds, so the
//                    map rehashes (8 to 64 buckets) and its destruction order (each clone's Release) follows the rehashed
//                    list.
// The context's own bodies are reached through it (each a note or a call on the fakes): 0xa38ce0 QueryInterface (and its
// +8 thunk 0xa38d08, the face's slot 0), 0xa37980 FindClone, 0xa371d0 the deleting destructor (the clones' Releases at
// the end of CloneInto).
//
// Each side's log: every slot call on the fakes in order (the context argument as presence only; which context it is
// noted by label: the caller's fake id, or the index of the side's own contexts), the notes of the children's context
// operations, the sizes of the blocks freed during the call (the context, its map's nodes and buckets; the log's own
// growth is the same on both sides, each starting from an empty log), then the clone Impl's words, the source Impl's
// words and every fake's count.
#include "difftest.h"
#include "sgfx.h"

#include "sg/cCompositionTreeNodeImpl.h"
#include "sg/cNodeCloneContext.h"

#include <random>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kRva = 0xa384f0;
constexpr uint32_t kContextIid = 0xecce4c52; // cINodeCloneContext
constexpr uint32_t kUnknownIid = 0x1234;     // answered by nobody
constexpr uint32_t kSentinel = 0x5e5e5e5e;   // an out nobody wrote
constexpr unsigned kTreeMax = 20, kOutsiders = 3, kMadeMax = 48, kSeeds = 160;

// The node fakes' table: kNode with slot 15's context as presence only (each side's context is its own heap object).
constexpr std::array<drawfx::Slot, 62> CloneSlots()
{
    std::array<drawfx::Slot, 62> r{};
    for (unsigned i = 0; i < 62; ++i) r[i] = sgfx::kNodeSlots[i];
    r[15] = {4, drawfx::Ret::Int, true, "Clone", "uuiW0"};
    return r;
}
constexpr auto kCloneSlots = CloneSlots();
constexpr drawfx::Iface kCloneNode{"CloneNode", 62, kCloneSlots.data()};

struct World {
    Fake source, clone;  // 100, 101
    Fake tree[kTreeMax]; // 110+: the subtree (each node's parent the source or an earlier tree node)
    Fake outsiders[kOutsiders]; // 150+: parents that never reach the source
    Fake made[kMadeMax]; // 700+: what a shallow clone hands out
    Fake ext[3], extCopy[3]; // 200+, 210+
    Fake callerContext;      // 430
    unsigned treeCount = 0, madeCount = 0;
    uint32_t impl[7], cloneImpl[7];
    std::mt19937 rng;
    std::vector<const void *> contexts; // the side's own contexts, in the order met
} w;

Fake *NewMade()
{
    if (w.madeCount == kMadeMax) return nullptr;
    Fake *m = &w.made[w.madeCount++];
    ++m->refs;
    return m;
}

void NoteContext(const void *context)
{
    if (context == &w.callerContext) {
        drawfx::Note("context", w.callerContext.id);
        return;
    }
    for (size_t i = 0; i < w.contexts.size(); ++i)
        if (w.contexts[i] == context) {
            drawfx::Note("context", uint32_t(1000 + i));
            return;
        }
    w.contexts.push_back(context);
    drawfx::Note("context", uint32_t(1000 + w.contexts.size() - 1));
}

// Any node of the world: the source, the subtree, the outsiders, the clone, a made clone.
Fake *AnyNode()
{
    const unsigned pick = w.rng() % 16;
    if (pick < 2) return &w.source;
    if (pick < 10) return &w.tree[w.rng() % w.treeCount];
    if (pick < 12) return &w.outsiders[w.rng() % kOutsiders];
    if (pick < 13) return &w.clone;
    return &w.made[w.rng() % kMadeMax];
}

uint32_t PointerKind(const void *p, const void *context)
{
    if (p == reinterpret_cast<const void *>(uintptr_t(kSentinel))) return 1;
    if (p == context) return 2;
    if (p == static_cast<const uint8_t *>(context) - 8) return 3;
    return p ? 4 : 0;
}

// One random operation on the context a child was handed.
void ContextOp(cINodeCloneContext *context)
{
    const unsigned op = w.rng() % 10;
    if (op < 6) {
        Fake *node = AnyNode();
        static const uint32_t kIids[] = {sgfx::kIidTree, sgfx::kIidGraph, kUnknownIid};
        const uint32_t iid = kIids[w.rng() % 3];
        void *out = reinterpret_cast<void *>(uintptr_t(kSentinel));
        context->FindClone(reinterpret_cast<cIObjectGraphNode *>(node), iid, &out);
        drawfx::Note("found", {node->id, iid, out == reinterpret_cast<void *>(uintptr_t(kSentinel)) ? 1u : drawfx::Norm(P(out))});
        if (out && out != reinterpret_cast<void *>(uintptr_t(kSentinel))) static_cast<cIGZUnknown *>(out)->Release();
        return;
    }
    if (op < 9) {
        static const uint32_t kIids[] = {kContextIid, 1, kUnknownIid};
        const uint32_t iid = kIids[op - 6];
        void *out = reinterpret_cast<void *>(uintptr_t(kSentinel));
        const bool answered = context->QueryInterface(iid, &out);
        drawfx::Note("query", {iid, uint32_t(answered), PointerKind(out, context)});
        if (answered && iid == 1) static_cast<cIGZUnknown *>(out)->Release();
        return;
    }
    const uint32_t up = context->AddRef();
    const uint32_t down = context->Release();
    drawfx::Note("counts", {up, down});
}

// Slot 15 of every node fake: mode 2 (or no context) hands out a fresh fake; a deep clone drives the context, then hands
// back what the context answers for the node itself (or nothing).
uint32_t NodeClone(Fake &self, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    auto *const context = reinterpret_cast<cINodeCloneContext *>(uintptr_t(a[3]));
    if (context) NoteContext(context);
    if (a[0] == 2 || !context) {
        *out = NewMade();
        return 0;
    }
    for (unsigned k = w.rng() % 6; k > 0; --k) ContextOp(context);
    void *result = nullptr;
    if (w.rng() % 4) context->FindClone(reinterpret_cast<cIObjectGraphNode *>(&self), sgfx::kIidTree, &result);
    *out = result;
    return 0;
}

void MakeCloneNode(Fake &f, uint32_t id)
{
    sgfx::MakeNode<kCloneNode>(f, id);
    f.On(15, &NodeClone);
}

void Build(uint32_t seed)
{
    w.rng.seed(seed * 2654435761u + 11u);
    std::mt19937 &rng = w.rng;
    MakeCloneNode(w.source, 100);
    MakeCloneNode(w.clone, 101);
    w.source.Returns(27, 0x5100u + seed); // GetTagHandle
    w.treeCount = 1 + rng() % kTreeMax;
    if (rng() % 8 == 0) w.treeCount = 0; // no children
    Fake *lastChildOf[kTreeMax + 1] = {};  // [0] the source, [i + 1] tree node i
    for (unsigned i = 0; i < kTreeMax; ++i) MakeCloneNode(w.tree[i], 110 + i);
    for (unsigned i = 0; i < w.treeCount; ++i) {
        Fake &n = w.tree[i];
        const unsigned parent = i == 0 || rng() % 2 ? 0 : 1 + rng() % i; // half the nodes the source's children
        Fake *const parentFake = parent ? &w.tree[parent - 1] : &w.source;
        sgfx::SetWord(n, sgfx::kParentWord, parentFake);
        if (Fake *last = lastChildOf[parent])
            sgfx::SetWord(*last, sgfx::kNextWord, &n);
        else
            sgfx::SetWord(*parentFake, sgfx::kFirstChildWord, &n);
        lastChildOf[parent] = &n;
    }
    for (unsigned i = 0; i < kOutsiders; ++i) {
        MakeCloneNode(w.outsiders[i], 150 + i);
        if (i) sgfx::SetWord(w.outsiders[i], sgfx::kParentWord, &w.outsiders[i - 1]);
    }
    for (unsigned i = 0; i < kMadeMax; ++i) MakeCloneNode(w.made[i], 700 + i);
    w.madeCount = 0;
    // The extensions: a chain of 0-3, each copy a fake or none.
    const unsigned extCount = rng() % 4;
    for (unsigned i = 0; i < 3; ++i) {
        sgfx::MakeExtension(w.ext[i], 200 + i);
        sgfx::MakeExtension(w.extCopy[i], 210 + i);
        if (rng() % 4) sgfx::SetWord(w.ext[i], sgfx::kExtCloneWord, &w.extCopy[i]);
    }
    for (unsigned i = 0; i + 1 < extCount; ++i) sgfx::SetWord(w.ext[i], sgfx::kExtNextWord, &w.ext[i + 1]);
    sgfx::MakeCloneContext(w.callerContext, rng() % 2 ? &w.made[kMadeMax - 1] : nullptr);
    // The Impl blocks: extension list, tag cell, parent / siblings (not read), first child, flags.
    w.impl[0] = extCount ? P(&w.ext[0]) : 0;
    for (unsigned i = 1; i < 5; ++i) w.impl[i] = rng();
    w.impl[5] = w.source.value[sgfx::kFirstChildWord];
    w.impl[6] = rng();
    for (uint32_t &word : w.cloneImpl) word = rng();
    w.contexts.clear();
    w.contexts.reserve(8); // allocated before either side's call, so the freed-block record holds no growth of it
    drawfx::AddRange(w.impl, sizeof(w.impl), 0x7a000000);
    drawfx::AddRange(w.cloneImpl, sizeof(w.cloneImpl), 0x7a100000);
}

void NoteWorld()
{
    drawfx::Call c;
    c.note = "clone impl";
    for (uint32_t word : w.cloneImpl) c.words.push_back(drawfx::Norm(word));
    for (uint32_t word : w.impl) c.words.push_back(drawfx::Norm(word));
    drawfx::gLog.push_back(std::move(c));
    auto refs = [](const char *label, const Fake *fakes, unsigned n) {
        drawfx::Call r;
        r.note = label;
        for (unsigned i = 0; i < n; ++i) r.words.push_back(uint32_t(fakes[i].refs));
        drawfx::gLog.push_back(std::move(r));
    };
    refs("source refs", &w.source, 1);
    refs("clone refs", &w.clone, 1);
    refs("tree refs", w.tree, kTreeMax);
    refs("outsider refs", w.outsiders, kOutsiders);
    refs("made refs", w.made, kMadeMax);
    refs("extension refs", w.ext, 3);
    refs("copy refs", w.extCopy, 3);
    refs("caller context refs", &w.callerContext, 1);
    drawfx::Note("made", w.madeCount);
}

using CloneIntoFn = void(__thiscall *)(void *impl, void *source, void *clone, void *cloneImpl, uint32_t mode,
                                       void *context);

} // namespace

DIFF_TEST(SG_CloneContext, 0xa384f0)
{
    const CloneIntoFn original = ctx.Original<CloneIntoFn>(kRva);
    sgfx::CheckSides(ctx, "CloneInto", kSeeds, [](uint32_t seed, drawfx::Cells &) { Build(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         const uint32_t mode = rng() % 4 ? 1 : rng() % 4;
                         Fake *const context = rng() % 4 == 0 ? &w.callerContext : nullptr;
                         drawfx::Note("call", {mode, context != nullptr});
                         difftest::FreeRecorder freed; // the context and its map, freed by its last Release
                         if (isOriginal)
                             original(w.impl, &w.source, &w.clone, w.cloneImpl, mode, context);
                         else
                             CloneCompositionInto(reinterpret_cast<cCompositionTreeNodeImpl *>(w.impl), nullptr,
                                                  reinterpret_cast<cICompositionTreeNode *>(&w.source),
                                                  reinterpret_cast<cICompositionTreeNode *>(&w.clone),
                                                  reinterpret_cast<cCompositionTreeNodeImpl *>(w.cloneImpl), mode,
                                                  reinterpret_cast<cINodeCloneContext *>(context));
                         freed.Stop();
                         size_t sizes[256];
                         const size_t n = freed.SortedSizes(sizes, 256);
                         drawfx::Call c;
                         c.note = "freed";
                         for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
                         drawfx::gLog.push_back(std::move(c));
                         NoteWorld();
                     });
}
