// Differential tests for nTSSG::cSunHeightMap (lane 261108i, scenegraph LG5, src/sg/cSunHeightMap.*): the constructor,
// Resize / ResetFlat, the shadow sweeps and the grid builders the light-map pass runs over the world DB. The builders run
// against the recording fakes of tests/worldfx.h (the world DB, which is also the cached one, its roofs and partitions
// objects, the catalog); the fakes log every call, with the pointed-to tiles, points and records in place of the
// pointers (which differ between the sides). Compared: every non-pointer field of the map, the sizes and bits of its four
// float vectors (not their capacities: an allocation detail nothing reads), and the logs. Cases run at the x87's 53-bit
// and 24-bit precision where the original computes on the x87 (the cell size, the wall tops).
#include "difftest.h"
#include "ts_services_fixture.h"
#include "worldfx.h"
#include "worldgrid.h"

#include "app/AppServices2.h"
#include "sg/cSunHeightMap.h"
#include "simantics/ServiceRefVector.h"
#include "world/cWorldDB.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using nTSSG::cSunHeightMap;
using nTSWorld::cIWorldDB;
using nTSWorld::WorldTile;
using worldfx::Fake;
using worldfx::gLog;
using worldfx::gWorldDb;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kDbId = 100, kRoofsId = 200, kPartsId = 201, kProductId = 202;

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

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

uint32_t gSalt;     // per case: the fakes' answers are hashes of their arguments and this
uint32_t gNaNEvery; // a hashed float answer is a NaN once in this many (0: never)

float NaNOf(uint32_t h) { return Fl(0x7f800000u | (h & 0x80000000u) | ((h >> 3) & 0x400000u) | (1 + (h % 0x3fffffu))); }

// A float answer from a hash: small multiples of 1/8, long significands, now and then a NaN (of either kind).
float Answer(uint32_t h)
{
    if (gNaNEvery && h % gNaNEvery == 0) return NaNOf(Mix(h, 0x55));
    switch ((h >> 8) % 6) {
    case 0: return float(int32_t(h >> 16) % 40) * 0.125f;
    case 1: return 0.0f;
    case 2: return float(int32_t(h >> 12) % 100000) * 0.0000731f - 2.0f;
    default: return float(int32_t(h >> 20) % 24) * 0.25f + 0.5f;
    }
}

// A float for the cases: the families the sweeps and sizes need.
float Nice(std::mt19937 &gen, float scale = 1.0f)
{
    switch (gen() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return float(int(gen() % 9) - 4);
    case 3: return Fl((gen() & 0x807fffffu) | (uint32_t(127 - 20 + gen() % 40) << 23));
    default: return (float(int32_t(gen() & 0xffffff) - 0x800000) / float(0x800000)) * scale;
    }
}

float NaN(std::mt19937 &gen) { return NaNOf(gen()); }

// --- the log ---------------------------------------------------------------------------------------------------------

// The tile slots log the tile after their entry: x, y, level (+ the segment byte where the callers set it).
const int32_t *LogTile(const uint32_t *a, bool segments)
{
    const auto *t = reinterpret_cast<const int32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 3; ++i) gLog.push_back(uint32_t(t[i]));
    if (segments) gLog.push_back(*reinterpret_cast<const uint8_t *>(t + 3));
    return t;
}

// The words a fake's slot appends after its entry, and its pointer argument (cleared), by fake id and slot.
struct Shape {
    unsigned appended;
    int pointer; // the argument index, or -1
    uint32_t byteArgs; // bit i: argument i is a byte (only the low byte is compared)
};
Shape ShapeOf(uint32_t id, uint32_t slot)
{
    if (id == kDbId) {
        switch (slot) {
        case 62: case 70: case 71: case 59: return {3, 0, 0};
        case 49: case 58: return {4, 0, 0};
        case 52: return {4, 0, 6};
        default: return {0, -1, 0};
        }
    }
    if (id == kRoofsId) {
        switch (slot) {
        case 28: return {0, 0, 0};
        case 37: case 22: return {3, 0, 0};
        default: return {0, -1, 0};
        }
    }
    if (id == kPartsId) {
        switch (slot) {
        case 56: return {0, 0, 0};
        case 50: return {6, 0, 0};
        default: return {0, -1, 0};
        }
    }
    return {0, -1, 0};
}

