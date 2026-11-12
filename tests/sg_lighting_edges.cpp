// Differential tests for nTSSG::cLightingManager's edge builders (lane 261109n, scenegraph LG4e;
// src/sg/cLightingManagerEdges.cpp): CurvedPoolEdges 0x2d94a0, BlendLotEdges 0x2da3b0 and ShadePortals 0x2d71f0. Hook
// shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// CurvedPoolEdges: tiles with small, negative and past-2^24 coordinates, every direction 0..7 and some large ones; the
// four outputs canary-filled, now and then the same buffer twice (the write order shows).
//
// BlendLotEdges: tests/sg_lighting_tiles.h's lightmaps at +0x24 (slot 22 the texture, three textures over gTexture with
// pitches whose low two bits are noise) plus slot 21 here (logs the segment, answers the next scripted rectangle); the
// world DB cell 0x10cc45c holds a fake whose slot 38 gives a curved pool fake or null; the pool's slot 11 lists 0 to 6
// edge ids on the allocator service, slot 10 gives the edge (a tile and a direction, odd and large ones too). The
// script per edge: a's and b's rectangles (now and then none, or no rows: the pass stops), c's and d's with at most as
// many rows as the columns (the original reads past them otherwise), widths 0 to 9, texture indexes 0 to 2 so the
// fetch is skipped or repeated. Compared: the log, the object and the whole texture buffer.
//
// ShadePortals: 0 to 9 records over a buffer (the vector's three words), samples 0 to 2 and -1, levels 0 / 1 / noise,
// segments on a half grid, unit normals now and then NaN, the reach, the two shades and the old shades drawn (NaNs,
// zeros, infinities among them). Compared: the object and the records.
#include "sg_lighting_tiles.h"

#include "rz/GameAllocator.h"
#include "simantics/ServiceRefVector.h"

#include <vector>

