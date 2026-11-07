// Differential tests for the cTSPersistSystem functions of lane EZ: the primary's AddSegment override (slot 28, 0x1e36f0).
//
// The world: one primary object block (0x300 bytes, random bytes from the seed) whose own vtable (slot 30 RemoveSegment,
// which the base slot 28 calls) and segment lock (+0x68) are recording fakes; the base deque at +0x70 with a map of eight
// pre-made blocks (so push_front never allocates); the segment vector at +0xd4 over a static array with or without room;
// a real mNamedMap hash table at +0xf4 (eight buckets, a load factor that never rehashes); mFieldC8 from the seed. The
// segments are fakes (QueryInterface, AddRef, Release, AutoOpen with the low byte of its bool logged, IsWritable,
// GetPath handing out paths with mixed case, slashes and heap-sized texts, SetSegmentID logging the id).
// Original callees replaced by recording stand-ins on both sides: 0x1c1931 (file attribute test: GetFileAttributes), 0x1c2182
// (can the file be written: creates and removes it), 0x1e9b50 / 0x1e97b0 (group bookkeeping, 853 / 919 bytes, they open
// the segment's key lists), and the vector growth (0x1d57e0 and our ServiceRefVector::InsertGrown, verified elsewhere).
// mNamedMap's emplace 0x1d84b0 runs as it is on both sides; the game's operator new / sized delete are redirected to ours
// so that the new nodes can be freed here. No file is touched.
// Compared: the result byte, the call log, the primary block, the deque blocks, the segment vector array, and mNamedMap
// walked in list order (each node's path and segment, which nodes are new) with its buckets as node numbers.
#include "difftest.h"
#include "rz/PathFileOps.h"
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "rz/PathAttributes.h"
#include "rz/cRZString.h"
#include "rz/nGZLog.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"
#include "shape.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using adt::Def;
using adt::gLog;

// Overwrites a function's first five bytes with a jump to a stand-in, for the scope of the test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

uint32_t gSeed = 0;
uint32_t gCalls = 0;
alignas(16) uint8_t gPrimary[0x300];

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

uint32_t TextHash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

