// The lighting manager's test world (lanes 261108a and 261108b, scenegraph LG1 / LG2), for tests that run the light
// evaluation underneath (tests/sg_lighting_eval.cpp, which describes it, sg_lighting_passes.cpp, sg_lighting_sky.cpp,
// the builder fixture sg_lighting_build.h, and through tests/sg_lighting_tiles.h the vertex, walls and edges tests). Its definitions are in an anonymous namespace (one
// copy per including test file). The log, bit casts, Mix, Nice, NaN and AtBothPrecisions are tests/sg_lighting_common.h's.
#pragma once

#include "sg_lighting_common.h"

#include "gz/cIGZString.h"
#include "sg/cLightingManager.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

namespace {

using nTSSG::cLightingManager;
using nTSSG::cLightSet;
using nTSSG::cSunHeightMap;

constexpr uint32_t kObjectSize = 0x548;
constexpr uint32_t kSunMap = 0x248;
constexpr int kGridMax = 12;

alignas(16) uint8_t gObject[kObjectSize];
alignas(16) cLightSet gSets[3];
alignas(16) float gSky[4][0x8c / 4];
alignas(16) float gGrid[(kGridMax + 3) * (kGridMax + 3)];
const void *gMaps[3];

void Put(uint32_t offset, uint32_t value) { std::memcpy(gObject + offset, &value, 4); }
void PutF(uint32_t offset, float value) { Put(offset, U(value)); }

// --- fakes ---------------------------------------------------------------------------------------------------------

uint32_t gNameMatch; // bit i: set i's name equals the query
uint8_t gQueryName[0x20];
uint32_t __fastcall NameIsEqual(void *self, void *, const void *other, uint32_t caseSensitive)
{
    const uint32_t index = (P(self) - P(gSets)) / sizeof(cLightSet);
    gLog.push_back(0x11), gLog.push_back(index), gLog.push_back(other == gQueryName), gLog.push_back(caseSensitive);
    return (gNameMatch >> index & 1) | (Mix(index, gNameMatch) & 0xffffff00u); // only al counts
}
void *gNameVT[12];

struct FakeMap {
    void **vtable;
    uint32_t width, height, salt;
};
FakeMap gMapObjects[3];
uint32_t __fastcall MapWidth(FakeMap *self, void *, uint32_t level)
{
    gLog.push_back(0x26), gLog.push_back(self->salt), gLog.push_back(level);
    return self->width;
}
uint32_t __fastcall MapHeight(FakeMap *self, void *, uint32_t level)
{
    gLog.push_back(0x27), gLog.push_back(self->salt), gLog.push_back(level);
    return self->height;
}
uint32_t gTexelNaN = ~0u; // the component of every texel that is a NaN (payload 0x7fa5a5a5), else ~0
float *__fastcall MapTexel(FakeMap *self, void *, float *buffer, uint32_t x, uint32_t y, uint32_t level)
{
    gLog.push_back(0x38), gLog.push_back(self->salt), gLog.push_back(x), gLog.push_back(y), gLog.push_back(level);
    for (uint32_t c = 0; c < 3; ++c) {
        const uint32_t h = Mix(Mix(self->salt, x * 3 + c), y);
        buffer[c] = h % 7 == 0 ? Fl(h) : float(h & 0xffff) / 4096.0f; // now and then any bits (a NaN among them)
        if (c == gTexelNaN) buffer[c] = Fl(0x7fa5a5a5u);
    }
    return buffer;
}
void *gMapVT[39];

struct FakeTerrain {
    void **vtable;
    uint32_t salt, nanAt[2]; // vertex hashes that answer NaN (a payload from the hash), else ~0
    bool doubles;
};
FakeTerrain gTerrain;
double TerrainValue(int32_t i, int32_t j)
{
    const uint32_t h = Mix(Mix(gTerrain.salt, uint32_t(i)), uint32_t(j));
    if (h == gTerrain.nanAt[0] || h == gTerrain.nanAt[1]) return double(Fl(0x7fc00000u | (h & 0x803fffffu)));
    switch (h % 8) {
    case 0: return 0.0;
    case 1: return double(int32_t(h >> 20) % 5);
    default: {
        const double base = double(int32_t(h >> 8) % 20000) / 256.0;
        return gTerrain.doubles ? base + double(h & 0xff) * 0x1p-40 : base;
    }
    }
}
double __fastcall TerrainHeightSlot(FakeTerrain *, void *, const int32_t *vertex)
{
    gLog.push_back(0x62), gLog.push_back(uint32_t(vertex[0])), gLog.push_back(uint32_t(vertex[1])), gLog.push_back(uint32_t(vertex[2]));
    return TerrainValue(vertex[0], vertex[1]);
}
void *gTerrainVT[63];

void FillFakes()
{
    gNameVT[11] = reinterpret_cast<void *>(&NameIsEqual);
    gMapVT[26] = reinterpret_cast<void *>(&MapWidth);
    gMapVT[27] = reinterpret_cast<void *>(&MapHeight);
    gMapVT[38] = reinterpret_cast<void *>(&MapTexel);
    gTerrainVT[62] = reinterpret_cast<void *>(&TerrainHeightSlot);
}

// --- the world -----------------------------------------------------------------------------------------------------

void FillSet(std::mt19937 &gen, uint8_t *set)
{
    for (uint32_t i = 0; i < sizeof(cLightSet); i += 4) {
        const float f = Nice(gen, 2.0f);
        std::memcpy(set + i, &f, 4);
    }
    const uint32_t vt = P(gNameVT);
    std::memcpy(set, &vt, 4);
}

// The object and its sets for one case (both sides start from the same draw).
void Build(uint32_t seed)
{
    std::mt19937 gen(seed);
    for (auto &b : gObject) b = uint8_t(gen());
    for (auto &s : gSets) FillSet(gen, reinterpret_cast<uint8_t *>(&s));
    FillSet(gen, gObject + 0x2d0);
    const unsigned sets = gen() % 4;
    Put(0x98, P(gSets)), Put(0x9c, P(gSets + sets)), Put(0xa0, P(gSets + 3));
    static const uint32_t kIndexes[] = {0, 1, 2, 3, 0xffffffff, 0x80000000};
    Put(0xd0, kIndexes[gen() % 6]);
    const unsigned maps = gen() % 4;
    for (unsigned i = 0; i < 3; ++i) {
        FakeMap &m = gMapObjects[i];
        m.vtable = gMapVT;
        static const uint32_t kSizes[] = {1, 2, 3, 16, 64, 256, 0, 0x80000001u};
        m.width = gen() % 6 ? 1 + gen() % 300 : kSizes[gen() % 8];
        m.height = gen() % 6 ? 1 + gen() % 300 : kSizes[gen() % 8];
        m.salt = gen();
        gMaps[i] = gen() % 4 ? &m : nullptr;
    }
    Put(0xa4, P(gMaps)), Put(0xa8, P(gMaps + maps)), Put(0xac, P(gMaps + 3));
    PutF(0x1ec, Nice(gen, 2.0f)), PutF(0x1f0, Nice(gen, 2.0f));
    for (auto &e : gSky) for (auto &f : e) f = Nice(gen, 4.0f);
    const unsigned sky = gen() % 5;
    Put(0x1f4, P(gSky)), Put(0x1f8, P(gSky) + sky * 0x8c), Put(0x1fc, P(gSky) + 4 * 0x8c);
    gTerrain = {gTerrainVT, uint32_t(gen()), {~0u, ~0u}, gen() % 2 == 0};
    Put(0x1c, P(&gTerrain));
    static const float kScales[] = {1.0f, 0.5f, 0.25f, 2.0f, 0.1f, 1.0f / 3.0f};
    PutF(0x200, gen() % 3 ? kScales[gen() % 6] : 0.05f + float(gen() % 1000) / 250.0f);
    Put(0x20c, gen() % 8 ? gen() % 40 : uint32_t(int32_t(gen() % 5) - 2)), Put(0x210, gen() % 8 ? gen() % 40 : uint32_t(int32_t(gen() % 5) - 2));
    gObject[0x69] = uint8_t(gen() % 4 ? 1 + gen() % 255 : 0);
    // the sun height map: a W x H grid (rows of W), cells per world unit 0.25..8
    const int32_t w = 1 + gen() % kGridMax, h = 1 + gen() % kGridMax;
    for (auto &f : gGrid) f = Nice(gen, 8.0f);
    Put(kSunMap + 0x18, P(gGrid)), Put(kSunMap + 0x24, uint32_t(w)), Put(kSunMap + 0x28, uint32_t(h));
    static const float kGridScales[] = {1.0f, 0.25f, 0.5f, 2.0f, 8.0f, 0.3f};
    PutF(kSunMap + 0x30, kGridScales[gen() % 6]);
    PutF(kSunMap + 0x4c, Nice(gen, 2.0f));
    PutF(0x2ac, gen() % 4 ? Nice(gen, 1.0f) : 0.5f);
}

// One or two float words of the object or a set made NaNs (inputs the case reads), or none.
void MaybeNaN(std::mt19937 &gen, float *const *places, unsigned count)
{
    const unsigned roll = gen() % 8;
    if (roll < 1 || !count) return;
    if (roll < 3) *places[gen() % count] = NaN(gen);
    else if (roll < 5) *places[gen() % count] = NaN(gen), *places[gen() % count] = NaN(gen);
}

cLightingManager *Ours() { return reinterpret_cast<cLightingManager *>(gObject); }
cSunHeightMap *OurSun() { return reinterpret_cast<cSunHeightMap *>(gObject + kSunMap); }

const cLightSet &Current()
{
    uint32_t begin, end, index;
    std::memcpy(&begin, gObject + 0x98, 4), std::memcpy(&end, gObject + 0x9c, 4), std::memcpy(&index, gObject + 0xd0, 4);
    if (index < (end - begin) / sizeof(cLightSet)) return reinterpret_cast<const cLightSet *>(uintptr_t(begin))[index];
    return *reinterpret_cast<const cLightSet *>(gObject + 0x2d0);
}

Blob End(const uint32_t *values, unsigned count)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (unsigned i = 0; i < count; ++i) o.push_back(values[i]);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        uint32_t w;
        std::memcpy(&w, gObject + i, 4);
        o.push_back(w);
    }
    const auto *s = reinterpret_cast<const uint32_t *>(gSets);
    for (size_t i = 0; i < sizeof(gSets) / 4; ++i) o.push_back(s[i]);
    return o;
}

} // namespace
