// Differential tests for nTSSG::cLightingManager's small slots (src/sg/cLightingManager.cpp). Hook shape: the original
// runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object is 0x548 random bytes whose vtable is a test table logging slots 1
// (AddRef) and 38 (the deleting destructor). The light-set vector (+0x98) points into a test array of 0..3 random sets,
// the current index is drawn in and out of range (out of range: the built-in set at +0x2d0). mName (+0xb4) is a short
// (in place) or long (test buffer) std::string; +0x2c points at a test block; mRef24 is a fake whose slot 16 writes a
// colour. mSamples (+0x2b0) holds a buffer from operator new (the game's operator new 0x824632 and sized delete 0x824624
// are patched to ours so that both sides grow it on one heap). The class's own 0x2d4010 (RebuildAll, lane 261110a) is a
// logging stand-in over the image's and ours, 0x2dfc80 (EvaluateLerpLights, lane 261109j) one over the image's and ours; the switch byte 0xffde64 is drawn and restored.
//
// Compared: results (float bits), out values, the event log, the object's bytes (normalised; the samples vector's
// pointers by size, capacity, contents and buffer reuse, its addresses being operator new's choice), every fake's
// reference count.
#include "code_patch.h"
#include "difftest.h"
#include "sg_deleting_destructors.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cLightingManager;
using nTSSG::cLightSet;
using nTSSG::cLightVector;

constexpr uint32_t kObjectSize = 0x548;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kSetsTag = 0x6e000000;
constexpr uint32_t kBlockTag = 0x6f000000;
constexpr uint32_t kSwitch = 0xffde64;
constexpr uint32_t kSamples = 0x2b0;
enum : uint32_t { evSelf = 0x86, evStand };
enum : unsigned { fColour = 0 };

alignas(16) uint8_t gObject[kObjectSize];
alignas(16) cLightSet gSets[3];
alignas(16) uint8_t gBlock[0x410];
alignas(16) char gLongName[40];

uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Ev(evSelf), Ev(1), Ev(Norm(P(self)));
    return 0x51;
}
void *__fastcall SelfDelete(void *self, void *, uint32_t flags)
{
    Ev(evSelf), Ev(38), Ev(Norm(P(self))), Ev(flags);
    return self;
}
void *gSelfVT[39];

void __fastcall Stand2d4010(void *self, void *, uint32_t a) { Ev(evStand), Ev(0x2d4010), Ev(Norm(P(self))), Ev(a); }
void __fastcall Stand2dfc80(void *self, void *, float a)
{
    uint32_t bits;
    std::memcpy(&bits, &a, 4);
    Ev(evStand), Ev(0x2dfc80), Ev(Norm(P(self))), Ev(bits);
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t gColour[3];
uint32_t Colour(Obj *, uint32_t *args) // slot 16 (&colour, key)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 3; ++i) out[i] = gColour[i] ^ args[1];
    return args[0];
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn a{0x2d4010, &cLightingManager::RebuildAll, &Stand2d4010};
    shape::StandIn b{0x2dfc80, &cLightingManager::EvaluateLerpLights, &Stand2dfc80};
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
    uint8_t savedSwitch = *GameAddress<uint8_t>(kSwitch);
    Fixture()
    {
        gSelfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
        gSelfVT[38] = reinterpret_cast<void *>(&SelfDelete);
    }
    ~Fixture() { *GameAddress<uint8_t>(kSwitch) = savedSwitch; }
};

cLightingManager *Ours() { return reinterpret_cast<cLightingManager *>(gObject); }

void Put(uint8_t *start, uint32_t offset, uint32_t value) { std::memcpy(start + offset, &value, 4); }

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    Put(start, 0, P(gSelfVT));
    Put(start, 0x24, P(&F(fColour)));
    Put(start, 0x2c, P(gBlock));
    const unsigned sets = gen() % 4;
    Put(start, 0x98, P(gSets));
    Put(start, 0x9c, P(gSets + sets));
    Put(start, 0xa0, P(gSets + 3));
    static const uint32_t kIndexes[] = {0, 1, 2, 3, 4, 0xffffffff, 0x80000000};
    Put(start, 0xd0, kIndexes[gen() % 7]);
    if (gen() % 2) { // a short name in place
        const unsigned length = gen() % 16;
        for (unsigned i = 0; i < 16; ++i) start[0xb4 + i] = i < length ? uint8_t('a' + gen() % 26) : 0;
        Put(start, 0xc4, length);
        Put(start, 0xc8, 15);
    } else { // a long name in the test buffer
        Put(start, 0xb4, P(gLongName));
        Put(start, 0xc4, 20);
        Put(start, 0xc8, 31);
    }
}

