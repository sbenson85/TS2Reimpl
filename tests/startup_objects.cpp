// Differential tests for src/misc/StartupObjects.h, cRZFileName::ConstructEmpty and math::Quaternion (lane HQ): the
// constructors of small static objects, each against the image's on junk-filled memory with 8-byte margins.
//
// StartupObjects_Simple: ZeroedVector20 (0xc0a664), FixedPool (0x945bd6: element sizes 0..3, the 0x200 / 0x800 edges,
//   large ones), cRZFileName's default constructor (0x1c7f30; then both destroyed with the verified Destruct): the window
//   and the returned pointer (FixedPool returns nothing a caller reads).
// StartupObjects_ListAndLock (0x1ce3f3), StartupObjects_NamedWordHashSet (0x97df6d): heap pointers compared by what they
//   point at (the list's head node: an empty ring; the lock's CRITICAL_SECTION fields; the bucket block's 16 entries all
//   the list's head, its size), the other words as they are; both torn down by our types' destructors.
// StartupObjects_RecordPoolReset (0x26bbb0): the 401-record block, the count, the flag and the object vector (0-5
//   objects on a fake vtable whose slot 33 logs the object and its flags) from junk: every byte, the calls in order.
// Quaternion_PreMultiplyNormalised (0x1a7020): random, unit, axis-aligned, zero, huge, tiny, infinite components (no NaN
//   inputs: see the test), rhs aliasing this: the 16 bytes (bit for bit, made NaNs included) and the returned pointer.
#include "difftest.h"
#include "game/game.h"
#include "misc/Quaternion.h"
#include "misc/StartupObjects.h"
#include "rz/cRZFileName.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace {

using namespace startupobj;

constexpr int kMargin = 8;

template <size_t N> struct Junk {
    alignas(8) uint8_t bytes[N + 2 * kMargin];
    void *Object() { return bytes + kMargin; }
};

template <size_t N> void Fill(Junk<N> &a, Junk<N> &b, std::mt19937 &rng)
{
    for (auto &x : a.bytes) x = static_cast<uint8_t>(rng());
    std::memcpy(b.bytes, a.bytes, sizeof a.bytes);
}

uint32_t WordAt(const void *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, 4);
    return v;
}

} // namespace

DIFF_TEST(StartupObjects_Simple, 0xc0a664)
{
    std::mt19937 rng(0xc0a664);
    {
        auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xc0a664);
        for (int i = 0; i < 50; ++i) {
            ++ctx.cases;
            Junk<16> a, b;
            Fill(a, b, rng);
            void *ra = orig(a.Object());
            void *rb = static_cast<ZeroedVector20 *>(b.Object())->Construct();
            if (ra != a.Object() || rb != b.Object()) ctx.Fail("ZeroedVector20: returned %p / %p", ra, rb);
            if (std::memcmp(a.bytes, b.bytes, sizeof a.bytes) != 0) ctx.Fail("ZeroedVector20: case %d differs", i);
        }
    }
    {
        auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x945bd6);
        const uint32_t edges[] = {0, 1, 2, 3, 4, 5, 8, 0x1ff, 0x200, 0x201, 0x7ff, 0x800, 0x801, 0x10000, 0x7fffffff, 0xffffffff};
        for (int i = 0; i < 300; ++i) {
            ++ctx.cases;
            const uint32_t size = i < 16 ? edges[i] : static_cast<uint32_t>(rng()) >> (rng() % 32);
            Junk<12> a, b;
            Fill(a, b, rng);
            orig(a.Object(), size);
            static_cast<FixedPool *>(b.Object())->Construct(size);
            if (std::memcmp(a.bytes, b.bytes, sizeof a.bytes) != 0)
                ctx.Fail("FixedPool(%x): %08x %08x vs ours %08x %08x", size, WordAt(a.Object(), 0), WordAt(a.Object(), 4),
                         WordAt(b.Object(), 0), WordAt(b.Object(), 4));
        }
    }
    {
        auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1c7f30);
        for (int i = 0; i < 20; ++i) {
            ++ctx.cases;
            Junk<sizeof(cRZFileName)> a, b;
            Fill(a, b, rng);
            void *ra = orig(a.Object());
            void *rb = static_cast<cRZFileName *>(b.Object())->ConstructEmpty();
            if (ra != a.Object() || rb != b.Object()) ctx.Fail("cRZFileName: returned %p / %p", ra, rb);
            if (std::memcmp(a.bytes, b.bytes, sizeof a.bytes) != 0) ctx.Fail("cRZFileName: case %d differs", i);
            static_cast<cRZFileName *>(a.Object())->Destruct();
            static_cast<cRZFileName *>(b.Object())->Destruct();
        }
    }
}