namespace {

using nTSSG::cPortalRecord;

// --- CurvedPoolEdges ---------------------------------------------------------------------------------------------

struct FakeEdge {
    uint32_t junk;
    int32_t tile[3];
    uint32_t direction;
};

FakeEdge DrawEdge(std::mt19937 &gen)
{
    FakeEdge e;
    e.junk = gen();
    for (int i = 0; i < 2; ++i) {
        switch (gen() % 6) {
        case 0: e.tile[i] = int32_t(gen()); break;
        case 1: e.tile[i] = int32_t((1u << 24) + gen() % 7) * (gen() % 2 ? 1 : -1); break;
        case 2: e.tile[i] = int32_t(gen() % 9) - 4; break;
        default: e.tile[i] = int32_t(gen() % 20); break;
        }
    }
    e.tile[2] = int32_t(gen() % 4 ? gen() % 4 : gen());
    static const uint32_t kOdd[] = {7, 8, 9, 0xffffffffu, 0x80000000u, 0x100};
    e.direction = gen() % 6 ? gen() % 8 : kOdd[gen() % 6];
    return e;
}

// --- BlendLotEdges -----------------------------------------------------------------------------------------------

constexpr uint32_t kWorldDbCell = 0x10cc45c;

struct ScriptRect {
    uint32_t x0, y0, x1, y1;
    int32_t texture;
    bool found;
};
std::vector<ScriptRect> gRects;
uint32_t gNextRect;

bool __fastcall EdgeRectSlot(FakeLightmaps *, void *, const float *segment, uint32_t *x0, uint32_t *y0, uint32_t *x1,
                             uint32_t *y1, int32_t *texture)
{
    gLog.push_back(0x21);
    for (int i = 0; i < 6; ++i) gLog.push_back(U(segment[i]));
    const ScriptRect &r = gRects[gNextRect++ % gRects.size()];
    *x0 = r.x0, *y0 = r.y0, *x1 = r.x1, *y1 = r.y1, *texture = r.texture;
    return r.found;
}

struct FakePools {
    void **vtable;
    std::vector<uint32_t> ids;
    FakeEdge edges[8];
} gPools;
void *gPoolsVT[12];

void __fastcall IdsSlot(FakePools *self, void *, ServiceBlockVector *out)
{
    gLog.push_back(0x111), gLog.push_back(P(out->mFirst)), gLog.push_back(P(out->mLast));
    if (self->ids.empty()) return;
    const uint32_t bytes = uint32_t(self->ids.size() * 4);
    auto *block = static_cast<uint32_t *>(as2::AllocateViaAllocatorService(bytes, 0));
    std::memcpy(block, self->ids.data(), bytes);
    out->mFirst = block, out->mLast = block + self->ids.size(), out->mEnd = block + self->ids.size();
}
const FakeEdge *__fastcall EdgeSlot(FakePools *self, void *, uint32_t id)
{
    gLog.push_back(0x110), gLog.push_back(id);
    return &self->edges[id % 8];
}

struct FakeWorldDb {
    void **vtable;
    void *pools;
} gWorldDb;
void *gWorldDbVT[40];
void *__fastcall PoolsSlot(FakeWorldDb *self, void *)
{
    gLog.push_back(0x138);
    return self->pools;
}

struct CellSave {
    uint32_t *cell = GameAddress<uint32_t>(kWorldDbCell);
    uint32_t saved = *cell;
    CellSave() = default;
    ~CellSave() { *cell = saved; }
    CellSave(const CellSave &) = delete;
    CellSave &operator=(const CellSave &) = delete;
};

void FillEdgeFakes()
{
    gLightmaps.vtable = gLightmapsVT;
    gLightmapsVT[21] = reinterpret_cast<void *>(&EdgeRectSlot);
    gLightmapsVT[22] = reinterpret_cast<void *>(&TextureSlot);
    for (auto &t : gTextures) t.vtable = gTextureVT;
    gTextureVT[9] = reinterpret_cast<void *>(&TexelsSlot);
    gTextureVT[27] = reinterpret_cast<void *>(&HeightSlot);
    gTextureVT[28] = reinterpret_cast<void *>(&PitchSlot);
    gPools.vtable = gPoolsVT;
    gPoolsVT[10] = reinterpret_cast<void *>(&EdgeSlot);
    gPoolsVT[11] = reinterpret_cast<void *>(&IdsSlot);
    gWorldDb.vtable = gWorldDbVT;
    gWorldDbVT[38] = reinterpret_cast<void *>(&PoolsSlot);
}

// A rectangle of `rows` rows from y0, x0..x0 + width, in texture `texture`.
ScriptRect RectOf(std::mt19937 &gen, uint32_t rows, uint32_t width, bool found = true)
{
    ScriptRect r;
    r.x0 = gen() % 12, r.y0 = gen() % 8;
    r.x1 = r.x0 + width, r.y1 = r.y0 + rows;
    r.texture = int32_t(gen() % 3);
    r.found = found;
    return r;
}

void SetUpBlend(uint32_t seed)
{
    std::mt19937 gen(seed * 7919u + 0x2da3b0u);
    for (auto &b : gObject) b = uint8_t(gen());
    Put(0x24, P(&gLightmaps));
    FillTexture(gen());
    // three textures over gTexture: 48 x 48, 24 x 96, 32 x 64 (the pitch's low bits noise)
    static const uint32_t kPitch[] = {48, 24, 32}, kHeight[] = {48, 96, 64};
    for (int i = 0; i < 3; ++i) {
        gTextures[i].texels = gTexture;
        gTextures[i].pitchBytes = kPitch[i] * 4 + gen() % 4;
        gTextures[i].height = kHeight[i];
    }
    gLog.clear();
    gWorldDb.pools = gen() % 16 ? &gPools : nullptr;
    *GameAddress<uint32_t>(kWorldDbCell) = gen() % 32 ? P(&gWorldDb) : 0u;
    for (auto &e : gPools.edges) e = DrawEdge(gen);
    gPools.ids.clear();
    const uint32_t edges = gen() % 8 ? gen() % 7 : 0;
    for (uint32_t i = 0; i < edges; ++i) gPools.ids.push_back(gen() % 4 ? gen() % 8 : gen());
    // the script, edge by edge; a and b give columns of at least the rows c and d blend
    gRects.clear(), gNextRect = 0;
    for (uint32_t i = 0; i < edges; ++i) {
        const unsigned roll = gen() % 16;
        const uint32_t rows = 1 + gen() % 8;
        // a's column is x1 - 1: at least one column wide
        gRects.push_back(RectOf(gen, roll == 0 ? 0 : rows + (gen() % 3 ? 0 : gen() % 3), 1 + gen() % 4, roll != 1));
        gRects.push_back(RectOf(gen, roll == 2 ? 0 : rows + (gen() % 3 ? 0 : gen() % 3), gen() % 4, roll != 3));
        const uint32_t widths[] = {0, 1, 2, 3, 4, 5, 9};
        gRects.push_back(RectOf(gen, gen() % 4 ? rows : gen() % (rows + 1), widths[gen() % 7], gen() % 8 != 0));
        gRects.push_back(RectOf(gen, gen() % 4 ? rows : gen() % (rows + 1), widths[gen() % 7], gen() % 8 != 0));
        if (gen() % 8 == 0) std::swap(gRects[gRects.size() - 1].x0, gRects[gRects.size() - 1].x1); // x0 above x1: none
    }
    if (gRects.empty()) gRects.push_back(RectOf(gen, 0, 0, false));
}

// --- ShadePortals ------------------------------------------------------------------------------------------------

constexpr unsigned kRecords = 9;
alignas(16) uint8_t gRecords[kRecords * sizeof(cPortalRecord)];
struct RecordVector {
    uint8_t *first, *last, *end;
} gRecordVector;

float Coord(std::mt19937 &gen)
{
    return gen() % 8 ? float(gen() % 9) * 0.5f : Nice(gen, 4.0f);
}

void SetUpShade(uint32_t seed)
{
    std::mt19937 gen(seed * 7919u + 0x2d71f0u);
    for (auto &b : gObject) b = uint8_t(gen());
    for (auto &b : gRecords) b = uint8_t(gen());
    const unsigned n = gen() % 8 ? gen() % (kRecords + 1) : 0;
    gRecordVector = {gRecords, gRecords + n * sizeof(cPortalRecord), gRecords + kRecords * sizeof(cPortalRecord)};
    static const int16_t kSamples[] = {0, 1, 1, 2, -1};
    const uint32_t levelNoise = gen();
    for (unsigned i = 0; i < n; ++i) {
        auto *r = reinterpret_cast<cPortalRecord *>(gRecords + i * sizeof(cPortalRecord));
        r->mSample = gen() % 6 ? kSamples[gen() % 5] : int16_t(gen());
        float *const s = r->mSegment;
        s[0] = Coord(gen), s[1] = Coord(gen), s[3] = Coord(gen), s[4] = Coord(gen);
        const uint32_t level = gen() % 6 ? gen() % 2 : levelNoise;
        std::memcpy(&s[2], &level, 4);
        if (gen() % 2) std::memcpy(&s[5], &level, 4);
        const float angle = float(gen() % 16) * 0.39269908f;
        r->mNormal[0] = gen() % 6 ? std::cos(angle) : Nice(gen, 1.0f);
        r->mNormal[1] = gen() % 6 ? std::sin(angle) : Nice(gen, 1.0f);
        if (gen() % 24 == 0) r->mNormal[gen() % 2] = NaN(gen);
        if (gen() % 24 == 0) s[gen() % 2 ? 0 : 4] = NaN(gen);
        if (gen() % 16 == 0) { // both ends NaN (differing payloads): the midpoint sum's operand order shows
            const int axis = int(gen() % 2);
            s[axis] = NaN(gen), s[axis + 3] = NaN(gen);
        }
    }
    if (n >= 2 && gen() % 4 == 0) { // a facing pair exactly 0.1f apart (zero-length segments: exact midpoints)
        auto *r0 = reinterpret_cast<cPortalRecord *>(gRecords);
        auto *r1 = reinterpret_cast<cPortalRecord *>(gRecords + sizeof(cPortalRecord));
        const float x = gen() % 2 ? 0.0f : float(gen() % 5), y = float(gen() % 5);
        const float offset[] = {0.1f, 0.0999999940395f, 0.100000008941f};
        const float gap = offset[gen() % 3];
        r0->mSample = r1->mSample = 1;
        r0->mSegment[0] = r0->mSegment[3] = x, r0->mSegment[1] = r0->mSegment[4] = y;
        r1->mSegment[0] = r1->mSegment[3] = x + gap, r1->mSegment[1] = r1->mSegment[4] = y;
        std::memcpy(&r1->mSegment[2], &r0->mSegment[2], 4);
        r0->mNormal[0] = 1.0f, r0->mNormal[1] = 0.0f;
        r1->mNormal[0] = -1.0f, r1->mNormal[1] = 0.0f;
    }
    static const float kSpecial[] = {0.0f, 1.0f, 0.1f, 1e30f, -1.0f};
    const auto draw = [&](float scale) {
        switch (gen() % 10) {
        case 0: return kSpecial[gen() % 5];
        case 1: return gen() % 2 ? NaN(gen) : Fl(0x7f800000u);
        default: return float(gen() % 2000) / 2000.0f * scale;
        }
    };
    PutF(0x1dc, draw(6.0f)), PutF(0x1e0, draw(1.0f)), PutF(0x1e4, draw(1.0f));
}

Blob ShadeEnd()
{
    Blob o = End(nullptr, 0);
    o.push_back(0xeeee);
    for (unsigned i = 0; i < sizeof(gRecords); i += 4) {
        uint32_t w;
        std::memcpy(&w, gRecords + i, 4);
        o.push_back(w);
    }
    return o;
}

} // namespace