std::string Hex(uint32_t v)
{
    return adt::Hex(v);
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

void Put(uint32_t off, uint32_t v)
{
    std::memcpy(gPrimary + off, &v, 4);
}

// The segment fakes all have the kind kOther (adt::Tag names them alike): logs and summaries name them by number.
std::vector<void *> gSegs;
std::string SegName(const void *p)
{
    for (size_t i = 0; i < gSegs.size(); ++i)
        if (gSegs[i] == p) return "seg" + std::to_string(i);
    return adt::Tag(p);
}
// Appends the segment's number to the log line the fake dispatcher has just written.
void NameSelf(const void *self)
{
    if (!gLog.empty()) gLog.back() += " @" + SegName(self);
}

// The primary's slot 54 (ClearSpeculativeLoader; past the fakes' 48 slots).
uint32_t __fastcall PrimaryClearLoader(void *, void *)
{
    gLog.push_back("primary.54");
    return 0;
}

// The game's operator new (0x824632) and sized delete (0x824624), redirected to ours.
void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDelete(void *p, size_t)
{
    ::operator delete(p);
}

// Stand-ins for the original callees (the same on both sides).
uint32_t __cdecl AttributesStand(const cRZString *path, uint32_t mode)
{
    ++gCalls;
    gLog.push_back("0x1c1931(S:" + adt::Describe(path) + ", " + Hex(mode) + ")");
    return (Mix(gSeed, 100 + gCalls) & 0xffffff00u) | (Mix(gSeed, 110 + gCalls) % 3 == 0 ? 1u : 0u);
}
uint32_t __cdecl WritableStand(const cRZString *path)
{
    ++gCalls;
    gLog.push_back("0x1c2182(S:" + adt::Describe(path) + ")");
    return (Mix(gSeed, 500 + gCalls) & 0xffffff00u) | (Mix(gSeed, 600 + gCalls) % 2);
}
uint32_t __fastcall GroupsStand(void *self, void *, void *segment)
{
    gLog.push_back("0x1e9b50(" + std::string(self == gPrimary ? "primary" : "?") + ", " + SegName(segment) + ")");
    return Mix(gSeed, 700);
}
uint32_t __fastcall RangesStand(void *self, void *, void *segment)
{
    gLog.push_back("0x1e97b0(" + std::string(self == gPrimary ? "primary" : "?") + ", " + SegName(segment) + ")");
    return Mix(gSeed, 701);
}
void *gVectorArray = nullptr;
void **__fastcall InsertGrownStand(void *self, void *, void **where, void *const *value)
{
    gLog.push_back("grow(" + std::string(self == gPrimary + 0xd4 ? "segments" : "?") + ", " +
                   std::to_string((Word(where) - Word(gVectorArray)) / 4) + ", " + SegName(*value) + ")");
    return where;
}

// The segments' slot 6 (AutoOpen): the original pushes the whole dword whose low byte it set (0x1e37a4), so only that
// byte of the bool is logged.
uint32_t __fastcall SegmentAutoOpen(void *self, void *, uint32_t read, uint32_t write)
{
    gLog.push_back("seg.6(" + SegName(self) + ", " + Hex(read & 0xff) + ", " + Hex(write & 0xff) + ")");
    return Mix(gSeed, 800);
}

const char *const kPaths[] = {"C:\\Sims\\Downloads\\Gizmo.package", "c:/sims/neighborhoods/N001/N001_Neighborhood.package",
                              "UPPER.PACKAGE", "", "D:/Other Place/Characters/", "short", "Mixed/Case\\Sep.files"};

// mNamedMap's nodes as the game lays them out (0x2c bytes).
struct MapNode {
    MapNode *next;
    MapNode *prev;
    cRZString path;
    void *segment;
};
static_assert(sizeof(MapNode) == 0x2c, "map node");
struct MapBucket {
    MapNode *first;
    MapNode *last;
};

struct NamedMap {
    MapNode head{};
    std::vector<MapNode *> nodes; // ours (made here); the others the code made
    MapBucket buckets[8];
    void Build(const std::vector<std::pair<std::string, void *>> &entries)
    {
        head.next = head.prev = &head;
        for (MapBucket &b : buckets) b.first = b.last = &head;
        std::vector<std::vector<MapNode *>> byBucket(8);
        for (const auto &e : entries) {
            auto *n = new MapNode();
            nodes.push_back(n);
            n->path.Construct(e.first.c_str());
            n->segment = e.second;
            byBucket[nGZLog::HashChars(e.first.c_str()) & 7].push_back(n);
        }
        MapNode *tail = &head;
        for (int b = 0; b < 8; ++b) {
            for (MapNode *n : byBucket[b]) {
                n->prev = tail;
                tail->next = n;
                tail = n;
            }
            if (!byBucket[b].empty()) {
                buckets[b].first = byBucket[b].front();
                buckets[b].last = byBucket[b].back();
            }
        }
        tail->next = &head;
        head.prev = tail;
    }
    // The table in list order (path, segment, made here or by the code) and the buckets as node numbers; then every node
    // is freed (those the code made came from the redirected game operator new).
    std::vector<std::string> SummaryAndFree()
    {
        std::vector<std::string> out;
        std::vector<MapNode *> order;
        for (MapNode *n = head.next; n != &head && order.size() < 64; n = n->next) order.push_back(n);
        auto number = [&](MapNode *n) {
            if (n == &head) return std::string("head");
            for (size_t i = 0; i < order.size(); ++i)
                if (order[i] == n) return std::to_string(i);
            return std::string("?");
        };
        for (MapNode *n : order) {
            bool mine = false;
            for (MapNode *m : nodes) mine = mine || m == n;
            out.push_back(std::string(mine ? "old " : "new ") + adt::Describe(&n->path) + " " + SegName(n->segment) + " prev " +
                          number(n->prev));
        }
        for (const MapBucket &b : buckets) out.push_back("bucket " + number(b.first) + ".." + number(b.last));
        for (MapNode *n : order) {
            bool mine = false;
            for (MapNode *m : nodes) mine = mine || m == n;
            n->path.Destruct();
            if (mine) delete n;
            else ::operator delete(n);
        }
        nodes.clear();
        return out;
    }
};

std::string Lower(std::string s)
{
    for (char &c : s) {
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// mNamedMap's key for a path: separators made backslashes (with one appended unless the path ends in one, as
// cRZFileName::NormaliseSeparators does), lowercase.
std::string NamedKey(const std::string &path)
{
    std::string key = Lower(path);
    if (key.empty() || key.back() != '\\') key += '\\';
    return key;
}

struct SideResult {
    uint32_t ret = 0;
    std::vector<std::string> log;
    std::vector<uint8_t> primary;
    std::vector<std::string> map;
    std::vector<std::string> vec;
    std::vector<uint32_t> deque;
};

} // namespace

PS_TEST(TSPersistSystemEZ_AddSegment, 0x1e36f0)
{
    using F = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e36f0);

    // The primary (kConsole): 30 RemoveSegment (from the base slot 28).
    Def(adt::kConsole, 30, 1, "p", [](void *, const uint32_t *) { return Mix(gSeed, 30); });
    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "");
    // Segments (kOther): 1 AddRef, 2 Release, 6 AutoOpen (replaced below), 8 IsWritable, 11 GetPath, 16 SetSegmentID.
    Def(adt::kOther, 1, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 1); });
    Def(adt::kOther, 2, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 2); });
    Def(adt::kOther, 6, 2, "ii");
    Def(adt::kOther, 8, 0, "", [](void *self, const uint32_t *) { NameSelf(self);
        ++gCalls;
        return (Mix(gSeed, 80 + gCalls) & 0xffffff00u) | (Mix(gSeed, 85 + gCalls) % 3 == 0 ? 1u : 0u);
    });
    Def(adt::kOther, 11, 1, "o", [](void *self, const uint32_t *args) { NameSelf(self);
        ++gCalls;
        reinterpret_cast<cRZString *>(args[0])->mString.assign(kPaths[Mix(gSeed, 11 + (gCalls > 3 ? 1u : 0u)) % 7]);
        return Mix(gSeed, 12);
    });
    Def(adt::kOther, 16, 1, "o", [](void *self, const uint32_t *args) { NameSelf(self);
        gLog.push_back("id " + Hex(*reinterpret_cast<const uint32_t *>(args[0])));
        return Mix(gSeed, 16);
    });

    std::vector<std::unique_ptr<adt::Fake>> segments;
    for (int i = 0; i < 4; ++i) {
        segments.emplace_back(new adt::Fake(adt::kOther));
        segments.back()->vtable[6] = reinterpret_cast<void *>(&SegmentAutoOpen);
    }
    gSegs.clear();
    for (auto &s : segments) gSegs.push_back(s->get());
    auto primaryVTable = adt::BuildVTable(adt::kConsole);
    auto lockVTable = adt::BuildVTable(adt::kLock);
    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch attributes(ctx.image.At<uint8_t>(0x1c1931), reinterpret_cast<void *>(&AttributesStand));
    CodePatch attributesOurs(reinterpret_cast<void *>(&PathAttributesMatch), reinterpret_cast<void *>(&AttributesStand)); // lane MA
    CodePatch writable(ctx.image.At<uint8_t>(0x1c2182), reinterpret_cast<void *>(&WritableStand));
    CodePatch groups(ctx.image.At<uint8_t>(0x1e9b50), reinterpret_cast<void *>(&GroupsStand));
    CodePatch ranges(ctx.image.At<uint8_t>(0x1e97b0), reinterpret_cast<void *>(&RangesStand));
    CodePatch groupsOurs(shape::MemberCode(&cTSPersistSystem::IndexSegmentGroups), reinterpret_cast<void *>(&GroupsStand));
    CodePatch rangesOurs(shape::MemberCode(&cTSPersistSystem::SegmentGroups), reinterpret_cast<void *>(&RangesStand));
    CodePatch grow(ctx.image.At<uint8_t>(0x1d57e0), reinterpret_cast<void *>(&InsertGrownStand));
    CodePatch growOurs(MemberCode(&ServiceRefVector::InsertGrown), reinterpret_cast<void *>(&InsertGrownStand));
    adt::gFakes[gPrimary] = adt::kConsole;
    adt::gFakes[gPrimary + 0x68] = adt::kLock;

    static void *vecArray[8];
    static uint32_t dequeBlocks[8][4];
    static uint32_t *dequeMap[8];
    gVectorArray = vecArray;
    NamedMap map;

    uint32_t added = 0, grown = 0, known = 0;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        std::mt19937 rng(seed * 31 + 7);
        void *segment = segments[rng() % 4]->get();
        // The segment vector: 0..6 entries from the first three fakes (maybe the argument), room or full.
        const uint32_t count = rng() % 7;
        std::vector<void *> entries;
        for (uint32_t i = 0; i < count; ++i) entries.push_back(segments[rng() % 3]->get());
        const uint32_t capacity = rng() % 3 == 0 ? count : count + 1 + rng() % (8 - count);
        // mNamedMap: some paths, maybe the one the segment will add.
        std::vector<std::pair<std::string, void *>> named;
        for (uint32_t n = rng() % 4; n > 0; --n) named.push_back({NamedKey(kPaths[rng() % 7]), segments[rng() % 4]->get()});
        const uint32_t dequeOffset = rng() % 32, dequeSize = rng() % 6;
        const uint32_t nextId = rng() % 4 ? rng() % 100 : 0xffffffffu;

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            Put(0, Word(primaryVTable.data()));
            Put(0x68, Word(lockVTable.data()));
            // The deque (map, map size, offset, size) over eight blocks.
            std::memset(dequeBlocks, 0, sizeof(dequeBlocks));
            for (int b = 0; b < 8; ++b) dequeMap[b] = dequeBlocks[b];
            Put(0x74, Word(dequeMap));
            Put(0x78, 8);
            Put(0x7c, dequeOffset);
            Put(0x80, dequeSize);
            Put(0xc8, nextId);
            // The segment vector.
            std::memset(vecArray, 0, sizeof(vecArray));
            for (uint32_t i = 0; i < count; ++i) vecArray[i] = entries[i];
            Put(0xd4, Word(vecArray));
            Put(0xd8, Word(vecArray + count));
            Put(0xdc, Word(vecArray + capacity));
            // mNamedMap: load factor, list head, size, bucket vector, mask, bucket count.
            map.Build(named);
            const float load = 4.0f;
            std::memcpy(gPrimary + 0xf4, &load, 4);
            Put(0xf8, Word(&map.head));
            Put(0xfc, static_cast<uint32_t>(named.size()));
            Put(0x100, Word(map.buckets));
            Put(0x104, Word(map.buckets + 8));
            Put(0x108, Word(map.buckets + 8));
            Put(0x10c, 7);
            Put(0x110, 8);

            const uint32_t r = side == 0 ? orig(gPrimary, segment) : psshape::Call(PSX(vf28), gPrimary, segment);
            SideResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            s.map = map.SummaryAndFree();
            for (void *p : vecArray) s.vec.push_back(SegName(p));
            for (auto &b : dequeBlocks)
                for (uint32_t w : b) s.deque.push_back(w);
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("id ", 0) == 0) ++added;
            if (line.rfind("grow(", 0) == 0) ++grown;
        }
        if (sides[0].log.size() > 0 && std::find(entries.begin(), entries.end(), segment) != entries.end()) ++known;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
        if (sides[0].map != sides[1].map) ctx.Fail("seed %u: mNamedMap differs: %s", seed, adt::FirstDifference(sides[0].map, sides[1].map).c_str());
        if (sides[0].vec != sides[1].vec) ctx.Fail("seed %u: segment vector differs", seed);
        if (sides[0].deque != sides[1].deque) ctx.Fail("seed %u: deque blocks differ", seed);
    }
    if (added < 200 || grown < 40 || known < 100) ctx.Fail("coverage: %u added, %u grown, %u already listed", added, grown, known);
    adt::gFakes.erase(gPrimary);
    adt::gFakes.erase(gPrimary + 0x68);
    for (int kind : {adt::kConsole, adt::kLock, adt::kOther})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---- RemoveSegment (primary slot 30, 0x1e57a0) ----