Blob Normalised(Blob log)
{
    for (size_t i = 0; i + 3 <= log.size();) {
        const uint32_t id = log[i], slot = log[i + 1], argc = log[i + 2];
        const Shape s = ShapeOf(id, slot);
        if (s.pointer >= 0 && uint32_t(s.pointer) < argc) log[i + 3 + s.pointer] = 0;
        for (uint32_t k = 0; k < argc; ++k)
            if (s.byteArgs >> k & 1) log[i + 3 + k] &= 0xff;
        i += 3 + argc + s.appended;
    }
    return log;
}

// --- the map ---------------------------------------------------------------------------------------------------------

void PutVector(Blob &o, const std::vector<float> &v)
{
    o.push_back(uint32_t(v.size()));
    for (float f : v) o.push_back(U(f));
}

// Every field but the vectors' pointers, then the vectors' sizes and contents.
Blob State(const cSunHeightMap &m)
{
    Blob o;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&m);
    for (uint32_t off = 0x24; off < 0x58; off += 4) {
        uint32_t w;
        std::memcpy(&w, bytes + off, 4);
        o.push_back(off == 0x38 ? (w & 0xff) : w); // mFlat: the byte (its padding is the constructor's leftovers)
    }
    PutVector(o, m.mValues);
    PutVector(o, m.mGround);
    PutVector(o, m.mHeights);
    PutVector(o, m.mRun);
    return o;
}

struct alignas(16) MapStorage {
    uint8_t bytes[sizeof(cSunHeightMap)];
    cSunHeightMap *Get() { return reinterpret_cast<cSunHeightMap *>(bytes); }
};

// Two maps alike: the same sizes, fields and grid contents (built by ours; the original's functions then work on one).
struct MapPair {
    MapStorage s[2];
    MapPair(std::mt19937 &gen, int32_t w, int32_t h, float cell)
    {
        const uint32_t seed = gen();
        for (auto &st : s) {
            std::mt19937 g(seed);
            for (auto &b : st.bytes) b = uint8_t(g());
            cSunHeightMap *m = ::new (static_cast<void *>(st.bytes)) cSunHeightMap;
            m->Resize(w, h, cell, -1000.0f);
            for (auto *v : {&m->mValues, &m->mGround, &m->mHeights})
                for (float &f : *v) f = g() % 3 ? Nice(g, 6.0f) : -1000.0f;
            m->mRun.resize(g() % 20);
            for (float &f : m->mRun) f = Nice(g, 6.0f);
            m->mFlat = uint8_t(g() % 2);
            for (int32_t &c : m->mChanged) c = int32_t(g() % 50) - 2;
            m->mBlendScale = Nice(g);
        }
    }
    ~MapPair()
    {
        for (auto &st : s) st.Get()->~cSunHeightMap();
    }
    cSunHeightMap *operator[](int i) { return s[i].Get(); }
};

template <class Case> void AtBothPrecisions(uint32_t cases, Case &&run)
{
    for (uint32_t seed = 0; seed < cases; ++seed) run(seed, "53-bit");
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    for (uint32_t seed = 0; seed < cases; ++seed) run(seed + 0x100000, "24-bit");
}

// The first differing word, for the failure message.
size_t FirstDiff(const Blob &a, const Blob &b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    return i;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const char *precision, const Blob &stateA,
             const Blob &stateB, const Blob &logA, const Blob &logB)
{
    ++ctx.cases;
    if (stateA != stateB) {
        const size_t i = FirstDiff(stateA, stateB);
        ctx.Fail("%s seed %#x (%s): map differs at word %zu (%#x vs %#x; %zu vs %zu words)", what, seed, precision, i,
                 i < stateA.size() ? stateA[i] : 0, i < stateB.size() ? stateB[i] : 0, stateA.size(), stateB.size());
    }
    ++ctx.cases;
    if (logA != logB) {
        const size_t i = FirstDiff(logA, logB);
        ctx.Fail("%s seed %#x (%s): calls differ at word %zu (%#x vs %#x; %zu vs %zu words)", what, seed, precision, i,
                 i < logA.size() ? logA[i] : 0, i < logB.size() ? logB[i] : 0, logA.size(), logB.size());
    }
}

// --- the world DB ----------------------------------------------------------------------------------------------------

uint32_t TileHash(const int32_t *t, uint32_t slot) { return Mix(Mix(Mix(Mix(gSalt, slot), uint32_t(t[0])), uint32_t(t[1])), uint32_t(t[2])); }

bool gLongElevations; // half the elevations: full significands, 1/8 to 2 (AddWalls: the order of its sums shows)

