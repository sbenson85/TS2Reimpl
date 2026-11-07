// Differential test for RefPtrVector::EmplaceReallocate (0x14aac0): the reallocating insert of the game's std::vector<counted
// pointer> over operator new (lane AJ). The elements are fakes whose AddRef and Release are logged (AddRef can be made to throw).
// Allocation is observed exactly: while the test runs, operator new / delete of the image (0x824632, 0x824624) and of our own
// build are redirected to stubs on malloc/free that log every block (size, an id in order of creation, frees) and can fail on
// huge sizes, so both sides' allocations, frees and leaks are compared, not only the final vector.
#include "difftest.h"

#include "simantics/cTSBehaviorTree.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

// ---- the log (a fixed array: the stubs run inside the code under test and must not allocate) -------------------------

uint32_t gLog[1 << 16];
size_t gLogCount = 0;
bool gLogOn = false;
// Other threads (left by earlier tests) allocate while the stubs are in place: only the test's own thread is logged and tracked.
DWORD gOwnerThread = 0;

bool Mine() { return GetCurrentThreadId() == gOwnerThread; }

void Log(uint32_t word)
{
    if (gLogOn && Mine() && gLogCount < sizeof(gLog) / sizeof(gLog[0])) gLog[gLogCount++] = word;
}

// ---- the blocks ----------------------------------------------------------------------------------------------------

struct Block {
    void *pointer;
    size_t size;
    uint32_t id;
    bool live;
};
constexpr size_t kMaxBlocks = 64;
Block gBlocks[kMaxBlocks];
size_t gBlockCount = 0;
uint32_t gNextId = 1;
size_t gFailAbove = 0x40000000; // operator new throws bad_alloc for larger requests
// The tracked blocks come from a private arena, not the process heap: thousands of small allocations and frees there change the heap's
// state (low-fragmentation buckets) for the heap-counting tests that run later.
alignas(16) uint8_t gArena[1 << 18];
size_t gArenaUsed = 0;
bool InArena(const void *p) { return p >= gArena && p < gArena + sizeof(gArena); }
bool gTrack = false;            // blocks are registered only while the vector under test is built and run (the test's own allocations are not)

Block *FindBlock(void *pointer)
{
    for (size_t i = 0; i < gBlockCount; ++i)
        if (gBlocks[i].pointer == pointer && gBlocks[i].live) return &gBlocks[i];
    return nullptr;
}

void *AllocateStub(size_t size)
{
    if (size > gFailAbove) {
        Log(0x30000000u);
        throw std::bad_alloc();
    }
    const bool tracked = gTrack && Mine() && gBlockCount < kMaxBlocks;
    void *pointer = nullptr;
    if (tracked && gArenaUsed + size + 16 <= sizeof(gArena)) {
        pointer = gArena + gArenaUsed;
        gArenaUsed += (size + 15) & ~static_cast<size_t>(15);
        gArenaUsed += 16; // a gap between blocks
    } else {
        pointer = std::malloc(size ? size : 1);
    }
    if (tracked) {
        gBlocks[gBlockCount++] = {pointer, size, gNextId, true};
        Log(0x10000000u | static_cast<uint32_t>(size));
        Log(0x11000000u | gNextId);
        ++gNextId;
    }
    return pointer;
}

void FreeStub(void *pointer)
{
    if (!pointer) return;
    const bool tracking = gTrack && Mine();
    Block *block = tracking ? FindBlock(pointer) : nullptr;
    if (block) {
        Log(0x20000000u | static_cast<uint32_t>(block->size));
        Log(0x21000000u | block->id);
        block->live = false;
    } else if (tracking) {
        Log(0x2fffffffu);
    }
    if (!InArena(pointer)) std::free(pointer);
}

void *__cdecl NewStub(size_t size) { return AllocateStub(size); }
void __cdecl DeleteStub(void *pointer) { FreeStub(pointer); }
void __cdecl SizedDeleteStub(void *pointer, size_t) { FreeStub(pointer); }

// Redirects a function (a jmp over its first five bytes) while it lives.
struct Patch {
    uint8_t *site;
    uint8_t saved[5];
    Patch(void *function, void *stand_in) : site(static_cast<uint8_t *>(function))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand_in) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    Patch(const Patch &) = delete;
    Patch &operator=(const Patch &) = delete;
    ~Patch() { std::memcpy(site, saved, 5); }
};

// ---- the counted elements ------------------------------------------------------------------------------------------

struct Counted {
    void **vt;
    uint32_t id;
    int32_t refs;
};
constexpr int kObjects = 8;
Counted gObjects[kObjects];
int gThrowAtAddRef = 0; // the Nth AddRef throws (0: none)
int gAddRefCalls = 0;

uint32_t __fastcall CountedAddRef(Counted *self, void *)
{
    Log(0x100u | self->id);
    if (++gAddRefCalls == gThrowAtAddRef) throw 0x77;
    ++self->refs;
    return 0;
}
uint32_t __fastcall CountedRelease(Counted *self, void *)
{
    Log(0x200u | self->id);
    --self->refs;
    return 0;
}
void *gCountedVt[3] = {nullptr, reinterpret_cast<void *>(&CountedAddRef), reinterpret_cast<void *>(&CountedRelease)};

