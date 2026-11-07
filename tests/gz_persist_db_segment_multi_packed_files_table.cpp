// Differential tests for the cGZPersistDBSegmentMultiPackedFiles slots that work on the item table and the item
// vector (field map in src/gz/cGZPersistDBSegmentMultiPackedFiles.h): the record forwarders (slots 35, 36, 37,
// 39, 40, 42), Close (slot 9), the constructor and the deleting destructor (slot 33), the table's Clear and
// Destroy, and the subobject's slots 0 to 3, 5, 6 and 10.
//
// Each run gets a fresh object built from the same seed: a real hash table (head node, buckets, nodes of 0x1c
// bytes allocated on the heap, as the original frees them), a vector of item pointers, and fake items whose
// vtables record their calls. Three runs are compared on the return value, the recorded calls (kind, object, the
// normalised arguments), the final state of the object, the table and the vector, and which heap blocks were
// freed:
//   original : the original code; the subobject's slots 7 and 8 are the original's
//   direct   : our function called directly; the subobject's slots 7 and 8 are ours (where the function calls them)
//   via slot : our function is also written into the vtable slot it is reached through, and called through it
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using QiFn = bool(__thiscall *)(void *, uint32_t, void **);
using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using U32Fn = uint32_t(__thiscall *)(void *);
using U32ArgFn = uint32_t(__thiscall *)(void *, uint32_t);
using BoolPtrFn = bool(__thiscall *)(void *, const void *);
using U32PtrFn = uint32_t(__thiscall *)(void *, const void *);
using BoolPtr3Fn = bool(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using U32Ptr3Fn = uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using PtrPtrFn = void *(__thiscall *)(void *, void *);
using U32U32Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using BoolKeyOutFn = bool(__thiscall *)(void *, const uint32_t *, void **);
using VoidKeyFn = void(__thiscall *)(void *, const uint32_t *, uint32_t);
using CtorFn = void *(__thiscall *)(void *);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

constexpr size_t kItems = 4;
constexpr size_t kMaxNodes = 8;
constexpr size_t kMaxBlocks = 40;
constexpr uint32_t kSelfSize = 0x94;
constexpr uint32_t kNodeSize = 0x1c;

// Recorded events: kind, then the normalised arguments. Addresses differ between the runs, so every pointer
// that names one of the run's objects is replaced by its id.
std::vector<uint32_t> gTrace;
std::vector<std::pair<uint32_t, uint32_t>> gIds; // address, id
uint32_t gRet = 0;                               // what the recorders return
bool gQiOk = false;                              // whether the items' QueryInterface succeeds
void *gPacked = nullptr;                         // the object their QueryInterface hands out
const void *gKeyPtr = nullptr;                   // what the record object's slot 3 returns

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

template <typename T> void *MemberFn(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

uint32_t Id(uint32_t value)
{
    for (const auto &e : gIds)
        if (e.first == value) return e.second;
    return value;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(Id(Addr(self)));
}

void Arg(uint32_t value)
{
    gTrace.push_back(Id(value));
}

uint32_t Load32(const void *base, size_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

// Recorders. __fastcall takes ecx and edx as a __thiscall callee does, and pops its stack arguments.
uint32_t __fastcall ItemAddRef(void *self, void *)
{
    Note(1, self);
    return gRet;
}
uint32_t __fastcall ItemRelease(void *self, void *)
{
    Note(2, self);
    return gRet;
}
bool __fastcall ItemQueryInterface(void *self, void *, uint32_t riid, void **ppv)
{
    Note(100, self);
    Arg(riid);
    if (gQiOk) *ppv = gPacked;
    return gQiOk;
}
void __fastcall ItemShutdown(void *self, void *)
{
    Note(4, self);
}
void __fastcall ItemClose(void *self, void *)
{
    Note(9, self);
}
uint32_t __fastcall ItemSum(void *self, void *, uint32_t a, uint32_t b)
{
    Note(19, self);
    Arg(a);
    Arg(b);
    return Load32(self, 4) + gRet;
}
uint32_t __fastcall ItemTest(void *self, void *, const void *key)
{
    Note(20, self);
    Arg(Addr(key));
    return gRet;
}
uint32_t __fastcall ItemSize(void *self, void *, const void *key)
{
    Note(21, self);
    Arg(Addr(key));
    return gRet;
}
uint32_t __fastcall ItemOpen(void *self, void *, const void *key, uint32_t a, uint32_t b)
{
    Note(22, self);
    Arg(Addr(key));
    Arg(a);
    Arg(b);
    return gRet;
}
uint32_t __fastcall ItemCloseRec(void *self, void *, const void *rec)
{
    Note(24, self);
    Arg(Addr(rec));
    return gRet;
}
uint32_t __fastcall ItemAbortRec(void *self, void *, const void *rec)
{
    Note(25, self);
    Arg(Addr(rec));
    return gRet;
}
uint32_t __fastcall ItemRead(void *self, void *, const void *key, uint32_t a, uint32_t b)
{
    Note(27, self);
    Arg(Addr(key));
    Arg(a);
    Arg(b);
    return gRet;
}
uint32_t __fastcall PackedOp35(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    Note(135, self);
    Arg(a);
    Arg(b);
    Arg(c);
    Arg(d);
    return gRet ^ a;
}
uint32_t __fastcall PackedRelease(void *self, void *)
{
    Note(102, self);
    return gRet;
}
const void *__fastcall RecordGetKey(void *self, void *)
{
    Note(103, self);
    return gKeyPtr;
}
uint32_t __fastcall LockLock(void *self, void *)
{
    Note(201, self);
    return 1;
}
uint32_t __fastcall LockUnlock(void *self, void *)
{
    Note(202, self);
    return 0;
}
uint32_t __fastcall ObjRelease(void *self, void *)
{
    Note(5, self);
    return 0;
}
void *__fastcall StringAssign(void *self, void *, void *arg)
{
    Note(16, self);
    Arg(Addr(arg));
    return reinterpret_cast<void *>(static_cast<uintptr_t>(gRet));
}

struct Block {
    void *p = nullptr;
    uint32_t kind = 0; // 0: list node (0x1c bytes), 1: array of `size` pointers, 2: the object (0x94 bytes)
    uint32_t size = 0;
    bool alive = true;
    // The heap block HeapFree gets: p, or for an array of 4 KB or more (MSVC's aligned big-block scheme) the
    // address stored just below p. (HeapValidate on such a p, used before, was always FALSE: "freed".)
    void *heapBlock = nullptr;
};

struct World {
    uint8_t *self = nullptr;
    alignas(8) uint8_t out[0x20];
    alignas(8) uint8_t cache[0x20];
    alignas(8) uint8_t ns[0x20];
    alignas(8) uint8_t record[0x10];
    alignas(8) uint8_t packed[0x10];
    alignas(8) uint8_t keyArg[0x10];
    alignas(8) uint8_t outCell[8];
    alignas(8) uint8_t items[kItems][0x10];
    void *selfVT[50];
    void *subVT[11];
    void *lockVT[3];
    void *itemVT[40];
    void *packedVT[40];
    void *recordVT[8];
    void *objVT[3];
    void *strVT[34];
    StdHashNode *head = nullptr;
    void **buckets = nullptr;
    void **itemArray = nullptr;
    uint32_t tableNodes = 0;
    StdHashNode *nodes[kMaxNodes] = {};
    Block blocks[kMaxBlocks];
    size_t blockCount = 0;
    uint32_t longString[2] = {0, 0}; // string buffers (pointers), mapped to ids when the strings are real
    uint32_t lockBlock = 0;          // the real lock's block pointer, mapped to an id
    uint32_t itemCap = 0;            // elements allocated for the item vector / the bucket array
    uint32_t bucketCap = 0;

    World() = default;
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    void Track(void *p, uint32_t kind, uint32_t size)
    {
        blocks[blockCount].p = p;
        blocks[blockCount].kind = kind;
        blocks[blockCount].size = size;
        blocks[blockCount].alive = true;
        blocks[blockCount].heapBlock = kind == 1 && size * sizeof(void *) >= 4096 ? static_cast<void **>(p)[-1] : p;
        ++blockCount;
    }

    // Marks which tracked blocks the call freed, from a FreeRecorder that ran across it.
    void Observe(const difftest::FreeRecorder &rec)
    {
        for (size_t i = 0; i < blockCount; ++i) blocks[i].alive = !rec.Freed(blocks[i].heapBlock);
    }

    ~World()
    {
        for (size_t i = 0; i < blockCount; ++i) {
            if (!blocks[i].alive) continue;
            switch (blocks[i].kind) {
            case 0: ::operator delete(blocks[i].p, kNodeSize); break;
            case 1: std::allocator<void *>().deallocate(static_cast<void **>(blocks[i].p), blocks[i].size); break;
            default: ::operator delete(blocks[i].p, kSelfSize); break;
            }
        }
    }
};

// How a world is set up: which slots of which table hold our functions, and the sizes.
struct Options {
    bool ourSub = false;       // our ItemCount and ItemAt in the subobject's table (slots 7 and 8)
    int primarySlot = -1;      // our function in this primary slot ...
    void *primaryFn = nullptr; // ... and its address
    int subSlot = -1;          // our function in this subobject slot ...
    void *subFn = nullptr;     // ... and its address
    int items = -1;            // -1: random count of items; otherwise this many
    int nodes = -1;            // -1: random count of table nodes
    bool bigItems = false;     // an item vector of at least 0x1000 bytes
    bool bigTable = false;     // a bucket array of at least 0x1000 bytes
    bool realLock = false;     // a real cRZCriticalSection at +8 (the destructor destroys it)
    bool realStrings = false;  // real cRZStrings at +0x24 and +0x44 (the destructor destroys them)
};

void Build(World &w, const difftest::Context &ctx, uint32_t seed, const Options &o)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    w.self = static_cast<uint8_t *>(::operator new(kSelfSize));
    w.Track(w.self, 2, kSelfSize);
    fill(w.self, kSelfSize);
    fill(w.out, sizeof(w.out));
    fill(w.cache, sizeof(w.cache));
    fill(w.ns, sizeof(w.ns));
    fill(w.record, sizeof(w.record));
    fill(w.packed, sizeof(w.packed));
    fill(w.keyArg, sizeof(w.keyArg));
    fill(w.outCell, sizeof(w.outCell));
    fill(w.items, sizeof(w.items));

    std::memcpy(w.selfVT, ctx.image.At(cGZPersistDBSegmentMultiPackedFiles::kVTable), sizeof(w.selfVT));
    std::memcpy(w.subVT, ctx.image.At(cGZPersistDBSegmentMultiPackedFiles::kSubVTable), sizeof(w.subVT));
    if (o.primarySlot >= 0) w.selfVT[o.primarySlot] = o.primaryFn;
    if (o.ourSub) {
        w.subVT[7] = MemberFn(&MultiPackedFilesSegmentView::ItemCount);
        w.subVT[8] = MemberFn(&MultiPackedFilesSegmentView::ItemAt);
    }
    if (o.subSlot >= 0) w.subVT[o.subSlot] = o.subFn;

    std::memset(w.lockVT, 0, sizeof(w.lockVT));
    std::memset(w.itemVT, 0, sizeof(w.itemVT));
    std::memset(w.packedVT, 0, sizeof(w.packedVT));
    std::memset(w.recordVT, 0, sizeof(w.recordVT));
    std::memset(w.objVT, 0, sizeof(w.objVT));
    std::memset(w.strVT, 0, sizeof(w.strVT));
    w.lockVT[1] = reinterpret_cast<void *>(&LockLock);
    w.lockVT[2] = reinterpret_cast<void *>(&LockUnlock);
    w.itemVT[0] = reinterpret_cast<void *>(&ItemQueryInterface);
    w.itemVT[1] = reinterpret_cast<void *>(&ItemAddRef);
    w.itemVT[2] = reinterpret_cast<void *>(&ItemRelease);
    w.itemVT[4] = reinterpret_cast<void *>(&ItemShutdown);
    w.itemVT[9] = reinterpret_cast<void *>(&ItemClose);
    w.itemVT[19] = reinterpret_cast<void *>(&ItemSum);
    w.itemVT[20] = reinterpret_cast<void *>(&ItemTest);
    w.itemVT[21] = reinterpret_cast<void *>(&ItemSize);
    w.itemVT[22] = reinterpret_cast<void *>(&ItemOpen);
    w.itemVT[24] = reinterpret_cast<void *>(&ItemCloseRec);
    w.itemVT[25] = reinterpret_cast<void *>(&ItemAbortRec);
    w.itemVT[27] = reinterpret_cast<void *>(&ItemRead);
    w.packedVT[2] = reinterpret_cast<void *>(&PackedRelease);
    w.packedVT[35] = reinterpret_cast<void *>(&PackedOp35);
    w.recordVT[3] = reinterpret_cast<void *>(&RecordGetKey);
    w.objVT[2] = reinterpret_cast<void *>(&ObjRelease);
    w.strVT[16] = reinterpret_cast<void *>(&StringAssign);

    // The base (cGZPersistDBSegment) at +0: vptr, count, lock, cache, namespace.
    static constexpr int32_t kCounts[] = {1, 1, 2, 3, 0, -5};
    Put(w.self, 0x00, Addr(w.selfVT));
    Put(w.self, 0x04, kCounts[rng() % 6]);
    Put(w.self, 0x08, Addr(w.lockVT));
    Put(w.self, 0x10, rng() % 3 ? Addr(w.cache) : 0u);
    Put(w.self, 0x14, rng() % 3 ? Addr(w.ns) : 0u);
    Put(w.cache, 0x00, Addr(w.objVT));
    Put(w.ns, 0x00, Addr(w.objVT));
    Put(w.self, 0x18, Addr(w.subVT));
    Put(w.self, 0x24, Addr(w.strVT));
    Put(w.self, 0x44, Addr(w.strVT));
    static constexpr uint8_t kOpen[] = {0, 1, 1, 0x80};
    Put(w.self, 0x64, kOpen[rng() % 4]);
    Put(w.self, 0x1c, static_cast<uint8_t>(rng()));
    Put(w.self, 0x20, static_cast<uint32_t>(rng()));
    Put(w.record, 0, Addr(w.recordVT));
    Put(w.packed, 0, Addr(w.packedVT));
    for (size_t i = 0; i < kItems; ++i) Put(w.items[i], 0, Addr(w.itemVT));

    if (o.realLock) {
        reinterpret_cast<cRZCriticalSection *>(w.self + 8)->Construct();
        w.lockBlock = Load32(w.self, 0xc);
    }
    if (o.realStrings) {
        static constexpr size_t kLengths[] = {0, 5, 15, 16, 40};
        for (int s = 0; s < 2; ++s) {
            auto *str = reinterpret_cast<cRZString *>(w.self + (s == 0 ? 0x24 : 0x44));
            std::string text(kLengths[rng() % 5], static_cast<char>('a' + s));
            // Construct from the text through the cRZString constructor, which writes the game's vtable.
            str->Construct(text.c_str());
            w.longString[s] = text.size() > 15 ? Load32(w.self, (s == 0 ? 0x28 : 0x48)) : 0;
        }
    }

    // The item vector.
    uint32_t n = o.items >= 0 ? static_cast<uint32_t>(o.items) : static_cast<uint32_t>(rng() % (kItems + 1));
    uint32_t cap = n + static_cast<uint32_t>(rng() % 4);
    if (o.bigItems) cap = 1100;
    if (cap == 0 && rng() % 2) {
        Put(w.self, 0x68, 0u);
        Put(w.self, 0x6c, 0u);
        Put(w.self, 0x70, 0u);
    } else {
        if (cap == 0) cap = 1;
        w.itemArray = std::allocator<void *>().allocate(cap);
        w.Track(w.itemArray, 1, cap);
        w.itemCap = cap;
        fill(w.itemArray, cap * sizeof(void *));
        for (uint32_t i = 0; i < n; ++i) w.itemArray[i] = w.items[i % kItems];
        Put(w.self, 0x68, Addr(w.itemArray));
        Put(w.self, 0x6c, Addr(w.itemArray + n));
        Put(w.self, 0x70, Addr(w.itemArray + cap));
    }

    // The table: head node, buckets (two pointers each), nodes sorted by bucket.
    uint32_t bucketCount = o.bigTable ? 512 : (rng() % 2 ? 4 : 8);
    uint32_t mask = bucketCount - 1;
    uint32_t nodeCount = o.nodes >= 0 ? static_cast<uint32_t>(o.nodes) : static_cast<uint32_t>(rng() % (kMaxNodes + 1));
    uint32_t arrayCap = bucketCount * 2 + (rng() % 2 ? 2 : 0);
    w.head = static_cast<StdHashNode *>(::operator new(kNodeSize));
    w.Track(w.head, 0, 0);
    fill(w.head, kNodeSize);
    w.buckets = std::allocator<void *>().allocate(arrayCap);
    w.Track(w.buckets, 1, arrayCap);
    w.bucketCap = arrayCap;
    fill(w.buckets, arrayCap * sizeof(void *));
    w.tableNodes = nodeCount;
    for (uint32_t i = 0; i < nodeCount; ++i) {
        auto *node = static_cast<StdHashNode *>(::operator new(kNodeSize));
        w.Track(node, 0, 0);
        fill(node, kNodeSize);
        uint32_t key[4] = {static_cast<uint32_t>(rng() % 3), static_cast<uint32_t>(rng() % 3),
                           static_cast<uint32_t>(rng() % (o.bigTable ? 600 : 16)), static_cast<uint32_t>(rng() % 3)};
        std::memcpy(reinterpret_cast<uint8_t *>(node) + 8, key, sizeof(key));
        Put(reinterpret_cast<uint8_t *>(node), 0x18, Addr(w.items[rng() % kItems]));
        w.nodes[i] = node;
    }
    std::vector<StdHashNode *> order(w.nodes, w.nodes + nodeCount);
    std::stable_sort(order.begin(), order.end(), [mask](StdHashNode *a, StdHashNode *b) {
        return (Load32(a, 0x10) & mask) < (Load32(b, 0x10) & mask);
    });
    StdHashNode *prev = w.head;
    for (StdHashNode *node : order) {
        prev->next = node;
        node->prev = prev;
        prev = node;
    }
    prev->next = w.head;
    w.head->prev = prev;
    for (uint32_t b = 0; b < bucketCount; ++b) {
        StdHashNode *first = w.head;
        StdHashNode *last = w.head;
        bool any = false;
        for (StdHashNode *node : order) {
            if ((Load32(node, 0x10) & mask) != b) continue;
            if (!any) first = node;
            last = node;
            any = true;
        }
        w.buckets[b * 2] = first;
        w.buckets[b * 2 + 1] = last;
    }
    float loadFactor = 1.0f;
    uint32_t loadBits;
    std::memcpy(&loadBits, &loadFactor, sizeof(loadBits));
    Put(w.self, 0x74, loadBits);
    Put(w.self, 0x78, Addr(w.head));
    Put(w.self, 0x7c, nodeCount);
    Put(w.self, 0x80, Addr(w.buckets));
    Put(w.self, 0x84, Addr(w.buckets + bucketCount * 2));
    Put(w.self, 0x88, Addr(w.buckets + arrayCap));
    Put(w.self, 0x8c, mask);
    Put(w.self, 0x90, 8u);

    // The search key: usually a copy of a node's key (so the lookup hits), sometimes one that is not there.
    if (nodeCount > 0 && rng() % 4 != 0) {
        std::memcpy(w.keyArg, reinterpret_cast<uint8_t *>(w.nodes[rng() % nodeCount]) + 8, 16);
    } else {
        uint32_t key[4] = {static_cast<uint32_t>(rng() % 4), static_cast<uint32_t>(rng() % 3),
                           static_cast<uint32_t>(rng() % (o.bigTable ? 600 : 16)), static_cast<uint32_t>(rng() % 3)};
        std::memcpy(w.keyArg, key, 16);
    }
}

struct Dump {
    bool selfFreed = false;   // do not read the object
    bool tableDead = false;   // do not read the table, the buckets, the nodes
    bool itemsDead = false;   // do not read the item vector
};

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
};

// Adds a world's addresses to the id map. Every world of a comparison is added (by Compare) before any runs, each
// role getting the same id in every world, so Id is one function for all the sides: a word that equals some
// world's address is renamed on every side, never on one only. The worlds hold heap blocks (the object, nodes,
// arrays), which a rebuild would put at new addresses, so one world rebuilt in place cannot give that.
void AddIds(const World &w)
{
    gIds.insert(gIds.end(), {{Addr(w.self), 0x100},        {Addr(w.self + 0x18), 0x118},   {Addr(w.self + 8), 0x108},
            {Addr(w.self + 0x24), 0x124}, {Addr(w.self + 0x44), 0x144},   {Addr(w.self + 0x74), 0x174},
            {Addr(w.ns), 0x101},          {Addr(w.cache), 0x102},         {Addr(w.out), 0x104},
            {Addr(w.record), 0x105},      {Addr(w.packed), 0x106},        {Addr(w.keyArg), 0x107},
            {Addr(w.outCell), 0x109},     {Addr(w.selfVT), 0x110},        {Addr(w.subVT), 0x111},
            {Addr(w.lockVT), 0x113},      {Addr(w.itemVT), 0x116},        {Addr(w.packedVT), 0x117},
            {Addr(w.recordVT), 0x119},    {Addr(w.objVT), 0x11a},         {Addr(w.strVT), 0x11b},
            {Addr(w.head), 0x1ff},        {Addr(w.buckets), 0x1f0},       {Addr(w.itemArray), 0x140}});
    for (size_t i = 0; i < kItems; ++i) gIds.push_back({Addr(w.items[i]), 0x130 + static_cast<uint32_t>(i)});
    for (uint32_t i = 0; i < w.tableNodes; ++i) gIds.push_back({Addr(w.nodes[i]), 0x200 + i});
    // Pointers into the arrays (the vector's end, the buckets' end).
    for (uint32_t k = 0; k <= w.itemCap && w.itemArray; ++k) gIds.push_back({Addr(w.itemArray + k), 0x1000 + k});
    for (uint32_t k = 0; k <= w.bucketCap; ++k) gIds.push_back({Addr(w.buckets + k), 0x4000 + k});
    if (w.longString[0]) gIds.push_back({w.longString[0], 0x180});
    if (w.longString[1]) gIds.push_back({w.longString[1], 0x181});
    if (w.lockBlock) gIds.push_back({w.lockBlock, 0x10c});
}

// The object's words, the table, the vector, the fakes, normalised (what d says is still there).
void Snapshot(const World &w, const Dump &d, std::vector<uint32_t> &t)
{
    if (!d.selfFreed) {
        for (size_t i = 0; i < kSelfSize; i += 4) {
            uint32_t v = Load32(w.self, i);
            // A destroyed long string keeps its buffer pointer with the low byte zeroed.
            if (i == 0x28 && w.longString[0] && v == (w.longString[0] & ~0xffu)) v = 0x190;
            else if (i == 0x48 && w.longString[1] && v == (w.longString[1] & ~0xffu)) v = 0x190;
            else v = Id(v);
            t.push_back(v);
        }
    }
    if (!d.tableDead) {
        // Walk the list from the head (at most a few more than the nodes there ever were).
        StdHashNode *node = w.head;
        for (int guard = 0; guard < 24; ++guard) {
            t.push_back(Id(Addr(node->next)));
            t.push_back(Id(Addr(node->prev)));
            for (int k = 2; k < 7; ++k) t.push_back(Id(Load32(node, k * 4)));
            node = node->next;
            if (node == w.head) break;
        }
        uint32_t cap = (Load32(w.self, 0x88) - Load32(w.self, 0x80)) / 4;
        for (uint32_t i = 0; i < cap && i < 1100; ++i) t.push_back(Id(Addr(w.buckets[i])));
    }
    if (!d.itemsDead && w.itemArray) {
        uint32_t cap = (Load32(w.self, 0x70) - Load32(w.self, 0x68)) / 4;
        // The vector's contents: when the call emptied it the cap stays, so use the allocation's own size.
        cap = w.itemCap;
        for (uint32_t i = 0; i < cap && i < 1100; ++i) t.push_back(Id(Addr(w.itemArray[i])));
    }
    for (size_t i = 0; i < sizeof(w.outCell); i += 4) t.push_back(Id(Load32(w.outCell, i)));
    for (size_t i = 0; i < sizeof(w.items); i += 4) t.push_back(Id(Load32(w.items, i)));
}

// The normalised state after the call, preceded by which tracked blocks the call freed.
template <typename Call> Outcome Run(World &w, const Dump &d, Call call)
{
    gTrace.clear();
    gTrace.reserve(8192);
    gPacked = w.packed;
    gKeyPtr = w.keyArg;

    Outcome out;
    difftest::FreeRecorder rec;
    out.ret = call(w);
    rec.Stop();
    w.Observe(rec);
    for (size_t i = 0; i < w.blockCount; ++i) gTrace.push_back(w.blocks[i].alive ? 1u : 0u);
    Snapshot(w, d, gTrace);
    out.trace = gTrace;
    return out;
}

std::string FirstDifference(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "first difference at word %zu: original 0x%x, ours 0x%x", i, a[i], b[i]);
            return buf;
        }
    }
    return "same words, different lengths";
}

