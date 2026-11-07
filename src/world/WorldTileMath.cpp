// The tile maths of nTSWorld's helper module (lane RY): src/world/WorldTileMath.h. Built for speed (/O2).
#include "world/WorldTileMath.h"

#include "game/original.h"
#include "misc/X87.h"

#include <cmath>
#include <cstring>

namespace worldmath {

namespace {

constexpr float kHalf = 0.5f;               // 0xd9c694
constexpr float kOne = 1.0f;                // 0xd9c698
constexpr float kTwo = 2.0f;                // 0xd9c69c
constexpr float kRoundBias = 12582912.0f;   // 0xd9ff00: 1.5 * 2^23, the round-to-nearest-even trick
constexpr float kFixedScale = 1.52587890625e-05f; // 0xdc87b8: 2^-16
constexpr float kQuarterPi = 0.7853981852531433f; // 0xdb1dcc
constexpr float kTwoPiFloat = 6.2831854820251465f; // 0xda20f0
constexpr double kTwoPiDouble = 6.2831854820251465; // 0xdbed40
constexpr float kPiFloat = 3.1415927410125732f;
constexpr float kThird = 0.33333334f;

// The world layer's ground height (0x7e4680, __cdecl, the point by pointer, the result in st0): still the original's.
float GroundHeightAt(const nTSWorld::WorldPoint *point)
{
    return OriginalFunction<float(__cdecl *)(const nTSWorld::WorldPoint *), 0x7e4680>()(point);
}

// The compiler's floor to an integer: round (v - 0.5) to nearest with the magic number, read the low bits of the sum, and
// one more when that integer plus one is exactly v.
int32_t FloorToInt(float v)
{
    const float biased = (v - kHalf) + kRoundBias;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    int32_t result = static_cast<int32_t>(bits - 0x4b400000u);
    if (static_cast<float>(result + 1) == v) result += 1;
    return result;
}

// Round to nearest even through the magic number.
int32_t RoundToInt(float v)
{
    const float biased = v + kRoundBias;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    return static_cast<int32_t>(bits - 0x4b400000u);
}

int32_t SignedRemainder8(int32_t value) { return value % 8; }

} // namespace

KindedTile *__thiscall KindedTile::Set(const int32_t *tile3)
{
    kind = 0;
    tile[0] = tile3[0];
    tile[1] = tile3[1];
    tile[2] = tile3[2];
    return this;
}

// ---- tile bytes and fixed values ----

float *__cdecl TileBytesToPosition(float *out, const int8_t *bytes)
{
    PositionAtGround(out, static_cast<float>(bytes[0]), static_cast<float>(bytes[1]), bytes[2]);
    return out;
}

void __cdecl TileBytesToFloats(float *out, const int8_t *bytes)
{
    const int8_t first = bytes[0];
    out[0] = static_cast<float>(bytes[1]);
    const int32_t level = bytes[2] - 1;
    std::memcpy(&out[2], &level, sizeof(level)); // the level's integer bits, not a float
    out[1] = static_cast<float>(first);
}

float *__cdecl FixedToPosition(float *out, const int32_t *fixed, int32_t level)
{
    PositionAtGround(out, static_cast<float>(fixed[1]) * kFixedScale, static_cast<float>(fixed[0]) * kFixedScale, level);
    return out;
}

void __cdecl Fixed16ToFloats(float *out, const int32_t *fixed)
{
    out[0] = static_cast<float>(fixed[0]) * kFixedScale;
    out[1] = static_cast<float>(fixed[1]) * kFixedScale;
}

int8_t *__cdecl TileToBytes(int8_t *out, const int32_t *tile)
{
    const int32_t x = tile[0];
    const int8_t y = static_cast<int8_t>(tile[1]);
    out[2] = static_cast<int8_t>(static_cast<int8_t>(tile[2]) + 1);
    out[0] = y;
    out[1] = static_cast<int8_t>(x);
    return out;
}

int32_t *__cdecl FloatsToFixed16(int32_t *out, const float *in)
{
    out[0] = 0;
    out[1] = 0;
    const float y = in[1];
    out[0] = static_cast<int32_t>(in[0] * 65536.0f);
    out[1] = static_cast<int32_t>(y * 65536.0f);
    return out;
}

void __cdecl PositionAtGround(float *out, float y, float x, int32_t level)
{
    const float height = HeightAtLevel(y, x, level);
    out[0] = x;
    out[1] = y;
    out[2] = height;
}

float __cdecl HeightAtLevel(float y, float x, int32_t level)
{
    nTSWorld::WorldPoint point = {x, y, level - 1};
    return GroundHeightAt(&point);
}

// ---- facings ----

float __cdecl FacingAngle(int32_t facing)
{
    return static_cast<float>(static_cast<int16_t>(-facing)) * kQuarterPi;
}

float *__cdecl FacingQuaternion(float *out, int32_t facing)
{
    const float inverseLength = 1.0f / static_cast<float>(std::sqrt(1.0));
    const float axisXY = inverseLength * 0.0f;
    const float axisZ = inverseLength;
    const double angle = x87::Rounded(static_cast<double>(FacingAngle(facing)) + static_cast<double>(kTwoPiFloat));
    const double reduced = std::fmod(angle, kTwoPiDouble);
    const double half = static_cast<double>(static_cast<float>(reduced) * kHalf);
    const float s = static_cast<float>(std::sin(half));
    out[2] = axisZ * s;
    out[0] = s * axisXY;
    out[1] = s * axisXY;
    out[3] = static_cast<float>(std::cos(half));
    return out;
}

int32_t __cdecl FacingToNotch(int32_t facing) { return SignedRemainder8(8 - static_cast<int16_t>(facing)); }

int32_t __cdecl FacingPtrToNotch(const int32_t *facing) { return SignedRemainder8(8 - *facing); }

int32_t __cdecl AngleToNotch(float angle)
{
    while (angle < -kTwoPiFloat) angle = angle + kTwoPiFloat;
    while (angle >= 0.0f) angle = angle - kTwoPiFloat;
    const float scaled = kHalf - (angle / kQuarterPi - kTwo);
    return SignedRemainder8(static_cast<int16_t>(FloorToInt(scaled)));
}

int16_t __cdecl AngleToFacing(float angle)
{
    while (angle < -kPiFloat) angle = angle + kTwoPiFloat;
    while (angle >= kPiFloat) angle = angle - kTwoPiFloat;
    return static_cast<int16_t>(FloorToInt(kHalf - angle / kQuarterPi));
}

// ---- tiles and positions ----

int32_t *__cdecl NeighbourTile(int32_t *out, const int32_t *tile, uint8_t direction)
{
    if (direction != 0x0f && direction != 0xff) {
        int32_t dx = static_cast<int32_t>(~direction & 1) - 1;
        int32_t dy = (direction >> 1) & 1;
        if (direction & 4) dx = 1;
        if (direction & 8) dy = -1;
        out[0] = tile[0] + dx;
        out[1] = tile[1] + dy;
        out[2] = tile[2];
        return out;
    }
    out[0] = tile[0];
    out[1] = tile[1];
    out[2] = tile[2];
    return out;
}

int32_t *__cdecl CornerTile(int32_t *out, const int32_t *tile, int32_t corner)
{
    int32_t x = tile[0];
    int32_t y = tile[1];
    if (corner != 1) {
        if (corner == 5) {
            out[1] = y;
            out[0] = x + 1;
            out[2] = tile[2];
            return out;
        }
        if (corner == 7) x = x + 1;
        else {
            out[1] = y;
            out[0] = x;
            out[2] = tile[2];
            return out;
        }
    }
    y = y + 1;
    out[1] = y;
    out[0] = x;
    out[2] = tile[2];
    return out;
}

float *__cdecl TileToFloats(float *out, const int32_t *tile)
{
    out[0] = static_cast<float>(tile[0]);
    const int32_t y = tile[1];
    std::memcpy(&out[2], &tile[2], sizeof(float)); // the level's integer bits
    out[1] = static_cast<float>(y);
    return out;
}

float *__cdecl CornerPosition(float *out, const int32_t *tile, int32_t corner)
{
    int32_t corners[3];
    CornerTile(corners, tile, corner);
    TileToFloats(out, corners);
    return out;
}

void __cdecl SnapToQuarters(float *out, const float *in)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    const float y = in[1];
    // The sum is a double: (double)(v * 4) + 0.5, truncated.
    const int32_t qy = static_cast<int32_t>(static_cast<double>(y * 4.0f) + 0.5);
    const int32_t qx = static_cast<int32_t>(static_cast<double>(in[0] * 4.0f) + 0.5);
    out[0] = static_cast<float>(qx) * 0.25f;
    out[1] = static_cast<float>(qy) * 0.25f;
    out[2] = in[2];
}