//
// The world adds to AddSegment's: lock C (+0x88, its own fake kind) and the value list (+0x90) the base slot 10 walks,
// the cITSPersistSystem face (+0xa8, slot 11 GetGroupIdForPath), and three more real hash tables: mSegmentsByGroup
// (+0x114, identity hash, nodes with a segment vector whose block is tracked by the allocator-service fake),
// mSegmentIdRanges (+0x134, FNV-1a of the id, nodes pointing at static group lists) and mKeyedObjects (+0x248, objects
// whose slot 9 logs the group it is given). The primary's own vtable has only slot 54 (ClearSpeculativeLoader). Nothing
// is stubbed in the image but the game's operator new / sized delete (to ours) and the allocator-service cache (the fake):
// the base slots 10 and 30, the vector moves 0x1d6df0, the erases 0x1d86f0 and 0x1e6a40 and the string teardown 0x1d8450
// run as they are on the original's side. Compared: result, call log, primary block, deque blocks, segment array, and
// every table in list order with its buckets (nodes the code frees are gone from the list; the rest are freed here).

namespace {

// A table node with the link at +0 (StdHashNode-compatible) as the tests build them.
struct Link {
    Link *next;
    Link *prev;
};
struct GroupNode {
    Link link;
    uint32_t group;
    void **first, **last, **end;
};
static_assert(sizeof(GroupNode) == 0x18, "group node");
struct IdNode {
    Link link;
    uint32_t id;
    void *range;
};
static_assert(sizeof(IdNode) == 0x10, "id node");
struct KeyedNode {
    Link link;
    uint32_t key;
    void *object;
};
static_assert(sizeof(KeyedNode) == 0x10, "keyed node");

uint32_t Fnv(uint32_t key)
{
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) h = (h ^ ((key >> (8 * i)) & 0xff)) * 0x01000193u;
    return h;
}