template <typename OrigCall, typename OursCall, typename ViaCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, const Options &plain, const Options &ours,
             const Options &via, const Dump &d, OrigCall origCall, OursCall oursCall, ViaCall viaCall)
{
    ++ctx.cases;
    // The first words of a trace say which tracked heap blocks the call freed (a FreeRecorder across the call:
    // exact, so a case is run once; HeapValidate, used before, could see a freed block reused by another thread
    // and needed retries).
    std::string failure;
    for (int attempt = 0; attempt < 1; ++attempt) {
        failure.clear();
        bool onlyFlags = true;
        World a, b, c;
        Build(a, ctx, seed, plain);
        Build(b, ctx, seed, ours);
        Build(c, ctx, seed, via);
        gIds.clear();
        AddIds(a);
        AddIds(b);
        AddIds(c);
        Outcome oa = Run(a, d, origCall);
        Outcome ob = Run(b, d, oursCall);
        Outcome oc = Run(c, d, viaCall);
        const size_t flags = a.blockCount;
        auto check = [&](const char *label, const Outcome &x) {
            if (oa.ret == x.ret && oa.trace == x.trace) return;
            bool same = oa.ret == x.ret && oa.trace.size() == x.trace.size();
            for (size_t i = flags; same && i < oa.trace.size(); ++i) same = oa.trace[i] == x.trace[i];
            if (!same) onlyFlags = false;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s seed %u: %s: original returned 0x%x (%zu words), ours 0x%x (%zu); ", name,
                          seed, label, oa.ret, oa.trace.size(), x.ret, x.trace.size());
            failure += buf + FirstDifference(oa.trace, x.trace);
        };
        check("direct", ob);
        check("via slot", oc);
        if (failure.empty() || !onlyFlags) break;
    }
    if (!failure.empty()) ctx.Fail("%s", failure.c_str());
}

