#pragma once
// The nTSSG lights (lane 261108n, scenegraph LG7a): the game's own lights on the standard lights (sg/cStandardLight.h,
// sg/cPointLight.h) and on the shade level cLightT<cILight, 4000> (sg/cLightLevels.h), which the lerp light set
// (sg/cLerpLightSet.cpp) and the lighting manager make. Whole classes: constructors, destructors and their own slots.
//
//   class cAmbientFromLightMapLight   : public cAmbientLight       0x3c7d30, 0x7c bytes  (ambient lit by the light map)
//   class cHeightMapShadowLight       : public cDirectionalLight   0x3c7fd0, 0x80 bytes  (sun dimmed by the height map)
//   class cHeightMapBinaryShadowLight : public cDirectionalLight   0x3c7e80, 0xb0 bytes  (sun on or off by the height map)
//   class cHalfspacePointLight        : public cPointLight         0x3c7de0, 0xa0 bytes  (point light cut by a plane)
//   class cShadeLightBase             : public cShadeLightLevel    (the game's cLightT<cILight, 4000>: no fields)
//   class cShadeLight                 : public cShadeLightBase     0x3c80b0, 100 bytes   (type 8)
//   class cTerrainShadeLight          : public cShadeLightBase     0x3c8120, 0x58 bytes  (type 0xb)
//
// Each keeps its base's faces (+0 the cSGResource part, +0x2c the node face, +0x34 the persist face, +0x38 the referent
// node's Impl), adds its fields after the base's and ends with the vtordisp and the virtual base cISGResource; MSVC gives
// that layout from the declarations (static_asserts in the .cpp).
//
// The switch: the six constructors are hooked to our compiler's own (thiscall, the most-derived flag last, as the game's;
// the /alternatename names in the .cpp, like cStandardLight.cpp's), so every one of these lights is ours, whoever makes it
// (the lerp light set calls ours directly; the lighting manager's 0x2ccf10 / 0x2cfd60 and 0x2d5430 through the hooks). The
// game's vtables of the six classes are stored by nothing that runs: their destructors, deleting destructors, own slot
// bodies and class-specific thunks are in effect through those callers.
//
// GetParams' second argument (slot 50, `unused` in cIStandardLight) is the cShaderApplyItem being lit: the light-map
// ambient and the height-map shadow read its draw record (+0x2c) and bones (+0xc, +0x14); the other lights ignore it.
#include "sg/cLightLevels.h"
#include "sg/cPointLight.h"
#include "sg/cStandardLight.h"

#include <cstddef>
#include <cstdint>

class cIGZUnknown;
namespace nTSWorld {
struct WorldTile;
}

namespace nTSSG {

using nGZSceneGraph::cINodeCloneContext;

// 0x3c9bd0 (cdecl, returns out): the world tile under `pos` ({x, y, z}): x and y floored, the segment of the tile the
// point lies in (8, 4, 2 or 1 from the fractions), and the highest level from the world's top down whose floor covers that
// segment and whose elevation is below z + 0.01 (level 0 when none; the levels stop above 0). The world is the placement
// service 0x20e0b2's (its slots 89, 50, 62).
nTSWorld::WorldTile *__cdecl TileUnder(nTSWorld::WorldTile *out, const float *pos);

// The ambient light lit by the light map at the item: colour * (1 - weight) + the light map's colour at the item's tile *
// (weight * scale).
class cAmbientFromLightMapLight : public nGZSceneGraph::cAmbientLight {
public:
    static constexpr size_t kSize = 0x7c;
    static constexpr float kStrength = 1000.0f; // 0xda4568: slots 38 and 39

    // 0x3c7d30 (hooked, ret 0x10): the light map (counted), the weight and the scale.
    cAmbientFromLightMapLight(cIGZUnknown *lightMap, float weight, float scale);
    ~cAmbientFromLightMapLight() override; // deleting 0x3c8260 (primary 12), 0x3c820a (node face 58): the light map released

