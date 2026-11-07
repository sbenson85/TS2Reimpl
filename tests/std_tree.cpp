// Differential tests for the std::_Tree helpers in std/StdTree.h. Trees for insertion and lookup
// live in plain buffers (head node at offset 0, then the nodes), so the original and the replacement
// run on identical copies and the whole buffer is compared afterwards, including fields an operation
// should not touch. Erasing frees nodes through the process heap, so those tests compare which
// blocks were released (a FreeRecorder across the call), not the order of the releases.
#include "difftest.h"
#include "free_scope.h"
#include "std/StdTree.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <functional>
#include <cstring>
#include <random>
#include <set>
#include <vector>

namespace {

// The node stride of std::map<uint32_t, uint32_t> (0x18) and of the 0x2c-byte nodes. Only the fields
// below +0x14 are used with the 0x18-byte nodes, so the same accessors work for both.
constexpr size_t kMapStride = 0x18;
constexpr size_t kTreeStride = 0x2c;

StdTreeNode *NodeAt(std::vector<uint8_t> &mem, size_t stride, size_t index)
{
    return reinterpret_cast<StdTreeNode *>(mem.data() + index * stride);
}

void InitHead(StdTreeNode *head)
{
    head->left = head;
    head->parent = head; // empty: the root is the head itself
    head->right = head;
    head->color = 1;
    head->isNil = 1;
}

// A new node as the game's allocation leaves it: children and parent at the head, red, not nil.
void InitNode(StdTreeNode *node, StdTreeNode *head, uint32_t key)
{
    node->left = head;
    node->parent = head;
    node->right = head;
    node->color = 0;
    node->isNil = 0;
    node->key = key;
}

// Plain BST insertion with random colours (the searches use only the key order).
void BstInsert(StdTreeNode *head, StdTreeNode *node, uint32_t key, std::mt19937 &rng)
{
    InitNode(node, head, key);
    node->color = static_cast<uint8_t>(rng() & 1);
    if (head->parent->isNil) {
        head->parent = node;
        head->left = node;
        head->right = node;
        node->parent = head;
        return;
    }
    StdTreeNode *cur = head->parent;
    for (;;) {
        if (key < cur->key) {
            if (cur->left->isNil) {
                cur->left = node;
                node->parent = cur;
                break;
            }
            cur = cur->left;
        } else {
            if (cur->right->isNil) {
                cur->right = node;
                node->parent = cur;
                break;
            }
            cur = cur->right;
        }
    }
    if (key < head->left->key) head->left = node;
    if (key >= head->right->key) head->right = node;
}

// Unique random keys (a set of misses comes from the range being wider than the count).
std::vector<uint32_t> RandomKeys(std::mt19937 &rng, int count, uint32_t range)
{
    std::set<uint32_t> seen;
    std::vector<uint32_t> keys;
    while (static_cast<int>(keys.size()) < count) {
        uint32_t k = rng() % range;
        if (seen.insert(k).second) keys.push_back(k);
    }
    return keys;
}

// Where a new key goes in a binary search tree: the last node visited, and whether the key goes to
// its left. An empty tree puts the node under the head.
void InsertionPoint(StdTreeNode *head, uint32_t key, StdTreeNode **where, uint32_t *addLeft)
{
    if (head->parent->isNil) {
        *where = head;
        *addLeft = 0;
        return;
    }
    StdTreeNode *cur = head->parent;
    for (;;) {
        *where = cur;
        if (key < cur->key) {
            *addLeft = 1;
            if (cur->left->isNil) return;
            cur = cur->left;
        } else {
            *addLeft = 0;
            if (cur->right->isNil) return;
            cur = cur->right;
        }
    }
}

// Nodes of the subtree at node in pre-order (none for the nil head).
void CollectSubtree(StdTreeNode *node, std::vector<void *> &out)
{
    if (node->isNil) return;
    out.push_back(node);
    CollectSubtree(node->left, out);
    CollectSubtree(node->right, out);
}

// The release state of each block, from the FreeRecorder that ran across one side's call
// (difftest::BeginFrees() right before it): 1 if the call freed it, 0 if it is live. HeapSize is not used
// on freed pointers (it raises STATUS_HEAP_CORRUPTION here).
struct FreedFlags {
    static constexpr size_t kMax = 64;
    int flag[kMax];
    size_t count;
};

FreedFlags Freed(const std::vector<void *> &blocks)
{
    FreedFlags f{};
    f.count = blocks.size();
    for (size_t i = 0; i < blocks.size() && i < FreedFlags::kMax; ++i)
        f.flag[i] = difftest::FreedSinceBegin(blocks[i]) ? 1 : 0;
    return f;
}

bool SameFlags(const FreedFlags &a, const FreedFlags &b)
{
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i)
        if (a.flag[i] != b.flag[i]) return false;
    return true;
}

