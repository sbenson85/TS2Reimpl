// Differential tests for the lot imposter's wall-slice record (lane 261111t, scenegraph row LT2a2a;
// src/sg/cImposterSliceRecord.cpp): the constructor (0x3f6610), the wall ends (0x3f66a0), the bottom / top / height /
// row count (0x3f69c0, 0x3f6b10, 0x3f6a80, 0x3f6a30), the angle test (0x3f6e30), the normal (0x3f69e0), the length
// (0x3f6aa0), the run's start, extension and walk (0x3f6b30, 0x3f6800, 0x3f6730) and the split into rows (0x3f6d00).
// Each side runs the original or ours on the same record; the walls and vertices are fakes that log every slot call
// (with their ids) and what they return, the ground height (0x7e4680) and segment end height (0x7e59c0) are stand-ins
// on both sides that log their arguments. Records and inputs are NaN-dense, with signed zeros and equal pairs (the
// min / max operand order), near-integer and half heights (the row rounding), angles at the 0.0001 boundary, and every
// case at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "hook/hooks.h"
#include "sg/cImposterSliceRecord.h"
#include "world/WorldHeights.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace lotimprecords_test {

using nTSSG::cImposterSliceRecord;
using nTSSG::cImposterWallEnds;
using Log = std::vector<uint32_t>;
Log gLog;
void L(uint32_t v) { gLog.push_back(v); }
uint32_t U(float f) { return std::bit_cast<uint32_t>(f); }
float Fl(uint32_t u) { return std::bit_cast<float>(u); }
void LogWords(const void *p, size_t words)
{
    for (size_t k = 0; k < words; ++k) {
        uint32_t w;
        std::memcpy(&w, static_cast<const uint8_t *>(p) + 4 * k, 4);
        L(w);
    }
}

// A float that tells min / max operand orders, roundings and NaN handling apart.
float Wild(std::mt19937 &r, float scale)
{
    switch (r() % 20) {
    case 0: return Fl(0x7fc00000u | (r() & 0x803fffffu)); // quiet NaN, either sign, payload
    case 1: return 0.0f;
    case 2: return -0.0f;
    case 3: return Fl(0x7f800000u | (r() & 0x80000000u)); // infinite
    case 4: return Fl((r() & 0x80000000u) | (1 + r() % 0x7fffffu)); // denormal
    case 5: return float(int32_t(r() % 17) - 8) * 0.5f;              // halves
    case 6: return std::nextafter(float(int32_t(r() % 9) - 4), r() % 2 ? 100.0f : -100.0f);
    default: return (float(int32_t(r() & 0xffffff) - 0x800000) / float(0x800000)) * scale;
    }
}
// A finite value: quarters, near-integers, plain.
float Tame(std::mt19937 &r, float scale)
{
    switch (r() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2:
    case 3: return float(int32_t(r() % 33) - 16) * 0.25f;
    case 4: return std::nextafter(float(int32_t(r() % 9) - 4), r() % 2 ? 100.0f : -100.0f);
    case 5: return Fl((r() & 0x807fffffu) | (uint32_t(127 - 12 + r() % 16) << 23)); // a full mantissa, 2^-12..2^3
    default: return (float(int32_t(r() & 0xffffff) - 0x800000) / float(0x800000)) * scale;
    }
}

// ---- the wall graph: pieces and vertices ----
struct FakeVertex {
    void **vt;
    uint32_t id;
    nTSWorld::WorldPoint p;
};
struct FakeWall {
    void **vt;
    uint32_t id;
    bool skipped, marked, markSkips;
    float angle;
    FakeVertex *start, *end;
    FakeWall *next, *prev;
};
void *gWallVT[40], *gVertexVT[8];