// A game hash table (list head, eight buckets) placed at `off` in the primary block: +0 load factor, +4 head, +8 size,
// +0xc..+0x14 the bucket vector, +0x18 mask, +0x1c bucket count.
struct Table {
    Link head{};
    Link *buckets[16];
    std::vector<Link *> made;
    void Build(uint32_t off, const std::vector<std::pair<Link *, uint32_t>> &nodes)
    {
        head.next = head.prev = &head;
        for (auto &b : buckets) b = &head;
        std::vector<std::vector<Link *>> byBucket(8);
        made.clear();
        for (const auto &n : nodes) {
            byBucket[n.second & 7].push_back(n.first);
            made.push_back(n.first);
        }
        Link *tail = &head;
        for (int b = 0; b < 8; ++b) {
            for (Link *n : byBucket[b]) {
                n->prev = tail;
                tail->next = n;
                tail = n;
            }
            if (!byBucket[b].empty()) {
                buckets[2 * b] = byBucket[b].front();
                buckets[2 * b + 1] = byBucket[b].back();
            }
        }
        tail->next = &head;
        head.prev = tail;
        const float load = 4.0f;
        std::memcpy(gPrimary + off, &load, 4);
        Put(off + 4, Word(&head));
        Put(off + 8, static_cast<uint32_t>(nodes.size()));
        Put(off + 0xc, Word(buckets));
        Put(off + 0x10, Word(buckets + 16));
        Put(off + 0x14, Word(buckets + 16));
        Put(off + 0x18, 7);
        Put(off + 0x1c, 8);
    }
    // The nodes in list order, each described by `describe`, then the buckets as node numbers.
    template <typename F> std::vector<std::string> Summary(F describe, std::vector<Link *> &order)
    {
        std::vector<std::string> out;
        order.clear();
        for (Link *n = head.next; n != &head && order.size() < 64; n = n->next) order.push_back(n);
        auto number = [&](Link *n) {
            if (n == &head) return std::string("head");
            for (size_t i = 0; i < order.size(); ++i)
                if (order[i] == n) return std::to_string(i);
            return std::string("?");
        };
        for (Link *n : order) out.push_back(describe(n) + " prev " + number(n->prev));
        for (int b = 0; b < 8; ++b) out.push_back("bucket " + number(buckets[2 * b]) + ".." + number(buckets[2 * b + 1]));
        return out;
    }
};

uint32_t gIsOpenCalls = 0;

} // namespace

