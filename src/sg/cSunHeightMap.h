// nTSSG::cSunHeightMap (lanes 261108a and 261108i, scenegraph LG1 and LG5): the sun height map the cLightingManager
// embeds at +0x248 (0x64 bytes, no vtable). Three grids of mWidth x mHeight floats, row-major, mCell world units per
// cell: mGround the ground (or floor) under each cell, mValues that plus what stands on it (awnings, roofs, walls), and
// mHeights the height of the sun's shadow over each cell, swept from mValues along the sun's direction. The lighting
// manager's light-map builders (0x2e4300) fill it; its shadowed lighters read it.
//
// The grid builders take the world DB and n, the grid cells per tile, and are __cdecl free functions in the original
// (0x3be1c0-0x3c06e0); they are static members here. HeightAbove and Blend (LG1) are defined in cLightingManager.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nTSWorld {
class cIWorldDB;
struct WorldTile;
} // namespace nTSWorld

namespace nTSSG {

struct cSunHeightMap {
    // 0x3be100 (returns this): empty grids, sizes and cell 0, mFill -1000, mFlat clear, mBlendScale 1, mField50 0.7,
    // mField54 50. mChanged is not set.
    cSunHeightMap();
    // The hook adapter: the constructor run on `this`.
    cSunHeightMap *Construct();
    // 0x2cb6c0, the destructor (the hook adapter, lane 261110t; src/sg/cLightingManagerCreate.cpp): mRun, mHeights,
    // mGround and mValues freed in turn (the four vectors' destructors). Only the lighting manager's Destruct calls it.
    void Destruct();
    // 0x3be1a0: every mHeights cell set to mFill, then mFlat set.
    void ResetFlat();
    // 0x3c0750 (ret 0x10): the size, cell and fill stored (mScale = 1 / cell); mGround, mValues and mHeights each
    // cleared and refilled with width * height copies of the fill, in that order.
    void Resize(int32_t width, int32_t height, float cell, float fill);
    // 0x3bfde0 (ret 4): with the sun above the horizon (not 0 >= dir.z; a NaN counts as above) and not straight
    // overhead (|dir.x| or |dir.y| at least 0.001), SweepY when |dir.y| > |dir.x|, else SweepX, and mFlat cleared;
    // otherwise ResetFlat unless mFlat is already set.
    void BuildShadow(const float *dir);
    // 0x3bfe60 (ret 4): the shadow swept along x, towards the side the sun is not on, one column at a time: each row's
    // running height takes the larger of itself and the column's mValues cell, is stored to mHeights where it differs,
    // then drops by mCell * dir.z / |dir.x|; between columns the running heights are sheared across rows by -dir.y /
    // dir.x (interpolating towards the neighbouring row). mChanged gets the box of the cells that changed.
    void SweepX(const float *dir);
    // 0x3c0280 (ret 4): SweepX with x and y exchanged (rows swept, the running heights per column).
    void SweepY(const float *dir);

    // 0x3c06e0 (__cdecl): the world DB's floor visibility (slot 49) for the tile; on level 0 of a world whose lowest
    // level is below 0, also that the tile one level down (the segment byte kept) has no deck foundation of kind 5.
    static bool __cdecl FloorVisible(nTSWorld::cIWorldDB *db, const nTSWorld::WorldTile *tile, int32_t minLevel);
    // 0x3bea70 (__cdecl): resizes the map to n cells per tile over the lot ((max + 1) * n each way, the tile size / n
    // per cell, fill -1000), then fills mGround and mValues per tile quadrant: the highest level with a visible floor
    // or ground there (HasFloorOrGround and FloorVisible) gives its ground height at each cell centre of the quadrant's
    // triangle; mGround always takes level 0's.
    static void __cdecl BuildGround(cSunHeightMap *map, nTSWorld::cIWorldDB *db, int32_t n);
    // 0x3be1c0 (__cdecl): every tile with an awning (slot 59 not -1) raises the cells under its box (corners 1, 3, 5,
    // 7, offset by dir * 0) to its corner-3 elevation plus 1. `dir` is read but its offset is always 0.
    static void __cdecl AddAwnings(cSunHeightMap *map, nTSWorld::cIWorldDB *db, int32_t n, const float *dir);
    // 0x3be5c0 (__cdecl): every roof (the roofs object's slot 28 ids, slot 25 records) but kind 15 sets the cells
    // under it to the roof's height there (slot 30 at each cell's corner): a diamond of tiles for the diagonal kinds
    // (0x7e17e0), else its bounding box.
    static void __cdecl AddRoofs(cSunHeightMap *map, nTSWorld::cIWorldDB *db, int32_t n);
    // 0x3bef50 (__cdecl): every wall (partitions slots 55 / 56) on a level of 0 and up rasterised into mValues as a
    // line of cells from its top at one end to its top at the other (the elevation plus the wall's partition height,
    // a roof's underside less 0.25, the next level's ground, or the wall height on the top level), on both sides of
    // the line unless `dir` (may be null) says the sun is on that side.
    static void __cdecl AddWalls(cSunHeightMap *map, nTSWorld::cIWorldDB *db, int32_t n, const float *dir);

    // 0x2e2aa0 (ret 4; the value on the x87 stack): the height stored for the cell under p (x and y each plus 0.001,
    // times mScale, floored) minus p's z, one x87 subtraction at the thread's precision.
    double HeightAbove(const float *p) const;
    // 0x2d1600 (ret 0x10; returns out): a + b * (t * mBlendScale), divided by its length when its squared length is
    // above 0.
    float *Blend(float *out, const float *a, const float *b, float t) const;

    std::vector<float> mValues;  // 0x00 slot 39's debug view draws them (thirds, as red / green / blue)
    std::vector<float> mGround;  // 0x0c the height the sun's shadow is measured from (the manager's +0x254; the shadow
                                 // lighters subtract it from mHeights)
    std::vector<float> mHeights; // 0x18
    int32_t mWidth;              // 0x24
    int32_t mHeight;             // 0x28
    float mCell;                 // 0x2c world units per cell
    float mScale;                // 0x30 grid cells per world unit
    float mFill;                 // 0x34 -1000 at construction; Resize's fill
    uint8_t mFlat;               // 0x38 mHeights all mFill (no shadow)
    int32_t mChanged[4];         // 0x3c the last sweep's changed cells: x0, y0, x1, y1 (x0 -1 when none; the sweep's
                                 // last column or row, not past it)
    float mBlendScale;           // 0x4c 1 at construction
    float mField50;              // 0x50 0.7 at construction
    float mField54;              // 0x54 50 at construction
    std::vector<float> mRun;     // 0x58 the sweeps' running heights
};
static_assert(offsetof(cSunHeightMap, mGround) == 0x0c);
static_assert(offsetof(cSunHeightMap, mHeights) == 0x18);
static_assert(offsetof(cSunHeightMap, mWidth) == 0x24);
static_assert(offsetof(cSunHeightMap, mScale) == 0x30);
static_assert(offsetof(cSunHeightMap, mFlat) == 0x38);
static_assert(offsetof(cSunHeightMap, mChanged) == 0x3c);
static_assert(offsetof(cSunHeightMap, mBlendScale) == 0x4c);
static_assert(offsetof(cSunHeightMap, mRun) == 0x58);
static_assert(sizeof(cSunHeightMap) == 0x64);

} // namespace nTSSG