    float StrengthOverBox(const float *, AffineTransform3 *) override { return kStrength; } // 38 (shared 0x3c9080)
    float StrengthAt(const float *) override { return kStrength; }                    // 39 (shared 0x3c9090)
    // 50 (0x3c8a00, ret 8): the item's position (its draw record's object, slot 54), clamped into the world (the world
    // service's cached DB, when it has one and the point is outside), its tile (TileUnder), the light map's colour at
    // {x, y, the tile's level as an integer word} (its slot 16), mixed with the colour; then five zeros.
    void GetParams(float *out, uint32_t item) override;

    cIGZUnknown *mLightMap; // +0x68 (nTSSG::cLightMapManager)
    float mWeight;          // +0x6c
    float mScale;           // +0x70
};

// The sun dimmed by the height map: GetParams scales the colour by the lighting manager's sun visibility at the item.
class cHeightMapShadowLight : public nGZSceneGraph::cDirectionalLight {
public:
    static constexpr size_t kSize = 0x80;

    // 0x3c7fd0 (hooked, ret 0xc): the lighting manager (counted), a value kept unread, and the bits of the render groups
    // "NoHMShadow" and "Portals" (registered if new).
    cHeightMapShadowLight(cIGZUnknown *manager, float value);
    ~cHeightMapShadowLight() override; // deleting 0x3c8360, 0x3c8222: the manager released

    // 38 (0x3c9630, ret 8) and 39 (0x3c96b0, ret 4): |colour| * intensity * 0.25, recomputed.
    float StrengthOverBox(const float *box, AffineTransform3 *toLight) override;
    float StrengthAt(const float *offset) override;
    // 50 (0x3c8cc0, ret 8): colour * intensity * the visibility, the factor, then four zeros. The visibility (1 at first)
    // is the manager's slot 32 at: the object's placement (a portal); else, for an object answering its slot 48 (0x17), the
    // top centre of its bone set's box (0.1 above); else (unless it is in NoHMShadow) the centre of its record's box.
    void GetParams(float *out, uint32_t item) override;

    cIGZUnknown *mManager;     // +0x68 (nTSSG::cLightingManager)
    float mValue;              // +0x6c (no body of the class reads it)
    uint32_t mNoShadowGroups;  // +0x70: 1 << the bit of "NoHMShadow"
    uint32_t mPortalGroups;    // +0x74: 1 << the bit of "Portals"
};

// The sun on or off: the strength is |colour| * intensity * 0.25 unless the sun is hidden (visibility at most the
// threshold) at the point (39) or at both grown corners of the box (38), taken into the sun's frame.
class cHeightMapBinaryShadowLight : public nGZSceneGraph::cDirectionalLight {
public:
    static constexpr size_t kSize = 0xb0;

    // 0x3c7e80 (hooked, ret 0x14): the lighting manager (counted), the frame (the rotation from -{0, 0, 1} to
    // `direction`: word 1, no offset, 0x19d130's matrix of the quaternion at scale 1), the margin and the threshold.
    cHeightMapBinaryShadowLight(cIGZUnknown *manager, const float *direction, float margin, float threshold);
    ~cHeightMapBinaryShadowLight() override; // deleting 0x3c82f0, 0x3c821a: the manager released

    float StrengthOverBox(const float *box, AffineTransform3 *toLight) override; // 38 (0x3c93b0, ret 8)
    float StrengthAt(const float *offset) override;                               // 39 (0x3c9570, ret 4)

    float Strength(); // |colour| * intensity * 0.25

    cIGZUnknown *mManager;       // +0x68 (nTSSG::cLightingManager)
    AffineTransform3 mFrame;     // +0x6c
    float mMargin;               // +0xa0: the box's corners pushed out by it
    float mThreshold;            // +0xa4
};

// The point light cut by a plane: nothing behind it.
class cHalfspacePointLight : public nGZSceneGraph::cPointLight {
public:
    static constexpr size_t kSize = 0xa0;