float Elevation(Fake *, const uint32_t *a)
{
    const uint32_t h = TileHash(LogTile(a, false), 62);
    if (gLongElevations && h % 2 && !(gNaNEvery && h % gNaNEvery == 0))
        return Fl((Mix(h, 0x77) & 0x807fffffu) | ((124u + (h >> 24) % 4) << 23));
    return Answer(h);
}
uint32_t FloorVisibility(Fake *, const uint32_t *a)
{
    const uint32_t h = TileHash(LogTile(a, true), 49);
    return (h & 0xffffff00u) | (h % 4 != 0); // only al counts
}
uint32_t DeckFoundation(Fake *, const uint32_t *a)
{
    const uint32_t h = TileHash(LogTile(a, true), 58);
    return h % 3 == 0 ? 5 : h % 7;
}
uint32_t FloorOrGround(Fake *, const uint32_t *a)
{
    const int32_t *t = LogTile(a, true);
    const uint32_t h = Mix(TileHash(t, 52), a[1] & 0xff);
    return (h & 0xffffff00u) | (t[2] == 0 ? h % 4 != 0 : h % 3 == 0);
}
uint32_t gAwningEvery = 3;
uint32_t Awning(Fake *, const uint32_t *a)
{
    const uint32_t h = TileHash(LogTile(a, false), 59);
    return h % gAwningEvery == 0 ? h % 5 : 0xffffffffu;
}
int32_t gWidth, gHeight;
uint32_t Width(Fake *, const uint32_t *) { return uint32_t(gWidth); }
uint32_t Height(Fake *, const uint32_t *) { return uint32_t(gHeight); }

Fake gRoofs, gParts, gProduct;

void ProgramDb(float tileSize)
{
    worldgrid::ProgramWorld(); // 88, 89, 81-84, 72, 71, 70, 62, 95 (the ground height's slots)
    gWorldDb.SetFloatFn(62, 1, &Elevation);
    gWorldDb.Set(49, 2, 0, &FloorVisibility);
    gWorldDb.Set(58, 1, 0, &DeckFoundation);
    gWorldDb.Set(52, 3, 0, &FloorOrGround);
    gWorldDb.Set(59, 1, 0, &Awning);
    gWorldDb.Set(85, 0, 0, &Width);
    gWorldDb.Set(86, 0, 0, &Height);
    gWorldDb.SetFloat(87, 0, tileSize);
    gWorldDb.Set(36, 0, P(&gRoofs));
    gWorldDb.Set(39, 0, P(&gParts));
}

float RandomTileSize(std::mt19937 &gen)
{
    static const float kSizes[] = {1.0f, 1.0f, 0.5f, 2.0f, 1.3f, 0.7f, 3.0f};
    return kSizes[gen() % 7];
}

cIWorldDB *Db() { return reinterpret_cast<cIWorldDB *>(&gWorldDb); }

// --- the roofs -------------------------------------------------------------------------------------------------------

struct RoofRecord {
    uint32_t words[12]; // +8 / +0xc one corner, +0x10 the level, +0x14 / +0x18 the other corner, +0x20 the kind
};
RoofRecord gRoofRecords[8];
uint32_t gRoofCount;
constexpr uint32_t kRoofIdBase = 0x500;

uint32_t RoofIds(Fake *, const uint32_t *a)
{
    auto *v = reinterpret_cast<ServiceBlockVector *>(uintptr_t(a[0]));
    if (!gRoofCount) return 0;
    auto *ids = static_cast<uint32_t *>(as2::AllocateViaAllocatorService(gRoofCount * 4, 0));
    for (uint32_t i = 0; i < gRoofCount; ++i) ids[i] = kRoofIdBase + i;
    v->mFirst = ids;
    v->mLast = v->mEnd = ids + gRoofCount;
    return 0;
}
uint32_t RoofRecordOf(Fake *, const uint32_t *a)
{
    const uint32_t i = a[0] - kRoofIdBase;
    return i < 8 ? P(&gRoofRecords[i]) : 0;
}
float RoofHeightAt(Fake *, const uint32_t *a) { return Answer(Mix(Mix(Mix(Mix(gSalt, a[0]), a[1]), a[2]), a[3] * 31 + a[4])); }
uint32_t RoofOver(Fake *, const uint32_t *a)
{
    const uint32_t h = TileHash(LogTile(a, false), 37);
    return (h & 0xffffff00u) | (h % 3 == 0);
}
float RoofUnderside(Fake *, const uint32_t *a) { return Answer(Mix(TileHash(LogTile(a, false), 22), a[1])); }

