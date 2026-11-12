// Differential tests for the queries around the heights of nTSWorld's helper module (lane RZ,
// src/world/WorldHeightQueries.cpp, 0x7e4250-0x7e5b50). Both sides run against the grid world of tests/worldgrid.h, the
// catalogue of tests/worldfx.h and the fakes below; the callees in WU-2's predicate range (0x7df000-0x7e3800), which both
// sides still call as the image's, are stand-ins patched over the image (one patch serves both sides: ours calls them
// through OriginalFunction). Every fake call and stand-in call is logged with the content its pointer arguments point
// at; a log word inside the thread's stack (a pointer to a caller's local) is cleared, since the two sides' locals sit at
// different addresses. The float results are compared as the doubles left in st0, at the harness's 53-bit precision and
// again at the game's 24-bit.
#include "difftest.h"
#include "code_patch.h"
#include "worldgrid.h"

#include "world/WorldHeights.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <random>
#include <string>
#include <vector>

using worldfx::Fake;
using worldfx::gLog;
using worldfx::gWorldDb;
using worldgrid::Bits;
using worldgrid::FromBits;
using worldgrid::gWorld;
using worldgrid::RandomCoord;
using worldgrid::RandomLevel;
using worldgrid::RandomWorld;

namespace {

uint32_t P(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }
uint32_t W(float f) { return Bits(f); }
uint32_t LevelWord(int32_t level) { return uint32_t(level); }

// ---- the outcome of one call ----
ULONG_PTR gStackLow, gStackHigh;
void StackRange() { GetCurrentThreadStackLimits(&gStackLow, &gStackHigh); }

struct Result {
    uint32_t w[8];
    uint32_t ret[2];
    std::string str;
    std::vector<uint32_t> log;
};

uint32_t gSeed, gCall;

template <class F> Result Go(F &&f)
{
    Result o;
    for (auto &w : o.w) w = 0xc5c5c5c5u;
    o.ret[0] = o.ret[1] = 0;
    gLog.clear();
    gCall = 0;
    f(o);
    o.log = gLog;
    for (auto &v : o.log)
        if (v >= gStackLow && v < gStackHigh) v = 0;
    return o;
}

bool Same(const Result &a, const Result &b)
{
    return std::memcmp(a.w, b.w, sizeof(a.w)) == 0 && a.ret[0] == b.ret[0] && a.ret[1] == b.ret[1] && a.str == b.str &&
           a.log == b.log;
}

// Where the two outcomes first differ.
std::string Describe(const Result &a, const Result &b)
{
    char buf[400];
    for (int i = 0; i < 8; ++i)
        if (a.w[i] != b.w[i]) {
            std::snprintf(buf, sizeof(buf), "out word %d: %08x vs %08x", i, a.w[i], b.w[i]);
            return buf;
        }
    if (a.ret[0] != b.ret[0] || a.ret[1] != b.ret[1]) {
        std::snprintf(buf, sizeof(buf), "result %08x%08x vs %08x%08x", a.ret[1], a.ret[0], b.ret[1], b.ret[0]);
        return buf;
    }
    if (a.str != b.str) return "string '" + a.str + "' vs '" + b.str + "'";
    size_t i = 0;
    while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
    int n = std::snprintf(buf, sizeof(buf), "log sizes %u / %u, first difference at %u:", unsigned(a.log.size()),
                          unsigned(b.log.size()), unsigned(i));
    const size_t from = i > 3 ? i - 3 : 0;
    for (size_t k = from; k < i + 6 && k < a.log.size() && n < 180; ++k) n += std::snprintf(buf + n, sizeof(buf) - n, " %x", a.log[k]);
    n += std::snprintf(buf + n, sizeof(buf) - n, " | ");
    for (size_t k = from; k < i + 6 && k < b.log.size() && n < 380; ++k) n += std::snprintf(buf + n, sizeof(buf) - n, " %x", b.log[k]);
    return buf;
}

#define EXPECT_SAME(a, b, fmt, ...)                                                                                    \
    do {                                                                                                               \
        ++ctx.cases;                                                                                                   \
        if (!Same(a, b)) ctx.Fail(fmt ": %s", __VA_ARGS__, Describe(a, b).c_str());                                    \
    } while (0)

void StoreDouble(Result &o, double d) { std::memcpy(o.ret, &d, sizeof(d)); }

// ---- the WU-2 stand-ins: logged as fake #700, slot = the RVA's low 16 bits ----
uint32_t Mix(uint32_t h, uint32_t v)
{
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    h *= 0x85ebca6bu;
    return h ^ (h >> 13);
}
uint32_t gPercent = 50; // how often a stand-in predicate holds, varied by case

uint32_t Stand(uint32_t rva, std::initializer_list<uint32_t> words)
{
    gLog.push_back(700);
    gLog.push_back(rva & 0xffff);
    gLog.push_back(uint32_t(words.size()));
    uint32_t h = Mix(Mix(gSeed, rva), ++gCall);
    for (uint32_t w : words) {
        gLog.push_back(w);
        h = Mix(h, w);
    }
    return h;
}
bool Holds(uint32_t h) { return (h >> 9) % 100 < gPercent; }
// A bool in al with junk above it (the callers read al).
uint32_t Answer(uint32_t h) { return (h & 0xffff0000u) | (Holds(h) ? 1u : 0u) | ((h & 0x7f) << 9); }

float gSnapped[6];
bool gSnapWrite;
uint32_t __cdecl Stand7e2de0(const float *centre, float *out)
{
    const uint32_t h = Stand(0x7e2de0, {W(centre[0]), W(centre[1]), W(centre[2])});
    if (gSnapWrite) std::memcpy(out, gSnapped, sizeof(gSnapped));
    return Answer(h);
}
template <uint32_t Rva> uint32_t __cdecl StandWord(const uint32_t *p) { return Answer(Stand(Rva, {p[0]})); }
template <uint32_t Rva> uint32_t __cdecl StandTile3(const uint32_t *p) { return Answer(Stand(Rva, {p[0], p[1], p[2]})); }
template <uint32_t Rva> uint32_t __cdecl StandQuadrant(const uint32_t *p)
{
    return Answer(Stand(Rva, {p[0], p[1], p[2], p[3] & 0xff}));
}
template <uint32_t Rva> uint32_t __cdecl StandPointer(const void *p) { return Answer(Stand(Rva, {P(p)})); }
uint32_t __cdecl Stand7e06e0(const uint32_t *p, uint32_t a, uint32_t b, uint32_t c)
{
    return Answer(Stand(0x7e06e0, {p[0], p[1], p[2], a, b, c}));
}
uint32_t __cdecl Stand7e00f0(const void *a, uint32_t b) { return Answer(Stand(0x7e00f0, {P(a), b})); }

void *gLookupObject; // what 0x7e3730 finds (or null)
uint32_t __cdecl Stand7e3730(uint32_t key)
{
    const uint32_t h = Stand(0x7e3730, {key});
    return Holds(h) ? P(gLookupObject) : 0u;
}
uint32_t __cdecl Stand7e37e0(uint32_t key, uint32_t n) { return Stand(0x7e37e0, {key, n}); }

// 0x7e29a0(out, bit, tile, context): the corner of the quadrant `bit` of the tile, as out[0..1] floats and out[2] a level.
float gCornerBase; // 0: corners on the grid; else shifted off it
uint32_t __cdecl Stand7e29a0(float *out, uint32_t bit, const int32_t *tile, const uint32_t *context)
{
    const uint32_t h = Stand(0x7e29a0, {bit, uint32_t(tile[0]), uint32_t(tile[1]), uint32_t(tile[2]), context[0]});
    out[0] = float(tile[0]) + gCornerBase + float(int32_t(h % 5) - 2) * 0.75f;
    out[1] = float(tile[1]) + gCornerBase + float(int32_t((h >> 4) % 5) - 2) * 0.75f;
    const int32_t level = tile[2] + int32_t((h >> 8) % 3) - 1;
    std::memcpy(&out[2], &level, 4);
    return h;
}
// 0x7df3f0(placement, object, deck, point, context): fills the three-word placement.
uint32_t __cdecl Stand7df3f0(uint32_t *placement, void *object, uint32_t deck, const uint32_t *point, uint32_t context)
{
    const uint32_t h = Stand(0x7df3f0, {P(object), deck, point[0], point[1], point[2], context});
    placement[0] = h;
    placement[1] = Mix(h, 1);
    placement[2] = Mix(h, 2);
    return h;
}
uint32_t __cdecl Stand7e2130(void *object, const uint32_t *placement)
{
    return Answer(Stand(0x7e2130, {P(object), placement[0], placement[1], placement[2]}));
}

// All of WU-2's callees of the range, patched for a test's lifetime.
struct Stands {
    std::vector<std::unique_ptr<shape::CodePatch>> patches;
    void Add(uint32_t rva, const void *fn) { patches.push_back(std::make_unique<shape::CodePatch>(rva, fn)); }
    Stands()
    {
        Add(0x7e2de0, reinterpret_cast<const void *>(&Stand7e2de0));
        Add(0x7e24f0, reinterpret_cast<const void *>(&StandWord<0x7e24f0>));
        Add(0x7e2520, reinterpret_cast<const void *>(&StandWord<0x7e2520>));
        Add(0x7e06e0, reinterpret_cast<const void *>(&Stand7e06e0));
        Add(0x7e0430, reinterpret_cast<const void *>(&StandQuadrant<0x7e0430>));
        Add(0x7e2400, reinterpret_cast<const void *>(&StandTile3<0x7e2400>));
        Add(0x7e3730, reinterpret_cast<const void *>(&Stand7e3730));
        Add(0x7e1820, reinterpret_cast<const void *>(&StandPointer<0x7e1820>));
        Add(0x7e20f0, reinterpret_cast<const void *>(&StandPointer<0x7e20f0>));
        Add(0x7e15a0, reinterpret_cast<const void *>(&StandPointer<0x7e15a0>));
        Add(0x7e1d40, reinterpret_cast<const void *>(&StandPointer<0x7e1d40>));
        Add(0x7dfdc0, reinterpret_cast<const void *>(&StandPointer<0x7dfdc0>));
        Add(0x7e0600, reinterpret_cast<const void *>(&StandPointer<0x7e0600>));
        Add(0x7e00f0, reinterpret_cast<const void *>(&Stand7e00f0));
        Add(0x7e29a0, reinterpret_cast<const void *>(&Stand7e29a0));
        Add(0x7df3f0, reinterpret_cast<const void *>(&Stand7df3f0));
        Add(0x7e2130, reinterpret_cast<const void *>(&Stand7e2130));
        Add(0x7dfc00, reinterpret_cast<const void *>(&StandQuadrant<0x7dfc00>));
        Add(0x7dfa70, reinterpret_cast<const void *>(&StandQuadrant<0x7dfa70>));
        Add(0x7e0920, reinterpret_cast<const void *>(&StandQuadrant<0x7e0920>));
        Add(0x7e37e0, reinterpret_cast<const void *>(&Stand7e37e0));
    }
    ~Stands()
    {
        while (!patches.empty()) patches.pop_back();
    }
};

// ---- the fakes ----
Fake gPartitions, gRoofs, gPools, gTerrain, gGraph, gNode, gEdge[8], gVertex[8], gProduct[3];
Fake gObject, gFace, gManager, gObjects, gTileObject[4], gAllocator;
float gVertexPos[8][3];

uint32_t Mixed(std::initializer_list<uint32_t> words)
{
    uint32_t h = gSeed;
    for (uint32_t w : words) h = Mix(h, w);
    return h;
}

// A tile pointer's content after the call's entry: x, y, level and the quadrant byte.
const uint32_t *LogTile4(uint32_t pointer)
{
    const uint32_t *t = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    gLog.push_back(t[0]);
    gLog.push_back(t[1]);
    gLog.push_back(t[2]);
    gLog.push_back(t[3] & 0xff);
    return t;
}
const uint32_t *LogWords(uint32_t pointer, int n)
{
    const uint32_t *t = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    for (int i = 0; i < n; ++i) gLog.push_back(t[i]);
    return t;
}

// The world DB, partitions, roofs, pools and terrain.
uint32_t gDeck;
uint32_t DeckFoundation(Fake *, const uint32_t *a)
{
    const uint32_t *t = LogTile4(a[0]);
    return Mixed({t[0], t[1], t[2], t[3] & 0xff, gDeck}) % 7;
}
float PoolSurface(Fake *, const uint32_t *a)
{
    const uint32_t *p = LogWords(a[0], 3);
    return float(Mixed({p[0], p[1], p[2]}) % 1000) * 0.0137f - 1.5f;
}
uint32_t gRoofMode;
float RoofAt(Fake *, const uint32_t *a)
{
    const uint32_t h = Mixed({a[0], a[1], a[3], gRoofMode});
    if (a[2] != 0 && (h & 8)) *reinterpret_cast<int32_t *>(uintptr_t(a[2])) = int32_t(h % 4);
    switch (gRoofMode) {
    case 0: return 0.0f;
    case 1: return FromBits(0x80000000u);
    case 2: return FromBits((h & 0x80000000u) | (h % 40u));
    case 3: return FromBits(0x3f800000u + (h % 3) * 0x01000000u);
    default: return float(h % 3000) * 0.01f - 5.0f;
    }
}
float TerrainAt(Fake *, const uint32_t *a) { return float(Mixed({a[0], a[1], 0x7e77}) % 900) * 0.01f - 3.0f; }
float GroundElevation(Fake *, const uint32_t *a) { return float(Mixed({a[0], a[1], 0x6e6e}) % 700) * 0.0125f - 2.0f; }
uint32_t gRoofHere;
uint32_t RoofHere(Fake *, const uint32_t *a)
{
    const uint32_t *p = LogWords(a[0], 3);
    return (Mixed({p[0], p[1], p[2], 0x3737}) % 100 < gRoofHere) ? 0x5501u : 0x5500u;
}
float RoofHeight(Fake *, const uint32_t *a)
{
    const uint32_t *p = LogWords(a[0], 3);
    return float(Mixed({p[0], p[1], p[2], 0x2222}) % 2000) * 0.0071f;
}
uint32_t gIds[6];
uint32_t PartitionAt(Fake *, const uint32_t *a)
{
    const uint32_t *s = LogWords(a[0], 6);
    return gIds[Mixed({s[0], s[1], s[2], s[3], s[4], s[5]}) % 6];
}
float PartitionKeyValue(Fake *, const uint32_t *a) { return float(Mixed({a[0], 0x8484}) % 500) * 0.013f - 0.5f; }
uint32_t gMaskOdds;
uint32_t PartitionBit(Fake *, const uint32_t *a)
{
    const uint32_t *q = LogWords(a[0], 4);
    return (Mixed({q[0], q[1], q[2], q[3]}) % 100 < gMaskOdds) ? 0x3301u : 0x3300u;
}

// The room records (partitions slot 76) and the room outline (slots 77, 40, 50).
struct TileRecord {
    int32_t x, y, level;
    uint8_t quadrants, pad[3];
};
TileRecord gRecords[16];
int gRecordCount;
bool gRecordNull;
uint32_t RoomRecords(Fake *, const uint32_t *a)
{
    TileRecord **vec = reinterpret_cast<TileRecord **>(uintptr_t(a[1]));
    TileRecord *begin = gRecordNull ? nullptr : gRecords;
    vec[0] = begin;
    vec[1] = begin ? begin + gRecordCount : nullptr;
    vec[2] = begin ? gRecords + 16 : nullptr;
    return 0x7676;
}
int32_t gFirst[3];
uint8_t gFirstQuadrant;
uint32_t RoomFirst(Fake *, const uint32_t *a)
{
    uint8_t *out = reinterpret_cast<uint8_t *>(uintptr_t(a[0]));
    std::memcpy(out, gFirst, 12);
    out[12] = gFirstQuadrant;
    return a[0];
}
bool gGraphNull, gNodeNull, gStartNull;
int gStart;
uint32_t GraphNode(Fake *, const uint32_t *a)
{
    LogWords(a[0], 6);
    return gNodeNull ? 0u : P(&gNode);
}

// The catalogue's products (slot 26) and their flag 11.
uint32_t gProductOdds;
uint32_t ProductFor(Fake *, const uint32_t *a)
{
    const uint32_t h = Mixed({a[0], a[1], 0x2626});
    return h % 100 < gProductOdds ? P(&gProduct[h % 3]) : 0u;
}

// Objects: the attributes (slot 13) by index.
int32_t gAttr3b, gAttr2a, gAttr52;
uint32_t ObjectAttribute(Fake *, const uint32_t *a)
{
    const uint32_t junk = Mixed({a[0], 0x1313}) & 0xffff0000u;
    switch (a[0]) {
    case 0x3b: return junk | (uint32_t(gAttr3b) & 0xffff);
    case 0x2a: return junk | (uint32_t(gAttr2a) & 0xffff);
    case 0x52: return junk | (uint32_t(gAttr52) & 0xffff);
    default: return junk | 0x5a5a;
    }
}
uint32_t TileObjectAttribute(Fake *self, const uint32_t *a) { return Mixed({self->id, a[0], 0x6262}) & (gMaskOdds > 50 ? 0xffffu : 0xfffeu); }
uint32_t ScratchArg(Fake *, const uint32_t *a)
{
    uint32_t *scratch = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    const uint32_t h = Mixed({a[1], 0x4242});
    scratch[0] = h;
    scratch[1] = h ^ 1;
    scratch[2] = h ^ 2;
    return (h & 1) ? a[0] : P(&gVertexPos[0]);
}
uint32_t QueryValue(Fake *, const uint32_t *a)
{
    const uint32_t *p = LogWords(a[0], 3);
    return Mixed({p[0], p[1], p[2], 0x178});
}
float ObjectOffset(Fake *, const uint32_t *) { return float(Mixed({gSeed, 0x4545}) % 400) * 0.0173f - 1.0f; }
uint32_t gClaimOdds;
uint32_t Claims(Fake *self, const uint32_t *a) { return (Mixed({self->id, a[0], 0x112}) % 100 < gClaimOdds) ? 0x7701u : 0x7700u; }

// Objects on a tile: CountObjects (65), GetObjectAt (67), the object manager's slot 29.
int32_t gObjectCount;
uint32_t CountObjects(Fake *, const uint32_t *a)
{
    LogTile4(a[0]);
    return uint32_t(gObjectCount);
}
uint32_t ObjectAt(Fake *, const uint32_t *a)
{
    const uint32_t *t = LogTile4(a[1]);
    return Mixed({a[0], t[0], t[1], 0x67}) % 6;
}
uint32_t ObjectFor(Fake *, const uint32_t *a) { return a[0] >= 1 && a[0] <= 4 ? P(&gTileObject[a[0] - 1]) : 0u; }

// The floor pattern (slot 43: the WorldName through the hidden pointer) and the catalogue's slot 42 entry with its name.
const char *const kPatterns[] = {"floor_wood", "tile_blue", "", "carpet"};
const char *const kNames[] = {"Oak Floor", "", "Blue Tiles", "Shag"};
uint32_t gPattern;
uint32_t PatternName(Fake *, const uint32_t *a)
{
    LogTile4(a[1]);
    *reinterpret_cast<const char **>(uintptr_t(a[0])) = kPatterns[gPattern % 4];
    return a[0];
}
bool gEntryNull;
uint32_t PatternEntry(Fake *, const uint32_t *) { return gEntryNull ? 0u : P(&gFace); }

struct Other {
    uint32_t head;
    std::string name;
};
Other gOther;

// ---- programming ----
void Base(worldfx::Scope &scope, worldfx::Catalog &cat)
{
    StackRange();
    worldgrid::ProgramWorld();
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    cat.Rebuild();
    cat.Plug(scope);
    gPartitions.Reset(400);
    gRoofs.Reset(401);
    gPools.Reset(402);
    gTerrain.Reset(403);
    gWorldDb.Set(39, 0, P(&gPartitions));
    gWorldDb.Set(36, 0, P(&gRoofs));
    gWorldDb.Set(37, 0, P(&gPools));
    gWorldDb.Set(35, 0, P(&gTerrain));
    gWorldDb.Set(58, 1, 0, &DeckFoundation);
    gWorldDb.SetFloatFn(64, 2, &GroundElevation);
    gWorldDb.Set(65, 1, 0, &CountObjects);
    gWorldDb.Set(67, 3, 0, &ObjectAt);
    gWorldDb.Set(43, 2, 0, &PatternName);
    gPools.SetFloatFn(17, 1, &PoolSurface);
    gRoofs.SetFloatFn(24, 4, &RoofAt);
    gRoofs.Set(37, 1, 0, &RoofHere);
    gRoofs.SetFloatFn(22, 2, &RoofHeight);
    gTerrain.SetFloatFn(11, 2, &TerrainAt);
    gPartitions.Set(50, 1, 0, &PartitionAt);
    gPartitions.SetFloatFn(84, 1, &PartitionKeyValue);
    gPartitions.Set(43, 1, 0, &PartitionBit);
    gPartitions.Set(76, 3, 0, &RoomRecords);
    gPartitions.Set(77, 3, 0, &RoomFirst);
    for (unsigned i = 0; i < 3; ++i) {
        gProduct[i].Reset(330 + i);
        gProduct[i].Set(11, 0, 0x1100u + i);
    }
    cat.CatalogFake().Set(26, 2, 0, &ProductFor);
    cat.CatalogFake().Set(42, 3, 0, &PatternEntry);
    // Objects.
    gObject.Reset(500);
    gFace.Reset(501);
    gManager.Reset(502);
    gObjects.Reset(503);
    gObject.Set(13, 1, 0, &ObjectAttribute);
    gObject.Set(42, 2, 0, &ScratchArg);
    gObject.Set(178, 1, 0, &QueryValue);
    gObject.SetFloatFn(45, 0, &ObjectOffset);
    gObject.Set(61, 0, P(&gFace));
    gObject.Set(1, 0, 0);
    gObject.Set(2, 0, 0);
    gObject.Set(112, 1, 0, &Claims);
    gObjects.Set(29, 1, 0, &ObjectFor);
    for (unsigned i = 0; i < 4; ++i) {
        gTileObject[i].Reset(510 + i);
        gTileObject[i].Set(1, 0, 0);
        gTileObject[i].Set(2, 0, 0);
        gTileObject[i].Set(13, 1, 0, &TileObjectAttribute);
        gTileObject[i].Set(112, 1, 0, &Claims);
    }
    cat.App().Set(26, 0, P(&gObjects));
    gAllocator.Reset(600);
    gAllocator.Set(4, 1, 0);
    scope.Set(0x10c95d4, &gAllocator);
    scope.SetWord(0x10c95d8, 0x80000000u);
}

// The catalogue ids partitions slot 50 answers, and their kinds (entry slot 24).
void RandomCatalog(std::mt19937 &r, worldfx::Catalog &cat)
{
    const uint32_t ids[] = {0x100, 0x101, 0x102, 0x103, 0x12d, 0x2d0ea907};
    for (int i = 0; i < 6; ++i) gIds[i] = ids[r() % 6];
    const int32_t kinds[] = {0, 1, 2, 3, 4, -1, 1, 3};
    for (unsigned e = 0; e < 6; ++e) cat.Entry(e).Set(24, 0, uint32_t(kinds[r() % 8]));
    cat.Map(0x100, 0);
    cat.Map(0x101, 1);
    cat.Map(0x102, r() % 2 ? 2 : 99);
    cat.Map(0x103, r() % 3 ? 99 : 3);
    cat.Map(0x12d, r() % 2 ? 4 : 99);
    cat.Map(0x2d0ea907, r() % 2 ? 5 : 99);
    gProductOdds = r() % 101;
    for (unsigned i = 0; i < 3; ++i) gProduct[i].Set(11, 0, (r() & 0xffffff00u) | (r() % 2));
}

void RandomPercent(std::mt19937 &r)
{
    const uint32_t odds[] = {0, 10, 35, 65, 90, 100};
    gPercent = odds[r() % 6];
}

float RandomPointCoord(std::mt19937 &r) { return RandomCoord(r); }


} // namespace

