// Differential tests for AnyStringSet and the string set it holds (src/gz/cGZAny.*): the set's construct /
// destruct / insert (both copies) / find / assignment, and AnyStringSet's copy, destructors, Contains, Assign,
// Compare and Clone.
//
// Every allocation goes through the allocator service (cache 0x10c95d4), here adt::AllocatorFake: its log gives the
// allocation sizes in order, and the blocks it was asked to free are named by what they were (node i of the list,
// node i's string buffer, the bucket vector, the list head) and compared in order. Sets are compared whole: max load
// factor, size, the strings in list order (with their capacities), the bucket vector as node indices, mask and
// bucket count.
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZAny.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

// MSVC 14.40's _Hash and list node, as raw memory.
struct RawNode {
    RawNode *next;
    RawNode *prev;
    GZString value;
};
static_assert(sizeof(RawNode) == 0x20);

struct RawHash {
    uint32_t maxLoadBits;
    RawNode *head;
    uint32_t size;
    RawNode **vecBegin;
    RawNode **vecEnd;
    RawNode **vecCap;
    uint32_t mask;
    uint32_t maxIdx;
};
static_assert(sizeof(RawHash) == sizeof(GZStringSet));

int NodeIndex(const RawHash &h, const RawNode *p)
{
    if (p == h.head) return -1;
    int i = 0;
    for (RawNode *n = h.head->next; n != h.head; n = n->next, ++i)
        if (n == p) return i;
    return p ? 999 : -2;
}

std::string Snapshot(const void *set)
{
    const auto &h = *static_cast<const RawHash *>(set);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ml %08x size %u mask %u maxidx %u cap %d |", h.maxLoadBits, h.size, h.mask, h.maxIdx,
                  static_cast<int>(h.vecCap - h.vecBegin));
    std::string out = buf;
    uint32_t count = 0;
    for (RawNode *n = h.head->next; n != h.head && count < 100000; n = n->next, ++count) {
        if (n->next->prev != n) out += " BROKEN";
        out += " '" + std::string(n->value.data(), n->value.size()) + "'/" + std::to_string(n->value.capacity());
    }
    out += " | buckets";
    for (RawNode **b = h.vecBegin; b != h.vecEnd; ++b) out += " " + std::to_string(NodeIndex(h, *b));
    return out;
}

// Names for the blocks a set owns, for the free log.
std::map<const void *, std::string> BlockNames(const void *set, const char *prefix)
{
    std::map<const void *, std::string> names;
    const auto &h = *static_cast<const RawHash *>(set);
    names[h.head] = std::string(prefix) + "head";
    if (h.vecBegin) names[h.vecBegin] = std::string(prefix) + "vec";
    int i = 0;
    for (RawNode *n = h.head->next; n != h.head; n = n->next, ++i) {
        names[n] = std::string(prefix) + "node" + std::to_string(i);
        if (n->value.capacity() > 15) names[n->value.data()] = std::string(prefix) + "buf" + std::to_string(i);
    }
    return names;
}

std::vector<std::string> FreedNames(const adt::AllocatorFake &alloc, const std::map<const void *, std::string> &names)
{
    std::vector<std::string> out;
    for (void *p : alloc.mFreed) {
        auto it = names.find(p);
        out.push_back(it == names.end() ? "other" : it->second);
    }
    return out;
}

// Texts over a small alphabet (frequent duplicates), lengths around the 15-character boundary, some with a NUL
// inside (the hash stops there, equality does not).
std::string RandomText(std::mt19937 &rng, int alphabet)
{
    static const char letters[] = "abcdefghij";
    int len = static_cast<int>(rng() % 5);
    if (rng() % 4 == 0) len = 14 + static_cast<int>(rng() % 4);
    if (rng() % 16 == 0) len = 20 + static_cast<int>(rng() % 30);
    std::string s;
    for (int i = 0; i < len; ++i) s += letters[rng() % static_cast<uint32_t>(alphabet)];
    if (len > 1 && rng() % 20 == 0) s[rng() % static_cast<uint32_t>(len)] = '\0';
    return s;
}

