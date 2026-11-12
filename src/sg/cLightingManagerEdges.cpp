// nTSSG::cLightingManager, the edge builders (lane 261109n, scenegraph LG4e): the curved pool edges' lightmap blend and
// the portal records' shading the portal-light builders start with. See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/original.h"
#include "game/vtable.h"
#include "simantics/ServiceRefVector.h"
#include "world/WorldService.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kNoLevel = 0xffffffffu; // the segments' levels (int32 -1)

// A segment {x0, y0, level, x1, y1, level} with both levels -1.
void PutSegment(float *out, float x0, float y0, float x1, float y1)
{
    out[0] = x0, out[1] = y0, out[3] = x1, out[4] = y1;
    std::memcpy(out + 2, &kNoLevel, 4);
    std::memcpy(out + 5, &kNoLevel, 4);
}

// The 49152.0 rounding: v * 256 rounded to an integer read off the float's bits, clamped to [0, 255].
uint32_t ChannelByte(float v)
{
    const uint32_t bits = BitsOf(v + 49152.0f) + 0xb8c00000u;
    const uint32_t low = static_cast<int32_t>(bits) < 0 ? 0u : bits;
    const uint32_t high = static_cast<int32_t>(0xffu - bits) < 0 ? 0xffu : 0u;
    return (low | high) & 0xffu;
}

// A texel's four bytes, high to low, as fractions of 255.
void Channels(uint32_t texel, float *out)
{
    out[0] = static_cast<float>(static_cast<int32_t>(texel >> 24)) / 255.0f;
    out[1] = static_cast<float>(static_cast<int32_t>((texel >> 16) & 0xffu)) / 255.0f;
    out[2] = static_cast<float>(static_cast<int32_t>((texel >> 8) & 0xffu)) / 255.0f;
    out[3] = static_cast<float>(static_cast<int32_t>(texel & 0xffu)) / 255.0f;
}

// 0x19d250 (__cdecl, still the game's): a 2D dot product on the x87 stack, b's y times a's y plus the x's.
double Dot2(const float *a, const float *b)
{
    return OriginalFunction<double(__cdecl *)(const float *, const float *), 0x19d250>()(a, b);
}

// A lightmap rectangle mRef24's slot 21 gives for a segment, and the texture it is in.
struct EdgeRect {
    uint32_t x0, y0, x1, y1;
    int32_t texture;
};

} // namespace

void __stdcall cLightingManager::CurvedPoolEdges(const void *edge, float *a, float *c, float *b, float *d)
{
    const uint8_t *const bytes = static_cast<const uint8_t *>(edge);
    float tile[3];
    worldmath::TileToFloats(tile, reinterpret_cast<const int32_t *>(bytes + 4));
    const float x = tile[0], y = tile[1];
    uint32_t direction;
    std::memcpy(&direction, bytes + 0x10, 4);
    switch (direction) {
    case 0:
        PutSegment(a, x - 1.0f, y + 1.0f, x, y + 1.0f);
        PutSegment(b, x + 1.0f, y, x + 1.0f, y - 1.0f);
        PutSegment(c, x, y + 1.0f, x + 1.0f, y + 1.0f);
        PutSegment(d, x + 1.0f, y + 1.0f, x + 1.0f, y);
        break;
    case 2:
        PutSegment(a, x - 1.0f, y - 1.0f, x - 1.0f, y);
        PutSegment(b, x, y + 1.0f, x + 1.0f, y + 1.0f);
        PutSegment(c, x - 1.0f, y, x - 1.0f, y + 1.0f);
        PutSegment(d, x - 1.0f, y + 1.0f, x, y + 1.0f);
        break;
    case 4:
        PutSegment(a, x + 1.0f, y - 1.0f, x, y - 1.0f);
        PutSegment(b, x - 1.0f, y, x - 1.0f, y + 1.0f);
        PutSegment(c, x, y - 1.0f, x - 1.0f, y - 1.0f);
        PutSegment(d, x - 1.0f, y - 1.0f, x - 1.0f, y);
        break;
    case 6:
        PutSegment(a, x + 1.0f, y + 1.0f, x + 1.0f, y);
        PutSegment(b, x, y - 1.0f, x - 1.0f, y - 1.0f);
        PutSegment(c, x + 1.0f, y, x + 1.0f, y - 1.0f);
        PutSegment(d, x + 1.0f, y - 1.0f, x, y - 1.0f);
        break;
    default:
        break;
    }
}

