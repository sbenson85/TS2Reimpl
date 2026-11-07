// Differential tests for src/std/StdInstances.h.
//
// ServiceStringObject::ConstructFromText (0x1773e0) allocates through the allocator service cached in
// game global 0x10c95d4 (guard 0x10c95d8). The tests point the cache at a fake service, with the guard
// marked initialised, whose slot 3 (Allocate) hands out heap blocks and records the size; slot 4 (Free)
// must not be called. The first-use path (GetAllocatorService) is the same code as 0x1bf795's and
// 0x177110's and is tested with them (tests/app_services2_cheats.cpp).
//
// UintUintMapObject::Find (0x141c15) runs on maps built by our STL (the game's layout); both sides
// search the same map and the returned nodes are compared.
//
// StaticObjectStub::Construct (0x57c3c0) is compared on its return value and on the bytes around this.
#include "difftest.h"
#include "free_recorder.h"
#include "std/StdInstances.h"

#include <windows.h>

#include <climits>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kAllocCache = 0x10c95d4, kAllocGuard = 0x10c95d8;

struct FakeService {
    void **vtable;
};

std::vector<uint32_t> gAllocSizes; // slot 3 sizes, in order
int gFrees = 0;

void *__fastcall FakeAllocate(FakeService *, void *, uint32_t size)
{
    gAllocSizes.push_back(size);
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void __fastcall FakeFree(FakeService *, void *, void *)
{
    ++gFrees;
}

void *__fastcall FakeUnexpected(FakeService *, void *)
{
    ++gFrees; // counted as a failure below
    return nullptr;
}

void *gServiceVTable[8];
FakeService gService{gServiceVTable};

// Sets the cache and guard for one side, restoring them when it goes.
struct AllocatorGlobals {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    explicit AllocatorGlobals(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(kAllocCache)), guard(ctx.image.At<int>(kAllocGuard)), savedCache(*cache), savedGuard(*guard)
    {
        for (auto &slot : gServiceVTable) slot = reinterpret_cast<void *>(&FakeUnexpected);
        gServiceVTable[3] = reinterpret_cast<void *>(&FakeAllocate);
        gServiceVTable[4] = reinterpret_cast<void *>(&FakeFree);
    }
    void Arm()
    {
        *cache = &gService;
        *guard = INT_MIN;
        gAllocSizes.clear();
        gFrees = 0;
    }
    ~AllocatorGlobals()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// The MSVC string layout, to compare what each side built.
struct RawText {
    union {
        char buf[16];
        char *ptr;
    };
    uint32_t size, capacity;
    const char *Data() const { return capacity > 15 ? ptr : buf; }
};
static_assert(sizeof(RawText) == 0x18);

} // namespace

DIFF_TEST(StdInstances_ServiceStringFromText, 0x1773e0)
{
    AllocatorGlobals globals(ctx);
    auto orig = ctx.Original<void *(__thiscall *)(void *, const char *)>(0x1773e0);
    std::mt19937 gen(1773);
    for (int round = 0; round < 400; ++round) {
        size_t length = round < 64 ? static_cast<size_t>(round) : gen() % 300;
        std::string text;
        for (size_t i = 0; i < length; ++i) text.push_back(static_cast<char>('a' + gen() % 26));
        if (length > 3 && round % 7 == 0) text[length / 2] = '\0'; // the copy stops at the first NUL
        RawText a, b;
        std::memset(&a, 0xcd, sizeof(a));
        std::memset(&b, 0xcd, sizeof(b));

        globals.Arm();
        void *retA = orig(&a, text.c_str());
        std::vector<uint32_t> sizesA = gAllocSizes;
        int freesA = gFrees;

        globals.Arm();
        void *retB = reinterpret_cast<ServiceStringObject *>(&b)->ConstructFromText(text.c_str());
        ++ctx.cases;

        bool same = retA == &a && retB == &b && a.size == b.size && a.capacity == b.capacity && sizesA == gAllocSizes &&
                    freesA == 0 && gFrees == 0 && std::memcmp(a.Data(), b.Data(), a.size + 1) == 0;
        // An inline string's whole 16-byte buffer is compared (the bytes past the text are zeroed first).
        if (same && a.capacity <= 15) same = std::memcmp(a.buf, b.buf, sizeof(a.buf)) == 0;
        if (!same)
            ctx.Fail("length %zu: size %u/%u capacity %u/%u, %zu/%zu allocations, frees %d/%d", length, a.size, b.size, a.capacity,
                     b.capacity, sizesA.size(), gAllocSizes.size(), freesA, gFrees);
        if (a.capacity > 15) HeapFree(GetProcessHeap(), 0, a.ptr);
        if (b.capacity > 15) HeapFree(GetProcessHeap(), 0, b.ptr);
    }
}

DIFF_TEST(StdInstances_UintUintMapFind, 0x141c15)
{
    using Find = void **(__thiscall *)(void *, void **, const uint32_t *);
    auto orig = ctx.Original<Find>(0x141c15);
    std::mt19937 gen(141);
    for (int round = 0; round < 200; ++round) {
        UintUintMapObject object{};
        size_t count = round == 0 ? 0 : gen() % 200;
        std::vector<uint32_t> keys;
        uint32_t range = round % 2 ? 64u : 0xffffffffu; // dense keys collide in buckets
        for (size_t i = 0; i < count; ++i) {
            uint32_t key = gen() % range;
            object.mMap[key] = gen();
            keys.push_back(key);
        }
        for (int probe = 0; probe < 100; ++probe) {
            uint32_t key = probe % 2 && !keys.empty() ? keys[gen() % keys.size()] : gen() % range;
            void *outA = reinterpret_cast<void *>(0x1111);
            void **retA = orig(&object, &outA, &key);
            auto it = object.Find(key);
            void *outB = *reinterpret_cast<void **>(&it);
            ++ctx.cases;
            if (retA != &outA || outA != outB) ctx.Fail("find(0x%08x) in %zu entries: original %p, ours %p", key, count, outA, outB);
        }
    }
}

// The set's destructor: the same random sets built twice, destroyed by each side; the freed blocks
// (every node, 0x14 bytes, and the head) are compared by count and sizes.
DIFF_TEST(StdInstances_IntSetDestroy, 0x1c0366)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x1c0366);
    std::mt19937 gen(1366);
    for (int round = 0; round < 300; ++round) {
        size_t count = round < 5 ? static_cast<size_t>(round) : gen() % 100;
        uint32_t seed = gen();
        alignas(IntSetObject) uint8_t rawA[sizeof(IntSetObject)], rawB[sizeof(IntSetObject)];
        auto *a = new (rawA) IntSetObject();
        auto *b = new (rawB) IntSetObject();
        std::mt19937 fill(seed);
        for (size_t i = 0; i < count; ++i) {
            int value = static_cast<int>(fill() % 1000);
            a->mSet.insert(value);
            b->mSet.insert(value);
        }
        size_t sizesA[256], sizesB[256];
        difftest::FreeRecorder recA;
        orig(a);
        recA.Stop();
        size_t nA = recA.SortedSizes(sizesA, 256);
        difftest::FreeRecorder recB;
        b->Destroy();
        recB.Stop();
        size_t nB = recB.SortedSizes(sizesB, 256);
        ++ctx.cases;
        if (recA.Count() != recB.Count() || nA != nB || std::memcmp(sizesA, sizesB, nA * sizeof(size_t)) != 0)
            ctx.Fail("destroy (%zu values): original freed %zu blocks, ours %zu", count, static_cast<size_t>(recA.Count()),
                     static_cast<size_t>(recB.Count()));
        // Both objects are now destroyed; their raw storage goes with the frame.
    }
}