float *__cdecl RoundPositionDown(float *out, const float *in)
{
    out[0] = -1.0f;
    out[1] = -1.0f;
    out[2] = 0.0f;
    out[0] = static_cast<float>(std::floor(static_cast<double>(in[0] + kHalf)));
    out[1] = static_cast<float>(std::floor(static_cast<double>(in[1] + kHalf)));
    std::memcpy(&out[2], &in[2], sizeof(float));
    return out;
}

int32_t *__cdecl RoundPositionToTile(int32_t *out, const float *in)
{
    out[0] = -1;
    out[1] = -1;
    out[2] = 0;
    out[0] = RoundToInt(in[0]);
    out[1] = RoundToInt(in[1]);
    std::memcpy(&out[2], &in[2], sizeof(float));
    return out;
}

int32_t *__cdecl RoundPositionToTileB(int32_t *out, const float *in)
{
    const float x = in[0];
    out[1] = RoundToInt(in[1]);
    std::memcpy(&out[2], &in[2], sizeof(float));
    out[0] = RoundToInt(x);
    return out;
}

int32_t *__cdecl PositionToTile(int32_t *out, const float *position)
{
    const int32_t x = FloorToInt(position[0]);
    const int32_t y = FloorToInt(position[1]);
    out[1] = y;
    std::memcpy(&out[2], &position[2], sizeof(float));
    out[0] = x;
    return out;
}

