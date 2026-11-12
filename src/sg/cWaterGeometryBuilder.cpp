// nTSSG::cWaterGeometryBuilder (lane 261108f). Layout, the grid and the hook decision: src/sg/cWaterGeometryBuilder.h.
#include "sg/cWaterGeometryBuilder.h"

#include "compgeom/TriangleSet.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/PlacementService.h"
#include "ifaces/TerrainPainter.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "ifaces/cITerrain.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject: the COM
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <climits>
#include <cfloat>
#include <cmath>
#include <cstddef>

namespace nTSSG {

static_assert(sizeof(cWaterGeometryBuilder) == 0x150, "cWaterGeometryBuilder size (cTerrain allocates 0x150)");
static_assert(sizeof(cWaterGeometryBuilder::cSubset) == 0x40, "cSubset size (AddSubset allocates 0x40)");
static_assert(offsetof(cWaterGeometryBuilder::cSubset, mName) == 0x08, "cSubset name");
static_assert(offsetof(cWaterGeometryBuilder::cSubset, mTriangles) == 0x28, "cSubset triangles");
static_assert(offsetof(cWaterGeometryBuilder::cSubset, mIndices) == 0x34, "cSubset indices");
static_assert(offsetof(cWaterGeometryBuilder, mPositions) == 0x14, "positions");
static_assert(offsetof(cWaterGeometryBuilder, mNormals) == 0x20, "normals");
static_assert(offsetof(cWaterGeometryBuilder, mTexCoords) == 0x2c, "texture coordinates");
static_assert(offsetof(cWaterGeometryBuilder, mColours) == 0x38, "colours");
static_assert(offsetof(cWaterGeometryBuilder, mDepths) == 0x44, "depths");
static_assert(offsetof(cWaterGeometryBuilder, mSubsets) == 0x50, "subsets");
static_assert(offsetof(cWaterGeometryBuilder, mSubsetIndex) == 0x5c, "subset index");
static_assert(offsetof(cWaterGeometryBuilder, mRandom) == 0x68, "generator");
static_assert(offsetof(cWaterGeometryBuilder, mTriangleSubsets) == 0x80, "triangle subsets");
static_assert(offsetof(cWaterGeometryBuilder, mSubsetsChanged) == 0x8c, "subsets changed");
static_assert(offsetof(cWaterGeometryBuilder, mCornerTable) == 0x90, "corner table");
static_assert(offsetof(cWaterGeometryBuilder, mTileCorners) == 0xc0, "tile corners");
static_assert(offsetof(cWaterGeometryBuilder, mChangedTriangles) == 0xd0, "changed triangles");
static_assert(offsetof(cWaterGeometryBuilder, mRebuildAll) == 0xd8, "rebuild flag");
static_assert(offsetof(cWaterGeometryBuilder, mCollisionEnabled) == 0xd9, "collision flag");
static_assert(offsetof(cWaterGeometryBuilder, mWidth) == 0xdc, "width");
static_assert(offsetof(cWaterGeometryBuilder, mStride) == 0xe4, "stride");
static_assert(offsetof(cWaterGeometryBuilder, mBounds) == 0xec, "bounds");
static_assert(offsetof(cWaterGeometryBuilder, mSizeX) == 0xfc, "size");
static_assert(offsetof(cWaterGeometryBuilder, mPaintSubdivisions) == 0x104, "paint subdivisions");
static_assert(offsetof(cWaterGeometryBuilder, mDirtyRect) == 0x10c, "dirty rectangle");
static_assert(offsetof(cWaterGeometryBuilder, mDirty) == 0x11c, "dirty flag");
static_assert(offsetof(cWaterGeometryBuilder, mSkipPaint) == 0x11d, "skip-paint flag");
static_assert(offsetof(cWaterGeometryBuilder, mUnused120) == 0x120, "unused list");
static_assert(offsetof(cWaterGeometryBuilder, mShallowLayer) == 0x12c, "layers");
static_assert(offsetof(cWaterGeometryBuilder, mShape) == 0x130, "shape");
static_assert(offsetof(cWaterGeometryBuilder, mGrid) == 0x140, "grid");

namespace {

using nGZCompGeom::cITriangleSetShape;

constexpr uint32_t kPositionIdentity = 0x5b830781; // the vertex positions element
constexpr uint32_t kNormalIdentity = 0x3b83078b;   // the vertex normals element
constexpr uint32_t kColourIdentity = 0xdb830795;   // the vertex colours element
constexpr uint32_t kTexCoordIdentity = 0xbb8307ab; // the texture coordinates element
constexpr uint32_t kTriangleList = 2;               // the groups' primitive type
constexpr uint32_t kInitialColour = 0x7f0000ff;
constexpr float kInitialDepth = -2000000.0f;        // 0xc9f42400
constexpr float kDepthChange = 0.01f;               // 0xda74e8: TileDepthChanged's threshold

// The layer names (cRZStrings in the image's .bss, built by its static initialisers; not ours).
constexpr uint32_t kShallowLayerName = 0x10da1f8; // "terrain-seabed-shallow"
constexpr uint32_t kDeepLayerName = 0x10da218;    // "terrain-seabed-deep"
constexpr uint32_t kBeachLayerName = 0x100d414;   // "terrain-beach"
constexpr uint32_t kDepthCurve = 0x10da238;       // the curve that turns a depth into the colour's alpha (not ours)

// The paint thresholds (owned statics, src/sg/cWaterGeometryBuilder.h).
float &kShallowMin = ts2_rva_10da1b8;
float &kShallowRamp = ts2_rva_10da1bc;
float &kShallowFade = ts2_rva_10da1c0;
float &kDeepRamp = ts2_rva_10da1c4;
float &kBeachMin = ts2_rva_10da1c8;
float &kBeachPeak = ts2_rva_10da1cc;
float &kBeachMax = ts2_rva_10da1d0;
float &kBeachUp = ts2_rva_10da1d4;
float &kBeachDown = ts2_rva_10da1d8;
float &kWetLimit = ts2_rva_10da1dc;
float &kWetScale = ts2_rva_10da1e0;
int32_t &kWetRadius = ts2_rva_10da1e4;
float &kWetAlpha = ts2_rva_10da1e8;
float &kShallowMax = ts2_rva_100d138;
float &kFadeStart = ts2_rva_100d13c;
float &kDeepFull = ts2_rva_100d140;

// 0x20e0b2 (__cdecl): the placement service (cached in 0x10cc298; made on first use). Borrowed.
cIGZUnknown *PlacementService() { return static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()()); }

// 0x3b58c0 (cTerrainGeometryData, __thiscall, ret 4): the z of the grid's vertex `index`.
float GridHeight(cIGZUnknown *grid, int32_t index)
{
    return OriginalFunction<float(__thiscall *)(void *, int32_t), 0x3b58c0>()(grid, index);
}

// The counted assignment the original inlines: the new one counted, stored, then the old one released.
template <typename T> void Assign(T *&member, T *value)
{
    if (value == member) return;
    if (value) value->AddRef();
    T *old = member;
    member = value;
    if (old) old->Release();
}

// Clear's release: the member nulled first, then the old one released.
template <typename T> void Drop(T *&member)
{
    if (!member) return;
    T *old = member;
    member = nullptr;
    old->Release();
}

nGZCompGeom::TriangleSet *TriangleSetOf(cITriangleSetShape *shape)
{
    return reinterpret_cast<nGZCompGeom::TriangleSet *>(shape->GetTriangleSet());
}

uint8_t LowByte(int32_t value) { return static_cast<uint8_t>(value); }

} // namespace