const nTSWorld::WorldPoint *__fastcall VertexPosition(FakeVertex *self, void *)
{
    L(0x5005), L(self->id);
    return &self->p;
}
void __fastcall WallSetMarked(FakeWall *self, void *, bool marked)
{
    L(0x5), L(self->id), L(marked);
    self->marked = marked;
}
FakeWall *__fastcall WallPrevious(FakeWall *self, void *)
{
    L(0x21), L(self->id), L(self->prev ? self->prev->id : 0);
    return self->prev;
}
FakeWall *__fastcall WallNext(FakeWall *self, void *)
{
    L(0x23), L(self->id), L(self->next ? self->next->id : 0);
    return self->next;
}
float __fastcall WallAngle(FakeWall *self, void *)
{
    L(0x30), L(self->id), L(U(self->angle));
    return self->angle;
}
bool __fastcall WallIsSkipped(FakeWall *self, void *)
{
    const bool skipped = self->skipped || (self->marked && self->markSkips);
    L(0x34), L(self->id), L(skipped);
    return skipped;
}
FakeVertex *__fastcall WallStart(FakeWall *self, void *)
{
    L(0x35), L(self->id), L(self->start ? self->start->id : 0);
    return self->start;
}
FakeVertex *__fastcall WallEnd(FakeWall *self, void *)
{
    L(0x36), L(self->id), L(self->end ? self->end->id : 0);
    return self->end;
}
void InitVTables()
{
    for (auto &s : gWallVT) s = nullptr;
    gWallVT[5] = reinterpret_cast<void *>(&WallSetMarked);
    gWallVT[21] = reinterpret_cast<void *>(&WallPrevious);
    gWallVT[23] = reinterpret_cast<void *>(&WallNext);
    gWallVT[30] = reinterpret_cast<void *>(&WallAngle);
    gWallVT[34] = reinterpret_cast<void *>(&WallIsSkipped);
    gWallVT[35] = reinterpret_cast<void *>(&WallStart);
    gWallVT[36] = reinterpret_cast<void *>(&WallEnd);
    gVertexVT[5] = reinterpret_cast<void *>(&VertexPosition);
}

// ---- the world height stand-ins ----
uint32_t gSalt;
uint32_t Hash(const void *p, size_t words, uint32_t salt)
{
    uint32_t h = salt * 2654435761u + 0x9e3779b9u;
    for (size_t k = 0; k < words; ++k) {
        uint32_t w;
        std::memcpy(&w, static_cast<const uint8_t *>(p) + 4 * k, 4);
        h = (h ^ w) * 0x01000193u;
        h ^= h >> 15;
    }
    return h;
}
double HeightFrom(uint32_t h, bool wild)
{
    std::mt19937 r(h);
    switch (r() % 12) {
    case 0: return wild ? std::numeric_limits<double>::quiet_NaN() : 1.0;
    case 1: return 0.0;
    case 2: return -0.0;
    case 3: return double(int32_t(r() % 9)) * 0.5;
    case 4: return 1.0 + 1e-12; // rounds to 1.0f
    case 5: return double(Fl(0x3f800001u)) + 1e-13;
    default: return double(int32_t(r() % 2000) - 400) / 128.0 + (r() % 2 ? 0.0 : 1e-9);
    }
}
bool gWildHeights;
double __cdecl StandGround(const nTSWorld::WorldPoint *point)
{
    L(0x7e4680), LogWords(point, 3);
    return HeightFrom(Hash(point, 3, gSalt), gWildHeights);
}
float __cdecl StandSegmentEnd(const float *segment, bool first)
{
    L(0x7e59c0), LogWords(segment, 6), L(first);
    return float(HeightFrom(Hash(segment, 6, gSalt + 1 + (first ? 7 : 0)), gWildHeights)) + 1.5f;
}

