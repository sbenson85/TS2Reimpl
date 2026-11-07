// Differential tests for cTSPersistContext::vf9 (src/ts/cTSPersistContext.h has the layout). Each
// world is a fake context with a random hash-bucketed node list. Both sides run on identical worlds
// (same seed, so the same shape, keys and refcounted objects; only the addresses differ, and results
// are compared as object and node ids). Compared: the return byte (al), the release calls (slot 2 of
// each refcounted object, in order), the list read from the sentinel forward with each node's prev,
// the bucket entries, the count, and the context bytes outside the pointer fields.
//
// Harness limit: the nodes that get deleted are not observable (the heap is not inspected), so a
// mutant that skips operator delete would survive. The sized delete is checked by the remaining-node
// bookkeeping only: nodes that are left are freed by the test, and removal is compared through the list.
#include "difftest.h"
#include "free_scope.h"
#include "ts/cTSPersistContext.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using VfDelete = uint8_t(__thiscall *)(void *, const uint32_t *);

constexpr uint32_t kNodeBytes = 0x2c;
constexpr uint32_t kSentinelId = 0xfffe;
constexpr uint32_t kNullId = 0xfffd;
constexpr uint32_t kUnknownId = 0xffff;
constexpr uint32_t kObjectIdBase = 100;
constexpr size_t kCtxBytes = 0x30;

std::vector<uint32_t> gTrace;
std::vector<std::pair<const void *, uint32_t>> gIds;

uint32_t IdOf(const void *p)
{
    if (!p) return kNullId;
    for (const auto &e : gIds)
        if (e.first == p) return e.second;
    return kUnknownId;
}

// Refcounted object's vtable slot 2 (Release). Recorded with __fastcall so it has no stack argument,
// as the game's call through `call [eax+8]` does.
uint32_t __fastcall SlotRelease(void *self, void *)
{
    gTrace.push_back(IdOf(self));
    return 0;
}

struct FakeObject {
    void *vtable;
};

struct World {
    alignas(8) uint8_t ctx[kCtxBytes];
    alignas(8) uint8_t sentinel[kNodeBytes];
    void *vt[3];
    FakeObject objs[4];
    std::vector<uint8_t *> nodes;
    std::vector<cTSPersistContextBucket> buckets;
    uint32_t key = 0;

    World()
    {
        std::memset(sentinel, 0, sizeof(sentinel));
    }
    ~World()
    {
        for (uint8_t *n : nodes) ::operator delete(n, kNodeBytes);
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

cTSPersistContextNode *NodeAt(const World &w, size_t i)
{
    return reinterpret_cast<cTSPersistContextNode *>(w.nodes[i]);
}

uint32_t BucketOf(const cTSPersistContextNode *node, uint32_t mask)
{
    return (node->key[0] + node->key[1] + node->key[2] + node->key[3]) & mask;
}

// The shape comes from the seed alone, so both worlds are identical.
void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto keyDword = [&rng]() { return rng() % 3 ? rng() % 4 : rng(); };

    w.vt[0] = nullptr;
    w.vt[1] = nullptr;
    w.vt[2] = reinterpret_cast<void *>(&SlotRelease);
    for (auto &o : w.objs) o.vtable = w.vt;
    std::memset(w.ctx, 0, sizeof(w.ctx));
    for (size_t i = 0; i < kCtxBytes; ++i) w.ctx[i] = static_cast<uint8_t>(rng());

    size_t n = rng() % 8;
    uint32_t mask = (1u << (rng() % 4)) - 1;
    w.key = rng() % 3 ? rng() % 4 : rng();

    // Nodes are created in random order, then sorted by bucket so that each bucket is a contiguous run
    // of the list (the invariant the bucket first/last ends rely on).
    std::vector<cTSPersistContextNode> spec(n);
    for (size_t i = 0; i < n; ++i) {
        cTSPersistContextNode &s = spec[i];
        for (auto &k : s.key) k = keyDword();
        s.refA = rng() % 3 ? &w.objs[rng() % 4] : nullptr;
        s.refB = rng() % 3 ? &w.objs[rng() % 4] : nullptr;
    }
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::shuffle(order.begin(), order.end(), rng);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return BucketOf(&spec[a], mask) < BucketOf(&spec[b], mask); });

    auto *sentinel = reinterpret_cast<cTSPersistContextNode *>(w.sentinel);
    std::vector<cTSPersistContextNode *> linked;
    for (size_t idx : order) {
        auto *node = reinterpret_cast<cTSPersistContextNode *>(::operator new(kNodeBytes));
        std::memset(node, 0, kNodeBytes);
        std::memcpy(node->key, spec[idx].key, sizeof(node->key));
        node->refA = spec[idx].refA;
        node->refB = spec[idx].refB;
        w.nodes.push_back(reinterpret_cast<uint8_t *>(node));
        linked.push_back(node);
    }

    cTSPersistContextNode *prev = sentinel;
    for (auto *node : linked) {
        node->prev = prev;
        prev->next = node;
        prev = node;
    }
    prev->next = sentinel;
    sentinel->prev = prev;

    w.buckets.assign(mask + 1, cTSPersistContextBucket{sentinel, sentinel});
    for (auto *node : linked) {
        cTSPersistContextBucket &b = w.buckets[BucketOf(node, mask)];
        if (b.first == sentinel) b.first = node;
        b.last = node;
    }

    Put(w.ctx, 0x10, Addr(w.sentinel));
    Put(w.ctx, 0x14, static_cast<uint32_t>(n));
    Put(w.ctx, 0x18, Addr(w.buckets.data()));
    Put(w.ctx, 0x24, mask);
}