// 0x3b5970 (ret 4, mostDerived): the lists and the map empty, the generator seeded from the clock (its constructor's
// call returns to 0x3b5a9c), collision on, everything else zero.
cWaterGeometryBuilder::cWaterGeometryBuilder() { mRandom.mRandom.ConstructAt(0xffffffff, 0x3b5a9c); }

// 0x3b5b80: Clear, then the references released (all null after Clear), then the members in reverse.
cWaterGeometryBuilder::~cWaterGeometryBuilder()
{
    Clear();
    if (mGrid) mGrid->Release();
    if (mTerrain) mTerrain->Release();
    if (mPainter) mPainter->Release();
    if (mService) mService->Release();
    if (mShape) mShape->Release();
}

// shared_vf_3b7bd0 (ret 4): its record (any version), cGeometryBuilder's Read; the record's name released last.
void cWaterGeometryBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cGeometryBuilder::Read(reader);
    if (info.mName) info.mName->Release();
}

// 0x3b94e0 (hidden out): {"cWaterGeometryBuilder" (a new counted cRZString), the class id, version 1}.
cPersistTypeInfo cWaterGeometryBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = nGZSceneGraph::NewTypeName("cWaterGeometryBuilder");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0x3b95c0 (ret 4): its record (0x3b94e0 directly), its name released, then cGeometryBuilder's Write.
void cWaterGeometryBuilder::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cWaterGeometryBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cGeometryBuilder::Write(writer);
}

// 0x3b63b6.
const char *cWaterGeometryBuilder::GetName() { return "WaterGeometryBuilder"; }

// 0x3b8e30: the vertex lists emptied (capacity kept), the subsets released and forgotten, the shape and the four
// references nulled and released, the flags reset (the rebuild flag kept).
void cWaterGeometryBuilder::Clear()
{
    mPositions.clear();
    mNormals.clear();
    mColours.clear();
    mDepths.clear();
    mTexCoords.clear();
    for (cSubset *subset : mSubsets)
        if (subset) subset->Release();
    mSubsets.clear();
    mSubsetIndex.clear();
    Drop(mShape);
    mTriangleSubsets.clear();
    Drop(mGrid);
    Drop(mTerrain);
    Drop(mPainter);
    Drop(mService);
    mChangedTriangles.clear();
    mCollisionEnabled = 1;
    mDirty = 0;
    mSubsetsChanged = 0;
    mShallowLayer = kNoSubset;
    mDeepLayer = kNoSubset;
    mBeachLayer = kNoSubset;
}