void ProgramRoofs()
{
    gRoofs.Reset(kRoofsId);
    gRoofs.Set(28, 1, 0, &RoofIds);
    gRoofs.Set(25, 1, 0, &RoofRecordOf);
    gRoofs.SetFloatFn(30, 5, &RoofHeightAt);
    gRoofs.Set(37, 1, 0, &RoofOver);
    gRoofs.SetFloatFn(22, 2, &RoofUnderside);
}

// --- the partitions --------------------------------------------------------------------------------------------------

struct Wall {
    uint32_t words[6]; // x0, y0, level0, x1, y1, level1
    uint32_t id;
};
std::vector<Wall> gWalls;
uint32_t gLastWall;

uint32_t WallCount(Fake *, const uint32_t *) { return uint32_t(gWalls.size()); }
uint32_t WallCopy(Fake *, const uint32_t *a)
{
    gLastWall = a[1];
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gWalls[a[1]].words, 24);
    return 0;
}
uint32_t WallId(Fake *, const uint32_t *a)
{
    const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 6; ++i) gLog.push_back(w[i]);
    return gWalls[gLastWall].id;
}
// A third of the answers have full significands, 1/8 to 2: the fence top adds it and the 0.75 raise in order.
float PartitionHeight(Fake *, const uint32_t *a)
{
    const uint32_t h = Mix(Mix(gSalt, 84), a[0]);
    if (h % 3 == 1) return Fl((Mix(h, 0x78) & 0x007fffffu) | ((124u + (h >> 24) % 4) << 23));
    return Answer(h);
}

void ProgramPartitions()
{
    gParts.Reset(kPartsId);
    gParts.Set(55, 0, 0, &WallCount);
    gParts.Set(56, 2, 0, &WallCopy);
    gParts.Set(50, 1, 0, &WallId);
    gParts.SetFloatFn(84, 1, &PartitionHeight);
}

// --- the catalog (kinds, the product flag) ---------------------------------------------------------------------------

const uint32_t kWallIds[] = {0x12d, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45};

uint32_t ProductOf(Fake *, const uint32_t *a) { return Mix(gSalt, a[0]) % 3 ? P(&gProduct) : 0; }

void ProgramCatalog(worldfx::Catalog &catalog, std::mt19937 &gen)
{
    catalog.Rebuild();
    static const uint32_t kKinds[] = {0, 1, 2, 3, 3, 1, 5};
    for (unsigned e = 0; e < worldfx::Catalog::kEntries; ++e) catalog.Entry(e).Set(24, 0, kKinds[gen() % 7]);
    for (uint32_t id : kWallIds) catalog.Map(id, gen() % (worldfx::Catalog::kEntries + 2)); // some ids without an entry
    catalog.CatalogFake().Set(26, 2, 0, &ProductOf);
    gProduct.Reset(kProductId);
    gProduct.Set(10, 0, (gen() & 0xffffff00u) | (gen() % 3 == 0));
}

} // namespace

// ---- the constructor, Resize and ResetFlat -----------------------------------------------------------------------------

DIFF_TEST(sg_sun_height_map_construct, 0x3be100)
{
    using Ctor = void *(__thiscall *)(void *);
    auto original = ctx.Original<Ctor>(0x3be100);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        MapStorage s[2];
        for (auto &st : s) {
            std::mt19937 g(seed);
            for (auto &b : st.bytes) b = uint8_t(g());
        }
        void *ra = original(s[0].bytes);
        void *rb = s[1].Get()->Construct();
        ++ctx.cases;
        if (ra != s[0].bytes || rb != s[1].bytes) ctx.Fail("seed %u: returns", seed);
        ++ctx.cases;
        if (std::memcmp(s[0].bytes, s[1].bytes, sizeof(s[0].bytes)) != 0) {
            uint32_t at = 0;
            while (s[0].bytes[at] == s[1].bytes[at]) ++at;
            ctx.Fail("seed %u: bytes differ from +%#x", seed, at);
        }
        for (auto &st : s) st.Get()->~cSunHeightMap();
    }
}

