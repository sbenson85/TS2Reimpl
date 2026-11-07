// Differential test for cGZPersistResourceManager::vf21 (0x9b5b20, unregisters a factory). Each case
// builds two identical managers whose hash maps (+0x28 factory id -> factory, +0x48 type -> factory
// id) hold random entries in the game's layout (a list whose nodes are grouped by bucket, each bucket
// {first, last} or {head, head} when empty, FNV-1a over the key's bytes), with small key sets so
// that ids repeat and types share factories. The factories are fakes whose Release records; lock A
// (+0x20) records Enter/Leave. Compared: the result byte, the recorded calls, both maps afterwards
// (size, list order of {key, value}, bucket table as list positions) and which nodes were freed
// (difftest::FreeRecorder).
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZPersistResourceManager.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

namespace {

struct Fake {
    void **vt;
    uint32_t id;
};

std::vector<uint32_t> gTrace;
void *gFakeVT[4];

void __fastcall FakeEnter(Fake *self, void *)
{
    gTrace.push_back(self->id);
    gTrace.push_back(1);
}

uint32_t __fastcall FakeLeave(Fake *self, void *)
{
    gTrace.push_back(self->id);
    gTrace.push_back(2);
    return 0x77;
}

uint32_t Hash(uint32_t key)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&key);
    uint32_t hash = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) hash = (hash ^ bytes[i]) * 0x1000193u;
    return hash;
}

struct Entry {
    uint32_t key;
    uint32_t value; // factory map: index of the fake factory; type map: the factory id
};

struct MapState {
    cGZPersistListNode *head = nullptr;
    std::vector<cGZPersistListNode *> nodes; // in list order, as built
    std::vector<cGZPersistBucket> buckets;
};

struct World {
    alignas(8) uint8_t self[0x100];
    Fake lock;
    Fake factories[4];
    MapState maps[2];
};

cGZPersistHashMap &MapAt(World &w, int which)
{
    return *reinterpret_cast<cGZPersistHashMap *>(w.self + (which == 0 ? 0x28 : 0x48));
}

uint32_t ValueOf(World &w, int which, uint32_t value)
{
    return which == 0 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.factories[value])) : value;
}

void BuildMap(World &w, int which, const std::vector<Entry> &entries, uint32_t bucketCount, std::mt19937 &order)
{
    MapState &m = w.maps[which];
    m.head = static_cast<cGZPersistListNode *>(::operator new(sizeof(cGZPersistListNode)));
    m.head->key = 0;
    m.head->value = 0;
    m.buckets.assign(bucketCount, cGZPersistBucket{m.head, m.head});
    // Group the entries by bucket, buckets in a random order.
    std::vector<uint32_t> bucketOrder(bucketCount);
    for (uint32_t i = 0; i < bucketCount; ++i) bucketOrder[i] = i;
    std::shuffle(bucketOrder.begin(), bucketOrder.end(), order);
    cGZPersistListNode *prev = m.head;
    for (uint32_t b : bucketOrder) {
        for (const Entry &e : entries) {
            if ((Hash(e.key) & (bucketCount - 1)) != b) continue;
            auto *node = static_cast<cGZPersistListNode *>(::operator new(sizeof(cGZPersistListNode)));
            node->key = e.key;
            node->value = ValueOf(w, which, e.value);
            node->prev = prev;
            prev->next = node;
            prev = node;
            if (m.buckets[b].first == m.head) m.buckets[b].first = node;
            m.buckets[b].last = node;
            m.nodes.push_back(node);
        }
    }
    prev->next = m.head;
    m.head->prev = prev;
    cGZPersistHashMap &map = MapAt(w, which);
    map.maxLoadFactor = 1.0f;
    map.head = m.head;
    map.size = static_cast<uint32_t>(m.nodes.size());
    map.buckets = m.buckets.data();
    map.bucketsEnd = m.buckets.data() + bucketCount;
    map.bucketsCap = map.bucketsEnd;
    map.mask = bucketCount - 1;
    map.maxIndex = bucketCount;
}

void Build(World &w, uint32_t seed)
{
    std::memset(w.self, 0, sizeof(w.self));
    for (int i = 0; i < 4; ++i) gFakeVT[i] = nullptr;
    gFakeVT[1] = reinterpret_cast<void *>(&FakeEnter);
    gFakeVT[2] = reinterpret_cast<void *>(&FakeLeave);
    w.lock = {gFakeVT, 0xa0};
    for (uint32_t i = 0; i < 4; ++i) w.factories[i] = {gFakeVT, 0xb0 + i};
    // Lock A is embedded at +0x20 (its vtable pointer, then the id word the fake records).
    std::memcpy(w.self + 0x20, &w.lock, sizeof(w.lock));

    std::mt19937 rng(seed);
    uint32_t idCount = 1 + rng() % 5;
    std::vector<Entry> factories, types;
    uint32_t nf = rng() % 7;
    for (uint32_t i = 0; i < nf; ++i) factories.push_back({1 + rng() % idCount, rng() % 4});
    uint32_t nt = rng() % 9;
    for (uint32_t i = 0; i < nt; ++i) types.push_back({rng() % 3 ? 0x1000 + rng() % 16 : rng(), 1 + rng() % (idCount + 1)});
    uint32_t buckets = 1u << (rng() % 4);
    BuildMap(w, 0, factories, buckets, rng);
    BuildMap(w, 1, types, 1u << (rng() % 4), rng);
}