// 0x7e4250 RoomAreaInTiles and 0x7e43c0 RoomMostlyFlagged.
DIFF_TEST(WorldHeightQueries_Room, 0x7e4250)
{
    worldfx::Scope scope;
    worldfx::Catalog cat;
    Stands stands;
    Base(scope, cat);
    auto area = ctx.Original<int32_t(__cdecl *)(int32_t, int32_t)>(0x7e4250);
    auto flagged = ctx.Original<bool(__cdecl *)(int32_t)>(0x7e43c0);
    std::mt19937 r(0x7e4250);
    for (int i = 0; i < 20000; ++i) {
        gSeed = r();
        gRecordNull = r() % 8 == 0;
        gRecordCount = gRecordNull ? 0 : int(r() % 17);
        for (auto &rec : gRecords) {
            rec.x = int32_t(r() % 64);
            rec.y = int32_t(r() % 64);
            rec.level = int32_t(r() % 4);
            const uint32_t pick = r() % 4;
            rec.quadrants = uint8_t(pick == 0 ? 0xf : pick == 1 ? r() % 16 : r() % 256);
            rec.pad[0] = rec.pad[1] = rec.pad[2] = uint8_t(r());
        }
        const int32_t room = int32_t(r() % 300) - 20, level = RandomLevel(r);
        const Result a = Go([&](Result &o) { o.ret[0] = uint32_t(area(room, level)); });
        const Result b = Go([&](Result &o) { o.ret[0] = uint32_t(worldheights::RoomAreaInTiles(room, level)); });
        EXPECT_SAME(a, b, "area case %d (room %d level %d, %d records)", i, room, level, gRecordCount);
    }
    for (int i = 0; i < 20000; ++i) {
        gSeed = r();
        RandomWorld(r);
        RandomCatalog(r, cat);
        RandomPercent(r);
        // The room's first quadrant and the snapped point.
        gFirst[0] = int32_t(r() % 20) - 2;
        gFirst[1] = int32_t(r() % 20) - 2;
        gFirst[2] = RandomLevel(r);
        const uint8_t quadrants[] = {1, 2, 4, 8, 0, 3, 0xf, 0x10};
        gFirstQuadrant = quadrants[r() % 8];
        gSnapWrite = r() % 6 != 0;
        for (int k = 0; k < 6; ++k) gSnapped[k] = RandomPointCoord(r);
        const int32_t snapLevel = RandomLevel(r);
        std::memcpy(&gSnapped[2], &snapLevel, 4);
        gSnapped[5] = gSnapped[2];
        float effective[3] = {-1.0f, -1.0f, 0.0f};
        if (gSnapWrite) std::memcpy(effective, gSnapped, sizeof(effective));
        // The outline: a ring of n edges, edge k from vertex k to vertex k + 1.
        const int n = 1 + int(r() % 8);
        gStart = int(r() % uint32_t(n));
        gGraphNull = r() % 16 == 0;
        gNodeNull = r() % 16 == 0;
        gStartNull = r() % 16 == 0;
        gGraph.Reset(410);
        gNode.Reset(411);
        gPartitions.Set(40, 1, gGraphNull ? 0u : P(&gGraph));
        gGraph.Set(12, 1, 0, &GraphNode);
        gNode.Set(2, 1, gStartNull ? 0u : P(&gEdge[gStart]));
        for (int k = 0; k < n; ++k) {
            gVertex[k].Reset(440 + k);
            gVertex[k].Set(5, 0, P(gVertexPos[k]));
            gVertexPos[k][0] = RandomPointCoord(r);
            gVertexPos[k][1] = RandomPointCoord(r);
            const int32_t vl = r() % 2 ? snapLevel : RandomLevel(r);
            std::memcpy(&gVertexPos[k][2], &vl, 4);
            gEdge[k].Reset(420 + k);
            gEdge[k].Set(35, 0, P(&gVertex[k]));
            gEdge[k].Set(36, 0, P(&gVertex[(k + 1) % n]));
            gEdge[k].Set(25, 0, P(&gEdge[(k + 1) % n]));
            gEdge[k].Set(11, 0, P(&gEdge[(k + int(r() % uint32_t(n))) % n]));
        }
        // The start edge's origin against the snapped point: the same, a few ulps off, another level, or anywhere.
        float *origin = gVertexPos[gStart];
        switch (r() % 5) {
        case 0: std::memcpy(origin, effective, 12); break;
        case 1:
        case 2:
            std::memcpy(origin, effective, 12);
            for (int axis = 0; axis < 2; ++axis)
                if (r() % 2) origin[axis] = FromBits(Bits(origin[axis]) + uint32_t(int32_t(r() % 80) - 40));
            break;
        case 3: {
            std::memcpy(origin, effective, 12);
            int32_t l;
            std::memcpy(&l, &origin[2], 4);
            l += 1;
            std::memcpy(&origin[2], &l, 4);
            break;
        }
        default: break;
        }
        const int32_t room = int32_t(r() % 300);
        const Result a = Go([&](Result &o) { o.ret[0] = flagged(room) ? 1u : 0u; });
        const Result b = Go([&](Result &o) { o.ret[0] = worldheights::RoomMostlyFlagged(room) ? 1u : 0u; });
        EXPECT_SAME(a, b, "flagged case %d (room %d, %d edges from %d)", i, room, n, gStart);
    }
}