DIFF_TEST(sg_sun_height_map_resize, 0x3c0750)
{
    using ResizeFn = void(__thiscall *)(void *, int32_t, int32_t, float, float);
    using ResetFn = void(__thiscall *)(void *);
    auto resize = ctx.Original<ResizeFn>(0x3c0750);
    auto reset = ctx.Original<ResetFn>(0x3be1a0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed);
        MapPair maps(gen, int32_t(gen() % 9), int32_t(gen() % 9), 0.25f);
        const int32_t w = int32_t(gen() % 14), h = gen() % 8 ? int32_t(gen() % 14) : 0;
        static const float kCells[] = {1.0f, 0.5f, 0.25f, 0.3f, 0.0f, -0.0f, 3.0f};
        float cell = kCells[gen() % 7], fill = gen() % 2 ? -1000.0f : Nice(gen, 50.0f);
        if (gen() % 10 == 0) cell = NaN(gen);
        if (gen() % 10 == 0) fill = NaN(gen);
        const bool alsoReset = gen() % 2 == 0;
        gLog.clear();
        resize(maps[0], w, h, cell, fill);
        if (alsoReset) reset(maps[0]);
        maps[1]->Resize(w, h, cell, fill);
        if (alsoReset) maps[1]->ResetFlat();
        Compare(ctx, alsoReset ? "Resize + ResetFlat" : "Resize", seed, "-", State(*maps[0]), State(*maps[1]), {}, {});
    }
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed + 0x9000);
        MapPair maps(gen, int32_t(gen() % 12), int32_t(gen() % 12), 0.5f);
        if (gen() % 4 == 0) maps[0]->mFill = maps[1]->mFill = NaN(gen);
        reset(maps[0]);
        maps[1]->ResetFlat();
        Compare(ctx, "ResetFlat", seed, "-", State(*maps[0]), State(*maps[1]), {}, {});
    }
}

// ---- the sweeps -----------------------------------------------------------------------------------------------------

namespace {

// A sun direction: mostly sideways and above, with ties between |x| and |y|, tiny components, zeros and NaNs.
void RandomDirection(std::mt19937 &gen, float *dir)
{
    for (int k = 0; k < 3; ++k) dir[k] = Nice(gen, 2.0f);
    switch (gen() % 10) {
    case 0: dir[1] = gen() % 2 ? dir[0] : -dir[0]; break; // |y| == |x|
    case 1: dir[gen() % 2] = float(int(gen() % 3) - 1) * 0.0009f; break; // straight overhead on one axis
    case 2: dir[0] = 0.0005f, dir[1] = -0.0007f; break;
    case 3: dir[gen() % 3] = NaN(gen); break;
    case 4: dir[gen() % 3] = NaN(gen), dir[gen() % 3] = NaN(gen); break;
    case 5: dir[2] = gen() % 2 ? 0.0f : -0.0f; break;
    case 6: dir[0] = 0.001f, dir[1] = 0.001f; break;
    default: break;
    }
    if (gen() % 4 == 0) dir[2] = Fl(U(dir[2]) & 0x7fffffffu); // above the horizon more often
}

// Plants heights equal to the values (ties of the running maximum) and to fill.
void PlantTies(std::mt19937 &gen, cSunHeightMap *m)
{
    for (size_t i = 0; i < m->mValues.size(); ++i) {
        const uint32_t roll = gen() % 6;
        if (roll == 0) m->mHeights[i] = m->mValues[i];
        else if (roll == 1) m->mValues[i] = m->mFill;
        else if (roll == 2 && gNaNEvery) m->mValues[i] = NaN(gen);
        else if (roll == 3 && i) m->mValues[i] = m->mValues[i - 1];
    }
}

} // namespace

DIFF_TEST(sg_sun_height_map_sweeps, 0x3bfde0)
{
    using SweepFn = void(__thiscall *)(void *, const float *);
    const struct {
        uint32_t rva;
        void (cSunHeightMap::*ours)(const float *);
        const char *name;
    } kFns[] = {
        {0x3bfde0, &cSunHeightMap::BuildShadow, "BuildShadow"},
        {0x3bfe60, &cSunHeightMap::SweepX, "SweepX"},
        {0x3c0280, &cSunHeightMap::SweepY, "SweepY"},
    };
    for (const auto &fn : kFns) {
        auto original = ctx.Original<SweepFn>(fn.rva);
        AtBothPrecisions(1500, [&](uint32_t seed, const char *precision) {
            std::mt19937 gen(seed * 3 + fn.rva);
            gNaNEvery = gen() % 3 == 0 ? 7 : 0;
            // SweepX with no rows and SweepY with no columns run away in the original (ORIGINAL-BUG in ours): at least one.
            const bool x = fn.rva == 0x3bfe60, y = fn.rva == 0x3c0280;
            const int32_t w = int32_t(gen() % 13) + (x ? 0 : 1), h = int32_t(gen() % 13) + (y ? 0 : 1);
            static const float kCells[] = {1.0f, 0.5f, 0.25f, 0.3f, 2.0f};
            MapPair maps(gen, w, h, kCells[gen() % 5]);
            std::mt19937 tiesA(gen()), tiesB = tiesA;
            PlantTies(tiesA, maps[0]);
            PlantTies(tiesB, maps[1]);
            if (gen() % 8 == 0) maps[0]->mCell = maps[1]->mCell = NaN(gen);
            float dir[3];
            RandomDirection(gen, dir);
            original(maps[0], dir);
            (maps[1]->*fn.ours)(dir);
            Compare(ctx, fn.name, seed, precision, State(*maps[0]), State(*maps[1]), {}, {});
        });
    }
    gNaNEvery = 0;
}