namespace {
// The list-and-lock as a comparable form: the head word replaced by whether the head node is an empty ring, the lock's
// block word by its CRITICAL_SECTION's state; the rest as it is.
std::vector<uint32_t> ListLockForm(const uint8_t *object)
{
    std::vector<uint32_t> out;
    const uint32_t head = WordAt(object, 0), block = WordAt(object, 12);
    const auto *node = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
    out.push_back(head && node[0] == head && node[1] == head);
    out.push_back(head ? static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, const_cast<uint32_t *>(node))) : 0);
    out.push_back(WordAt(object, 4));
    out.push_back(WordAt(object, 8)); // the lock's vtable
    if (block) {
        const auto *cs = reinterpret_cast<const CRITICAL_SECTION *>(static_cast<uintptr_t>(block));
        out.push_back(static_cast<uint32_t>(cs->LockCount));
        out.push_back(static_cast<uint32_t>(cs->RecursionCount));
        out.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cs->OwningThread)));
        out.push_back(WordAt(cs, 0x18));
    } else {
        out.push_back(0xdead);
    }
    return out;
}
} // namespace

DIFF_TEST(StartupObjects_ListAndLock, 0x1ce3f3)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x1ce3f3);
    std::mt19937 rng(0x1ce3f3);
    for (int i = 0; i < 30; ++i) {
        ++ctx.cases;
        Junk<16> a, b;
        Fill(a, b, rng);
        void *ra = orig(a.Object());
        void *rb = static_cast<ListAndLock *>(b.Object())->Construct();
        if (ra != a.Object() || rb != b.Object()) ctx.Fail("ListAndLock: returned %p / %p", ra, rb);
        if (ListLockForm(a.bytes + kMargin) != ListLockForm(b.bytes + kMargin)) ctx.Fail("ListAndLock: case %d differs", i);
        if (std::memcmp(a.bytes, b.bytes, kMargin) || std::memcmp(a.bytes + kMargin + 16, b.bytes + kMargin + 16, kMargin))
            ctx.Fail("ListAndLock: margins written");
        for (Junk<16> *j : {&a, &b}) {
            auto *object = static_cast<ListAndLock *>(j->Object());
            object->lock.Destruct();
            std::destroy_at(&object->items);
        }
    }
}

namespace {
std::vector<uint32_t> HashForm(const uint8_t *object)
{
    std::vector<uint32_t> out;
    const uint32_t head = WordAt(object, 4);
    const auto *node = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
    out.push_back(WordAt(object, 0));
    out.push_back(head && node[0] == head && node[1] == head);
    out.push_back(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, const_cast<uint32_t *>(node))));
    out.push_back(WordAt(object, 8));
    const uint32_t first = WordAt(object, 0xc), last = WordAt(object, 0x10), end = WordAt(object, 0x14);
    out.push_back(last - first);
    out.push_back(end - first);
    for (uint32_t p = first; p != last && p - first < 0x100; p += 4) out.push_back(*reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(p)) == head);
    out.push_back(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, reinterpret_cast<void *>(static_cast<uintptr_t>(first)))));
    out.push_back(WordAt(object, 0x18));
    out.push_back(WordAt(object, 0x1c));
    return out;
}
} // namespace

DIFF_TEST(StartupObjects_NamedWordHashSet, 0x97df6d)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *, const void *)>(0x97df6d);
    std::mt19937 rng(0x97df6d);
    for (int i = 0; i < 30; ++i) {
        ++ctx.cases;
        Junk<0x20> a, b;
        Fill(a, b, rng);
        uint32_t traits[2] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())}; // read, then overwritten
        uint8_t allocator = static_cast<uint8_t>(rng());
        void *ra = orig(a.Object(), traits, &allocator);
        void *rb = static_cast<NamedWordHashSet *>(b.Object())->Construct(traits, &allocator);
        if (ra != a.Object() || rb != b.Object()) ctx.Fail("hash: returned %p / %p", ra, rb);
        if (HashForm(a.bytes + kMargin) != HashForm(b.bytes + kMargin)) ctx.Fail("hash: case %d differs", i);
        if (std::memcmp(a.bytes, b.bytes, kMargin) || std::memcmp(a.bytes + kMargin + 0x20, b.bytes + kMargin + 0x20, kMargin))
            ctx.Fail("hash: margins written");
        std::destroy_at(&static_cast<NamedWordHashSet *>(a.Object())->set);
        std::destroy_at(&static_cast<NamedWordHashSet *>(b.Object())->set);
    }
}

namespace {
std::vector<uint32_t> gSlot33Log;
const uint8_t *gObjectsBase = nullptr;
void *__fastcall FakeSlot33(void *self, void *, uint32_t flags)
{
    gSlot33Log.push_back(static_cast<uint32_t>(static_cast<const uint8_t *>(self) - gObjectsBase));
    gSlot33Log.push_back(flags);
    return self;
}
} // namespace

