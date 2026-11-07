// Differential tests for nTSSG::cSceneManager's small slots (src/sg/cSceneManager.cpp). Hook shape: the original runs on
// the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object is 0x528 random bytes whose vtable is a test table logging slots 1
// (AddRef) and 28 (the deleting destructor); the counters' strings at +0x68 and +0x188 get a test vtable whose slot 5
// (ToChar) logs. The frame deque points at a test block map; mSamples at a buffer from operator new. References are
// recording fakes. The global tick counter (0x10d92dc) is drawn and restored afterwards.
//
// Compared: results, the event log, the object's bytes (normalised), every fake's reference count, the tick counter.
#include "difftest.h"
#include "sg_deleting_destructors.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cSceneManager.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cSceneFrame;
using nTSSG::cSceneManager;

constexpr uint32_t kObjectSize = 0x528;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlocksTag = 0x6e000000;
constexpr uint32_t kMapTag = 0x6f000000;
constexpr uint32_t kTickCounter = 0x10d92dc;
enum : uint32_t { evSelf = 0x85 };
enum : unsigned { fRef51C = 0, fRef8, fNew, fOld };

alignas(16) uint8_t gObject[kObjectSize];
alignas(16) cSceneFrame gBlocks[16][2];
alignas(16) cSceneFrame *gMap[16];

uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Ev(evSelf), Ev(1), Ev(Norm(P(self)));
    return 0x51;
}
void *__fastcall SelfDelete(void *self, void *, uint32_t flags)
{
    Ev(evSelf), Ev(28), Ev(Norm(P(self)));
    Ev(flags);
    return self;
}
const char *__fastcall StringToChar(void *self, void *)
{
    Ev(evSelf), Ev(5), Ev(Norm(P(self)));
    return reinterpret_cast<const char *>(uintptr_t(0x7e570000u + (P(self) & 0xfff)));
}
void *gSelfVT[29];
void *gStringVT[6];

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    Fixture()
    {
        gSelfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
        gSelfVT[28] = reinterpret_cast<void *>(&SelfDelete);
        gStringVT[5] = reinterpret_cast<void *>(&StringToChar);
        globals.SetWord(kTickCounter, 0);
    }
};

cSceneManager *Ours() { return reinterpret_cast<cSceneManager *>(gObject); }

void Put(uint8_t *start, uint32_t offset, uint32_t value) { std::memcpy(start + offset, &value, 4); }

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    Put(start, 0, P(gSelfVT));
    Put(start, 0x68, P(gStringVT));
    Put(start, 0x188, P(gStringVT));
}

void Begin(const uint8_t *start)
{
    ResetAll();
    F(fRef51C).Set(18, "d");
    std::memcpy(gObject, start, kObjectSize);
    AddRange(gObject, kObjectSize, kObjectTag);
    AddRange(gBlocks, sizeof(gBlocks), kBlocksTag);
    AddRange(gMap, sizeof(gMap), kMapTag);
}

// The object's words as End logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddd ddpddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd ddpddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dd";
// The same with the samples vector at 0x1ac (begin, end, capacity: words 107..109) as the tick test sets it, pointers
// into its buffer.
constexpr char kSamplesKinds[] =
    "pddddddd dddddddd dddddddd ddpddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd ddpddddd dddpppdd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dd";
static_assert(sizeof(kSamplesKinds) == sizeof(kObjectKinds));
Blob End(uint32_t result, const char *kinds = kObjectKinds)
{
    Blob o = gEv;
    o.push_back(0xeeee);
    o.push_back(result);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        uint32_t word;
        std::memcpy(&word, gObject + i, 4);
        svcfx::Field(o, word, kinds, i);
    }
    CaptureFakes(o);
    o.push_back(*GameAddress<uint32_t>(kTickCounter));
    return o;
}

} // namespace

// 0x2bfa90 QueryInterface (ids 1, 0x69512c64, 0x69512c6d and neighbours; null out), 0x2bfac0 Release (counts 0..3,
// high) and 0x2bff90 SetRef520 (null, the same or another object, old null or not).
DIFF_TEST(sg_scene_manager_query_release, 0x2bfa90)
{
    Fixture fixture;
    auto query = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x2bfa90);
    auto release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2bfac0);
    auto setRef = ctx.Original<void(__thiscall *)(void *, void *)>(0x2bff90);
    static const uint32_t kIids[] = {1, 0x69512c64, 0x69512c6d, 0x69512c65, 0x69512c6c, 0, 2};
    static const uint32_t kCounts[] = {1, 2, 3, 0x40, 0};
    for (uint32_t seed = 0; seed < 450; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2bfa90u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const unsigned which = seed % 3;
        const uint32_t iid = kIids[gen() % 7];
        const bool nullOut = gen() % 5 == 0;
        Put(start, 4, kCounts[gen() % 5]);
        const unsigned oldKind = gen() % 3, newKind = gen() % 4;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start);
            uint32_t cell = 0xcdcdcdcd;
            void *old = oldKind == 0 ? nullptr : oldKind == 1 ? &F(fOld) : &F(fNew);
            std::memcpy(gObject + 0x520, &old, 4);
            void *object = newKind == 0 ? nullptr : newKind == 1 ? &F(fNew) : newKind == 2 ? &F(fOld) : old;
            ctx.Mark(side ? "ours" : "original");
            uint32_t result = 0;
            if (which == 0) {
                void **out = nullOut ? nullptr : reinterpret_cast<void **>(&cell);
                result = (side == 0 ? query(gObject, iid, out) : Ours()->QueryInterface(iid, out)) ? 1 : 0;
            } else if (which == 1) {
                result = side == 0 ? release(gObject) : Ours()->Release();
            } else if (side == 0) {
                setRef(gObject, object);
            } else {
                Ours()->SetRef520(object);
            }
            outcome[side] = End(result);
            outcome[side].push_back(NormOut(cell, 0xcdcdcdcd));
        }
        Same(ctx, outcome[0], outcome[1], "query_release", seed);
    }
}