bool AllFlags(const FreedFlags &f, int value)
{
    for (size_t i = 0; i < f.count; ++i)
        if (f.flag[i] != value) return false;
    return true;
}

// A set of 0x2c-byte heap nodes and a heap head, built for the erase tests.
struct HeapTree {
    StdTreeNode *head;
    std::vector<void *> nodes; // pre-order after building

    void Build(const std::vector<uint32_t> &keys, std::mt19937 &rng)
    {
        head = static_cast<StdTreeNode *>(::operator new(kTreeStride));
        InitHead(head);
        for (uint32_t key : keys) BstInsert(head, static_cast<StdTreeNode *>(::operator new(kTreeStride)), key, rng);
        nodes.clear();
        CollectSubtree(head->parent, nodes);
    }
};

// Frees the blocks that are still live after a test (the flags are the state measured on that side).
void FreeLive(const std::vector<void *> &blocks, const FreedFlags &freed)
{
    for (size_t i = 0; i < blocks.size(); ++i)
        if (!freed.flag[i]) ::operator delete(blocks[i], kTreeStride);
}

} // namespace

// std::map::find for std::map<uint32_t, uint32_t> (0x18-byte nodes): the same node (or the head for a
// miss) must come back, and the tree must not change.
DIFF_TEST(StdTree_Find, 0x144c35)
{
    auto orig = ctx.Original<StdTreeNode *(__thiscall *)(StdTreeHead *, const uint32_t *)>(0x144c35);
    std::mt19937 rng(41);
    for (int iter = 0; iter < 4000; ++iter) {
        int count = static_cast<int>(rng() % 50);
        std::vector<uint32_t> keys = RandomKeys(rng, count, 200);
        std::vector<uint8_t> base((count + 1) * kMapStride, 0);
        StdTreeNode *head = NodeAt(base, kMapStride, 0);
        InitHead(head);
        for (int i = 0; i < count; ++i) BstInsert(head, NodeAt(base, kMapStride, i + 1), keys[i], rng);
        StdTreeHead mapOrig{head, static_cast<uint32_t>(count)};
        StdTreeHead mapNew = mapOrig;
        std::vector<uint8_t> before = base;
        for (uint32_t key = 0; key < 210; key += 1 + rng() % 3) {
            StdTreeNode *ro = orig(&mapOrig, &key);
            StdTreeNode *rn = mapNew.Find(&key);
            ++ctx.cases;
            if (ro != rn) ctx.Fail("map find %u (%d nodes): returned %p, original %p", key, count, rn, ro);
        }
        if (std::memcmp(before.data(), base.data(), base.size()) != 0) ctx.Fail("a search changed the tree");
        if (mapOrig.head != head || mapNew.head != head || mapOrig.size != mapNew.size) ctx.Fail("map header changed");
    }
}

