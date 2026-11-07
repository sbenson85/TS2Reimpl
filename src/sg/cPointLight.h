#pragma once
// nGZSceneGraph::cPointLight (lane OT, scenegraph N2): the standard light with a falloff and a range, class 0xc9c81ba9.
//
//   class cPointLight : public cStandardLightBase       (sg/cStandardLight.h has the levels below it)
//
//   +0x00..0x63 as the ambient light's (vtables 0xe6a670 (14 slots), 0xe6a6ac (59), 0xe6a79c (7), 0xe6a7bc (25))
//   +0x64 mFalloff (0.4 at first), +0x68 mRange (0), +0x6c mStrength (0)
//   +0x70 the embedded sphere shape (0x18 bytes, the game's vtable 0xda1970; slot 37 hands it out)
//   +0x88 the vtordisp, +0x8c the virtual base cISGResource; 0x90 bytes
//
// The switch: the director's factory 0xa47c90 is hooked to Create, so every point light the director makes is ours. The
// game's constructor 0xa46060 and slot bodies stay live: nTSSG::cHalfspacePointLight (0x3c7de0) derives from this class
// and calls the constructor, so the game's vtable bodies keep running on those objects and are not hooked (status mapped
// for them; the factory's entry lists the bodies ours replace).
#include "sg/SGLightShapes.h"
#include "sg/cStandardLight.h"

namespace nGZSceneGraph {

class cPointLight : public cStandardLightBase {
public:
    static constexpr size_t kSize = 0x90;
    static constexpr uint32_t kClassId = 0xc9c81ba9;
    static constexpr float kInitialFalloff = 0.4f; // 0x3ecccccd
    static constexpr float kStrengthScale = 0.25f; // 0xda03e0

    cPointLight() = default;           // 0xa46060 (flag 1)
    ~cPointLight() override = default; // 0xa46740; deleting 0xa46b40 (primary 12), 0xa4686a (node face 58)

    // 0xa47c90 (cdecl; the director's factory, hooked): a new light's node face, uncounted.
    static cIStandardLight *__cdecl Create();

    // 13 (0xa48330): the range and the strength from the colour, intensity and falloff and the cutoff setting, the
    // sphere's radius, kBoundChangedEvent, then the base's.
    void OnLightChanged() override;
    // 15 (0xa47300, ret 0x10): a new light, CopyContentTo, viewed as `iid` in *out.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    uint32_t GetLightType() override { return kPoint; }  // 36 (shared 0x15e570)
    void *GetBoundShape() override { return &mShape; }   // 37 (shared 0x474200: node face + 0x44)
    // 38 (0x3c90a0, ret 8): the strength at the box's eight corners taken into the light's space, the largest (maxss).
    float StrengthOverBox(const float *box, AffineTransform3 *toLight) override;
    float StrengthAt(const float *offset) override; // 39 (0xa49540, ret 4): strength / (d2 * falloff + 1) inside the range
    void SetFalloff(float value) override;           // 44 (shared 0xa4a170): when it differs, stored and 13
    void SetRange(float value) override;             // 45 (0xa4a2d0): when it differs, stored, the radius, both events
    void Update() override { OnLightChanged(); }     // 46 (shared 0xa47440)
    // 50 (0xa48bc0, ret 8): colour * intensity, the factor, the falloff, the range, then two zeros.
    void GetParams(float *out, uint32_t unused) override;
    float GetFalloff() override { return mFalloff; } // 54 (shared 0x22ab70)
    float GetRange() override { return mRange; }     // 55 (shared 0x4e5170)

    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa48f30: the base's, falloff, range, then 13
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa4aaf0: {"cPointLight", 0xc9c81ba9, 5}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4b0e0: the record, the base's, falloff, range
    const void *GetRecord19() override { return "5pl"; }    // 0xa48190

    void CopyContentTo(cPointLight *target, uint32_t mode); // 0xa47f50 (ret 8): CopyTo, the three fields, the sphere's data

    float mFalloff = kInitialFalloff; // +0x64
    float mRange = 0.0f;              // +0x68
    float mStrength = 0.0f;           // +0x6c
    EmbeddedSphereShape mShape;       // +0x70
};

} // namespace nGZSceneGraph
