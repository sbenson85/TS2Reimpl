#pragma once
// nTSSG::cLerpLightSet (lane 261108l, scenegraph LG5b): the lighting manager's set of scene-graph lights that fade
// between the light-set values it is given. Each entry holds a light-reference node (cILightRefNode, the director's class
// 0x253d2018) and the light it places (or a texture light, or an existing node of the lot's), the target colour,
// direction and value, and which of those it fades. The lighting manager's builders (0x2cfd60, 0x2d0250, 0x2e1560,
// 0x2ccf10, BuildRenderGroupLights here) mark every entry inactive (ClearActive), add their lights (an entry already there
// for the same group and kind is reused: only its target changes), then let the rest fade out (FadeOutUnused: target
// black); Evaluate (0x2dfc80's) moves each active entry towards its target and drops the entries that went dark.
//
// Standalone shape: a cRZRefCount (the game's vtable 0xdbea94, 5 slots, slot 0 the deleting destructor 0x3c1080), 0x1c
// bytes. The constructor, both destructors, the entry's constructors and destructor and every member are hooked; the
// vector of entries is std::vector (the game's instances 0x3c08b0-0x3c0f30 and 0x3c3700 are library code, as is the bitset
// bound check 0x232e30 in BuildRenderGroupLights).
//
// Game code still called (original_calls): the service getter 0x20defb (slots 48 / 49 give the shade lights' source), the
// light-node registry getter 0x20df51 (slot 7 adds a node, slot 9 removes it). The node's name setter 0xae78f0 and
// direction setter 0xaeee30 are ours (sg/SGUtilities.h). The nTSSG lights it makes (cAmbientFromLightMapLight, cShadeLight,
// cTerrainShadeLight, the two directional shadow lights and cHalfspacePointLight) are ours (sg/nTSSGLights.h, lane
// 261108n): the creators call our constructors at their C names, as the game calls its own (thiscall, flag 1, each
// returns the object; the light is its node face at +0x2c).
#include "rz/RZRefCountBase.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZCompGeom {
struct RigidTransform3;
}
namespace nGZSceneGraph {
class cILightRefNode;
class cIStandardLight;
class cITextureLight;
} // namespace nGZSceneGraph

namespace nTSSG {

struct cLightSet; // sg/cLightingManager.h

class cLerpLightSet : public rz::cRZRefCount {
public:
    static constexpr uint32_t kVTable = 0xdbea94;

    // What an entry holds (the creators' kind argument). Names follow the light each one makes.
    enum Kind : uint32_t {
        kAmbient = 0,             // the director's ambient light (class 0xc9c81b9b)
        kLightMapAmbient = 1,     // cAmbientFromLightMapLight (0x3c7d30)
        kDirectional = 2,         // the director's directional light (class 0xc9c81ba3)
        kShadowDirectional = 3,   // 0x3c7fd0's directional shadow light
        kShadowDirectionalB = 4,  // 0x3c7e80's (a direction and two values; not reused)
        kHalfspacePoint = 5,      // cHalfspacePointLight (0x3c7de0)
        kShade = 6,               // cShadeLight (0x3c80b0; not reused)
        kTerrainShade = 7,        // cTerrainShadeLight (0x3c8120; not reused)
        kEnvironment = 8,         // the director's environment cube light (class 0x6a97042f, cITextureLight)
        kExisting = 9,            // a node the caller owns (its light is driven through the node's attributes)
    };

    // One light (0x38 bytes). The group and the value are left as they were by the default constructor.
    struct Entry {
        uint32_t mKind;                            // +0x00 Kind
        uint32_t mGroup;                           // +0x04 the render-group bits the node draws in
        nGZSceneGraph::cILightRefNode *mNode;      // +0x08 counted
        nGZSceneGraph::cIStandardLight *mLight;    // +0x0c counted (the standard lights' node face)
        nGZSceneGraph::cITextureLight *mTexture;   // +0x10 counted (kEnvironment)
        uint8_t mLerpColor;                        // +0x14 the colour fades (1 at first)
        uint8_t mLerpDirection;                    // +0x15 the direction fades
        uint8_t mLerpValue;                        // +0x16 the value (the falloff, or attribute 0x49e37821) fades
        uint8_t mPad17;
        float mColor[3];                           // +0x18 the target colour (black at first)
        float mDirection[3];                       // +0x24 the target direction ((0, 0, 1) at first)
        float mValue;                              // +0x30 the target value
        uint8_t mActive;                           // +0x34 kept by Evaluate (1 at first)
        uint8_t mReusable;                         // +0x35 a creator may take it over (1 at first)
        uint8_t mPad36[2];

        // 0x3c0e30.
        Entry();
        // 0x3c0d60 (ret 4): each reference counted (slot 1) in field order.
        Entry(const Entry &other);
        // Inlined in Evaluate: per reference, in field order, a different one is counted before the old one is let go.
        Entry &operator=(const Entry &other);
        // 0x3c0fc0: the references let go of (slot 2) in reverse field order.
        ~Entry();

