// The tile, quadrant and point iterators of nTSWorld's helper module (lane RZ). See WorldIterators.h.
#include "world/WorldIterators.h"

#include "world/WorldService.h"

#include <cstddef>
#include <cstring>

namespace worlditer {

static_assert(sizeof(TileDiamondIterator) == 0x2c && offsetof(TileDiamondIterator, x) == 0x20);
static_assert(sizeof(InnerDiamondIterator) == 0x2c && offsetof(InnerDiamondIterator, level) == 0x28);
static_assert(offsetof(QuadDiamondIterator, mask) == 0x20 && offsetof(QuadDiamondIterator, x) == 0x24 &&
              offsetof(QuadDiamondIterator, bit) == 0x30);
static_assert(offsetof(TileBoxIterator, cur) == 0x18 && offsetof(TileBoxIterator, extra) == 0x24);
static_assert(sizeof(TileTriangle) == 0x28);
static_assert(sizeof(RingPointIterator) == 0x10 && sizeof(TileCornerIterator) == 0x10 && sizeof(TileEdgeIterator) == 0x10);

namespace {

// round(v) for |v| < 2^22 through the float's mantissa: v + 1.5 * 2^23 lands on a whole number, read from the bits.
int32_t RoundHalfSum(int32_t sum)
{
    const float f = float(sum) * 0.5f + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return int32_t(bits - 0x4b400000u);
}

int32_t Wrap(uint32_t v) { return int32_t(v); }

nTSWorld::cIWorldDB *WorldDbOr(nTSWorld::cIWorldDB *db) { return db != nullptr ? db : worldsvc::CachedWorldDB(); }

// The corner, edge-neighbour and ring tables (game data 0xe0392c..., 0xe038c8..., 0xe038ec...).
constexpr int32_t kCornerDx[4] = {0, 1, 1, 0};
constexpr int32_t kCornerDy[4] = {0, 0, 1, 1};
constexpr int32_t kEdgeDx[4] = {-1, 0, 0, -1};
constexpr int32_t kEdgeDy[4] = {-1, -1, 0, 0};
constexpr uint8_t kEdgeQuadrants[4] = {6, 3, 9, 12};
constexpr float kRingDx[8] = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -0.5f};
constexpr float kRingDy[8] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, -0.5f};

// The low and high bounds after a column, moving only when x is not on its turning column.
void TurnBounds(int32_t x, int32_t lowTurnX, int32_t highTurnX, int32_t &low, int32_t &high)
{
    if (x < lowTurnX) --low;
    else if (x > lowTurnX) ++low;
    if (x < highTurnX) ++high;
    else if (x > highTurnX) --high;
}

} // namespace

void __cdecl RotateCornersInt(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t *a, int32_t *b, int32_t *c,
                              int32_t *d)
{
    const uint32_t ux1 = uint32_t(x1), uy1 = uint32_t(y1), ux2 = uint32_t(x2), uy2 = uint32_t(y2);
    *a = RoundHalfSum(Wrap(ux1 - uy2 + uy1 + ux2));
    *b = RoundHalfSum(Wrap(ux1 - ux2 + uy1 + uy2));
    *c = RoundHalfSum(Wrap(ux1 - uy1 + ux2 + uy2));
    *d = RoundHalfSum(Wrap(uy1 - ux1 + ux2 + uy2));
}

void __cdecl RotateCornersFloat(float x1, float y1, float x2, float y2, float *a, float *b, float *c, float *d)
{
    const float sum = x1 + y1;
    *a = ((sum + x2) - y2) * 0.5f;
    *b = ((sum - x2) + y2) * 0.5f;
    *c = (((x1 - y1) + x2) + y2) * 0.5f;
    *d = (((y1 - x1) + x2) + y2) * 0.5f;
}