PS_TEST(TSPersistSystemEZ_RemoveSegment, 0x1e57a0)
{
    using F = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e57a0);

    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "");
    Def(adt::kStreamBase, 1, 0, ""); // lock C
    Def(adt::kStreamBase, 2, 0, "");
    // The cITSPersistSystem face (kFramework): 11 GetGroupIdForPath.
    Def(adt::kFramework, 11, 1, "S", [](void *, const uint32_t *) { return 0x7f000000u | (Mix(gSeed, 111) & 0xffffff); });
    // Values of the list at +0x90 (kConsoleService): 10 holds the key?, 9 releases it.
    Def(adt::kConsoleService, 10, 1, "q", [](void *, const uint32_t *args) {
        return (Mix(gSeed, 210) & 0xffffff00u) | (Mix(gSeed, *reinterpret_cast<const uint32_t *>(args[0])) % 2);
    });
    Def(adt::kConsoleService, 9, 1, "q", [](void *, const uint32_t *args) {
        return (Mix(gSeed, 209) & 0xffffff00u) | (Mix(gSeed, 1 + *reinterpret_cast<const uint32_t *>(args[0])) % 2);
    });
    // Keyed objects (kCommonImpl): 9 is told the group.
    Def(adt::kCommonImpl, 9, 1, "o", [](void *, const uint32_t *args) {
        gLog.push_back("keyed.9 group " + Hex(*reinterpret_cast<const uint32_t *>(args[0])));
        return 0u;
    });
    // Segments (kOther): 1 AddRef, 2 Release, 7 IsOpen, 9 Close, 11 GetPath, 15 GetSegmentID, 19 GetResourceKeyList.
    static adt::AllocatorFake *allocPtr = nullptr;
    Def(adt::kOther, 1, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 1); });
    Def(adt::kOther, 2, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 2); });
    Def(adt::kOther, 7, 0, "", [](void *self, const uint32_t *) { NameSelf(self);
        ++gIsOpenCalls;
        return (Mix(gSeed, 70 + gIsOpenCalls) & 0xffffff00u) | (Mix(gSeed, 75 + gIsOpenCalls) % 4 != 0 ? 1u : 0u);
    });
    Def(adt::kOther, 9, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 9); });
    Def(adt::kOther, 11, 1, "o", [](void *self, const uint32_t *args) { NameSelf(self);
        reinterpret_cast<cRZString *>(args[0])->mString.assign(kPaths[Mix(gSeed, 11) % 7]);
        return 0u;
    });
    Def(adt::kOther, 15, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return 0x100u + (Word(self) >> 4 & 0xff) % 7; });
    Def(adt::kOther, 19, 2, "oi", [](void *self, const uint32_t *args) { NameSelf(self);
        const uint32_t count = Mix(gSeed, 19) % 4;
        const uint32_t answer = Mix(gSeed, 20) % 5 == 0 ? 0u : count + (Mix(gSeed, 21) % 2);
        if (count) {
            auto *block = static_cast<uint32_t *>(::operator new(count * 16 + 8));
            allocPtr->Track(block);
            for (uint32_t i = 0; i < count * 4; ++i) block[i] = Mix(gSeed, 300 + i);
            auto *list = reinterpret_cast<uint32_t **>(args[0]);
            list[0] = block;
            list[1] = block + count * 4;
            list[2] = block + count * 4 + 2;
        }
        return answer;
    });

    adt::AllocatorFake alloc(ctx);
    allocPtr = &alloc;
    std::vector<std::unique_ptr<adt::Fake>> segments, values, keyed;
    for (int i = 0; i < 4; ++i) segments.emplace_back(new adt::Fake(adt::kOther));
    gSegs.clear();
    for (auto &s : segments) gSegs.push_back(s->get());
    for (int i = 0; i < 2; ++i) values.emplace_back(new adt::Fake(adt::kConsoleService));
    for (int i = 0; i < 2; ++i) keyed.emplace_back(new adt::Fake(adt::kCommonImpl));
    auto lockVTable = adt::BuildVTable(adt::kLock);
    auto lockCVTable = adt::BuildVTable(adt::kStreamBase);
    auto itsVTable = adt::BuildVTable(adt::kFramework);
    static std::array<void *, 64> primaryVTable{};
    primaryVTable[54] = reinterpret_cast<void *>(&PrimaryClearLoader);
    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    adt::gFakes[gPrimary + 0x68] = adt::kLock;
    adt::gFakes[gPrimary + 0x88] = adt::kStreamBase;
    adt::gFakes[gPrimary + 0xa8] = adt::kFramework;

    static void *vecArray[8];
    static uint32_t dequeBlocks[8][4];
    static uint32_t *dequeMap[8];
    static uint32_t groupLists[4][6];
    static uint32_t ranges[4][2];
    gVectorArray = vecArray;
    NamedMap named;
    Table groupsTable, idsTable, keyedTable;
    struct ValueNode {
        ValueNode *next, *prev;
        void *value;
    };
    ValueNode valueHead{}, valueNodes[2]{};
    const uint32_t kGroups[] = {0x10, 0x7f000123, 0x6f000001, 0x22, 0x10};

    uint32_t removed = 0, groupsGone = 0, namedGone = 0, notListed = 0, namedExpected = 0;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        std::mt19937 rng(seed * 37 + 3);
        void *segment = segments[rng() % 4]->get();
        const uint32_t count = rng() % 6;
        std::vector<void *> entries;
        for (uint32_t i = 0; i < count; ++i) entries.push_back(segments[rng() % 4]->get());
        if (rng() % 3 && count) entries[rng() % count] = segment;
        // mNamedMap: maybe the segment's own path (lowercased) once or twice, with other segments next to it.
        std::vector<std::pair<std::string, void *>> namedEntries;
        for (uint32_t n = rng() % 5; n > 0; --n) {
            const bool own = rng() % 4 != 0;
            const std::string text = own ? NamedKey(kPaths[Mix(seed, 11) % 7]) : NamedKey(kPaths[rng() % 7]);
            namedEntries.push_back({text, rng() % 3 ? segment : segments[rng() % 4]->get()});
        }
        const uint32_t dequeOffset = rng() % 32, dequeSize = rng() % 6;
        uint32_t dequeContent[8];
        for (uint32_t &w : dequeContent) w = Word(segments[rng() % 4]->get());
        // Group lists for the four segment ids (0x100 + n), present or not in the id table.
        uint32_t groupCounts[4], idPresent[4];
        for (int s = 0; s < 4; ++s) {
            groupCounts[s] = rng() % 4;
            idPresent[s] = rng() % 4 != 0;
            for (uint32_t g = 0; g < 6; ++g) groupLists[s][g] = kGroups[rng() % 5];
        }
        // Segments by group: each group of kGroups (distinct ones) present or not, with 0..3 segments.
        struct GroupPlan {
            uint32_t group;
            std::vector<void *> members;
        };
        std::vector<GroupPlan> groupPlan;
        for (uint32_t g : {0x10u, 0x7f000123u, 0x6f000001u, 0x22u}) {
            if (rng() % 4 == 0) continue;
            GroupPlan p{g, {}};
            for (uint32_t n = rng() % 4; n > 0; --n) p.members.push_back(segments[rng() % 4]->get());
            if (rng() % 2) p.members.push_back(segment);
            groupPlan.push_back(p);
        }
        const uint32_t valueCount = rng() % 3, keyedCount = rng() % 3;

        SideResult sides[2];
        std::vector<std::string> tables[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gIsOpenCalls = 0;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            Put(0, Word(primaryVTable.data()));
            Put(0x68, Word(lockVTable.data()));
            Put(0x88, Word(lockCVTable.data()));
            Put(0xa8, Word(itsVTable.data()));
            // The deque over eight blocks, its elements from the plan.
            std::memset(dequeBlocks, 0, sizeof(dequeBlocks));
            for (int b = 0; b < 8; ++b) dequeMap[b] = dequeBlocks[b];
            for (uint32_t i = 0; i < dequeSize; ++i) {
                const uint32_t pos = (dequeOffset + i) & 31;
                dequeBlocks[pos >> 2][pos & 3] = dequeContent[i];
            }
            // +0x70 is the deque's container proxy (the game's iterators reach the deque through its first word).
            static uint32_t proxy[2];
            proxy[0] = Word(gPrimary + 0x70);
            proxy[1] = 0;
            Put(0x70, Word(proxy));
            Put(0x74, Word(dequeMap));
            Put(0x78, 8);
            Put(0x7c, dequeOffset);
            Put(0x80, dequeSize);
            // The value list.
            valueHead.next = valueHead.prev = &valueHead;
            ValueNode *tail = &valueHead;
            for (uint32_t i = 0; i < valueCount; ++i) {
                valueNodes[i].value = values[i]->get();
                valueNodes[i].prev = tail;
                tail->next = &valueNodes[i];
                tail = &valueNodes[i];
            }
            tail->next = &valueHead;
            valueHead.prev = tail;
            Put(0x90, Word(&valueHead));
            Put(0x94, valueCount);
            // The segment vector.
            std::memset(vecArray, 0, sizeof(vecArray));
            for (uint32_t i = 0; i < count; ++i) vecArray[i] = entries[i];
            Put(0xd4, Word(vecArray));
            Put(0xd8, Word(vecArray + count));
            Put(0xdc, Word(vecArray + 8));
            // mNamedMap.
            named.Build(namedEntries);
            {
                const float load = 4.0f;
                std::memcpy(gPrimary + 0xf4, &load, 4);
            }
            Put(0xf8, Word(&named.head));
            Put(0xfc, static_cast<uint32_t>(namedEntries.size()));
            Put(0x100, Word(named.buckets));
            Put(0x104, Word(named.buckets + 8));
            Put(0x108, Word(named.buckets + 8));
            Put(0x10c, 7);
            Put(0x110, 8);
            // mSegmentsByGroup.
            std::vector<std::pair<Link *, uint32_t>> nodes;
            for (const GroupPlan &p : groupPlan) {
                auto *n = static_cast<GroupNode *>(::operator new(sizeof(GroupNode)));
                n->group = p.group;
                const size_t cap = p.members.size() + 1;
                n->first = static_cast<void **>(::operator new(cap * sizeof(void *)));
                alloc.Track(n->first);
                for (size_t i = 0; i < p.members.size(); ++i) n->first[i] = p.members[i];
                n->last = n->first + p.members.size();
                n->end = n->first + cap;
                nodes.push_back({&n->link, p.group});
            }
            groupsTable.Build(0x114, nodes);
            // mSegmentIdRanges.
            nodes.clear();
            for (int s = 0; s < 4; ++s) {
                if (!idPresent[s]) continue;
                auto *n = static_cast<IdNode *>(::operator new(sizeof(IdNode)));
                n->id = 0x100u + (Word(segments[s]->get()) >> 4 & 0xff) % 7;
                ranges[s][0] = Word(groupLists[s]);
                ranges[s][1] = Word(groupLists[s] + groupCounts[s]);
                n->range = ranges[s];
                bool duplicate = false;
                for (auto &e : nodes) duplicate = duplicate || reinterpret_cast<IdNode *>(e.first)->id == n->id;
                if (duplicate) {
                    ::operator delete(n);
                    continue;
                }
                nodes.push_back({&n->link, Fnv(n->id)});
            }
            idsTable.Build(0x134, nodes);
            // mKeyedObjects.
            nodes.clear();
            for (uint32_t i = 0; i < keyedCount; ++i) {
                auto *n = static_cast<KeyedNode *>(::operator new(sizeof(KeyedNode)));
                n->key = 0x500 + i;
                n->object = keyed[i]->get();
                nodes.push_back({&n->link, Fnv(n->key)});
            }
            keyedTable.Build(0x248, nodes);

            const uint32_t r = side == 0 ? orig(gPrimary, segment) : psshape::Call(PSX(vf30), gPrimary, segment);
            SideResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            s.map = named.SummaryAndFree();
            for (void *p : vecArray) s.vec.push_back(SegName(p));
            for (auto &b : dequeBlocks)
                for (uint32_t w : b) s.deque.push_back(w);
            std::vector<Link *> order;
            for (const std::string &line : groupsTable.Summary(
                     [](Link *l) {
                         auto *n = reinterpret_cast<GroupNode *>(l);
                         std::string d = "group " + Hex(n->group) + " cap " + std::to_string(n->end - n->first) + ":";
                         for (void **p = n->first; p != n->last; ++p) d += " " + SegName(*p);
                         return d;
                     },
                     order))
                tables[side].push_back(line);
            for (Link *l : order) ::operator delete(l);
            for (const std::string &line : idsTable.Summary(
                     [](Link *l) {
                         auto *n = reinterpret_cast<IdNode *>(l);
                         return "id " + Hex(n->id);
                     },
                     order))
                tables[side].push_back(line);
            for (Link *l : order) ::operator delete(l);
            for (const std::string &line : keyedTable.Summary([](Link *l) { return "keyed " + Hex(reinterpret_cast<KeyedNode *>(l)->key); }, order))
                tables[side].push_back(line);
            for (Link *l : order) ::operator delete(l);
            alloc.Release();
        }
        ++ctx.cases;
        if (sides[0].ret) ++removed;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("alloc.4(", 0) == 0) ++groupsGone;
        }
        if (sides[0].map.size() < namedEntries.size() + 8) ++namedGone;
        {
            const bool listed = std::find(entries.begin(), entries.end(), segment) != entries.end();
            bool own = false;
            for (auto &e : namedEntries) own = own || (e.first == NamedKey(kPaths[Mix(seed, 11) % 7]) && e.second == segment);
            if (listed && own) ++namedExpected;
        }
        if (std::find(entries.begin(), entries.end(), segment) == entries.end()) ++notListed;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
        if (sides[0].map != sides[1].map) ctx.Fail("seed %u: mNamedMap differs: %s", seed, adt::FirstDifference(sides[0].map, sides[1].map).c_str());
        if (sides[0].vec != sides[1].vec) ctx.Fail("seed %u: segment vector differs", seed);
        if (sides[0].deque != sides[1].deque) ctx.Fail("seed %u: deque blocks differ", seed);
        if (tables[0] != tables[1]) ctx.Fail("seed %u: tables differ: %s", seed, adt::FirstDifference(tables[0], tables[1]).c_str());
    }
    if (removed < 200 || groupsGone < 100 || namedGone < 100 || notListed < 100)
        ctx.Fail("coverage: %u removed, %u frees, %u named erased (%u expected), %u not listed", removed, groupsGone, namedGone, namedExpected, notListed);
    allocPtr = nullptr;
    for (uint32_t off : {0x68u, 0x88u, 0xa8u}) adt::gFakes.erase(gPrimary + off);
    for (int kind : {adt::kLock, adt::kStreamBase, adt::kFramework, adt::kConsoleService, adt::kCommonImpl, adt::kOther})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---- DeleteGroupFiles (cITSPersistSystem slot 24, 0x1de470) ----