// A record with every field drawn (`wild`: NaNs and infinities among them).
void FillRecord(std::mt19937 &r, cImposterSliceRecord &rec, bool wild)
{
    uint32_t words[20];
    for (auto &w : words) w = r();
    std::memcpy(&rec, words, sizeof words);
    float *f = reinterpret_cast<float *>(&rec);
    for (int k = 0; k < 12; ++k) f[k] = wild ? Wild(r, 8.0f) : Tame(r, 8.0f);
    // Equal pairs and signed-zero pairs among the heights.
    if (r() % 4 == 0) rec.mBottomEnd.height = rec.mBottomStart.height;
    if (r() % 4 == 0) rec.mTopEnd.height = rec.mTopStart.height;
    if (r() % 6 == 0) rec.mBottomStart.height = 0.0f, rec.mBottomEnd.height = -0.0f;
    if (r() % 6 == 0) rec.mTopStart.height = -0.0f, rec.mTopEnd.height = 0.0f;
    rec.mColumns = int32_t(r() % 5);
    rec.mAngle = r() % 3 ? float(int32_t(r() % 8)) * 0.785398f : Wild(r, 4.0f);
}
void LogRecord(const cImposterSliceRecord &rec) { LogWords(&rec, 20); }

bool Same(difftest::Context &ctx, const char *what, uint32_t seed, const Log (&logs)[2])
{
    ++ctx.cases;
    if (logs[0] == logs[1]) return true;
    size_t k = 0;
    while (k < logs[0].size() && k < logs[1].size() && logs[0][k] == logs[1][k]) ++k;
    ctx.Fail("%s seed %u: the logs differ at word %zu of %zu / %zu (original %08x, ours %08x)", what, seed, k,
             logs[0].size(), logs[1].size(), k < logs[0].size() ? logs[0][k] : 0u, k < logs[1].size() ? logs[1][k] : 0u);
    return false;
}

// Seeds 0..cases at 53-bit, then 0x100000.. at 24-bit; `run(seed)` compares one case and returns false to stop.
template <class Run> void BothPrecisions(uint32_t cases, Run &&run)
{
    for (uint32_t seed = 0; seed < cases; ++seed)
        if (!run(seed)) return;
    difftest::X87Precision single(difftest::X87Pc::Bits24);
    for (uint32_t seed = 0; seed < cases; ++seed)
        if (!run(seed + 0x100000u)) return;
}

template <class Fn, class M> Fn Ours(M member) { return reinterpret_cast<Fn>(MemberAddress(member)); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

} // namespace lotimprecords_test

using namespace lotimprecords_test;