// ---- FloorVisible ---------------------------------------------------------------------------------------------------

DIFF_TEST(sg_sun_height_map_floor_visible, 0x3c06e0)
{
    using Fn = bool(__cdecl *)(void *, const WorldTile *, int32_t);
    auto original = ctx.Original<Fn>(0x3c06e0);
    ProgramDb(1.0f);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed);
        gSalt = gen();
        WorldTile tile;
        auto *raw = reinterpret_cast<uint32_t *>(&tile);
        for (int i = 0; i < 4; ++i) raw[i] = gen();
        tile.x = int32_t(gen() % 10), tile.y = int32_t(gen() % 10), tile.level = int32_t(gen() % 5) - 2;
        if (gen() % 2) tile.level = 0;
        const int32_t minLevel = int32_t(gen() % 4) - 2;
        gLog.clear();
        const bool ra = original(&gWorldDb, &tile, minLevel);
        const Blob logA = Normalised(gLog);
        gLog.clear();
        const bool rb = cSunHeightMap::FloorVisible(Db(), &tile, minLevel);
        const Blob logB = Normalised(gLog);
        Compare(ctx, "FloorVisible", seed, "-", {ra}, {rb}, logA, logB);
    }
}

// ---- BuildGround ----------------------------------------------------------------------------------------------------

DIFF_TEST(sg_sun_height_map_build_ground, 0x3bea70)
{
    using Fn = void(__cdecl *)(void *, void *, int32_t);
    auto original = ctx.Original<Fn>(0x3bea70);
    worldfx::Scope scope;
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    AtBothPrecisions(150, [&](uint32_t seed, const char *precision) {
        std::mt19937 gen(seed + 0x3bea70);
        gSalt = gen();
        // No NaN elevations: BuildGround only stores the ground heights, and the ground height (0x7e4680, lane RZ's
        // worldheights::GroundHeightAt) keeps a different NaN of two than the original's (a probe found 2 in 3000
        // points with NaN corners; backlog), which would fail this test for a callee's reason.
        gNaNEvery = 0;
        auto &world = worldgrid::gWorld;
        world.size = 1 + int32_t(gen() % 6);
        world.minLevel = int32_t(gen() % 3) - 1;
        world.maxLevel = world.minLevel + int32_t(gen() % 4);
        world.wall = gen() % 2 ? 3.0f : 2.5f;
        world.rough = gen() % 2 == 0;
        ProgramDb(RandomTileSize(gen));
        const int32_t n = 1 + int32_t(gen() % 3);
        MapPair maps(gen, int32_t(gen() % 6), int32_t(gen() % 6), 1.0f);
        gLog.clear();
        original(maps[0], &gWorldDb, n);
        const Blob logA = Normalised(gLog);
        gLog.clear();
        cSunHeightMap::BuildGround(maps[1], Db(), n);
        const Blob logB = Normalised(gLog);
        Compare(ctx, "BuildGround", seed, precision, State(*maps[0]), State(*maps[1]), logA, logB);
    });
    gNaNEvery = 0;
}

// ---- AddAwnings -----------------------------------------------------------------------------------------------------