//
// The world: the primary block with its own vtable (slot 30 RemoveSegment, logging which segment) and lock B; a real
// mSegmentsByGroup (+0x114, the group's node present or not; absent groups go to the fill-in 0x1e95c0, a stand-in that
// hands out a static list or null) and mMap154 (+0x154, nodes with a key and maybe a heap vector of dwords); the
// allocator-service fake (0x1d89b0's copy allocates through it, the copy is freed through it). Recording stand-ins on both
// sides: 0x1e6e80 (the path's key: it reads the cTSAppCommonImpl options), 0x1c1931 (file attributes), 0x1c5fa0 and
// 0x1c2e25 (the deletes). 0x1d89b0, 0x1d5c20 and 0x1d9a10 run as they are. Compared: result, call log, primary block,
// both tables in list order with buckets.

namespace {

struct PathNode154 {
    Link link;
    cRZString key;
    uint32_t rest[2];
    uint32_t *first, *last, *end;
};
static_assert(sizeof(PathNode154) == 0x3c, "mMap154 node");

const char *const kKeys[] = {"short\\", "upper.package\\", "%gamedatadir%\\x.package\\", "mixed\\case\\sep.files\\", "\\"};
void *gFillList = nullptr;

void __fastcall MakeKeyStand(void *self, void *, const cRZString *path, cRZString *key)
{
    gLog.push_back("0x1e6e80(" + std::string(self == gPrimary ? "primary" : "?") + ", S:" + adt::Describe(path) + ")");
    key->mString.assign(kKeys[Mix(gSeed, TextHash(path->mString)) % 5]);
}
void *__fastcall FillInStand(void *self, void *, uint32_t group)
{
    gLog.push_back("0x1e95c0(" + std::string(self == gPrimary ? "primary" : "?") + ", " + Hex(group) + ")");
    return Mix(gSeed, 950) % 3 ? gFillList : nullptr;
}
uint32_t __cdecl DeleteFolderStand(const cRZString *path, uint32_t recursive)
{
    gLog.push_back("0x1c5fa0(S:" + adt::Describe(path) + ", " + Hex(recursive) + ")");
    return Mix(gSeed, 951);
}
uint32_t __cdecl DeleteFileStand(const cRZString *path)
{
    gLog.push_back("0x1c2e25(S:" + adt::Describe(path) + ")");
    return Mix(gSeed, 952);
}

} // namespace