// The constructor, the bottom / top / height / row count, the angle test, the normal and the length on drawn records.
DIFF_TEST(ImposterRecords_Measures, 0x3f6a30)
{
    using CtorFn = void *(__thiscall *)(void *);
    using FloatFn = float(__thiscall *)(const void *);
    using DoubleFn = double(__thiscall *)(const void *);
    using IntFn = int32_t(__thiscall *)(const void *);
    using AngleFn = bool(__thiscall *)(const void *, float);
    using NormalFn = float *(__thiscall *)(const void *, float *);
    const CtorFn ctor[2] = {ctx.Original<CtorFn>(0x3f6610), reinterpret_cast<CtorFn>(&nTSSG::ConstructImposterSliceRecord)};
    const FloatFn minBottom[2] = {ctx.Original<FloatFn>(0x3f69c0), Ours<FloatFn>(&cImposterSliceRecord::MinBottom)};
    const FloatFn maxTop[2] = {ctx.Original<FloatFn>(0x3f6b10), Ours<FloatFn>(&cImposterSliceRecord::MaxTop)};
    const DoubleFn height[2] = {ctx.Original<DoubleFn>(0x3f6a80), Ours<DoubleFn>(&cImposterSliceRecord::Height)};
    const IntFn rows[2] = {ctx.Original<IntFn>(0x3f6a30), Ours<IntFn>(&cImposterSliceRecord::RowCount)};
    const AngleFn same[2] = {ctx.Original<AngleFn>(0x3f6e30), Ours<AngleFn>(&cImposterSliceRecord::SameAngle)};
    const NormalFn normal[2] = {ctx.Original<NormalFn>(0x3f69e0), Ours<NormalFn>(&cImposterSliceRecord::Normal)};
    const FloatFn length[2] = {ctx.Original<FloatFn>(0x3f6aa0), Ours<FloatFn>(&cImposterSliceRecord::Length)};
    int lessOne = 0, sameAngles = 0, boundary = 0, inexact = 0;
    BothPrecisions(20000, [&](uint32_t seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::mt19937 r(seed * 2246822519u + 0x3f6a30u);
            alignas(4) uint8_t raw[0x50];
            for (auto &b : raw) b = uint8_t(r());
            L(ctor[side](raw) == raw), LogWords(raw, 20);
            cImposterSliceRecord rec;
            FillRecord(r, rec, r() % 3 == 0);
            L(U(minBottom[side](&rec))), L(U(maxTop[side](&rec)));
            const double h = height[side](&rec);
            uint64_t hb = std::bit_cast<uint64_t>(h);
            L(uint32_t(hb)), L(uint32_t(hb >> 32));
            const bool finite = std::isfinite(h);
            L(uint32_t(rows[side](&rec)));
            if (side == 0 && seed >= 0x100000u) { // at 24-bit, a height the x87 rounds
                const float lo = rec.mBottomEnd.height < rec.mBottomStart.height ? rec.mBottomEnd.height : rec.mBottomStart.height;
                const float hi = rec.mTopEnd.height > rec.mTopStart.height ? rec.mTopEnd.height : rec.mTopStart.height;
                const double exact = double(hi) - double(lo);
                inexact += std::isfinite(exact) && double(float(exact)) != exact;
            }
            if (side == 0 && finite) {
                const float q = float(h);
                if (std::fabs(q) < 1e6f && float(int32_t(q)) == q && int32_t(q) % 2) ++lessOne; // odd: q + 0.5 rounds up
            }
            float angle = rec.mAngle;
            switch (r() % 6) {
            case 0: angle = Wild(r, 4.0f); break;
            case 1: angle = std::nextafter(rec.mAngle + 0.0001f, 100.0f); break;
            case 2: angle = rec.mAngle - 0.0001f; break;
            case 3: angle = rec.mAngle + (r() % 2 ? 0.00009f : -0.00011f); break;
            case 4: // exactly at the double 0.0001: the float 0.0001 is above it, the float below it under
                rec.mAngle = r() % 2 ? 0.0f : -0.0f;
                angle = (r() % 2 ? 1.0f : -1.0f) * (r() % 2 ? 0.0001f : std::nextafter(0.0001f, 0.0f));
                break;
            default: break;
            }
            const bool sameResult = same[side](&rec, angle);
            L(uint8_t(sameResult));
            if (side == 0) {
                const double d = std::fabs(double(rec.mAngle - angle));
                sameAngles += sameResult;
                boundary += d > 9e-5 && d < 1.1e-4;
            }
            float out[4] = {Fl(0xcdcdcdcd), Fl(0xcdcdcdcd), Fl(0xcdcdcdcd), Fl(0xcdcdcdcd)};
            L(normal[side](&rec, out) == out), LogWords(out, 4);
            L(U(length[side](&rec)));
            LogRecord(rec);
            logs[side] = gLog;
        }
        return Same(ctx, "measures", seed, logs) || ctx.failures.size() < 6;
    });
    if (lessOne < 500 || sameAngles < 4000 || boundary < 2000 || inexact < 500)
        ctx.Fail("coverage: row count one less %d, same angles %d, at the boundary %d, rounded at 24-bit %d", lessOne,
                 sameAngles, boundary, inexact);
}