std::vector<std::string> RandomTexts(std::mt19937 &rng, int count, int alphabet)
{
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) out.push_back(RandomText(rng, alphabet));
    return out;
}

GZString Text(const std::string &s)
{
    return GZString(s.data(), s.size());
}

// A set built by our code (the same STL as the game's) from texts in order.
void BuildSet(void *at, const std::vector<std::string> &texts)
{
    auto *set = new (at) GZStringSet();
    for (const std::string &s : texts) set->insert(Text(s));
}

struct alignas(4) SetBuffer {
    uint8_t bytes[sizeof(GZStringSet)];
};

struct alignas(4) AnyBuffer {
    uint8_t bytes[sizeof(AnyStringSet)];
};

void FillRandom(void *p, size_t n, std::mt19937 &rng)
{
    for (size_t i = 0; i < n; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(rng());
}

// An AnyStringSet built by our code: the game's vtable, then the set.
AnyStringSet *BuildAny(difftest::Context &ctx, void *at, const std::vector<std::string> &texts)
{
    auto *any = static_cast<AnyStringSet *>(at);
    any->mVTable = ctx.image.At<void>(gzany::kStringSetVTable);
    BuildSet(&any->mSet, texts);
    return any;
}

} // namespace

// Construct: an empty set with 8 buckets (16 vector entries pointing at the head) and max load factor 1.
DIFF_TEST(GZAnyStringSet_Construct, 0x551af0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x551af0);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(30);
    for (int i = 0; i < 50; ++i) {
        std::string snap[2];
        std::vector<std::string> log[2];
        uint32_t seed = rng();
        for (int side = 0; side < 2; ++side) {
            SetBuffer buf;
            std::mt19937 fill(seed);
            FillRandom(&buf, sizeof(buf), fill);
            adt::ResetSide();
            void *ret = side == 0 ? orig(&buf) : reinterpret_cast<GZStringSetOps *>(&buf)->Construct();
            if (ret != &buf) ctx.Fail("Construct returned another object");
            snap[side] = Snapshot(&buf);
            log[side] = adt::gLog;
            alloc.Release();
        }
        ++ctx.cases;
        if (snap[0] != snap[1] || log[0] != log[1]) ctx.Fail("Construct: %s / %s", snap[0].c_str(), snap[1].c_str());
    }
}

