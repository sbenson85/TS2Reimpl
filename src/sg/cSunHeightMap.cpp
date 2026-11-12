// nTSSG::cSunHeightMap (lane 261108i, scenegraph LG5): the sun height map's constructor, sizing, the grid builders the
// lighting manager's light-map pass (0x2e4300) runs over the world DB, and the shadow sweeps. See the header.
#include "sg/cSunHeightMap.h"

#include "compgeom/ConvexMath.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/X87.h"
#include "rz/GameAllocator.h"
#include "simantics/ServiceRefVector.h"
#include "world/WorldCatalog.h"
#include "world/WorldHeights.h"
#include "world/WorldIterators.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <algorithm>
#include <new>

namespace {

// The original's SSE operations with their operand order (docs/lane/test.md "NaN operand order").
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::FloatOf;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZCompGeom::convex::X87Pick;

using nTSWorld::cIWorldDB;
using nTSWorld::WorldPoint;
using nTSWorld::WorldTile;

constexpr float kBias = 12582912.0f; // 1.5 * 2^23: adding it leaves round-to-nearest-even in the low mantissa bits
constexpr uint32_t kBiasBits = 0x4b400000u;
constexpr float kFloatMax = 3.40282347e38f; // the original's FLT_MAX (0xda7660)

float Abs(float f) { return FloatOf(BitsOf(f) & 0x7fffffffu); } // andps with 0x7fffffff

// fadd dword [mem] on a register at the thread's precision: of two NaNs the larger significand wins. Every mem
// operand here is a constant or a float a call returned in st0, so it is never a signalling NaN.
double AddMem87(double reg, float mem)
{
    if (reg != reg && mem != mem) return x87::Rounded(X87Pick(reg, static_cast<double>(mem)));
    return x87::Rounded(reg + static_cast<double>(mem));
}

// The original's inlined roundings, in 32-bit wrapping arithmetic as there. Round: to nearest even.
int32_t Round(float v) { return static_cast<int32_t>(BitsOf(AddS(v, kBias)) - kBiasBits); }
// Floor: (v - 0.5) rounded, one more when that plus one equals v.
int32_t Floor(float v)
{
    const uint32_t r = BitsOf(AddS(SubS(v, 0.5f), kBias)) - kBiasBits, next = r + 1;
    return static_cast<float>(static_cast<int32_t>(next)) == v ? static_cast<int32_t>(next) : static_cast<int32_t>(r);
}
// Ceil: (v + 0.5) rounded, one less when that minus one equals v.
int32_t Ceil(float v)
{
    const uint32_t r = BitsOf(AddS(AddS(v, 0.5f), kBias)) - kBiasBits, prev = r - 1;
    return static_cast<float>(static_cast<int32_t>(prev)) == v ? static_cast<int32_t>(prev) : static_cast<int32_t>(r);
}

float MinSS(float a, float b) { return a < b ? a : b; }
float MaxSS(float a, float b) { return a > b ? a : b; }

// The cell size for n cells per tile: the tile size divided by (float)n on the x87.
float CellSize(cIWorldDB *db, int32_t n)
{
    const float tile = db->GetTileSize();
    return static_cast<float>(x87::Rounded(static_cast<double>(tile) / static_cast<double>(static_cast<float>(n))));
}

double ElevationAt(cIWorldDB *db, int32_t x, int32_t y, int32_t level)
{
    WorldTile tile{};
    tile.x = x;
    tile.y = y;
    tile.level = level;
    return static_cast<double>(db->GetElevation(&tile));
}

// Roof kinds (the record's +0x20): not rendered (15), and the diagonal kinds 10-21 by the original's table. Not ours yet.
bool RoofKindSkipped(int32_t kind) { return OriginalFunction<bool(__cdecl *)(int32_t), 0x7e1d90>()(kind); }
bool RoofKindDiagonal(int32_t kind) { return OriginalFunction<bool(__cdecl *)(int32_t), 0x7e17e0>()(kind); }
// A diagonal roof's footprint as the four turned corners. Not ours yet.
void RoofDiamond(const float *min, const float *max, float *o0, float *o1, float *o2, float *o3)
{
    OriginalFunction<void(__cdecl *)(const float *, const float *, float *, float *, float *, float *), 0x7df5a0>()(
        min, max, o0, o1, o2, o3);
}
// Whether a wall raises its top by 0.75 (its tiles' sides, through the cached world DB's slots 71 and 58). Not ours yet.
bool WallRaised(const void *wall) { return OriginalFunction<bool(__cdecl *)(const void *), 0x7e2050>()(wall); }

// The roofs object's ids (slot 28 fills it; allocator-service storage).
struct IdList : ServiceBlockVector {
    IdList() { mFirst = mLast = mEnd = nullptr; }
    ~IdList() { FreeBlock(); }
    uint32_t Count() const
    {
        return static_cast<uint32_t>(static_cast<const uint32_t *>(mLast) - static_cast<const uint32_t *>(mFirst));
    }
    uint32_t At(uint32_t i) const { return static_cast<const uint32_t *>(mFirst)[i]; }
};

// A roof's height at a position (slot 30, on the x87 stack).
float RoofHeight(void *roofs, uint32_t id, float x, float y, int32_t level)
{
    // receiver: the world DB's roofs object (slot 36)
    return static_cast<float>(RawSlot<30, double>(roofs, id, x, y, level, 1));
}

// A wall record (partitions slot 56): its two ends, each x, y and level.
struct WallRecord {
    float x0, y0;
    int32_t level0;
    float x1, y1;
    int32_t level1;
};

} // namespace

