// cGZPersistResourceManager::Shutdown (cRZSystemService slot 9, 0x9b57e0, `this` = the +4 face): original vs ours on a
// manager built with the library containers the game's are (value list of cache strategies, factory and type maps,
// segment deque, default segment), every object a logging fake. Compared: the result, the calls in order (locks,
// strategy / factory / segment slots), every heap block freed (FreeRecorder: sorted sizes), and the object afterwards
// (every word; heap pointers as "unchanged", containers by structure). Lane NP.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZPersistResourceManager.h"

#include <cstring>
#include <deque>
#include <new>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

using FactoryMap = std::unordered_map<uint32_t, uint32_t>;
static_assert(sizeof(std::deque<void *>) == sizeof(cGZPersistDeque), "the segment deque's library type");

constexpr size_t kMgrBytes = 0xa8;
constexpr int kObjects = 24;

alignas(16) uint8_t gMgr[kMgrBytes];
struct FakeObj {
    void *const *vtable;
    uint32_t pad[3];
};
FakeObj gObjs[kObjects];
void *gObjTable[12];
void *gLockTable[3];
std::vector<int64_t> gLog;

enum Kind : int64_t { kEnter = 1, kLeave, kSlot };

int64_t Id(const void *p)
{
    const auto a = reinterpret_cast<uintptr_t>(p);
    const auto in = [&](const void *base, size_t n) {
        const auto b = reinterpret_cast<uintptr_t>(base);
        return a >= b && a < b + n;
    };
    if (p == nullptr) return 0;
    if (in(gMgr, sizeof(gMgr))) return 0x10000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gMgr));
    if (in(gObjs, sizeof(gObjs))) return 0x20000 + static_cast<int64_t>(a - reinterpret_cast<uintptr_t>(gObjs));
    if (p == gLockTable) return 0x30000;
    return -1;
}

void __fastcall LockEnter(void *self, void *) { gLog.insert(gLog.end(), {kEnter, Id(self)}); }
uint32_t __fastcall LockLeave(void *self, void *)
{
    gLog.insert(gLog.end(), {kLeave, Id(self)});
    return 0x5a;
}
template <int Slot> uint32_t __fastcall ObjSlot(void *self, void *)
{
    gLog.insert(gLog.end(), {kSlot, Slot, Id(self)});
    return 0;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t n) { ::operator delete(p, n); }

template <typename T> T &At(size_t off) { return *reinterpret_cast<T *>(gMgr + off); }
uint32_t Word(size_t off)
{
    uint32_t v;
    std::memcpy(&v, gMgr + off, 4);
    return v;
}
uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// An object pointer or null, as a world word.
uint32_t PickObj(std::mt19937 &rng, bool allowNull)
{
    if (allowNull && rng() % 4 == 0) return 0;
    return Addr(&gObjs[rng() % kObjects]);
}

struct Before {
    uint32_t words[kMgrBytes / 4];
    uint8_t typeMap[0x20];
};

// The manager for one seed: random bytes everywhere the call must not care about, then the locks and containers.
void Build(uint32_t seed, Before &before)
{
    std::mt19937 rng(seed);
    for (auto &b : gMgr) b = static_cast<uint8_t>(rng());
    At<void *>(0x20) = gLockTable;
    At<void *>(0x68) = gLockTable;
    At<void *>(0x88) = gLockTable;
    auto *factories = new (&At<uint8_t>(0x28)) FactoryMap();
    for (uint32_t n = rng() % 7; n > 0; --n) (*factories)[rng() % 64] = PickObj(rng, true);
    if (rng() % 3 == 0) factories->rehash(64 + rng() % 64); // big tables take the other clear path
    auto *types = new (&At<uint8_t>(0x48)) FactoryMap();
    for (uint32_t n = rng() % 4; n > 0; --n) (*types)[rng() % 64] = rng() % 64;
    auto *segments = new (&At<uint8_t>(0x70)) std::deque<void *>();
    for (uint32_t n = rng() % 10; n > 0; --n) {
        void *obj = reinterpret_cast<void *>(static_cast<uintptr_t>(PickObj(rng, true)));
        if (rng() % 2) segments->push_back(obj);
        else segments->push_front(obj);
    }
    for (uint32_t n = rng() % 3; n > 0 && !segments->empty(); --n) segments->pop_front(); // a non-zero offset
    At<uint32_t>(0x84) = PickObj(rng, true);
    auto *head = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
    head->next = head;
    head->prev = head;
    head->value = nullptr;
    uint32_t size = 0;
    for (uint32_t n = rng() % 5; n > 0; --n, ++size) {
        auto *node = static_cast<cGZPersistValueNode *>(::operator new(sizeof(cGZPersistValueNode)));
        node->value = &gObjs[rng() % kObjects]; // the original calls slot 4 on every value unchecked
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
    }
    At<cGZPersistValueNode *>(0x90) = head;
    At<uint32_t>(0x94) = size;
    std::memcpy(before.words, gMgr, kMgrBytes);
    std::memcpy(before.typeMap, gMgr + 0x48, 0x20);
}

