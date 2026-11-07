// Differential tests for cTSNeighborhoodConstants: the deleting destructor (slot 4, 0x417060), GetFloat (slot 5,
// 0x474100), GetValue (slot 6, 0x4740a0), and the cTSConstantsClient destructor body (0x4df1b0).
//
// The getters run on an object whose vtable is a test table: its slot 3 (the load) logs the call and sets the loaded
// byte and some fields, so a getter that loads first hands out the loaded values. Fields are random words (NaN and
// denormal bit patterns included for the floats); ids are drawn around and outside the valid ranges. Compared: the
// result (al), the log, the out word (or that it was left alone) and the object's words.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSNeighborhoodConstants.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using Constants = cTSNeighborhoodConstants;
using Blob = std::vector<uint32_t>;

Blob gLog;
uint32_t gLoadWords[6];

void __fastcall FakeSlot(void *, void *) { gLog.push_back(0xdead); }
void __fastcall FakeLoad(void *self, void *)
{
    auto *bytes = static_cast<uint8_t *>(self);
    gLog.push_back(0x10ad);
    bytes[0xc] = 1;
    std::memcpy(bytes + 0x10, gLoadWords, sizeof(gLoadWords));
}

void *gTable[7] = {reinterpret_cast<void *>(&FakeSlot), reinterpret_cast<void *>(&FakeSlot),
                   reinterpret_cast<void *>(&FakeSlot), reinterpret_cast<void *>(&FakeLoad),
                   reinterpret_cast<void *>(&FakeSlot), reinterpret_cast<void *>(&FakeSlot),
                   reinterpret_cast<void *>(&FakeSlot)};

uint32_t RandomFloatBits(std::mt19937 &rng)
{
    static const uint32_t kSpecial[] = {0x7fc00000u, 0x7f800001u, 0xffc00001u, 0x00000001u, 0x80000000u, 0x7f800000u};
    if (rng() % 4 == 0) return kSpecial[rng() % 6];
    return uint32_t(rng());
}

void Fill(uint32_t *words, std::mt19937 &rng)
{
    words[0] = uint32_t(uintptr_t(gTable));
    for (int i = 1; i < 10; ++i) words[i] = uint32_t(rng());
    // The loaded byte (+0xc) with the next three bytes: 0, 1, a set byte under random bytes, a clear one under them.
    static const uint32_t kLoaded[] = {0u, 1u, 0x5a5a5a01u, 0x5a5a5a00u, 0x00010000u};
    words[3] = rng() % 3 == 0 ? kLoaded[rng() % 5] : uint32_t(rng());
    for (int i = 7; i < 10; ++i) words[i] = RandomFloatBits(rng);
    for (int i = 0; i < 6; ++i) gLoadWords[i] = i < 3 ? uint32_t(rng()) : RandomFloatBits(rng);
}

template <class Call> void RunGetter(difftest::Context &ctx, const char *what, uint32_t salt, Call &&call)
{
    alignas(16) uint32_t object[10];
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed ^ salt);
            Fill(object, rng);
            static const uint32_t kIds[] = {0, 1, 2, 3, 4, 5, 6, 0xffffffffu, 0x80000003u, 0x103u};
            const uint32_t id = kIds[rng() % 10];
            uint32_t out = 0x0badf00d;
            gLog.clear();
            const uint32_t r = call(side, reinterpret_cast<Constants *>(object), id, &out);
            Blob &b = sides[side];
            b = gLog;
            b.push_back(r & 0xff);
            b.push_back(out);
            b.insert(b.end(), object, object + 10);
        }
        if (sides[0] != sides[1]) {
            ctx.Fail("%s seed %u differs", what, seed);
            break;
        }
        ++ctx.cases;
    }
}

} // namespace

DIFF_TEST(TSNeighborhoodConstants_GetFloat, 0x474100)
{
    RunGetter(ctx, "GetFloat", 0x474100, [&](int side, Constants *c, uint32_t id, uint32_t *out) -> uint32_t {
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t *)>(0x474100)(c, id, out)
                         : uint32_t(c->GetFloat(id, reinterpret_cast<float *>(out)));
    });
}

DIFF_TEST(TSNeighborhoodConstants_GetValue, 0x4740a0)
{
    RunGetter(ctx, "GetValue", 0x4740a0, [&](int side, Constants *c, uint32_t id, uint32_t *out) -> uint32_t {
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t *)>(0x4740a0)(c, id, out)
                         : uint32_t(c->GetValue(id, out));
    });
}

DIFF_TEST(TSNeighborhoodConstants_DeletingDestructor, 0x417060)
{
    // A heap object of random words, flags 0, 1 or 0x100. Compared: the result, whether the object was freed and
    // with what size, and its words when it was not (the vtable as an offset from the base class's).
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed * 11 + 5);
        const uint32_t flags = rng() % 2 ? 1u : rng() % 2 ? 0x100u : 0u;
        uint32_t words[10];
        for (uint32_t &w : words) w = uint32_t(rng());
        Blob sides[2];
        for (int side = 0; side < 2; ++side) {
            auto *object = static_cast<uint32_t *>(::operator new(Constants::kObjectSize));
            std::memcpy(object, words, sizeof(words));
            difftest::FreeRecorder frees;
            void *result = side == 0
                ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x417060)(object, flags)
                : static_cast<void *>(reinterpret_cast<Constants *>(object)->DeletingDestructor(flags));
            frees.Stop();
            Blob &b = sides[side];
            b.clear();
            b.push_back(result == object);
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[4];
            const size_t n = frees.SortedSizes(sizes, 4);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Freed(object));
            if ((flags & 1) == 0) {
                b.push_back(object[0] - uint32_t(uintptr_t(GameAddress<void>(cTSConstantsClient::kVTable))));
                b.insert(b.end(), object + 1, object + 10);
                ::operator delete(object);
            }
        }
        if (sides[0] != sides[1]) {
            ctx.Fail("DeletingDestructor seed %u differs", seed);
            break;
        }
        ++ctx.cases;
    }
}

DIFF_TEST(TSConstantsClient_Destruct, 0x4df1b0)
{
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed + 99);
        uint32_t words[2][4];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 fill(seed);
            for (uint32_t &w : words[side]) w = uint32_t(fill());
            if (side == 0)
                ctx.Original<void(__thiscall *)(void *)>(0x4df1b0)(words[side]);
            else
                reinterpret_cast<cTSConstantsClient *>(words[side])->Destruct();
        }
        if (std::memcmp(words[0], words[1], sizeof(words[0])) != 0) {
            ctx.Fail("Destruct seed %u differs", seed);
            break;
        }
        ++ctx.cases;
    }
}