// Lower-bound search over a 0x2c-byte-node map (0x1463b0): the three-field result and the returned
// buffer must match. Both result buffers start with different random garbage, so every field has to
// be written by the search.
DIFF_TEST(StdTree_FindLowerBound, 0x1463b0)
{
    auto orig = ctx.Original<StdTreeBound *(__thiscall *)(StdTreeHead *, StdTreeBound *, const uint32_t *)>(0x1463b0);
    std::mt19937 rng(42);
    for (int iter = 0; iter < 20000; ++iter) {
        int count = static_cast<int>(rng() % 40);
        std::vector<uint32_t> keys = RandomKeys(rng, count, 120);
        std::vector<uint8_t> base((count + 1) * kTreeStride, 0);
        StdTreeNode *head = NodeAt(base, kTreeStride, 0);
        InitHead(head);
        for (int i = 0; i < count; ++i) BstInsert(head, NodeAt(base, kTreeStride, i + 1), keys[i], rng);
        StdTreeHead mapOrig{head, static_cast<uint32_t>(count)};
        StdTreeHead mapNew = mapOrig;
        std::vector<uint8_t> before = base;
        for (int q = 0; q < 4; ++q) {
            uint32_t key = (q == 0 && count > 0) ? keys[rng() % count] : static_cast<uint32_t>(rng() % 130);
            StdTreeBound outOrig, outNew;
            outOrig.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outOrig.wentLeft = rng();
            outOrig.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.wentLeft = rng();
            outNew.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            StdTreeBound *ro = orig(&mapOrig, &outOrig, &key);
            StdTreeBound *rn = mapNew.FindLowerBound(&outNew, &key);
            ++ctx.cases;
            if (ro != &outOrig || rn != &outNew) ctx.Fail("lower bound did not return its result buffer");
            if (outOrig.node != outNew.node || outOrig.wentLeft != outNew.wentLeft || outOrig.bound != outNew.bound)
                ctx.Fail("lower bound of %u (%d nodes): node %p/%p, wentLeft %u/%u, bound %p/%p", key, count, outNew.node,
                         outOrig.node, outNew.wentLeft, outOrig.wentLeft, outNew.bound, outOrig.bound);
        }
        if (std::memcmp(before.data(), base.data(), base.size()) != 0) ctx.Fail("lower-bound search changed the tree");
    }
}

// Insertion with rebalancing (0x148bb0). A base tree is built with the original's insertion (so it is a
// valid red-black tree), then one more key goes in. The whole buffer, the header (root and count) and
// the returned node must match. Key sequences: random, ascending and descending.
DIFF_TEST(StdTree_InsertAt, 0x148bb0)
{
    auto orig = ctx.Original<StdTreeNode *(__thiscall *)(StdTreeHead *, StdTreeNode *, uint32_t, StdTreeNode *)>(0x148bb0);
    std::mt19937 rng(43);
    constexpr int kMaxNodes = 96;
    for (int iter = 0; iter < 6000; ++iter) {
        int count = static_cast<int>(rng() % (iter % 4 == 0 ? kMaxNodes : 40));
        int mode = iter % 3;
        std::vector<uint32_t> keys = RandomKeys(rng, count + 1, 1000);
        if (mode == 1) std::sort(keys.begin(), keys.end());
        if (mode == 2) std::sort(keys.begin(), keys.end(), std::greater<uint32_t>());
        std::vector<uint8_t> mem((kMaxNodes + 1) * kTreeStride);
        for (auto &b : mem) b = static_cast<uint8_t>(rng()); // fields no operation writes keep this noise
        StdTreeNode *head = NodeAt(mem, kTreeStride, 0);
        InitHead(head);
        StdTreeHead map{head, 0};
        for (int i = 0; i < count; ++i) {
            StdTreeNode *node = NodeAt(mem, kTreeStride, i + 1);
            InitNode(node, head, keys[i]);
            StdTreeNode *where;
            uint32_t addLeft;
            InsertionPoint(head, keys[i], &where, &addLeft);
            orig(&map, where, addLeft, node);
        }
        StdTreeNode *node = NodeAt(mem, kTreeStride, count + 1);
        InitNode(node, head, keys[count]);
        StdTreeNode *where;
        uint32_t addLeft;
        InsertionPoint(head, keys[count], &where, &addLeft);

        std::vector<uint8_t> snapshot = mem;
        StdTreeHead snapshotMap = map;

        StdTreeNode *ro = orig(&map, where, addLeft, node);
        std::vector<uint8_t> afterOrig = mem;
        StdTreeHead mapOrig = map;

        std::memcpy(mem.data(), snapshot.data(), mem.size());
        map = snapshotMap;
        StdTreeNode *rn = map.InsertAt(where, addLeft, node);
        ++ctx.cases;

        if (ro != node || rn != node) ctx.Fail("insert returned %p/%p, expected the new node %p", ro, rn, node);
        if (mapOrig.head != map.head || mapOrig.size != map.size) ctx.Fail("insert: count %u, original %u", map.size, mapOrig.size);
        if (afterOrig != mem) {
            size_t diff = 0;
            while (afterOrig[diff] == mem[diff]) ++diff;
            ctx.Fail("insert of %u into %d nodes (mode %d): buffers differ at byte %zu (node %zu)", keys[count], count, mode, diff,
                     diff / kTreeStride);
        }
    }
}