// 0x3b6c50 (ret 0xc): a flat grid of W x H tiles at z 0 for the terrain, its painter's layers, a new collision shape.
// ORIGINAL-BUG: a width or height of 0 makes BuildCollision's unsigned loops run forever (the bound W - 1 wraps).
void cWaterGeometryBuilder::Setup(uint32_t width, uint32_t height, void *terrain)
{
    Clear();
    Assign(mService, PlacementService());
    Assign(mPainter, reinterpret_cast<cIGZUnknown *>(As<ifaces::PlacementService>(mService)->GetTerrainPainter()));
    auto *const painter = As<ifaces::TerrainPainter>(mPainter);
    mPaintSubdivisions = painter->GetSubdivisions();
    mPaintStep = 1.0f / static_cast<float>(static_cast<double>(mPaintSubdivisions));
    mShallowLayer = painter->GetLayerIndex(GameAddress<void>(kShallowLayerName));
    mDeepLayer = painter->GetLayerIndex(GameAddress<void>(kDeepLayerName));
    mBeachLayer = painter->GetLayerIndex(GameAddress<void>(kBeachLayerName));
    Assign(mTerrain, static_cast<cIGZUnknown *>(terrain));
    // cTerrain +0x3c: its cTerrainGeometryData.
    Assign(mGrid, *reinterpret_cast<cIGZUnknown **>(static_cast<uint8_t *>(terrain) + 0x3c));

    mWidth = width;
    mHeight = height;
    mBounds.xmin = 0;
    mBounds.xmax = static_cast<int32_t>(width - 1);
    mBounds.ymax = static_cast<int32_t>(height - 1);
    mStride = static_cast<int32_t>(2 * height + 2);
    mBounds.ymin = 0;
    mSizeX = static_cast<int32_t>(width);
    mSizeY = static_cast<int32_t>(height);
    mTrianglesPerColumn = static_cast<int32_t>(4 * height);
    FillCornerTables();

    const uint32_t count = (width + 1) * static_cast<uint32_t>(mStride);
    mPositions.resize(count);
    mNormals.resize(count);
    mTexCoords.resize(count);
    mColours.resize(count);
    mDepths.resize(count);
    mTriangleSubsets.resize(width * height * 4, kNoSubset);

    uint32_t index = 0;
    for (uint32_t x = 0; x <= width; ++x) {
        for (uint32_t y = 0; y <= height; ++y) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const Vec3 corner{fx, fy, 0.0f};
            InitVertex(index++, &corner);
            const Vec3 centre{fx + 0.5f, fy + 0.5f, 0.0f};
            InitVertex(index++, &centre);
        }
    }

    Drop(mShape);
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(cITriangleSetShape::kClassId, cITriangleSetShape::kClassId, reinterpret_cast<void **>(&mShape));
    BuildCollision();
    mDirty = 0;
    mSkipPaint = 1;
}

// 0x3b6bd0 (ret 8).
void cWaterGeometryBuilder::InitVertex(uint32_t index, const Vec3 *position)
{
    mPositions[index] = *position;
    mNormals[index] = Vec3{ts2_rva_100d168[0], ts2_rva_100d168[1], ts2_rva_100d168[2]};
    mTexCoords[index] = Vec2{position->x, position->y};
    mColours[index] = kInitialColour;
    mDepths[index] = kInitialDepth;
}

// 0x3b6470: triangle k's corners fan from the tile's centre (+1) between two neighbouring grid vertices, in the order
// bottom, left side, top, right side; the tile's four grid vertices in the order TileDepths reads them.
void cWaterGeometryBuilder::FillCornerTables()
{
    const int32_t s = mStride;
    const int32_t corners[12] = {0, 1, 2, 2, 1, s + 2, s + 2, 1, s, s, 1, 0};
    for (int i = 0; i < 12; ++i) mCornerTable[i] = corners[i];
    mTileCorners[0] = 0;
    mTileCorners[1] = 2;
    mTileCorners[2] = s + 2;
    mTileCorners[3] = s;
}

