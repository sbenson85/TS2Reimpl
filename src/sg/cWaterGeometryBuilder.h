#pragma once
// nTSSG::cWaterGeometryBuilder (lane 261108f, scenegraph LT1b): the geometry builder of a neighbourhood or lot's water
// surface. cTerrain makes one (slot 3: new 0x150, the constructor with mostDerived 1), sets it up for its tile grid
// (Setup) and moves its vertices (SetVertexHeight); Update then compares the water with the terrain under it, repaints the
// seabed and beach layers of the tiles whose depth changed (through the placement service's terrain painter) and keeps
// each triangle's subset: a triangle under water belongs to the subset (draw group) named after the lot it lies on, a dry
// one to none. Build hands the container the vertices (positions, normals, colours with the depth as alpha, texture
// coordinates), one triangle-list group per non-empty subset and the collision mesh.
//
// The grid: W x H tiles, each a centre vertex and four triangles (k 0..3) fanning around it; vertex (x, y) is index
// s*x + 2y with s = 2H + 2, and the centre of tile (x, y) is s*x + 2y + 1. Triangle k of tile (x, y) is number
// (H*x + y)*4 + k in the collision mesh and the per-triangle subset list.
//
// Standalone shape (docs/standalone-shape.md), on our cGeometryBuilder (src/sg/cGeometryBuilder.h), as
// cTangentSpaceBuilder but with no interface of its own:
//
//   class cWaterGeometryBuilder : public cGeometryBuilder
//
//   +0x00 the behaviour template's vtable (0xdbe5ac: the deleting destructor 0x3b5f70), +0x04 vbptr, +0x08 count,
//   +0x0c cIGZPersistSerializable2 (0xdbe5bc), +0x10 the listener; the fields below; +0x148 the vtordisp,
//   +0x14c the virtual base cIGeometryBuilder (18 slots, each a this-adjusting thunk 0x3b7ba5 ... 0x3b63b6).
//
// Hooks: the constructor (cTerrain's) and the methods cTerrain calls (Setup, SetVertexHeight, Update, SetTriangleSubset,
// RemoveTriangle, IsTriangleDry, GrowDirtyRect, ClearSubsetsChanged, GetVertexHeight); the rest is reached through them
// or through our vtables (db/status/cWaterGeometryBuilder.jsonl). Not ours yet (row LT1c): the render-parameter reader
// 0x3b7c40 that fills the paint thresholds below, and its Load/Unload 0x3b9360/0x3b9380.
#include "compgeom/CompGeom.h"
#include "game/owned.h"
#include "rz/GameAllocator.h"
#include "rz/RZRefCountBase.h"
#include "rz/cRZRandom.h"
#include "sg/SGLocalString.h"
#include "sg/cBoundingVolumeBuilder.h" // cBoundingVolumeNameLess: the subset names' case-blind order
#include "sg/cGeometryBuilder.h"

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// The paint thresholds (.bss; written by the render-parameter reader 0x3b7c40, row LT1c) and the defaults in .data.
TS2_OWNED_STATIC(float, 10da1b8);   // the shallow seabed's lowest depth (exclusive)
TS2_OWNED_STATIC(float, 10da1bc);   // the shallow seabed's ramp scale (below the fade start)
TS2_OWNED_STATIC(float, 10da1c0);   // the shallow seabed's fade scale (above the fade start)
TS2_OWNED_STATIC(float, 10da1c4);   // the deep seabed's ramp scale
TS2_OWNED_STATIC(float, 10da1c8);   // the beach's lowest depth (exclusive)
TS2_OWNED_STATIC(float, 10da1cc);   // the beach's peak depth
TS2_OWNED_STATIC(float, 10da1d0);   // the beach's highest depth (exclusive)
TS2_OWNED_STATIC(float, 10da1d4);   // the beach's ramp-up scale
TS2_OWNED_STATIC(float, 10da1d8);   // the beach's ramp-down scale
TS2_OWNED_STATIC(float, 10da1dc);   // the beach's wet-corner distance limit (exclusive)
TS2_OWNED_STATIC(float, 10da1e0);   // the beach's wet-corner distance scale
TS2_OWNED_STATIC(int32_t, 10da1e4); // the beach's wet-corner search radius, in tiles
TS2_OWNED_STATIC(float, 10da1e8);   // the beach's wet-corner alpha scale
TS2_OWNED_STATIC(float, 100d138);   // 0.6 in the image: the shallow seabed's highest depth (exclusive)
TS2_OWNED_STATIC(float, 100d13c);   // 0.5 in the image: the shallow seabed's fade start, the deep seabed's lowest depth
TS2_OWNED_STATIC(float, 100d140);   // 1.2 in the image: the deep seabed's full depth
TS2_OWNED_STATIC(float, 100d168)[3]; // {0, 0, 1} in the image: every vertex's normal

