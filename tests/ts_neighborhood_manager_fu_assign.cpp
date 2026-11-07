// Differential test for ServiceRefVector::AssignCounted (0x41d6e0, std::vector<counted pointer>::_Assign_counted_range over the
// allocator service; lane FU), the list copy of cTSNeighborhoodManager's GetNamedList / Filter / FilterWith. The elements are
// fakes whose AddRef and Release are logged (AddRef can be made to throw); the blocks come from the game's allocator. Cases:
// no block, shrinking, growing within the capacity, growing past it (the old block freed, the new one 1.5 times the old
// capacity or the count), sources apart from the vector or inside it (the same elements, shifted), and AddRef throwing while
// the tail is copied. Whether a block was freed is observed through the allocator (a freed block is the next one handed out
// for its size); the new block's size by probing the size class.
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

void *RandomElement(CountWorld &w, std::mt19937 &r)
{
    return r() % 6 == 0 ? nullptr : &w.objects[r() % kObjects];
}

bool IsFree(void *block, uint32_t bytes)
{
    void *const probe = RZMemory::Malloc(bytes);
    const bool free = probe == block;
    RZMemory::Free(probe);
    return free;
}

// The allocator service cache of the image (0x10c95d4, guard 0x10c95d8) with the game's own service vtable, so the original's
// blocks meet ours.
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

using AssignFn = void(__thiscall *)(void *, void *const *, uint32_t);

} // namespace

DIFF_TEST(ServiceRefVector_AssignCounted, 0x41d6e0)
{
    GameAllocatorFixture allocator;
    AllocatorServiceScope service(ctx);
    auto original = ctx.Original<AssignFn>(0x41d6e0);
    uint32_t shrink = 0, within = 0, grown = 0, noBlock = 0, inside = 0, thrown = 0, empty = 0;
    for (uint32_t seed = 0; seed < 30000; ++seed) {
        std::vector<uint32_t> described[2];
        bool wasThrown[2] = {};
        for (int side = 0; side < 2; ++side) {
            CountWorld world;
            gCounts = &world;
            std::mt19937 r(seed * 2654435761u + 17);
            for (int i = 0; i < kObjects; ++i)
                world.objects[i] = {gCountedVt, static_cast<uint32_t>(i + 1), static_cast<int32_t>(r() % 5)};
            // The vector: `size` elements in a block of `capacity` (none for capacity 0).
            const uint32_t size = r() % 7;
            uint32_t capacity = size + (r() % 3 == 0 ? 0 : r() % 4);
            if (size == 0 && r() % 3 == 0) capacity = 0;
            ServiceRefVector vector{};
            if (capacity) {
                vector.mFirst = static_cast<void **>(RZMemory::Malloc(capacity * 4));
                for (uint32_t i = 0; i < size; ++i) vector.mFirst[i] = RandomElement(world, r);
                vector.mLast = vector.mFirst + size;
                vector.mEnd = vector.mFirst + capacity;
            }
            // The source: apart (0..10 elements), or inside the vector (only within its elements and without a new block).
            void *source[10];
            void *const *first = source;
            uint32_t count = r() % 11;
            const bool fromInside = size != 0 && r() % 4 == 0;
            if (fromInside) {
                const uint32_t offset = r() % size;
                count = r() % (size - offset + 1);
                first = vector.mFirst + offset;
            } else {
                for (void *&s : source) s = RandomElement(world, r);
                if (count && r() % 5 == 0) source[r() % count] = vector.mFirst && size ? vector.mFirst[r() % size] : nullptr;
            }
            world.throwAtAddRef = r() % 5 == 0 ? 1 + static_cast<int>(r() % 8) : 0;
            // The new block's size class (when one is made): the probe sits in its free list, so the call takes it.
            uint32_t newCapacity = capacity + capacity / 2;
            if (newCapacity < count) newCapacity = count;
            void *probe = nullptr;
            if (count > capacity) {
                probe = RZMemory::Malloc(newCapacity * 4);
                RZMemory::Free(probe);
            }
            void **const oldBlock = vector.mFirst;

            try {
                if (side == 0) original(&vector, first, count);
                else vector.AssignCounted(first, count);
            } catch (int) {
                wasThrown[side] = true;
            }
            std::vector<uint32_t> &d = described[side];
            d.push_back(wasThrown[side]);
            d.push_back(vector.mFirst == oldBlock);
            d.push_back(vector.mFirst ? 1u : 0u);
            d.push_back(static_cast<uint32_t>(vector.mEnd - vector.mFirst));
            d.push_back(static_cast<uint32_t>(vector.mLast - vector.mFirst));
            for (void **p = vector.mFirst; p != vector.mLast; ++p) d.push_back(IdOf(*p));
            if (probe) d.push_back(vector.mFirst == probe);
            for (const Counted &o : world.objects) d.push_back(static_cast<uint32_t>(o.refs));
            d.push_back(0xe0e0e0e0u);
            d.insert(d.end(), world.events.begin(), world.events.end());
            if (oldBlock && vector.mFirst != oldBlock) d.push_back(IsFree(oldBlock, capacity * 4));
            if (vector.mFirst) RZMemory::Free(vector.mFirst);
            gCounts = nullptr;
            if (side == 0) {
                shrink += count <= size;
                within += count > size && count <= capacity;
                grown += count > capacity && capacity != 0;
                noBlock += capacity == 0 && count != 0;
                inside += fromInside;
                thrown += wasThrown[0];
                empty += count == 0;
            }
        }
        ++ctx.cases;
        if (described[0] != described[1])
            ctx.Fail("seed %u: original and ours differ (%zu / %zu words, thrown %d/%d)", seed, described[0].size(), described[1].size(),
                wasThrown[0], wasThrown[1]);
    }
    if (!shrink || !within || !grown || !noBlock || !inside || !thrown || !empty)
        ctx.Fail("coverage: shrink %u within %u grown %u no block %u inside %u thrown %u empty %u", shrink, within, grown, noBlock,
            inside, thrown, empty);
}
