// Differential tests of nTSSG::cIntervalTree (lane 261108o, scenegraph row IT1; src/sg/cIntervalTree.h) and the dynamic
// render strategy's trees member (DynamicStrategyTrees, row TS1e; src/sg/cDisplayListDynamicRenderStrategy.h).
//
//   SG_IntervalTree   per seed one tree on each side (ours, or a block given the original's constructor 0x401910),
//       owning its nodes or not, then random steps: nodes made (the original's 0x401980 / ours, on blocks filled alike;
//       some intervals reversed, values from a small range so queries meet duplicates) and inserted (0x401af0), queries
//       (0x401d40) from the root or another node over random bounds into a set that may already hold nodes, and Clear
//       (0x401a90). After every step the tree is noted: its words (the vtable skipped) and every node's words, links as
//       node numbers; after a query the set's nodes in order. At the end the tree is destroyed (the deleting destructor
//       0x401a70, with or without the free) and the heap counted (an owning tree deletes its nodes: DeleteSubtree
//       0x401ab0). The node's default constructor 0x4019d0 is compared on its own.
//   SG_DynamicTrees   per seed one trees member on each side (the original's constructor 0x36e9c0, verified, or ours),
//       then several Builds (0x370b70) from random rectangle lists (empty to 40, the node vector growing and shrinking),
//       each followed by Overlaps queries (0x373750) with random rectangles; noted: the node count, the vector's size and
//       capacity (not its address), every node, both roots, Overlaps' answer and both hit sets. Then HitsShareRectangle
//       (0x3735b0) on random sets of nodes, and the members destroyed with our destructor on both sides (the original's
//       is inline in the strategy's 0x36f220; its trees then run the original's deleting destructor) and the heap counted.
//
// The game's operator new / delete (0x824632 / 0x824624) go to ours on both sides, so blocks cross freely and are counted.
#include "code_patch.h"
#include "difftest.h"
#include "heap_poison.h"

#include "sg/cDisplayListDynamicRenderStrategy.h"
#include "sg/cIntervalTree.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <new>
#include <random>
#include <string>
#include <vector>

using nTSSG::cIntervalTree;
using nTSSG::DynamicStrategyTrees;
using nTSSG::DynamicWords16;
using Node = cIntervalTree::Node;
using NodeSet = cIntervalTree::NodeSet;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct HeapPatches {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

using TreeCtor = void *(__thiscall *)(void *, uint8_t);
using TreeDeleting = void *(__thiscall *)(void *, uint32_t);
using TreeClear = void(__thiscall *)(void *);
using TreeInsert = void(__thiscall *)(void *, Node *);
using TreeQuery = void(__thiscall *)(void *, Node *, int32_t, int32_t, NodeSet *);
using NodeCtor = Node *(__thiscall *)(void *, int32_t, int32_t, int32_t);
using NodeDefault = void(__thiscall *)(void *);
using TreesCtor = void *(__thiscall *)(void *);
using TreesBuild = void(__thiscall *)(void *, const std::list<DynamicWords16> *);
using TreesOverlaps = bool(__thiscall *)(void *, const int32_t *);
using ShareFn = bool(__cdecl *)(NodeSet::const_iterator, NodeSet::const_iterator, NodeSet::const_iterator,
                                NodeSet::const_iterator);

constexpr uint8_t kPoison = 0xc5;

// The log of one side: words, compared between the sides.
using Log = std::vector<uint32_t>;

// A node as its number in nodes (1-based), 0 for none, 0xeeeeeeee for a pointer to anything else.
uint32_t NodeNumber(const std::vector<Node *> &nodes, const Node *p)
{
    if (!p) return 0;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i] == p) return static_cast<uint32_t>(i + 1);
    return 0xeeeeeeeeu;
}

void NoteNode(Log &log, const std::vector<Node *> &nodes, const Node *n)
{
    log.push_back(NodeNumber(nodes, n->mParent));
    log.push_back(NodeNumber(nodes, n->mLeft));
    log.push_back(NodeNumber(nodes, n->mRight));
    log.push_back(static_cast<uint32_t>(n->mData));
    log.push_back(n->mColour);
    log.push_back(static_cast<uint32_t>(n->mLow));
    log.push_back(static_cast<uint32_t>(n->mHigh));
    log.push_back(static_cast<uint32_t>(n->mMax));
}

