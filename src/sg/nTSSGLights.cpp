// The nTSSG lights (lane 261108n). See nTSSGLights.h.
#include "sg/nTSSGLights.h"

#include "compgeom/ConvexShapes.h" // ConvexBox
#include "compgeom/Rigid.h"        // Dot87
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "misc/Quaternion.h"
#include "misc/UtilitiesC.h"
#include "sg/SGLightShapes.h" // LightColorLength
#include "sg/cRenderGroupNameIterator.h"
#include "world/WorldService.h"
#include "world/cWorldDB.h"

#include <bit>
#include <cstring>

// The decorated names of the constructors (`??0<class>@QAE@<arguments>@Z`, the most-derived flag hidden): the linker
// fails if one is wrong.
#pragma comment(linker, "/alternatename:_cAmbientFromLightMapLight_Constructor=??0cAmbientFromLightMapLight@nTSSG@@QAE@PAVcIGZUnknown@@MM@Z")
#pragma comment(linker, "/alternatename:_cHeightMapShadowLight_Constructor=??0cHeightMapShadowLight@nTSSG@@QAE@PAVcIGZUnknown@@M@Z")
#pragma comment(linker, "/alternatename:_cHeightMapBinaryShadowLight_Constructor=??0cHeightMapBinaryShadowLight@nTSSG@@QAE@PAVcIGZUnknown@@PBMMM@Z")
#pragma comment(linker, "/alternatename:_cHalfspacePointLight_Constructor=??0cHalfspacePointLight@nTSSG@@QAE@PBM0@Z")
#pragma comment(linker, "/alternatename:_cShadeLight_Constructor=??0cShadeLight@nTSSG@@QAE@PBM@Z")
#pragma comment(linker, "/alternatename:_cTerrainShadeLight_Constructor=??0cTerrainShadeLight@nTSSG@@QAE@PAVcIGZUnknown@@@Z")

extern "C" const int nTSSGLights_Anchor = 1;

