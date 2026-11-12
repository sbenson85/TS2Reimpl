// Differential tests for nTSSG::cShadowManager's small slots (src/sg/cShadowManager.cpp). Hook shape: the original runs
// on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object is 0xd4 random bytes whose vtable is a test table logging slots 1
// (AddRef) and 17 (the deleting destructor); the renderer (+0x50) and the texture service (cell 0x10cc2d0, which 0x20dfb5
// returns when it is set) are recording fakes. The shadow vector (+0x54) points into a test array of 0..4 random entries
// whose user counts are drawn (-1, 0, 1, 3).
//
// Compared: results, out buffers (bits), the event log, the object's bytes (normalised), every fake's reference count.
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cShadowManager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cShadowBox;
using nTSSG::cShadowEntry;
using nTSSG::cShadowManager;

constexpr uint32_t kObjectSize = 0xd4;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kEntriesTag = 0x6e000000;
constexpr uint32_t kOutTag = 0x6f000000;
constexpr uint32_t kServiceCell = 0x10cc2d0;
enum : uint32_t { evSelf = 0x84, evService };
enum : unsigned { fRenderer = 0, fService };

alignas(16) uint8_t gObject[kObjectSize];
// Raw bytes (cShadowEntry is not trivial: its name is a string); the code under test reads them as four entries.
alignas(16) uint8_t gEntries[4 * sizeof(cShadowEntry)];
alignas(16) uint32_t gOut[20];

uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Ev(evSelf), Ev(1), Ev(Norm(P(self)));
    return 0x51;
}
void *__fastcall SelfDelete(void *self, void *, uint32_t flags)
{
    Ev(evSelf), Ev(17), Ev(Norm(P(self))), Ev(flags);
    return self;
}
void *gSelfVT[18];

uint32_t ServiceTexture(Obj *, uint32_t *args) // slot 24 (&result, handle): result[0] = handle ^ 0x77; returns &result
{
    auto *result = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    result[0] = args[1] ^ 0x77;
    return args[0];
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    Fixture()
    {
        gSelfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
        gSelfVT[17] = reinterpret_cast<void *>(&SelfDelete);
        globals.SetWord(kServiceCell, 0);
    }
};

uint32_t StampOf(int32_t index)
{
    uint32_t stamp;
    std::memcpy(&stamp, gEntries + index * sizeof(cShadowEntry) + offsetof(cShadowEntry, mMatrixStamp), 4);
    return stamp;
}

cShadowManager *Ours() { return reinterpret_cast<cShadowManager *>(gObject); }

void Start(std::mt19937 &gen, uint8_t *start, unsigned entries)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    void *vt = gSelfVT, *renderer = &F(fRenderer);
    std::memcpy(start, &vt, 4);
    std::memcpy(start + 0x50, &renderer, 4);
    cShadowEntry *first = reinterpret_cast<cShadowEntry *>(gEntries), *last = first + entries, *end = first + 4;
    std::memcpy(start + 0x54, &first, 4);
    std::memcpy(start + 0x58, &last, 4);
    std::memcpy(start + 0x5c, &end, 4);
}

void FillEntries(std::mt19937 &gen, uint8_t *entries)
{
    static const int32_t kUsers[] = {-1, 0, 1, 3};
    for (uint32_t i = 0; i < sizeof(gEntries); ++i) entries[i] = uint8_t(gen());
    for (unsigned i = 0; i < 4; ++i) {
        const int32_t users = kUsers[gen() % 4];
        std::memcpy(entries + i * sizeof(cShadowEntry), &users, 4);
    }
}

void Begin(const uint8_t *start, const uint8_t *entries)
{
    ResetAll();
    F(fRenderer).Set(8, "ddpp", 0x8888u);
    F(fService).Set(24, "pd", 0u, &ServiceTexture);
    std::memcpy(gObject, start, kObjectSize);
    std::memcpy(gEntries, entries, sizeof(gEntries));
    std::memset(gOut, 0xcd, sizeof(gOut));
    AddRange(gObject, kObjectSize, kObjectTag);
    AddRange(gEntries, sizeof(gEntries), kEntriesTag);
    AddRange(gOut, sizeof(gOut), kOutTag);
}

// The object's words as End logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] = "pddddddd dddddddd ddddpppp dddddddd dddddddd dddddddd ddddd";
Blob End(uint32_t result)
{
    Blob o = gEv;
    o.push_back(0xeeee);
    o.push_back(result);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        uint32_t word;
        std::memcpy(&word, gObject + i, 4);
        svcfx::Field(o, word, kObjectKinds, i);
    }
    for (uint32_t v : gOut) o.push_back(v);
    CaptureFakes(o);
    return o;
}

} // namespace