// The tree's words after the vtable (the count, the root as a node number, the owning flag's whole word) and its nodes.
void NoteTree(Log &log, const std::vector<Node *> &nodes, const void *tree)
{
    uint32_t words[4];
    std::memcpy(words, tree, sizeof(words));
    log.push_back(0x7eee);
    log.push_back(words[1]);
    log.push_back(NodeNumber(nodes, *reinterpret_cast<Node *const *>(uintptr_t(words[2]))));
    log.push_back(words[3]);
    for (const Node *n : nodes) NoteNode(log, nodes, n);
}

void NoteSet(Log &log, const std::vector<Node *> &nodes, const NodeSet &set)
{
    log.push_back(0x5e7);
    log.push_back(static_cast<uint32_t>(set.size()));
    for (const Node *n : set) log.push_back(NodeNumber(nodes, n));
}

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

// A block of n bytes filled with the poison pattern.
void *Block(size_t n)
{
    void *const p = ::operator new(n);
    std::memset(p, kPoison, n);
    return p;
}

// ---- SG_IntervalTree ----

struct TreeStep {
    int kind;   // 0 insert, 1 query, 2 clear
    int32_t a, b, c;
    uint32_t from;  // query: 0 the root, else a node number
    uint32_t prefill; // query: how many of the existing nodes the set starts with
};

void RunTree(difftest::Context &ctx, bool original, bool owns, uint32_t destroyKind, const std::vector<TreeStep> &steps,
             Log &log)
{
    difftest::AllocationCounter counter;
    void *tree;
    if (original) {
        tree = ctx.Original<TreeCtor>(0x401910)(Block(sizeof(cIntervalTree)), owns);
    } else {
        tree = new (Block(sizeof(cIntervalTree))) cIntervalTree(owns);
    }
    auto *ours = static_cast<cIntervalTree *>(tree);
    std::vector<Node *> nodes;
    std::vector<Node *> unowned; // blocks the tree doesn't delete (freed at the end)
    for (const TreeStep &s : steps) {
        if (s.kind == 0) {
            void *const block = Block(sizeof(Node));
            Node *n;
            if (original) {
                n = ctx.Original<NodeCtor>(0x401980)(block, s.a, s.b, s.c);
                if (n != block) ctx.Fail("node constructor: returned %p, not its block %p", n, block);
            } else {
                n = new (block) Node(s.a, s.b, s.c);
            }
            nodes.push_back(n);
            if (!owns) unowned.push_back(n);
            if (original)
                ctx.Original<TreeInsert>(0x401af0)(tree, n);
            else
                ours->Insert(n);
            NoteTree(log, nodes, tree);
        } else if (s.kind == 1) {
            NodeSet set;
            for (uint32_t i = 0; i < s.prefill && i < nodes.size(); ++i) set.insert(nodes[(i * 7 + s.c) % nodes.size()]);
            Node *const root = *reinterpret_cast<Node **>(static_cast<uint32_t *>(tree)[2]);
            Node *const from = s.from && s.from <= nodes.size() ? nodes[s.from - 1] : root;
            if (original)
                ctx.Original<TreeQuery>(0x401d40)(tree, from, s.a, s.b, &set);
            else
                ours->Query(from, s.a, s.b, &set);
            NoteSet(log, nodes, set);
            NoteTree(log, nodes, tree);
        } else {
            if (original)
                ctx.Original<TreeClear>(0x401a90)(tree);
            else
                ours->Clear();
            // An owning tree has deleted its nodes: forget them.
            if (owns) nodes.clear();
            NoteTree(log, nodes, tree);
        }
    }
    // The destructor (deleting or not), then the block itself when it wasn't freed.
    if (original) {
        ctx.Original<TreeDeleting>(0x401a70)(tree, destroyKind);
    } else if (destroyKind & 1) {
        delete ours;
    } else {
        ours->~cIntervalTree();
    }
    if (!(destroyKind & 1)) ::operator delete(tree);
    for (Node *n : unowned) ::operator delete(n);
    counter.Stop();
    log.push_back(0x4ea9);
    log.push_back(static_cast<uint32_t>(counter.Blocks()));
    log.push_back(static_cast<uint32_t>(counter.Bytes()));
}

