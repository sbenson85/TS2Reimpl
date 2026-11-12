#pragma once
// nTSSG::cLightSettings (lane 261108v, scenegraph row LG7b): the lighting script's named light settings. The lighting
// parser state (cLightingParserState, its +0x404 word) makes one (0x2da270) and keeps it; the lightAttributes commands
// (src/sg/LightAttributeCommands.h) feed it one attribute at a time (slot 7) or copy a base block's (slot 8), and the
// lighting applies them to a node tree (slot 9): each light node whose tag names settings gets the light values set on
// its standard light, and the diffusion values on its diffusion extension (found, or made through the COM director).
// RVAs 1.18.0.187; all /O2.
//
// Layout (0x48, operator new; RTTI order): cILightSettings +0 (vtable 0xdbefa0, 11 slots), rz::cRZUnknown +4 (0xdbefd0,
// 6 slots; the count at +8), the light values by settings key +0xc, the diffusion values +0x14, the name (a cRZString)
// +0x1c, the attribute names (a NameValueTable of the 17 names below) +0x3c.
//
// A settings key is "name:tag" (the settings' own name, a colon, the node's tag) or the tag alone: lookups try the long
// form first. Keys compare case-insensitively (_stricmp).
//
// Standalone shape (docs/standalone-shape.md): the constructor 0x3c6a30 is hooked to ours (its only caller is 0x2da270),
// so every light settings object carries our vtables; the slots and the destructor are in effect through it.
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "std/StdInstances.h" // NameValueTable

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nTSSG {

// One attribute's value (0x28 bytes): its id (the name's value in the table) and what the attribute's kind fills.
// The original builds the record on the stack with only +8..+0x1f cleared, so the words a kind does not write (the value
// of a colour or a range, the range of anything else) are stack leftovers there; nothing reads them. Ours clears all.
struct LightSettingValue {
    uint32_t id;      // +0
    union {           // +4
        float number; // the float kinds
        uint8_t flag; // diffusionSourceIsDirectional (EvalBool's byte)
    };
    float vector3[3]; // +8 color (EvalVector3, at least 1 component)
    float rgb[3];     // +0x14 diffusionSourceColor (EvalColorRGB)
    float range[2];   // +0x20 diffusionSourceMin / Max (EvalVector2, at least 1 component)
};
static_assert(sizeof(LightSettingValue) == 0x28 && offsetof(LightSettingValue, vector3) == 8 &&
                  offsetof(LightSettingValue, rgb) == 0x14 && offsetof(LightSettingValue, range) == 0x20,
              "LightSettingValue layout");

// The attribute ids (the table at 0xdbed50 pairs these with the names).
namespace lightsetting {
constexpr uint32_t kIntensity = 0xe9d24d51;
constexpr uint32_t kRelSpecIntensity = 0x69de427a;
constexpr uint32_t kColor = 0x29de3049;
constexpr uint32_t kFalloffRate = 0x49e37821;
constexpr uint32_t kRange = 0x0a5622e9;
constexpr uint32_t kConeAngle = 0x29de5328;
constexpr uint32_t kConePenumbra = 0x49de5331;
constexpr uint32_t kSourceMin = 0x8a561f18;
constexpr uint32_t kSourceMax = 0x8a561f1e;
constexpr uint32_t kSourceIntensity = 0x8a561f27;
constexpr uint32_t kSourceColor = 0xaa561f2f;
constexpr uint32_t kSourceIsDirectional = 0xaa561f38;
constexpr uint32_t kSourceFalloff = 0x2a689d83;
constexpr uint32_t kSourceDirectionalFalloff = 0x0a611e4f;
constexpr uint32_t kSourceWallIntensityScale = 0xca6915b9;
constexpr uint32_t kSourceFloorIntensityScale = 0x0a6e450d;
constexpr uint32_t kSourceInterFloorFalloff = 0xea70f36d;
} // namespace lightsetting

// nTSSG::cILightSettings (interface id 0x0b902b18; 11 slots).
class cILightSettings : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x0b902b18;
    virtual bool Init() = 0;                       // 3 (shared 0x144910: true)
    virtual bool Shutdown() = 0;                   // 4 (shared 0x144910: true)
    virtual void Clear() = 0;                      // 5
    virtual std::string &SetName(const char *name) = 0; // 6: the name string (the original tail-calls its assign)
    // 7: one attribute of the settings `key` (the parser face evaluates `value`).
    virtual void ParseAttribute(void *parser, const char *key, const char *attribute, const char *value) = 0;
    virtual void CopySettings(const char *from, const char *to) = 0; // 8: `from`'s values appended to `to`'s
    virtual void Apply(nGZSceneGraph::cICompositionTreeNode *node) = 0; // 9: the tree under node
    virtual const char *GetName() = 0;             // 10
};

