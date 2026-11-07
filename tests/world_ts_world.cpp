// Differential tests for the nTSWorld functions in world/TSWorld.h. The QueryInterface cases run the
// original and ours on identical copies of one object block, the same way tests/query_interfaces.cpp
// does: recording vtables at +0, +4 and +0x320, a ppv slot at +0x7f0 with a sentinel, AddRef calls
// recorded with the offset they hit, and the whole block compared afterwards.
//
// Release also records the deleting-destructor call (slot 24 of the primary vtable) with its flag, and
// the count at +8 is part of the block, so a missed or extra delete fails the test.
//
// Every QueryInterface here returns false on a null ppv without touching anything, so the null-ppv
// case runs for every id. A null `this` is not tested: the original's AddRef would fault on it.
#include "difftest.h"
#include "rz/QueryInterfaces.h"
#include "rz/RZFrameworkHelpers.h"
#include "world/TSWorld.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

using OriginalQI = uint8_t(__thiscall *)(void *, uint32_t, void **);
using OurQI = bool(__fastcall *)(void *, void *, uint32_t, void **);
using OriginalRelease = uint32_t(__thiscall *)(void *);
using OurRelease = uint32_t(__fastcall *)(void *, void *);
using OriginalGetCLSID = uint32_t(__thiscall *)(void *);
using OurGetCLSID = uint32_t(__fastcall *)(void *, void *);

constexpr uint32_t kSize = 0x800;
constexpr uint32_t kOut = 0x7f0; // the ppv slot is at +0x7f0 inside the block
constexpr uint32_t kCountOffset = 8;
constexpr uint32_t kSentinel = 0xdeadbeef;
constexpr int kDeleteSlot = 24; // 0x60 in `call [eax+0x60]`

alignas(16) uint8_t gBlock[kSize];
uint8_t *gBase = nullptr;
std::vector<int> gEvents;
void *gRecordingVTable[32];

int OffsetOf(const void *p)
{
    return static_cast<int>(static_cast<const uint8_t *>(p) - gBase);
}

// Vtable entries take no stack arguments (AddRef, Release) or one (the deleting destructor's flag).
// The __fastcall dummy edx matches the thiscall the original uses.
uint32_t __fastcall RecAddRef(void *self, void * /*edx*/)
{
    gEvents.push_back(1000 + OffsetOf(self));
    return 1;
}

uint32_t __fastcall RecRelease(void *self, void * /*edx*/)
{
    gEvents.push_back(2000 + OffsetOf(self));
    return 1;
}

// `ret 4` like the real deleting destructor: the flag is the one stack argument.
uint32_t __fastcall RecDelete(void *self, void * /*edx*/, uint32_t flag)
{
    gEvents.push_back(3000 + OffsetOf(self));
    gEvents.push_back(static_cast<int>(flag));
    return 0x5a5a5a5a;
}

void SetupRecording()
{
    gRecordingVTable[1] = reinterpret_cast<void *>(&RecAddRef);
    gRecordingVTable[2] = reinterpret_cast<void *>(&RecRelease);
    gRecordingVTable[kDeleteSlot] = reinterpret_cast<void *>(&RecDelete);
}

void FillBlock(uint8_t *block, uint32_t count)
{
    std::memset(block, 0, kSize);
    void *vt = gRecordingVTable;
    std::memcpy(block + 0x000, &vt, sizeof(vt));
    std::memcpy(block + 0x004, &vt, sizeof(vt));
    std::memcpy(block + 0x320, &vt, sizeof(vt));
    std::memcpy(block + kCountOffset, &count, sizeof(count));
    uint32_t sentinel = kSentinel;
    std::memcpy(block + kOut, &sentinel, sizeof(sentinel));
}

struct Outcome {
    uint32_t result;
    std::vector<int> events;
    std::vector<uint8_t> block;
};