// 0x7e4c00 SurfaceHeightAt, 0x7e4ce0 HighestSurfaceAt and 0x7e59c0 SegmentEndHeight.
namespace {
void SurfaceCases(difftest::Context &ctx, worldfx::Catalog &cat, const char *label, uint32_t seed, int count)
{
    using Point1 = double(__cdecl *)(const float *);
    using Highest = double(__cdecl *)(float, float, uint32_t, bool *, int32_t *, bool *);
    using Segment = double(__cdecl *)(const float *, bool);
    auto surface = ctx.Original<Point1>(0x7e4c00);
    auto highest = ctx.Original<Highest>(0x7e4ce0);
    auto segment = ctx.Original<Segment>(0x7e59c0);
    auto oursSurface = reinterpret_cast<Point1>(&worldheights::SurfaceHeightAt);
    auto oursHighest = reinterpret_cast<Highest>(&worldheights::HighestSurfaceAt);
    auto oursSegment = reinterpret_cast<Segment>(&worldheights::SegmentEndHeight);
    std::mt19937 r(seed);
    for (int i = 0; i < count; ++i) {
        gSeed = r();
        RandomWorld(r);
        gWorld.rough = r() % 2 == 0;
        RandomCatalog(r, cat);
        RandomPercent(r);
        gDeck = r();
        gRoofMode = r() % 5;
        gRoofHere = r() % 101;
        gWorldDb.Set(35, 0, r() % 4 ? P(&gTerrain) : 0u);
        // SurfaceHeightAt.
        float point[3] = {RandomCoord(r), RandomCoord(r), 0};
        const int32_t level = RandomLevel(r);
        std::memcpy(&point[2], &level, 4);
        Result a = Go([&](Result &o) { StoreDouble(o, surface(point)); });
        Result b = Go([&](Result &o) { StoreDouble(o, oursSurface(point)); });
        EXPECT_SAME(a, b, "%s surface case %d (%08x %08x level %d)", label, i, Bits(point[0]), Bits(point[1]), level);
        // HighestSurfaceAt, with each out pointer present or null.
        const uint32_t roofArg = r();
        const uint32_t outs = r() % 8;
        auto call = [&](Highest f) {
            return [&, f](Result &o) {
                bool *onRoof = (outs & 1) ? reinterpret_cast<bool *>(&o.w[0]) : nullptr;
                int32_t *lvl = (outs & 2) ? reinterpret_cast<int32_t *>(&o.w[1]) : nullptr;
                bool *sunken = (outs & 4) ? reinterpret_cast<bool *>(&o.w[2]) : nullptr;
                StoreDouble(o, f(point[0], point[1], roofArg, onRoof, lvl, sunken));
            };
        };
        a = Go(call(highest));
        b = Go(call(oursHighest));
        EXPECT_SAME(a, b, "%s highest case %d (%08x %08x outs %u roof mode %u)", label, i, Bits(point[0]), Bits(point[1]), outs,
                    gRoofMode);
        // SegmentEndHeight.
        float seg[6] = {RandomCoord(r), RandomCoord(r), 0, RandomCoord(r), RandomCoord(r), 0};
        const int32_t l0 = RandomLevel(r), l1 = RandomLevel(r);
        std::memcpy(&seg[2], &l0, 4);
        std::memcpy(&seg[5], &l1, 4);
        const bool first = r() % 2 == 0;
        a = Go([&](Result &o) { StoreDouble(o, segment(seg, first)); });
        b = Go([&](Result &o) { StoreDouble(o, oursSegment(seg, first)); });
        EXPECT_SAME(a, b, "%s segment case %d (first %d, ids %x %x)", label, i, int(first), gIds[0], gIds[1]);
    }
    gWorld.rough = false;
}
} // namespace

