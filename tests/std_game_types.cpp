// Differential tests for the standard-library members instantiated with game element types (src/std/StdGameTypes*.cpp,
// milestone H package H6). The containers are built with our STL (the game's layout); the original and ours then run on
// two copies built from the same seed, and the results are compared through raw layouts (node links as positions,
// values, sizes, colours), never through our own types' accessors. Allocations by either side come from the process
// heap (the game's operator new and ours both use it); frees are observed with difftest::FreeRecorder.
// Library matches (db/library-matches.jsonl: the set insert 0x1c01b9 / 0x5cb103, the class map size check 0x1aafcb,
// map/set constructors, temporary-node and emplace-guard constructors, find_last_of, the hash erase) are our toolchain's own
// code and have no entry here.
#include "difftest.h"
#include "free_recorder.h"
#include "std/StdGameTypes.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace StdGameTypes;

namespace {

constexpr uint32_t kFwRefOpCtor = 0x141503, kFwRefOpDtor = 0x141f0c;
constexpr uint32_t kRegionErase = 0x1be496, kHeapDeallocate = 0x1be435;
constexpr uint32_t kAppendChar = 0x15aa90, kFindText = 0x1adba0, kFindLastOfText = 0x1af5d6;
constexpr uint32_t kRegionEmplace = 0x1bdc30, kRegionTemp = 0x1bdbc9, kHeapAllocate = 0x1be421;
constexpr uint32_t kFreeServiceNode = 0x1419c3, kFreeRefListNode = 0x1419de, kFreePriorityNode = 0x1419f9;
constexpr uint32_t kDeleteServicePair = 0x1421bb, kDeletePriorityPair = 0x1421e1;
constexpr uint32_t kByteResize = 0x1af86f;

// ---- counted fakes: slot 1 AddRef, slot 2 Release, recorded as +id / -id -------------------------------------------
struct FakeRef {
    void **vtable;
    int id;
};
std::vector<int> gEvents;
uint32_t __fastcall FakeQuery(FakeRef *, void *, uint32_t, void **) { return 0; }
uint32_t __fastcall FakeAddRef(FakeRef *self, void *)
{
    gEvents.push_back(self->id);
    return 1;
}
uint32_t __fastcall FakeRelease(FakeRef *self, void *)
{
    gEvents.push_back(-self->id);
    return 1;
}
void *gFakeVTable[3] = {reinterpret_cast<void *>(&FakeQuery), reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};
FakeRef gFakes[16];
void InitFakes()
{
    for (int i = 0; i < 16; ++i) gFakes[i] = {gFakeVTable, i + 1};
}
int IdOf(const void *object) { return object ? static_cast<const FakeRef *>(object)->id : 0; }

std::string Events()
{
    std::string s;
    for (int e : gEvents) s += std::to_string(e) + " ";
    return s;
}

// ---- raw tree dump: in order, each node's value words, colour and links as in-order positions (head = -1) ---------------
struct RawTreeNode {
    RawTreeNode *left, *parent, *right;
    uint8_t color, isNil;
};
template <size_t ValueWords> std::string DumpTree(const void *head, uint32_t size)
{
    std::vector<const RawTreeNode *> order;
    auto *h = static_cast<const RawTreeNode *>(head);
    // In-order walk through the links themselves (the root is head->parent).
    std::vector<const RawTreeNode *> stack;
    const RawTreeNode *at = h->parent;
    while ((at != h && !at->isNil) || !stack.empty()) {
        while (at != h && !at->isNil) {
            stack.push_back(at);
            at = at->left;
        }
        at = stack.back();
        stack.pop_back();
        order.push_back(at);
        at = at->right;
        if (order.size() > 100000) return "cycle";
    }
    auto pos = [&](const RawTreeNode *n) -> int {
        if (n == h) return -1;
        auto it = std::find(order.begin(), order.end(), n);
        return it == order.end() ? -2 : static_cast<int>(it - order.begin());
    };
    std::string s = "size " + std::to_string(size) + " head(" + std::to_string(pos(h->left)) + "," + std::to_string(pos(h->parent)) +
                    "," + std::to_string(pos(h->right)) + "," + std::to_string(h->color) + "," + std::to_string(h->isNil) + ")";
    for (const RawTreeNode *n : order) {
        const uint32_t *value = reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(n) + 0x10);
        char buf[64];
        s += " [";
        for (size_t w = 0; w < ValueWords; ++w) {
            std::snprintf(buf, sizeof(buf), "%x%s", value[w], w + 1 < ValueWords ? ":" : "");
            s += buf;
        }
        std::snprintf(buf, sizeof(buf), " %d %d %d %d %d]", pos(n->left), pos(n->parent), pos(n->right), n->color, n->isNil);
        s += buf;
    }
    return s;
}