struct Outcome {
    uint32_t result = 0;
    std::vector<int64_t> log;
    std::vector<int64_t> dump;
};

// Words that hold heap pointers: the factory and type maps' heads and bucket vectors, the deque's proxy and map, the
// value list's head.
bool HeapWord(size_t off)
{
    return off == 0x2c || (off >= 0x34 && off < 0x40) || off == 0x4c || (off >= 0x54 && off < 0x60) || off == 0x70 ||
           off == 0x74 || off == 0x90;
}

std::vector<int64_t> Dump(const Before &before)
{
    std::vector<int64_t> d;
    for (size_t off = 0; off < kMgrBytes; off += 4) {
        const uint32_t v = Word(off);
        if (off == 0x74) d.push_back(v != 0); // the deque's map: null once cleared
        else if (HeapWord(off)) d.push_back(v == before.words[off / 4]);
        else {
            const int64_t id = Id(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)));
            d.push_back(id > 0 ? id : static_cast<int64_t>(v));
        }
    }
    // The value list: its head linked to itself.
    auto *head = At<cGZPersistValueNode *>(0x90);
    d.push_back(head->next == head);
    d.push_back(head->prev == head);
    // The factory map: list empty, every bucket at the head.
    auto *map = reinterpret_cast<const cGZPersistHashMap *>(gMgr + 0x28);
    d.push_back(map->head->next == map->head);
    d.push_back(map->head->prev == map->head);
    d.push_back(map->bucketsEnd - map->buckets);
    for (const cGZPersistBucket *b = map->buckets; b != map->bucketsEnd; ++b) d.push_back(b->first == map->head && b->last == map->head);
    // The type map: untouched, entries and all.
    d.push_back(std::memcmp(gMgr + 0x48, before.typeMap, 0x20) == 0);
    for (auto &entry : At<FactoryMap>(0x48)) d.insert(d.end(), {entry.first, entry.second});
    return d;
}

void Teardown()
{
    std::destroy_at(&At<FactoryMap>(0x28));
    std::destroy_at(&At<FactoryMap>(0x48));
    std::destroy_at(&At<std::deque<void *>>(0x70));
    auto *head = At<cGZPersistValueNode *>(0x90);
    // A correct Shutdown leaves only the head; anything else is freed so the next world starts clean.
    for (cGZPersistValueNode *node = head->next; node != head;) {
        cGZPersistValueNode *next = node->next;
        ::operator delete(node);
        node = next;
    }
    ::operator delete(head);
}

template <typename Call> Outcome Run(uint32_t seed, Call call)
{
    Before before;
    Build(seed, before);
    gLog.clear();
    Outcome out;
    difftest::FreeRecorder frees;
    out.result = call(gMgr + 4);
    frees.Stop();
    out.log = gLog;
    size_t sizes[512];
    const size_t n = frees.SortedSizes(sizes, 512);
    out.log.push_back(static_cast<int64_t>(n));
    for (size_t k = 0; k < n; ++k) out.log.push_back(static_cast<int64_t>(sizes[k]));
    out.log.push_back(frees.Overflowed());
    out.dump = Dump(before);
    Teardown();
    gLog.clear();
    return out;
}

} // namespace

// 0x9b57e0 on 3000 managers: 0..6 factories (some null; one table in three rehashed to 64+ buckets, so the game's
// clear takes its erase-each path as well as its whole-table one), 0..3 types, 0..9 segments (some null, pushed at
// both ends, some popped from the front), a default segment or none, 0..4 cache strategies.
DIFF_TEST(GZPersistResourceManager_Shutdown, 0x9b57e0)
{
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    gLockTable[1] = reinterpret_cast<void *>(&LockEnter);
    gLockTable[2] = reinterpret_cast<void *>(&LockLeave);
    for (int i = 0; i < 12; ++i) gObjTable[i] = reinterpret_cast<void *>(&ObjSlot<99>);
    gObjTable[1] = reinterpret_cast<void *>(&ObjSlot<1>);
    gObjTable[2] = reinterpret_cast<void *>(&ObjSlot<2>);
    gObjTable[4] = reinterpret_cast<void *>(&ObjSlot<4>);
    gObjTable[9] = reinterpret_cast<void *>(&ObjSlot<9>);
    gObjTable[10] = reinterpret_cast<void *>(&ObjSlot<10>);
    for (auto &o : gObjs) o.vtable = gObjTable;
    gLog.reserve(1 << 14);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b57e0);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        auto a = Run(seed, [&](uint8_t *face) { return static_cast<uint32_t>(orig(face)); });
        auto b = Run(seed, [&](uint8_t *face) {
            return static_cast<uint32_t>(reinterpret_cast<cGZPersistResourceManager *>(face)->Shutdown());
        });
        ++ctx.cases;
        if (a.result != b.result) ctx.Fail("Shutdown seed %u: result original %u, ours %u", seed, a.result, b.result);
        if (a.log != b.log) ctx.Fail("Shutdown seed %u: calls or frees differ (%zu / %zu values)", seed, a.log.size(), b.log.size());
        if (a.dump != b.dump) ctx.Fail("Shutdown seed %u: manager state differs", seed);
    }
}