TileQuadrant *__cdecl PositionToTileQuadrant(TileQuadrant *out, const float *position)
{
    const float x = position[0];
    const float y = position[1];
    const int32_t tx = FloorToInt(x);
    const int32_t ty = FloorToInt(y);
    const int32_t level = reinterpret_cast<const int32_t *>(position)[2];
    out->x = tx;
    out->y = ty;
    out->level = level;
    out->quadrant = QuadrantOfPoint(x, y);
    return out;
}

uint8_t __cdecl QuadrantOfPoint(float x, float y)
{
    const float fx = x - static_cast<float>(std::floor(static_cast<double>(x)));
    const float complement = kOne - fx;
    const float fy = y - static_cast<float>(std::floor(static_cast<double>(y)));
    if (fx > fy) return static_cast<uint8_t>(((complement > fy) ? 1 : 0) * 4 + 4);
    return static_cast<uint8_t>((!(complement > fy) ? 1 : 0) + 1);
}

uint8_t __cdecl QuadrantOfPositionXY(const float *position) { return QuadrantOfPoint(position[0], position[1]); }

void __cdecl QuadrantOffset(float *out, uint8_t quadrant)
{
    float y = 0.0f;
    float x = 0.0f;
    out[0] = 0.0f;
    out[1] = 0.0f;
    if (quadrant & 1) {
        x = -kThird;
        out[0] = -kThird;
    }
    if (quadrant & 2) {
        out[1] = kThird;
        y = kThird;
    }
    if (quadrant & 4) out[0] = x + kThird;
    if (quadrant & 8) out[1] = y - kThird;
}

float *__cdecl QuadrantCentre(float *out, const TileQuadrant *tile)
{
    float offset[2];
    const float x = static_cast<float>(tile->x) + kHalf;
    const float y = static_cast<float>(tile->y) + kHalf;
    QuadrantOffset(offset, tile->quadrant);
    const int32_t level = tile->level;
    out[0] = offset[0] + x;
    out[2] = *reinterpret_cast<const float *>(&level);
    out[1] = offset[1] + y;
    return out;
}

float *__cdecl QuadrantCentreOnGround(float *out, const TileQuadrant *tile)
{
    float centre[3];
    QuadrantCentre(centre, tile);
    WithGroundHeight(out, centre);
    return out;
}

float *__cdecl TileCentreOnGround(float *out, const int32_t *tile)
{
    float centre[3];
    centre[0] = static_cast<float>(tile[0]) + kHalf;
    centre[1] = static_cast<float>(tile[1]) + kHalf;
    std::memcpy(&centre[2], &tile[2], sizeof(float));
    WithGroundHeight(out, centre);
    return out;
}

