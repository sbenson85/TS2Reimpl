#pragma once
// nTSSG::cDiffusionLightExtension (lane 261109c, scenegraph LG7e; its flat slot bodies first by an earlier lane): the
// extension a light node keeps for the diffusion lighting pass: a light source's rectangle (min, max), intensity, colour,
// directional flag and five falloff / scale factors, each with a default and an optional override. Class id 0xaa561e2e
// (the COM director's factory 0x2ea3b0), interface cIDiffusionLightExtension 0x8a561e23 (src/sg/cIDiffusionLightExtension.h);
// the extension's type name (cIExtension slot 25) is "light".
//
// The game's object (0xbc bytes; constructor 0x2ea080) is cExtension's part (src/sg/cExtension.h), then:
//   +0x18 the defaults: min float[2] (-0.5, -0.5), +0x20 max float[2] (0.5, 0.5), +0x28 intensity (0), +0x2c colour
//         float[3] (1, 1, 1), +0x38 directional bool (false), +0x3c falloff (1), +0x40 directional falloff (0), +0x44 wall
//         intensity scale (1), +0x48 floor intensity scale (1), +0x4c inter-floor falloff (0.5)
//   +0x50 the overrides, each a value and a flag (false when made; the values are left uninitialised): min +0x50 / +0x58,
//         max +0x5c / +0x64, intensity +0x68 / +0x6c, colour +0x70 / +0x7c, directional +0x7d (false when made) / +0x7e,
//         falloff +0x80 / +0x84, directional falloff +0x88 / +0x8c, wall scale +0x90 / +0x94, floor scale +0x98 / +0x9c,
//         inter-floor falloff +0xa0 / +0xa4
//   +0xa8 the vtordisp, +0xac the virtual base cIExtension (vtable 0xdb6b6c)
//   +0xb0 the vtordisp, +0xb4 the virtual base cIDiffusionLightExtension (vtable 0xdb6bdc, 31 slots; its vbptr at +0xb8)
// Only the defaults are persisted (Read 0x2ea630 / Write 0x2eaab0); the overrides are what cLightSettings sets per light.
//
// Ours is our compiler's class with the same bases (cBoneDataExtension's shape). It is a leaf: the factory 0x2ea3b0 is
// hooked and nothing else is. The constructor 0x2ea080 has one caller, the factory, so the factory hook is the constructor's
// switch too; every other body is reached only through the vtables our objects replace, or from the constructors (status
// entries "in effect through callers"). The face's default setters 13 and 16-20 are folded bodies (shared_vf_*) the game
// shares with unrelated classes: ours are overrides in our vtables only.
#include "sg/cExtension.h"
#include "sg/cIDiffusionLightExtension.h"

#include <cstdint>

namespace nTSSG {

// C4250 ("inherits ... via dominance"): intended, as cBoneDataExtension's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cDiffusionLightExtension final : public nGZSceneGraph::cExtension, public virtual cIDiffusionLightExtension {
public:
    using cIExtension = nGZSceneGraph::cIExtension;

    static constexpr uint32_t kClassId = cIDiffusionLightExtension::kClassId;
    static constexpr uint32_t kVersion = 1; // the type-info record's version (0x2ea9e0); Read checks none

    // 0x2ea3b0 (__cdecl): the COM director's factory for kClassId: a new extension (count 0), as its cIExtension.
    static cIGZUnknown *__cdecl Create();

    cDiffusionLightExtension();                                         // 0x2ea080
    cDiffusionLightExtension(const cDiffusionLightExtension &other);    // 0x2e9eb0: every value and flag copied
    cDiffusionLightExtension &operator=(const cDiffusionLightExtension &) = delete;
    ~cDiffusionLightExtension() override;                               // 0x2ea1d0 (deleting 0x2ea220)

    // ---- the referent face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0x2ea5d0: kIid, else cExtension's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0x2ea630
    cPersistTypeInfo GetTypeInfo() override;                // 0x2ea9e0: {"cDiffusionLightExtension", kClassId, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0x2eaab0