template <typename Call> Outcome RunOne(Call call, uint32_t riid, bool nullOut)
{
    FillBlock(gBlock, 0);
    gBase = gBlock;
    gEvents.clear();
    void **ppv = nullOut ? nullptr : reinterpret_cast<void **>(gBlock + kOut);
    Outcome o;
    o.result = call(static_cast<void *>(gBlock), riid, ppv);
    o.events = gEvents;
    o.block.assign(gBlock, gBlock + kSize);
    return o;
}

// The ids every QueryInterface here compares against, plus or minus one, the edges, and random values.
std::vector<uint32_t> Ids()
{
    static const uint32_t kSpecial[] = {
        0,          1,          2,          0x2a284d16, 0xe4fda3d4, 0xeb589762, 0xab9406b1, 0x2c900fe6,
        0x916da151, 0x49ff7d6f, 0x49ff7d76, 0xb21f9b25, 0x6ca2a642, 0x6ca2a64e, 0x8ce45d17, 0x287fb697,
        0x7fffffff,
        0x80000000, 0xffffffff,
    };
    std::vector<uint32_t> ids;
    for (uint32_t s : kSpecial) {
        ids.push_back(s);
        ids.push_back(s + 1);
        ids.push_back(s - 1);
    }
    std::mt19937 rng(0x0b1f9b25);
    for (int i = 0; i < 2000; ++i) ids.push_back(static_cast<uint32_t>(rng()));
    return ids;
}

// Runs the original and ours on every id, with a valid ppv and with a null one. Compares the result,
// the AddRef/delete sequence and the whole block.
void RunQI(difftest::Context &ctx, uint32_t rva, OurQI ours)
{
    auto orig = ctx.Original<OriginalQI>(rva);
    SetupRecording();
    for (uint32_t riid : Ids()) {
        for (int nullOut = 0; nullOut < 2; ++nullOut) {
            Outcome a = RunOne([&](void *self, uint32_t id, void **ppv) { return orig(self, id, ppv); }, riid, nullOut != 0);
            Outcome b = RunOne(
                [&](void *self, uint32_t id, void **ppv) { return static_cast<uint32_t>(ours(self, nullptr, id, ppv)); },
                riid, nullOut != 0);
            ++ctx.cases;
            if ((a.result & 0xff) != (b.result & 0xff))
                ctx.Fail("riid %08x out %s: al original %u, ours %u", riid, nullOut ? "null" : "set", a.result & 0xff,
                         b.result & 0xff);
            if (a.events != b.events)
                ctx.Fail("riid %08x out %s: AddRef events differ (original %zu, ours %zu)", riid, nullOut ? "null" : "set",
                         a.events.size(), b.events.size());
            if (a.block != b.block) ctx.Fail("riid %08x out %s: block differs", riid, nullOut ? "null" : "set");
        }
    }
}

// Release: each count in `counts` runs once. A count of 1 deletes (slot 24 with flag 1), a count of 0
// wraps to 0xffffffff and does not. The full eax is compared, since the original returns the new
// count in eax and clears eax after a delete.
void RunRelease(difftest::Context &ctx, uint32_t rva, OurRelease ours)
{
    auto orig = ctx.Original<OriginalRelease>(rva);
    SetupRecording();
    const uint32_t counts[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe};
    std::mt19937 rng(0x6ca2a642);
    std::vector<uint32_t> all(std::begin(counts), std::end(counts));
    for (int i = 0; i < 500; ++i) all.push_back(static_cast<uint32_t>(rng()) | 1u);
    for (uint32_t count : all) {
        FillBlock(gBlock, count);
        gBase = gBlock;
        gEvents.clear();
        uint32_t a = orig(gBlock);
        std::vector<int> eventsA = gEvents;
        std::vector<uint8_t> blockA(gBlock, gBlock + kSize);

        FillBlock(gBlock, count);
        gEvents.clear();
        uint32_t b = ours(gBlock, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("count %08x: result original %08x, ours %08x", count, a, b);
        if (eventsA != gEvents)
            ctx.Fail("count %08x: delete/AddRef events differ (original %zu, ours %zu)", count, eventsA.size(), gEvents.size());
        if (blockA != std::vector<uint8_t>(gBlock, gBlock + kSize)) ctx.Fail("count %08x: block differs", count);
    }
}

// GetGZCLSID returns a constant and touches nothing: one call with a few objects is enough.
void RunGetCLSID(difftest::Context &ctx, uint32_t rva, OurGetCLSID ours)
{
    auto orig = ctx.Original<OriginalGetCLSID>(rva);
    FillBlock(gBlock, 0);
    uint32_t a = orig(gBlock);
    uint32_t b = ours(gBlock, nullptr);
    ++ctx.cases;
    if (a != b) ctx.Fail("GetGZCLSID original %08x, ours %08x", a, b);
}

} // namespace

