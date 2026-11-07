// Differential test for nTSCASCore::cTSCASBodyPartSystem's AverageWeightedOffset (slot 80), lane JA. The system's
// entry helper 0xcf04b0 and the weight lookup 0xcf29b0 are fakes returning a mesh entry and weights built per case.
// See cas_body_part_system.h for the fixture.
#include "cas_body_part_system.h"

namespace {

using namespace bpt;

struct Vertex {
    float pos[3];
    float pad;
    float base[3];
    uint8_t rest[0x68 - 0x1c];
};
static_assert(sizeof(Vertex) == 0x68, "vertex");
struct Group {
    Vertex *first, *last, *end;
};
struct Entry {
    Group *groupsFirst, *groupsLast;
    uint8_t pad[0x58 - 8];
    uint32_t meshA, meshB;
};
struct Weights {
    uint8_t pad[0x44];
    float *first, *last;
};

Vertex gVertices[4][6];
Group gGroups[4];
Entry gEntry;
float gWeightValues[32];
Weights gWeights;

uint8_t *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, partId});
    return reinterpret_cast<uint8_t *>(&gEntry);
}
Weights *__fastcall F_cf29b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t zero, uint32_t morph)
{
    gEvents.push_back({0xcf29b0, Rel(self), a, b, id, zero, morph});
    return &gWeights;
}

float RandomFloat(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return static_cast<float>(static_cast<int>(rng() % 21) - 10);
    case 1: return 0.0f;
    default: return (static_cast<float>(rng() % 200001) - 100000.0f) / 997.0f;
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_AverageWeightedOffset, 0xce16d0)
{
    std::mt19937 rng(0xce16d0);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, float *)>(0xce16d0);
    gEvents.reserve(1024);
    VTable vt = CopyVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    Redirect f2(ctx, 0xcf29b0, reinterpret_cast<const void *>(&F_cf29b0));
    const float choices[] = {1.0f, 1.0f, 0.5f, 0.0f, 0.99999994f, 1.0000001f, -1.0f};
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        // Groups: 0 to 4 (0 and 1 first), each 0 to 6 vertices.
        const int groups = i < 200 ? i % 2 : static_cast<int>(rng() % 5);
        int vertices = 0;
        for (int g = 0; g < 4; ++g) {
            const int n = static_cast<int>(rng() % 7);
            gGroups[g] = {gVertices[g], gVertices[g] + n, gVertices[g] + 6};
            if (g < groups) vertices += n;
            for (auto &v : gVertices[g]) {
                for (int k = 0; k < 3; ++k) {
                    v.pos[k] = RandomFloat(rng);
                    v.base[k] = rng() % 3 == 0 ? v.pos[k] : RandomFloat(rng);
                }
                v.pad = RandomFloat(rng);
            }
        }
        gEntry.groupsFirst = gGroups;
        gEntry.groupsLast = gGroups + groups;
        gEntry.meshA = static_cast<uint32_t>(rng());
        gEntry.meshB = static_cast<uint32_t>(rng());
        // Weights: none (when asked), exactly as many as vertices, or a few more; never fewer (that throws).
        const int extra = rng() % 3 == 0 ? 0 : static_cast<int>(rng() % 4);
        const int weights = rng() % 8 == 0 ? 0 : vertices + extra;
        const bool allOnes = rng() % 4 == 0;
        for (auto &w : gWeightValues) w = allOnes ? 1.0f : choices[rng() % 7];
        gWeights.first = gWeightValues;
        gWeights.last = gWeightValues + weights;
        const uint32_t morph = rng() % 5 == 0 ? 0u : static_cast<uint32_t>(rng() % 3 == 0 ? rng() : 1 + rng() % 4);
        const uint32_t a = rng() % 6, b = static_cast<uint32_t>(rng()), c = rng() % 4;
        Compare(ctx, "AverageWeightedOffset", sc, vt, [&](World &w, bool original) -> uint64_t {
            float out[4] = {-7.0f, -7.0f, -7.0f, -7.0f};
            const uint8_t r = original ? orig(w.S(), a, b, c, morph, out) : w.S()->AverageWeightedOffset(a, b, c, morph, out);
            uint32_t bits[4];
            std::memcpy(bits, out, sizeof(bits));
            gEvents.push_back({0xfff0, bits[0], bits[1], bits[2], bits[3]});
            return r;
        });
    }
}