DIFF_TEST(sg_lighting_edges_curved_pool, 0x2d94a0)
{
    using Fn = void(__stdcall *)(const void *, float *, float *, float *, float *);
    auto original = ctx.Original<Fn>(0x2d94a0);
    AtBothPrecisions(ctx, "curved pool edges", 20000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2d94a0u);
        const FakeEdge edge = DrawEdge(gen);
        const unsigned alias = gen() % 8; // 0..3: one output given twice
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            uint32_t out[4][6];
            std::mt19937 fill(seed);
            for (auto &row : out) for (auto &w : row) w = fill();
            float *p[4];
            for (int i = 0; i < 4; ++i) p[i] = reinterpret_cast<float *>(out[i]);
            if (alias < 4) p[alias] = p[(alias + 1) % 4];
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(&edge, p[0], p[1], p[2], p[3]);
            else cLightingManager::CurvedPoolEdges(&edge, p[0], p[1], p[2], p[3]);
            outcome[side].assign(&out[0][0], &out[0][0] + 24);
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

DIFF_TEST(sg_lighting_edges_blend, 0x2da3b0)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2da3b0);
    svcfx::AllocatorScope allocator;
    CellSave cell;
    FillEdgeFakes();
    AtBothPrecisions(ctx, "lot edges", 4000, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            SetUpBlend(seed);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject);
            else Ours()->BlendLotEdges();
            outcome[side] = TextureEnd();
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

DIFF_TEST(sg_lighting_edges_shade_portals, 0x2d71f0)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x2d71f0);
    AtBothPrecisions(ctx, "shade portals", 20000, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            SetUpShade(seed);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject, &gRecordVector);
            else Ours()->ShadePortals(reinterpret_cast<std::vector<cPortalRecord> *>(&gRecordVector));
            outcome[side] = ShadeEnd();
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}
