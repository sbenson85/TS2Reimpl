// Differential tests for the hash-table lookups in std/StdHash.h. Each case builds two identical
// tables in heap memory (MSVC layout: list with sentinel head, bucket array, mask), one searched by
// the original and one by the reimplementation. Keys come from small pools with near-duplicates, so
// same-bucket collisions, duplicate keys and keys differing in one byte all occur. Compared: the
// returned pointer, the result pair (as node indices), and the table after the lookup (links, bucket
// ranges, and the fields the lookup must not read, which hold random values).
#include "difftest.h"
#include "std/StdHash.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kNodeSize = 0x40; // nodes are bigger than the key, and their junk is random

using LookupFn = StdHashFindResult *(__thiscall *)(void *, StdHashFindResult *, const void *, uint32_t);

uint32_t Fnv(const void *data, size_t size)
{
    uint32_t h = 2166136261u;
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

// One copy of a table and everything it points at. Destroys the std::strings it constructed.
struct Built {
    std::unique_ptr<StdHashTable> table;
    std::unique_ptr<StdHashNode> head;
    std::unique_ptr<StdHashBucket[]> buckets;
    std::vector<std::unique_ptr<uint8_t[]>> nodeMem;
    std::vector<std::unique_ptr<uint8_t[]>> argMem;
    std::vector<StdHashNode *> nodes; // by item index
    std::vector<std::function<void()>> cleanup;

    Built() : table(new StdHashTable()), head(new StdHashNode()) {}
    ~Built()
    {
        for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it) (*it)();
    }
    Built(const Built &) = delete;
    Built &operator=(const Built &) = delete;
};

using Str = std::string;

// Placement-constructs a std::string at `at`; the Built destroys it.
void ConstructString(Built &b, uint8_t *at, const Str &s)
{
    new (at) Str(s);
    auto *p = reinterpret_cast<Str *>(at);
    b.cleanup.push_back([p] { p->~Str(); });
}

// Fixed-size keys of N bytes. The pool is biased to 0, 1, 0x7f, 0x80 and 0xff, so equal and
// one-byte-different keys are common.
template <size_t N> struct FixedTraits {
    using Key = std::array<uint8_t, N>;

    static Key Random(std::mt19937 &rng)
    {
        static const uint8_t kBiased[] = {0, 1, 0x7f, 0x80, 0xff};
        Key k{};
        for (auto &c : k) c = (rng() % 3 == 0) ? static_cast<uint8_t>(rng()) : kBiased[rng() % 5];
        return k;
    }
    static Key Mutate(Key k, std::mt19937 &rng)
    {
        k[rng() % N] = static_cast<uint8_t>(rng());
        return k;
    }
    static uint32_t Hash(const Key &k) { return Fnv(k.data(), N); }
    static void PlaceNode(Built &, uint8_t *node, const Key &k) { std::memcpy(node + 8, k.data(), N); }
    static const void *MakeArg(Built &b, const Key &k)
    {
        b.argMem.push_back(std::make_unique<uint8_t[]>(N));
        std::memcpy(b.argMem.back().get(), k.data(), N);
        return b.argMem.back().get();
    }
};

// std::string keys, stored at NodeOffset in the node, and at ArgOffset in the search argument (the
// argument is an object with the string at that offset). Equality is by length and bytes, and the
// alphabet includes NULs.
template <size_t NodeOffset, size_t ArgOffset> struct StringTraits {
    using Key = Str;

    static Key Random(std::mt19937 &rng)
    {
        static const char kAlphabet[] = {'a', 'B', '\0', 'z', '_'};
        size_t len = rng() % 4 == 0 ? 14 + rng() % 4 : rng() % 41; // around the small-string boundary
        Str s;
        for (size_t i = 0; i < len; ++i) s += kAlphabet[rng() % 5];
        return s;
    }
    static Key Mutate(Key s, std::mt19937 &rng)
    {
        switch (rng() % 3) {
        case 0:
            if (!s.empty()) s[rng() % s.size()] = static_cast<char>('a' + rng() % 3);
            break;
        case 1: s += 'z'; break;
        default:
            if (!s.empty()) s.pop_back();
            break;
        }
        return s;
    }
    static uint32_t Hash(const Key &k) { return Fnv(k.data(), k.size()); }
    static void PlaceNode(Built &b, uint8_t *node, const Key &k) { ConstructString(b, node + NodeOffset, k); }
    static const void *MakeArg(Built &b, const Key &k)
    {
        b.argMem.push_back(std::make_unique<uint8_t[]>(0x20));
        uint8_t *mem = b.argMem.back().get();
        ConstructString(b, mem + ArgOffset, k);
        return mem;
    }
};