namespace nTSSG {

// C4250 ("inherits ... via dominance"): intended, as cTangentSpaceBuilder's; C4324: RandomBlock's alignment, intended.
#pragma warning(push)
#pragma warning(disable : 4250 4324)
class cWaterGeometryBuilder final : public nGZSceneGraph::cGeometryBuilder {
public:
    using Vec3 = nGZCompGeom::Vec3;
    struct Vec2 {
        float x, y;
    };
    struct Rect {
        int32_t xmin, ymin, xmax, ymax;
    };
    // A triangle: tile (x, y), quarter k (0..3).
    struct TriangleCoord {
        uint32_t x, y, k;
    };

    static constexpr uint32_t kClassId = 0x2b6efb38;
    static constexpr uint32_t kVersion = 1;
    static constexpr uint8_t kNoSubset = 0xff;

    // A draw group: the lot's name, its triangles (sorted numbers) and their corners (three indices each, in the same
    // order). Counted (rz::cRZRefCount: the deleting destructor 0x3b5e60, vtable 0xdbe594).
    class cSubset final : public rz::cRZRefCount {
    public:
        cSubset() = default;
        ~cSubset() override = default;

        SGLocalString mName;               // +0x08
        std::vector<uint32_t> mTriangles;  // +0x28
        std::vector<uint16_t> mIndices;    // +0x34
    };

    // +0x68: a generator the class never draws from (seeded from the clock by the constructor) and 12 bytes nothing
    // touches; 8-aligned in the original's layout.
    struct alignas(8) RandomBlock {
        cRZRandom mRandom;
        uint32_t mUnused[3];
    };

    cWaterGeometryBuilder();               // 0x3b5970
    cWaterGeometryBuilder(const cWaterGeometryBuilder &) = delete;
    cWaterGeometryBuilder &operator=(const cWaterGeometryBuilder &) = delete;
    ~cWaterGeometryBuilder() override;     // 0x3b5b80 (deleting 0x3b5f70)

    // ---- the persist face (+0xc) ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // shared_vf_3b7bd0: the record, then cGeometryBuilder's
    cPersistTypeInfo GetTypeInfo() override;                // 0x3b94e0: {"cWaterGeometryBuilder", kClassId, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0x3b95c0

    // ---- cIGeometryBuilder ----
    void Build(nGZSceneGraph::cIGeometryDataContainer *container) override; // 16: 0x3b75b4 (vtordisp adjustment, body 0x3b75c0)
    const char *GetName() override;                          // 17: 0x3b63b6, "WaterGeometryBuilder"

    // ---- what cTerrain calls ----
    void Setup(uint32_t width, uint32_t height, void *terrain);           // 0x3b6c50
    void Update();                                                         // 0x3b7850
    void SetVertexHeight(uint32_t x, uint32_t y, float z);                 // 0x3b9140
    void SetTriangleSubset(const TriangleCoord *coord, const char *name);  // 0x3b9060
    void RemoveTriangle(const TriangleCoord *coord);                       // 0x3b6510
    bool IsTriangleDry(TriangleCoord coord);                               // 0x3b7080
    void GrowDirtyRect(int32_t x, int32_t y);                              // 0x3b7110
    void ClearSubsetsChanged();                                            // 0x3b7190
    float GetVertexHeight(uint32_t x, uint32_t y);                         // 0x3b8e10