    // ---- cIExtension ----
    cIExtension *Clone() override;                          // 15: 0x2ea2d0
    bool IsKindOf(uint32_t iid) override;                   // 23: 0x2ea9b0
    const char *GetTypeName() override;                     // 25: 0x2ea380 (folded with a cLightCommand's), "light"

    // ---- cIDiffusionLightExtension (the game's bodies: the thunk + body RVAs in the .size.cpp) ----
    void ResetFlags() override;
    void SetSourceMin(const float *xy) override;
    void SetSourceMax(const float *xy) override;
    void SetSourceIntensity(float value) override;
    void SetSourceColor(const float *rgb) override;
    void SetSourceIsDirectional(bool value) override;
    void SetSourceFalloff(float value) override;
    void SetSourceDirectionalFalloff(float value) override;
    void SetSourceWallIntensityScale(float value) override;
    void SetSourceFloorIntensityScale(float value) override;
    void SetSourceInterFloorFalloff(float value) override;
    void SetDefaultSourceMin(const float *xy) override;
    void SetDefaultSourceMax(const float *xy) override;
    void SetDefaultSourceIntensity(float value) override;
    void SetDefaultSourceColor(const float *rgb) override;
    void SetDefaultSourceIsDirectional(bool value) override;
    void SetDefaultSourceFalloff(float value) override;
    void SetDefaultSourceDirectionalFalloff(float value) override;
    void SetDefaultSourceWallIntensityScale(float value) override;
    void SetDefaultSourceFloorIntensityScale(float value) override;
    void SetDefaultSourceInterFloorFalloff(float value) override;
    const float *GetSourceMin() override;
    const float *GetSourceMax() override;
    float GetSourceIntensity() override;
    const float *GetSourceColor() override;
    bool GetSourceIsDirectional() override;
    float GetSourceFalloff() override;
    float GetSourceDirectionalFalloff() override;
    float GetSourceWallIntensityScale() override;
    float GetSourceFloorIntensityScale() override;
    float GetSourceInterFloorFalloff() override;

private:
    friend struct cDiffusionLightExtensionLayout;

    // The defaults (persisted).
    float mDefaultMin[2] = {-0.5f, -0.5f};          // +0x18
    float mDefaultMax[2] = {0.5f, 0.5f};            // +0x20
    float mDefaultIntensity = 0.0f;                 // +0x28
    float mDefaultColor[3] = {1.0f, 1.0f, 1.0f};    // +0x2c
    bool mDefaultIsDirectional = false;             // +0x38
    float mDefaultFalloff = 1.0f;                   // +0x3c
    float mDefaultDirectionalFalloff = 0.0f;        // +0x40
    float mDefaultWallIntensityScale = 1.0f;        // +0x44
    float mDefaultFloorIntensityScale = 1.0f;       // +0x48
    float mDefaultInterFloorFalloff = 0.5f;         // +0x4c
    // The overrides: a value (uninitialised until its setter runs, as the game's) and its flag.
    float mMin[2];                                  // +0x50
    bool mHasMin = false;                           // +0x58
    float mMax[2];                                  // +0x5c
    bool mHasMax = false;                           // +0x64
    float mIntensity;                               // +0x68
    bool mHasIntensity = false;                     // +0x6c
    float mColor[3];                                // +0x70
    bool mHasColor = false;                         // +0x7c
    bool mIsDirectional = false;                    // +0x7d (the constructor's word store at +0x7c clears it)
    bool mHasIsDirectional = false;                 // +0x7e
    float mFalloff;                                 // +0x80
    bool mHasFalloff = false;                       // +0x84
    float mDirectionalFalloff;                      // +0x88
    bool mHasDirectionalFalloff = false;            // +0x8c
    float mWallIntensityScale;                      // +0x90
    bool mHasWallIntensityScale = false;            // +0x94
    float mFloorIntensityScale;                     // +0x98
    bool mHasFloorIntensityScale = false;           // +0x9c
    float mInterFloorFalloff;                       // +0xa0
    bool mHasInterFloorFalloff = false;             // +0xa4
};
#pragma warning(pop)

} // namespace nTSSG