void __cdecl DiamondBounds(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t *startX, int32_t *startY,
                           int32_t *lowTurnX, int32_t *highTurnX)
{
    int32_t a, b, c, d;
    RotateCornersInt(x1, y1, x2, y2, &a, &b, &c, &d);
    if (Wrap(uint32_t(x2) - uint32_t(x1)) < Wrap(uint32_t(y2) - uint32_t(y1))) {
        *startX = a;
        *startY = b;
        *lowTurnX = x1;
        *highTurnX = x2;
    } else {
        *startX = x1;
        *startY = y1;
        *lowTurnX = a;
        *highTurnX = c;
    }
}

// ---- TileDiamondIterator ----

TileDiamondIterator *__thiscall TileDiamondIterator::Construct(const TileCoord *a, const TileCoord *b)
{
    firstLevel = a->level;
    lastLevel = b->level;
    x = -1;
    y = -1;
    level = 0;
    DiamondBounds(a->x, a->y, b->x, b->y, &startX, &startY, &lowTurnX, &highTurnX);
    Start();
    return this;
}

TileDiamondIterator *__thiscall TileDiamondIterator::Next()
{
    if (y < high) {
        ++y;
        return this;
    }
    ++x;
    low += x > lowTurnX ? 1 : -1;
    high = x > highTurnX ? high - 1 : high + 1;
    if (low <= high) {
        y = low;
        return this;
    }
    ++level;
    StartLevel();
    return this;
}

bool __thiscall TileDiamondIterator::Done() const { return level > lastLevel; }

TileCoord *__thiscall TileDiamondIterator::Get(TileCoord *out) const
{
    out->x = x;
    out->y = y;
    out->level = level;
    return out;
}

void __thiscall TileDiamondIterator::Start()
{
    StartLevel();
    level = firstLevel;
}

void __thiscall TileDiamondIterator::StartLevel()
{
    x = startX;
    low = startY;
    high = startY;
    y = startY;
}

// ---- InnerDiamondIterator ----

InnerDiamondIterator *__thiscall InnerDiamondIterator::Construct(const TileCoord *a, const TileCoord *b)
{
    firstLevel = a->level;
    lastLevel = b->level;
    x = -1;
    y = -1;
    level = 0;
    DiamondBounds(a->x, a->y, b->x, b->y, &startX, &startY, &lowTurnX, &highTurnX);
    Start();
    return this;
}

void __thiscall InnerDiamondIterator::Start()
{
    level = firstLevel;
    StartLevel();
}

InnerDiamondIterator *__thiscall InnerDiamondIterator::Next()
{
    ++y;
    if (y <= high) return this;
    ++x;
    TurnBounds(x, lowTurnX, highTurnX, low, high);
    if (low <= high) {
        y = low;
        return this;
    }
    ++level;
    StartLevel();
    return this;
}

void __thiscall InnerDiamondIterator::StartLevel()
{
    x = startX + 1;
    const bool lowTurn = x == lowTurnX, highTurn = x == highTurnX;
    if (lowTurn && highTurn) {
        level = lastLevel + 1;
        return;
    }
    const int32_t bottom = lowTurn ? startY : startY - 1;
    low = bottom;
    high = highTurn ? startY - 1 : startY;
    y = bottom;
}

// ---- QuadDiamondIterator ----

QuadDiamondIterator *__thiscall QuadDiamondIterator::Construct(const TileCoord *a, const TileCoord *b)
{
    firstLevel = a->level;
    lastLevel = b->level;
    x = -1;
    y = -1;
    level = 0;
    bit = -1;
    DiamondBounds(a->x, a->y, b->x, b->y, &startX, &startY, &lowTurnX, &highTurnX);
    Start();
    return this;
}

bool __thiscall QuadDiamondIterator::Done() const { return level > lastLevel; }

TileCoordQuadrant *__thiscall QuadDiamondIterator::Get(TileCoordQuadrant *out) const
{
    out->x = x;
    out->y = y;
    out->level = level;
    out->quadrant = uint8_t(bit);
    return out;
}