    // ---- helpers ----
    void Clear();                                                          // 0x3b8e30
    void InitVertex(uint32_t index, const Vec3 *position);                 // 0x3b6bd0
    void FillCornerTables();                                               // 0x3b6470
    void BuildCollision();                                                 // 0x3b6a00
    void ResetCollision();                                                 // 0x3b6610
    int32_t CornerIndex(const TriangleCoord *coord, int32_t corner);       // 0x3b71a0
    int32_t CentreIndex(int32_t x, int32_t y);                             // 0x3b6450
    uint8_t SubsetOfTriangle(const TriangleCoord *coord);                  // 0x3b93a0
    void UpdateCentreVertex(int32_t x, int32_t y);                         // 0x3b63d0
    void SetVertexZ(int32_t index, float z);                               // 0x3b8fe0
    void SetDepth(int32_t index, float depth);                             // 0x3b90f0
    void TileDepths(int32_t x, int32_t y, float *out);                     // 0x3b66f0
    float Bilerp(const float *corners, float u, float v);                  // 0x3b67e0
    static float NearestDistance(const Vec2 *point, const std::vector<Vec2> *points); // 0x3b6650 (cdecl)
    void CollectWetCorners(const Rect *rect, std::vector<Vec2> *out);      // 0x3b6840
    bool TileDepthChanged(uint32_t x, int32_t y);                          // 0x3b6940
    void GrowClampRect(Rect *rect, int32_t by);                            // 0x3b6550
    void GrowClampRectToSize(Rect *rect, int32_t by);                      // 0x3b65b0
    void PaintTile(int32_t x, int32_t y);                                  // 0x3b71e0
    uint8_t FindOrAddSubset(const char *name);                             // 0x3b93d0
    void AddSubset(const char *name);                                      // 0x3b5fb0
    void AddTriangleToSubset(uint8_t subset, const TriangleCoord *coord);  // 0x3b6240
    void RemoveTriangleFromSubset(uint8_t subset, uint32_t triangle);      // 0x3b8d80

    std::vector<Vec3> mPositions;                    // +0x14
    std::vector<Vec3> mNormals;                      // +0x20
    std::vector<Vec2> mTexCoords;                    // +0x2c
    std::vector<uint32_t> mColours;                  // +0x38: 0x00ffffff, the depth curve's alpha in the top byte
    std::vector<float> mDepths;                      // +0x44: water height minus terrain height, per vertex
    std::vector<cSubset *> mSubsets;                 // +0x50 counted
    std::map<std::string, uint32_t, nGZSceneGraph::cBoundingVolumeNameLess> mSubsetIndex; // +0x5c: name -> index
    RandomBlock mRandom;                             // +0x68
    std::vector<uint8_t> mTriangleSubsets;           // +0x80: each triangle's subset, kNoSubset for none
    uint8_t mSubsetsChanged = 0;                     // +0x8c
    int32_t mCornerTable[12] = {};                   // +0x90: triangle k's corners c at [k*3 + c], tile-relative
    int32_t mTileCorners[4] = {};                    // +0xc0: the tile's four grid vertices, tile-relative
    std::set<uint32_t, std::less<uint32_t>, GZServiceAllocator<uint32_t>> mChangedTriangles; // +0xd0: to refit
    uint8_t mRebuildAll = 0;                         // +0xd8: Build rebuilds the collision tree (else refits)
    uint8_t mCollisionEnabled = 1;                   // +0xd9
    uint32_t mWidth = 0;                             // +0xdc tiles
    uint32_t mHeight = 0;                            // +0xe0 tiles
    int32_t mStride = 0;                             // +0xe4: 2H + 2, the vertices of one grid column
    int32_t mTrianglesPerColumn = 0;                 // +0xe8: 4H
    Rect mBounds = {};                               // +0xec: {0, 0, W - 1, H - 1}
    int32_t mSizeX = 0;                              // +0xfc: W
    int32_t mSizeY = 0;                              // +0x100: H
    uint32_t mPaintSubdivisions = 0;                 // +0x104: paint texels per tile edge
    float mPaintStep = 0.0f;                         // +0x108: 1 / subdivisions
    Rect mDirtyRect = {};                            // +0x10c: the vertices moved since the last Update
    uint8_t mDirty = 0;                              // +0x11c
    uint8_t mSkipPaint = 0;                          // +0x11d: the next Update leaves the paint alone (after Setup)
    std::vector<uint64_t> mUnused120;                // +0x120: never filled
    uint8_t mShallowLayer = 0;                       // +0x12c "terrain-seabed-shallow"
    uint8_t mDeepLayer = 0;                          // +0x12d "terrain-seabed-deep"
    uint8_t mBeachLayer = 0;                         // +0x12e "terrain-beach"
    nGZCompGeom::cITriangleSetShape *mShape = nullptr; // +0x130 counted: the collision mesh
    cIGZUnknown *mService = nullptr;                 // +0x134 counted: the placement service
    cIGZUnknown *mPainter = nullptr;                 // +0x138 counted: its terrain painter
    cIGZUnknown *mTerrain = nullptr;                 // +0x13c counted: the cTerrain
    cIGZUnknown *mGrid = nullptr;                    // +0x140 counted: the terrain's cTerrainGeometryData
};
#pragma warning(pop)

} // namespace nTSSG