// 0x3b6a00: the collision mesh's vertices (each coordinate + 0, so -0 is stored as +0) and its triangles over the
// bounds, numbered in order; the tree is rebuilt by the next Build.
void cWaterGeometryBuilder::BuildCollision()
{
    const int32_t vertexCount = static_cast<int32_t>(mPositions.size());
    TriangleSetOf(mShape)->Resize(static_cast<uint32_t>(vertexCount), mHeight * mWidth * 4);
    for (int32_t i = 0; i < vertexCount; ++i) {
        const Vec3 &p = mPositions[i];
        const Vec3 v{p.x + 0.0f, p.y + 0.0f, p.z + 0.0f};
        TriangleSetOf(mShape)->SetVertex(static_cast<uint16_t>(i), &v);
    }
    int32_t triangle = 0;
    for (uint32_t x = static_cast<uint32_t>(mBounds.xmin); x <= static_cast<uint32_t>(mBounds.xmax); ++x) {
        for (uint32_t y = static_cast<uint32_t>(mBounds.ymin); y <= static_cast<uint32_t>(mBounds.ymax); ++y) {
            for (uint32_t k = 0; k < 4; ++k) {
                const TriangleCoord coord{x, y, k};
                const uint16_t a = static_cast<uint16_t>(CornerIndex(&coord, 0));
                const uint16_t b = static_cast<uint16_t>(CornerIndex(&coord, 1));
                const uint16_t c = static_cast<uint16_t>(CornerIndex(&coord, 2));
                TriangleSetOf(mShape)->SetTriangle(triangle++, a, b, c);
            }
        }
    }
    ResetCollision();
}

// 0x3b6610.
void cWaterGeometryBuilder::ResetCollision()
{
    mRebuildAll = 1;
    mChangedTriangles.clear();
}

// 0x3b71a0 (ret 8).
int32_t cWaterGeometryBuilder::CornerIndex(const TriangleCoord *coord, int32_t corner)
{
    return mCornerTable[coord->k * 3 + corner] + mStride * static_cast<int32_t>(coord->x) +
           2 * static_cast<int32_t>(coord->y);
}

// 0x3b6450 (ret 8).
int32_t cWaterGeometryBuilder::CentreIndex(int32_t x, int32_t y) { return mStride * x + 2 * y + 1; }

// 0x3b93a0 (ret 4).
uint8_t cWaterGeometryBuilder::SubsetOfTriangle(const TriangleCoord *coord)
{
    return mTriangleSubsets[(mHeight * coord->x + coord->y) * 4 + coord->k];
}

// 0x3b8e10 (ret 8).
float cWaterGeometryBuilder::GetVertexHeight(uint32_t x, uint32_t y)
{
    return mPositions[static_cast<uint32_t>(mStride) * x + 2 * y].z;
}

// 0x3b63d0 (ret 8): the tile's centre at the mean height of its four grid vertices.
void cWaterGeometryBuilder::UpdateCentreVertex(int32_t x, int32_t y)
{
    const int32_t centre = CentreIndex(x, y);
    const int32_t i = mStride * x + 2 * y;
    const int32_t j = (x + 1) * mStride + 2 * y;
    const float z = ((((mPositions[i].z + 0.0f) + mPositions[i + 2].z) + mPositions[j + 2].z) + mPositions[j].z) * 0.25f;
    SetVertexZ(centre, z);
}

// 0x3b8fe0 (ret 8): the vertex's z, and its copy in the collision mesh (each coordinate + 0).
void cWaterGeometryBuilder::SetVertexZ(int32_t index, float z)
{
    Vec3 &p = mPositions[index];
    p.z = z;
    const Vec3 v{p.x + 0.0f, p.y + 0.0f, z + 0.0f};
    TriangleSetOf(mShape)->SetVertex(static_cast<uint16_t>(index), &v);
}

// 0x3b90f0 (ret 8): the depth, and the colour's alpha from the depth curve (0xc0a6a1 on the curve 0x10da238).
void cWaterGeometryBuilder::SetDepth(int32_t index, float depth)
{
    mDepths[index] = depth;
    const float alpha = OriginalFunction<float(__thiscall *)(void *, float), 0xc0a6a1>()(GameAddress<void>(kDepthCurve), depth);
    mColours[index] = 0x00ffffffu | (static_cast<uint32_t>(LowByte(static_cast<int32_t>(alpha))) << 24);
}

// 0x3b66f0 (ret 0xc): the depth (water minus terrain) at the tile's four grid vertices, in the order (x, y),
// (x, y + 1), (x + 1, y + 1), (x + 1, y).
void cWaterGeometryBuilder::TileDepths(int32_t x, int32_t y, float *out)
{
    const int32_t y2 = 2 * y;
    const int32_t at[4] = {mStride * x + y2, y2 + 2 + mStride * x, y2 + 2 + mStride * (x + 1), mStride * (x + 1) + y2};
    for (int c = 0; c < 4; ++c) out[c] = mPositions[at[c]].z - GridHeight(mGrid, at[c]);
}

// 0x3b67e0 (ret 0xc): the bilinear blend of the four depths (TileDepths' order) at (u, v) in the tile.
float cWaterGeometryBuilder::Bilerp(const float *corners, float u, float v)
{
    return (v * corners[1] + (1.0f - v) * corners[0]) * (1.0f - u) + ((1.0f - v) * corners[3] + v * corners[2]) * u;
}