// A node's position in the as-built list, 0xffff for the head, 0xfffe for anything else.
uint32_t Pos(const MapState &m, const cGZPersistListNode *n)
{
    if (n == m.head) return 0xffff;
    for (size_t i = 0; i < m.nodes.size(); ++i)
        if (m.nodes[i] == n) return static_cast<uint32_t>(i);
    return 0xfffe;
}

std::vector<uint32_t> Summary(World &w, const difftest::FreeRecorder &rec)
{
    std::vector<uint32_t> s;
    for (int which = 0; which < 2; ++which) {
        MapState &m = w.maps[which];
        cGZPersistHashMap &map = MapAt(w, which);
        s.push_back(map.size);
        s.push_back(map.head == m.head ? 1u : 0u);
        int guard = 0;
        for (cGZPersistListNode *n = m.head->next; n != m.head && guard < 64; n = n->next, ++guard) {
            s.push_back(Pos(m, n));
            s.push_back(n->key);
            s.push_back(which == 0 ? (n->value - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.factories[0]))) : n->value);
            s.push_back(Pos(m, n->prev));
        }
        for (const cGZPersistBucket &b : m.buckets) {
            s.push_back(Pos(m, b.first));
            s.push_back(Pos(m, b.last));
        }
        for (cGZPersistListNode *n : m.nodes) s.push_back(rec.Freed(n) ? 1u : 0u);
    }
    return s;
}

// How many of the world's nodes the run freed.
size_t FreedNodes(const World &w, const difftest::FreeRecorder &rec)
{
    size_t n = 0;
    for (const MapState &m : w.maps)
        for (cGZPersistListNode *node : m.nodes) n += rec.Freed(node) ? 1 : 0;
    return n;
}

// Frees what is still linked (the removed nodes were freed by the run), then the heads.
void Cleanup(World &w)
{
    for (MapState &m : w.maps) {
        for (cGZPersistListNode *n = m.head->next; n != m.head;) {
            cGZPersistListNode *next = n->next;
            ::operator delete(n);
            n = next;
        }
        ::operator delete(m.head);
    }
}

using Vf21Fn = uint8_t(__thiscall *)(void *, uint32_t);

} // namespace

DIFF_TEST(GZPersistResourceManager_UnregisterFactory, 0x9b5b20)
{
    auto vf21 = ctx.Original<Vf21Fn>(0x9b5b20);
    std::mt19937 rng(21);
    for (int i = 0; i < 5000; ++i) {
        uint32_t seed = rng();
        uint32_t id = rng() % 8 ? 1 + rng() % 6 : rng();
        World a, b;
        Build(a, seed);
        Build(b, seed);

        gTrace.clear();
        difftest::FreeRecorder ra;
        uint8_t resA = vf21(a.self, id);
        ra.Stop();
        std::vector<uint32_t> ta = gTrace;
        std::vector<uint32_t> sa = Summary(a, ra);

        gTrace.clear();
        difftest::FreeRecorder rb;
        uint8_t resB = reinterpret_cast<cGZPersistResourceManager *>(b.self)->vf21(id);
        rb.Stop();
        std::vector<uint32_t> tb = gTrace;
        std::vector<uint32_t> sb = Summary(b, rb);

        ++ctx.cases;
        if (resA != resB || ta != tb)
            ctx.Fail("vf21 case %d: original returned %u (%zu trace words), ours %u (%zu)", i, resA, ta.size(), resB, tb.size());
        if (sa != sb) {
            size_t k = 0;
            while (k < sa.size() && k < sb.size() && sa[k] == sb[k]) ++k;
            ctx.Fail("vf21 case %d: the maps or frees differ afterwards (word %zu of %zu/%zu: %#x against %#x)", i, k, sa.size(),
                     sb.size(), k < sa.size() ? sa[k] : 0u, k < sb.size() ? sb[k] : 0u);
        }
        // The game's free can release blocks of its own (the original's count is larger in a few
        // cases): ours must free exactly the removed nodes.
        if (rb.Count() != FreedNodes(b, rb)) ctx.Fail("vf21 case %d: ours freed %zu blocks, %zu of them nodes", i, rb.Count(), FreedNodes(b, rb));
        Cleanup(a);
        Cleanup(b);
    }
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