DIFF_TEST(WorldHeightQueries_Surface, 0x7e4c00)
{
    worldfx::Scope scope;
    worldfx::Catalog cat;
    Stands stands;
    Base(scope, cat);
    SurfaceCases(ctx, cat, "53-bit", 0x7e4c00, 15000);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    SurfaceCases(ctx, cat, "24-bit", 0x7e4c01, 15000);
}

// 0x7e4f40 ObjectQueryAt, 0x7e4f90 IsRaisedObject, 0x7e5170 TileClaimsObject, 0x7e5330 AnyStructureAt, 0x7e56e0
// TileHasFlaggedObject and 0x7e5b40 Lookup9.
namespace {
void RandomObject(std::mt19937 &r)
{
    const int32_t kinds[] = {9, 10, 11, 12, 5, 0, 1, 2, 3, 4, 6, 7, 8, 13, -1};
    gObject.Set(98, 0, (r() % 4 == 0 ? r() & 0xffffff00u : 0u) + uint32_t(kinds[r() % 15]));
    const int32_t attrs[] = {8, 1, 4, 5, 6, 28, 70, 148, 0, 2, 3, 7, 9, 0x10008, -8};
    gAttr3b = attrs[r() % 15];
    const int32_t counts[] = {0, 1, 2, -1, 5};
    gObject.Set(159, 0, uint32_t(counts[r() % 5]));
    gFace.Set(28, 0, r() % 3 == 0 ? 0xb0860d98u : r());
    gManager.Set(43, 1, (r() & 0xffffff00u) | (r() % 4 == 0 ? 1u : 0u));
    gManager.Set(49, 1, (r() & 0xffffff00u) | (r() % 4 == 0 ? 1u : 0u));
    gManager.Set(51, 1, (r() & 0xffffff00u) | (r() % 4 == 0 ? 1u : 0u));
}
} // namespace