// 0x2e89a0 QueryInterface (ids 1, 0xcbf9c006, 0x452294aa, others; null out) and 0x2e8a10 Release (counts 1, 2, high).
DIFF_TEST(sg_shadow_manager_query_release, 0x2e89a0)
{
    Fixture fixture;
    auto query = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x2e89a0);
    auto release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2e8a10);
    static const uint32_t kIids[] = {1, 0xcbf9c006, 0x452294aa, 0x452294ab, 0, 0xcbf9c007};
    static const uint32_t kCounts[] = {1, 2, 3, 0x40, 0};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2e89a0u);
        uint8_t start[kObjectSize], entries[sizeof(gEntries)];
        Start(gen, start, 0);
        FillEntries(gen, entries);
        const bool releasing = seed % 2;
        const uint32_t iid = kIids[gen() % 6], count = kCounts[gen() % 5];
        const bool nullOut = gen() % 5 == 0;
        std::memcpy(start + 0xc, &count, 4);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start, entries);
            void **out = nullOut ? nullptr : reinterpret_cast<void **>(gOut);
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (releasing) result = side == 0 ? release(gObject) : Ours()->Release();
            else result = side == 0 ? query(gObject, iid, out) : Ours()->QueryInterface(iid, out);
            if (!releasing) result &= 0xff;
            gOut[0] = NormOut(gOut[0], 0xcdcdcdcd);
            outcome[side] = End(result);
        }
        Same(ctx, outcome[0], outcome[1], "query_release", seed);
    }
}

// 0x2e8d80 SetOption (indexes 0..12, `on` 0, 1, 2, 0xff) and 0x2e7b90 GetOptionBits.
DIFF_TEST(sg_shadow_manager_options, 0x2e8d80)
{
    Fixture fixture;
    auto setOption = ctx.Original<void(__thiscall *)(void *, uint32_t, uint8_t)>(0x2e8d80);
    auto getBits = ctx.Original<void(__thiscall *)(void *, uint32_t *)>(0x2e7b90);
    static const uint8_t kOn[] = {0, 1, 2, 0xff};
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2e8d80u);
        uint8_t start[kObjectSize], entries[sizeof(gEntries)];
        Start(gen, start, 0);
        FillEntries(gen, entries);
        const uint32_t index = gen() % 13;
        const uint8_t on = kOn[gen() % 4];
        const bool getting = seed % 5 == 0;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start, entries);
            ctx.Mark(side ? "ours" : "original");
            if (getting) {
                if (side == 0) getBits(gObject, gOut);
                else Ours()->GetOptionBits(gOut);
            } else {
                if (side == 0) setOption(gObject, index, on);
                else Ours()->SetOption(index, on);
            }
            outcome[side] = End(0);
        }
        Same(ctx, outcome[0], outcome[1], "options", seed);
    }
}

// 0x2e9620 ShadowTexture, 0x2e9480 GetShadowMatrix (stamp null, equal or not), 0x2e95a0 GetShadowScale and 0x2e9500
// GetShadowBox: indexes -2..5 over 0..4 entries, used or not.
DIFF_TEST(sg_shadow_manager_entries, 0x2e9480)
{
    Fixture fixture;
    auto texture = ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(0x2e9620);
    auto matrix = ctx.Original<bool(__thiscall *)(void *, int32_t, float *, uint32_t *)>(0x2e9480);
    auto scale = ctx.Original<void(__thiscall *)(void *, int32_t, float *, float *)>(0x2e95a0);
    auto box = ctx.Original<cShadowBox *(__thiscall *)(void *, cShadowBox *, int32_t)>(0x2e9500);
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2e9480u);
        uint8_t start[kObjectSize], entries[sizeof(gEntries)];
        Start(gen, start, gen() % 5);
        FillEntries(gen, entries);
        const int32_t index = int32_t(gen() % 8) - 2;
        const unsigned which = seed % 4;
        const unsigned stampKind = gen() % 3; // 0 none, 1 equal to the entry's (when there is one), 2 other
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Begin(start, entries);
            *GameAddress<void *>(kServiceCell) = &F(fService);
            uint32_t *stamp = nullptr;
            if (stampKind) {
                stamp = &gOut[19];
                *stamp = stampKind == 1 && index >= 0 && index < 4 ? StampOf(index) : 0x5bad5bad;
            }
            ctx.Mark(side ? "ours" : "original");
            uint32_t result = 0;
            auto *floats = reinterpret_cast<float *>(gOut);
            switch (which) {
            case 0: result = side == 0 ? texture(gObject, index) : Ours()->ShadowTexture(index); break;
            case 1: result = (side == 0 ? matrix(gObject, index, floats, stamp) : Ours()->GetShadowMatrix(index, floats, stamp)) ? 1 : 0; break;
            case 2:
                if (side == 0) scale(gObject, index, floats, floats + 2);
                else Ours()->GetShadowScale(index, floats, floats + 2);
                break;
            default: {
                cShadowBox value;
                std::memset(&value, 0xab, sizeof(value));
                if (side == 0) box(gObject, &value, index);
                else value = Ours()->GetShadowBox(index);
                std::memcpy(gOut, &value, sizeof(value));
            }
            }
            outcome[side] = End(result);
            *GameAddress<void *>(kServiceCell) = nullptr;
        }
        Same(ctx, outcome[0], outcome[1], "entries", seed);
    }
}