namespace nTSSG {

cSunHeightMap::cSunHeightMap()
    : mWidth(0), mHeight(0), mCell(0.0f), mScale(0.0f), mFill(-1000.0f), mFlat(0), mBlendScale(1.0f),
      mField50(0.7f), mField54(50.0f)
{
}

cSunHeightMap *cSunHeightMap::Construct() { return ::new (static_cast<void *>(this)) cSunHeightMap; }

void cSunHeightMap::ResetFlat()
{
    std::fill(mHeights.begin(), mHeights.end(), mFill);
    mFlat = 1;
}

void cSunHeightMap::Resize(int32_t width, int32_t height, float cell, float fill)
{
    const float scale = DivS(1.0f, cell);
    mWidth = width;
    mHeight = height;
    mCell = cell;
    mScale = scale;
    mFill = fill;
    mGround.clear();
    mValues.clear();
    mHeights.clear();
    const size_t count = static_cast<size_t>(static_cast<uint32_t>(width * height));
    mGround.resize(count, mFill);
    mValues.resize(count, mFill);
    mHeights.resize(count, mFill);
}

void cSunHeightMap::BuildShadow(const float *dir)
{
    if (!(0.0f >= dir[2])) {
        const float ax = Abs(dir[0]), ay = Abs(dir[1]);
        if (!(0.001 > static_cast<double>(ax)) || !(0.001 > static_cast<double>(ay))) {
            if (ay > ax)
                SweepY(dir);
            else
                SweepX(dir);
            mFlat = 0;
            return;
        }
    }
    if (!mFlat) ResetFlat();
}

void cSunHeightMap::SweepX(const float *dir)
{
    int32_t rect[4] = {-1, -1, 0, 0};
    const int32_t w = mWidth, h = mHeight;
    mRun.resize(static_cast<size_t>(static_cast<uint32_t>(h)));
    std::fill(mRun.begin(), mRun.end(), mFill);
    float shear = DivS(Negated(dir[1]), dir[0]);
    const float drop = DivS(MulS(mCell, dir[2]), dir[0]);
    int32_t x, end, dx;
    float step;
    if (0.0f > drop) { // the sun on the high-x side: sweep from x = 0 up
        x = 0;
        end = w;
        step = drop;
    } else {
        x = w - 1;
        end = -1;
        step = Negated(drop);
        shear = Negated(shear);
    }
    dx = 0.0f > drop ? 1 : -1;
    if (x != end) {
        float *run = mRun.data();
        const uint32_t rows = static_cast<uint32_t>(h);
        do {
            const float *values = mValues.data();
            float *heights = mHeights.data();
            int32_t first = -1, last = 0;
            for (uint32_t i = 0; i < rows; ++i) {
                const uint32_t cell = i * static_cast<uint32_t>(w) + static_cast<uint32_t>(x);
                if (values[cell] > run[i]) run[i] = values[cell];
                if (!(heights[cell] == run[i])) {
                    heights[cell] = run[i];
                    last = static_cast<int32_t>(i);
                    if (first < 0) first = static_cast<int32_t>(i);
                }
                run[i] = AddS(step, run[i]);
            }
            if (first >= 0) {
                rect[2] = x;
                if (rect[0] < 0) {
                    rect[0] = x;
                    rect[1] = first;
                    rect[3] = last;
                } else {
                    rect[1] = std::min(first, rect[1]);
                    rect[3] = std::max(last, rect[3]);
                }
            }
            if (shear > 0.0f) {
                // ORIGINAL-BUG: with no rows (h == 0) the count rows - 1 is unsigned, so the loop runs off the empty
                // buffer (reproduced; the test leaves h == 0 with a positive shear out).
                for (uint32_t i = 0; i < rows - 1; ++i) run[i] = AddS(MulS(SubS(run[i + 1], run[i]), shear), run[i]);
            } else {
                for (int32_t i = h - 1; i > 0; --i) run[i] = SubS(run[i], MulS(SubS(run[i - 1], run[i]), shear));
            }
            x += dx;
        } while (x != end);
    }
    for (int k = 0; k < 4; ++k) mChanged[k] = rect[k];
}

void cSunHeightMap::SweepY(const float *dir)
{
    int32_t rect[4] = {-1, -1, 0, 0};
    const int32_t w = mWidth, h = mHeight;
    mRun.resize(static_cast<size_t>(static_cast<uint32_t>(w)));
    std::fill(mRun.begin(), mRun.end(), mFill);
    float shear = DivS(Negated(dir[0]), dir[1]);
    const float drop = DivS(MulS(mCell, dir[2]), dir[1]);
    int32_t y, end, dy;
    float step;
    if (0.0f > drop) {
        y = 0;
        end = h;
        step = drop;
    } else {
        y = h - 1;
        end = -1;
        step = Negated(drop);
        shear = Negated(shear);
    }
    dy = 0.0f > drop ? 1 : -1;
    if (y != end) {
        float *run = mRun.data();
        const uint32_t columns = static_cast<uint32_t>(w);
        do {
            const float *values = mValues.data();
            float *heights = mHeights.data();
            int32_t first = -1, last = 0;
            for (uint32_t j = 0; j < columns; ++j) {
                const uint32_t cell = static_cast<uint32_t>(y) * static_cast<uint32_t>(w) + j;
                if (values[cell] > run[j]) run[j] = values[cell];
                if (!(heights[cell] == run[j])) {
                    heights[cell] = run[j];
                    last = static_cast<int32_t>(j);
                    if (first < 0) first = static_cast<int32_t>(j);
                }
                run[j] = AddS(step, run[j]);
            }
            if (first >= 0) {
                rect[3] = y;
                if (rect[1] < 0) {
                    rect[1] = y;
                    rect[0] = first;
                    rect[2] = last;
                } else {
                    rect[0] = std::min(first, rect[0]);
                    rect[2] = std::max(last, rect[2]);
                }
            }
            if (shear > 0.0f) {
                // ORIGINAL-BUG: as SweepX's, with no columns (w == 0) the loop runs away (reproduced; left out of the test).
                for (uint32_t j = 0; j < columns - 1; ++j) run[j] = AddS(MulS(SubS(run[j + 1], run[j]), shear), run[j]);
            } else {
                for (int32_t j = w - 1; j > 0; --j) run[j] = SubS(run[j], MulS(SubS(run[j - 1], run[j]), shear));
            }
            y += dy;
        } while (y != end);
    }
    for (int k = 0; k < 4; ++k) mChanged[k] = rect[k];
}

bool __cdecl cSunHeightMap::FloorVisible(cIWorldDB *db, const WorldTile *tile, int32_t minLevel)
{
    bool visible = db->GetFloorVisibility(tile, nullptr);
    if (visible && tile->level == 0 && minLevel < 0) {
        WorldTile below{};
        below.x = tile->x;
        below.y = tile->y;
        below.level = -1;
        below.segments = tile->segments;
        visible = db->GetDeckFoundation(&below) != 5;
    }
    return visible;
}

void __cdecl cSunHeightMap::BuildGround(cSunHeightMap *map, cIWorldDB *db, int32_t n)
{
    const float cell = CellSize(db, n);
    const int32_t minX = db->GetMinX(), minY = db->GetMinY();
    const int32_t endX = db->GetMaxX() + 1, endY = db->GetMaxY() + 1;
    map->Resize(endX * n, endY * n, cell, -1000.0f);
    const int32_t maxLevel = db->GetMaxLevel(), minLevel = db->GetMinLevel();
    for (int32_t ty = minY; ty < endY; ++ty) {
        for (int32_t tx = minX; tx < endX; ++tx) {
            for (int32_t q = 0; q < 4; ++q) {
                const int32_t triangle = (q >> 1) ^ q; // the quadrant's triangle in the cell test below
                const int8_t mask = static_cast<int8_t>(1 << q);
                // The highest level with a visible floor (or the ground) in this quadrant.
                int32_t level = maxLevel;
                bool found = false;
                for (; level >= minLevel; --level) {
                    WorldTile tile{};
                    tile.x = tx;
                    tile.y = ty;
                    tile.level = level;
                    tile.segments = mask;
                    if (db->HasFloorOrGround(&tile, mask, true) && FloorVisible(db, &tile, minLevel)) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
                for (int32_t i = 0; i < n; ++i) {
                    const int32_t r = ty * n + i;
                    for (int32_t j = 0; j < n; ++j) {
                        const int32_t c = tx * n + j;
                        if (((j >= n - 1 - i ? 1 : 0) | (j >= i ? 2 : 0)) != triangle) continue;
                        const float px = static_cast<float>((static_cast<double>(c) + 0.5) * static_cast<double>(cell));
                        const float py = static_cast<float>((static_cast<double>(r) + 0.5) * static_cast<double>(cell));
                        if (level == 0) {
                            const WorldPoint point{px, py, 0};
                            const float height = static_cast<float>(worldheights::GroundHeightAt(&point));
                            map->mGround.data()[map->mWidth * r + c] = height;
                            map->mValues.data()[map->mWidth * r + c] = height;
                        } else {
                            const WorldPoint point{px, py, level};
                            map->mValues.data()[map->mWidth * r + c] =
                                static_cast<float>(worldheights::GroundHeightAt(&point));
                            const WorldPoint ground{px, py, 0};
                            map->mGround.data()[map->mWidth * r + c] =
                                static_cast<float>(worldheights::GroundHeightAt(&ground));
                        }
                    }
                }
            }
        }
    }
}

void __cdecl cSunHeightMap::AddAwnings(cSunHeightMap *map, cIWorldDB *db, int32_t n, const float *dir)
{
    const int32_t lo = db->GetMinLevel(), hi = db->GetMaxLevel();
    const float width = static_cast<float>(db->GetWidth()), height = static_cast<float>(db->GetHeight());
    std::vector<worlditer::TileCoord, GZServiceAllocator<worlditer::TileCoord>> tiles; // allocator-service storage
    for (int32_t level = lo; level <= hi; ++level) {
        worlditer::TileBoxIterator it;
        it.ConstructLevel(level, db);
        for (; !it.Done(); it.Next()) {
            WorldTile tile{};
            it.Get(reinterpret_cast<worlditer::TileCoord *>(&tile));
            if (db->GetAwning(&tile) != 0xffffffffu) tiles.push_back({tile.x, tile.y, tile.level});
        }
    }
    const float fn = static_cast<float>(n);
    for (const worlditer::TileCoord &t : tiles) {
        WorldTile corner{};
        worldmath::CornerTile(&corner.x, &t.x, 3);
        const float elevation = db->GetElevation(&corner);
        float minX = kFloatMax, minY = kFloatMax, maxX = 0.0f, maxY = 0.0f;
        for (int32_t k = 1; k < 8; k += 2) {
            float c[3];
            worldmath::CornerPosition(c, &t.x, k);
            const float px = AddS(MulS(dir[0], 0.0f), c[0]);
            const float py = AddS(MulS(dir[1], 0.0f), c[1]);
            minX = MinSS(minX, px);
            maxX = MaxSS(maxX, px);
            minY = MinSS(minY, py);
            maxY = MaxSS(maxY, py);
        }
        const int32_t x0 = Floor(MulS(fn, minX)), x1 = Ceil(MulS(MinSS(width, maxX), fn));
        const int32_t y0 = Floor(MulS(fn, minY)), y1 = Ceil(MulS(MinSS(height, maxY), fn));
        const float value = AddS(elevation, 1.0f);
        for (int32_t y = y0; y < y1; ++y)
            for (int32_t x = x0; x < x1; ++x) map->mValues.data()[map->mWidth * y + x] = value;
    }
}

void __cdecl cSunHeightMap::AddRoofs(cSunHeightMap *map, cIWorldDB *db, int32_t n)
{
    void *roofs = db->GetRoofs();
    const float cell = CellSize(db, n);
    const float fn = static_cast<float>(n);
    IdList ids;
    RawSlot<28>(roofs, static_cast<ServiceBlockVector *>(&ids)); // receiver: the roofs object; the ids of every roof
    for (uint32_t i = 0; i < ids.Count(); ++i) {
        // receiver: the roofs object; the roof's record
        const uint8_t *record = static_cast<const uint8_t *>(RawSlot<25, void *>(roofs, ids.At(i)));
        const float *min = reinterpret_cast<const float *>(record + 8);
        const float *max = reinterpret_cast<const float *>(record + 0x14);
        const int32_t level = *reinterpret_cast<const int32_t *>(record + 0x10);
        const int32_t kind = *reinterpret_cast<const int32_t *>(record + 0x20);
        if (RoofKindSkipped(kind)) continue;
        if (RoofKindDiagonal(kind)) {
            float o0, o1, o2, o3;
            RoofDiamond(min, max, &o0, &o1, &o2, &o3);
            const int32_t kindNow = *reinterpret_cast<const int32_t *>(record + 0x20);
            if (kindNow == 0x15 || kindNow == 0x14 || kindNow == 0x13) {
                o3 = SubS(o3, 0.5f);
                o1 = AddS(o1, 0.5f);
                o2 = SubS(o2, 0.5f);
                o0 = AddS(o0, 0.5f);
            }
            const worlditer::TileCoord a{static_cast<int32_t>(o0), static_cast<int32_t>(o1), level};
            const worlditer::TileCoord b{static_cast<int32_t>(o2), static_cast<int32_t>(o3), level};
            worlditer::TileDiamondIterator it;
            it.Construct(&a, &b);
            for (; !it.Done(); it.Next()) {
                worlditer::TileCoord t;
                it.Get(&t);
                for (int32_t r = t.y * n; r < (t.y + 1) * n; ++r) {
                    const float y = MulS(static_cast<float>(r), cell);
                    for (int32_t c = t.x * n; c < (t.x + 1) * n; ++c) {
                        const int32_t lvl = *reinterpret_cast<const int32_t *>(record + 0x10);
                        float *values = map->mValues.data();
                        const int32_t at = map->mWidth * r + c;
                        values[at] = RoofHeight(roofs, ids.At(i), MulS(static_cast<float>(c), cell), y, lvl);
                    }
                }
            }
        } else {
            const int32_t x0 = Floor(MulS(fn, min[0] > max[0] ? max[0] : min[0]));
            const int32_t x1 = Ceil(MulS(fn, max[0] > min[0] ? max[0] : min[0]));
            const int32_t y0 = Floor(MulS(fn, min[1] > max[1] ? max[1] : min[1]));
            const int32_t y1 = Ceil(MulS(fn, max[1] > min[1] ? max[1] : min[1]));
            for (int32_t r = y0; r < y1; ++r) {
                const float y = MulS(static_cast<float>(r), cell);
                for (int32_t c = x0; c < x1; ++c) {
                    const int32_t lvl = *reinterpret_cast<const int32_t *>(record + 0x10);
                    float *values = map->mValues.data();
                    const int32_t at = map->mWidth * r + c;
                    values[at] = RoofHeight(roofs, ids.At(i), MulS(static_cast<float>(c), cell), y, lvl);
                }
            }
        }
    }
}

void __cdecl cSunHeightMap::AddWalls(cSunHeightMap *map, cIWorldDB *db, int32_t n, const float *dir)
{
    (void)db->GetTileSize(); // read and dropped
    void *walls = db->GetPartitions();
    void *roofs = db->GetRoofs();
    const int32_t maxLevel = db->GetMaxLevel();
    const int32_t count = static_cast<int32_t>(RawSlot<55>(walls)); // receiver: the partitions object; the wall count
    for (int32_t index = 0; index < count; ++index) {
        WallRecord wall;
        RawSlot<56>(walls, &wall, index); // receiver: the partitions object; wall `index` copied out
        if (wall.level0 < 0) continue;
        const int32_t rx0 = Round(wall.x0), ry0 = Round(wall.y0), rx1 = Round(wall.x1), ry1 = Round(wall.y1);
        uint32_t id = RawSlot<50>(walls, &wall); // receiver: the partitions object; the wall's catalog id
        const bool id12d = worldcatalog::IdIs12d(&id);
        const bool fence = worldcatalog::IsKind3(&id) || id12d; // kind 3 or the 0x12d id: tops follow the partition
        const bool kindWall = worldcatalog::IsKind1Or3(&id) || worldcatalog::IsKind2(&id);
        float v0, v1;
        if (kindWall && !fence) {
            float base = 0.0f;
            if (!worldcatalog::ProductFlag10(&id)) base = worldcatalog::PartitionValue(&id);
            v0 = static_cast<float>(AddMem87(ElevationAt(db, rx0, ry0, wall.level0), base));
            v1 = static_cast<float>(AddMem87(ElevationAt(db, rx1, ry1, wall.level1), base));
        } else if (wall.level0 < maxLevel) {
            const WorldPoint p0{static_cast<float>(rx0), static_cast<float>(ry0), wall.level0};
            if (RawSlot<37, bool>(roofs, &p0)) // receiver: the roofs object; a roof over the point
                // receiver: the roofs object; the roof's underside at the point (x87)
                v0 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &p0, 1) - 0.25));
            else if (fence) {
                const float raise = (!id12d && WallRaised(&wall)) ? 0.75f : 0.0f;
                const float part = worldcatalog::PartitionValue(&id);
                v0 = static_cast<float>(AddMem87(AddMem87(ElevationAt(db, rx0, ry0, wall.level0), part), raise));
            } else
                v0 = static_cast<float>(ElevationAt(db, rx0, ry0, wall.level0 + 1));
            const WorldPoint p1{static_cast<float>(rx1), static_cast<float>(ry1), wall.level1};
            if (RawSlot<37, bool>(roofs, &p1)) // receiver: the roofs object
                v1 = static_cast<float>(x87::Rounded(RawSlot<22, double>(roofs, &p1, 1) - 0.25)); // receiver: roofs
            else if (fence)
                v1 = v0;
            else
                v1 = static_cast<float>(ElevationAt(db, rx1, ry1, wall.level1 + 1));
        } else {
            const float top = db->GetWallHeight();
            v0 = static_cast<float>(AddMem87(ElevationAt(db, rx0, ry0, wall.level0), top));
            v1 = static_cast<float>(AddMem87(ElevationAt(db, rx1, ry1, wall.level1), top));
        }