class cLightSettings : public cILightSettings, public rz::cRZUnknown {
public:
    // The settings keys' order (the original's 0x2eb8c0 lower bound and 0x3c6cc0 operator[]: library instantiations).
    struct NoCaseLess {
        bool operator()(const std::string &a, const std::string &b) const;
    };
    using Values = std::vector<LightSettingValue>;
    using ValueMap = std::map<std::string, Values, NoCaseLess>;

    cLightSettings();           // 0x3c6a30
    ~cLightSettings() override; // 0x3c6b40; deleting 0x3c6de0, its +4 thunk 0x3c6dd0 (the cRZUnknown face's slot 3)
    static cLightSettings *__fastcall Construct(void *memory, void *); // the tests' way to the constructor

    bool QueryInterface(uint32_t riid, void **out) override; // 0x3c7c70 (+4 thunk 0x3c7c97): kIid here, else cRZUnknown's
    uint32_t AddRef() override;
    uint32_t Release() override;

    bool Init() override;
    bool Shutdown() override;
    void Clear() override;                                                       // 0x3c7540
    std::string &SetName(const char *name) override;                             // 0x3c7ca0 (folded with cTSTreeConstant's)
    void ParseAttribute(void *parser, const char *key, const char *attribute, const char *value) override; // 0x3c6ec0
    void CopySettings(const char *from, const char *to) override;                // 0x3c75a0
    void Apply(nGZSceneGraph::cICompositionTreeNode *node) override;              // 0x3c7180
    const char *GetName() override;                                              // 0x3c7590

    // Helpers (thiscall in the original).
    void AddLightValue(const char *key, const LightSettingValue &value);     // 0x3c70c0 (ret 8)
    void AddDiffusionValue(const char *key, const LightSettingValue &value); // 0x3c6e00 (ret 8)
    const Values &FindLightValues(const char *tag) const;     // 0x3c79c0 (ret 4): "name:tag", else "tag", else none
    const Values &FindDiffusionValues(const char *tag) const; // 0x3c7710 (ret 4)

    ValueMap mLight;     // +0xc
    ValueMap mDiffusion; // +0x14
    union {
        cRZString mName; // +0x1c: built and destroyed by hand (Construct / Destruct)
    };
    union {
        NameValueTable mNames; // +0x3c: built by Construct (it builds the vector in place), destroyed by hand
    };
};
static_assert(offsetof(cLightSettings, mRefCount) == 8 && offsetof(cLightSettings, mLight) == 0xc &&
                  offsetof(cLightSettings, mDiffusion) == 0x14 && offsetof(cLightSettings, mName) == 0x1c &&
                  offsetof(cLightSettings, mNames) == 0x3c,
              "cLightSettings layout");
static_assert(sizeof(cLightSettings) == 0x48, "cLightSettings size");

} // namespace nTSSG

// Our compiler's constructor as the hook target (thiscall, no arguments). The /alternatename is in the .cpp, which the
// anchor pulls in from the library.
extern "C" const uint8_t cLightSettings_Constructor;
extern "C" const int cLightSettings_Anchor;
#pragma comment(linker, "/include:_cLightSettings_Anchor")