// Insert, both copies (0x5573a0 from the want tree manager's unit, 0x236a4b from the set's own), on sets grown one
// string at a time from empty through several rehashes: each result (the node, inserted or not), the allocations,
// and the whole set at the end.
DIFF_TEST(GZAnyStringSet_Insert, 0x5573a0)
{
    using InsertFn = void *(__thiscall *)(void *, void *, const GZString *);
    InsertFn origs[2] = {ctx.Original<InsertFn>(0x5573a0), ctx.Original<InsertFn>(0x236a4b)};
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(31);
    for (int round = 0; round < 60; ++round) {
        int which = round % 2;
        int count = round < 10 ? 20 : static_cast<int>(rng() % 300);
        std::vector<std::string> texts = RandomTexts(rng, count, 2 + static_cast<int>(rng() % 9));
        std::string snap[2];
        std::vector<std::string> results[2], log[2];
        for (int side = 0; side < 2; ++side) {
            SetBuffer buf;
            new (&buf) GZStringSet();
            std::vector<GZString> keys;
            for (const std::string &s : texts) keys.push_back(Text(s));
            adt::ResetSide();
            for (const GZString &key : keys) {
                uint32_t result[2] = {0xcccccccc, 0xcccccccc};
                if (side == 0) {
                    void *ret = origs[which](&buf, result, &key);
                    if (ret != result) ctx.Fail("Insert returned another buffer");
                } else {
                    auto *ops = reinterpret_cast<GZStringSetOps *>(&buf);
                    auto pair = which == 0 ? ops->Insert(key) : ops->InsertSize(key);
                    std::memcpy(&result[0], &pair.first, 4);
                    result[1] = pair.second ? 1u : 0u;
                }
                auto &h = *reinterpret_cast<const RawHash *>(&buf);
                results[side].push_back(std::to_string(NodeIndex(h, reinterpret_cast<RawNode *>(result[0]))) + "/" +
                                        std::to_string(result[1] & 0xff));
            }
            log[side] = adt::gLog;
            snap[side] = Snapshot(&buf);
            keys.clear(); // their buffers go back to the fake before it releases everything
            alloc.Release();
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("Insert (%s) round %d: results differ", which ? "0x236a4b" : "0x5573a0", round);
        if (snap[0] != snap[1]) ctx.Fail("Insert round %d: set %s / %s", round, snap[0].c_str(), snap[1].c_str());
        if (log[0] != log[1]) ctx.Fail("Insert round %d: allocations differ (%zu / %zu)", round, log[0].size(), log[1].size());
    }
}

// Find: the node or the head (end), nothing allocated.
DIFF_TEST(GZAnyStringSet_Find, 0x236b0b)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, const GZString *)>(0x236b0b);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(32);
    for (int round = 0; round < 40; ++round) {
        int alphabet = 2 + static_cast<int>(rng() % 9);
        SetBuffer buf;
        BuildSet(&buf, RandomTexts(rng, static_cast<int>(rng() % 200), alphabet));
        auto &h = *reinterpret_cast<const RawHash *>(&buf);
        for (int i = 0; i < 200; ++i) {
            GZString key = Text(RandomText(rng, alphabet));
            adt::ResetSide();
            uint32_t want = 0xcccccccc;
            if (orig(&buf, &want, &key) != &want) ctx.Fail("Find returned another buffer");
            GZStringSet::iterator got = reinterpret_cast<GZStringSetOps *>(&buf)->Find(key);
            uint32_t gotPtr;
            std::memcpy(&gotPtr, &got, 4);
            ++ctx.cases;
            if (NodeIndex(h, reinterpret_cast<RawNode *>(want)) != NodeIndex(h, reinterpret_cast<RawNode *>(gotPtr)) ||
                !adt::gLog.empty())
                ctx.Fail("Find '%s': %d / %d", key.c_str(), NodeIndex(h, reinterpret_cast<RawNode *>(want)),
                         NodeIndex(h, reinterpret_cast<RawNode *>(gotPtr)));
        }
        reinterpret_cast<GZStringSet *>(&buf)->~GZStringSet();
        alloc.Release();
    }
}

// Destruct: everything the set owns freed, in the same order.
DIFF_TEST(GZAnyStringSet_Destruct, 0x551d90)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x551d90);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(33);
    for (int round = 0; round < 40; ++round) {
        std::vector<std::string> texts = RandomTexts(rng, round == 0 ? 0 : static_cast<int>(rng() % 100), 2 + static_cast<int>(rng() % 9));
        std::vector<std::string> freed[2], log[2];
        for (int side = 0; side < 2; ++side) {
            SetBuffer buf;
            BuildSet(&buf, texts);
            auto names = BlockNames(&buf, "");
            adt::ResetSide();
            alloc.mFreed.clear();
            if (side == 0) orig(&buf);
            else reinterpret_cast<GZStringSetOps *>(&buf)->Destruct();
            freed[side] = FreedNames(alloc, names);
            log[side] = adt::gLog;
            alloc.Release();
        }
        ++ctx.cases;
        if (freed[0] != freed[1] || log[0] != log[1] || freed[0].empty() || freed[0].back() != "head")
            ctx.Fail("Destruct round %d: %zu / %zu frees", round, freed[0].size(), freed[1].size());
    }
}

// ---- AnyStringSet ----