void Begin(const uint8_t *start, std::mt19937 &gen)
{
    ResetAll();
    F(fColour).Set(16, "pd", 0u, &Colour);
    std::memcpy(gObject, start, kObjectSize);
    std::mt19937 fill(gen());
    for (auto &b : *reinterpret_cast<uint8_t(*)[sizeof(gSets)]>(gSets)) b = uint8_t(fill());
    for (auto &b : gBlock) b = uint8_t(fill());
    for (auto &c : gLongName) c = char('A' + fill() % 26);
    for (auto &c : gColour) c = fill();
    AddRange(gObject, kObjectSize, kObjectTag);
    AddRange(gSets, sizeof(gSets) + 1, kSetsTag);
    AddRange(gBlock, sizeof(gBlock), kBlockTag);
    AddRange(gLongName, sizeof(gLongName), kBlockTag | 0x1000);
}

// The object's words as End logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] =
    "pddddddd dpdpdddd dddddddd dddddddd ddddddpp pddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dd";
Blob End(uint32_t result, const uint32_t *extra, unsigned extraCount, bool skipSamples)
{
    Blob o = gEv;
    o.push_back(0xeeee);
    o.push_back(result);
    for (unsigned i = 0; i < extraCount; ++i) o.push_back(extra[i]);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        if (skipSamples && i >= kSamples && i < kSamples + 12) continue;
        uint32_t word;
        std::memcpy(&word, gObject + i, 4);
        svcfx::Field(o, word, kObjectKinds, i);
    }
    CaptureFakes(o);
    return o;
}

} // namespace

// 0x2dfda0 QueryInterface (ids 1, 0x6b390bfe, 0x452294aa and neighbours; null out) and 0x2dfee0 Release (counts 0..3,
// high).
DIFF_TEST(sg_lighting_manager_query_release, 0x2dfda0)
{
    Fixture fixture;
    auto query = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x2dfda0);
    auto release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2dfee0);
    static const uint32_t kIids[] = {1, 0x6b390bfe, 0x452294aa, 0x6b390bff, 0x452294a9, 0, 2};
    static const uint32_t kCounts[] = {1, 2, 3, 0x40, 0};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2dfda0u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const bool releasing = seed % 2;
        const uint32_t iid = kIids[gen() % 7];
        const bool nullOut = gen() % 5 == 0;
        Put(start, 8, kCounts[gen() % 5]);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 world(seed);
            Begin(start, world);
            uint32_t cell = 0xcdcdcdcd;
            void **out = nullOut ? nullptr : reinterpret_cast<void **>(&cell);
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (releasing) result = side == 0 ? release(gObject) : Ours()->Release();
            else result = (side == 0 ? query(gObject, iid, out) : Ours()->QueryInterface(iid, out)) ? 1 : 0;
            const uint32_t extra = NormOut(cell, 0xcdcdcdcd);
            outcome[side] = End(result, &extra, 1, false);
        }
        Same(ctx, outcome[0], outcome[1], "query_release", seed);
    }
}

// 0x2e1010 SetFlag44, 0x2d8370 Refresh (switch 0xffde64 clear or set), 0x2ded30 Name (default flag 0, 1, 2; short and
// long names), 0x2db060 Value404, 0x2e3690 / 0x2d8310 / 0x2d4e20 / 0x2df7e0 the current set's vectors (0..3 sets, the
// index in or out of range), 0x2df860 Vector2C4 and 0x2ded50 Luminance.
DIFF_TEST(sg_lighting_manager_getters, 0x2e3690)
{
    Fixture fixture;
    auto setFlag = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x2e1010);
    auto refresh = ctx.Original<void(__thiscall *)(void *)>(0x2d8370);
    auto name = ctx.Original<const char *(__thiscall *)(void *)>(0x2ded30);
    auto value404 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2db060);
    using VectorFn = cLightVector *(__thiscall *)(void *, cLightVector *);
    const VectorFn vectors[5] = {ctx.Original<VectorFn>(0x2e3690), ctx.Original<VectorFn>(0x2d8310),
                                 ctx.Original<VectorFn>(0x2d4e20), ctx.Original<VectorFn>(0x2df7e0),
                                 ctx.Original<VectorFn>(0x2df860)};
    auto luminance = ctx.Original<float(__thiscall *)(void *, uint32_t)>(0x2ded50);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2e3690u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        start[0x4c] = uint8_t(gen() % 3);
        const unsigned which = seed % 10;
        const uint8_t value = uint8_t(gen());
        const uint8_t switchByte = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
        const uint32_t key = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 world(seed);
            Begin(start, world);
            *GameAddress<uint8_t>(kSwitch) = switchByte;
            ctx.Mark(side ? "ours" : "original");
            uint32_t result = 0, extra[3] = {0, 0, 0};
            if (which == 0) {
                if (side == 0) setFlag(gObject, value);
                else Ours()->SetFlag44(value);
            } else if (which == 1) {
                if (side == 0) refresh(gObject);
                else Ours()->Refresh();
            } else if (which == 2) {
                result = Norm(P(side == 0 ? name(gObject) : Ours()->Name()));
            } else if (which == 3) {
                result = side == 0 ? value404(gObject) : Ours()->Value404();
            } else if (which <= 8) {
                cLightVector v;
                std::memset(&v, 0xab, sizeof(v));
                if (side == 0) {
                    vectors[which - 4](gObject, &v);
                } else {
                    switch (which) {
                    case 4: v = Ours()->Vector18C(); break;
                    case 5: v = Ours()->Vector19C(); break;
                    case 6: v = Ours()->Vector174(); break;
                    case 7: v = Ours()->NegatedDirection(); break;
                    default: v = Ours()->Vector2C4();
                    }
                }
                std::memcpy(extra, &v, sizeof(v));
            } else {
                const float f = side == 0 ? luminance(gObject, key) : Ours()->Luminance(key);
                std::memcpy(&result, &f, 4);
            }
            outcome[side] = End(result, extra, 3, false);
        }
        Same(ctx, outcome[0], outcome[1], "getters", seed);
    }
}