// 0x3b6650 (ret 8): the distance from the point to the nearest of the points (FLT_MAX's root for none).
float cWaterGeometryBuilder::NearestDistance(const Vec2 *point, const std::vector<Vec2> *points)
{
    float nearest = FLT_MAX;
    for (const Vec2 &p : *points) {
        float d = (point->y - p.y) * (point->y - p.y) + (point->x - p.x) * (point->x - p.x);
        if (nearest <= d) d = nearest;
        nearest = d;
    }
    return static_cast<float>(std::sqrt(static_cast<double>(nearest)));
}

// 0x3b6840 (ret 8): the grid vertices in the rectangle (signed, inclusive) whose water is above the terrain.
void cWaterGeometryBuilder::CollectWetCorners(const Rect *rect, std::vector<Vec2> *out)
{
    for (int32_t x = rect->xmin; x <= rect->xmax; ++x) {
        for (int32_t y = rect->ymin; y <= rect->ymax; ++y) {
            const int32_t i = mStride * x + 2 * y;
            if (GridHeight(mGrid, i) < mPositions[i].z) out->push_back(Vec2{static_cast<float>(x), static_cast<float>(y)});
        }
    }
}

// 0x3b6940 (ret 8): whether the depth at one of the tile's grid vertices moved more than 0.01 from the stored one.
bool cWaterGeometryBuilder::TileDepthChanged(uint32_t x, int32_t y)
{
    const int32_t base = mStride * static_cast<int32_t>(x) + 2 * y;
    for (int c = 0; c < 4; ++c) {
        const int32_t i = mTileCorners[c] + base;
        const float depth = mPositions[i].z - GridHeight(mGrid, i);
        if (std::fabs(depth - mDepths[i]) > kDepthChange) return true;
    }
    return false;
}

// 0x3b6550 (ret 8): grown by `by` on every side, clamped to the tile bounds (signed).
void cWaterGeometryBuilder::GrowClampRect(Rect *rect, int32_t by)
{
    rect->xmin -= by;
    rect->xmax += by;
    rect->ymin -= by;
    rect->ymax += by;
    if (rect->xmin < mBounds.xmin) rect->xmin = mBounds.xmin;
    if (mBounds.xmax < rect->xmax) rect->xmax = mBounds.xmax;
    if (rect->ymin < mBounds.ymin) rect->ymin = mBounds.ymin;
    if (mBounds.ymax < rect->ymax) rect->ymax = mBounds.ymax;
}

// 0x3b65b0 (ret 8): the same, the high sides clamped to the grid size (the last grid vertex) instead.
void cWaterGeometryBuilder::GrowClampRectToSize(Rect *rect, int32_t by)
{
    rect->xmin -= by;
    rect->xmax += by;
    rect->ymin -= by;
    rect->ymax += by;
    if (rect->xmin < mBounds.xmin) rect->xmin = mBounds.xmin;
    if (mSizeX < rect->xmax) rect->xmax = mSizeX;
    if (rect->ymin < mBounds.ymin) rect->ymin = mBounds.ymin;
    if (mSizeY < rect->ymax) rect->ymax = mSizeY;
}

// 0x3b7080 (ret 0xc, the coordinate by value): dry unless the water is above the terrain at one of its corners.
bool cWaterGeometryBuilder::IsTriangleDry(TriangleCoord coord)
{
    const int32_t base = mStride * static_cast<int32_t>(coord.x) + 2 * static_cast<int32_t>(coord.y);
    for (int c = 0; c < 3; ++c) {
        const int32_t i = mCornerTable[coord.k * 3 + c] + base;
        if (GridHeight(mGrid, i) < mPositions[i].z) return false;
    }
    return true;
}

// 0x3b7110 (ret 8): the dirty rectangle grown to the vertex (signed), or started at it.
void cWaterGeometryBuilder::GrowDirtyRect(int32_t x, int32_t y)
{
    if (!mDirty) {
        mDirtyRect = Rect{x, y, x, y};
        mDirty = 1;
        return;
    }
    if (x < mDirtyRect.xmin) mDirtyRect.xmin = x;
    if (mDirtyRect.xmax < x) mDirtyRect.xmax = x;
    if (y < mDirtyRect.ymin) mDirtyRect.ymin = y;
    if (mDirtyRect.ymax < y) mDirtyRect.ymax = y;
}

// 0x3b7190.
void cWaterGeometryBuilder::ClearSubsetsChanged() { mSubsetsChanged = 0; }