// C strings: the node holds a `const char *` at +8. The search key is a `const char *` passed by
// address. The pool strings have no NULs (they are compared as C strings).
struct CStringTraits {
    using Key = Str;

    static Key Random(std::mt19937 &rng)
    {
        static const char kAlphabet[] = {'a', 'B', 'z', '_'};
        size_t len = rng() % 4 == 0 ? 14 + rng() % 4 : rng() % 41;
        Str s;
        for (size_t i = 0; i < len; ++i) s += kAlphabet[rng() % 4];
        return s;
    }
    static Key Mutate(Key s, std::mt19937 &rng)
    {
        switch (rng() % 3) {
        case 0:
            if (!s.empty()) s[rng() % s.size()] = static_cast<char>('a' + rng() % 3);
            break;
        case 1: s += 'z'; break;
        default:
            if (!s.empty()) s.pop_back();
            break;
        }
        return s;
    }
    static uint32_t Hash(const Key &k) { return Fnv(k.data(), k.size()); }
    static void PlaceNode(Built &, uint8_t *node, const Key &k)
    {
        const char *p = k.c_str();
        std::memcpy(node + 8, &p, sizeof(p));
    }
    static const void *MakeArg(Built &b, const Key &k)
    {
        b.argMem.push_back(std::make_unique<uint8_t[]>(sizeof(const char *)));
        const char *p = k.c_str();
        std::memcpy(b.argMem.back().get(), &p, sizeof(p));
        return b.argMem.back().get();
    }
};

// Builds one copy. `items` are pool indices, one node each. Nodes are linked bucket by bucket (the
// buckets in a random order) and, within a bucket, in item order, so each bucket is contiguous from
// first to last. Empty buckets have last == head. The fields the lookup must not read get random
// values, identical in both copies.
template <class Traits>
std::unique_ptr<Built> Build(const std::vector<typename Traits::Key> &pool, const std::vector<int> &items, uint32_t mask,
                             uint32_t seed)
{
    auto b = std::make_unique<Built>();
    std::mt19937 rng(seed);
    const uint32_t count = mask + 1;

    std::vector<std::vector<int>> perBucket(count);
    for (size_t i = 0; i < items.size(); ++i)
        perBucket[Traits::Hash(pool[items[i]]) & mask].push_back(static_cast<int>(i));

    std::vector<uint32_t> order(count);
    std::iota(order.begin(), order.end(), 0u);
    std::shuffle(order.begin(), order.end(), rng);

    StdHashNode *head = b->head.get();
    head->next = head;
    head->prev = head;
    b->nodes.assign(items.size(), nullptr);
    for (size_t i = 0; i < items.size(); ++i) {
        b->nodeMem.push_back(std::make_unique<uint8_t[]>(kNodeSize));
        uint8_t *mem = b->nodeMem.back().get();
        for (size_t j = 0; j < kNodeSize; ++j) mem[j] = static_cast<uint8_t>(rng());
        b->nodes[i] = reinterpret_cast<StdHashNode *>(mem);
        Traits::PlaceNode(*b, mem, pool[items[i]]);
    }

    StdHashNode *prev = head;
    for (uint32_t bi : order)
        for (int i : perBucket[bi]) {
            StdHashNode *n = b->nodes[i];
            prev->next = n;
            n->prev = prev;
            prev = n;
        }
    prev->next = head;
    head->prev = prev;

    b->buckets.reset(new StdHashBucket[count]);
    for (uint32_t bi = 0; bi < count; ++bi) {
        const auto &list = perBucket[bi];
        if (list.empty()) {
            // Only `last` matters for an empty bucket. `first` is left stale (a random node or the
            // head), as it can be in a real table, so reading it would diverge.
            StdHashNode *stale = rng() % 2 ? head : b->nodes.empty() ? head : b->nodes[rng() % b->nodes.size()];
            b->buckets[bi] = {stale, head};
        } else {
            b->buckets[bi] = {b->nodes[list.front()], b->nodes[list.back()]};
        }
    }

    StdHashTable *t = b->table.get();
    t->mUnknown00 = static_cast<uint32_t>(rng());
    t->mHead = head;
    t->mUnknown08 = static_cast<uint32_t>(rng());
    t->mBuckets = b->buckets.get();
    t->mUnknown10[0] = static_cast<uint32_t>(rng());
    t->mUnknown10[1] = static_cast<uint32_t>(rng());
    t->mMask = mask;
    return b;
}