DIFF_TEST(WorldHeightQueries_Objects, 0x7e4f90)
{
    worldfx::Scope scope;
    worldfx::Catalog cat;
    Stands stands;
    Base(scope, cat);
    gLookupObject = &gObject;
    auto query = ctx.Original<uint32_t(__cdecl *)(uint32_t, float)>(0x7e4f40);
    auto raised = ctx.Original<bool(__cdecl *)(void *)>(0x7e4f90);
    auto claims = ctx.Original<bool(__cdecl *)(const float *, void *)>(0x7e5170);
    auto any = ctx.Original<bool(__cdecl *)(const void *, uint32_t)>(0x7e5330);
    auto tileFlagged = ctx.Original<bool(__cdecl *)(const nTSWorld::WorldTile *)>(0x7e56e0);
    auto lookup = ctx.Original<int32_t *(__cdecl *)(uint32_t)>(0x7e5b40);
    static const uint32_t kStructure[4] = {3, 4, 1, 0xf};
    std::mt19937 r(0x7e4f90);
    for (int i = 0; i < 30000; ++i) {
        gSeed = r();
        RandomWorld(r);
        RandomPercent(r);
        RandomObject(r);
        cat.App().Set(24, 0, r() % 5 ? P(&gManager) : 0u);
        cat.App().Set(16, 0, r() % 8 ? P(&cat.CatalogFake()) : 0u);
        gClaimOdds = r() % 101;
        gObjectCount = int32_t(r() % 6) - 1;
        gPattern = r();
        gEntryNull = r() % 4 == 0;
        gFace.Set(9, 0, P(kNames[r() % 4]));
        gMaskOdds = r() % 101;
        // ObjectQueryAt.
        const uint32_t key = r();
        const float f = RandomCoord(r);
        Result a = Go([&](Result &o) { o.ret[0] = query(key, f); });
        Result b = Go([&](Result &o) { o.ret[0] = worldheights::ObjectQueryAt(key, f); });
        EXPECT_SAME(a, b, "query case %d (key %08x)", i, key);
        // IsRaisedObject (and a null object now and then).
        void *object = r() % 32 ? static_cast<void *>(&gObject) : nullptr;
        a = Go([&](Result &o) { o.ret[0] = raised(object) ? 1u : 0u; });
        b = Go([&](Result &o) { o.ret[0] = worldheights::IsRaisedObject(object) ? 1u : 0u; });
        EXPECT_SAME(a, b, "raised case %d (kind %d attr %d)", i, int32_t(gObject.ret[98]), gAttr3b);
        // TileClaimsObject: the claimed name lands in the string after the other object's first word.
        const float point[3] = {RandomCoord(r), RandomCoord(r), FromBits(LevelWord(RandomLevel(r)))};
        a = Go([&](Result &o) {
            gOther.head = 0x0e0e;
            gOther.name = "old";
            o.ret[0] = claims(point, &gOther) ? 1u : 0u;
            o.str = gOther.name;
            o.w[0] = gOther.head;
        });
        b = Go([&](Result &o) {
            gOther.head = 0x0e0e;
            gOther.name = "old";
            o.ret[0] = worldheights::TileClaimsObject(point, &gOther) ? 1u : 0u;
            o.str = gOther.name;
            o.w[0] = gOther.head;
        });
        EXPECT_SAME(a, b, "claims case %d (%08x %08x, %d objects)", i, Bits(point[0]), Bits(point[1]), gObjectCount);
        // AnyStructureAt.
        const uint32_t extra = r();
        a = Go([&](Result &o) { o.ret[0] = any(kStructure, extra) ? 1u : 0u; });
        b = Go([&](Result &o) { o.ret[0] = worldheights::AnyStructureAt(kStructure, extra) ? 1u : 0u; });
        EXPECT_SAME(a, b, "any case %d", i);
        // TileHasFlaggedObject.
        nTSWorld::WorldTile tile{};
        tile.x = int32_t(r() % 20) - 2;
        tile.y = int32_t(r() % 20) - 2;
        tile.level = RandomLevel(r);
        tile.segments = r() % 16;
        a = Go([&](Result &o) { o.ret[0] = tileFlagged(&tile) ? 1u : 0u; });
        b = Go([&](Result &o) { o.ret[0] = worldheights::TileHasFlaggedObject(&tile) ? 1u : 0u; });
        EXPECT_SAME(a, b, "flagged object case %d (%d objects)", i, gObjectCount);
        // Lookup9.
        a = Go([&](Result &o) { o.ret[0] = P(lookup(key)); });
        b = Go([&](Result &o) { o.ret[0] = P(worldheights::Lookup9(key)); });
        EXPECT_SAME(a, b, "lookup case %d", i);
    }
    gOther.name.clear();
    gOther.name.shrink_to_fit();
}