// 0x3b9140 (ret 0xc): a grid vertex moved (nothing when its z is unchanged): the tiles around it re-centred and, unless
// a rebuild is pending, their triangles queued for the collision refit.
void cWaterGeometryBuilder::SetVertexHeight(uint32_t x, uint32_t y, float z)
{
    const int32_t index = mStride * static_cast<int32_t>(x) + 2 * static_cast<int32_t>(y);
    if (!(mPositions[index].z != z)) return;
    SetVertexZ(index, z);
    GrowDirtyRect(static_cast<int32_t>(x), static_cast<int32_t>(y));
    const uint32_t x0 = static_cast<int32_t>(x) < 1 ? x : x - 1;
    const uint32_t x1 = x >= mWidth ? x - 1 : x;
    const uint32_t y0 = static_cast<int32_t>(y) < 1 ? y : y - 1;
    const uint32_t y1 = y >= mHeight ? y - 1 : y;
    if (x0 > x1) return;
    for (uint32_t tx = x0; tx <= x1; ++tx)
        for (uint32_t ty = y0; ty <= y1; ++ty) UpdateCentreVertex(static_cast<int32_t>(tx), static_cast<int32_t>(ty));
    for (uint32_t tx = x0; tx <= x1; ++tx) {
        for (uint32_t ty = y0; ty <= y1; ++ty) {
            for (uint32_t k = 0; k < 4; ++k) {
                const uint32_t triangle = k + (mHeight * tx + ty) * 4;
                if (!mRebuildAll) mChangedTriangles.insert(triangle);
            }
        }
    }
}

// 0x3b71e0 (ret 8): the tile's texels of the three layers reset to 0xff, then blended by the depth interpolated at
// each texel: the shallow seabed ramps in above its lowest depth and fades out towards its highest, the deep seabed
// ramps in from the fade start, the beach ramps up to its peak and down after it; above the water (a negative depth)
// the beach shows only near a wet grid vertex (within the search radius, collected once per tile), fading with the
// distance.
void cWaterGeometryBuilder::PaintTile(int32_t x, int32_t y)
{
    std::vector<Vec2> wet;
    bool collected = false;
    float depths[4];
    TileDepths(x, y, depths);
    const int32_t n = static_cast<int32_t>(mPaintSubdivisions);
    auto *const painter = As<ifaces::TerrainPainter>(mPainter);
    float v = 0.0f;
    for (int32_t py = n * y; py < n * y + n; ++py) {
        float u = 0.0f;
        for (int32_t px = n * x; px < n * x + n; ++px) {
            painter->SetLayerValue(mShallowLayer, px, py, 0xff);
            painter->SetLayerValue(mDeepLayer, px, py, 0xff);
            painter->SetLayerValue(mBeachLayer, px, py, 0xff);
            const float d = Bilerp(depths, u, v);
            if (kShallowMin < d) {
                if (d < kShallowMax) {
                    const uint8_t alpha = kFadeStart <= d
                                              ? static_cast<uint8_t>(~LowByte(static_cast<int32_t>((d - kFadeStart) * kShallowFade)))
                                              : LowByte(static_cast<int32_t>(d * kShallowRamp));
                    if (alpha != 0) painter->BlendLayer(mShallowLayer, px, py, alpha, 0);
                }
                if (kFadeStart < d) {
                    uint8_t alpha = 0xff;
                    if (d < kDeepFull) alpha = LowByte(static_cast<int32_t>((d - kFadeStart) * kDeepRamp));
                    if (alpha != 0) painter->BlendLayer(mDeepLayer, px, py, alpha, 0);
                }
            }
            if (kBeachMin < d && d < kBeachMax) {
                if (!collected) {
                    Rect around{x, y, x + 1, y + 1};
                    GrowClampRectToSize(&around, kWetRadius);
                    CollectWetCorners(&around, &wet);
                    collected = true;
                }
                const float level = kBeachPeak < d ? 255.0f - (d - kBeachPeak) * kBeachDown : (d - kBeachMin) * kBeachUp;
                if (0.0f <= d) {
                    const uint8_t alpha = LowByte(static_cast<int32_t>(level));
                    if (alpha != 0) painter->BlendLayer(mBeachLayer, px, py, alpha, 0);
                } else if (!wet.empty()) {
                    const Vec2 point{u + static_cast<float>(x), v + static_cast<float>(y)};
                    const float distance = NearestDistance(&point, &wet);
                    if (distance < kWetLimit) {
                        const float faded = (1.0f - distance * kWetScale) * level * kWetAlpha;
                        const uint8_t alpha = faded < 255.0f ? LowByte(static_cast<int32_t>(faded)) : 0xff;
                        if (alpha != 0) painter->BlendLayer(mBeachLayer, px, py, alpha, 0);
                    }
                }
            }
            u += mPaintStep;
        }
        v += mPaintStep;
    }
}