// Copy construct (and Clone, which allocates 0x24 bytes and copy-constructs): the source's strings inserted in its
// list order into a fresh set.
DIFF_TEST(GZAnyStringSet_AnyCopyAndClone, 0x236c35)
{
    auto copy = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x236c35);
    auto clone = ctx.Original<void *(__thiscall *)(const void *)>(0x2375ea);
    const void *vt = ctx.image.At<void>(gzany::kStringSetVTable);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(35);
    for (int round = 0; round < 60; ++round) {
        std::vector<std::string> texts = RandomTexts(rng, static_cast<int>(rng() % 150), 2 + static_cast<int>(rng() % 9));
        bool viaClone = round % 2;
        std::string snap[2], snapSource[2];
        std::vector<std::string> log[2];
        for (int side = 0; side < 2; ++side) {
            AnyBuffer source, target;
            FillRandom(&target, sizeof(target), rng);
            BuildAny(ctx, &source, texts);
            adt::ResetSide();
            AnyStringSet *made;
            if (viaClone) {
                made = static_cast<AnyStringSet *>(side == 0 ? clone(&source) : static_cast<void *>(reinterpret_cast<AnyStringSet *>(&source)->Clone()));
            } else {
                made = static_cast<AnyStringSet *>(side == 0 ? copy(&target, &source)
                                                             : static_cast<void *>(reinterpret_cast<AnyStringSet *>(&target)->Construct(
                                                                   *reinterpret_cast<const AnyStringSet *>(&source))));
                if (made != reinterpret_cast<AnyStringSet *>(&target)) ctx.Fail("copy returned another object");
            }
            if (made->mVTable != vt) ctx.Fail("copy side %d: vtable %p", side, made->mVTable);
            log[side] = adt::gLog;
            snap[side] = Snapshot(&made->mSet);
            snapSource[side] = Snapshot(&reinterpret_cast<AnyStringSet *>(&source)->mSet);
            if (viaClone) ::operator delete(made);
            alloc.Release();
        }
        ++ctx.cases;
        if (snap[0] != snap[1] || snapSource[0] != snapSource[1] || log[0] != log[1])
            ctx.Fail("AnyStringSet %s round %d: %s / %s", viaClone ? "Clone" : "copy", round, snap[0].c_str(), snap[1].c_str());
    }
}

// Destruct and the deleting destructor: the set freed in order, the vtable reset to cIGZAny's, the object itself
// freed (operator delete, 0x24 bytes) on flags & 1.
DIFF_TEST(GZAnyStringSet_AnyDestruct, 0x236f02)
{
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(0x236f02);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x237034);
    const void *anyVT = ctx.image.At<void>(gzany::kAnyVTable);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(36);
    for (int round = 0; round < 60; ++round) {
        std::vector<std::string> texts = RandomTexts(rng, static_cast<int>(rng() % 60), 2 + static_cast<int>(rng() % 9));
        bool viaDeleting = round % 2;
        uint32_t flags = rng();
        std::vector<std::string> freed[2], log[2];
        bool heapFreed[2] = {};
        for (int side = 0; side < 2; ++side) {
            void *obj = ::operator new(sizeof(AnyStringSet));
            BuildAny(ctx, obj, texts);
            auto names = BlockNames(&static_cast<AnyStringSet *>(obj)->mSet, "");
            adt::ResetSide();
            alloc.mFreed.clear();
            difftest::FreeRecorder rec;
            if (!viaDeleting) {
                if (side == 0) destruct(obj);
                else static_cast<AnyStringSet *>(obj)->Destruct();
            } else {
                void *ret = side == 0 ? deleting(obj, flags) : static_cast<AnyStringSet *>(obj)->DeletingDestructor(flags);
                if (ret != obj) ctx.Fail("deleting destructor returned another object");
            }
            rec.Stop();
            heapFreed[side] = rec.Freed(obj);
            freed[side] = FreedNames(alloc, names);
            log[side] = adt::gLog;
            if (!heapFreed[side]) {
                if (static_cast<AnyStringSet *>(obj)->mVTable != anyVT) ctx.Fail("destructor side %d left another vtable", side);
                ::operator delete(obj);
            }
            alloc.Release();
        }
        ++ctx.cases;
        bool shouldFree = viaDeleting && (flags & 1);
        if (freed[0] != freed[1] || log[0] != log[1] || heapFreed[0] != shouldFree || heapFreed[1] != shouldFree)
            ctx.Fail("AnyStringSet destruct round %d (deleting %d, flags %08x): %zu / %zu frees", round, viaDeleting, flags,
                     freed[0].size(), freed[1].size());
    }
}