float *__cdecl PositionToTileCentre(float *out, const float *position)
{
    int32_t tile[3];
    PositionToTile(tile, position);
    out[0] = static_cast<float>(tile[0]) + kHalf;
    out[1] = static_cast<float>(tile[1]) + kHalf;
    std::memcpy(&out[2], &tile[2], sizeof(float));
    return out;
}

float *__cdecl WithGroundHeight(float *out, const float *in)
{
    const float height = GroundHeightAt(reinterpret_cast<const nTSWorld::WorldPoint *>(in));
    out[0] = in[0];
    out[1] = in[1];
    out[2] = height;
    return out;
}

int32_t *__cdecl WithTileValue(int32_t *out, const int32_t *in)
{
    const int32_t value = OriginalFunction<int32_t(__cdecl *)(const int32_t *), 0x7e2cc0>()(in);
    out[0] = in[0];
    out[1] = in[1];
    out[2] = value;
    return out;
}

// ---- quadrant bytes ----

bool __cdecl IsCornerQuadrant(int8_t quadrant)
{
    switch (quadrant) {
    case 3:
    case 6:
    case 9:
    case 12: return true;
    default: return false;
    }
}

bool __cdecl IsSideQuadrant(int8_t quadrant)
{
    switch (quadrant) {
    case 1:
    case 2:
    case 4:
    case 8: return true;
    default: return false;
    }
}

uint8_t __cdecl OppositeQuadrant(int8_t quadrant)
{
    switch (quadrant) {
    case 1: return 4;
    case 2: return 8;
    case 3: return 12;
    case 4: return 1;
    case 6: return 9;
    case 8: return 2;
    case 9: return 6;
    case 12: return 3;
    case 15: return 15;
    default: return 0xff;
    }
}

uint8_t __cdecl CompassToQuadrant(const uint32_t *compass)
{
    switch (*compass) {
    case 0: return 2;
    case 2: return 1;
    case 4: return 8;
    case 6: return 4;
    default: return 0xff;
    }
}

// ---- one more tile ----

TileQuadrant *__cdecl StepQuadrantTile(TileQuadrant *out, const TileQuadrant *tile, uint32_t step)
{
    int8_t q = static_cast<int8_t>(tile->quadrant);
    const int32_t at[3] = {tile->x, tile->y, tile->level};
    if (IsSideQuadrant(q)) {
        if (step < 2) {
            switch (q) {
            case 1: q = (step == 0) ? 2 : 8; break;
            case 2: q = (step == 0) ? 4 : 1; break;
            case 4: q = (step == 0) ? 8 : 2; break;
            case 8: q = (step == 0) ? 1 : 4; break;
            default: break;
            }
            out->y = at[1];
            out->x = at[0];
            out->level = at[2];
            out->quadrant = static_cast<uint8_t>(q);
            return out;
        }
    } else if (IsCornerQuadrant(q)) {
        const uint8_t even = ~step & 1;
        switch (q) {
        case 3: q = static_cast<int8_t>((even ^ 1) + 1); break;
        case 6: q = static_cast<int8_t>(((even ^ 1) + 1) * 2); break;
        case 9: q = even ? 8 : 1; break;
        case 12: q = static_cast<int8_t>(((even ^ 1) + 1) * 4); break;
        default: break;
        }
        const uint8_t opposite = OppositeQuadrant(q);
        if (step < 2) {
            int32_t moved[3];
            NeighbourTile(moved, at, static_cast<uint8_t>(q));
            out->x = moved[0];
            out->y = moved[1];
            out->level = moved[2];
            out->quadrant = opposite;
            return out;
        }
        out->y = at[1];
        out->x = at[0];
        out->level = at[2];
        out->quadrant = opposite;
        return out;
    } else {
        q = static_cast<int8_t>(1 << (step & 0x1f));
    }
    int32_t moved[3];
    NeighbourTile(moved, at, static_cast<uint8_t>(q));
    out->x = moved[0];
    out->y = moved[1];
    out->level = moved[2];
    out->quadrant = OppositeQuadrant(q);
    return out;
}

} // namespace worldmath