// 0x3b7850: after vertices moved, the tiles around the dirty rectangle whose depth changed repainted (unless Setup ran
// since the last Update) and, on the lot (cTerrain +0x68..+0x74, unsigned), their triangles' subsets kept: a dry one
// leaves its subset, a wet one without a subset is handed to cTerrain (0x297ad0, which names the lot's subset and calls
// SetTriangleSubset); then the dirty rectangle's depths and colours, and the painted region redrawn (cTerrain slot 13).
void cWaterGeometryBuilder::Update()
{
    if (mDirty) {
        Rect grown = mDirtyRect;
        mDirty = 0;
        GrowClampRect(&grown, 1);
        SGLocalString name;
        // 0x298710 (cTerrain, ret 4): the base name of the lot subsets, "waterOnTerrain".
        OriginalFunction<void(__thiscall *)(void *, cRZString *), 0x298710>()(mTerrain, &name.value);
        Rect painted{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
        const uint32_t *const lot = reinterpret_cast<const uint32_t *>(reinterpret_cast<uint8_t *>(mTerrain) + 0x68);
        for (uint32_t x = static_cast<uint32_t>(grown.xmin); x <= static_cast<uint32_t>(grown.xmax); ++x) {
            for (uint32_t y = static_cast<uint32_t>(grown.ymin); y <= static_cast<uint32_t>(grown.ymax); ++y) {
                if (!TileDepthChanged(x, static_cast<int32_t>(y))) continue;
                if (!mSkipPaint) {
                    PaintTile(static_cast<int32_t>(x), static_cast<int32_t>(y));
                    const int32_t sx = static_cast<int32_t>(x), sy = static_cast<int32_t>(y);
                    if (sx < painted.xmin) painted.xmin = sx;
                    if (painted.xmax < sx) painted.xmax = sx;
                    if (sy < painted.ymin) painted.ymin = sy;
                    if (painted.ymax < sy) painted.ymax = sy;
                }
                if (x < lot[0] || y < lot[1] || x > lot[2] || y > lot[3]) continue;
                for (uint32_t k = 0; k < 4; ++k) {
                    const TriangleCoord coord{x, y, k};
                    const uint8_t current = SubsetOfTriangle(&coord);
                    if (IsTriangleDry(coord)) {
                        if (current != kNoSubset) RemoveTriangleFromSubset(current, (mHeight * x + y) * 4 + k);
                    } else if (current == kNoSubset) {
                        // 0x297ad0 (cTerrain, ret 8): the lot's subset name for the triangle, then SetTriangleSubset.
                        OriginalFunction<void(__thiscall *)(void *, const TriangleCoord *, const char *), 0x297ad0>()(
                            mTerrain, &coord, name.Text());
                    }
                }
            }
        }
        for (int32_t x = mDirtyRect.xmin; x <= mDirtyRect.xmax; ++x) {
            const uint32_t last = static_cast<uint32_t>(CentreIndex(x, mDirtyRect.ymax));
            for (uint32_t i = static_cast<uint32_t>(mStride * x + 2 * mDirtyRect.ymin); i <= last; ++i)
                SetDepth(static_cast<int32_t>(i), mPositions[i].z - GridHeight(mGrid, static_cast<int32_t>(i)));
        }
        if (painted.xmin <= painted.xmax && painted.ymin <= painted.ymax) {
            const int32_t tiles[4] = {painted.xmin, painted.ymin, std::min(painted.xmax + 1, mSizeX),
                                      std::min(painted.ymax + 1, mSizeY)};
            const int32_t n = static_cast<int32_t>(mPaintSubdivisions);
            const int32_t texels[4] = {n * tiles[0], n * tiles[1], n * tiles[2], n * tiles[3]};
            As<ifaces::cITerrain>(mTerrain)->RedrawRegion(tiles, texels);
        }
    }
    mSkipPaint = 0;
}

// Slot 16 (0x3b75b4, the body at 0x3b75c0): the collision tree rebuilt or refitted, then the four vertex elements of
// one linkage, one triangle-list group per subset with triangles, and the collision shape as the bounding mesh.
void cWaterGeometryBuilder::Build(nGZSceneGraph::cIGeometryDataContainer *container)
{
    if (mCollisionEnabled) {
        if (mRebuildAll) {
            TriangleSetOf(mShape)->Build();
            mRebuildAll = 0;
        } else if (!mChangedTriangles.empty()) {
            // The argument is unused by the refit; the original passes the set.
            TriangleSetOf(mShape)->Refit(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mChangedTriangles)));
            mChangedTriangles.clear();
        }
    }
    const uint32_t positions = static_cast<uint32_t>(mPositions.size());
    container->AddElement(kPositionIdentity, 0, 2, 0);
    container->SetElementData(0, positions, mPositions.empty() ? nullptr : mPositions.data(), positions * 12, nullptr, 0);
    const uint32_t normals = static_cast<uint32_t>(mNormals.size());
    container->AddElement(kNormalIdentity, 0, 2, 1);
    container->SetElementData(1, normals, mNormals.empty() ? nullptr : mNormals.data(), normals * 12, nullptr, 0);
    const uint32_t colours = static_cast<uint32_t>(mColours.size());
    container->AddElement(kColourIdentity, 0, 4, 2);
    container->SetElementData(2, colours, mColours.empty() ? nullptr : mColours.data(), colours * 4, nullptr, 0);
    const uint32_t texCoords = static_cast<uint32_t>(mTexCoords.size());
    container->AddElement(kTexCoordIdentity, 0, 1, 2);
    container->SetElementData(3, texCoords, mTexCoords.empty() ? nullptr : mTexCoords.data(), texCoords * 8, nullptr, 0);
    container->AddLinkage();
    container->SetLinkageReferencedSize(0, positions);
    for (uint16_t element = 0; element < 4; ++element) container->AddLinkageElement(0, element);
    uint32_t group = 0;
    for (uint32_t i = 0; i < mSubsets.size(); ++i) {
        cSubset *const subset = mSubsets[i];
        const uint32_t bytes = static_cast<uint32_t>(subset->mIndices.size() * sizeof(uint16_t));
        if (static_cast<int32_t>(bytes & ~1u) <= 0) continue;
        container->AddGroup(kTriangleList, subset->mName.Text(), 0, -1);
        container->SetGroupVertexIndices(group++, subset->mIndices.data(), bytes >> 1);
    }
    container->SetBoundingMesh(mShape);
}