    // 0x3c7de0 (hooked, ret 0xc): the plane through `point` with `normal` (distance -dot87(normal, point)).
    cHalfspacePointLight(const float *normal, const float *point);
    ~cHalfspacePointLight() override = default; // deleting 0x3c82c0, 0x3c8212

    // 39 (0x3c9360, ret 4): 0 when -distance > dot87(offset, normal) (behind the plane), else the point light's.
    float StrengthAt(const float *offset) override;

    float mNormal[3]; // +0x88
    float mDistance;  // +0x94
};

// The game's cLightT<cILight, 4000> (its constructor 0x3c7cb0 is the level's, hooked in cLightLevels.cpp): a light with
// no standard face and no bound whose strength is 4000 everywhere.
class cShadeLightBase : public nGZSceneGraph::cShadeLightLevel {
public:
    static constexpr float kStrength = 4000.0f; // the template's argument (0xdbfb30)

    cShadeLightBase() = default;
    ~cShadeLightBase() override = default; // 0x3c81c0; deleting 0x3c8240, 0x3c8202 (node face 40)

    nGZSceneGraph::cIStandardLight *GetStandardLightConst() override { return nullptr; } // 34 (shared 0x6c6b0)
    nGZSceneGraph::cIStandardLight *GetStandardLight() override { return nullptr; }      // 35 (shared 0x6c6b0)
    void *GetBoundShape() override { return nullptr; }                                   // 37 (shared 0x6c6b0)
    float StrengthOverBox(const float *, AffineTransform3 *) override { return kStrength; } // 38 (0x3c9060)
    float StrengthAt(const float *) override { return kStrength; }                    // 39 (0x3c9070)
    const void *GetRecord19() override { return ""; } // resource 19 (shared 0x3a5990: the image's empty text 0xd9b9d0)
};

class cShadeLight : public cShadeLightBase {
public:
    static constexpr size_t kSize = 100;
    static constexpr uint32_t kType = 8;

    cShadeLight(const float *values); // 0x3c80b0 (hooked, ret 8): the four words (the lerp light set's {1, r, g, b})
    ~cShadeLight() override = default; // deleting 0x3c83d0, 0x3c822a

    uint32_t GetLightType() override { return kType; } // 36 (shared 0x1559c0)
    // 15 (0x3c84e0, ret 0x10): a new shade light of the same words, the words again, and the light's copy (0x3a57a0).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    float mValues[4]; // +0x4c
};

class cTerrainShadeLight : public cShadeLightBase {
public:
    static constexpr size_t kSize = 0x58;
    static constexpr uint32_t kType = 0xb;

    cTerrainShadeLight(cIGZUnknown *manager); // 0x3c8120 (hooked, ret 8): the lighting manager, counted
    ~cTerrainShadeLight() override;           // deleting 0x3c83f0, 0x3c8232: the manager released

    uint32_t GetLightType() override { return kType; } // 36
    // 15 (0x3c8560, ret 0x10): a new terrain shade light of the same manager, and the light's copy (0x3a57a0).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    cIGZUnknown *mManager; // +0x4c (nTSSG::cLightingManager)
};

} // namespace nTSSG

// Our compiler's constructors as hook targets (thiscall, the most-derived flag last). The /alternatename names are in
// nTSSGLights.cpp.
extern "C" const uint8_t cAmbientFromLightMapLight_Constructor;
extern "C" const uint8_t cHeightMapShadowLight_Constructor;
extern "C" const uint8_t cHeightMapBinaryShadowLight_Constructor;
extern "C" const uint8_t cHalfspacePointLight_Constructor;
extern "C" const uint8_t cShadeLight_Constructor;
extern "C" const uint8_t cTerrainShadeLight_Constructor;
extern "C" const int nTSSGLights_Anchor;
#pragma comment(linker, "/include:_nTSSGLights_Anchor")
