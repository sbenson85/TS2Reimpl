// Differential tests for the reference-counted deque the person's interaction queues use (src/simantics/RefCountedDeque.h):
// push_back 0x1d49e0, pop_front 0x525380, clear 0x1d48f0, the range error 0x5248f0, and the UI manager setter 0x4529c0.
// The deques are real std::deque<void *> objects on the heap (the functions allocate and free blocks), compared by their logical
// contents and bookkeeping words (the heap addresses differ between the two sides); the elements are fakes whose AddRef and
// Release are logged.
#include "difftest.h"
#include "game/game.h"
#include "simantics/PrimPerson.h"
#include "simantics/RefCountedDeque.h"

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Elem {
    void **vt;
    uint32_t id;
};

std::vector<uint32_t> gLog;

uint32_t __fastcall ElemAddRef(Elem *self, void *)
{
    gLog.push_back(0x100u | self->id);
    return 0;
}
uint32_t __fastcall ElemRelease(Elem *self, void *)
{
    gLog.push_back(0x200u | self->id);
    return 0;
}

void *gElemVTable[3] = {nullptr, reinterpret_cast<void *>(&ElemAddRef), reinterpret_cast<void *>(&ElemRelease)};

constexpr int kPoolSize = 12;
Elem gPool[kPoolSize];

void InitPool()
{
    for (int i = 0; i < kPoolSize; ++i) gPool[i] = {gElemVTable, static_cast<uint32_t>(i + 1)};
}

uint32_t IdOf(void *p)
{
    return p ? static_cast<Elem *>(p)->id : 0;
}

uint32_t Word(const uint8_t *raw, size_t off)
{
    uint32_t v;
    std::memcpy(&v, raw + off, 4);
    return v;
}

// The deque's observable state: [0] has a map, [1] map size, [2] offset, [3] size, [4] blocks present, then every element's id.
void Describe(const uint8_t *raw, std::vector<uint32_t> &out)
{
    const uint32_t mapPtr = Word(raw, 4), mapSize = Word(raw, 8), offset = Word(raw, 0xc), size = Word(raw, 0x10);
    out.push_back(mapPtr != 0);
    out.push_back(mapSize);
    out.push_back(offset);
    out.push_back(size);
    void *const *map = reinterpret_cast<void *const *>(static_cast<uintptr_t>(mapPtr));
    uint32_t blocks = 0;
    for (uint32_t i = 0; mapPtr && i < mapSize; ++i) blocks += map[i] != nullptr;
    out.push_back(blocks);
    for (uint32_t i = 0; i < size; ++i) {
        const uint32_t pos = offset + i;
        void **const block = static_cast<void **>(map[(pos >> 2) & (mapSize - 1)]);
        out.push_back(IdOf(block[pos & 3]));
    }
}

// A deque with a random history: pushes (some null) and pops, so the offset and the map size vary.
struct Subject {
    alignas(8) uint8_t raw[sizeof(std::deque<void *>)];
    std::deque<void *> *q;
    Subject(uint32_t seed, uint32_t minSize)
    {
        static_assert(sizeof(std::deque<void *>) == 0x14, "deque layout");
        q = new (raw) std::deque<void *>();
        std::mt19937 r(seed * 2654435761u + 7);
        const uint32_t pushes = r() % 10 == 0 ? r() % 70 : r() % 14;
        for (uint32_t i = 0; i < pushes; ++i) q->push_back(r() % 6 == 0 ? nullptr : &gPool[r() % kPoolSize]);
        const uint32_t pops = pushes ? r() % pushes : 0;
        for (uint32_t i = 0; i < pops; ++i) q->pop_front();
        while (q->size() < minSize) q->push_back(r() % 6 == 0 ? nullptr : &gPool[r() % kPoolSize]);
    }
    ~Subject() { q->~deque(); }
    Subject(const Subject &) = delete;
};

struct Seen {
    std::vector<uint32_t> before, after; // Describe() of the original's deque before and after the operation
};

// Runs `op(useOriginal, rawDeque, seed)` on two equal deques and compares the AddRef / Release log and the final state.
template <typename Op> bool CompareDeque(uint32_t seed, uint32_t minSize, Op op, std::string &why, Seen &seen)
{
    Subject a(seed, minSize), b(seed, minSize);
    std::vector<uint32_t> da, db, la, lb;
    Describe(a.raw, seen.before);
    gLog.clear();
    op(true, a.raw, seed);
    la = gLog;
    Describe(a.raw, da);
    gLog.clear();
    op(false, b.raw, seed);
    lb = gLog;
    Describe(b.raw, db);
    seen.after = da;
    if (la != lb) why = "AddRef/Release log differs";
    else if (da != db) why = "deque state differs";
    else return true;
    return false;
}