uint32_t IdOf(const void *p) { return p ? static_cast<const Counted *>(p)->id : 0; }

// A block as the vector's allocate makes it: small ones as they are, big ones aligned with the real pointer before them.
// Built before the logging starts (the stubs still register it).
void **MakeBlock(uint32_t elements)
{
    const uint32_t bytes = elements * 4;
    gTrack = true;
    void **block;
    if (bytes < 0x1000) {
        block = static_cast<void **>(AllocateStub(bytes));
    } else {
        void *raw = AllocateStub(bytes + 0x23);
        block = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~static_cast<uintptr_t>(0x1f));
        block[-1] = raw;
    }
    gTrack = false;
    return block;
}

void *RawOf(void **block, uint32_t elements)
{
    return elements * 4 >= 0x1000 ? block[-1] : static_cast<void *>(block);
}

void Describe(const RefPtrVector &v, std::vector<uint32_t> &out)
{
    out.push_back(v.mFirst != nullptr);
    out.push_back(static_cast<uint32_t>(v.mEnd - v.mFirst));
    out.push_back(static_cast<uint32_t>(v.mLast - v.mFirst));
    for (void **p = v.mFirst; p != v.mLast; ++p) out.push_back(IdOf(*p));
}

enum class Kind { Normal, AddRefThrows, FakeHugeCapacity, NegativeCapacity };

using InsertFn = void **(__thiscall *)(void *, void **, void *const *);

} // namespace

