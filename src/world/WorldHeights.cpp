// The heights of nTSWorld's helper module (lane RZ): src/world/WorldHeights.h. Built for speed (/O2).
#include "world/WorldHeights.h"

#include "game/original.h"
#include "misc/X87.h"
#include "world/WorldTileMath.h"
#include "world/WorldService.h"

#include <cmath>
#include <cstring>

namespace worldheights {

namespace {

constexpr float kHalf = 0.5f;             // 0xd9c694
constexpr float kOne = 1.0f;              // 0xd9c698
constexpr float kRoundBias = 12582912.0f; // 0xd9ff00: 1.5 * 2^23

using nTSWorld::WorldPoint;
using nTSWorld::WorldTile;

// The compiler's floor to an integer (as worldmath's): round (v - 0.5) through the magic number, one more when that
// integer plus one is exactly v.
int32_t FloorToInt(float v)
{
    const float biased = (v - kHalf) + kRoundBias;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    int32_t result = static_cast<int32_t>(bits - 0x4b400000u);
    if (static_cast<float>(result + 1) == v) result += 1;
    return result;
}

// A float's bits as a sign-magnitude integer, so that the difference of two counts the floats between them.
int32_t Ordered(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    const uint32_t sign = bits >> 31;
    return static_cast<int32_t>((((0u - sign) >> 1) ^ bits) + sign);
}

// The original's closeness test: a and b at most 31 floats apart (the difference's bits above the low five all equal
// its sign).
bool Near(float a, float b)
{
    const int32_t distance = static_cast<int32_t>(static_cast<uint32_t>(Ordered(a)) - static_cast<uint32_t>(Ordered(b)));
    return ((distance >> 26 ^ distance) & ~31) == 0;
}

float Elevation(nTSWorld::cIWorldDB *db, int32_t x, int32_t y, int32_t level)
{
    WorldTile tile{};
    tile.x = x;
    tile.y = y;
    tile.level = level;
    return db->GetElevation(&tile);
}

// h0 at t = 0, h1 at t = 1, on the x87: h1 * t + (1 - t) * h0.
double Lerp87(float h0, float h1, float t)
{
    const double far = x87::Rounded(static_cast<double>(h1) * t);
    const double near = x87::Rounded(x87::Rounded(1.0 - static_cast<double>(t)) * h0);
    return x87::Rounded(far + near);
}

} // namespace

double __cdecl GroundHeightAt(const WorldPoint *point)
{
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    const int32_t level = point->level;
    if (level < db->GetMinLevel()) {
        if (!db->IsInWorld(point->x, point->y)) return 0.0;
        const WorldPoint up{point->x, point->y, point->level + 1};
        const float above = static_cast<float>(GroundHeightAt(&up));
        const float wall = db->GetWallHeight();
        return x87::Rounded(static_cast<double>(above) - wall);
    }
    if (level > db->GetMaxLevel()) {
        if (!db->IsInWorld(point->x, point->y)) return 0.0;
        const float wall = db->GetWallHeight();
        const WorldPoint down{point->x, point->y, point->level - 1};
        return x87::Rounded(GroundHeightAt(&down) + wall);
    }

    const int32_t tx = FloorToInt(point->x);
    const int32_t ty = FloorToInt(point->y);
    WorldTile tile{};
    tile.x = tx;
    tile.y = ty;
    tile.level = level;
    if (!db->IsInside(&tile)) {
        // Clamp onto the corner grid: x to [GetMinX, GetMaxX + 1], y to [GetMinY, GetMaxY + 1] (in the original's call order).
        int32_t highX = db->GetMaxX() + 1;
        int32_t x = tx;
        const int32_t lowX = db->GetMinX();
        if (x < lowX) x = lowX;
        if (x <= highX) highX = x;
        int32_t highY = db->GetMaxY() + 1;
        int32_t y = ty;
        const int32_t lowY = db->GetMinY();
        if (y < lowY) y = lowY;
        if (y <= highY) highY = y;
        WorldTile corner{};
        corner.x = highX;
        corner.y = highY;
        corner.level = level;
        if (!db->IsInsideCorners(&corner)) return 0.0f;
        return db->GetElevation(&corner);
    }

    const float x = point->x;
    const float y = point->y;
    const float floorX = static_cast<float>(std::floor(static_cast<double>(x)));
    const float floorY = static_cast<float>(std::floor(static_cast<double>(y)));
    const bool onX = Near(x, floorX);
    const bool onY = Near(y, floorY);
    if (onX) {
        if (onY) return Elevation(db, tx, ty, level);
        const float t = y - floorY;
        const float h0 = Elevation(db, tx, ty, level);
        const float h1 = Elevation(db, tx, ty + 1, level);
        return Lerp87(h0, h1, t);
    }
    if (onY) {
        const float t = x - floorX;
        const float h0 = Elevation(db, tx, ty, level);
        const float h1 = Elevation(db, tx + 1, ty, level);
        return Lerp87(h0, h1, t);
    }
    const float u = x - floorX;
    const float v = y - floorY;
    const float a = Elevation(db, tx, ty, level);
    const float b = Elevation(db, tx + 1, ty, level);
    const float c = Elevation(db, tx + 1, ty + 1, level);
    const float d = Elevation(db, tx, ty + 1, level);
    const float rv = kOne - v;
    const float nearEdge = d * v + a * rv;
    const float farEdge = b * rv + c * v;
    return nearEdge * (kOne - u) + farEdge * u;
}

namespace {
// 0x7e28d0 (WU-2's, an original here): the level whose ground is the highest at or under the point's z.
int32_t LevelOfGround(const float *in)
{
    return OriginalFunction<int32_t(__cdecl *)(const float *), 0x7e28d0>()(in);
}
} // namespace

int32_t *__cdecl WithLevelAboveGround(int32_t *out, const float *in)
{
    const int32_t level = LevelOfGround(in);
    std::memcpy(&out[0], &in[0], sizeof(int32_t));
    std::memcpy(&out[1], &in[1], sizeof(int32_t));
    out[2] = level + 1;
    return out;
}

int32_t *__cdecl WithLevelOfGround(int32_t *out, const float *in)
{
    const int32_t level = LevelOfGround(in);
    std::memcpy(&out[0], &in[0], sizeof(int32_t));
    std::memcpy(&out[1], &in[1], sizeof(int32_t));
    out[2] = level;
    return out;
}

bool __cdecl HasFloorOrGroundAt(const float *position, bool checkVisible)
{
    worldmath::TileQuadrant tile;
    worldmath::PositionToTileQuadrant(&tile, position);
    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    if (!db) return false;
    const auto *worldTile = reinterpret_cast<const WorldTile *>(&tile);
    if (!db->IsInside(worldTile)) return false;
    return db->HasFloorOrGround(worldTile, static_cast<int8_t>(tile.quadrant), checkVisible);
}

} // namespace worldheights