void cLightingManager::BlendLotEdges()
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    if (!db) return;
    void *const pools = db->GetCurvedPools(); // nTSWorld::cWorldCurvedPool
    if (!pools) return;

    ServiceBlockVector ids{};
    RawSlot<11, void>(pools, &ids); // receiver: nTSWorld::cWorldCurvedPool (lists its edges' ids)
    const uint32_t *const end = static_cast<const uint32_t *>(ids.mLast);

    // The texture last fetched (none yet: index -1, no texels) carries over from edge to edge.
    int32_t loaded = -1;
    uint32_t *texels = nullptr;
    int32_t stride = 0;
    EdgeRect r;
    const auto rectOf = [&](const float *segment) {
        // receiver: the lightmaps (mRef24, class not identified), slot 21 a wall segment's texel rectangle
        if (!RawSlot<21, bool>(mRef24, segment, &r.x0, &r.y0, &r.x1, &r.y1, &r.texture)) return false;
        if (r.texture != loaded) {
            LoadTexture(0, static_cast<uint32_t>(r.texture), texels, stride);
            loaded = r.texture;
        }
        return true;
    };
    const auto texel = [&](uint32_t x, uint32_t y) -> uint32_t * {
        return texels + static_cast<int32_t>(y * static_cast<uint32_t>(stride) + x);
    };

    for (const uint32_t *id = static_cast<const uint32_t *>(ids.mFirst); id != end; ++id) {
        const void *const edge = RawSlot<10, const void *>(pools, *id); // receiver: nTSWorld::cWorldCurvedPool
        float a[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
        float b[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
        float c[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
        float d[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
        CurvedPoolEdges(edge, a, c, b, d);

        // The column at a's inner end (its last), and at b's (its first), top to bottom.
        std::vector<uint32_t> columns[2];
        if (rectOf(a)) {
            for (uint32_t y = r.y0; y < r.y1; ++y) columns[0].push_back(*texel(r.x1 - 1, y));
        }
        if (rectOf(b)) {
            for (uint32_t y = r.y0; y < r.y1; ++y) columns[1].push_back(*texel(r.x0, y));
        }
        // ORIGINAL-BUG: an edge with nothing in either column ends the whole pass, not just this edge.
        if (columns[0].empty() || columns[1].empty()) break;

        // c's and d's rows blend from a's texel to b's: c over the first half of the way, d the second.
        // ORIGINAL-BUG: a row of c or d past the columns' ends reads past them.
        for (int half = 0; half < 2; ++half) {
            if (!rectOf(half ? d : c)) continue;
            const uint32_t span = r.x1 - r.x0;
            const float width = static_cast<float>(static_cast<double>(span));
            const float steps = std::max(2.0f, width + width) - 1.0f;
            for (uint32_t y = r.y0; y < r.y1; ++y) {
                float from[4], to[4], delta[4];
                Channels(columns[0].data()[y - r.y0], from);
                Channels(columns[1].data()[y - r.y0], to);
                for (int i = 0; i < 4; ++i) delta[i] = to[i] - from[i];
                uint32_t *out = texel(r.x0, y);
                for (uint32_t x = r.x0; x < r.x1; ++x) {
                    const uint32_t step = half ? x - r.x0 + span : x - r.x0;
                    const float t = static_cast<float>(static_cast<int32_t>(step)) / steps;
                    uint32_t value = 0;
                    for (int i = 0; i < 4; ++i) value = value << 8 | ChannelByte(t * delta[i] + from[i]);
                    *out++ = value;
                }
            }
        }
    }
    ids.FreeBlock();
}

void cLightingManager::ShadePortals(std::vector<cPortalRecord> *records)
{
    for (cPortalRecord &record : *records) record.mShade = 1.0f;
    cPortalRecord *const first = records->data();
    cPortalRecord *const last = first + records->size();
    for (cPortalRecord *r1 = first; r1 != last; ++r1) {
        if (r1->mSample == 0) continue;
        for (cPortalRecord *r2 = first; r2 != last; ++r2) {
            if (r2 == r1 || r2->mSample != r1->mSample) continue;
            if (BitsOf(r2->mSegment[2]) != BitsOf(r1->mSegment[2])) continue; // the same level
            // From r1's midpoint to r2's.
            const auto mid = [](const cPortalRecord *r, int axis) {
                return static_cast<float>(static_cast<double>(AddS(r->mSegment[axis], r->mSegment[axis + 3])) * 0.5);
            };
            const float between[2] = {SubS(mid(r2, 0), mid(r1, 0)), SubS(mid(r2, 1), mid(r1, 1))};
            // r1 faces r2 and r2 faces r1 (a NaN dot passes).
            const float towards = static_cast<float>(Dot2(r1->mNormal, between));
            if (-0.001f > towards) continue;
            const float back = static_cast<float>(Dot2(r2->mNormal, between));
            if (-0.001f > -back) continue;
            const float distance = static_cast<float>(
                std::sqrt(static_cast<double>(AddS(MulS(between[0], between[0]), MulS(between[1], between[1])))));
            if (!(mPortalReach >= distance)) continue; // a NaN on either side does not shade
            if (0.1f > distance) continue;
            const float alignment = static_cast<float>(Dot2(r1->mNormal, r2->mNormal));
            const float shade = MulS(AddS(MulS(SubS(mPortalShadeAligned, mPortalShadeCrossed), MulS(alignment, alignment)),
                                          mPortalShadeCrossed),
                                     SubS(1.0f, DivS(distance, mPortalReach)));
            r1->mShade = r1->mShade > shade ? SubS(r1->mShade, shade) : 0.0f;
        }
    }
}

} // namespace nTSSG