// The name table: random arrays (many equal lengths, repeated names, the empty name), both sides'
// vectors compared element by element (text and value, in order), then freed by our vector.
DIFF_TEST(StdInstances_NameValueTable, 0xaf6560)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const NameValueEntry *)>(0xaf6560);
    std::mt19937 gen(6560);
    for (int round = 0; round < 400; ++round) {
        size_t count = round < 3 ? static_cast<size_t>(round) : gen() % 80;
        std::vector<std::string> names;
        for (size_t i = 0; i < count; ++i) {
            if (!names.empty() && gen() % 6 == 0) {
                names.push_back(names[gen() % names.size()]);
                continue;
            }
            size_t length = gen() % (round % 3 == 0 ? 4 : 30);
            std::string name;
            for (size_t c = 0; c < length; ++c) name.push_back(static_cast<char>(round % 5 == 0 ? (gen() % 256) | 1 : 'a' + gen() % 4));
            names.push_back(name);
        }
        std::vector<NameValueEntry> entries;
        for (const auto &name : names) entries.push_back({name.c_str(), gen()});
        entries.push_back({nullptr, gen()});

        alignas(NameValueTable) uint8_t rawA[sizeof(NameValueTable)], rawB[sizeof(NameValueTable)];
        for (size_t i = 0; i < sizeof(rawA); ++i) rawA[i] = rawB[i] = static_cast<uint8_t>(gen());
        void *retA = orig(rawA, entries.data());
        void *retB = reinterpret_cast<NameValueTable *>(rawB)->Construct(entries.data());
        auto *a = reinterpret_cast<NameValueTable *>(rawA);
        auto *b = reinterpret_cast<NameValueTable *>(rawB);
        ++ctx.cases;
        bool same = retA == rawA && retB == rawB && a->mEntries.size() == b->mEntries.size() && a->mEntries.size() == count;
        for (size_t i = 0; same && i < a->mEntries.size(); ++i)
            same = a->mEntries[i].first == b->mEntries[i].first && a->mEntries[i].second == b->mEntries[i].second;
        if (!same) ctx.Fail("table of %zu names: original %zu entries, ours %zu", count, a->mEntries.size(), b->mEntries.size());
        a->mEntries.~vector();
        b->mEntries.~vector();
    }
}

DIFF_TEST(StdInstances_StaticObjectStub, 0x57c3c0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x57c3c0);
    std::mt19937 gen(57);
    for (int i = 0; i < 1000; ++i) {
        uint8_t a[16], b[16];
        for (auto &x : a) x = static_cast<uint8_t>(gen());
        std::memcpy(b, a, sizeof(a));
        void *retA = orig(a + 4);
        void *retB = reinterpret_cast<StaticObjectStub *>(b + 4)->Construct();
        ++ctx.cases;
        if (retA != a + 4 || retB != b + 4 || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("stub differs (case %d)", i);
    }
}