cGZPersistDBSegmentMultiPackedFiles *Self(World &w)
{
    return reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(w.self);
}

MultiPackedFilesSegmentView *View(World &w)
{
    return reinterpret_cast<MultiPackedFilesSegmentView *>(w.self + 0x18);
}

Options Primary(int slot, void *fn)
{
    Options o;
    o.primarySlot = slot;
    o.primaryFn = fn;
    return o;
}

Options Sub(int slot, void *fn)
{
    Options o;
    o.subSlot = slot;
    o.subFn = fn;
    return o;
}

} // namespace

// The record forwarders share their shape: with the open flag set and the key in the table, the matching item's
// slot is called with the same arguments; otherwise the answer is 0.
DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_TestForRecord, 0x9b9570)
{
    std::mt19937 rng(0x351);
    auto orig = ctx.Original<BoolPtrFn>(0x9b9570);
    Options plain;
    Options via = Primary(35, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::TestForRecordImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "TestForRecord", seed, plain, plain, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self, w.keyArg)); },
            [&](World &w) { return uint32_t(Self(w)->TestForRecordImpl(reinterpret_cast<const uint32_t *>(w.keyArg))); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 35)(w.self, w.keyArg)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_GetRecordSize, 0x9b9410)
{
    std::mt19937 rng(0x362);
    auto orig = ctx.Original<U32PtrFn>(0x9b9410);
    Options plain;
    Options via = Primary(36, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::GetRecordSizeImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "GetRecordSize", seed, plain, plain, via, Dump{},
            [&](World &w) { return orig(w.self, w.keyArg); },
            [&](World &w) { return Self(w)->GetRecordSizeImpl(reinterpret_cast<const uint32_t *>(w.keyArg)); },
            [&](World &w) { return VirtualSlot<U32PtrFn>(w.self, 36)(w.self, w.keyArg); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_OpenRecord, 0x9b94b0)
{
    std::mt19937 rng(0x373);
    auto orig = ctx.Original<BoolPtr3Fn>(0x9b94b0);
    Options plain;
    Options via = Primary(37, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::OpenRecordImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng();
        Compare(
            ctx, "OpenRecord", seed, plain, plain, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self, w.keyArg, a, b)); },
            [&](World &w) { return uint32_t(Self(w)->OpenRecordImpl(reinterpret_cast<const uint32_t *>(w.keyArg), a, b)); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolPtr3Fn>(w.self, 37)(w.self, w.keyArg, a, b)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_CloseRecord, 0x9b93a0)
{
    std::mt19937 rng(0x394);
    auto orig = ctx.Original<BoolPtrFn>(0x9b93a0);
    Options plain;
    Options via = Primary(39, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::CloseRecordImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "CloseRecord", seed, plain, plain, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self, w.record)); },
            [&](World &w) { return uint32_t(Self(w)->CloseRecordImpl(w.record)); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 39)(w.self, w.record)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_AbortRecord, 0x9b9340)
{
    std::mt19937 rng(0x405);
    auto orig = ctx.Original<BoolPtrFn>(0x9b9340);
    Options plain;
    Options via = Primary(40, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::AbortRecordImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "AbortRecord", seed, plain, plain, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self, w.record)); },
            [&](World &w) { return uint32_t(Self(w)->AbortRecordImpl(w.record)); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolPtrFn>(w.self, 40)(w.self, w.record)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_ReadRecord, 0x9b9510)
{
    std::mt19937 rng(0x426);
    auto orig = ctx.Original<U32Ptr3Fn>(0x9b9510);
    Options plain;
    Options via = Primary(42, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::ReadRecordImpl));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t a = rng(), b = rng();
        Compare(
            ctx, "ReadRecord", seed, plain, plain, via, Dump{},
            [&](World &w) { return orig(w.self, w.keyArg, a, b); },
            [&](World &w) { return Self(w)->ReadRecordImpl(reinterpret_cast<const uint32_t *>(w.keyArg), a, b); },
            [&](World &w) { return VirtualSlot<U32Ptr3Fn>(w.self, 42)(w.self, w.keyArg, a, b); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_GetItem, 0x9b95c0)
{
    std::mt19937 rng(0x6b);
    auto orig = ctx.Original<BoolKeyOutFn>(0x9b95c0);
    Options plain;
    Options via = Sub(6, MemberFn(&MultiPackedFilesSegmentView::GetItem));
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "GetItem", seed, plain, plain, via, Dump{},
            [&](World &w) {
                return uint32_t(orig(w.self + 0x18, reinterpret_cast<const uint32_t *>(w.keyArg),
                                     reinterpret_cast<void **>(w.outCell)));
            },
            [&](World &w) {
                return uint32_t(View(w)->GetItem(reinterpret_cast<const uint32_t *>(w.keyArg),
                                                 reinterpret_cast<void **>(w.outCell)));
            },
            [&](World &w) {
                return uint32_t(VirtualSlot<BoolKeyOutFn>(w.self + 0x18, 6)(
                    w.self + 0x18, reinterpret_cast<const uint32_t *>(w.keyArg), reinterpret_cast<void **>(w.outCell)));
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_RemoveItem, 0x9b99a0)
{
    std::mt19937 rng(0xa10);
    auto orig = ctx.Original<VoidKeyFn>(0x9b99a0);
    Options plain;
    Options via = Sub(10, MemberFn(&MultiPackedFilesSegmentView::RemoveItem));
    for (int i = 0; i < 4000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t unused = rng();
        // The original's result register is not defined on any path (void): only the effects are compared.
        Compare(
            ctx, "RemoveItem", seed, plain, plain, via, Dump{},
            [&](World &w) { orig(w.self + 0x18, reinterpret_cast<const uint32_t *>(w.keyArg), unused); return 0u; },
            [&](World &w) { View(w)->RemoveItem(reinterpret_cast<const uint32_t *>(w.keyArg), unused); return 0u; },
            [&](World &w) {
                VirtualSlot<VoidKeyFn>(w.self + 0x18, 10)(w.self + 0x18, reinterpret_cast<const uint32_t *>(w.keyArg), unused);
                return 0u;
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_Close, 0x9b91b0)
{
    std::mt19937 rng(0xc1);
    auto orig = ctx.Original<BoolFn>(0x9b91b0);
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        // The original reaches the count through the subobject's slot 7: ours is installed there in the other runs.
        Options plain;
        Options ours;
        ours.ourSub = true;
        Options via = Primary(9, MemberFn(&cGZPersistDBSegmentMultiPackedFiles::Close));
        via.ourSub = true;
        if (i % 50 == 0) {
            plain.bigItems = ours.bigItems = via.bigItems = true;
            plain.bigTable = ours.bigTable = via.bigTable = true;
        }
        Compare(
            ctx, "Close", seed, plain, ours, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self)); },
            [&](World &w) { return uint32_t(Self(w)->Close()); },
            [&](World &w) { return uint32_t(VirtualSlot<BoolFn>(w.self, 9)(w.self)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_TableClear, 0x1fc2f0)
{
    std::mt19937 rng(0x1c);
    auto orig = ctx.Original<VoidFn>(0x1fc2f0);
    Options plain;
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        Options o = plain;
        o.bigTable = (i % 40 == 0);
        Compare(
            ctx, "TableClear", seed, o, o, o, Dump{},
            [&](World &w) { orig(w.self + 0x74); return 0u; },
            [&](World &w) { Self(w)->mTable.Clear(); return 0u; },
            [&](World &w) { Self(w)->mTable.Clear(); return 0u; });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_DeletingDestructor, 0x9b8ef0)
{
    std::mt19937 rng(0xd7);
    auto orig = ctx.Original<DtorFn>(0x9b8ef0);
    for (int i = 0; i < 1500; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        uint32_t flags = rng() % 4; // bit 0 frees the object
        Options plain;
        plain.realLock = true;
        plain.realStrings = true;
        plain.bigItems = (i % 30 == 0);
        plain.bigTable = (i % 30 == 15);
        Options via = plain;
        via.primarySlot = 33;
        via.primaryFn = MemberFn(&cGZPersistDBSegmentMultiPackedFiles::DeletingDestructor);
        Dump d;
        d.tableDead = true;
        d.itemsDead = true;
        d.selfFreed = (flags & 1) != 0;
        Compare(
            ctx, "DeletingDestructor", seed, plain, plain, via, d,
            [&](World &w) { return uint32_t(Addr(orig(w.self, flags)) == Addr(w.self)); },
            [&](World &w) { return uint32_t(Addr(Self(w)->DeletingDestructor(flags)) == Addr(w.self)); },
            [&](World &w) { return uint32_t(Addr(VirtualSlot<DtorFn>(w.self, 33)(w.self, flags)) == Addr(w.self)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_Construct, 0x9b8df0)
{
    std::mt19937 rng(0xc0);
    auto orig = ctx.Original<CtorFn>(0x9b8df0);
    HANDLE heap = GetProcessHeap();
    for (int i = 0; i < 300; ++i) {
        uint32_t seed = rng();
        auto make = [&]() {
            std::mt19937 fill(seed);
            auto *mem = static_cast<uint8_t *>(::operator new(kSelfSize));
            for (uint32_t k = 0; k < kSelfSize; ++k) mem[k] = static_cast<uint8_t>(fill());
            return mem;
        };
        // The words of the object with the heap pointers replaced by what can be compared: the lock's block by
        // whether it exists, the buckets' end pointers by their distance from the begin, the head and the buckets
        // by their allocation sizes and the links they hold.
        auto dump = [&](uint8_t *m) {
            std::vector<uint32_t> out;
            uint32_t buckets = Load32(m, 0x80);
            auto *head = reinterpret_cast<StdHashNode *>(static_cast<uintptr_t>(Load32(m, 0x78)));
            for (uint32_t off = 0; off < kSelfSize; off += 4) {
                uint32_t v = Load32(m, off);
                if (off == 0xc) v = v != 0;
                else if (off == 0x78 || off == 0x80) v = 0x1111;
                else if (off == 0x84 || off == 0x88) v -= buckets;
                out.push_back(v);
            }
            out.push_back(static_cast<uint32_t>(HeapSize(heap, 0, head)));
            out.push_back(static_cast<uint32_t>(HeapSize(heap, 0, reinterpret_cast<void *>(static_cast<uintptr_t>(buckets)))));
            out.push_back(head->next == head);
            out.push_back(head->prev == head);
            for (int k = 0; k < 16; ++k)
                out.push_back(reinterpret_cast<StdHashNode **>(static_cast<uintptr_t>(buckets))[k] == head);
            return out;
        };
        uint8_t *a = make();
        uint8_t *b = make();
        void *ra = orig(a);
        void *rb = reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct seed %u: does not return this", seed);
        std::vector<uint32_t> da = dump(a), db = dump(b);
        if (da != db) ctx.Fail("Construct seed %u: %s", seed, FirstDifference(da, db).c_str());
        // Both objects are destroyed by our destructor (the original's is tested above) and freed.
        reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(a)->DeletingDestructor(1);
        reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(b)->DeletingDestructor(1);
    }
}

// The subobject's thunks to the primary object.
DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SubQueryInterface, 0x9b997f)
{
    std::mt19937 rng(0x9f);
    auto orig = ctx.Original<QiFn>(0x9b997f);
    Options plain;
    Options via = Sub(0, MemberFn(&MultiPackedFilesSegmentView::QueryInterface));
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        static constexpr uint32_t kIds[] = {0xe90f85f3, 0x656b8f0c, 1, 2, 0};
        uint32_t riid = rng() % 3 ? kIds[rng() % 5] : rng();
        Compare(
            ctx, "SubQueryInterface", seed, plain, plain, via, Dump{},
            [&](World &w) { return uint32_t(orig(w.self + 0x18, riid, reinterpret_cast<void **>(w.outCell))); },
            [&](World &w) { return uint32_t(View(w)->QueryInterface(riid, reinterpret_cast<void **>(w.outCell))); },
            [&](World &w) {
                return uint32_t(VirtualSlot<QiFn>(w.self + 0x18, 0)(w.self + 0x18, riid, reinterpret_cast<void **>(w.outCell)));
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SubAddRef, 0x9b9012)
{
    std::mt19937 rng(0xa1);
    auto orig = ctx.Original<U32Fn>(0x9b9012);
    Options plain;
    Options via = Sub(1, MemberFn(&MultiPackedFilesSegmentView::AddRef));
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        Compare(
            ctx, "SubAddRef", seed, plain, plain, via, Dump{},
            [&](World &w) { return orig(w.self + 0x18); },
            [&](World &w) { return View(w)->AddRef(); },
            [&](World &w) { return VirtualSlot<U32Fn>(w.self + 0x18, 1)(w.self + 0x18); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SubRelease, 0x9b9995)
{
    std::mt19937 rng(0xe1);
    auto orig = ctx.Original<U32Fn>(0x9b9995);
    Options plain;
    Options via = Sub(2, MemberFn(&MultiPackedFilesSegmentView::Release));
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        // At a count of zero after the decrement the base calls slot 34 (the deleting destructor): a fake that
        // records the call stands in for it.
        Compare(
            ctx, "SubRelease", seed, plain, plain, via, Dump{},
            [&](World &w) { w.selfVT[34] = reinterpret_cast<void *>(&ObjRelease); return orig(w.self + 0x18); },
            [&](World &w) { w.selfVT[34] = reinterpret_cast<void *>(&ObjRelease); return View(w)->Release(); },
            [&](World &w) {
                w.selfVT[34] = reinterpret_cast<void *>(&ObjRelease);
                return VirtualSlot<U32Fn>(w.self + 0x18, 2)(w.self + 0x18);
            });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SetPattern, 0x9b9a40)
{
    std::mt19937 rng(0x5a);
    auto orig = ctx.Original<PtrPtrFn>(0x9b9a40);
    Options plain;
    Options via = Sub(3, MemberFn(&MultiPackedFilesSegmentView::SetPattern));
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        Compare(
            ctx, "SetPattern", seed, plain, plain, via, Dump{},
            [&](World &w) { return Addr(orig(w.self + 0x18, w.out)); },
            [&](World &w) { return Addr(View(w)->SetPattern(w.out)); },
            [&](World &w) { return Addr(VirtualSlot<PtrPtrFn>(w.self + 0x18, 3)(w.self + 0x18, w.out)); });
    }
}

DIFF_TEST(cGZPersistDBSegmentMultiPackedFiles_SumPackedFiles, 0x9b92d0)
{
    std::mt19937 rng(0x55);
    auto orig = ctx.Original<U32U32Fn>(0x9b92d0);
    for (int i = 0; i < 3000; ++i) {
        uint32_t seed = rng();
        gRet = rng();
        gQiOk = rng() % 3 != 0;
        uint32_t a = rng(), b = rng();
        // The count and item come from the subobject's slots 7 and 8: ours in the other two runs.
        Options plain;
        Options ours;
        ours.ourSub = true;
        Options via = Sub(5, MemberFn(&MultiPackedFilesSegmentView::SumPackedFiles));
        via.ourSub = true;
        Compare(
            ctx, "SumPackedFiles", seed, plain, ours, via, Dump{},
            [&](World &w) { return orig(w.self + 0x18, a, b); },
            [&](World &w) { return View(w)->SumPackedFiles(a, b); },
            [&](World &w) { return VirtualSlot<U32U32Fn>(w.self + 0x18, 5)(w.self + 0x18, a, b); });
    }
}

