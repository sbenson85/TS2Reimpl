// Differential tests for nTSSG::cDisplayListDynamicRenderStrategy's strategy-interface slots 3, 6, 7, 9 and 10
// (src/sg/cDisplayListDynamicRenderStrategy.cpp). Hook shape: the original runs on the same object image as ours, entered
// at the interface (+8).
//
// The world (tests/ts_services_fixture.h): the object is 0x194 random bytes; the wrapped strategy (+0xc) is a fake whose
// +8 interface is a recording fake. mRegistered (+0x138) holds a buffer from operator new (the game's operator new
// 0x824632 and sized delete 0x824624, which the original's vector growth 0x18b860 and erase use, are patched over the
// image to ours, so both sides share one heap) with drawn size and capacity, its items drawn from a small pool so that
// finds hit and miss.
//
// Compared: results, the event log, the object's bytes outside the vector (normalised), every fake's reference count,
// and the vector's size, capacity, contents and whether it still uses its first buffer. The buffer addresses themselves
// are left out: after growth they are whatever operator new returns (0x18b860 allocates through 0x824632).
#include "code_patch.h"
#include "difftest.h"
#include "sg_deleting_destructors.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <new>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cDisplayListDynamicRenderStrategy;
using nTSSG::cDisplayListDynamicRenderStrategyFace;

constexpr uint32_t kObjectSize = 0x194;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kVector = 0x138;

enum : unsigned { fInner = 0 };

alignas(16) uint8_t gObject[kObjectSize];

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
};

cDisplayListDynamicRenderStrategyFace *Face() { return reinterpret_cast<cDisplayListDynamicRenderStrategyFace *>(gObject + 8); }

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    uint8_t *inner = reinterpret_cast<uint8_t *>(&F(fInner)) - 8;
    std::memcpy(start + 0xc, &inner, 4);
}

// The object's words as Capture logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] =
    "dddpdddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddd-- -ddddddd dddddddd ddddd";
void Capture(Blob &o, uint32_t result)
{
    o.push_back(0xeeee);
    o.push_back(result);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        if (i >= kVector && i < kVector + 12) continue;
        uint32_t word;
        std::memcpy(&word, gObject + i, 4);
        svcfx::Field(o, word, kObjectKinds, i);
    }
    CaptureFakes(o);
}

} // namespace

// 0x3703e0 / 0x373690 / 0x3703d0: slots 3, 6 and 7 go to the wrapped strategy's interface (its +8) with the argument;
// slot 6 sets +0x2c first. The fake's results are drawn and must come back.
DIFF_TEST(sg_display_list_dynamic_forwards, 0x3703e0)
{
    Fixture fixture;
    auto slot3 = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x3703e0);
    auto slot6 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x373690);
    auto slot7 = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x3703d0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3703e0u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const unsigned which = seed % 3;
        const uint32_t ret = gen(), argument = gen() % 2 ? gen() : P(&F(1));
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            F(fInner).Set(3, "d", ret);
            F(fInner).Set(6, 0, ret ^ 0x5a5a);
            F(fInner).Set(7, "d", ret + 3);
            std::memcpy(gObject, start, kObjectSize);
            AddRange(gObject, kObjectSize, kObjectTag);
            ctx.Mark(side ? "ours" : "original");
            void *arg = reinterpret_cast<void *>(uintptr_t(argument));
            uint32_t result;
            if (side == 0) result = which == 0 ? slot3(gObject + 8, arg) : which == 1 ? slot6(gObject + 8) : slot7(gObject + 8, arg);
            else result = which == 0 ? Face()->ForwardSlot3(arg) : which == 1 ? Face()->ForwardSlot6() : Face()->ForwardSlot7(arg);
            outcome[side] = gEv;
            Capture(outcome[side], result);
        }
        Same(ctx, outcome[0], outcome[1], "forwards", seed);
    }
}

// 0x370380 Register / 0x373de0 Unregister: vectors of 0..9 items (capacity up to 4 more, or none at all), the item in
// it once, several times or not at all; a run of operations per seed so that growth and erasure chain.
DIFF_TEST(sg_display_list_dynamic_register, 0x370380)
{
    Fixture fixture;
    auto registerItem = ctx.Original<void(__thiscall *)(void *, void *)>(0x370380);
    auto unregisterItem = ctx.Original<void(__thiscall *)(void *, void *)>(0x373de0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x370380u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const uint32_t pool[] = {0, 0x11, 0x22, 0x33, P(&F(2)), P(&F(3))};
        const unsigned size = gen() % 10, capacity = gen() % 6 == 0 ? 0 : size + gen() % 5;
        uint32_t items[16];
        for (unsigned i = 0; i < size; ++i) items[i] = pool[gen() % 6];
        const unsigned steps = 1 + gen() % 6;
        uint32_t ops[6], args[6];
        for (unsigned i = 0; i < steps; ++i) ops[i] = gen() % 2, args[i] = pool[gen() % 6];
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            std::memcpy(gObject, start, kObjectSize);
            uint32_t *buffer = nullptr;
            if (capacity || size) {
                const unsigned slots = capacity < size ? size : capacity;
                buffer = static_cast<uint32_t *>(::operator new(slots * 4));
                std::memcpy(buffer, items, size * 4);
                uint32_t words[3] = {P(buffer), P(buffer + size), P(buffer + slots)};
                std::memcpy(gObject + kVector, words, 12);
            } else {
                std::memset(gObject + kVector, 0, 12);
            }
            AddRange(gObject, kObjectSize, kObjectTag);
            ctx.Mark(side ? "ours" : "original");
            for (unsigned i = 0; i < steps; ++i) {
                void *arg = reinterpret_cast<void *>(uintptr_t(args[i]));
                if (side == 0) (ops[i] ? registerItem : unregisterItem)(gObject + 8, arg);
                else if (ops[i]) Face()->Register(arg);
                else Face()->Unregister(arg);
            }
            Blob &o = outcome[side];
            o = gEv;
            Capture(o, 0);
            uint32_t words[3];
            std::memcpy(words, gObject + kVector, 12);
            o.push_back((words[1] - words[0]) / 4);
            o.push_back((words[2] - words[0]) / 4);
            o.push_back(words[0] == P(buffer));
            for (uint32_t *p = reinterpret_cast<uint32_t *>(uintptr_t(words[0])); P(p) != words[1]; ++p) {
                // An item is a pool entry (tags 0x11..0x33 and fakes alike): logged as its pool index.
                const auto at = std::find(std::begin(pool), std::end(pool), *p);
                o.push_back(at == std::end(pool) ? Norm(*p) : 0x9001u + unsigned(at - pool));
            }
            ::operator delete(reinterpret_cast<void *>(uintptr_t(words[0])));
        }
        Same(ctx, outcome[0], outcome[1], "register", seed);
    }
}

// 0x36fb80 the deleting destructor: the destructor 0x36f220 stood in; flags 0..3 and random; 0x194 bytes freed on bit 0.
DIFF_TEST(sg_display_list_dynamic_deleting_destructor, 0x36fb80)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x36fb80);
    sgdel::Run(
        ctx, 0x36f220, kObjectSize, [&](void *p, uint32_t flags) { return original(p, flags); },
        [](void *p, uint32_t flags) -> void * {
            return static_cast<cDisplayListDynamicRenderStrategy *>(p)->DeletingDestructor(flags);
        },
        0x36fb80u);
}