// cWorldTerrain::QueryInterface: 0xe4fda3d4 and 0xeb589762 store masked offsets; the rest goes to
// cRZUnknown::QueryInterface on this.
DIFF_TEST(TSWorld_WorldTerrainQueryInterface, 0x8032e0)
{
    RunQI(ctx, 0x8032e0, &tsworld::WorldTerrainQueryInterface);
}

DIFF_TEST(TSWorld_WorldRoofQueryInterface, 0x80f370)
{
    RunQI(ctx, 0x80f370, &tsworld::WorldRoofQueryInterface);
}

DIFF_TEST(TSWorld_WorldPoolQueryInterface, 0x811e00)
{
    RunQI(ctx, 0x811e00, &tsworld::WorldPoolQueryInterface);
}

DIFF_TEST(TSWorld_WorldCurvedPoolQueryInterface, 0x812f60)
{
    RunQI(ctx, 0x812f60, &tsworld::WorldCurvedPoolQueryInterface);
}

// cWallGraph::QueryInterface: no fall-through, so an unknown id returns false.
DIFF_TEST(TSWorld_WallGraphQueryInterface, 0x806c70)
{
    RunQI(ctx, 0x806c70, &tsworld::WallGraphQueryInterface);
}

DIFF_TEST(TSWorld_WorldOutsideSupportQueryInterface, 0x81df10)
{
    RunQI(ctx, 0x81df10, &tsworld::WorldOutsideSupportQueryInterface);
}

// cPartitionManager::QueryInterface: the base is called on this + 4.
DIFF_TEST(TSWorld_PartitionManagerQueryInterface, 0x816c40)
{
    RunQI(ctx, 0x816c40, &tsworld::PartitionManagerQueryInterface);
}

DIFF_TEST(TSWorld_ProxyManagerQueryInterface, 0x81bbe0)
{
    RunQI(ctx, 0x81bbe0, &tsworld::ProxyManagerQueryInterface);
}

DIFF_TEST(TSWorld_WallGraphRelease, 0x807240)
{
    RunRelease(ctx, 0x807240, &tsworld::WallGraphRelease);
}

DIFF_TEST(TSWorld_WorldTerrainGetGZCLSID, 0x801ff0)
{
    RunGetCLSID(ctx, 0x801ff0, &tsworld::WorldTerrainGetGZCLSID);
}

DIFF_TEST(TSWorld_WallGraphGetGZCLSID, 0x806a90)
{
    RunGetCLSID(ctx, 0x806a90, &tsworld::WallGraphGetGZCLSID);
}