// 0x3b9060 (ret 8): the triangle moved to the subset of the name (none when dry), adding the subset if new.
void cWaterGeometryBuilder::SetTriangleSubset(const TriangleCoord *coord, const char *name)
{
    const uint8_t wanted = IsTriangleDry(*coord) ? kNoSubset : FindOrAddSubset(name);
    const uint8_t current = SubsetOfTriangle(coord);
    if (wanted == current) return;
    if (current != kNoSubset) RemoveTriangleFromSubset(current, (mHeight * coord->x + coord->y) * 4 + coord->k);
    if (wanted != kNoSubset) AddTriangleToSubset(wanted, coord);
}

// 0x3b6510 (ret 4): the triangle taken out of its subset, if any.
void cWaterGeometryBuilder::RemoveTriangle(const TriangleCoord *coord)
{
    const uint8_t current = SubsetOfTriangle(coord);
    if (current != kNoSubset) RemoveTriangleFromSubset(current, coord->k + (mHeight * coord->x + coord->y) * 4);
}

// 0x3b93d0 (ret 4): the index of the subset of the name (case blind), a new subset when there is none.
uint8_t cWaterGeometryBuilder::FindOrAddSubset(const char *name)
{
    const std::string key(name);
    const auto found = mSubsetIndex.find(key);
    if (found != mSubsetIndex.end()) return static_cast<uint8_t>(found->second);
    const uint32_t index = static_cast<uint32_t>(mSubsets.size());
    AddSubset(name);
    return static_cast<uint8_t>(index);
}

// 0x3b5fb0 (ret 4): the name mapped to the next index (an existing entry overwritten), a new empty subset appended.
void cWaterGeometryBuilder::AddSubset(const char *name)
{
    const uint32_t index = static_cast<uint32_t>(mSubsets.size());
    mSubsetIndex[std::string(name)] = index;
    cSubset *const subset = new cSubset;
    subset->AddRef();
    subset->mName.value.mString.assign(name);
    mSubsets.push_back(subset);
    subset->AddRef();
    mSubsetsChanged = 1;
    subset->Release();
}

// 0x3b6240 (ret 8): the triangle's number inserted in the subset's sorted list and its three corners at the same place.
void cWaterGeometryBuilder::AddTriangleToSubset(uint8_t subset, const TriangleCoord *coord)
{
    cSubset *const target = mSubsets[subset];
    const uint32_t triangle = coord->k + (mHeight * coord->x + coord->y) * 4;
    const auto at = std::lower_bound(target->mTriangles.begin(), target->mTriangles.end(), triangle);
    const size_t position = static_cast<size_t>(at - target->mTriangles.begin());
    target->mTriangles.insert(at, triangle);
    for (int32_t c = 0; c < 3; ++c)
        target->mIndices.insert(target->mIndices.begin() + position * 3 + c, static_cast<uint16_t>(CornerIndex(coord, c)));
    mTriangleSubsets[triangle] = subset;
    mSubsetsChanged = 1;
}

// 0x3b8d80 (ret 8): the triangle and its three corners taken out of the subset, if it is there.
void cWaterGeometryBuilder::RemoveTriangleFromSubset(uint8_t subset, uint32_t triangle)
{
    cSubset *const target = mSubsets[subset];
    const auto at = std::find(target->mTriangles.begin(), target->mTriangles.end(), triangle);
    if (at == target->mTriangles.end()) return;
    const size_t position = static_cast<size_t>(at - target->mTriangles.begin());
    target->mTriangles.erase(at);
    const auto first = target->mIndices.begin() + position * 3;
    target->mIndices.erase(first, first + 3);
    mTriangleSubsets[triangle] = kNoSubset;
    mSubsetsChanged = 1;
}

} // namespace nTSSG