// ---- SG_DynamicTrees ----

struct TreesRound {
    std::list<DynamicWords16> rects;
    std::vector<std::array<int32_t, 4>> queries;
};

void NoteTrees(Log &log, const DynamicStrategyTrees &t)
{
    std::vector<Node *> nodes;
    for (const Node &n : t.mNodes) nodes.push_back(const_cast<Node *>(&n));
    log.push_back(0x7eee5);
    log.push_back(t.mNodeCount);
    log.push_back(static_cast<uint32_t>(t.mNodes.size()));
    log.push_back(static_cast<uint32_t>(t.mNodes.capacity()));
    for (const Node *n : nodes) NoteNode(log, nodes, n);
    log.push_back(NodeNumber(nodes, t.mXTree->Root()));
    log.push_back(NodeNumber(nodes, t.mYTree->Root()));
    NoteSet(log, nodes, t.mXHits);
    NoteSet(log, nodes, t.mYHits);
}

int32_t Coordinate(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return -static_cast<int32_t>(rng() % 50);
    case 1: return static_cast<int32_t>(rng() % 2000);
    default: return static_cast<int32_t>(rng() % 200);
    }
}

} // namespace

// Verifies: cIntervalTree's constructor 0x401910, destructor 0x401a10 and deleting destructor 0x401a70 (slot 3), Clear
// 0x401a90, DeleteSubtree 0x401ab0, Insert 0x401af0 with InsertUnbalanced 0x401e90 and the rotations 0x401bf0 / 0x401c90,
// Query 0x401d40, and the node's constructors 0x401980 and 0x4019d0.
DIFF_TEST(SG_IntervalTree, 0x401af0)
{
    heappoison::Scope poison(ctx, kPoison);
    HeapPatches patches;
    // The node's default constructor: every word zero (its eax is left as it was: no result to compare).
    for (int i = 0; i < 4; ++i) {
        uint32_t original[8], ours[8];
        std::memset(original, 0x5a + i, sizeof(original));
        std::memset(ours, 0x5a + i, sizeof(ours));
        ctx.Original<NodeDefault>(0x4019d0)(original);
        new (ours) Node();
        if (std::memcmp(original, ours, sizeof(ours)) != 0) ctx.Fail("node default constructor: words differ");
        ++ctx.cases;
    }
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        std::mt19937 rng(seed);
        const bool owns = rng() % 2 != 0;
        const uint32_t destroyKind = rng() % 4 == 0 ? 0u : 1u;
        const int32_t span = seed % 3 == 0 ? 8 : 60;
        std::vector<TreeStep> steps;
        const unsigned count = rng() % 48;
        for (unsigned i = 0; i < count; ++i) {
            TreeStep s{};
            const uint32_t pick = rng() % 10;
            s.kind = pick < 6 ? 0 : pick < 9 || seed % 5 != 0 ? 1 : 2;
            s.a = static_cast<int32_t>(rng() % span) - 5;
            s.b = rng() % 6 == 0 ? s.a : static_cast<int32_t>(rng() % span) - 5;
            s.c = static_cast<int32_t>(rng() % (seed % 2 ? 12 : 100)) - 2;
            s.from = rng() % 4 == 0 ? 1 + rng() % (i + 1) : 0;
            s.prefill = rng() % 3 == 0 ? rng() % 4 : 0;
            steps.push_back(s);
        }
        Log original, ours;
        RunTree(ctx, true, owns, destroyKind, steps, original);
        RunTree(ctx, false, owns, destroyKind, steps, ours);
        if (!CompareLogs(ctx, "interval tree", seed, original, ours)) return;
        ++ctx.cases;
    }
}

