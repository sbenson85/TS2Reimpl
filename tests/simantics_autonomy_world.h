#pragma once
// Shared fakes of the autonomy (FindBestAction) tests: the person harness (tests/simantics_prims_person.h) plus recording
// stand-ins for the services the autonomy code reaches through game globals: the resource service (cached singleton C, BCON
// tables) and the service object that hands out cached services.
#include "simantics_prims_person.h"

#include "simantics/BconTable.h"

#include "rz/GeneralAllocator.h"

namespace {

// The game globals the autonomy tests point at their fakes or fill; saved when a test starts and put back when it ends, so the
// tests that run after it see the image as the harness mapped it.
struct AutonomyGlobals {
    struct Range {
        uint32_t rva;
        uint32_t size;
        std::vector<uint8_t> saved;
    };
    std::vector<Range> ranges;
    AutonomyGlobals()
    {
        const Range list[] = {{0x10c9378, 4, {}},  {0x10cc288, 0xc, {}},  {0x10cc45c, 4, {}},   {0x10faf94, 4, {}},   {0x10dac2c, 8, {}},
                              {0x10dac40, 8, {}},  {0x10dac5a, 0x52, {}}, {0x10da968, 8, {}},   {0x101450c, 0x28, {}}, {0x1014404, 4, {}},
                              {0x10149e8, 0x30, {}}};
        for (Range range : list) {
            const uint8_t *p = GameAddress<uint8_t>(range.rva);
            range.saved.assign(p, p + range.size);
            ranges.push_back(std::move(range));
        }
    }
    ~AutonomyGlobals()
    {
        for (const Range &range : ranges) std::memcpy(GameAddress<uint8_t>(range.rva), range.saved.data(), range.size);
    }
};

constexpr uint32_t kGlobalServiceObject = 0x10c9378;
constexpr uint32_t kGlobalSingletonA = 0x10cc288;
constexpr uint32_t kGlobalCachedC = 0x10cc290;
constexpr uint32_t kGlobalOptionMask = 0x10faf94;

// Resource service slot 72 (key, interface id, out): logs the key's four words and the interface id; the answer is the
// script's word (success when its low byte is non-zero); the table (fake `tableFake`) is stored through `out` when bit 8 or 9
// of the answer is set, so answers with a table but no success byte, and the reverse, both occur.
int gTableFake = 0;
uint32_t __fastcall GetResourceFake(void *self, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(72u << 8 | 3u);
    for (int i = 0; i < 4; ++i) gTrace.push_back(key[i]);
    gTrace.push_back(iid);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    const uint32_t answer = Scripted(Off(self), 72);
    if ((answer & 0x300) != 0) *out = &gW->fk[gTableFake];
    return answer;
}

// A service whose slot 5 (id, id, out) hands the fake `gServiceHandsOut` to the caller (the acquiring pattern of the cached
// singletons); the ids are logged.
std::vector<std::pair<uint32_t, int>> gHandsOut; // first service id -> fake handed out (no entry: null)
uint32_t __fastcall AcquireFake(void *self, void *, uint32_t id1, uint32_t id2, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(5u << 8 | 3u);
    gTrace.push_back(id1);
    gTrace.push_back(id2);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(out)));
    for (const auto &e : gHandsOut) {
        if (e.first == id1) *out = &gW->fk[e.second];
    }
    return Scripted(Off(self), 5);
}

// A fake slot returning a float: the answer is a small integer from the script, divided by `scale`.
template <int S, int N> struct FloatSlot;
template <int S> struct FloatSlot<S, 0> {
    static float __fastcall F(void *self, void *)
    {
        Tick();
        gTrace.push_back(Off(self));
        gTrace.push_back(static_cast<uint32_t>(S) << 8);
        return static_cast<float>(static_cast<int16_t>(Scripted(Off(self), S))) / 64.0f;
    }
};
template <int S> struct FloatSlot<S, 1> {
    static float __fastcall F(void *self, void *, uint32_t a1)
    {
        Tick();
        gTrace.push_back(Off(self));
        gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
        gTrace.push_back(Norm(a1));
        return static_cast<float>(static_cast<int16_t>(Scripted(Off(self), S))) / 64.0f;
    }
};
template <int S> struct FloatSlot<S, 2> {
    static float __fastcall F(void *self, void *, uint32_t a1, uint32_t a2)
    {
        Tick();
        gTrace.push_back(Off(self));
        gTrace.push_back(static_cast<uint32_t>(S) << 8 | 2u);
        gTrace.push_back(Norm(a1));
        gTrace.push_back(Norm(a2));
        return static_cast<float>(static_cast<int16_t>(Scripted(Off(self), S))) / 64.0f;
    }
};

template <int S, int N> void UseFloat(void **vtable)
{
    vtable[S] = reinterpret_cast<void *>(&FloatSlot<S, N>::F);
}

uint32_t Bits(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

} // namespace