        // The line of cells from end 0 to end 1, in grid cells; t runs from half a step to one less half a step.
        const int32_t x0 = rx0 * n, y0 = ry0 * n, x1 = rx1 * n, y1 = ry1 * n;
        const int32_t w = map->mWidth, h = map->mHeight;
        float *values = map->mValues.data();
        const float rise = SubS(v1, v0);
        auto at = [&](float t) { return AddS(MulS(rise, t), v0); };
        if (x0 == x1) { // along y: the columns either side of x0
            const float step = static_cast<float>(1.0 / static_cast<double>(y1 - y0));
            const float half = static_cast<float>(static_cast<double>(step) * 0.5);
            const float t0 = y1 < y0 ? SubS(1.0f, half) : half;
            int32_t lo = y1 >= y0 ? y0 : y1, hi = y1 >= y0 ? y1 : y0;
            if (lo > 0 && (!dir || !(0.0f > dir[1]))) --lo;
            if (hi >= h || (dir && dir[1] > 0.0f)) --hi;
            if (x0 < w && !(dir && dir[0] > 0.0f)) {
                float t = t0;
                for (int32_t r = lo; r <= hi; ++r, t = AddS(t, step)) values[w * r + x0] = at(t);
            }
            if (x0 > 0 && !(dir && 0.0f > dir[0])) {
                float t = t0;
                for (int32_t r = lo; r <= hi; ++r, t = AddS(t, step)) values[w * r + x0 - 1] = at(t);
            }
            continue;
        }
        const float step = static_cast<float>(1.0 / static_cast<double>(x1 - x0));
        const float half = static_cast<float>(static_cast<double>(step) * 0.5);
        if (y0 == y1) { // along x: the rows either side of y0
            float t0 = half;
            int32_t lo = x0, hi = x1;
            if (x1 < x0) {
                t0 = SubS(1.0f, half);
                lo = x1;
                hi = x0;
            }
            if (lo > 0 && (!dir || !(0.0f > dir[0]))) --lo;
            if (hi >= w || (dir && dir[0] > 0.0f)) --hi;
            if (y0 < h && !(dir && dir[1] > 0.0f)) {
                float t = t0;
                for (int32_t c = lo; c <= hi; ++c, t = AddS(t, step)) values[w * y0 + c] = at(t);
            }
            if (y0 > 0 && !(dir && 0.0f > dir[1])) {
                float t = t0;
                for (int32_t c = lo; c <= hi; ++c, t = AddS(t, step)) values[w * (y0 - 1) + c] = at(t);
            }
            continue;
        }
        // Diagonal: one cell per column, the row moving by dy; then the cells beside the line on each side.
        float t0;
        int32_t lo, hi, row, dy;
        if (x1 < x0) {
            t0 = SubS(1.0f, half);
            lo = x1;
            hi = x0;
            row = y0 <= y1 ? y1 - 1 : y1;
            dy = y0 > y1 ? 1 : -1;
        } else {
            t0 = half;
            lo = x0;
            hi = x1;
            row = y1 > y0 ? y0 : y0 - 1;
            dy = y1 > y0 ? 1 : -1;
        }
        {
            float t = t0;
            int32_t r = row;
            for (int32_t c = lo; c < hi; ++c, r += dy, t = AddS(t, step)) values[w * r + c] = at(t);
        }
        bool sideA = true, sideB = true; // the cells after / before the line along the row direction
        if (dir) {
            const float side = SubS(MulS(static_cast<float>(dy), dir[1]), dir[0]);
            if (side > 0.0f) sideA = false;
            if (0.0f > side) sideB = false;
        }
        if (sideB) {
            float t = t0;
            int32_t r = row;
            for (int32_t c = lo + 1; c < hi; ++c, r += dy, t = AddS(t, step)) values[w * r + c] = at(t);
        }
        if (sideA) {
            float t = t0;
            int32_t r = row + dy;
            for (int32_t c = lo; c < hi - 1; ++c, r += dy, t = AddS(t, step)) values[w * r + c] = at(t);
        }
        const int32_t length = hi - lo;
        if (lo > 0 && sideA) values[w * row + lo - 1] = at(t0);
        if (hi < w && sideB) values[w * ((length - 1) * dy + row) + hi] = at(SubS(1.0f, t0));
        const int32_t rowC = dy * length + row;
        if (rowC >= 0 && rowC < h && sideA) values[w * rowC + hi - 1] = at(SubS(1.0f, t0));
        const int32_t rowD = row - dy;
        if (rowD >= 0 && rowD < h && sideB) values[w * rowD + lo] = at(t0);
    }
}

} // namespace nTSSG