// Verifies: DynamicStrategyTrees' Build 0x370b70 and Overlaps 0x373750, and HitsShareRectangle 0x3735b0.
DIFF_TEST(SG_DynamicTrees, 0x370b70)
{
    heappoison::Scope poison(ctx, kPoison);
    HeapPatches patches;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 rng(seed);
        std::vector<TreesRound> rounds(1 + rng() % 5);
        for (TreesRound &r : rounds) {
            const unsigned n = rng() % 5 == 0 ? 0 : rng() % 41;
            for (unsigned i = 0; i < n; ++i) {
                DynamicWords16 w;
                for (uint32_t &v : w.w) v = static_cast<uint32_t>(Coordinate(rng));
                r.rects.push_back(w);
            }
            const unsigned q = rng() % 12;
            for (unsigned i = 0; i < q; ++i) {
                std::array<int32_t, 4> rect;
                for (int32_t &v : rect) v = Coordinate(rng);
                if (rng() % 3 == 0 && !r.rects.empty()) {
                    // A rectangle built from one of the list's, nudged: hits and near misses.
                    auto it = r.rects.begin();
                    std::advance(it, rng() % r.rects.size());
                    for (int k = 0; k < 4; ++k)
                        rect[k] = static_cast<int32_t>(it->w[k]) + static_cast<int32_t>(rng() % 5) - 2;
                }
                r.queries.push_back(rect);
            }
        }
        // Sets for HitsShareRectangle: node values from a small range.
        const unsigned shareCount = rng() % 6;
        std::vector<std::pair<std::vector<int32_t>, std::vector<int32_t>>> shares(shareCount);
        for (auto &s : shares) {
            for (unsigned i = rng() % 8; i; --i) s.first.push_back(static_cast<int32_t>(rng() % 20));
            for (unsigned i = rng() % 8; i; --i) s.second.push_back(static_cast<int32_t>(rng() % 20));
        }

        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            const bool original = side == 0;
            Log &log = logs[side];
            difftest::AllocationCounter counter;
            void *const block = Block(sizeof(DynamicStrategyTrees));
            DynamicStrategyTrees *t;
            if (original)
                t = static_cast<DynamicStrategyTrees *>(ctx.Original<TreesCtor>(0x36e9c0)(block));
            else
                t = new (block) DynamicStrategyTrees;
            for (const TreesRound &r : rounds) {
                if (original)
                    ctx.Original<TreesBuild>(0x370b70)(t, &r.rects);
                else
                    t->Build(&r.rects);
                NoteTrees(log, *t);
                for (const auto &q : r.queries) {
                    const bool hit = original ? ctx.Original<TreesOverlaps>(0x373750)(t, q.data()) : t->Overlaps(q.data());
                    log.push_back(0x0e1a);
                    log.push_back(hit);
                    NoteTrees(log, *t);
                }
            }
            for (const auto &s : shares) {
                std::vector<Node> a(s.first.size()), b(s.second.size());
                NodeSet setA, setB;
                for (size_t i = 0; i < a.size(); ++i) {
                    a[i].mData = s.first[i];
                    setA.insert(&a[i]);
                }
                for (size_t i = 0; i < b.size(); ++i) {
                    b[i].mData = s.second[i];
                    setB.insert(&b[i]);
                }
                const bool share = original ? ctx.Original<ShareFn>(0x3735b0)(setA.begin(), setA.end(), setB.begin(), setB.end())
                                            : nTSSG::HitsShareRectangle(setA.begin(), setA.end(), setB.begin(), setB.end());
                log.push_back(0x5a4e);
                log.push_back(share);
            }
            t->~DynamicStrategyTrees();
            ::operator delete(block);
            counter.Stop();
            log.push_back(0x4ea9);
            log.push_back(static_cast<uint32_t>(counter.Blocks()));
            log.push_back(static_cast<uint32_t>(counter.Bytes()));
        }
        if (!CompareLogs(ctx, "dynamic trees", seed, logs[0], logs[1])) return;
        ++ctx.cases;
    }
}