namespace lotimprecords_test {

// A chain or ring of up to 7 wall pieces over their vertices, drawn from `r`.
struct Graph {
    FakeVertex vertices[8];
    FakeWall walls[7];
    int count;
    int gate;
};
void BuildGraph(std::mt19937 &r, Graph &g, bool nullVertices)
{
    g.count = 1 + int(r() % 7);
    const bool ring = r() % 3 == 0;
    for (int i = 0; i < 8; ++i) {
        FakeVertex &v = g.vertices[i];
        v.vt = gVertexVT, v.id = 0x100 + uint32_t(i);
        v.p.x = Tame(r, 16.0f), v.p.y = Tame(r, 16.0f), v.p.level = int32_t(r() % 5) - 1;
        if (i && r() % 6 == 0) v.p = g.vertices[i - 1].p;
    }
    const float base = r() % 4 ? float(int32_t(r() % 8)) * 0.785398f : Wild(r, 4.0f);
    for (int i = 0; i < g.count; ++i) {
        FakeWall &w = g.walls[i];
        w.vt = gWallVT, w.id = 0x200 + uint32_t(i);
        w.skipped = r() % 7 == 0;
        w.marked = r() % 9 == 0;
        w.markSkips = r() % 3 != 0 || ring; // a ring whose marks don't stop the walk never ends (on both sides)
        switch (r() % 9) {
        case 0: w.angle = Wild(r, 4.0f); break;
        case 1: w.angle = base + 0.00009f; break;
        case 2: w.angle = base - 0.00011f; break;
        case 3: w.angle = std::nextafter(base + 0.0001f, r() % 2 ? 100.0f : -100.0f); break;
        default: w.angle = base; break;
        }
        w.start = &g.vertices[i];
        w.end = &g.vertices[(ring && i + 1 == g.count) ? 0 : i + 1];
        if (nullVertices && r() % 4 == 0) (r() % 2 ? w.start : w.end) = nullptr;
    }
    for (int i = 0; i < g.count; ++i) {
        FakeWall &w = g.walls[i];
        const int n = i + 1 == g.count ? (ring ? 0 : -1) : i + 1;
        const int p = i == 0 ? (ring ? g.count - 1 : -1) : i - 1;
        w.next = n < 0 || r() % 10 == 0 ? nullptr : &g.walls[n];
        w.prev = p < 0 || r() % 10 == 0 ? nullptr : &g.walls[p];
    }
}
void LogGraph(const Graph &g)
{
    L(0x9a9a);
    for (int i = 0; i < g.count; ++i) L(g.walls[i].marked);
}

} // namespace lotimprecords_test

// The wall ends, the run's start, its extension at either side and the walk through a wall's neighbours.
DIFF_TEST(ImposterRecords_Walls, 0x3f6730)
{
    using EndsFn = void *(__thiscall *)(void *, void *);
    using BeginFn = bool(__thiscall *)(void *, void *, void *);
    using ExtendFn = bool(__thiscall *)(void *, int32_t, void *, void *);
    using BuildFn = bool(__thiscall *)(void *, void *, void *, int32_t);
    const EndsFn ends[2] = {ctx.Original<EndsFn>(0x3f66a0), Ours<EndsFn>(&cImposterWallEnds::Init)};
    const BeginFn begin[2] = {ctx.Original<BeginFn>(0x3f6b30), Ours<BeginFn>(&cImposterSliceRecord::Begin)};
    const ExtendFn extend[2] = {ctx.Original<ExtendFn>(0x3f6800), Ours<ExtendFn>(&cImposterSliceRecord::Extend)};
    const BuildFn build[2] = {ctx.Original<BuildFn>(0x3f6730), Ours<BuildFn>(&cImposterSliceRecord::Build)};
    InitVTables();
    shape::StandIn ground{0x7e4680u, &worldheights::GroundHeightAt, &StandGround};
    shape::StandIn segmentEnd{0x7e59c0u, &worldheights::SegmentEndHeight, &StandSegmentEnd};
    int counts[4][2] = {}; // per operation: false, true
    int longRuns = 0;
    BothPrecisions(15000, [&](uint32_t seed) {
        Log logs[2];
        int op = 0;
        bool result = false;
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::mt19937 r(seed * 2654435761u + 0x3f6730u);
            gSalt = r(), gWildHeights = r() % 3 == 0;
            op = int(r() % 4);
            static Graph g;
            BuildGraph(r, g, op == 0);
            FakeWall *wall = r() % 12 ? &g.walls[r() % uint32_t(g.count)] : nullptr;
            void *gate = r() % 12 ? &g.gate : nullptr;
            cImposterSliceRecord rec;
            FillRecord(r, rec, r() % 3 == 0);
            switch (op) {
            case 0: {
                alignas(4) uint8_t raw[0x18];
                for (auto &b : raw) b = uint8_t(r());
                L(ends[side](raw, wall) == raw), LogWords(raw, 6);
                result = true;
                break;
            }
            case 1: result = begin[side](&rec, wall, gate); break;
            case 2: {
                const int32_t which = int32_t(r() % 3); // 2: any other non-zero side
                rec.mAngle = wall && r() % 4 ? wall->angle : rec.mAngle;
                result = extend[side](&rec, which, wall, gate);
                break;
            }
            default: {
                static const int32_t kMax[] = {-1, -5, 0, 1, 2, 3, 4, 100};
                if (r() % 5) rec.mColumns = 0;
                result = build[side](&rec, wall, gate, kMax[r() % 8]);
                break;
            }
            }
            L(0xe0d), L(uint8_t(result)), LogRecord(rec), LogGraph(g);
            logs[side] = gLog;
            if (side == 0) {
                ++counts[op][result ? 1 : 0];
                if (op == 3 && result && rec.mColumns >= 3) ++longRuns;
            }
        }
        return Same(ctx, "walls", seed, logs) || ctx.failures.size() < 6;
    });
    for (int op = 1; op < 4; ++op)
        if (counts[op][0] < 500 || counts[op][1] < 500)
            ctx.Fail("coverage: operation %d false %d, true %d", op, counts[op][0], counts[op][1]);
    if (longRuns < 300) ctx.Fail("coverage: runs of 3 or more %d", longRuns);
}

