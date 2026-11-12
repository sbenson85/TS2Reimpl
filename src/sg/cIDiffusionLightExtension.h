#pragma once
// nTSSG::cIDiffusionLightExtension (interface id 0x8a561e23; class id 0xaa561e2e): the face of a light node's diffusion
// extension (cDiffusionLightExtension, src/sg/cDiffusionLightExtension.h; the game's vtable for this face 0xdb6bdc). A
// virtual cIExtension: its own 31 slots at +0, a vbptr at +4. The setters (1-10) set a value and its flag, the defaults
// (11-20) the value used while the flag is clear, the getters (21-30) answer the value when set, else the default.
// Every setter is ret 4 (tools/arity.py --rets). Lane 261108v (cLightSettings::Apply is the first caller in our code).
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nTSSG {

class cIDiffusionLightExtension : public virtual nGZSceneGraph::cIExtension {
public:
    static constexpr uint32_t kIid = 0x8a561e23;
    static constexpr uint32_t kClassId = 0xaa561e2e;

    virtual void ResetFlags() = 0;                                  // 0
    virtual void SetSourceMin(const float *xy) = 0;                 // 1
    virtual void SetSourceMax(const float *xy) = 0;                 // 2
    virtual void SetSourceIntensity(float value) = 0;               // 3
    virtual void SetSourceColor(const float *rgb) = 0;              // 4
    virtual void SetSourceIsDirectional(bool value) = 0;            // 5
    virtual void SetSourceFalloff(float value) = 0;                 // 6
    virtual void SetSourceDirectionalFalloff(float value) = 0;      // 7
    virtual void SetSourceWallIntensityScale(float value) = 0;      // 8
    virtual void SetSourceFloorIntensityScale(float value) = 0;     // 9
    virtual void SetSourceInterFloorFalloff(float value) = 0;       // 10
    virtual void SetDefaultSourceMin(const float *xy) = 0;          // 11
    virtual void SetDefaultSourceMax(const float *xy) = 0;          // 12
    virtual void SetDefaultSourceIntensity(float value) = 0;        // 13
    virtual void SetDefaultSourceColor(const float *rgb) = 0;       // 14
    virtual void SetDefaultSourceIsDirectional(bool value) = 0;     // 15
    virtual void SetDefaultSourceFalloff(float value) = 0;          // 16
    virtual void SetDefaultSourceDirectionalFalloff(float value) = 0; // 17
    virtual void SetDefaultSourceWallIntensityScale(float value) = 0; // 18
    virtual void SetDefaultSourceFloorIntensityScale(float value) = 0; // 19
    virtual void SetDefaultSourceInterFloorFalloff(float value) = 0; // 20
    virtual const float *GetSourceMin() = 0;                        // 21
    virtual const float *GetSourceMax() = 0;                        // 22
    virtual float GetSourceIntensity() = 0;                         // 23
    virtual const float *GetSourceColor() = 0;                      // 24
    virtual bool GetSourceIsDirectional() = 0;                      // 25
    virtual float GetSourceFalloff() = 0;                           // 26
    virtual float GetSourceDirectionalFalloff() = 0;                // 27
    virtual float GetSourceWallIntensityScale() = 0;                // 28
    virtual float GetSourceFloorIntensityScale() = 0;               // 29
    virtual float GetSourceInterFloorFalloff() = 0;                 // 30
};

} // namespace nTSSG