DIFF_TEST(StartupObjects_RecordPoolReset, 0x26bbb0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x26bbb0);
    constexpr uint32_t kFirst = 0x10cca38, kEnd = 0x10d8cb0; // count (0x10cca40), records, the vector (0x10d8ca0)
    constexpr uint32_t kFlagFirst = RecordPoolReset::kReady - kMargin, kFlagEnd = RecordPoolReset::kReady + 1 + kMargin;
    void *object = ctx.image.At<void>(0x10cca4e);
    std::vector<uint8_t> saved(ctx.image.At<uint8_t>(kFirst), ctx.image.At<uint8_t>(kEnd));
    std::vector<uint8_t> savedFlag(ctx.image.At<uint8_t>(kFlagFirst), ctx.image.At<uint8_t>(kFlagEnd));
    void *vtable[34];
    for (auto &slot : vtable) slot = nullptr;
    vtable[33] = reinterpret_cast<void *>(&FakeSlot33);
    std::mt19937 rng(0x26bbb0);
    for (int i = 0; i < 24; ++i) {
        ++ctx.cases;
        const int count = i % 6;
        std::vector<uint8_t> objects(0x20 * 6);
        for (auto &x : objects) x = static_cast<uint8_t>(rng());
        for (int k = 0; k < count; ++k) {
            void **vp = &vtable[0];
            std::memcpy(&objects[0x20 * k], &vp, 4);
        }
        gObjectsBase = objects.data();
        std::vector<uint8_t> start(kEnd - kFirst), flag(kFlagEnd - kFlagFirst);
        for (auto &x : start) x = static_cast<uint8_t>(rng());
        for (auto &x : flag) x = static_cast<uint8_t>(rng());
        const uint32_t first = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(objects.data()));
        const uint32_t last = first + 0x20 * count;
        std::memcpy(&start[RecordPoolReset::kObjects - kFirst], &first, 4);
        std::memcpy(&start[RecordPoolReset::kObjects + 4 - kFirst], &last, 4);
        std::vector<uint8_t> after[2], afterFlag[2];
        std::vector<uint32_t> log[2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            std::memcpy(ctx.image.At<uint8_t>(kFirst), start.data(), start.size());
            std::memcpy(ctx.image.At<uint8_t>(kFlagFirst), flag.data(), flag.size());
            gSlot33Log.clear();
            returned[side] = side == 0 ? orig(object) : GameAddress<RecordPoolReset>(0x10cca4e)->Construct();
            after[side].assign(ctx.image.At<uint8_t>(kFirst), ctx.image.At<uint8_t>(kEnd));
            afterFlag[side].assign(ctx.image.At<uint8_t>(kFlagFirst), ctx.image.At<uint8_t>(kFlagEnd));
            log[side] = gSlot33Log;
        }
        if (returned[0] != object || returned[1] != object) ctx.Fail("case %d: returned %p / %p", i, returned[0], returned[1]);
        if (after[0] != after[1]) ctx.Fail("case %d: the block differs", i);
        if (afterFlag[0] != afterFlag[1]) ctx.Fail("case %d: the flag differs", i);
        if (log[0] != log[1] || log[0].size() != 2u * count) ctx.Fail("case %d: slot 33 calls differ (%zu, %zu)", i, log[0].size(), log[1].size());
    }
    std::memcpy(ctx.image.At<uint8_t>(kFirst), saved.data(), saved.size());
    std::memcpy(ctx.image.At<uint8_t>(kFlagFirst), savedFlag.data(), savedFlag.size());
}

DIFF_TEST(Quaternion_PreMultiplyNormalised, 0x1a7020)
{
    using math::Quaternion;
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x1a7020);
    std::mt19937 rng(0x1a7020);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    // Infinities, zeros, denormals and overflow are in, NaN inputs are not: when two different NaNs meet in one SSE
    // operation the result is the first operand's, which depends on the operand order the compiler picked for each
    // commutative addss / mulss (the original's 0x1a7081 `addss xmm1, xmm0` vs ours, e.g.); the NaNs the operations make
    // themselves (inf * 0, inf - inf) are all the one default NaN, so those compare bit for bit. Callers pass rotations.
    const float specials[] = {0.0f, -0.0f, 1.0f, -1.0f, 1e-20f, 1e20f, 1e-40f, 3e38f,
                              std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0.5f, 0.70710678f};
    for (int i = 0; i < 20000; ++i) {
        ++ctx.cases;
        Quaternion q[2];
        for (Quaternion &x : q) {
            float *f = &x.x;
            for (int k = 0; k < 4; ++k) {
                switch (rng() % 8) {
                case 0: f[k] = specials[rng() % std::size(specials)]; break;
                case 1: f[k] = unit(rng) * 1000.0f; break;
                default: f[k] = unit(rng); break;
                }
            }
        }
        const bool alias = i % 50 == 7;
        Quaternion a = q[0], b = q[0];
        const Quaternion rhs = q[1];
        void *ra = orig(&a, alias ? &a : &rhs);
        void *rb = b.PreMultiplyNormalised(alias ? &b : &rhs);
        if (ra != &a || rb != &b) ctx.Fail("returned %p / %p", ra, rb);
        if (std::memcmp(&a, &b, sizeof a) != 0) {
            ctx.Fail("case %d%s: (%g %g %g %g) * (%g %g %g %g): %g %g %g %g vs ours %g %g %g %g", i, alias ? " aliased" : "",
                     rhs.x, rhs.y, rhs.z, rhs.w, q[0].x, q[0].y, q[0].z, q[0].w, a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
            return;
        }
    }
}