// 0x7e5370 ObjectHeightAt and 0x7e55c0 ObjectPositionAt (whose raised-surface test runs the image's 0x7e4f90 on the
// original's side and ours on ours, both against the same fakes).
namespace {
void ObjectHeightCases(difftest::Context &ctx, const char *label, uint32_t seed, int count)
{
    using Height = double(__cdecl *)(void *, const float *, const uint32_t *);
    using Position = float *(__cdecl *)(float *, void *, const float *, const uint32_t *);
    auto height = ctx.Original<Height>(0x7e5370);
    auto position = ctx.Original<Position>(0x7e55c0);
    auto oursHeight = reinterpret_cast<Height>(&worldheights::ObjectHeightAt);
    std::mt19937 r(seed);
    for (int i = 0; i < count; ++i) {
        gSeed = r();
        RandomWorld(r);
        gWorld.rough = r() % 2 == 0;
        RandomPercent(r);
        RandomObject(r);
        gDeck = r();
        gWorldDb.Set(35, 0, r() % 4 ? P(&gTerrain) : 0u);
        gAttr2a = r() % 3 == 0 ? int32_t(0x4000 | (r() & 0xbfff)) : int32_t(r() & 0xbfff);
        gAttr52 = r() % 2 == 0 ? int32_t(0x200 | (r() & 0xfdff)) : int32_t(r() & 0xfdff);
        const uint32_t flagChoices[] = {0, 0x5f, 1, 2, 4, 8, 0x10, 0x40, 0x20, 0x80, 0xa0, 0x1234, 0xffff, 0x8001, r()};
        gObject.Set(153, 0, flagChoices[r() % 15] | (r() % 2 ? r() & 0xffff0000u : 0u));
        gCornerBase = r() % 3 == 0 ? float(gWorld.size + 3) : 0.0f;
        static uint32_t context;
        context = r();
        float point[3] = {RandomCoord(r), RandomCoord(r), 0};
        const int32_t level = r() % 3 == 0 ? 0 : RandomLevel(r);
        std::memcpy(&point[2], &level, 4);
        Result a = Go([&](Result &o) { StoreDouble(o, height(&gObject, point, &context)); });
        Result b = Go([&](Result &o) { StoreDouble(o, oursHeight(&gObject, point, &context)); });
        EXPECT_SAME(a, b, "%s height case %d (%08x %08x level %d flags %x)", label, i, Bits(point[0]), Bits(point[1]), level,
                    gObject.ret[153]);
        if (i % 4 == 0) {
            a = Go([&](Result &o) {
                float *out = reinterpret_cast<float *>(&o.w[0]);
                o.ret[0] = position(out, &gObject, point, &context) == out ? 1u : 0u;
            });
            b = Go([&](Result &o) {
                float *out = reinterpret_cast<float *>(&o.w[0]);
                o.ret[0] = worldheights::ObjectPositionAt(out, &gObject, point, &context) == out ? 1u : 0u;
            });
            EXPECT_SAME(a, b, "%s position case %d", label, i);
        }
    }
    gWorld.rough = false;
}
} // namespace