// ---- raw hash dump: the list in order (key, value words), then the bucket vector as list positions -----------------------
struct RawListNode {
    RawListNode *next, *prev;
};
template <size_t ValueWords> std::string DumpHash(const void *map)
{
    const uint32_t *w = static_cast<const uint32_t *>(map);
    auto *head = reinterpret_cast<const RawListNode *>(w[1]);
    std::vector<const RawListNode *> order;
    for (const RawListNode *n = head->next; n != head && order.size() < 100000; n = n->next) order.push_back(n);
    auto pos = [&](uint32_t p) -> int {
        auto *n = reinterpret_cast<const RawListNode *>(p);
        if (n == head) return -1;
        auto it = std::find(order.begin(), order.end(), n);
        return it == order.end() ? -2 : static_cast<int>(it - order.begin());
    };
    char buf[96];
    std::snprintf(buf, sizeof(buf), "load %08x size %u mask %u maxidx %u list", w[0], w[2], w[6], w[7]);
    std::string s = buf;
    for (size_t i = 0; i < order.size(); ++i) {
        const RawListNode *n = order[i];
        const uint32_t *value = reinterpret_cast<const uint32_t *>(n + 1);
        s += " (";
        for (size_t k = 0; k < ValueWords; ++k) {
            std::snprintf(buf, sizeof(buf), "%x%s", value[k], k + 1 < ValueWords ? ":" : "");
            s += buf;
        }
        // The back link as a position.
        std::snprintf(buf, sizeof(buf), " prev %d)", pos(reinterpret_cast<uint32_t>(n->prev)));
        s += buf;
    }
    s += " buckets";
    for (uint32_t *b = reinterpret_cast<uint32_t *>(w[3]); b != reinterpret_cast<uint32_t *>(w[4]); ++b) s += " " + std::to_string(pos(*b));
    std::snprintf(buf, sizeof(buf), " capacity %u", (w[5] - w[3]) / 4);
    return s + buf;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// std::list<FwRef>

DIFF_TEST(StdGameTypes_FwRefEmplaceOp, kFwRefOpCtor)
{
    using Op = FwRefList::EmplaceOp;
    auto originalCtor = ctx.Original<void *(__fastcall *)(void *, void *, void *, const FwRef *)>(kFwRefOpCtor);
    auto originalDtor = ctx.Original<void(__fastcall *)(void *)>(kFwRefOpDtor);
    InitFakes();
    std::mt19937 rng(kFwRefOpCtor);
    for (int i = 0; i < 2000; ++i) {
        const int valueId = static_cast<int>(rng() % 17);
        FwRef value(valueId ? &gFakes[valueId - 1] : nullptr);
        // The allocator is an empty class: the guard keeps its address only. Any address will do.
        auto *alloc = reinterpret_cast<FwRefList::NodeAlloc *>(static_cast<uintptr_t>(rng() | 1));
        // Constructor: the stored allocator, a node holding the value (AddRef), the returned guard.
        uint32_t ops[2][3];
        std::string events[2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            ops[side][0] = ops[side][1] = ops[side][2] = 0xcdcdcdcd;
            gEvents.clear();
            returned[side] = side == 0 ? originalCtor(ops[0], nullptr, alloc, &value)
                                       : ConstructFwRefEmplaceOp(reinterpret_cast<Op *>(ops[1]), nullptr, *alloc, value);
            events[side] = Events();
        }
        auto *nodes0 = reinterpret_cast<uint32_t *>(ops[0][1]), *nodes1 = reinterpret_cast<uint32_t *>(ops[1][1]);
        ++ctx.cases;
        // The node's links (+0, +4) are left as the heap gave them on both sides: not compared.
        if (returned[0] != ops[0] || returned[1] != ops[1] || ops[0][0] != ops[1][0] || ops[0][2] != ops[1][2] || !nodes0 || !nodes1 ||
            nodes0[2] != nodes1[2] || events[0] != events[1] || HeapSize(GetProcessHeap(), 0, nodes0) != HeapSize(GetProcessHeap(), 0, nodes1))
            ctx.Fail("guard ctor %d: alloc %08x/%08x node value %08x/%08x events %s/ %s", valueId, ops[0][0], ops[1][0],
                nodes0 ? nodes0[2] : 0, nodes1 ? nodes1[2] : 0, events[0].c_str(), events[1].c_str());
        // Destructor: a held node has its value released and is freed (12 bytes); a released guard (null node) does nothing.
        const bool released = i % 4 == 3;
        size_t freedSizes[2] = {};
        size_t freedCounts[2] = {};
        bool freedNode[2] = {};
        for (int side = 0; side < 2; ++side) {
            uint32_t *node = reinterpret_cast<uint32_t *>(ops[side][1]);
            if (released) ops[side][1] = 0;
            gEvents.clear();
            difftest::FreeRecorder rec;
            if (side == 0) originalDtor(ops[0]);
            else DestroyFwRefEmplaceOp(reinterpret_cast<Op *>(ops[1]));
            rec.Stop();
            events[side] = Events();
            freedCounts[side] = rec.Count();
            freedSizes[side] = rec.Count() ? rec.Size(0) : 0;
            freedNode[side] = rec.Freed(node);
            if (released) ::operator delete(node, 12);
        }
        ++ctx.cases;
        if (events[0] != events[1] || freedCounts[0] != freedCounts[1] || freedSizes[0] != freedSizes[1] || freedNode[0] != freedNode[1] ||
            freedNode[0] == released)
            ctx.Fail("guard dtor %d%s: events %s/ %s, frees %zu/%zu sizes %zu/%zu node %d/%d", valueId, released ? " (released)" : "",
                events[0].c_str(), events[1].c_str(), freedCounts[0], freedCounts[1], freedSizes[0], freedSizes[1], freedNode[0],
                freedNode[1]);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The block pools' region map

DIFF_TEST(StdGameTypes_RegionMapErase, kRegionErase)
{
    using Node = RegionMapAccess::Node;
    auto original = ctx.Original<Node *(__fastcall *)(void *, void *, Node *)>(kRegionErase);
    std::mt19937 rng(kRegionErase);
    for (int i = 0; i < 3000; ++i) {
        const size_t n = 1 + rng() % 40;
        std::vector<uint32_t> keys(n);
        for (uint32_t &k : keys) k = rng() % 100;
        const size_t pick = rng();
        std::string dumps[2];
        uint32_t successor[2] = {};
        bool freedNode[2] = {};
        size_t frees[2] = {};
        for (int side = 0; side < 2; ++side) {
            RegionMapAccess map;
            for (uint32_t k : keys) map.emplace(k, k * 7 + 1);
            auto where = map.begin();
            std::advance(where, pick % map.size());
            Node *node = where._Ptr;
            difftest::FreeRecorder rec;
            Node *next = side == 0 ? original(&map, nullptr, node) : map.EraseNode(node);
            rec.Stop();
            frees[side] = rec.Count();
            freedNode[side] = rec.Freed(node);
            successor[side] = next == map.end()._Ptr ? 0xffffffffu : next->_Myval.first;
            dumps[side] = DumpTree<2>(map.end()._Ptr, static_cast<uint32_t>(map.size()));
        }
        ++ctx.cases;
        if (dumps[0] != dumps[1] || successor[0] != successor[1] || !freedNode[0] || !freedNode[1] || frees[0] != 1 || frees[1] != 1)
            ctx.Fail("erase #%zu of %zu: successor %x/%x freed %d/%d (%zu/%zu)\n %s\n %s", pick % n, n, successor[0], successor[1],
                freedNode[0], freedNode[1], frees[0], frees[1], dumps[0].c_str(), dumps[1].c_str());
    }
}

DIFF_TEST(StdGameTypes_ProcessHeapDeallocate, kHeapDeallocate)
{
    auto original = ctx.Original<void(__cdecl *)(void *, size_t)>(kHeapDeallocate);
    std::mt19937 rng(kHeapDeallocate);
    for (int i = 0; i < 500; ++i) {
        const size_t bytes = 1 + rng() % 200;
        bool freed[2];
        size_t counts[2];
        for (int side = 0; side < 2; ++side) {
            void *block = HeapAlloc(GetProcessHeap(), 0, bytes);
            difftest::FreeRecorder rec;
            if (side == 0) original(block, rng());
            else ProcessHeapDeallocate(block, rng());
            rec.Stop();
            freed[side] = rec.Freed(block);
            counts[side] = rec.Count();
        }
        ++ctx.cases;
        if (!freed[0] || !freed[1] || counts[0] != 1 || counts[1] != 1) ctx.Fail("free %zu bytes: %d/%d (%zu/%zu)", bytes, freed[0], freed[1], counts[0], counts[1]);
    }
    // A null block: HeapFree accepts it, nothing is freed.
    for (int side = 0; side < 2; ++side) {
        difftest::FreeRecorder rec;
        if (side == 0) original(nullptr, 1);
        else ProcessHeapDeallocate(nullptr, 1);
        rec.Stop();
        ++ctx.cases;
        if (rec.Count() != 0) ctx.Fail("free null: %zu frees", rec.Count());
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// std::string

namespace {
const char kLetters[] = "abcxyzABC/.\\ ";
std::string RandomString(std::mt19937 &rng, size_t maxLength)
{
    std::string s;
    for (size_t n = rng() % (maxLength + 1); n; --n) s += kLetters[rng() % (sizeof(kLetters) - 1)];
    return s;
}
std::string Describe(const std::string &s)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(&s);
    return "\"" + s + "\" size " + std::to_string(w[4]) + " cap " + std::to_string(w[5]);
}
} // namespace

DIFF_TEST(StdGameTypes_AppendCharMove, kAppendChar)
{
    auto original = ctx.Original<void *(__cdecl *)(void *, std::string *, int)>(kAppendChar);
    std::mt19937 rng(kAppendChar);
    for (int i = 0; i < 4000; ++i) {
        // Lengths around the small-buffer edge (15) and the capacities a growth gives (31, 47, ...).
        std::string text = RandomString(rng, i % 4 == 0 ? 70 : 20);
        const int reserveExtra = static_cast<int>(rng() % 3);
        const char c = static_cast<char>(rng());
        std::string results[2], lefts[2];
        size_t frees[2], freedSize[2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            std::string left = text;
            if (reserveExtra == 1) left.reserve(left.size() + 1);
            if (reserveExtra == 2) left.shrink_to_fit();
            alignas(4) uint8_t raw[sizeof(std::string)];
            std::memset(raw, 0xcd, sizeof(raw));
            difftest::FreeRecorder rec;
            // Ours is called the way the hook calls it: the hidden result pointer first, the char as a stack word.
            auto ours = reinterpret_cast<void *(__cdecl *)(void *, std::string *, int)>(&AppendCharMove);
            returned[side] = (side == 0 ? original : ours)(raw, &left, c);
            rec.Stop();
            frees[side] = rec.Count();
            freedSize[side] = rec.Count() ? rec.Size(0) : 0;
            auto *result = reinterpret_cast<std::string *>(raw);
            results[side] = Describe(*result);
            lefts[side] = Describe(left);
            result->~basic_string();
            if (returned[side] != raw) results[side] += " (returned another pointer)";
        }
        ++ctx.cases;
        if (results[0] != results[1] || lefts[0] != lefts[1] || frees[0] != frees[1] || freedSize[0] != freedSize[1])
            ctx.Fail("\"%s\" + %02x: %s / %s, left %s / %s, frees %zu/%zu", text.c_str(), static_cast<uint8_t>(c), results[0].c_str(),
                results[1].c_str(), lefts[0].c_str(), lefts[1].c_str(), frees[0], frees[1]);
    }
}

DIFF_TEST(StdGameTypes_StringFind, kFindText)
{
    auto findText = ctx.Original<size_t(__fastcall *)(const std::string *, void *, const char *, size_t)>(kFindText);
    auto findLastOfText = ctx.Original<size_t(__fastcall *)(const std::string *, void *, const char *, size_t)>(kFindLastOfText);
    std::mt19937 rng(kFindText);
    for (int i = 0; i < 20000; ++i) {
        const StringAccess hay{RandomString(rng, i % 3 == 0 ? 40 : 12)};
        const std::string needle = RandomString(rng, i % 2 ? 3 : 1);
        const size_t offset = i % 9 == 0 ? static_cast<size_t>(-1) : i % 9 == 1 ? rng() : rng() % (hay.size() + 3);
        ++ctx.cases;
        size_t ra = findText(&hay, nullptr, needle.c_str(), offset), rb = hay.FindText(needle.c_str(), offset);
        if (ra != rb) ctx.Fail("\"%s\".find(\"%s\", %zu): %zu vs %zu", hay.c_str(), needle.c_str(), offset, ra, rb);
        ra = findLastOfText(&hay, nullptr, needle.c_str(), offset);
        rb = hay.FindLastOfText(needle.c_str(), offset);
        if (ra != rb) ctx.Fail("\"%s\".find_last_of(\"%s\", %zu): %zu vs %zu", hay.c_str(), needle.c_str(), offset, ra, rb);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The region map's insert, its temporary node, the process-heap allocation

DIFF_TEST(StdGameTypes_RegionMapEmplace, kRegionEmplace)
{
    using Node = RegionMapAccess::Node;
    auto original = ctx.Original<void *(__fastcall *)(void *, void *, void *, const RegionMapAccess::value_type *)>(kRegionEmplace);
    std::mt19937 rng(kRegionEmplace);
    for (int i = 0; i < 3000; ++i) {
        const size_t n = rng() % 40;
        std::vector<uint32_t> keys(n);
        const uint32_t range = i % 3 == 0 ? 0xffffffffu : 64;
        for (uint32_t &k : keys) k = rng() % range;
        const uint32_t key = i % 7 == 0 && n ? keys[rng() % n] : rng() % range;
        const RegionMapAccess::value_type value{key, static_cast<uint32_t>(rng())};
        const bool full = i % 40 == 39;
        std::string dumps[2];
        bool threw[2] = {}, inserted[2] = {};
        uint32_t found[2][2] = {};
        for (int side = 0; side < 2; ++side) {
            RegionMapAccess map;
            for (uint32_t k : keys) map.emplace(k, k * 5 + 3);
            uint32_t *size = reinterpret_cast<uint32_t *>(&map) + 1;
            const uint32_t realSize = *size;
            if (full) *size = 0xaaaaaaa; // max_size(): a new key throws, an existing one is found first
            uint32_t result[2] = {0xcdcdcdcd, 0xcdcdcdcd};
            try {
                if (side == 0) {
                    original(&map, nullptr, result, &value);
                } else {
                    const auto r = map.Emplace(value);
                    result[0] = reinterpret_cast<uint32_t>(r.first);
                    result[1] = (result[1] & 0xffffff00u) | static_cast<uint8_t>(r.second);
                }
            } catch (...) {
                threw[side] = true;
            }
            if (full) *size = realSize;
            // The bool's byte only (the other three bytes of the pair's second word are padding).
            inserted[side] = (result[1] & 0xff) != 0;
            if (!threw[side]) {
                found[side][0] = reinterpret_cast<Node *>(result[0])->_Myval.first;
                found[side][1] = reinterpret_cast<Node *>(result[0])->_Myval.second;
            }
            dumps[side] = DumpTree<2>(map.end()._Ptr, *size);
        }
        ++ctx.cases;
        if (dumps[0] != dumps[1] || threw[0] != threw[1] || inserted[0] != inserted[1] || found[0][0] != found[1][0] ||
            found[0][1] != found[1][1] || (!threw[0] && found[0][0] != key))
            ctx.Fail("insert %x into %zu%s: threw %d/%d inserted %d/%d found %x:%x / %x:%x\n %s\n %s", key, n, full ? " (full)" : "",
                threw[0], threw[1], inserted[0], inserted[1], found[0][0], found[0][1], found[1][0], found[1][1], dumps[0].c_str(),
                dumps[1].c_str());
    }
}

DIFF_TEST(StdGameTypes_RegionMapTempNode, kRegionTemp)
{
    auto original = ctx.Original<void *(__fastcall *)(void *, void *, void *, void *, const void *)>(kRegionTemp);
    std::mt19937 rng(kRegionTemp);
    for (int i = 0; i < 1000; ++i) {
        auto *alloc = reinterpret_cast<RegionMapAccess::NodeAlloc *>(static_cast<uintptr_t>(rng() | 1));
        auto *head = reinterpret_cast<RegionMapAccess::Node *>(static_cast<uintptr_t>(rng()));
        const uint32_t value[2] = {rng(), rng()};
        uint32_t guards[2][2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            guards[side][0] = guards[side][1] = 0xcdcdcdcd;
            returned[side] = side == 0 ? original(guards[0], nullptr, alloc, head, value)
                                       : ConstructRegionMapTempNode(reinterpret_cast<RegionMapAccess::TempNode *>(guards[1]), nullptr, *alloc,
                                             head, *reinterpret_cast<const RegionMapAccess::value_type *>(value));
        }
        auto *a = reinterpret_cast<uint8_t *>(guards[0][1]), *b = reinterpret_cast<uint8_t *>(guards[1][1]);
        ++ctx.cases;
        // Node bytes +0xe..+0xf are padding the constructor never writes (heap contents): not compared. The node comes from
        // the process heap on both sides.
        if (returned[0] != guards[0] || returned[1] != guards[1] || guards[0][0] != guards[1][0] || !a || !b ||
            std::memcmp(a, b, 0xe) != 0 || std::memcmp(a + 0x10, b + 0x10, 8) != 0 || std::memcmp(a + 0x10, value, 8) != 0 ||
            HeapSize(GetProcessHeap(), 0, a) != 0x18 || HeapSize(GetProcessHeap(), 0, b) != 0x18)
            ctx.Fail("temp node %x:%x head %p: guards %08x / %08x", value[0], value[1], static_cast<void *>(head), guards[0][0], guards[1][0]);
        if (a) HeapFree(GetProcessHeap(), 0, a);
        if (b) HeapFree(GetProcessHeap(), 0, b);
    }
}

DIFF_TEST(StdGameTypes_ProcessHeapAllocate, kHeapAllocate)
{
    auto original = ctx.Original<void *(__cdecl *)(size_t)>(kHeapAllocate);
    std::mt19937 rng(kHeapAllocate);
    for (int i = 0; i < 500; ++i) {
        const size_t bytes = i % 50 == 0 ? 0 : 1 + rng() % (i % 10 == 0 ? 100000 : 300);
        void *a = original(bytes), *b = ProcessHeapAllocate(bytes);
        ++ctx.cases;
        if (!a || !b || HeapSize(GetProcessHeap(), 0, a) != bytes || HeapSize(GetProcessHeap(), 0, b) != bytes)
            ctx.Fail("allocate %zu: %p %p", bytes, a, b);
        if (a) HeapFree(GetProcessHeap(), 0, a);
        if (b) HeapFree(GetProcessHeap(), 0, b);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// cGZFramework's service containers: node frees, element destructors

namespace {
// A node free: a node of `bytes` from the heap, the reference (a fake, or null) at `refOffset`, the other words random.
void NodeFreeTest(difftest::Context &ctx, uint32_t rva, size_t bytes, size_t refOffset,
                  void(__cdecl *ours)(void *alloc, void *node))
{
    auto original = ctx.Original<void(__cdecl *)(void *, void *)>(rva);
    InitFakes();
    std::mt19937 rng(rva);
    for (int i = 0; i < 1000; ++i) {
        const int id = static_cast<int>(rng() % 17);
        auto *alloc = reinterpret_cast<void *>(static_cast<uintptr_t>(rng() | 1));
        std::string events[2];
        size_t frees[2] = {}, sizes[2] = {};
        bool freedNode[2] = {};
        for (int side = 0; side < 2; ++side) {
            auto *node = static_cast<uint32_t *>(::operator new(bytes));
            for (size_t w = 0; w < bytes / 4; ++w) node[w] = rng();
            *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(node) + refOffset) = id ? &gFakes[id - 1] : nullptr;
            gEvents.clear();
            difftest::FreeRecorder rec;
            if (side == 0) original(alloc, node);
            else ours(alloc, node);
            rec.Stop();
            events[side] = Events();
            frees[side] = rec.Count();
            sizes[side] = rec.Count() ? rec.Size(0) : 0;
            freedNode[side] = rec.Freed(node);
        }
        ++ctx.cases;
        const std::string expected = id ? std::to_string(-id) + " " : "";
        if (events[0] != events[1] || events[0] != expected || frees[0] != 1 || frees[1] != 1 || sizes[0] != sizes[1] || !freedNode[0] ||
            !freedNode[1])
            ctx.Fail("free node (ref %d): events %s/ %s frees %zu/%zu sizes %zu/%zu node %d/%d", id, events[0].c_str(), events[1].c_str(),
                frees[0], frees[1], sizes[0], sizes[1], freedNode[0], freedNode[1]);
    }
}

void __cdecl OurFreeServiceNode(void *alloc, void *node)
{
    FreeFwServiceMapNode(*static_cast<std::allocator<FwServiceMapAccess::Node> *>(alloc), static_cast<FwServiceMapAccess::Node *>(node));
}
void __cdecl OurFreeRefListNode(void *alloc, void *node)
{
    FreeFwRefListNode(*static_cast<FwRefList::NodeAlloc *>(alloc), static_cast<FwRefList::Node *>(node));
}
void __cdecl OurFreePriorityNode(void *alloc, void *node)
{
    FreeFwPriorityNode(*static_cast<std::allocator<FwPriorityNode> *>(alloc), static_cast<FwPriorityNode *>(node));
}

// An element's deleting destructor: the reference at refOffset released, the element freed (bytes) when flags & 1.
void PairDeleteTest(difftest::Context &ctx, uint32_t rva, size_t bytes, size_t refOffset,
                    void *(__fastcall *ours)(void *self, void *edx, uint32_t flags))
{
    auto original = ctx.Original<void *(__fastcall *)(void *, void *, uint32_t)>(rva);
    InitFakes();
    std::mt19937 rng(rva);
    for (int i = 0; i < 1000; ++i) {
        const int id = static_cast<int>(rng() % 17);
        const uint32_t flags = i % 4 == 3 ? rng() : rng() % 4;
        std::string events[2];
        size_t frees[2] = {}, sizes[2] = {};
        bool freed[2] = {}, returned[2] = {};
        for (int side = 0; side < 2; ++side) {
            auto *object = static_cast<uint32_t *>(::operator new(bytes));
            for (size_t w = 0; w < bytes / 4; ++w) object[w] = rng();
            *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(object) + refOffset) = id ? &gFakes[id - 1] : nullptr;
            gEvents.clear();
            difftest::FreeRecorder rec;
            void *r = side == 0 ? original(object, nullptr, flags) : ours(object, nullptr, flags);
            rec.Stop();
            events[side] = Events();
            frees[side] = rec.Count();
            sizes[side] = rec.Count() ? rec.Size(0) : 0;
            freed[side] = rec.Freed(object);
            returned[side] = r == object;
            if (!freed[side]) ::operator delete(object, bytes);
        }
        ++ctx.cases;
        const std::string expected = id ? std::to_string(-id) + " " : "";
        if (events[0] != events[1] || events[0] != expected || frees[0] != frees[1] || sizes[0] != sizes[1] || freed[0] != freed[1] ||
            freed[0] != ((flags & 1) != 0) || !returned[0] || !returned[1])
            ctx.Fail("delete element (ref %d, flags %x): events %s/ %s frees %zu/%zu sizes %zu/%zu freed %d/%d returned %d/%d", id, flags,
                events[0].c_str(), events[1].c_str(), frees[0], frees[1], sizes[0], sizes[1], freed[0], freed[1], returned[0], returned[1]);
    }
}

void *__fastcall OurDeleteServicePair(void *self, void *, uint32_t flags)
{
    return DeleteFwServicePair(static_cast<FwServicePair *>(self), nullptr, flags);
}
void *__fastcall OurDeletePriorityPair(void *self, void *, uint32_t flags)
{
    return DeleteFwPriorityPair(static_cast<FwPriorityPair *>(self), nullptr, flags);
}
} // namespace

DIFF_TEST(StdGameTypes_FreeFwServiceMapNode, kFreeServiceNode) { NodeFreeTest(ctx, kFreeServiceNode, 0x10, 0xc, &OurFreeServiceNode); }
DIFF_TEST(StdGameTypes_FreeFwRefListNode, kFreeRefListNode) { NodeFreeTest(ctx, kFreeRefListNode, 0xc, 0x8, &OurFreeRefListNode); }
DIFF_TEST(StdGameTypes_FreeFwPriorityNode, kFreePriorityNode) { NodeFreeTest(ctx, kFreePriorityNode, 0x1c, 0x18, &OurFreePriorityNode); }
DIFF_TEST(StdGameTypes_DeleteFwServicePair, kDeleteServicePair) { PairDeleteTest(ctx, kDeleteServicePair, 8, 4, &OurDeleteServicePair); }
DIFF_TEST(StdGameTypes_DeleteFwPriorityPair, kDeletePriorityPair) { PairDeleteTest(ctx, kDeletePriorityPair, 12, 8, &OurDeletePriorityPair); }

// ---------------------------------------------------------------------------------------------------------------------
// std::vector<char>

DIFF_TEST(StdGameTypes_ByteVectorResize, kByteResize)
{
    auto original = ctx.Original<void(__fastcall *)(void *, void *, size_t, const void *)>(kByteResize);
    std::mt19937 rng(kByteResize);
    const uint8_t tag = 0;
    for (int i = 0; i < 3000; ++i) {
        const bool empty = i % 17 == 0;
        const bool big = i % 10 == 0; // past 4 KiB: the library's aligned big-block allocation on both sides
        const size_t oldSize = empty ? 0 : rng() % (big ? 6000 : 40);
        const size_t capacity = empty ? 0 : oldSize + rng() % 20 + (oldSize == 0 ? 1 : 0);
        const bool tooLong = i % 50 == 49;
        size_t newSize;
        if (tooLong) newSize = 0x80000000u + rng() % 0x1000;
        else if (i % 13 == 0) newSize = oldSize + rng() % (capacity - oldSize + 1); // within the capacity (still reallocates)
        else newSize = capacity + 1 + rng() % (big ? 9000 : 60);
        const uint32_t fill = rng();
        std::string dumps[2];
        bool threw[2] = {};
        size_t frees[2] = {};
        bool freedOld[2] = {};
        for (int side = 0; side < 2; ++side) {
            std::vector<char> v;
            if (!empty) {
                v.reserve(capacity);
                v.resize(oldSize);
                for (size_t k = 0; k < oldSize; ++k) v[k] = static_cast<char>((fill >> (k % 24)) + k);
            }
            // Garbage past the old size: the resize must zero the new elements.
            for (size_t k = oldSize; k < capacity; ++k) v.data()[k] = static_cast<char>(0xcc);
            const char *oldFirst = v.data();
            difftest::FreeRecorder rec;
            try {
                if (side == 0) original(&v, nullptr, newSize, &tag);
                else reinterpret_cast<ByteVector *>(&v)->ResizeReallocate(newSize, &tag);
            } catch (...) {
                threw[side] = true;
            }
            rec.Stop();
            frees[side] = rec.Count();
            freedOld[side] = oldFirst && rec.Count() == 1;
            const uint32_t *w = reinterpret_cast<const uint32_t *>(&v);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "size %u capacity %u moved %d data", w[1] - w[0], w[2] - w[0], v.data() != oldFirst);
            std::string d = buf;
            uint32_t sum = 0;
            for (size_t k = 0; k < v.size(); ++k) sum = sum * 31 + static_cast<uint8_t>(v[k]);
            d += " " + std::to_string(sum);
            bool zeroed = true;
            for (size_t k = oldSize; k < v.size(); ++k) zeroed &= v[k] == 0;
            dumps[side] = d + (zeroed ? " zeroed" : " NOT zeroed");
        }
        ++ctx.cases;
        // A throw frees the exception's own message block on both sides (the vector is left as it was: the dump).
        if (dumps[0] != dumps[1] || threw[0] != threw[1] || threw[0] != tooLong || frees[0] != frees[1] || freedOld[0] != freedOld[1] ||
            (!tooLong && frees[0] != (empty ? 0u : 1u)))
            ctx.Fail("resize %zu (size %zu capacity %zu): threw %d/%d frees %zu/%zu\n %s\n %s", newSize, oldSize, capacity, threw[0], threw[1],
                frees[0], frees[1], dumps[0].c_str(), dumps[1].c_str());
    }
}