// Contains and Compare (1 when each set holds every string of the other, 0 otherwise or for a null other).
DIFF_TEST(GZAnyStringSet_AnyContainsCompare, 0x237621)
{
    auto contains = ctx.Original<uint8_t(__thiscall *)(const void *, const GZString *)>(0x237c75);
    auto compare = ctx.Original<int32_t(__thiscall *)(const void *, const void *)>(0x237621);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(37);
    for (int round = 0; round < 300; ++round) {
        int alphabet = 2 + static_cast<int>(rng() % 3);
        std::vector<std::string> a = RandomTexts(rng, static_cast<int>(rng() % 12), alphabet);
        std::vector<std::string> b;
        switch (rng() % 4) {
        case 0: b = a; break;                                                     // the same strings
        case 1: b = a; std::shuffle(b.begin(), b.end(), rng); break;              // another order
        case 2: b = a; b.push_back(RandomText(rng, alphabet)); break;             // maybe one more
        default: b = RandomTexts(rng, static_cast<int>(rng() % 12), alphabet); break;
        }
        AnyBuffer bufA, bufB;
        auto *setA = BuildAny(ctx, &bufA, a);
        auto *setB = BuildAny(ctx, &bufB, b);
        adt::ResetSide();
        const void *other = rng() % 20 == 0 ? nullptr : static_cast<const void *>(setB);
        int32_t want = compare(setA, other), got = setA->Compare(other);
        ++ctx.cases;
        if (want != got) ctx.Fail("Compare round %d: %d / %d", round, want, got);
        if (!adt::gLog.empty()) ctx.Fail("Compare allocated");
        for (int i = 0; i < 5; ++i) {
            GZString key = Text(RandomText(rng, alphabet));
            adt::ResetSide();
            uint8_t w = contains(setA, &key);
            bool g = setA->Contains(key);
            ++ctx.cases;
            if ((w != 0) != g || w > 1) ctx.Fail("Contains '%s': %u / %d", key.c_str(), w, g);
            if (!adt::gLog.empty()) ctx.Fail("Contains allocated");
        }
        setA->mSet.~GZStringSet();
        setB->mSet.~GZStringSet();
        alloc.Release();
    }
}

// Assign: only from the same type (GetType of the source, then of this, through their vtables: the game's
// AnyStringSet / AnyUint32 vtables here), then the set assignment.
DIFF_TEST(GZAnyStringSet_AnyAssign, 0x237679)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const void *)>(0x237679);
    adt::AllocatorFake alloc(ctx);
    std::mt19937 rng(38);
    for (int round = 0; round < 60; ++round) {
        std::vector<std::string> a = RandomTexts(rng, static_cast<int>(rng() % 50), 4);
        std::vector<std::string> b = RandomTexts(rng, static_cast<int>(rng() % 50), 4);
        int kind = static_cast<int>(rng() % 3); // 0 another set, 1 itself, 2 an AnyUint32
        uint8_t result[2];
        std::string snap[2];
        std::vector<std::string> log[2], freed[2];
        for (int side = 0; side < 2; ++side) {
            AnyBuffer bufA, bufB;
            auto *target = BuildAny(ctx, &bufA, a);
            auto *source = BuildAny(ctx, &bufB, b);
            uint32_t uint32Any[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kUint32VTable))), 5};
            const void *from = kind == 0 ? static_cast<const void *>(source) : kind == 1 ? static_cast<const void *>(target) : uint32Any;
            auto names = BlockNames(&target->mSet, "t.");
            adt::ResetSide();
            alloc.mFreed.clear();
            result[side] = side == 0 ? orig(target, from) : target->Assign(from);
            log[side] = adt::gLog;
            freed[side] = FreedNames(alloc, names);
            snap[side] = Snapshot(&target->mSet);
            alloc.Release();
        }
        ++ctx.cases;
        if (result[0] != result[1] || snap[0] != snap[1] || log[0] != log[1] || freed[0] != freed[1])
            ctx.Fail("AnyStringSet Assign round %d (kind %d): %u / %u, %s / %s", round, kind, result[0], result[1], snap[0].c_str(),
                     snap[1].c_str());
    }
}