DIFF_TEST(sg_sun_height_map_add_awnings, 0x3be1c0)
{
    using Fn = void(__cdecl *)(void *, void *, int32_t, const float *);
    auto original = ctx.Original<Fn>(0x3be1c0);
    svcfx::AllocatorScope allocator; // both sides keep the awning tiles in allocator-service storage
    worldfx::Scope scope;
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed + 0x3be1c0);
        gSalt = gen();
        gNaNEvery = gen() % 3 == 0 ? 9 : 0;
        gAwningEvery = 2 + gen() % 4;
        auto &world = worldgrid::gWorld;
        world.size = 1 + int32_t(gen() % 8);
        world.minLevel = int32_t(gen() % 3) - 1;
        world.maxLevel = world.minLevel + int32_t(gen() % 4);
        ProgramDb(RandomTileSize(gen));
        gWidth = world.size + int32_t(gen() % 5) - 3; // the clamp at the lot's edge, sometimes inside the tiles
        gHeight = world.size + int32_t(gen() % 5) - 3;
        if (gWidth < 0) gWidth = 0;
        if (gHeight < 0) gHeight = 0;
        const int32_t n = 1 + int32_t(gen() % 4);
        MapPair maps(gen, world.size * n, world.size * n, 1.0f / float(n));
        float dir[3];
        for (float &d : dir) d = Nice(gen, 2.0f); // finite: the box of a NaN corner would be anywhere
        gLog.clear();
        original(maps[0], &gWorldDb, n, dir);
        const Blob logA = Normalised(gLog);
        gLog.clear();
        cSunHeightMap::AddAwnings(maps[1], Db(), n, dir);
        const Blob logB = Normalised(gLog);
        Compare(ctx, "AddAwnings", seed, "-", State(*maps[0]), State(*maps[1]), logA, logB);
    }
    gNaNEvery = 0;
}

// ---- AddRoofs -------------------------------------------------------------------------------------------------------

namespace {

// A roof corner coordinate inside [lo, hi] tiles: whole, half, quarter or long-significand values.
float RoofCoord(std::mt19937 &gen, int32_t lo, int32_t hi)
{
    const float base = float(lo + int32_t(gen() % uint32_t(hi - lo)));
    switch (gen() % 5) {
    case 0: return base;
    case 1: return base + 0.5f;
    case 2: return base + 0.25f * float(gen() % 4);
    default: return base + float(gen() % 10000) / 10000.0f;
    }
}

} // namespace

DIFF_TEST(sg_sun_height_map_add_roofs, 0x3be5c0)
{
    using Fn = void(__cdecl *)(void *, void *, int32_t);
    auto original = ctx.Original<Fn>(0x3be5c0);
    svcfx::AllocatorScope allocator;
    worldfx::Scope scope;
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    constexpr int32_t kTiles = 32, kLo = 10, kHi = 21; // the roofs well inside the map (a diamond reaches past its box)
    AtBothPrecisions(300, [&](uint32_t seed, const char *precision) {
        std::mt19937 gen(seed + 0x3be5c0);
        gSalt = gen();
        gNaNEvery = gen() % 3 == 0 ? 13 : 0;
        ProgramDb(RandomTileSize(gen));
        ProgramRoofs();
        gRoofCount = gen() % 6;
        for (uint32_t i = 0; i < 8; ++i) {
            RoofRecord &r = gRoofRecords[i];
            for (uint32_t &w : r.words) w = gen();
            r.words[2] = U(RoofCoord(gen, kLo, kHi)), r.words[3] = U(RoofCoord(gen, kLo, kHi));
            r.words[5] = U(RoofCoord(gen, kLo, kHi)), r.words[6] = U(RoofCoord(gen, kLo, kHi));
            r.words[4] = gen() % 4;
            r.words[8] = gen() % 4 ? gen() % 26 : 0x13 + gen() % 3; // 15 skipped, 10-21 the diagonal kinds' range
        }
        const int32_t n = 1 + int32_t(gen() % 3);
        MapPair maps(gen, kTiles * n, kTiles * n, 1.0f / float(n));
        gLog.clear();
        original(maps[0], &gWorldDb, n);
        const Blob logA = Normalised(gLog);
        gLog.clear();
        cSunHeightMap::AddRoofs(maps[1], Db(), n);
        const Blob logB = Normalised(gLog);
        Compare(ctx, "AddRoofs", seed, precision, State(*maps[0]), State(*maps[1]), logA, logB);
    });
    gNaNEvery = 0;
}

// ---- AddWalls -------------------------------------------------------------------------------------------------------