// Index of a pointer in a built table: -1 for null, -2 for the head, 900 for anything unexpected.
int IndexOf(const Built &b, const void *p)
{
    if (!p) return -1;
    if (p == b.head.get()) return -2;
    for (size_t i = 0; i < b.nodes.size(); ++i)
        if (p == b.nodes[i]) return static_cast<int>(i);
    return 900;
}

// Everything a lookup must leave unchanged, with pointers as indices (so the two copies compare equal).
std::vector<long long> Snapshot(const Built &b)
{
    std::vector<long long> s;
    const StdHashTable *t = b.table.get();
    s.push_back(t->mUnknown00);
    s.push_back(IndexOf(b, t->mHead));
    s.push_back(t->mUnknown08);
    s.push_back(t->mUnknown10[0]);
    s.push_back(t->mUnknown10[1]);
    s.push_back(t->mMask);
    s.push_back(IndexOf(b, b.head->next));
    s.push_back(IndexOf(b, b.head->prev));
    for (const auto *n : b.nodes) {
        s.push_back(IndexOf(b, n->next));
        s.push_back(IndexOf(b, n->prev));
    }
    for (uint32_t bi = 0; bi <= t->mMask; ++bi) {
        s.push_back(IndexOf(b, t->mBuckets[bi].first));
        s.push_back(IndexOf(b, t->mBuckets[bi].last));
    }
    return s;
}

// One lookup on one copy, with its outcome: the out pair as node indices, whether the call returned
// the out pointer, and whether the table is unchanged afterwards.
struct Outcome {
    int node;
    int where;
    bool returnedOut;
    bool tableIntact;
};

template <typename Call> Outcome RunOn(Built &b, const void *arg, uint32_t hash, Call call)
{
    constexpr uintptr_t kSentinel = 0x5a5a5a5au;
    StdHashFindResult out{reinterpret_cast<StdHashNode *>(kSentinel), reinterpret_cast<StdHashNode *>(kSentinel)};
    const auto before = Snapshot(b);
    void *ret = call(b.table.get(), &out, arg, hash);
    Outcome o;
    o.node = IndexOf(b, out.node);
    o.where = IndexOf(b, out.where);
    o.returnedOut = ret == &out;
    o.tableIntact = before == Snapshot(b);
    return o;
}