// The split into rows: the copies, their bottoms and tops, a vector of any size and capacity before, and a record
// inside the vector it splits into (its bottom and top read again on every row).
DIFF_TEST(ImposterRecords_Split, 0x3f6d00)
{
    using SplitFn = void(__thiscall *)(const void *, float, void *);
    const SplitFn split[2] = {ctx.Original<SplitFn>(0x3f6d00), Ours<SplitFn>(&cImposterSliceRecord::Split)};
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    int one = 0, many = 0, none = 0, aliased = 0;
    BothPrecisions(15000, [&](uint32_t seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::mt19937 r(seed * 40503u + 0x3f6d00u);
            cImposterSliceRecord rec;
            float h = 1.0f;
            bool ok = false;
            for (int tries = 0; tries < 20 && !ok; ++tries) {
                FillRecord(r, rec, false);
                static const float kRows[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 0.75f, 1.25f};
                h = r() % 4 ? kRows[r() % 8] : std::fabs(Tame(r, 4.0f));
                if (r() % 40 == 0) h = Fl(0x7f800000u);
                const double q = (double(rec.MaxTop()) - double(rec.MinBottom())) / double(h);
                ok = q > -0.9 && q < 40.0;
            }
            if (!ok) h = Fl(0x7f800000u);
            std::vector<cImposterSliceRecord> v;
            const bool alias = r() % 5 == 0;
            const size_t size = alias ? 50 : r() % 6;
            if (!alias && r() % 2) v.reserve(size + r() % 40);
            for (size_t k = 0; k < size; ++k) {
                cImposterSliceRecord e;
                FillRecord(r, e, true);
                v.push_back(e);
            }
            const cImposterSliceRecord *self = &rec;
            if (alias) {
                const size_t k = r() % 3;
                v[k] = rec;
                self = &v[k];
            }
            split[side](self, h, &v);
            L(0xe0d), L(uint32_t(v.size())), L(uint32_t(v.capacity()));
            for (const auto &e : v) LogRecord(e);
            LogRecord(rec);
            logs[side] = gLog;
            if (side == 0) {
                (v.empty() ? none : v.size() == 1 ? one : many)++;
                aliased += alias && v.size() > 1;
            }
        }
        return Same(ctx, "split", seed, logs) || ctx.failures.size() < 6;
    });
    if (one < 1000 || many < 5000 || none < 500 || aliased < 1000)
        ctx.Fail("coverage: one row %d, more %d, none %d, inside its vector %d", one, many, none, aliased);
}