DIFF_TEST(WorldHeightQueries_ObjectHeight, 0x7e5370)
{
    worldfx::Scope scope;
    worldfx::Catalog cat;
    Stands stands;
    Base(scope, cat);
    cat.App().Set(24, 0, P(&gManager));
    ObjectHeightCases(ctx, "53-bit", 0x7e5370, 20000);
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    ObjectHeightCases(ctx, "24-bit", 0x7e5371, 20000);
}

// 0x7e5600 TileHasStructure and 0x7e5810 PartitionMaskRotated.
DIFF_TEST(WorldHeightQueries_Tiles, 0x7e5600)
{
    worldfx::Scope scope;
    worldfx::Catalog cat;
    Stands stands;
    Base(scope, cat);
    auto structure = ctx.Original<bool(__cdecl *)(const nTSWorld::WorldTile *)>(0x7e5600);
    auto mask = ctx.Original<uint32_t(__cdecl *)(const nTSWorld::WorldTile *, uint32_t)>(0x7e5810);
    std::mt19937 r(0x7e5600);
    for (int i = 0; i < 30000; ++i) {
        gSeed = r();
        RandomWorld(r);
        const uint32_t odds[] = {0, 3, 10, 35, 100};
        gPercent = odds[r() % 5];
        gMaskOdds = r() % 101;
        nTSWorld::WorldTile tile{};
        tile.x = int32_t(r() % uint32_t(gWorld.size + 4)) - 2;
        tile.y = int32_t(r() % uint32_t(gWorld.size + 4)) - 2;
        tile.level = RandomLevel(r);
        tile.segments = r();
        Result a = Go([&](Result &o) { o.ret[0] = structure(&tile) ? 1u : 0u; });
        Result b = Go([&](Result &o) { o.ret[0] = worldheights::TileHasStructure(&tile) ? 1u : 0u; });
        EXPECT_SAME(a, b, "structure case %d (%d %d %d, levels %d..%d)", i, tile.x, tile.y, tile.level, gWorld.minLevel,
                    gWorld.maxLevel);
        const uint32_t rotation = r() % 4 == 0 ? r() : r() % 6;
        a = Go([&](Result &o) { o.ret[0] = mask(&tile, rotation); });
        b = Go([&](Result &o) { o.ret[0] = worldheights::PartitionMaskRotated(&tile, rotation); });
        EXPECT_SAME(a, b, "mask case %d (rotation %u)", i, rotation);
    }
}