// Erase of one subtree (0x146370), the subtree at a random node or nothing (the head). The two trees
// have the same shape, so pre-order positions correspond. The blocks of the subtree must be freed on
// both sides, and no other block may be.
DIFF_TEST(StdTree_EraseSubtree, 0x146370)
{
    auto orig = ctx.Original<void(__thiscall *)(StdTreeHead *, void *, StdTreeNode *)>(0x146370);
    std::mt19937 rng(44);
    for (int iter = 0; iter < 1500; ++iter) {
        int count = static_cast<int>(rng() % 30);
        std::vector<uint32_t> keys = RandomKeys(rng, count, 500);
        HeapTree a, b;
        a.Build(keys, rng);
        b.Build(keys, rng);
        size_t pick = rng() % (a.nodes.size() + 1);
        StdTreeNode *rootA = pick < a.nodes.size() ? static_cast<StdTreeNode *>(a.nodes[pick]) : a.head;
        StdTreeNode *rootB = pick < b.nodes.size() ? static_cast<StdTreeNode *>(b.nodes[pick]) : b.head;

        std::vector<void *> subA, subB;
        CollectSubtree(rootA, subA);
        CollectSubtree(rootB, subB);
        std::vector<void *> restA, restB;
        for (void *p : a.nodes)
            if (std::find(subA.begin(), subA.end(), p) == subA.end()) restA.push_back(p);
        for (void *p : b.nodes)
            if (std::find(subB.begin(), subB.end(), p) == subB.end()) restB.push_back(p);
        restA.push_back(a.head);
        restB.push_back(b.head);

        StdTreeHead mapA{a.head, static_cast<uint32_t>(count)};
        StdTreeHead mapB{b.head, static_cast<uint32_t>(count)};
        // Each side is measured right after its own call, before the other side runs.
        difftest::BeginFrees();
        orig(&mapA, nullptr, rootA);
        FreedFlags freedA = Freed(subA), keptA = Freed(restA);
        difftest::BeginFrees();
        mapB.EraseSubtree(nullptr, rootB);
        FreedFlags freedB = Freed(subB), keptB = Freed(restB);
        ++ctx.cases;

        if (!AllFlags(freedA, 1)) ctx.Fail("original left a subtree block allocated (pick %zu of %d)", pick, count);
        if (!AllFlags(keptA, 0)) ctx.Fail("original freed a block outside the subtree");
        if (!AllFlags(freedB, 1)) ctx.Fail("replacement left a subtree block allocated (pick %zu of %d)", pick, count);
        if (!AllFlags(keptB, 0)) ctx.Fail("replacement freed a block outside the subtree (pick %zu of %d)", pick, count);
        if (!SameFlags(freedA, freedB) || !SameFlags(keptA, keptB)) ctx.Fail("replacement freed a different set of blocks");

        FreeLive(restA, keptA);
        FreeLive(restB, keptB);
    }
}

// Erase of the whole map (0x146320): every node and then the head is freed, on both sides.
DIFF_TEST(StdTree_Tidy, 0x146320)
{
    auto orig = ctx.Original<void(__thiscall *)(StdTreeHead *, void *)>(0x146320);
    std::mt19937 rng(45);
    for (int iter = 0; iter < 1500; ++iter) {
        int count = static_cast<int>(rng() % 30);
        std::vector<uint32_t> keys = RandomKeys(rng, count, 500);
        HeapTree a, b;
        a.Build(keys, rng);
        b.Build(keys, rng);
        std::vector<void *> blocksA = a.nodes, blocksB = b.nodes;
        blocksA.push_back(a.head);
        blocksB.push_back(b.head);
        StdTreeHead mapA{a.head, static_cast<uint32_t>(count)};
        StdTreeHead mapB{b.head, static_cast<uint32_t>(count)};
        // Each side is measured right after its own call, before the other side runs.
        difftest::BeginFrees();
        orig(&mapA, nullptr);
        FreedFlags freedA = Freed(blocksA);
        difftest::BeginFrees();
        mapB.Tidy(nullptr);
        FreedFlags freedB = Freed(blocksB);
        ++ctx.cases;
        if (!AllFlags(freedA, 1)) ctx.Fail("original left a block allocated (%d nodes)", count);
        if (!AllFlags(freedB, 1)) ctx.Fail("replacement left a block allocated (%d nodes)", count);
        if (!SameFlags(freedA, freedB)) ctx.Fail("replacement freed a different set of blocks (%d nodes)", count);
    }
}