// ---- Bodies that are not QueryInterface: wall vertex/section/edge accessors, partition manager setters
// and world DB helpers. Each case fills gBlock with random bytes, optionally writes a field, takes a
// snapshot, runs the original, restores the snapshot and runs ours. The result and the whole block are
// compared, so both the returned pointer and any store must match.
namespace {

std::vector<uint8_t> gSnapshot;

void RandomBlock(std::mt19937 &rng)
{
    for (uint32_t i = 0; i < kSize; i += 4) {
        uint32_t v = static_cast<uint32_t>(rng());
        std::memcpy(gBlock + i, &v, sizeof(v));
    }
}

void PutDword(uint32_t offset, uint32_t value)
{
    std::memcpy(gBlock + offset, &value, sizeof(value));
}

void Snapshot()
{
    gSnapshot.assign(gBlock, gBlock + kSize);
}

void Restore()
{
    std::memcpy(gBlock, gSnapshot.data(), kSize);
}

std::vector<uint8_t> Block()
{
    return std::vector<uint8_t>(gBlock, gBlock + kSize);
}

uintptr_t Bits(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// The values the indices and set/store helpers take: boundaries and random 32-bit values.
std::vector<uint32_t> Words(std::mt19937 &rng)
{
    std::vector<uint32_t> words = {0, 1, 2, 3, 4, 5, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe};
    for (int i = 0; i < 400; ++i) words.push_back(static_cast<uint32_t>(rng()));
    return words;
}

using OriginalSetPair = void(__thiscall *)(void *, uint32_t, uint32_t);
using OriginalSetFour = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);

// cPartitionManager::vf6 and vf5: setters that store their arguments at fixed offsets.
template <typename Ours> void RunPairSetter(difftest::Context &ctx, uint32_t rva, Ours ours)
{
    auto orig = ctx.Original<OriginalSetPair>(rva);
    std::mt19937 rng(0x8188e0);
    for (uint32_t value : Words(rng)) {
        RandomBlock(rng);
        void *self = gBlock + rng() % (kSize - 0x30);
        uint32_t second = static_cast<uint32_t>(rng());
        Snapshot();
        orig(self, value, second);
        std::vector<uint8_t> afterA = Block();
        Restore();
        ours(self, nullptr, value, second);
        ++ctx.cases;
        if (afterA != Block()) ctx.Fail("value %08x: block differs", value);
    }
}

template <typename Ours> void RunFourSetter(difftest::Context &ctx, uint32_t rva, Ours ours)
{
    auto orig = ctx.Original<OriginalSetFour>(rva);
    std::mt19937 rng(0x818900);
    std::vector<uint32_t> words = Words(rng);
    for (size_t i = 0; i + 3 < words.size(); i += 4) {
        RandomBlock(rng);
        void *self = gBlock + rng() % (kSize - 0x30);
        uint32_t a = words[i], b = words[i + 1], c = words[i + 2], d = words[i + 3];
        Snapshot();
        orig(self, a, b, c, d);
        std::vector<uint8_t> afterA = Block();
        Restore();
        ours(self, nullptr, a, b, c, d);
        ++ctx.cases;
        if (afterA != Block()) ctx.Fail("args %08x %08x %08x %08x: block differs", a, b, c, d);
    }
}

} // namespace

DIFF_TEST(TSWorld_PartitionManagerSetPair, 0x8188e0)
{
    RunPairSetter(ctx, 0x8188e0, &tsworld::PartitionManagerSetPair);
}

DIFF_TEST(TSWorld_PartitionManagerSetFour, 0x818900)
{
    RunFourSetter(ctx, 0x818900, &tsworld::PartitionManagerSetFour);
}

DIFF_TEST(TSWorld_WorldRoofGetGZCLSID, 0x80e640)
{
    RunGetCLSID(ctx, 0x80e640, &tsworld::WorldRoofGetGZCLSID);
}

DIFF_TEST(TSWorld_WorldPoolGetGZCLSID, 0x811280)
{
    RunGetCLSID(ctx, 0x811280, &tsworld::WorldPoolGetGZCLSID);
}

DIFF_TEST(TSWorld_WorldCurvedPoolGetGZCLSID, 0x812f30)
{
    RunGetCLSID(ctx, 0x812f30, &tsworld::WorldCurvedPoolGetGZCLSID);
}