using DequeFn = void(__thiscall *)(void *);
using PushFn = void(__thiscall *)(void *, void *const *);

} // namespace

DIFF_TEST(RefCountedDeque_PushBack, 0x1d49e0)
{
    InitPool();
    auto original = ctx.Original<PushFn>(0x1d49e0);
    uint32_t grownMap = 0, newBlock = 0, nulls = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::string why;
        Seen seen;
        bool pushedNull = false;
        const bool ok = CompareDeque(seed, 0, [&](bool useOriginal, uint8_t *raw, uint32_t s) {
            std::mt19937 r(s ^ 0x5bd1e995u);
            void *holder = r() % 5 == 0 ? nullptr : &gPool[r() % kPoolSize];
            pushedNull = holder == nullptr;
            if (useOriginal) original(raw, &holder);
            else reinterpret_cast<RefCountedDeque *>(raw)->PushBack(&holder);
        }, why, seen);
        ++ctx.cases;
        if (!ok) ctx.Fail("seed %u: %s", seed, why.c_str());
        grownMap += seen.after[1] != seen.before[1];
        newBlock += seen.after[4] != seen.before[4];
        nulls += pushedNull;
    }
    if (grownMap < 50 || newBlock < 200 || nulls < 100) ctx.Fail("coverage: grown map %u new block %u null pushes %u", grownMap, newBlock, nulls);
}

DIFF_TEST(RefCountedDeque_PopFront, 0x525380)
{
    InitPool();
    auto original = ctx.Original<DequeFn>(0x525380);
    uint32_t emptied = 0, nullFront = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::string why;
        Seen seen;
        const bool ok = CompareDeque(seed, 1, [&](bool useOriginal, uint8_t *raw, uint32_t) {
            if (useOriginal) original(raw);
            else reinterpret_cast<RefCountedDeque *>(raw)->PopFront();
        }, why, seen);
        ++ctx.cases;
        if (!ok) ctx.Fail("seed %u: %s", seed, why.c_str());
        emptied += seen.before[3] == 1;
        nullFront += seen.before[5] == 0;
    }
    if (emptied < 100 || nullFront < 100) ctx.Fail("coverage: emptied %u null front %u", emptied, nullFront);
}

DIFF_TEST(RefCountedDeque_Clear, 0x1d48f0)
{
    InitPool();
    auto original = ctx.Original<DequeFn>(0x1d48f0);
    uint32_t big = 0, empty = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::string why;
        Seen seen;
        const bool ok = CompareDeque(seed, 0, [&](bool useOriginal, uint8_t *raw, uint32_t) {
            if (useOriginal) original(raw);
            else reinterpret_cast<RefCountedDeque *>(raw)->Clear();
        }, why, seen);
        ++ctx.cases;
        if (!ok) ctx.Fail("seed %u: %s", seed, why.c_str());
        big += seen.before[3] > 16;
        empty += seen.before[3] == 0;
    }
    if (big < 50 || empty < 100) ctx.Fail("coverage: big %u empty %u", big, empty);
}

DIFF_TEST(Deque_ThrowRangeError, 0x5248f0)
{
    auto original = ctx.Original<void(__cdecl *)()>(0x5248f0);
    std::string whatOriginal = "(none)", whatOurs = "(none)";
    try {
        original();
    } catch (const std::out_of_range &e) {
        whatOriginal = e.what();
    } catch (...) {
        whatOriginal = "(other)";
    }
    try {
        ThrowDequeRangeError();
    } catch (const std::out_of_range &e) {
        whatOurs = e.what();
    } catch (...) {
        whatOurs = "(other)";
    }
    ++ctx.cases;
    if (whatOriginal != whatOurs) ctx.Fail("original threw '%s', ours '%s'", whatOriginal.c_str(), whatOurs.c_str());
    if (whatOurs != "invalid deque<T> subscript") ctx.Fail("message '%s'", whatOurs.c_str());
}

DIFF_TEST(SetUiManager, 0x4529c0)
{
    auto original = ctx.Original<void(__cdecl *)(void *)>(0x4529c0);
    void **const global = GameAddress<void *>(0x10da968);
    void *const saved = *global;
    std::mt19937 r(5);
    for (int i = 0; i < 200; ++i) {
        void *const value = i == 0 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(r()));
        *global = reinterpret_cast<void *>(0x1234);
        original(value);
        void *const a = *global;
        *global = reinterpret_cast<void *>(0x1234);
        SetUiManager(value);
        void *const b = *global;
        ++ctx.cases;
        if (a != b || b != value) ctx.Fail("value %p: original left %p, ours %p", value, a, b);
    }
    *global = saved;
}
