// Differential tests for the UtilitiesD group. Fakes carry recording vtables, so the vtable calls each
// side makes are compared (order, arguments, and results), not just the return values.
#include "difftest.h"
#include "game/game.h"
#include "misc/UtilitiesD.h"

#include <windows.h>

#include <climits>
#include <cstring>
#include <random>

namespace {

// --- FindChildAndSetEnabled (0x9351fd) -------------------------------------------------------------

struct Call {
    int slot;
    uint32_t a, b;
};
Call gCalls[8];
int gCallCount = 0;
void *gChild = nullptr;
uint8_t gFoundAl = 0;

void Record(int slot, uint32_t a, uint32_t b)
{
    if (gCallCount < 8) gCalls[gCallCount] = {slot, a, b};
    ++gCallCount;
}

uint8_t __fastcall FakeFind(void * /*self*/, void * /*edx*/, uint32_t id, uint32_t kind, void **out)
{
    Record(45, id, kind);
    *out = gChild;
    return gFoundAl; // the caller reads only al
}
void __fastcall FakeRelease(void * /*self*/, void * /*edx*/)
{
    Record(2, 0, 0);
}
void __fastcall FakeOn(void * /*self*/, void * /*edx*/)
{
    Record(6, 0, 0);
}
void __fastcall FakeOff(void * /*self*/, void * /*edx*/)
{
    Record(7, 0, 0);
}

void TestFindChildAndSetEnabled(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = uint8_t(__cdecl *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9351fd);

    void *windowVt[46] = {};
    windowVt[45] = reinterpret_cast<void *>(&FakeFind);
    void *childVt[8] = {};
    childVt[2] = reinterpret_cast<void *>(&FakeRelease);
    childVt[6] = reinterpret_cast<void *>(&FakeOn);
    childVt[7] = reinterpret_cast<void *>(&FakeOff);
    void *child[2] = {childVt, nullptr};
    void *window[2] = {windowVt, nullptr};

    for (int i = 0; i < 20000; ++i) {
        uint32_t id = rng();
        // Enabled values: all dwords, plus ones whose low byte is zero or nonzero, to exercise the byte test.
        uint32_t enabled = (i & 1) ? (rng() & 0xffffff00u) : rng();
        if (i % 3 == 0) enabled &= 1;
        // The found byte is random in its upper bits too; only its low byte is the return value's al.
        gFoundAl = (i % 5 == 0) ? 0 : static_cast<uint8_t>(rng() | 1);
        if (i % 7 == 1) gFoundAl = 0; // a few more not-found cases
        // A found child is always stored. When not found, the lookup writes the child on some runs,
        // and the original releases what it holds; when it writes nothing, the local stays null.
        gChild = (gFoundAl != 0 || i % 11 == 0) ? child : nullptr;
        uint8_t foundAl = gFoundAl;

        gCallCount = 0;
        uint8_t ra = orig(window, id, enabled);
        int countA = gCallCount;
        Call logA[8];
        std::memcpy(logA, gCalls, sizeof(logA));

        gCallCount = 0;
        gFoundAl = foundAl;
        uint8_t rb = FindChildAndSetEnabled(window, id, enabled);
        int countB = gCallCount;

        ++ctx.cases;
        if (ra != rb || countA != countB) ctx.Fail("FindChildAndSetEnabled(%u, %08x): result %u vs %u, calls %d vs %d", id, enabled, ra, rb, countA, countB);
        for (int k = 0; k < countA && k < 8; ++k) {
            if (logA[k].slot != gCalls[k].slot || logA[k].a != gCalls[k].a || logA[k].b != gCalls[k].b) ctx.Fail("FindChildAndSetEnabled(%u, %08x): call %d differs (slot %d vs %d)", id, enabled, k, logA[k].slot, gCalls[k].slot);
        }
    }
}

// --- cUtilitiesDOrigin::SubtractOrigin (0xd13780) --------------------------------------------------

void TestSubtractOrigin(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = void(__thiscall *)(void *, const int32_t *, int32_t *, int32_t *, int32_t *);
    auto orig = ctx.Original<Fn>(0xd13780);
    for (int i = 0; i < 20000; ++i) {
        uint8_t objA[0x40], objB[0x40];
        for (auto &byte : objA) byte = static_cast<uint8_t>(rng());
        std::memcpy(objB, objA, sizeof(objA));

        int32_t wordsA[8], wordsB[8];
        for (auto &w : wordsA) w = static_cast<int32_t>(rng());
        std::memcpy(wordsB, wordsA, sizeof(wordsA));

        // The source is words s..s+2 and the outputs are any words, so they may alias the source or each
        // other: the reads and writes must then happen in the same order on both sides.
        int s = static_cast<int>(rng() % 6);
        int o0 = static_cast<int>(rng() % 8), o1 = static_cast<int>(rng() % 8), o2 = static_cast<int>(rng() % 8);
        if (i % 4 == 0) { o0 = s; o1 = s + 1; o2 = s + 2; } // in-place
        if (i % 9 == 0) { o0 = s + 1; } // output into the next source word

        orig(objA, &wordsA[s], &wordsA[o0], &wordsA[o1], &wordsA[o2]);
        reinterpret_cast<cUtilitiesDOrigin *>(objB)->SubtractOrigin(&wordsB[s], &wordsB[o0], &wordsB[o1], &wordsB[o2]);

        ++ctx.cases;
        if (std::memcmp(wordsA, wordsB, sizeof(wordsA)) != 0) ctx.Fail("SubtractOrigin %d (s %d, out %d %d %d): words differ", i, s, o0, o1, o2);
        if (std::memcmp(objA, objB, sizeof(objA)) != 0) ctx.Fail("SubtractOrigin %d: object changed", i);
    }
}

// --- trees (MSVC _Tree_node layout) ------------------------------------------------------------------

struct TreeBuild {
    IntTreeNode head;
    IntTreeNode nodes[16];
    IntTree tree;
};

// Random binary search tree shape with null children pointing at the head (MSVC style), and the root
// stored in head.parent. `n` nodes take keys from a small range plus the extremes, so duplicates and
// misses both occur.
void BuildTree(TreeBuild &t, std::mt19937 &rng, int n)
{
    std::memset(&t, 0, sizeof(t));
    IntTreeNode *head = &t.head;
    head->isNil = 1;
    head->left = head->right = head;
    head->parent = head; // empty: the root is the head itself
    IntTreeNode *root = head;
    for (int i = 0; i < n; ++i) {
        IntTreeNode *node = &t.nodes[i];
        node->isNil = 0;
        node->color = static_cast<uint8_t>(rng() & 1);
        uint32_t kind = rng() % 10;
        if (kind == 0) node->key = (rng() & 1) ? INT_MIN : INT_MAX;
        else node->key = static_cast<int32_t>(rng() % 41) - 20;
        node->value = rng();
        node->left = node->right = head;
        if (root == head) {
            root = node;
            node->parent = head;
            continue;
        }
        IntTreeNode *cur = root;
        for (;;) {
            if (node->key < cur->key) {
                if (cur->left == head) {
                    cur->left = node;
                    break;
                }
                cur = cur->left;
            } else {
                if (cur->right == head) {
                    cur->right = node;
                    break;
                }
                cur = cur->right;
            }
        }
        node->parent = cur;
    }
    // The head's parent is the root; the root's parent is the head.
    head->parent = root;
    if (root != head) root->parent = head;
    t.tree.mHead = head;
    t.tree.mSize = static_cast<uint32_t>(n);
}

int32_t RandomKey(std::mt19937 &rng)
{
    uint32_t kind = rng() % 10;
    if (kind == 0) return (rng() & 1) ? INT_MIN : INT_MAX;
    return static_cast<int32_t>(rng() % 43) - 21;
}

// --- IntTree::LowerBound (0x5f921a) -------------------------------------------------------------------

void TestLowerBound(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = void *(__thiscall *)(void *, void *, const int32_t *);
    auto orig = ctx.Original<Fn>(0x5f921a);
    for (int i = 0; i < 20000; ++i) {
        auto *t = new TreeBuild;
        BuildTree(*t, rng, static_cast<int>(rng() % 15));
        int32_t key = RandomKey(rng);

        IntTreeFindResult outA, outB;
        std::memset(&outA, 0xa5, sizeof(outA));
        std::memset(&outB, 0xa5, sizeof(outB));
        void *ra = orig(&t->tree, &outA, &key);
        void *rb = t->tree.LowerBound(&outB, &key);

        ++ctx.cases;
        if (ra != &outA || rb != &outB) ctx.Fail("LowerBound %d: return %p / %p", i, ra, rb);
        if (std::memcmp(&outA, &outB, sizeof(outA)) != 0) ctx.Fail("LowerBound %d key %d: out (%p %u %p) vs (%p %u %p)", i, key, static_cast<void *>(outA.last), outA.flag, static_cast<void *>(outA.bound), static_cast<void *>(outB.last), outB.flag, static_cast<void *>(outB.bound));
        delete t;
    }
}

// --- IntTree::FindNode (0x5f91ea) ---------------------------------------------------------------------

void TestFindNode(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = IntTreeNode *(__thiscall *)(void *, const int32_t *);
    auto orig = ctx.Original<Fn>(0x5f91ea);
    for (int i = 0; i < 20000; ++i) {
        auto *t = new TreeBuild;
        BuildTree(*t, rng, static_cast<int>(rng() % 15));
        // Keys taken from the tree half the time, so hits are common.
        int32_t key = (i & 1) && t->tree.mSize ? t->nodes[rng() % t->tree.mSize].key : RandomKey(rng);
        IntTreeNode *ra = orig(&t->tree, &key);
        IntTreeNode *rb = t->tree.FindNode(&key);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("FindNode %d key %d: %p vs %p (head %p)", i, key, static_cast<void *>(ra), static_cast<void *>(rb), static_cast<void *>(&t->head));
        delete t;
    }
}

// --- KeyNotLessThanNode (0x5b7da4) --------------------------------------------------------------------

void TestKeyNotLessThanNode(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = uint8_t(__stdcall *)(const void *, const int32_t *);
    auto orig = ctx.Original<Fn>(0x5b7da4);
    for (int i = 0; i < 20000; ++i) {
        uint8_t raw[0x18];
        for (auto &byte : raw) byte = static_cast<uint8_t>(rng());
        auto *node = reinterpret_cast<IntTreeNode *>(raw);
        // isnil is tested as a nonzero byte, so the random byte covers that and the usual 0/1 values.
        if (i % 3 == 0) node->isNil = 0;
        if (i % 5 == 0) node->isNil = 1;
        node->key = RandomKey(rng);
        int32_t key = (i % 2) ? node->key : RandomKey(rng);
        // Neighbours of the node key, computed unsigned so that INT_MIN and INT_MAX wrap instead of overflowing.
        if (i % 4 == 1) key = static_cast<int32_t>(static_cast<uint32_t>(node->key) - 1u);
        if (i % 4 == 2) key = static_cast<int32_t>(static_cast<uint32_t>(node->key) + 1u);
        uint8_t ra = orig(node, &key);
        uint8_t rb = KeyNotLessThanNode(node, &key);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("KeyNotLessThanNode isnil %u key %d node %d: %u vs %u", node->isNil, key, node->key, ra, rb);
    }
}

// --- cUtilitiesDTreeOwner::ValueForKey (0x93d6eb) -----------------------------------------------------

void TestValueForKey(difftest::Context &ctx, std::mt19937 &rng)
{
    using Fn = uint32_t(__thiscall *)(void *, int32_t);
    auto orig = ctx.Original<Fn>(0x93d6eb);
    for (int i = 0; i < 20000; ++i) {
        auto *t = new TreeBuild;
        BuildTree(*t, rng, static_cast<int>(rng() % 15));
        // The owner's other bytes are random and the tree is at +0x100 (the owner's map).
        uint8_t owner[0x108];
        for (auto &byte : owner) byte = static_cast<uint8_t>(rng());
        std::memcpy(owner + 0x100, &t->tree, sizeof(IntTree));
        int32_t key = (i & 1) && t->tree.mSize ? t->nodes[rng() % t->tree.mSize].key : RandomKey(rng);
        uint32_t ra = orig(owner, key); // the original takes the owner and adds 0x100 for its map
        uint32_t rb = reinterpret_cast<cUtilitiesDTreeOwner *>(owner)->ValueForKey(key);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("ValueForKey %d key %d: %08x vs %08x", i, key, ra, rb);
        delete t;
    }
}

} // namespace

DIFF_TEST(UtilitiesD_FindChildAndSetEnabled, 0x9351fd)
{
    std::mt19937 rng(0x9351fd);
    TestFindChildAndSetEnabled(ctx, rng);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

DIFF_TEST(UtilitiesD_SubtractOrigin, 0xd13780)
{
    std::mt19937 rng(0xd13780);
    TestSubtractOrigin(ctx, rng);
}

DIFF_TEST(UtilitiesD_KeyNotLessThanNode, 0x5b7da4)
{
    std::mt19937 rng(0x5b7da4);
    TestKeyNotLessThanNode(ctx, rng);
}

DIFF_TEST(UtilitiesD_LowerBound, 0x5f921a)
{
    std::mt19937 rng(0x5f921a);
    TestLowerBound(ctx, rng);
}

DIFF_TEST(UtilitiesD_FindNode, 0x5f91ea)
{
    std::mt19937 rng(0x5f91ea);
    TestFindNode(ctx, rng);
}

DIFF_TEST(UtilitiesD_ValueForKey, 0x93d6eb)
{
    std::mt19937 rng(0x93d6eb);
    TestValueForKey(ctx, rng);
}
