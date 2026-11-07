// Differential tests for the vectors of counted pointers whose storage comes from the allocator service (src/simantics/ServiceRefVector.h):
// the reallocating insert 0x1d57e0, the release-and-free 0x38f140 and the block free 0x195720. The elements are fakes whose AddRef
// and Release are logged (and whose AddRef can be made to throw); the blocks come from the game's allocator. Whether a block was
// freed is observed through the allocator: a freed small block is the next one it hands out for the same size.
#include "simantics_autonomy_records.h"

#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

struct Counted {
    void **vt;
    uint32_t id;
    int32_t refs;
};

constexpr int kObjects = 8;

struct CountWorld {
    Counted objects[kObjects];
    std::vector<uint32_t> events;
    int throwAtAddRef = 0; // the Nth AddRef throws (0: none)
    int addRefCalls = 0;
};
CountWorld *gCounts = nullptr;

uint32_t __fastcall CountedAddRef(Counted *self, void *)
{
    gCounts->events.push_back(0x100u | self->id);
    if (++gCounts->addRefCalls == gCounts->throwAtAddRef) throw 0x77;
    ++self->refs;
    return 0;
}
uint32_t __fastcall CountedRelease(Counted *self, void *)
{
    gCounts->events.push_back(0x200u | self->id);
    --self->refs;
    return 0;
}
void *gCountedVt[3] = {nullptr, reinterpret_cast<void *>(&CountedAddRef), reinterpret_cast<void *>(&CountedRelease)};

uint32_t IdOf(const void *p)
{
    return p ? static_cast<const Counted *>(p)->id : 0;
}

void InitObjects(CountWorld &w, std::mt19937 &r)
{
    for (int i = 0; i < kObjects; ++i) w.objects[i] = {gCountedVt, static_cast<uint32_t>(i + 1), static_cast<int32_t>(r() % 5)};
}

// A vector with `size` random elements (some null) in a block of `capacity` elements (no block for capacity 0).
void BuildVector(CountWorld &w, std::mt19937 &r, ServiceRefVector &v, uint32_t size, uint32_t capacity)
{
    v.mFirst = v.mLast = v.mEnd = nullptr;
    if (capacity == 0) return;
    v.mFirst = static_cast<void **>(RZMemory::Malloc(capacity * 4));
    for (uint32_t i = 0; i < size; ++i) v.mFirst[i] = r() % 6 == 0 ? nullptr : &w.objects[r() % kObjects];
    v.mLast = v.mFirst + size;
    v.mEnd = v.mFirst + capacity;
}

void DescribeVector(const ServiceRefVector &v, std::vector<uint32_t> &out)
{
    out.push_back(v.mFirst != nullptr);
    out.push_back(static_cast<uint32_t>(v.mEnd - v.mFirst));
    out.push_back(static_cast<uint32_t>(v.mLast - v.mFirst));
    for (void **p = v.mFirst; p != v.mLast; ++p) out.push_back(IdOf(*p));
}

void DescribeWorld(const CountWorld &w, std::vector<uint32_t> &out)
{
    for (const Counted &o : w.objects) out.push_back(static_cast<uint32_t>(o.refs));
    out.push_back(0xe0e0e0e0u);
    out.insert(out.end(), w.events.begin(), w.events.end());
}

// Whether `block` (of `bytes`) is free again: the allocator hands it out for the next request of that size. The probe is given back.
bool IsFree(void *block, uint32_t bytes)
{
    void *const probe = RZMemory::Malloc(bytes);
    const bool free = probe == block;
    RZMemory::Free(probe);
    return free;
}

// While alive, the allocator service cache of the image (0x10c95d4, guard 0x10c95d8) holds an object with the game's own service
// vtable (slot 3 allocates, slot 4 frees, both through the game allocator), so the original's allocations meet ours.
struct AllocatorServiceScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16];
    explicit AllocatorServiceScope(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        std::memset(object, 0, sizeof(object));
        const void *vtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
    }
    AllocatorServiceScope(const AllocatorServiceScope &) = delete;
    ~AllocatorServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

using InsertFn = void **(__thiscall *)(void *, void **, void *const *);

} // namespace