namespace {

// A wall end coordinate that rounds to `v`: exact, a quarter off, nearly half off, or (v even) the tie that rounds to it.
float WallCoord(std::mt19937 &gen, int32_t v)
{
    const float f = float(v);
    switch (gen() % 6) {
    case 0: return f + 0.25f;
    case 1: return f - 0.25f;
    case 2: return f + (gen() % 2 ? 0.49f : -0.49f);
    case 3: return v % 2 == 0 ? f + (gen() % 2 ? 0.5f : -0.5f) : f;
    default: return f;
    }
}

void RandomWalls(std::mt19937 &gen, int32_t size)
{
    gWalls.assign(gen() % 7, Wall{});
    for (Wall &w : gWalls) {
        int32_t x0, y0, x1, y1;
        const uint32_t shape = gen() % 8;
        auto any = [&] { return int32_t(gen() % uint32_t(size + 1)); };
        auto inner = [&] { return 1 + int32_t(gen() % uint32_t(size - 1)); };
        if (shape < 3) { // along y
            x0 = x1 = any(), y0 = any(), y1 = any();
        } else if (shape < 6) { // along x
            y0 = y1 = any(), x0 = any(), x1 = any();
        } else { // a diagonal at 45 degrees, inside the lot's edge (its cells spill one either side)
            x0 = inner(), y0 = inner();
            const int32_t sx = gen() % 2 ? 1 : -1, sy = gen() % 2 ? 1 : -1;
            const int32_t roomX = sx > 0 ? size - 1 - x0 : x0 - 1, roomY = sy > 0 ? size - 1 - y0 : y0 - 1;
            const int32_t room = roomX < roomY ? roomX : roomY;
            const int32_t len = room > 0 ? 1 + int32_t(gen() % uint32_t(room)) : 0;
            x1 = x0 + sx * len, y1 = y0 + sy * len;
        }
        static const int32_t kLevels[] = {0, 0, 1, 2, 3, -1, 0, 1};
        const int32_t level0 = kLevels[gen() % 8];
        const int32_t level1 = gen() % 6 ? level0 : level0 + 1;
        w.words[0] = U(WallCoord(gen, x0)), w.words[1] = U(WallCoord(gen, y0)), w.words[2] = uint32_t(level0);
        w.words[3] = U(WallCoord(gen, x1)), w.words[4] = U(WallCoord(gen, y1)), w.words[5] = uint32_t(level1);
        w.id = gen() % 6 ? kWallIds[gen() % 7] : gen();
    }
}

} // namespace

DIFF_TEST(sg_sun_height_map_add_walls, 0x3bef50)
{
    using Fn = void(__cdecl *)(void *, void *, int32_t, const float *);
    auto original = ctx.Original<Fn>(0x3bef50);
    worldfx::Scope scope;
    worldfx::Catalog catalog;
    catalog.Plug(scope);
    scope.Set(worldfx::kWorldDbCell, &gWorldDb);
    AtBothPrecisions(800, [&](uint32_t seed, const char *precision) {
        std::mt19937 gen(seed + 0x3bef50);
        gSalt = gen();
        gNaNEvery = gen() % 3 == 0 ? 5 : 0;
        gLongElevations = seed % 2 == 0;
        auto &world = worldgrid::gWorld;
        world.size = 3 + int32_t(gen() % 8);
        world.minLevel = int32_t(gen() % 2) - 1;
        world.maxLevel = int32_t(gen() % 4);
        world.wall = gen() % 2 ? 3.0f : 2.5f;
        world.rough = gen() % 3 == 0;
        ProgramCatalog(catalog, gen);
        ProgramDb(RandomTileSize(gen));
        ProgramRoofs();
        ProgramPartitions();
        RandomWalls(gen, world.size);
        const int32_t n = 1 + int32_t(gen() % 3);
        MapPair maps(gen, world.size * n, world.size * n, 1.0f / float(n));
        float dir[3];
        for (float &d : dir) d = Nice(gen, 2.0f);
        if (gen() % 6 == 0) dir[gen() % 3] = NaN(gen);
        if (gen() % 5 == 0) dir[0] = 0.0f;
        if (gen() % 5 == 0) dir[1] = gen() % 2 ? 0.0f : -0.0f;
        const float *d = gen() % 4 == 0 ? nullptr : dir;
        gLog.clear();
        original(maps[0], &gWorldDb, n, d);
        const Blob logA = Normalised(gLog);
        gLog.clear();
        cSunHeightMap::AddWalls(maps[1], Db(), n, d);
        const Blob logB = Normalised(gLog);
        Compare(ctx, "AddWalls", seed, precision, State(*maps[0]), State(*maps[1]), logA, logB);
    });
    gNaNEvery = 0;
    gLongElevations = false;
}