namespace {

// Plain BST insertion of a node whose key is already set, ordered by less(node, other). Random colours,
// like BstInsert; leftmost and rightmost are kept as the game's insertion keeps them.
template <typename Less>
void BstInsertBy(StdTreeNode *head, StdTreeNode *node, Less less, std::mt19937 &rng)
{
    node->color = static_cast<uint8_t>(rng() & 1);
    if (head->parent->isNil) {
        head->parent = node;
        head->left = node;
        head->right = node;
        node->parent = head;
        return;
    }
    StdTreeNode *cur = head->parent;
    for (;;) {
        if (less(node, cur)) {
            if (cur->left->isNil) {
                cur->left = node;
                node->parent = cur;
                break;
            }
            cur = cur->left;
        } else {
            if (cur->right->isNil) {
                cur->right = node;
                node->parent = cur;
                break;
            }
            cur = cur->right;
        }
    }
    if (less(node, head->left)) head->left = node;
    if (!less(node, head->right)) head->right = node;
}

// Four key words at +0x10 of a 0x2c-byte node.
using Words = std::array<uint32_t, 4>;

void SetWords(StdTreeNode *node, const Words &w)
{
    uint32_t *p = reinterpret_cast<uint32_t *>(node) + 4;
    for (int i = 0; i < 4; ++i) p[i] = w[i];
}

// The order the original's lower-bound walk (0x1d5d10) uses: words 0, 1, 3, 2, unsigned. Written
// independently of the replacement, so a wrong order shows as a difference between the two walks.
bool WordsLess(const Words &a, const Words &b)
{
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    if (a[3] != b[3]) return a[3] < b[3];
    return a[2] < b[2];
}

Words NodeWords(const StdTreeNode *node)
{
    const uint32_t *p = reinterpret_cast<const uint32_t *>(node) + 4;
    return Words{p[0], p[1], p[2], p[3]};
}

} // namespace

// Lower-bound search with signed int32 keys (0x1d5d80). Keys are drawn from -60..60, so the sign decides
// most comparisons; an unsigned compare in the replacement would fail on the negative keys.
DIFF_TEST(StdTree_FindLowerBoundSigned, 0x1d5d80)
{
    auto orig = ctx.Original<StdTreeBound *(__thiscall *)(StdTreeHead *, StdTreeBound *, const int32_t *)>(0x1d5d80);
    std::mt19937 rng(46);
    for (int iter = 0; iter < 20000; ++iter) {
        int count = static_cast<int>(rng() % 40);
        std::set<int32_t> seen;
        std::vector<int32_t> keys;
        while (static_cast<int>(keys.size()) < count) {
            int32_t k = static_cast<int32_t>(rng() % 121) - 60;
            if (seen.insert(k).second) keys.push_back(k);
        }
        std::vector<uint8_t> base((count + 1) * kTreeStride, 0);
        StdTreeNode *head = NodeAt(base, kTreeStride, 0);
        InitHead(head);
        auto less = [](const StdTreeNode *a, const StdTreeNode *b) {
            return static_cast<int32_t>(a->key) < static_cast<int32_t>(b->key);
        };
        for (int i = 0; i < count; ++i) {
            StdTreeNode *node = NodeAt(base, kTreeStride, i + 1);
            InitNode(node, head, static_cast<uint32_t>(keys[i]));
            BstInsertBy(head, node, less, rng);
        }
        StdTreeHead mapOrig{head, static_cast<uint32_t>(count)};
        StdTreeHead mapNew = mapOrig;
        std::vector<uint8_t> before = base;
        for (int q = 0; q < 4; ++q) {
            int32_t key = (q == 0 && count > 0) ? keys[rng() % count] : static_cast<int32_t>(rng() % 130) - 65;
            StdTreeBound outOrig, outNew;
            outOrig.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outOrig.wentLeft = rng();
            outOrig.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.wentLeft = rng();
            outNew.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            StdTreeBound *ro = orig(&mapOrig, &outOrig, &key);
            StdTreeBound *rn = mapNew.FindLowerBoundSigned(&outNew, &key);
            ++ctx.cases;
            if (ro != &outOrig || rn != &outNew) ctx.Fail("signed lower bound did not return its result buffer");
            if (outOrig.node != outNew.node || outOrig.wentLeft != outNew.wentLeft || outOrig.bound != outNew.bound)
                ctx.Fail("signed lower bound of %d (%d nodes): node %p/%p, wentLeft %u/%u, bound %p/%p", key, count,
                         static_cast<void *>(outNew.node), static_cast<void *>(outOrig.node), outNew.wentLeft,
                         outOrig.wentLeft, static_cast<void *>(outNew.bound), static_cast<void *>(outOrig.bound));
        }
        if (std::memcmp(before.data(), base.data(), base.size()) != 0) ctx.Fail("signed lower-bound search changed the tree");
    }
}

