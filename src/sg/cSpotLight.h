#pragma once
// nGZSceneGraph::cSpotLight (lane OT, scenegraph N2): the point light with a cone, class 0xc9c81bad.
//
//   class cSpotLight : public cStandardLightBase
//
//   +0x00..0x63 as the ambient light's (vtables 0xe6a82c (14 slots), 0xe6a868 (59), 0xe6a958 (7), 0xe6a978 (25))
//   +0x64 mFalloff (0.4 at first), +0x68 mRange, +0x6c mConeAngle, +0x70 mConeFalloffAngle, +0x74 mStrength,
//   +0x78 mCosOuter (cos of half the sum of the angles), +0x7c mCosInner (cos of half the cone angle)
//   +0x80 the embedded cone shape (0x1c bytes, the game's vtable 0xda1b00; slot 37 hands it out)
//   +0x9c the vtordisp, +0xa0 the virtual base cISGResource; 0xa4 bytes
//
// The switch: the director's factory 0xa47cf0 is hooked to Create. Unlike the point light nothing in the game derives from
// this class, and its constructor 0xa46140 has no other caller than the factory and the game's Clone 0xa47380, so once the
// factory is ours no spot light has the game's vtables: every body below is in effect through the factory (lane TL folded the
// SGLights group's nine hooked bodies in: 0xa49900, 0xa4a0b0, 0xa4a0e0, 0xa48c40 and the five resource-face thunks).
#include "sg/SGLightShapes.h"
#include "sg/cStandardLight.h"

namespace nGZSceneGraph {

class cSpotLight : public cStandardLightBase {
public:
    static constexpr size_t kSize = 0xa4;
    static constexpr uint32_t kClassId = 0xc9c81bad;
    static constexpr float kInitialFalloff = 0.4f;
    static constexpr float kStrengthScale = 0.25f;
    static constexpr float kMaxAngleSum = 3.1101768f; // 0xe6ba50 (bits 0x40470d23): past it the cone is reset

    cSpotLight() = default;           // 0xa46140 (flag 1)
    ~cSpotLight() override = default; // 0xa46790; deleting 0xa46b70 (primary 12), 0xa46872 (node face 58)

    static cIStandardLight *__cdecl Create(); // 0xa47cf0 (hooked)

    // 13 (0xa48540): the angles made positive (and reset when their sum passes kMaxAngleSum), the range, the two cosines,
    // the strength, the cone, kBoundChangedEvent, then the base's.
    void OnLightChanged() override;
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override; // 15 (0xa47380)
    uint32_t GetLightType() override { return kSpot; }  // 36 (shared 0x18c4d0)
    void *GetBoundShape() override { return &mShape; }  // 37 (shared 0x3ec390: node face + 0x54)
    // 38 (0xa495b0, ret 8): the strength at the box's eight corners and its centre, the largest (maxss).
    float StrengthOverBox(const float *box, AffineTransform3 *toLight) override;
    float StrengthAt(const float *offset) override; // 39 (0xa49900, ret 4; the SGLights group once hooked it)
    void SetFalloff(float value) override;           // 44 (shared 0xa4a170)
    void SetRange(float value) override;             // 45 (0xa4a370)
    void Update() override { OnLightChanged(); }     // 46 (shared 0xa47440)
    void SetConeAngle(float value) override;         // 47 (0xa4a0b0, once hooked in SGLights)
    void SetConeFalloffAngle(float value) override;  // 48 (0xa4a0e0)
    void GetParams(float *out, uint32_t unused) override; // 50 (0xa48c40): as the point light's, then the two angles
    float GetFalloff() override { return mFalloff; }  // 54
    float GetRange() override { return mRange; }      // 55
    float GetConeAngle() override { return mConeAngle; }              // 56 (shared 0x20c680)
    float GetConeFalloffAngle() override { return mConeFalloffAngle; } // 57 (shared 0x1547a0)

    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa48fc0: the base's, falloff, cone, cone falloff, range, 13
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa4abb0: {"cSpotLight", 0xc9c81bad, 5}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4b180
    const void *GetRecord19() override { return "5sl"; }    // 0xa48196

    void CopyContentTo(cSpotLight *target, uint32_t mode);  // 0xa47f90 (ret 8)

    float mFalloff = kInitialFalloff; // +0x64
    float mRange = 0.0f;              // +0x68
    float mConeAngle = 0.0f;          // +0x6c
    float mConeFalloffAngle = 0.0f;   // +0x70
    float mStrength = 0.0f;           // +0x74
    float mCosOuter = 0.0f;           // +0x78
    float mCosInner = 0.0f;           // +0x7c
    EmbeddedConeShape mShape;         // +0x80
};

} // namespace nGZSceneGraph