PS_TEST(TSPersistSystemEZ_DeleteGroupFiles, 0x1de470)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1de470);

    Def(adt::kConsole, 30, 1, "i", [](void *, const uint32_t *args) {
        gLog.back() += " " + SegName(reinterpret_cast<void *>(args[0]));
        return Mix(gSeed, 30);
    });
    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "");
    static void *statusPtr = nullptr;
    // Segments (kOther): 0 QueryInterface (the key-status face, or none), 1 AddRef, 2 Release, 11 GetPath.
    Def(adt::kOther, 0, 2, "io", [](void *self, const uint32_t *args) {
        NameSelf(self);
        void *answer = Mix(gSeed, 400 + (Word(self) >> 4 & 0xff)) % 3 ? statusPtr : nullptr;
        *reinterpret_cast<void **>(args[1]) = answer;
        return (Mix(gSeed, 401) & 0xffffff00u) | (answer ? 1u : 0u);
    });
    Def(adt::kOther, 1, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 1); });
    Def(adt::kOther, 2, 0, "", [](void *self, const uint32_t *) { NameSelf(self); return Mix(gSeed, 2); });
    Def(adt::kOther, 11, 1, "o", [](void *self, const uint32_t *args) {
        NameSelf(self);
        reinterpret_cast<cRZString *>(args[0])->mString.assign(kPaths[Mix(gSeed, 11 + (Word(self) >> 4 & 0xff)) % 7]);
        return 0u;
    });
    // The key-status face (kConsoleService): 2 Release, 6 the status (1 = unused).
    static uint32_t statusCalls = 0;
    Def(adt::kConsoleService, 2, 0, "");
    Def(adt::kConsoleService, 6, 0, "", [](void *, const uint32_t *) {
        ++statusCalls;
        const uint32_t r = Mix(gSeed, 600 + statusCalls) % 4;
        return r == 3 ? 0x101u : r;
    });

    adt::AllocatorFake alloc(ctx);
    std::vector<std::unique_ptr<adt::Fake>> segments;
    for (int i = 0; i < 4; ++i) segments.emplace_back(new adt::Fake(adt::kOther));
    gSegs.clear();
    for (auto &s : segments) gSegs.push_back(s->get());
    adt::Fake status(adt::kConsoleService);
    statusPtr = status.get();
    auto primaryVTable = adt::BuildVTable(adt::kConsole);
    auto lockVTable = adt::BuildVTable(adt::kLock);
    CodePatch gameNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch attributes(ctx.image.At<uint8_t>(0x1c1931), reinterpret_cast<void *>(&AttributesStand));
    CodePatch attributesOurs(reinterpret_cast<void *>(&PathAttributesMatch), reinterpret_cast<void *>(&AttributesStand)); // lane MA
    CodePatch makeKey(ctx.image.At<uint8_t>(0x1e6e80), reinterpret_cast<void *>(&MakeKeyStand));
    CodePatch makeKeyOurs(shape::MemberCode(&cTSPersistSystem::MakePathKey), reinterpret_cast<void *>(&MakeKeyStand));
    CodePatch fillIn(ctx.image.At<uint8_t>(0x1e95c0), reinterpret_cast<void *>(&FillInStand));
    CodePatch fillInOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStand));
    CodePatch deleteFolder(ctx.image.At<uint8_t>(0x1c5fa0), reinterpret_cast<void *>(&DeleteFolderStand));
    CodePatch deleteFile(ctx.image.At<uint8_t>(0x1c2e25), reinterpret_cast<void *>(&DeleteFileStand));
    CodePatch deleteFileOurs(reinterpret_cast<void *>(&PathDelete), reinterpret_cast<void *>(&DeleteFileStand));
    adt::gFakes[gPrimary] = adt::kConsole;
    adt::gFakes[gPrimary + 0x68] = adt::kLock;

    Table groupsTable, keysTable;
    static void *fillArray[6];
    static void *fillVector[3];
    gFillList = fillVector;

    uint32_t deleted = 0, skipped = 0, empty = 0, keysGone = 0;
    // 1200 seeds: the only-unused flag comes up in about a quarter of them, and 800 left it near its floor of 200.
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 rng(seed * 41 + 5);
        const uint32_t group = rng() % 3 ? 0x7f000010u + rng() % 3 : 0x6f000001u;
        const uint32_t onlyUnused = rng() % 2 ? (rng() & 0xffffff00u) | (rng() % 2) : 0u;
        // mSegmentsByGroup: up to three groups (maybe the asked one) with 0..4 segments.
        struct GroupPlan {
            uint32_t group;
            std::vector<void *> members;
        };
        std::vector<GroupPlan> plan;
        for (uint32_t g : {0x7f000010u, 0x7f000011u, 0x7f000012u}) {
            if (rng() % 3 == 0) continue;
            GroupPlan p{g, {}};
            for (uint32_t n = rng() % 5; n > 0; --n) p.members.push_back(segments[rng() % 4]->get());
            plan.push_back(p);
        }
        std::vector<void *> fillMembers;
        for (uint32_t n = rng() % 4; n > 0; --n) fillMembers.push_back(segments[rng() % 4]->get());
        // mMap154: some keys, each with or without a heap vector.
        std::vector<std::pair<std::string, uint32_t>> keyPlan;
        for (uint32_t n = rng() % 5; n > 0; --n) keyPlan.push_back({kKeys[rng() % 5], rng() % 4});

        SideResult sides[2];
        std::vector<std::string> tables[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            statusCalls = 0;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            Put(0, Word(primaryVTable.data()));
            Put(0x68, Word(lockVTable.data()));
            std::vector<std::pair<Link *, uint32_t>> nodes;
            for (const GroupPlan &p : plan) {
                auto *n = static_cast<GroupNode *>(::operator new(sizeof(GroupNode)));
                n->group = p.group;
                const size_t cap = p.members.size() + 1;
                n->first = static_cast<void **>(::operator new(cap * sizeof(void *)));
                alloc.Track(n->first);
                for (size_t i = 0; i < p.members.size(); ++i) n->first[i] = p.members[i];
                n->last = n->first + p.members.size();
                n->end = n->first + cap;
                nodes.push_back({&n->link, p.group});
            }
            groupsTable.Build(0x114, nodes);
            std::memset(fillArray, 0, sizeof(fillArray));
            for (size_t i = 0; i < fillMembers.size(); ++i) fillArray[i] = fillMembers[i];
            fillVector[0] = fillArray;
            fillVector[1] = fillArray + fillMembers.size();
            fillVector[2] = fillArray + 6;
            nodes.clear();
            for (const auto &k : keyPlan) {
                auto *n = static_cast<PathNode154 *>(::operator new(sizeof(PathNode154)));
                n->key.Construct(k.first.c_str());
                n->first = n->last = n->end = nullptr;
                if (k.second) {
                    n->first = static_cast<uint32_t *>(::operator new(k.second * 4));
                    n->last = n->first + k.second / 2;
                    n->end = n->first + k.second;
                }
                n->rest[0] = n->rest[1] = 0;
                nodes.push_back({&n->link, nGZLog::HashChars(k.first.c_str())});
            }
            keysTable.Build(0x154, nodes);

            void *face = gPrimary + 0xa8;
            const uint32_t r = side == 0 ? orig(face, group, onlyUnused)
                                         : psshape::Call(PSX(DeleteGroupFiles), face, group, onlyUnused);
            SideResult &s = sides[side];
            s.ret = r & 0xff;
            s.log = gLog;
            s.primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            std::vector<Link *> order;
            for (const std::string &line : groupsTable.Summary(
                     [](Link *l) {
                         auto *n = reinterpret_cast<GroupNode *>(l);
                         std::string d = "group " + Hex(n->group) + ":";
                         for (void **p = n->first; p != n->last; ++p) d += " " + SegName(*p);
                         return d;
                     },
                     order))
                tables[side].push_back(line);
            for (Link *l : order) ::operator delete(l);
            for (const std::string &line : keysTable.Summary(
                     [](Link *l) {
                         auto *n = reinterpret_cast<PathNode154 *>(l);
                         return "key " + adt::Describe(&n->key) + " " + std::to_string(n->end - n->first);
                     },
                     order))
                tables[side].push_back(line);
            for (Link *l : order) {
                auto *n = reinterpret_cast<PathNode154 *>(l);
                n->key.Destruct();
                ::operator delete(n->first);
                ::operator delete(n);
            }
            for (void *p : fillArray) s.vec.push_back(SegName(p));
            alloc.Release();
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("0x1c5fa0", 0) == 0 || line.rfind("0x1c2e25", 0) == 0) ++deleted;
        }
        if (sides[0].ret == 0) ++empty;
        if (onlyUnused & 0xff) ++skipped;
        if (tables[0].size() < plan.size() + keyPlan.size() + 16) ++keysGone;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
        if (sides[0].vec != sides[1].vec) ctx.Fail("seed %u: fill-in list differs", seed);
        if (tables[0] != tables[1]) ctx.Fail("seed %u: tables differ: %s", seed, adt::FirstDifference(tables[0], tables[1]).c_str());
    }
    if (deleted < 300 || empty < 100 || skipped < 200 || keysGone < 100)
        ctx.Fail("coverage: %u deletes, %u empty, %u only-unused, %u keys erased", deleted, empty, skipped, keysGone);
    adt::gFakes.erase(gPrimary);
    adt::gFakes.erase(gPrimary + 0x68);
    for (int kind : {adt::kConsole, adt::kLock, adt::kConsoleService, adt::kOther})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}