        // 0x3c2e60 (ret 4): one step towards the target; t above 0.95 sets it outright, and a colour reaching black
        // (no texture light) makes the entry inactive.
        void Evaluate(float t);
    };

    cLerpLightSet() = default;
    // 0x3c1030: Clear, then the vector and the count's own.
    ~cLerpLightSet() override;

    // ---- hook adapters (thiscall on the object) ----
    cLerpLightSet *Construct();                         // 0x3c0ec0 (returns this)
    void Destruct();                                    // 0x3c1030
    cLerpLightSet *DeletingDestructor(uint32_t flags);  // 0x3c1080 (slot 0, ret 4): sized delete of 0x1c on bit 0

    // 0x3c2570: every entry inactive.
    void ClearActive();
    // 0x3c25d0: each inactive reusable entry active again with a black target and no direction fade.
    void FadeOutUnused();
    // 0x3c2660 (ret 8): the first inactive reusable entry of the group and kind, or -1.
    int32_t Find(uint32_t group, uint32_t kind);
    // 0x3c26c0 (ret 0xc): Find's test, and the node's local transform within 32 ulps of `transform` (the rotation, or
    // its negation, as a whole), or -1.
    int32_t FindPoint(uint32_t group, uint32_t kind, const nGZCompGeom::RigidTransform3 *transform);
    // 0x3c2cc0: every node the registry holds (not kExisting, and with a parent) removed from it; every entry dropped.
    void Clear();
    // 0x3c2e30 (ret 8): new keys clear the set.
    void SetKeys(uint32_t layerTag, uint32_t lightFlag);
    // 0x3c3450 (ret 4): each active entry evaluated; inactive ones removed from the registry (as Clear) and dropped,
    // the rest moved down in order.
    void Evaluate(float t);

    // ---- creators ----
    // 0x3c10a0 (ret 0x1c): kinds 0, 1, 6, 7, 8; `a6` / `a7` go to kind 1's constructor.
    void AddLight(uint32_t group, const char *name, uint32_t kind, const float *color, float scale, float a6, float a7);
    // 0x3c1600 (ret 0x20): kinds 2, 3, 4 (direction fade on).
    void AddDirectional(uint32_t group, const char *name, uint32_t kind, const float *color, float scale,
                        const float *direction, float a7, float a8);
    // 0x3c1a80 (ret 0x1c): a node of the caller's (kExisting), driven through attributes e9d24d51 / 29de3049 /
    // 49e37821; not added to the registry. `unused` is not read.
    void AddExisting(uint32_t group, const char *name, nGZSceneGraph::cILightRefNode *node, uint32_t unused,
                     const float *color, float scale, float value);
    // 0x3c1d60 (ret 0x20): a half-space point light placed by `transform` (direction and position given in the world,
    // carried into the node's frame); nothing for a scale of 0 or below.
    void AddPoint(uint32_t group, const char *name, const nGZCompGeom::RigidTransform3 *transform,
                  const float *direction, const float *position, const float *color, float scale, float value);

    // 0x3c2d80 (__cdecl): the node's attributes e9d24d51 (1.0), 29de3049 (the colour) and 49e37821 (the value) set.
    static void __cdecl SetLightEvents(nGZSceneGraph::cILightRefNode *node, const float *color, float value);
    // 0x3c2b50 (__cdecl): the same three read, moved `t` of the way to the targets (the first back to 1.0) and set.
    static void __cdecl LerpLightEvents(nGZSceneGraph::cILightRefNode *node, float t, const float *color, float value);
    // 0x3c2120 (__cdecl): a light set's lights for the render groups: the outdoor ambient (bit 7), the seven
    // directional lights (bits 0-6; indoor sets, any of bits 4-6, light the Sims with 4-6 and the rest with 0-3), less
    // the ambient, and the environment cube (bit 9). Light 0 takes `kind0` and `a7`.
    static void __cdecl BuildRenderGroupLights(const cLightSet *set, cLerpLightSet *lerp, uint32_t kind0, float a7);

    uint32_t mLayerTag = 0;      // +0x08 each node's SetLayerTag
    uint32_t mLightFlag = 2;     // +0x0c each node's SetLightFlag (its low byte)
    std::vector<Entry> mEntries; // +0x10
};

static_assert(sizeof(cLerpLightSet::Entry) == 0x38, "Entry size");
static_assert(offsetof(cLerpLightSet::Entry, mColor) == 0x18, "Entry colour");
static_assert(offsetof(cLerpLightSet::Entry, mValue) == 0x30, "Entry value");
static_assert(offsetof(cLerpLightSet::Entry, mActive) == 0x34, "Entry active");
static_assert(sizeof(cLerpLightSet) == 0x1c, "cLerpLightSet size");
static_assert(offsetof(cLerpLightSet, mEntries) == 0x10, "cLerpLightSet entries");

} // namespace nTSSG