DIFF_TEST(RefPtrVector_EmplaceReallocate, 0x14aac0)
{
    for (int i = 0; i < kObjects; ++i) gObjects[i] = {gCountedVt, static_cast<uint32_t>(i + 1), 0};
    auto original = ctx.Original<InsertFn>(0x14aac0);
    gOwnerThread = GetCurrentThreadId();

    Patch imageNew(ctx.image.At<uint8_t>(0x824632), reinterpret_cast<void *>(&NewStub));
    Patch imageDelete(ctx.image.At<uint8_t>(0x824624), reinterpret_cast<void *>(&SizedDeleteStub));
    Patch ourNew(reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&::operator new)), reinterpret_cast<void *>(&NewStub));
    Patch ourDelete(reinterpret_cast<void *>(static_cast<void (*)(void *)>(&::operator delete)), reinterpret_cast<void *>(&DeleteStub));
    Patch ourSizedDelete(reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&::operator delete)), reinterpret_cast<void *>(&SizedDeleteStub));

    uint32_t thrown = 0, badAlloc = 0, middle = 0, empty = 0, atEnd = 0, bigOld = 0, bigNew = 0, nullElement = 0;
    const uint32_t kSeeds = 12000;
    // The descriptions are reused (not made anew per seed): see the arena above.
    std::vector<uint32_t> described[2];
    described[0].reserve(1 << 15);
    described[1].reserve(1 << 15);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        described[0].clear();
        described[1].clear();
        int thrownKind[2] = {};
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2654435761u + 17);
            for (Counted &o : gObjects) o.refs = static_cast<int32_t>(r() % 5);
            gBlockCount = 0;
            gArenaUsed = 0;
            gNextId = 1;
            gLogOn = false;
            gLogCount = 0;
            gAddRefCalls = 0;

            // The shape of the vector.
            Kind kind = Kind::Normal;
            uint32_t size, capacity;
            const uint32_t pick = r() % 100;
            if (pick >= 6 && pick < 10) {
                // Around the sizes where a block becomes big (4096 bytes): the new capacity is 1023/1024 elements for 682/683.
                const uint32_t caps[] = {682, 683, 684, 1022, 1023, 1024, 1025, 1100};
                capacity = caps[r() % 8];
                size = capacity - (r() % 3 == 0 ? r() % 4 : 0);
                if (r() % 4 == 0) size = r() % (capacity + 1);
            } else {
                if (pick < 3) kind = Kind::FakeHugeCapacity;
                else if (pick < 6) kind = Kind::NegativeCapacity;
                size = r() % 7;
                capacity = r() % 9;
                if (capacity < size) capacity = size;
                if (size == 0 && r() % 3 == 0) capacity = 0;
            }
            RefPtrVector vector{nullptr, nullptr, nullptr};
            uint32_t realCapacity = capacity;
            if (kind == Kind::FakeHugeCapacity || kind == Kind::NegativeCapacity) {
                size = r() % 5;
                realCapacity = size + r() % 3;
                if (realCapacity == 0) realCapacity = 1;
            }
            if (realCapacity != 0) {
                vector.mFirst = MakeBlock(realCapacity);
                for (uint32_t i = 0; i < size; ++i) vector.mFirst[i] = r() % 6 == 0 ? nullptr : &gObjects[r() % kObjects];
                vector.mLast = vector.mFirst + size;
                vector.mEnd = vector.mFirst + realCapacity;
            }
            // The capacity limit is faked beyond what the allocator accepts, or below the begin (a negative count).
            if (kind == Kind::FakeHugeCapacity) {
                const uint32_t hugeCaps[] = {0x1c000000, 0x1fffffff, 0x2aaaaaab, 0x2aaaaaaa, 0x1b000000};
                vector.mEnd = vector.mFirst + hugeCaps[r() % 5];
            } else if (kind == Kind::NegativeCapacity) {
                vector.mEnd = vector.mFirst - (1 + r() % 16);
            }
            const uint32_t position = r() % (size + 1);
            void *const value = r() % 6 == 0 ? nullptr : &gObjects[r() % kObjects];
            gThrowAtAddRef = (kind == Kind::Normal && r() % 5 == 0) ? 1 + static_cast<int>(r() % 9) : 0;
            void **const oldBlock = vector.mFirst;
            const uint32_t oldCapacity = realCapacity;

            if (side == 0) {
                if (kind == Kind::Normal) {
                    thrown += gThrowAtAddRef != 0;
                    empty += capacity == 0;
                    middle += position != size;
                    atEnd += position == size && size != 0;
                    bigOld += capacity * 4 >= 0x1000;
                    bigNew += capacity * 4 < 0x1000 && (capacity + capacity / 2 > size ? capacity + capacity / 2 : size + 1) * 4 >= 0x1000;
                    nullElement += value == nullptr;
                }
            }

            void **returned = nullptr;
            gLogOn = true;
            gTrack = true;
            try {
                returned = side == 0 ? original(&vector, vector.mFirst + position, &value) : vector.EmplaceReallocate(vector.mFirst + position, &value);
            } catch (int) {
                thrownKind[side] = 1;
            } catch (...) {
                thrownKind[side] = 2;
            }
            gLogOn = false;
            gTrack = false;
            if (side == 0 && thrownKind[0] == 2) ++badAlloc;

            std::vector<uint32_t> &d = described[side];
            d.push_back(static_cast<uint32_t>(thrownKind[side]));
            d.push_back(returned && vector.mFirst ? static_cast<uint32_t>(returned - vector.mFirst) : 0xffffu);
            d.push_back(vector.mFirst == oldBlock);
            Describe(vector, d); // a failed call leaves the vector as it was
            // A new big block is 32-byte aligned with the real pointer in the dword before it (the block address is not compared: it differs;
            // a block that was not replaced has whatever alignment the test's own allocation had).
            d.push_back(thrownKind[side] == 0 && vector.mFirst && (vector.mEnd - vector.mFirst) * 4 >= 0x1000 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vector.mFirst) & 0x1f) : 0xffu);
            for (const Counted &o : gObjects) d.push_back(static_cast<uint32_t>(o.refs));
            d.push_back(0xe0e0e0e0u);
            d.insert(d.end(), gLog, gLog + gLogCount);
            d.push_back(0xe1e1e1e1u);
            // Blocks still live: the vector's own are expected; any other one is a leak and shows up here.
            void *const ownBlock = (thrownKind[side] == 0 && vector.mFirst) ? RawOf(vector.mFirst, static_cast<uint32_t>(vector.mEnd - vector.mFirst)) : RawOf(oldBlock ? oldBlock : nullptr, oldCapacity);
            for (size_t i = 0; i < gBlockCount; ++i) {
                if (!gBlocks[i].live) continue;
                d.push_back(gBlocks[i].id);
                d.push_back(static_cast<uint32_t>(gBlocks[i].size));
                d.push_back(gBlocks[i].pointer == ownBlock);
                if (!InArena(gBlocks[i].pointer)) std::free(gBlocks[i].pointer);
            }
            gThrowAtAddRef = 0;
        }
        ++ctx.cases;
        if (described[0] != described[1]) {
        {
            size_t at = 0;
            while (at < described[0].size() && at < described[1].size() && described[0][at] == described[1][at]) ++at;
            ctx.Fail("seed %u: original and ours differ (%zu / %zu words, thrown %d/%d), first difference at word %zu: %08x / %08x", seed, described[0].size(), described[1].size(), thrownKind[0],
                     thrownKind[1], at, at < described[0].size() ? described[0][at] : 0u, at < described[1].size() ? described[1][at] : 0u);
            if (ctx.failures.size() < 2) {
                for (int s = 0; s < 2; ++s) {
                    std::string text;
                    char buf[16];
                    for (uint32_t w : described[s]) {
                        std::snprintf(buf, sizeof(buf), "%x ", w);
                        text += buf;
                    }
                    ctx.Fail("  side %d: %s", s, text.c_str());
                }
            }
        }
        }
    }
    gLogOn = false;
    if (!thrown || !badAlloc || !middle || !empty || !atEnd || !bigOld || !bigNew || !nullElement)
        ctx.Fail("coverage: thrown %u bad_alloc %u middle %u empty %u at end %u big old %u big new %u null %u", thrown, badAlloc, middle, empty, atEnd, bigOld, bigNew, nullElement);
}
