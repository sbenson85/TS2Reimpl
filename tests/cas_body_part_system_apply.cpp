// Differential test for nTSCASCore::cTSCASBodyPartSystem's ApplyPart (slot 24), lane JA. The part, the system's entry
// helpers and the slots it calls are fakes answering from a script (a hash of the case's seed and the number of calls
// recorded so far). See cas_body_part_system.h for the fixture.
#include "cas_body_part_system.h"

namespace {

using namespace bpt;
using nTSCASCore::IdVector;

uint32_t gScriptSeed;

uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

struct FakeObject {
    const void *const *vtable;
    uint32_t id;
};
FakeObject gParts[2];
std::array<const void *, 64> gPartVt;
uint8_t gEntries[3][0x60];

uint32_t EntryIndex(const uint8_t *e)
{
    for (int i = 0; i < 3; ++i)
        if (e == gEntries[i]) return 0x100u + i;
    return 0xdead;
}

void *__fastcall Slot52(void *self, void *, uint32_t a, uint32_t b, uint32_t id)
{
    gEvents.push_back({52, Rel(self), a, b, id});
    return Pick() % 5 == 0 ? nullptr : &gParts[Pick() % 2];
}
void __fastcall Slot31(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({31, Rel(self), a, b, c, d});
}
void __fastcall PartIds(FakeObject *self, void *, IdVector *out)
{
    gEvents.push_back({0x2038, self->id, static_cast<uint32_t>(out->size())});
    const uint32_t n = Pick() % 4;
    for (uint32_t k = 0; k < n; ++k) {
        gEvents.push_back({0x2039, k});
        out->push_back(Pick() % 6);
    }
}
uint32_t __fastcall PartFirstId(FakeObject *self, void *)
{
    gEvents.push_back({0x2039, self->id});
    return Pick() % 6;
}
void *__fastcall PartEntry(FakeObject *self, void *, uint32_t id)
{
    gEvents.push_back({0x2030, self->id, id});
    return Pick() % 4 == 0 ? nullptr : self;
}
uint8_t *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, partId});
    uint8_t *e = gEntries[Pick() % 3];
    int32_t index;
    std::memcpy(&index, e + 0x50, 4);
    gEvents.push_back({0xcf04b1, static_cast<uint32_t>(index)}); // what the entry held at +0x50
    return e;
}
void __fastcall F_cfbbb0(void *self, void *, uint8_t *e)
{
    int32_t index;
    std::memcpy(&index, e + 0x50, 4);
    gEvents.push_back({0xcfbbb0, Rel(self), EntryIndex(e), static_cast<uint32_t>(index)});
}
void __fastcall F_ce7430(void *self, void *, uint8_t *e)
{
    gEvents.push_back({0xce7430, Rel(self), EntryIndex(e)});
}
void __fastcall F_cfb180(void *self, void *, uint8_t *e, uint32_t a, uint32_t b)
{
    gEvents.push_back({0xcfb180, Rel(self), EntryIndex(e), a, b});
    const int32_t index = static_cast<int32_t>(Pick() % 7);
    std::memcpy(e + 0x50, &index, 4);
}

uint32_t gIds[2][8];

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ApplyPart, 0xcfaec0)
{
    std::mt19937 rng(0xcfaec0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t,
                                                uint8_t, uint8_t)>(0xcfaec0);
    gEvents.reserve(8192);
    VTable vt = CopyVTable(ctx);
    vt.slots[52] = reinterpret_cast<const void *>(&Slot52);
    vt.slots[31] = reinterpret_cast<const void *>(&Slot31);
    gPartVt = {};
    gPartVt[48] = reinterpret_cast<const void *>(&PartEntry);
    gPartVt[56] = reinterpret_cast<const void *>(&PartIds);
    gPartVt[57] = reinterpret_cast<const void *>(&PartFirstId);
    for (int i = 0; i < 2; ++i) gParts[i] = {gPartVt.data(), 0x10u + i};
    Env env(ctx);
    Redirect f1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect f2(ctx, 0xcfbbb0, reinterpret_cast<const void *>(&F_cfbbb0));
    Redirect f3(ctx, 0xce7430, reinterpret_cast<const void *>(&F_ce7430));
    Redirect f4(ctx, 0xcfb180, reinterpret_cast<const void *>(&F_cfb180));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        gScriptSeed = static_cast<uint32_t>(rng());
        // Lists A (+0x60) and B (+0x6c): 0 to 3 ids each.
        const int offsets[2] = {0x60, 0x6c};
        for (int l = 0; l < 2; ++l) {
            for (auto &x : gIds[l]) x = rng() % 8;
            uint32_t *first = gIds[l], *last = gIds[l] + rng() % 4;
            std::memcpy(sc.bytes + kIfaceOffset + offsets[l], &first, 4);
            std::memcpy(sc.bytes + kIfaceOffset + offsets[l] + 4, &last, 4);
        }
        for (auto &e : gEntries) {
            const int32_t index = static_cast<int32_t>(rng() % 5);
            std::memcpy(e + 0x50, &index, 4);
        }
        const uint32_t kinds[] = {0, 3, 7, 7, 5};
        const uint32_t kind = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : kinds[rng() % 5];
        const uint32_t key = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        const uint32_t id = rng() % 3 == 0 ? 0u : static_cast<uint32_t>(rng() % 4);
        uint8_t f[7];
        for (auto &x : f) x = rng() % 2 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1);
        Compare(ctx, "ApplyPart", sc, vt, [&](World &w, bool original) -> uint64_t {
            for (auto &e : gEntries) std::memset(e + 0x50, 0, 4);
            if (original) orig(w.S(), kind, key, id, f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
            else w.S()->ApplyPart(kind, key, id, f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
            for (auto &e : gEntries) {
                uint32_t index;
                std::memcpy(&index, e + 0x50, 4);
                gEvents.push_back({0xfff0, index});
            }
            return 0;
        });
    }
}