// 0x2cbd40 AddSample: vectors of 0..5 samples with spare capacity or none (or no buffer), chains of 1..4 appends.
DIFF_TEST(sg_lighting_manager_add_sample, 0x2cbd40)
{
    Fixture fixture;
    auto add = ctx.Original<void(__thiscall *)(void *, const float *, float)>(0x2cbd40);
    for (uint32_t seed = 0; seed < 500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2cbd40u);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const unsigned size = gen() % 6, capacity = gen() % 5 == 0 ? 0 : size + gen() % 3;
        uint32_t initial[8][5], values[4][4], weights[4];
        for (auto &e : initial) for (auto &w : e) w = gen();
        for (auto &e : values) for (auto &w : e) w = gen();
        for (auto &w : weights) w = gen() % 2 ? gen() : 0x3f800000;
        const unsigned steps = 1 + gen() % 4;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 world(seed);
            Begin(start, world);
            uint32_t *buffer = nullptr;
            if (size || capacity) {
                const unsigned slots = capacity < size ? size : capacity;
                buffer = static_cast<uint32_t *>(::operator new(slots * 20));
                std::memcpy(buffer, initial, size * 20);
                const uint32_t words[3] = {P(buffer), P(buffer + size * 5), P(buffer + slots * 5)};
                std::memcpy(gObject + kSamples, words, 12);
            } else {
                std::memset(gObject + kSamples, 0, 12);
            }
            ctx.Mark(side ? "ours" : "original");
            for (unsigned i = 0; i < steps; ++i) {
                float weight;
                std::memcpy(&weight, &weights[i], 4);
                const float *v = reinterpret_cast<const float *>(values[i]);
                if (side == 0) add(gObject, v, weight);
                else Ours()->AddSample(v, weight);
            }
            uint32_t words[3];
            std::memcpy(words, gObject + kSamples, 12);
            const uint32_t extra[3] = {(words[1] - words[0]) / 20, (words[2] - words[0]) / 20, words[0] == P(buffer)};
            Blob &o = outcome[side];
            o = End(0, extra, 3, true);
            for (const uint32_t *p = reinterpret_cast<const uint32_t *>(uintptr_t(words[0])); P(p) != words[1]; ++p) o.push_back(*p);
            ::operator delete(reinterpret_cast<void *>(uintptr_t(words[0])));
        }
        Same(ctx, outcome[0], outcome[1], "add_sample", seed);
    }
}

// 0x2cbc40 the deleting destructor: the destructor stood in on both sides (the image's 0x2cb800 and ours, Destruct,
// lane 261110t); flags 0..3 and random; 0x548 bytes freed on bit 0.
DIFF_TEST(sg_lighting_manager_deleting_destructor, 0x2cbc40)
{
    Fixture fixture;
    shape::CodePatch oursDestructor{shape::CodeOf(&cLightingManager::Destruct), shape::CodeOf(&sgdel::StandDestructor)};
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2cbc40);
    sgdel::Run(
        ctx, 0x2cb800, kObjectSize, [&](void *p, uint32_t flags) { return original(p, flags); },
        [](void *p, uint32_t flags) -> void * { return static_cast<cLightingManager *>(p)->DeletingDestructor(flags); },
        0x2cbc40u);
}