// 0x2be740 LastFrame, 0x2be7f0 FrameValue and 0x2be820 FrameFlag over block maps of 1..16 entries, offsets that wrap,
// frames anywhere in the window.
DIFF_TEST(sg_scene_manager_frames, 0x2be7f0)
{
    Fixture fixture;
    auto last = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2be740);
    auto value = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x2be7f0);
    auto flag = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x2be820);
    for (uint32_t seed = 0; seed < 900; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2be7f0u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const uint32_t mapSize = 1u << (gen() % 5);
        const uint32_t count = 1 + gen() % (mapSize * 2);
        const uint32_t offset = gen() % 64, base = gen() % 3 == 0 ? gen() : gen() % 1000;
        Put(start, 0x14, P(gMap));
        Put(start, 0x18, mapSize);
        Put(start, 0x1c, offset);
        Put(start, 0x20, count);
        Put(start, 0x24, base);
        const uint32_t frame = base + gen() % count;
        uint8_t blocks[sizeof(gBlocks)];
        for (uint8_t &b : blocks) b = uint8_t(gen());
        const unsigned which = seed % 3;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start);
            std::memcpy(gBlocks, blocks, sizeof(gBlocks));
            for (unsigned i = 0; i < 16; ++i) gMap[i] = gBlocks[(i * 5 + seed) % 16];
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (which == 0) result = side == 0 ? last(gObject) : Ours()->LastFrame();
            else if (which == 1) result = side == 0 ? value(gObject, frame) : Ours()->FrameValue(frame);
            else result = side == 0 ? flag(gObject, frame) : Ours()->FrameFlag(frame);
            outcome[side] = End(result);
        }
        Same(ctx, outcome[0], outcome[1], "frames", seed);
    }
}

// 0x2be6f0 Tick (counter 0, 1, 28, 29 and others), 0x2be6e0 Name1, 0x2be6d0 Name10 and 0x2c00c0 ResetSamples (0..6
// doubles, spare capacity or none, or no buffer).
DIFF_TEST(sg_scene_manager_tick_names_samples, 0x2be6f0)
{
    Fixture fixture;
    auto tick = ctx.Original<void *(__thiscall *)(void *)>(0x2be6f0);
    auto name1 = ctx.Original<const char *(__thiscall *)(void *)>(0x2be6e0);
    auto name10 = ctx.Original<const char *(__thiscall *)(void *)>(0x2be6d0);
    auto reset = ctx.Original<void(__thiscall *)(void *)>(0x2c00c0);
    static const uint32_t kCounters[] = {0, 1, 28, 29, 15, 0x7fffffff, 0xffffffff};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2be6f0u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        Put(start, 0x51c, P(&F(fRef51C)));
        if (gen() % 2) Put(start, 8, P(&F(fRef8)));
        const uint32_t counter = kCounters[gen() % 7];
        const unsigned which = seed % 4;
        const unsigned size = gen() % 7, capacity = gen() % 5 == 0 ? 0 : size + gen() % 3;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start);
            *GameAddress<uint32_t>(kTickCounter) = counter;
            double *buffer = nullptr;
            if (capacity) {
                buffer = static_cast<double *>(::operator new(capacity * sizeof(double)));
                for (unsigned i = 0; i < capacity; ++i) buffer[i] = double(i) + 0.5;
                const uint32_t words[3] = {P(buffer), P(buffer + size), P(buffer + capacity)};
                std::memcpy(gObject + 0x1ac, words, 12);
                AddRange(buffer, capacity * sizeof(double) + 1, 0x70000000); // + 1: the end pointer
            } else {
                std::memset(gObject + 0x1ac, 0, 12);
            }
            ctx.Mark(side ? "ours" : "original");
            uint32_t result = 0;
            switch (which) {
            case 0: result = Norm(P(side == 0 ? tick(gObject) : Ours()->Tick())); break;
            case 1: result = P(side == 0 ? name1(gObject) : Ours()->Name1()); break;
            case 2: result = P(side == 0 ? name10(gObject) : Ours()->Name10()); break;
            default:
                if (side == 0) reset(gObject);
                else Ours()->ResetSamples();
            }
            outcome[side] = End(result, kSamplesKinds);
            for (unsigned i = 0; i < capacity; ++i) outcome[side].push_back(uint32_t(buffer[i] * 2));
            ::operator delete(buffer);
            *GameAddress<uint32_t>(kTickCounter) = 0;
        }
        Same(ctx, outcome[0], outcome[1], "tick_names_samples", seed);
    }
}

// 0x2bde20 the deleting destructor: the destructor 0x2bd900 stood in; flags 0..3 and random; 0x528 bytes freed on bit 0.
DIFF_TEST(sg_scene_manager_deleting_destructor, 0x2bde20)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2bde20);
    sgdel::Run(
        ctx, 0x2bd900, kObjectSize, [&](void *p, uint32_t flags) { return original(p, flags); },
        [](void *p, uint32_t flags) -> void * { return static_cast<cSceneManager *>(p)->DeletingDestructor(flags); },
        0x2bde20u);
}