struct Outcome {
    uint8_t ret = 0;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
    std::vector<uint8_t> ctxBytes;
};

// Identifies objects and nodes by id, so the two worlds' addresses don't show in the comparison.
void Register(const World &w)
{
    gIds.clear();
    gIds.push_back({w.sentinel, kSentinelId});
    for (size_t i = 0; i < w.nodes.size(); ++i) gIds.push_back({w.nodes[i], static_cast<uint32_t>(i)});
    for (size_t i = 0; i < 4; ++i) gIds.push_back({&w.objs[i], kObjectIdBase + static_cast<uint32_t>(i)});
}

Outcome Snapshot(const World &w, uint8_t ret, const std::vector<uint32_t> &trace)
{
    Outcome out;
    out.ret = ret;
    out.trace = trace;

    const auto *sentinel = reinterpret_cast<const cTSPersistContextNode *>(w.sentinel);
    const uint32_t count = *reinterpret_cast<const uint32_t *>(w.ctx + 0x14);
    out.state.push_back(count);
    out.state.push_back(IdOf(sentinel->prev));

    // Walk forward, bounded. Each step records the node and its prev link.
    const cTSPersistContextNode *node = sentinel->next;
    for (int steps = 0; steps < 16 && node != sentinel; ++steps) {
        uint32_t id = IdOf(node);
        out.state.push_back(id);
        if (id == kUnknownId) break;
        out.state.push_back(IdOf(node->prev));
        node = node->next;
    }
    out.state.push_back(0xabcd);

    // Which nodes are still live heap blocks: a removed node must have been freed by the function
    // (the FreeRecorder Run() started right before the call).
    for (uint8_t *p : w.nodes) out.state.push_back(difftest::FreedSinceBegin(p) ? 0u : 1u);

    // Buckets, read from the array the context points at (the mask is in the context bytes).
    const uint32_t mask = *reinterpret_cast<const uint32_t *>(w.ctx + 0x24);
    cTSPersistContextBucket *buckets = nullptr;
    std::memcpy(&buckets, w.ctx + 0x18, sizeof(buckets));
    for (uint32_t i = 0; i <= mask && i < 8; ++i) {
        out.state.push_back(IdOf(buckets[i].first));
        out.state.push_back(IdOf(buckets[i].last));
    }

    // Context bytes outside the two pointer fields (+0x10 and +0x18 are compared through the state).
    out.ctxBytes.assign(w.ctx, w.ctx + kCtxBytes);
    for (size_t i = 0x10; i < 0x20; ++i) out.ctxBytes[i] = 0;
    return out;
}

// Calls the function on `w` and returns what it did. `call` gets the fake context and the key pointer.
template <typename Call> Outcome Run(World &w, Call call)
{
    Register(w);
    gTrace.clear();
    uint32_t key = w.key;
    difftest::BeginFrees();
    uint8_t ret = call(w.ctx, &key);
    difftest::CurrentFrees()->Stop();
    return Snapshot(w, ret, gTrace);
}

// Frees the nodes still in the list (the removed ones were freed by the function under test).
void FreeRemaining(World &w)
{
    const auto *sentinel = reinterpret_cast<const cTSPersistContextNode *>(w.sentinel);
    std::vector<uint8_t *> remaining;
    const cTSPersistContextNode *node = sentinel->next;
    for (size_t steps = 0; steps < w.nodes.size() && node != sentinel; ++steps) {
        remaining.push_back(reinterpret_cast<uint8_t *>(const_cast<cTSPersistContextNode *>(node)));
        node = node->next;
    }
    for (uint8_t *p : remaining) ::operator delete(p, kNodeBytes);
    // Every node is now either freed by the function (removed) or by the loop above: forget them all
    // so the destructor doesn't free the removed ones a second time.
    w.nodes.clear();
}

uint64_t gRemovingCases = 0;

template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, OrigCall origCall, OursCall oursCall)
{
    auto a = std::make_unique<World>();
    auto b = std::make_unique<World>();
    Build(*a, seed);
    Build(*b, seed);

    Outcome oa = Run(*a, origCall);
    Outcome ob = Run(*b, oursCall);
    ++ctx.cases;
    if (oa.ret) ++gRemovingCases;

    if (oa.ret != ob.ret) {
        ctx.Fail("%s seed %u: original returned %u, ours %u", name, seed, oa.ret, ob.ret);
    }
    if (oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: release calls differ (original %zu words, ours %zu)", name, seed, oa.trace.size(), ob.trace.size());
    }
    if (oa.state != ob.state) {
        ctx.Fail("%s seed %u: list or bucket state differs (original %zu words, ours %zu)", name, seed, oa.state.size(), ob.state.size());
    }
    if (oa.ctxBytes != ob.ctxBytes) {
        ctx.Fail("%s seed %u: context bytes outside the list fields differ", name, seed);
    }

    FreeRemaining(*a);
    FreeRemaining(*b);
}

} // namespace

DIFF_TEST(TSPersistContext_vf9, 0x1fac50)
{
    auto orig = ctx.Original<VfDelete>(0x1fac50);
    gRemovingCases = 0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Compare(
            ctx, "vf9", seed,
            [&](void *self, const uint32_t *key) -> uint8_t { return orig(self, key); },
            [&](void *self, const uint32_t *key) -> uint8_t {
                return static_cast<uint8_t>(reinterpret_cast<cTSPersistContext *>(self)->vf9(key));
            });
    }
    // The generator must actually exercise removal, or the comparison proves nothing (about 28% of the
    // seeds remove at least one node).
    if (gRemovingCases < 500)ctx.Fail("only %llu cases removed a node", static_cast<unsigned long long>(gRemovingCases));
}