// Lower-bound search with four-word keys (0x1d5d10). Words are drawn from small ranges so that the words
// tie often and the later words decide; the tree is built with WordsLess.
DIFF_TEST(StdTree_FindLowerBoundKey4, 0x1d5d10)
{
    auto orig = ctx.Original<StdTreeBound *(__thiscall *)(StdTreeHead *, StdTreeBound *, const uint32_t *)>(0x1d5d10);
    std::mt19937 rng(47);
    auto randomWords = [&rng]() { return Words{rng() % 3, rng() % 2, rng() % 3, rng() % 2}; };
    for (int iter = 0; iter < 20000; ++iter) {
        int count = static_cast<int>(rng() % 40);
        std::set<Words> seen;
        std::vector<Words> keys;
        for (int tries = 0; static_cast<int>(keys.size()) < count && tries < 10000; ++tries) {
            Words w = randomWords();
            if (seen.insert(w).second) keys.push_back(w);
        }
        count = static_cast<int>(keys.size());
        std::vector<uint8_t> base((count + 1) * kTreeStride, 0);
        StdTreeNode *head = NodeAt(base, kTreeStride, 0);
        InitHead(head);
        auto less = [](const StdTreeNode *a, const StdTreeNode *b) { return WordsLess(NodeWords(a), NodeWords(b)); };
        for (int i = 0; i < count; ++i) {
            StdTreeNode *node = NodeAt(base, kTreeStride, i + 1);
            InitNode(node, head, 0);
            SetWords(node, keys[i]);
            BstInsertBy(head, node, less, rng);
        }
        StdTreeHead mapOrig{head, static_cast<uint32_t>(count)};
        StdTreeHead mapNew = mapOrig;
        std::vector<uint8_t> before = base;
        for (int q = 0; q < 4; ++q) {
            Words key = (q == 0 && count > 0) ? keys[rng() % count] : randomWords();
            if (q == 1 && count > 0) key[2] = keys[rng() % count][2] + 1; // a near miss on the last word
            StdTreeBound outOrig, outNew;
            outOrig.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outOrig.wentLeft = rng();
            outOrig.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.node = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            outNew.wentLeft = rng();
            outNew.bound = reinterpret_cast<StdTreeNode *>(static_cast<uintptr_t>(rng()));
            StdTreeBound *ro = orig(&mapOrig, &outOrig, key.data());
            StdTreeBound *rn = mapNew.FindLowerBoundKey4(&outNew, key.data());
            ++ctx.cases;
            if (ro != &outOrig || rn != &outNew) ctx.Fail("key4 lower bound did not return its result buffer");
            if (outOrig.node != outNew.node || outOrig.wentLeft != outNew.wentLeft || outOrig.bound != outNew.bound)
                ctx.Fail("key4 lower bound of (%u,%u,%u,%u) (%d nodes): node %p/%p, wentLeft %u/%u, bound %p/%p", key[0],
                         key[1], key[2], key[3], count, static_cast<void *>(outNew.node), static_cast<void *>(outOrig.node),
                         outNew.wentLeft, outOrig.wentLeft, static_cast<void *>(outNew.bound),
                         static_cast<void *>(outOrig.bound));
        }
        if (std::memcmp(before.data(), base.data(), base.size()) != 0) ctx.Fail("key4 lower-bound search changed the tree");
    }
}