namespace nTSSG {

static_assert(sizeof(cAmbientFromLightMapLight) == cAmbientFromLightMapLight::kSize, "cAmbientFromLightMapLight size");
static_assert(offsetof(cAmbientFromLightMapLight, mLightMap) == 0x68, "cAmbientFromLightMapLight light map");
static_assert(offsetof(cAmbientFromLightMapLight, mWeight) == 0x6c, "cAmbientFromLightMapLight weight");
static_assert(offsetof(cAmbientFromLightMapLight, mScale) == 0x70, "cAmbientFromLightMapLight scale");
static_assert(sizeof(cHeightMapShadowLight) == cHeightMapShadowLight::kSize, "cHeightMapShadowLight size");
static_assert(offsetof(cHeightMapShadowLight, mManager) == 0x68, "cHeightMapShadowLight manager");
static_assert(offsetof(cHeightMapShadowLight, mValue) == 0x6c, "cHeightMapShadowLight value");
static_assert(offsetof(cHeightMapShadowLight, mNoShadowGroups) == 0x70, "cHeightMapShadowLight NoHMShadow");
static_assert(offsetof(cHeightMapShadowLight, mPortalGroups) == 0x74, "cHeightMapShadowLight Portals");
static_assert(sizeof(cHeightMapBinaryShadowLight) == cHeightMapBinaryShadowLight::kSize, "binary shadow size");
static_assert(offsetof(cHeightMapBinaryShadowLight, mManager) == 0x68, "binary shadow manager");
static_assert(offsetof(cHeightMapBinaryShadowLight, mFrame) == 0x6c, "binary shadow frame");
static_assert(offsetof(cHeightMapBinaryShadowLight, mMargin) == 0xa0, "binary shadow margin");
static_assert(offsetof(cHeightMapBinaryShadowLight, mThreshold) == 0xa4, "binary shadow threshold");
static_assert(sizeof(cHalfspacePointLight) == cHalfspacePointLight::kSize, "cHalfspacePointLight size");
static_assert(offsetof(cHalfspacePointLight, mNormal) == 0x88, "cHalfspacePointLight normal");
static_assert(offsetof(cHalfspacePointLight, mDistance) == 0x94, "cHalfspacePointLight distance");
static_assert(sizeof(cShadeLight) == cShadeLight::kSize, "cShadeLight size");
static_assert(offsetof(cShadeLight, mValues) == 0x4c, "cShadeLight values");
static_assert(sizeof(cTerrainShadeLight) == cTerrainShadeLight::kSize, "cTerrainShadeLight size");
static_assert(offsetof(cTerrainShadeLight, mManager) == 0x4c, "cTerrainShadeLight manager");

namespace {

using nGZCompGeom::RigidTransform3;

// The original's floor (round to nearest of v - 0.5 by the 1.5 * 2^23 bias, then one up when that lands on v exactly).
int32_t FloorOf(float v)
{
    const float biased = (v - 0.5f) + 12582912.0f;
    const int32_t r = std::bit_cast<int32_t>(biased) - 0x4b400000;
    return float(r + 1) == v ? r + 1 : r;
}

// The lighting manager's sun visibility at a world point.
float SunVisibility(cIGZUnknown *manager, const float *point)
{
    return RawSlot<32, float>(manager, point); // receiver: nTSSG::cLightingManager vf32 0x2e31d0
}

// The item's draw record (+0x2c): the object at +0, its render groups at +4, its box at +0x44.
const uint8_t *RecordOf(uint32_t item) { return *reinterpret_cast<const uint8_t *const *>(item + 0x2c); }
void *ObjectOf(const uint8_t *record) { return *reinterpret_cast<void *const *>(record); }
uint32_t GroupsOf(const uint8_t *record) { return *reinterpret_cast<const uint32_t *>(record + 4); }

// The object's placement (slot 54, into `scratch`).
const RigidTransform3 *PlacementOf(void *object, void *scratch)
{
    return RawSlot<54, const RigidTransform3 *>(object, scratch); // receiver: nGZSceneGraph::cICompositionTreeNode (the draw record's renderable node; GetWorldTransform into scratch, ret 4: 0xa4bca0 / 0xa37b80 / 0xa3a730 / 0xa6ace0 / 0xa6ad20 / 0xa6ad80)
}

// The centre of a box carried by a rigid transform (each axis (max - min) * 0.5 + min).
nGZCompGeom::BoundBox *Carried(nGZCompGeom::ConvexBox *box, const RigidTransform3 *placement)
{
    return box->TransformRigid(placement);
}

} // namespace

nTSWorld::WorldTile *__cdecl TileUnder(nTSWorld::WorldTile *out, const float *pos)
{
    out->x = -1;
    out->y = -1;
    out->level = 0;
    out->segments = static_cast<int8_t>(0xff);
    out->x = FloorOf(pos[0]);
    out->y = FloorOf(pos[1]);
    const float fx = pos[0] - float(out->x);
    const float fy = pos[1] - float(out->y);
    const float rest = 1.0f - fx;
    if (fx > fy) out->segments = rest > fy ? 8 : 4;
    else out->segments = rest > fy ? 1 : 2;

    auto *const db = static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()());
    out->level = db->GetMaxLevel();
    while (out->level > 0) {
        bool found = false;
        if (db->HasFloor(out, out->segments, true)) {
            // The original leaves the copy's segment word unset (GetElevation reads only the cell and level).
            nTSWorld::WorldTile tile{};
            tile.x = out->x;
            tile.y = out->y;
            tile.level = out->level;
            const double top = double(pos[2]) + 0.01;
            found = top > double(db->GetElevation(&tile));
        }
        if (found) break;
        --out->level;
    }
    return out;
}

// ---- cAmbientFromLightMapLight ----

cAmbientFromLightMapLight::cAmbientFromLightMapLight(cIGZUnknown *lightMap, float weight, float scale)
{
    mLightMap = lightMap;
    if (lightMap) lightMap->AddRef();
    mWeight = weight;
    mScale = scale;
}

cAmbientFromLightMapLight::~cAmbientFromLightMapLight()
{
    if (mLightMap) mLightMap->Release();
}

