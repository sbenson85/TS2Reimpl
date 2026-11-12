// The lighting tests' shared helpers (scenegraph LG1-LG4; lane 261109s): the log and its writers, the bit casts, the
// hashes, the float draws, the runners at both x87 precisions and the saved game globals. tests/sg_lighting_world.h
// sits on it; tests/sg_lighting_fakes.h holds the fake objects. Its definitions are in an anonymous namespace (one copy
// per including test file). The two hashes Mix and Mix2 are different functions on purpose: each test keeps the one its
// draws were made with (so the cases stay the same).
#pragma once

#include "difftest.h"
#include "ts_services_fixture.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace {

using svcfx::Blob;

Blob gLog;
void L(uint32_t w) { gLog.push_back(w); }

uint32_t U(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Fl(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
uint32_t P(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }
void LF(const float *p, int n)
{
    for (int i = 0; i < n; ++i) L(U(p[i]));
}

// FNV-1a of a text (0 for none).
uint32_t Hash(const char *s)
{
    uint32_t h = 0x811c9dc5u;
    for (; s && *s; ++s) h = (h ^ uint8_t(*s)) * 0x01000193u;
    return s ? h : 0;
}

// The world's hash (tests/sg_lighting_world.h and the tests on it).
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}
// The builders' hash (tests/sg_lighting_rebuild.cpp, sg_lighting_room_portals.cpp, sg_lighting_config.cpp and the shared
// lot value).
uint32_t Mix2(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b9u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    h ^= h >> 15, h *= 0xc2b2ae35u, h ^= h >> 13;
    return h;
}

template <class T> T Word(const uint8_t *base, uint32_t offset)
{
    T v;
    std::memcpy(&v, base + offset, sizeof v);
    return v;
}
void PutWord(uint8_t *base, uint32_t offset, uint32_t value) { std::memcpy(base + offset, &value, 4); }
void PutFloat(uint8_t *base, uint32_t offset, float f) { std::memcpy(base + offset, &f, 4); }

// A float from the families the cases need, scaled around `scale`.
float Nice(std::mt19937 &gen, float scale = 1.0f)
{
    switch (gen() % 20) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return float(int(gen() % 9) - 4);
    case 3: return Fl((gen() & 0x807fffffu) | (uint32_t(127 - 30 + gen() % 60) << 23)); // a wide exponent
    case 4: return gen() % 8 ? Fl((gen() & 0x80000000u) | (1 + gen() % 0x7fffffu)) : Fl(0x7f800000u | (gen() & 0x80000000u)); // denormal or (rarely) infinite
    default: return (float(int32_t(gen() & 0xffffff) - 0x800000) / float(0x800000)) * scale;
    }
}

float NaN(std::mt19937 &gen)
{
    return Fl(0x7f800000u | (gen() & 0x80000000u) | (gen() % 2 ? 0x400000u : 0u) | (1 + gen() % 0x3fffffu));
}

// Like Nice, with NaNs and infinities a family each.
float Wide(std::mt19937 &gen, float scale)
{
    switch (gen() % 24) {
    case 0: return NaN(gen);
    case 1: return 0.0f;
    case 2: return -0.0f;
    case 3: return float(int(gen() % 9) - 4);
    case 4: return Fl((gen() & 0x807fffffu) | (uint32_t(127 - 30 + gen() % 60) << 23));
    case 5: return Fl((gen() & 0x80000000u) | (1 + gen() % 0x7fffffu)); // denormal
    case 6: return Fl(0x7f800000u | (gen() & 0x80000000u));             // infinite
    default: return (float(int32_t(gen() & 0xffffff) - 0x800000) / float(0x800000)) * scale;
    }
}

// `run(seed, label)` for seeds 0..cases at the x87's 53-bit precision, then for seeds 0x100000.. at 24-bit.
template <class Case> void AtBothPrecisions(difftest::Context &ctx, const char *what, uint32_t cases, Case &&run)
{
    char label[64];
    std::snprintf(label, sizeof(label), "%s at 53-bit", what);
    for (uint32_t seed = 0; seed < cases; ++seed) run(seed, label);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    std::snprintf(label, sizeof(label), "%s at 24-bit", what);
    for (uint32_t seed = 0; seed < cases; ++seed) run(seed + 0x100000, label);
}

// Each seed's two sides at the x87's 53-bit, then 24-bit precision: `side(draw, s)` gives side s's outcome (0 the
// original, 1 ours) for the case drawn from `draw` (the seed, plus 0x100000 at 24-bit; the reported seed is the plain
// one), and the two are compared. `count`: one more ctx.cases per seed besides Same's own.
template <class Side> void SidesAtBothPrecisions(difftest::Context &ctx, const char *what, uint32_t cases, bool count, Side &&side)
{
    for (int precision = 0; precision < 2; ++precision) {
        std::unique_ptr<difftest::X87Precision> single;
        if (precision) single = std::make_unique<difftest::X87Precision>(difftest::X87Pc::Bits24);
        char label[64];
        std::snprintf(label, sizeof label, "%s at %s", what, precision ? "24-bit" : "53-bit");
        for (uint32_t seed = 0; seed < cases; ++seed) {
            Blob outcome[2];
            for (int s = 0; s < 2; ++s) outcome[s] = side(seed + uint32_t(precision) * 0x100000u, s);
            if (count) ++ctx.cases;
            svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
        }
    }
}

// Game globals a test draws: saved by Save, restored on destruction, appended to an outcome by Append.
struct SavedGlobals {
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> saved;
    SavedGlobals() = default;
    SavedGlobals(const SavedGlobals &) = delete;
    SavedGlobals &operator=(const SavedGlobals &) = delete;
    void Save(uint32_t rva, size_t bytes)
    {
        const uint8_t *at = GameAddress<uint8_t>(rva);
        saved.push_back({rva, std::vector<uint8_t>(at, at + bytes)});
    }
    ~SavedGlobals()
    {
        for (auto &s : saved) std::memcpy(GameAddress<uint8_t>(s.first), s.second.data(), s.second.size());
    }
    void Append(Blob &o) const
    {
        for (auto &s : saved)
            for (size_t i = 0; i < s.second.size(); ++i) o.push_back(GameAddress<uint8_t>(s.first)[i]);
    }
};

} // namespace