// Runs `iterations` random cases of the lookup at `rva`, against the reimplementation Traits::Ours.
template <class Traits> void RunLookupTests(difftest::Context &ctx, uint32_t rva, uint32_t iterations, uint32_t seed)
{
    auto orig = ctx.Original<LookupFn>(rva);
    auto ours = [](void *self, StdHashFindResult *out, const void *key, uint32_t hash) {
        return Traits::Ours(static_cast<StdHashTable *>(self), out, key, hash);
    };
    for (uint32_t iter = 0; iter < iterations; ++iter) {
        std::mt19937 rng(seed + iter * 7919u);
        std::vector<typename Traits::Key> pool;
        const size_t base = 1 + rng() % 6;
        for (size_t p = 0; p < base; ++p) pool.push_back(Traits::Random(rng));
        const size_t extra = rng() % (base + 1);
        for (size_t p = 0; p < extra; ++p) pool.push_back(Traits::Mutate(pool[rng() % base], rng));
        const size_t absent = pool.size(); // never stored, but it may share a bucket with stored keys
        pool.push_back(rng() % 2 ? Traits::Random(rng) : Traits::Mutate(pool[rng() % base], rng));

        const uint32_t mask = (1u << (rng() % 5)) - 1;
        std::vector<int> items;
        const size_t itemCount = rng() % 20;
        for (size_t i = 0; i < itemCount; ++i) items.push_back(static_cast<int>(rng() % absent));

        const size_t searchIndex = rng() % 4 == 0 ? absent : rng() % pool.size();
        const uint32_t hash = rng() % 8 == 0 ? static_cast<uint32_t>(rng()) : Traits::Hash(pool[searchIndex]);
        const uint32_t buildSeed = static_cast<uint32_t>(rng());

        auto a = Build<Traits>(pool, items, mask, buildSeed);
        auto b = Build<Traits>(pool, items, mask, buildSeed);
        const void *argA = Traits::MakeArg(*a, pool[searchIndex]);
        const void *argB = Traits::MakeArg(*b, pool[searchIndex]);

        Outcome oa = RunOn(*a, argA, hash, orig);
        Outcome ob = RunOn(*b, argB, hash, ours);
        ++ctx.cases;
        if (!oa.returnedOut || !ob.returnedOut) ctx.Fail("lookup did not return its out pointer (case %u)", iter);
        if (!oa.tableIntact) ctx.Fail("original changed the table (case %u)", iter);
        if (!ob.tableIntact) ctx.Fail("reimplementation changed the table (case %u)", iter);
        if (oa.node == 900 || oa.where == 900) ctx.Fail("original returned a pointer outside the table (case %u)", iter);
        if (oa.node != ob.node || oa.where != ob.where) {
            ctx.Fail("lookup differs (case %u, %zu items, mask %x, hash %08x): node %d/%d where %d/%d", iter, items.size(),
                     mask, hash, oa.node, ob.node, oa.where, ob.where);
        }
        if (!ctx.failures.empty()) return;
    }
}

// Wrappers that call the reimplementation member for one key type.
#define HASH_TRAITS(Name, Base, Method)                                                                  \
    struct Name : Base {                                                                                 \
        static StdHashFindResult *Ours(StdHashTable *t, StdHashFindResult *o, const void *k, uint32_t h) \
        {                                                                                                \
            return t->Method(o, k, h);                                                                   \
        }                                                                                                \
    }

using StringAt12Base = StringTraits<0xc, 4>;

HASH_TRAITS(Find32Traits, FixedTraits<4>, Find32);
HASH_TRAITS(Find64Traits, FixedTraits<8>, Find64);
HASH_TRAITS(Find96Traits, FixedTraits<12>, Find96);
HASH_TRAITS(Find128Traits, FixedTraits<16>, Find128);
HASH_TRAITS(StringAt12Traits, StringAt12Base, FindStringAt12);
HASH_TRAITS(CStringTraitsCall, CStringTraits, FindCString);

} // namespace

DIFF_TEST(StdHash_Find32, 0x15ac20) { RunLookupTests<Find32Traits>(ctx, 0x15ac20, 20000, 1003); }
DIFF_TEST(StdHash_Find64_1f10b0, 0x1f10b0) { RunLookupTests<Find64Traits>(ctx, 0x1f10b0, 20000, 1004); }
DIFF_TEST(StdHash_Find96_2c43b0, 0x2c43b0) { RunLookupTests<Find96Traits>(ctx, 0x2c43b0, 20000, 1006); }
DIFF_TEST(StdHash_Find96_4eba70, 0x4eba70) { RunLookupTests<Find96Traits>(ctx, 0x4eba70, 20000, 1007); }
DIFF_TEST(StdHash_Find128, 0x1f5570) { RunLookupTests<Find128Traits>(ctx, 0x1f5570, 20000, 1008); }
DIFF_TEST(StdHash_StringAt12_1d5c20, 0x1d5c20) { RunLookupTests<StringAt12Traits>(ctx, 0x1d5c20, 6000, 1009); }
DIFF_TEST(StdHash_StringAt12_4ebb00, 0x4ebb00) { RunLookupTests<StringAt12Traits>(ctx, 0x4ebb00, 6000, 1010); }
DIFF_TEST(StdHash_CString, 0xcd8b30) { RunLookupTests<CStringTraitsCall>(ctx, 0xcd8b30, 6000, 1012); }
