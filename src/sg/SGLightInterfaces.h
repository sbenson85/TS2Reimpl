#pragma once
// The scene graph's light interfaces (lane OR, scenegraph N2): a light is an object-graph node (no tree links) that is
// also a scene-graph resource and a referent (other nodes register with it to hear its notifications). Declared in
// slot order from cAmbientLight's node-face vtable (0xe6a334, 59 slots) and the bodies behind it; names past the
// object-graph node's are ours and say what the bodies do (tentative where the slot is a no-op in every light here).
// Slot numbers are vtable slots of the node face (the light's +0x2c).
#include "misc/UtilitiesC.h" // AffineTransform3
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nGZSceneGraph {

class cISGResource; // sg/cSGResource.h

// nGZSceneGraph::cIReferentNode (interface id 0x9c6ab984): an object-graph node others refer to. A referrer (a behaviour,
// typically a node that uses this one) registers itself and then hears every notification the node sends (its slot 3,
// with the node as the sender) and every query it asks (its slot 4).
class cIReferentNode : public cIObjectGraphNode {
public:
    static constexpr uint32_t kIid = 0x9c6ab984;

    virtual void AddReferrer(nGZEvent::cIAbstractBehavior *referrer) = 0;    // 29: at the end of the list
    virtual void RemoveReferrer(nGZEvent::cIAbstractBehavior *referrer) = 0; // 30: it must be in the list
    virtual uint32_t GetReferrerCount() = 0;                                  // 31
    virtual cISGResource *GetResourceConst() = 0;                             // 32: the resource face (null for null)
    virtual cISGResource *GetResource() = 0;                                  // 33
};

// nGZSceneGraph::cILight (interface id 0x9c5ed164): a referent node that lights.
class cIStandardLight;
class cILight : public cIReferentNode {
public:
    static constexpr uint32_t kIid = 0x9c5ed164;
    // Slot 36's answers.
    enum Type : uint32_t { kAmbient = 0, kDirectional = 1, kPoint = 2, kSpot = 3, kEnvironmentCube = 4, kLinearFog = 5, kDrawState = 6 };

    virtual cIStandardLight *GetStandardLightConst() = 0; // 34: the standard light face, or null (the other lights)
    virtual cIStandardLight *GetStandardLight() = 0;      // 35
    virtual uint32_t GetLightType() = 0;                  // 36: Type
    virtual void *GetBoundShape() = 0;                    // 37: the light's bound (point: a sphere, spot: a cone), or null
    // 38 / 39: the light's strength (ambient and directional: the value slot 13 of the resource face computes). 38 takes
    // a box (min xyz, max xyz) and the transform into the light's space (ret 8): the largest strength at the box's eight
    // corners (the spot adds the centre); 39 takes one point in the light's space (ret 4; the spot's 0xa49900,
    // src/sg/cSpotLight.h).
    virtual float StrengthOverBox(const float *box, AffineTransform3 *toLight) = 0;
    virtual float StrengthAt(const float *offset) = 0;
};

// nGZSceneGraph::cIStandardLight (interface id 0xea932e72): the ambient, directional, point and spot lights (and the
// nTSSG lights built on them). A name (a string-pool handle), an intensity, a second factor and a colour; the point and
// spot lights add a falloff, a range and the spot's cone (47 / 48, 56 / 57). Setters tell the light (slot 13 of its
// resource face), which recomputes and notifies event 0x69f75eba.
class cIStandardLight : public cILight {
public:
    static constexpr uint32_t kIid = 0xea932e72;
    static constexpr uint32_t kChangedEvent = 0x69f75eba; // what the light notifies after a change
    static constexpr uint32_t kBoundChangedEvent = 0xdd041f30; // the point / spot notify it first when their bound changes

    // 40 / 49 are not called SetName / GetName: a light is also a cSGResource, whose cISGResource::GetName (slot 17,
    // the resource's own name) a GetName here would override as well (one final overrider for both).
    virtual void SetLightName(const char *name) = 0;        // 40
    virtual void SetColor(const float *rgb) = 0;            // 41: kept unless within 32 ulps of the current colour
    virtual void SetIntensity(float value) = 0;             // 42
    virtual void SetFactor(float value) = 0;                // 43 (the word after the intensity; 1.0 at first)
    virtual void SetFalloff(float value) = 0;               // 44 (point; nothing here)
    virtual void SetRange(float value) = 0;                 // 45 (point / spot; nothing here)
    virtual void Update() = 0;                              // 46 (point: slot 13 of the resource face; nothing here)
    virtual void SetConeAngle(float value) = 0;             // 47 (spot; nothing here)
    virtual void SetConeFalloffAngle(float value) = 0;      // 48 (spot; nothing here)
    virtual const char *GetLightName() = 0;                 // 49: null without a name
    virtual void GetParams(float *out, uint32_t unused) = 0; // 50: 8 floats, per light (ret 8: the second word unread)
    virtual float *GetColor(float *rgb) = 0;                 // 51: returns rgb (the lighting manager reads through it)
    virtual float GetIntensity() = 0;                       // 52
    virtual float GetFactor() = 0;                          // 53
    virtual float GetFalloff() = 0;                         // 54: 0 here
    virtual float GetRange() = 0;                           // 55: 10000 here
    virtual float GetConeAngle() = 0;                       // 56: 0 here
    virtual float GetConeFalloffAngle() = 0;                // 57: 0 here
};

// nGZSceneGraph::cILinearFogLight (interface id 0x0c069ed0, lane OT): the fog light (slot 36's answer 5). Slot 42 hands out
// the near and far distances (two floats, at the light's +0x4c). Neither setter tells anyone.
class cILinearFogLight : public cILight {
public:
    static constexpr uint32_t kIid = 0x0c069ed0;

    virtual void SetNearFar(float nearDistance, float farDistance) = 0; // 40 (ret 8, 0x3cad30)
    virtual void SetColor(const float *rgb) = 0;                         // 41 (0xa4a1a0): 12 bytes
    virtual float *GetNearFar() = 0;                                     // 42 (shared 0x1758b0): the two floats, in place
};

// nGZSceneGraph::cIDrawStateLight (interface id 0x0c069ecb, lane OT; slot 36's answer 6): two flag bytes at the light's
// +0x4c / +0x4d, both 1 at first; what the renderer reads from them is not understood (names tentative).
class cIDrawStateLight : public cILight {
public:
    static constexpr uint32_t kIid = 0x0c069ecb;

    virtual void SetFirstFlag(uint8_t value) = 0;  // 40 (shared 0x46de50): the byte at +0x4c, stored as it is
    virtual void SetSecondFlag(uint8_t value) = 0; // 41 (shared 0x46e260): the byte at +0x4d
    virtual uint8_t *GetFlags() = 0;               // 42 (shared 0x1758b0): the two bytes, in place
};

// nGZSceneGraph::cITextureLight (interface id 0x6a970429, lane TC; slot 36's answer 4): the environment cube light. A
// texture (a counted texture handle at the light's +0x4c, set by name) and four RGBA colours (+0x50, 16 bytes each, all
// 1.0 at first). What the renderer does with the four colours is not understood (names tentative).
class cITextureLight : public cILight {
public:
    static constexpr uint32_t kIid = 0x6a970429;

    virtual void SetTexture(const char *name) = 0;                 // 40 (0xa4a520): the named texture's handle
    virtual void SetColor(uint32_t index, const float *rgba) = 0;  // 41 (0xa4a090, ret 8): 16 bytes, no bounds check
    // 42 (0xa47420, ret 8): the colour by value (MSVC's hidden pointer comes first and is returned).
    virtual float *GetColor(float *rgba, uint32_t index) = 0;
};

} // namespace nGZSceneGraph