QuadDiamondIterator *__thiscall QuadDiamondIterator::Next()
{
    for (bit = int8_t(bit + bit); bit <= 8; bit = int8_t(bit + bit)) {
        if ((uint8_t(bit) & mask) != 0) return this;
    }
    ++y;
    if (y < high) {
        mask = 0xf;
        bit = 1;
        return this;
    }
    if (y == high) {
        const bool pastTurn = x >= highTurnX;
        mask = pastTurn ? 9 : 12;
        bit = pastTurn ? 1 : 4;
        return this;
    }
    ++x;
    TurnBounds(x, lowTurnX, highTurnX, low, high);
    if (low > high) {
        ++level;
        StartColumn();
        return this;
    }
    StartRow();
    return this;
}

void __thiscall QuadDiamondIterator::Start()
{
    StartColumn();
    level = firstLevel;
}

void __thiscall QuadDiamondIterator::StartColumn()
{
    x = startX;
    low = startY;
    high = startY;
    StartRow();
}

void __thiscall QuadDiamondIterator::StartRow()
{
    y = low - 1;
    const bool beforeTurn = x < lowTurnX;
    mask = beforeTurn ? 6 : 3;
    bit = beforeTurn ? 2 : 1;
}

// ---- TileBoxIterator ----

TileBoxIterator *__thiscall TileBoxIterator::Construct(const TileCoord *lo, const TileCoord *hi)
{
    min = *lo;
    max = *hi;
    cur = *lo;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructQuadrants(const TileCoord *lo, const TileCoord *hi)
{
    min = *lo;
    max = *hi;
    cur = min;
    reinterpret_cast<uint8_t *>(&extra)[0] = 1;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructBits(const TileCoord *lo, const TileCoord *hi)
{
    min = *lo;
    max = *hi;
    cur = *lo;
    extra = 1;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructLevel(int32_t level, nTSWorld::cIWorldDB *db)
{
    nTSWorld::cIWorldDB *const world = WorldDbOr(db);
    min.x = world->GetMinX();
    min.y = world->GetMinY();
    min.level = level;
    max.x = world->GetMaxX();
    max.y = world->GetMaxY();
    max.level = level;
    cur = min;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructLevelQuadrants(int32_t level, nTSWorld::cIWorldDB *db)
{
    ConstructLevel(level, db);
    reinterpret_cast<uint8_t *>(&extra)[0] = 1;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructLevelCorners(int32_t level, nTSWorld::cIWorldDB *db)
{
    nTSWorld::cIWorldDB *const world = WorldDbOr(db);
    min.x = world->GetMinX();
    min.y = world->GetMinY();
    min.level = level;
    max.x = world->GetMaxX() + 1;
    max.y = world->GetMaxY() + 1;
    max.level = level;
    cur = min;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructAll(nTSWorld::cIWorldDB *db)
{
    nTSWorld::cIWorldDB *const world = WorldDbOr(db);
    min.x = world->GetMinX();
    min.y = world->GetMinY();
    min.level = world->GetMinLevel();
    max.x = world->GetMaxX();
    max.y = world->GetMaxY();
    max.level = world->GetMaxLevel();
    cur = min;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::ConstructAllQuadrants(nTSWorld::cIWorldDB *db)
{
    ConstructAll(db);
    reinterpret_cast<uint8_t *>(&extra)[0] = 1;
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::Next()
{
    if (++cur.y > max.y) {
        cur.y = min.y;
        if (++cur.x > max.x) {
            ++cur.level;
            cur.x = min.x;
        }
    }
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::NextQuadrant()
{
    uint8_t &quadrant = reinterpret_cast<uint8_t *>(&extra)[0];
    quadrant = uint8_t(quadrant + quadrant);
    if (int8_t(quadrant) > 8) {
        quadrant = 1;
        Next();
    }
    return this;
}

TileBoxIterator *__thiscall TileBoxIterator::NextBit()
{
    extra <<= 1;
    if (extra > 0x20) {
        extra = 1;
        Next();
    }
    return this;
}

bool __thiscall TileBoxIterator::Done() const { return cur.level > max.level; }

void __thiscall TileBoxIterator::Reset() { cur = min; }

TileCoord *__thiscall TileBoxIterator::Get(TileCoord *out) const
{
    *out = cur;
    return out;
}

TileCoordQuadrant *__thiscall TileBoxIterator::GetQuadrant(TileCoordQuadrant *out) const
{
    out->x = cur.x;
    out->y = cur.y;
    out->level = cur.level;
    out->quadrant = reinterpret_cast<const uint8_t *>(&extra)[0];
    return out;
}

TileCoordBit *__thiscall TileBoxIterator::GetBit(TileCoordBit *out) const
{
    out->x = cur.x;
    out->y = cur.y;
    out->level = cur.level;
    out->bit = extra;
    return out;
}

// ---- TileTriangle ----

TileTriangle *__thiscall TileTriangle::Construct(const TileCoordQuadrant *tile)
{
    index = 0;
    const float fx = float(tile->x), fy = float(tile->y);
    const uint8_t quadrant = tile->quadrant;
    if (quadrant & 1) {
        point[0].x = fx;
        point[0].y = fy + 1.0f;
        point[1].x = fx;
        point[1].y = fy;
    } else if (quadrant & 2) {
        point[0].x = fx + 1.0f;
        point[0].y = fy + 1.0f;
        point[1].x = fx;
        point[1].y = fy + 1.0f;
    } else if (quadrant & 4) {
        point[0].x = fx + 1.0f;
        point[0].y = fy;
        point[1].x = fx + 1.0f;
        point[1].y = fy + 1.0f;
    } else {
        point[0].x = fx;
        point[0].y = fy;
        point[1].x = fx + 1.0f;
        point[1].y = fy;
    }
    point[2].x = fx + 0.5f;
    point[2].y = fy + 0.5f;
    point[0].level = tile->level;
    point[1].level = tile->level;
    point[2].level = tile->level;
    return this;
}

void __thiscall TileTriangle::Destroy() {}

PointCoord *__thiscall TileTriangle::Edge(PointCoord *out) const
{
    out[0] = point[index];
    out[1] = point[(index + 1) % 3];
    return out;
}

bool __thiscall TileTriangle::Done() const { return index > 2; }

// ---- RingPointIterator ----

RingPointIterator *__thiscall RingPointIterator::Construct(const TileCoord *tile)
{
    index = 0;
    centre.x = float(tile->x);
    centre.y = float(tile->y);
    centre.level = tile->level;
    return this;
}

PointCoord *__thiscall RingPointIterator::Get(PointCoord *out) const
{
    out->x = kRingDx[index] + centre.x;
    out->y = kRingDy[index] + centre.y;
    out->level = centre.level;
    return out;
}

bool __thiscall RingPointIterator::Done() const { return index == 8; }

// ---- TileCornerIterator / TileEdgeIterator ----

TileCoord *__thiscall TileCornerIterator::Get(TileCoord *out) const
{
    out->x = kCornerDx[index] + tile.x;
    out->y = kCornerDy[index] + tile.y;
    out->level = tile.level;
    return out;
}

bool __thiscall TileCornerIterator::Done() const { return index == 4; }

TileCoordQuadrant *__thiscall TileEdgeIterator::Get(TileCoordQuadrant *out) const
{
    out->level = tile.level;
    out->x = kEdgeDx[index] + tile.x;
    out->y = kEdgeDy[index] + tile.y;
    out->quadrant = kEdgeQuadrants[index];
    return out;
}

// ---- QuadrantBitIterator ----

QuadrantBitIterator *__thiscall QuadrantBitIterator::Next()
{
    bit <<= 1;
    return this;
}

bool __thiscall QuadrantBitIterator::Done() const { return bit > 8; }

} // namespace worlditer