DIFF_TEST(ServiceRefVector_InsertGrown, 0x1d57e0)
{
    GameAllocatorFixture allocator;
    AllocatorServiceScope service(ctx);
    auto original = ctx.Original<InsertFn>(0x1d57e0);
    uint32_t thrown = 0, middle = 0, empty = 0, atEnd = 0;
    for (uint32_t seed = 0; seed < 30000; ++seed) {
        std::vector<uint32_t> described[2];
        bool wasThrown[2] = {};
        bool oldBlockFree[2] = {}, newBlockLeaked[2] = {};
        for (int side = 0; side < 2; ++side) {
            CountWorld world;
            gCounts = &world;
            std::mt19937 r(seed * 2654435761u + 5);
            InitObjects(world, r);
            const uint32_t size = r() % 7;
            const uint32_t capacities[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
            uint32_t capacity = capacities[r() % 9];
            if (capacity < size) capacity = size;
            if (size == 0 && r() % 3 == 0) capacity = 0;
            ServiceRefVector vector;
            BuildVector(world, r, vector, size, capacity);
            const uint32_t position = r() % (size + 1);
            void *const value = r() % 6 == 0 ? nullptr : &world.objects[r() % kObjects];
            world.throwAtAddRef = r() % 4 == 0 ? 1 + static_cast<int>(r() % 9) : 0;
            // The new block's size class: the probe sits in its free list, so the call takes it.
            const uint32_t oldCapacity = capacity;
            uint32_t newCapacity = oldCapacity + oldCapacity / 2;
            if (newCapacity < size + 1) newCapacity = size + 1;
            void *const probe = RZMemory::Malloc(newCapacity * 4);
            RZMemory::Free(probe);
            void **const oldBlock = vector.mFirst;

            void **returned = nullptr;
            try {
                returned = side == 0 ? original(&vector, vector.mFirst + position, &value) : vector.InsertGrown(vector.mFirst + position, &value);
            } catch (int) {
                wasThrown[side] = true;
            }
            described[side].push_back(wasThrown[side]);
            described[side].push_back(returned ? static_cast<uint32_t>(returned - vector.mFirst) : 0xffffu);
            DescribeVector(vector, described[side]);
            DescribeWorld(world, described[side]);
            if (wasThrown[side]) {
                // The new block went back to the allocator, the vector is as it was.
                newBlockLeaked[side] = !IsFree(probe, newCapacity * 4);
                described[side].push_back(newBlockLeaked[side]);
            } else if (oldBlock) {
                oldBlockFree[side] = IsFree(oldBlock, oldCapacity * 4);
                described[side].push_back(oldBlockFree[side]);
            }
            if (vector.mFirst) RZMemory::Free(vector.mFirst);
            gCounts = nullptr;
            if (side == 0) {
                thrown += wasThrown[0];
                middle += !wasThrown[0] && position != size;
                empty += capacity == 0;
                atEnd += !wasThrown[0] && position == size && size != 0;
            }
        }
        ++ctx.cases;
        if (described[0] != described[1]) ctx.Fail("seed %u: original and ours differ (%zu / %zu words, thrown %d/%d)", seed, described[0].size(), described[1].size(), wasThrown[0], wasThrown[1]);
    }
    if (!thrown || !middle || !empty || !atEnd) ctx.Fail("coverage: thrown %u middle %u empty %u at end %u", thrown, middle, empty, atEnd);
}

DIFF_TEST(ServiceRefVector_ReleaseAndFree, 0x38f140)
{
    GameAllocatorFixture allocator;
    AllocatorServiceScope service(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x38f140);
    uint32_t withBlock = 0, noBlock = 0;
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        std::vector<uint32_t> described[2];
        for (int side = 0; side < 2; ++side) {
            CountWorld world;
            gCounts = &world;
            std::mt19937 r(seed * 2654435761u + 9);
            InitObjects(world, r);
            const uint32_t size = r() % 7;
            uint32_t capacity = size + r() % 3;
            if (r() % 5 == 0) capacity = 0;
            ServiceRefVector vector;
            BuildVector(world, r, vector, capacity ? size : 0, capacity);
            void **const block = vector.mFirst;
            if (side == 0) {
                withBlock += block != nullptr;
                noBlock += block == nullptr;
            }
            if (side == 0) original(&vector); else vector.ReleaseAndFree();
            DescribeVector(vector, described[side]);
            DescribeWorld(world, described[side]);
            if (block) described[side].push_back(IsFree(block, capacity * 4));
            gCounts = nullptr;
        }
        ++ctx.cases;
        if (described[0] != described[1]) ctx.Fail("seed %u: original and ours differ", seed);
    }
    if (!withBlock || !noBlock) ctx.Fail("coverage: with block %u without %u", withBlock, noBlock);
}

DIFF_TEST(ServiceRefVector_BlockVectorFree, 0x195720)
{
    GameAllocatorFixture allocator;
    AllocatorServiceScope service(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x195720);
    uint32_t withBlock = 0, noBlock = 0;
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::vector<uint32_t> described[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2654435761u + 11);
            ServiceBlockVector vector{nullptr, nullptr, nullptr};
            const uint32_t capacity = r() % 5 == 0 ? 0 : 1 + r() % 12;
            if (capacity) {
                vector.mFirst = RZMemory::Malloc(capacity * 8);
                vector.mLast = static_cast<uint8_t *>(vector.mFirst) + (r() % (capacity + 1)) * 8;
                vector.mEnd = static_cast<uint8_t *>(vector.mFirst) + capacity * 8;
            }
            void *const block = vector.mFirst;
            if (side == 0) {
                withBlock += block != nullptr;
                noBlock += block == nullptr;
            }
            if (side == 0) original(&vector); else vector.FreeBlock();
            described[side].push_back(vector.mFirst != nullptr);
            described[side].push_back(vector.mLast != nullptr);
            described[side].push_back(vector.mEnd != nullptr);
            if (block) described[side].push_back(IsFree(block, capacity * 8));
        }
        ++ctx.cases;
        if (described[0] != described[1]) ctx.Fail("seed %u: original and ours differ", seed);
    }
    if (!withBlock || !noBlock) ctx.Fail("coverage: with block %u without %u", withBlock, noBlock);
}