void cAmbientFromLightMapLight::GetParams(float *out, uint32_t item)
{
    uint8_t scratch[0x40];
    const float *const placement =
        reinterpret_cast<const float *>(PlacementOf(ObjectOf(RecordOf(item)), scratch));
    float x = placement[0];
    float y = placement[1];
    const float z = placement[2];

    nTSWorld::cIWorldDB *const db = worldsvc::CachedWorldDB();
    if (db && !db->IsInWorld(x, y)) {
        const float maxX = db->GetMaxXF();
        if (0.0f > x) x = 0.0f;
        if (x > maxX) x = maxX;
        const float maxY = db->GetMaxYF();
        if (0.0f > y) y = 0.0f;
        if (y > maxY) y = maxY;
    }

    const float pos[3] = {x, y, z};
    nTSWorld::WorldTile tile;
    TileUnder(&tile, pos);

    // The light map's query: x, y, and the tile's level as an integer word.
    uint32_t query[3];
    std::memcpy(&query[0], &x, 4);
    std::memcpy(&query[1], &y, 4);
    query[2] = static_cast<uint32_t>(tile.level);
    float colour[3];
    const float *const lit = RawSlot<16, const float *>(mLightMap, colour, query); // receiver: nTSSG::cLightMapManager vf16 0x2c76a0

    const float mapWeight = mWeight * mScale;
    const float ownWeight = 1.0f - mWeight;
    out[0] = mColor[0] * ownWeight + lit[0] * mapWeight;
    out[1] = mColor[1] * ownWeight + lit[1] * mapWeight;
    out[2] = mColor[2] * ownWeight + lit[2] * mapWeight;
    out[3] = 0.0f;
    out[4] = 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

// ---- cHeightMapShadowLight ----

cHeightMapShadowLight::cHeightMapShadowLight(cIGZUnknown *manager, float value)
{
    mManager = manager;
    if (manager) manager->AddRef();
    mValue = value;
    mNoShadowGroups = 1u << (nGZSceneGraph::RenderGroupRegistry()->BitOfName("NoHMShadow") & 31);
    mPortalGroups = 1u << (nGZSceneGraph::RenderGroupRegistry()->BitOfName("Portals") & 31);
}

cHeightMapShadowLight::~cHeightMapShadowLight()
{
    if (mManager) mManager->Release();
}

float cHeightMapShadowLight::StrengthOverBox(const float *, AffineTransform3 *)
{
    return nGZSceneGraph::LightColorLength(mColor) * mIntensity * kStrengthScale;
}

float cHeightMapShadowLight::StrengthAt(const float *)
{
    return nGZSceneGraph::LightColorLength(mColor) * mIntensity * kStrengthScale;
}

void cHeightMapShadowLight::GetParams(float *out, uint32_t item)
{
    float visibility = 1.0f;
    const uint8_t *const record = RecordOf(item);
    void *const object = ObjectOf(record);
    uint8_t scratch[0x40];
    if (GroupsOf(record) & mPortalGroups) {
        visibility = SunVisibility(mManager, reinterpret_cast<const float *>(PlacementOf(object, scratch)));
    } else if (RawSlot<48, bool>(object, 0x17u)) { // receiver: nGZSceneGraph::cICompositionTreeNode (TestFlag(0x17), ret 4: 0xa38fd0 / 0xa6cc60)
        const RigidTransform3 *const placement = PlacementOf(object, scratch);
        void *const bones = *reinterpret_cast<void *const *>(item + 0xc);
        const uint32_t boneSet = *reinterpret_cast<const uint16_t *>(item + 0x14);
        const uint8_t *const set = RawSlot<5, const uint8_t *>(bones, boneSet); // receiver: nGZSceneGraph::cIGeometryRender (the item's +0xc; GetDrawInfo(subset), a GeometrySubset, ret 4: 0xa42970 / 0xa441a0 / 0x3af590 / 0x3af4a0)
        nGZCompGeom::ConvexBox box;
        std::memcpy(&box, set + 0x1c, sizeof(box));
        const nGZCompGeom::BoundBox *const r = Carried(&box, placement);
        const float top[3] = {(r->max.x - r->min.x) * 0.5f + r->min.x, (r->max.y - r->min.y) * 0.5f + r->min.y,
                              r->max.z + 0.1f};
        visibility = SunVisibility(mManager, top);
    } else if (!(GroupsOf(record) & mNoShadowGroups)) {
        const RigidTransform3 *const placement = PlacementOf(object, scratch);
        nGZCompGeom::ConvexBox box;
        std::memcpy(&box, record + 0x44, sizeof(box));
        const nGZCompGeom::BoundBox *const r = Carried(&box, placement);
        const float centre[3] = {(r->max.x - r->min.x) * 0.5f + r->min.x, (r->max.y - r->min.y) * 0.5f + r->min.y,
                                 (r->max.z - r->min.z) * 0.5f + r->min.z};
        visibility = SunVisibility(mManager, centre);
    }

    const float scale = visibility * mIntensity;
    out[0] = mColor[0] * scale;
    out[1] = mColor[1] * scale;
    out[2] = mColor[2] * scale;
    out[3] = mFactor;
    out[4] = 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

// ---- cHeightMapBinaryShadowLight ----

cHeightMapBinaryShadowLight::cHeightMapBinaryShadowLight(cIGZUnknown *manager, const float *direction, float margin,
                                                         float threshold)
{
    mManager = manager;
    if (manager) manager->AddRef();
    static constexpr float kUp[3] = {0.0f, 0.0f, 1.0f}; // 0x100ee90
    const float from[3] = {-kUp[0], -kUp[1], -kUp[2]};
    math::Quaternion rotation;
    const math::Quaternion *const q = rotation.SetRotationBetween(from, direction);
    mFrame.mUnknown0 = 1;
    mFrame.mOffset[0] = 0.0f;
    mFrame.mOffset[1] = 0.0f;
    mFrame.mOffset[2] = 0.0f;
    OriginalFunction<void(__thiscall *)(AffineTransform3 *, const math::Quaternion *, float), 0x19d130>()(&mFrame, q,
                                                                                                         1.0f);
    mMargin = margin;
    mThreshold = threshold;
}

cHeightMapBinaryShadowLight::~cHeightMapBinaryShadowLight()
{
    if (mManager) mManager->Release();
}

float cHeightMapBinaryShadowLight::Strength()
{
    return nGZSceneGraph::LightColorLength(mColor) * mIntensity * kStrengthScale;
}

float cHeightMapBinaryShadowLight::StrengthOverBox(const float *box, AffineTransform3 *toLight)
{
    float t[3], v[3], w[3];
    toLight->TransformPoint(t, box);
    const float low = mMargin;
    v[0] = t[0] - low;
    v[1] = t[1] - low;
    v[2] = t[2] - low;
    mFrame.TransformPoint(w, v);
    const float first = SunVisibility(mManager, w);

    toLight->TransformPoint(t, box + 3);
    const float high = -mMargin;
    v[0] = t[0] - high;
    v[1] = t[1] - high;
    v[2] = t[2] - high;
    mFrame.TransformPoint(w, v);
    const float second = SunVisibility(mManager, w);

    if (mThreshold >= first && mThreshold >= second) return 0.0f;
    return Strength();
}

float cHeightMapBinaryShadowLight::StrengthAt(const float *offset)
{
    float w[3];
    mFrame.TransformPoint(w, offset);
    if (mThreshold >= SunVisibility(mManager, w)) return 0.0f;
    return Strength();
}

// ---- cHalfspacePointLight ----

cHalfspacePointLight::cHalfspacePointLight(const float *normal, const float *point)
{
    mNormal[0] = normal[0];
    mNormal[1] = normal[1];
    mNormal[2] = normal[2];
    mDistance = -float(nGZCompGeom::Dot87(normal, point));
}

float cHalfspacePointLight::StrengthAt(const float *offset)
{
    const float along = float(nGZCompGeom::Dot87(offset, mNormal));
    if (-mDistance > along) return 0.0f;
    return cPointLight::StrengthAt(offset);
}

// ---- cShadeLight, cTerrainShadeLight ----

cShadeLight::cShadeLight(const float *values)
{
    mValues[0] = values[0];
    mValues[1] = values[1];
    mValues[2] = values[2];
    mValues[3] = values[3];
}

// ORIGINAL-BUG: iid and out are ignored: the clone is never viewed as `iid` or handed out, so it leaks at count 0 and
// *out keeps whatever it held.
void cShadeLight::Clone(uint32_t mode, uint32_t, void **, cINodeCloneContext *)
{
    cShadeLight *const clone = new cShadeLight(mValues);
    std::memcpy(clone->mValues, mValues, sizeof(mValues));
    mImpl.CopyTo(this, clone, mode, nullptr);
}

cTerrainShadeLight::cTerrainShadeLight(cIGZUnknown *manager)
{
    mManager = manager;
    if (manager) manager->AddRef();
}

cTerrainShadeLight::~cTerrainShadeLight()
{
    if (mManager) mManager->Release();
}

// ORIGINAL-BUG: as cShadeLight::Clone, iid and out are ignored (the clone leaks, *out is not written).
void cTerrainShadeLight::Clone(uint32_t mode, uint32_t, void **, cINodeCloneContext *)
{
    cTerrainShadeLight *const clone = new cTerrainShadeLight(mManager);
    mImpl.CopyTo(this, clone, mode, nullptr);
}

} // namespace nTSSG
