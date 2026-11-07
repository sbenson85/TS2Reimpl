#pragma once
// The standard lights (lane OR, scenegraph N2): nGZSceneGraph::cStandardLightBase, the base of the ambient, directional,
// point and spot lights (cIStandardLight), and the two leaves built so far, cAmbientLight (class 0xc9c81b9b) and
// cDirectionalLight (class 0xc9c81ba3). Standalone shape (sg/SGLightTemplates.h has the template levels):
//
//   class cStandardLightBase : public cLightT<cIStandardLight, cStandardLightBehaviorTraits>
//   class cAmbientLight / cDirectionalLight : public cStandardLightBase
//
//   +0x00 the cSGResource part (vtables 0xe6a2f8 ambient / 0xe6a4b4 directional, 14 slots: 13 is OnLightChanged)
//   +0x2c the node face cIStandardLight (0xe6a334 / 0xe6a4f0, 59 slots: 58 the deleting destructor), +0x30 its count,
//   +0x34 its persist face (0xe6a424 / 0xe6a5e0), +0x38 the cReferentNodeImpl
//   +0x4c mIntensity, +0x50 mFactor (1.0 at first), +0x54 mColor[3], +0x60 mLightName (a string-pool handle)
//   +0x64 mStrength (the leaf's: sqrt(colour . colour) * intensity, a quarter of that for the directional light)
//   +0x68 the vtordisp, +0x6c the virtual base cISGResource (0xe6a444 / 0xe6a600, 25 slots); 0x70 bytes
//
// MSVC gives that layout from the declarations (static_asserts in the .cpp).
//
// The switch: the director's factories 0xa47ab0 / 0xa47b10 are hooked to Create, so every ambient and directional light
// the director makes is ours. The nTSSG lights the game derives from these (0x3c7d30's ambient, 0x3c7e80 / 0x3c7fd0's
// directional, and the point light's 0xa46060 on the base) still call the game's constructors as base parts (flag 0):
// those constructors and the destructors are hooked to our compiler's own (lane VW; the /alternatename names in the
// .cpp, like cLightLevels.h), so the game's vtables of these three classes are stored by nothing that runs. The slot
// bodies the nTSSG vtables still hold (they are the game's, so their entries point at the game's bodies) and those the
// game's point light still calls are hooked to cStandardLightAdapters below; the rest are reached only from the dead
// vtables (in effect through their callers). Shared bodies the family rule doesn't let us hook stay the game's: the Read
// 0xa48ce0 (it reads the record and calls the base's Read 0xa49070, hooked).
#include "game/owned.h"
#include "rz/StaticGetters2.h" // ServiceCell
#include "sg/SGLightTemplates.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The event registry of the standard lights (0xa49210, game code: a function-local static map built on first use, which
// registers kChangedEvent).
nGZEvent::cEventAttributeMap *StandardLightEventRegistry();

// The behaviour cells of cAbstractBehaviorTypeT<cIStandardLight, cRefSingleThreaded>.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10da480); // the cell below (game/owned.h)
struct cStandardLightBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10da480; // 0x3c85f0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10da480; }
    static nGZEvent::cEventAttributeMap *Registry() { return StandardLightEventRegistry(); }

    // 0xa49210 (cdecl; src/sg/cStandardLight.cpp): the registry itself. Built on first use (its holder 0xa457f0: the
    // object-graph events 0x2570a0 stores, the shared simple-light map given the standard light's own 0xdd041f30 record,
    // then 0x69f75eba through this function; the map a copy of the shared map, 0xa480c0); a non-zero id stores
    // `attribute`. Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only
};

class cStandardLightBase : public cLightT<cIStandardLight, cStandardLightBehaviorTraits> {
    using Light = cLightT<cIStandardLight, cStandardLightBehaviorTraits>;

public:
    static constexpr uint32_t kVersion = 0xb;          // the type-info record's version
    static constexpr float kRange = 10000.0f;          // slot 55's answer (0xdba8d4)

    cStandardLightBase();               // 0xa46250 (hooked): intensity 0, factor 1, colour 0, no name
    ~cStandardLightBase() override;     // 0xa467e0 (hooked): the name closed, then the light

    // Primary slot 13 (0xa488a0): the light changed; the leaves recompute their strength first. Tells the node's listeners
    // (and its referrers) kChangedEvent with a cNullEvent.
    virtual void OnLightChanged();

    // ---- cIGZUnknown ----
    // 0xa48a70 (ret 8): cIStandardLight is the node face, counted; else the light's (0x3a5a60).
    bool QueryInterface(uint32_t iid, void **out) override;

    // ---- cIObjectGraphNode ----
    bool IsKindOf(uint32_t iid) override; // 0xa4a5d0: cIStandardLight, else the light's (0x3a6460)

    // ---- cILight ----
    cIStandardLight *GetStandardLightConst() override { return this; } // 0xa46c40 (the node face; null for null)
    cIStandardLight *GetStandardLight() override { return this; }      // 0xa46c40
    void *GetBoundShape() override { return nullptr; }                  // shared 0x6c6b0

    // ---- cIStandardLight ----
    void SetLightName(const char *name) override; // 40 (0xa4a1f0): interned (flag 0), then kChangedEvent (no recompute)
    void SetColor(const float *rgb) override;     // 41 (0xa49f40): unless each part is within 32 ulps, stored and 13
    void SetIntensity(float value) override;      // 42 (0xa4a110): when it differs, stored and 13
    void SetFactor(float value) override;         // 43 (0xa4a4c0): when it differs, stored and 13
    void SetFalloff(float) override {}            // 44 (shared 0x300a0)
    void SetRange(float) override {}              // 45 (shared 0x300a0)
    void Update() override {}                     // 46 (shared 0x140fe0)
    void SetConeAngle(float) override {}         // 47 (shared 0x300a0)
    void SetConeFalloffAngle(float) override {}         // 48 (shared 0x300a0)
    const char *GetLightName() override;          // 49 (0xa480a0): null without a name
    float *GetColor(float *rgb) override;          // 51 (0xa47400): returns rgb
    float GetIntensity() override { return mIntensity; } // 52 (shared 0x4db560)
    float GetFactor() override { return mFactor; }       // 53 (shared 0x153f90)
    float GetFalloff() override { return 0.0f; }         // 54 (shared 0x3b58e0)
    float GetRange() override { return kRange; }         // 55 (0xa48cc0)
    float GetConeAngle() override { return 0.0f; }      // 56 (shared 0x3b58e0)
    float GetConeFalloffAngle() override { return 0.0f; }      // 57 (shared 0x3b58e0)

    // ---- cIGZPersistSerializableReferent ----
    // 0xa49070 (ret 4): the record (dropped last), the cSGResource part (0xa3c540), the light (0x3a5c30: so the
    // cSGResource part is read twice, as the game writes it), the name (interned, flag 0), intensity, factor, colour;
    // then slot 13.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa4ac70: {"cStandardLightBase", 0, 0xb}
    // 0xa4b250 (ret 4): the record, the cSGResource part (0xa3ced0), the light (0xa4ad50), the name's text, intensity,
    // factor, colour.
    void Write(ifaces::cIGZPersistWriter *writer) override;

    // 0xa47ff0 (ret 8, this the source): the object-graph Impl's CopyTo (extensions and tag; 0x3a57a0), then intensity,
    // factor, colour and name. The cSGResource part and the referrers are not copied.
    void CopyTo(cStandardLightBase *target, uint32_t mode);

    float mIntensity = 0.0f;            // +0x4c
    float mFactor = 1.0f;               // +0x50
    float mColor[3] = {};               // +0x54
    ServiceCell mLightName{kNoName};    // +0x60
};

class cAmbientLight : public cStandardLightBase {
public:
    static constexpr size_t kSize = 0x70;
    static constexpr uint32_t kClassId = 0xc9c81b9b; // the director's class id, and the type-info record's word

    cAmbientLight();           // 0xa45d40 (hooked): strength 0
    ~cAmbientLight() override; // 0xa465f0 (hooked); deleting 0xa46a90 (primary 12), 0xa46842 (node face 58)
    static constexpr const char *kRecord19 = "5al"; // 0xa48140's record (the text; the game's is in its image)

    // 0xa47ab0 (cdecl; the director's factory, hooked): a new light's node face, uncounted.
    static cIStandardLight *__cdecl Create();

    void OnLightChanged() override; // 0xa48230: strength = |colour| * intensity, then the base's
    // 15 (0xa47080, ret 0x10): a new light (the context is not asked), CopyTo and the strength, viewed as `iid` in
    // *out (the clone's QueryInterface: counted when answered; a clone nobody asked for stays at count 0).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    uint32_t GetLightType() override { return kAmbient; }                           // 36 (shared 0x6c6b0)
    float StrengthOverBox(const float *, AffineTransform3 *) override { return mStrength; } // 38 (shared 0xa49520)
    float StrengthAt(const float *) override { return mStrength; }                    // 39 (shared 0xa49530)
    // 50 (0xa48ac0, ret 8): colour * intensity, then five zeros.
    void GetParams(float *out, uint32_t unused) override;

    // shared 0xa48ce0 (with the directional light): the record (dropped last), then the base's Read.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa4a6d0: {"cAmbientLight", 0xc9c81b9b, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4ade0: the record, then the base's Write
    const void *GetRecord19() override { return kRecord19; } // 0xa48140

    // shared 0xa47eb0 (ret 8, with the directional light): CopyTo, then the strength.
    void CopyContentTo(cAmbientLight *target, uint32_t mode);

    float mStrength = 0.0f; // +0x64
};

class cDirectionalLight : public cStandardLightBase {
public:
    static constexpr size_t kSize = 0x70;
    static constexpr uint32_t kClassId = 0xc9c81ba3;
    static constexpr float kStrengthScale = 0.25f; // 0xda03e0

    cDirectionalLight();           // 0xa45db0 (hooked): strength 0
    ~cDirectionalLight() override; // 0xa46630 (hooked); deleting 0xa46ab0 (primary 12), 0xa4684a (node face 58)
    static constexpr const char *kRecord19 = "5dl"; // 0xa48150's record

    static cIStandardLight *__cdecl Create(); // 0xa47b10 (hooked)

    void OnLightChanged() override; // 0xa482b0: strength = |colour| * intensity * 0.25, then the base's
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override; // 0xa47100
    uint32_t GetLightType() override { return kDirectional; }                      // 36 (shared 0x15e510)
    float StrengthOverBox(const float *, AffineTransform3 *) override { return mStrength; } // 38 (shared 0xa49520)
    float StrengthAt(const float *) override { return mStrength; }                    // 39 (shared 0xa49530)
    // 50 (0xa48b40, ret 8): colour * intensity, the factor, then four zeros.
    void GetParams(float *out, uint32_t unused) override;

    void Read(ifaces::cIGZPersistReader *reader) override;  // shared 0xa48ce0
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa4a790: {"cDirectionalLight", 0xc9c81ba3, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4ae60
    const void *GetRecord19() override { return kRecord19; } // 0xa48150

    void CopyContentTo(cDirectionalLight *target, uint32_t mode); // shared 0xa47eb0 (as the ambient light's)

    float mStrength = 0.0f; // +0x64
};

// The RVA hooks' entry points (src/hook/tables/cStandardLight.cpp, lane VW): the game's bodies the nTSSG lights' vtables
// hold (and those the game's point light calls), each taking the `this` its face delivers (+0 the cSGResource part, +0x2c
// the node face, +0x34 the persist face) and calling our class's body qualified (inlined). The ambient and directional
// leaves are the same layout, so the bodies of one of them serve the nTSSG lights derived from it.
struct cStandardLightAdapters {
    using Self = cStandardLightBase;
    static Self *FromPart(void *face) { return static_cast<Self *>(face); }
    static Self *FromNode(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - 0x2c); }
    static Self *FromPersist(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - 0x34); }
    static cAmbientLight *AmbientFrom(Self *self) { return static_cast<cAmbientLight *>(self); }
    static cDirectionalLight *DirectionalFrom(Self *self) { return static_cast<cDirectionalLight *>(self); }

    // The part (+0).
    static bool __fastcall QueryInterface(void *face, void *, uint32_t iid, void **out);        // 0xa48a70
    static uint32_t __fastcall Release(void *face, void *);                                     // 0x3c9030
    static void __fastcall OnLightChanged(void *face, void *);                                  // 0xa488a0
    static void __fastcall AmbientOnLightChanged(void *face, void *);                           // 0xa48230
    static void __fastcall DirectionalOnLightChanged(void *face, void *);                       // 0xa482b0
    // The node face (+0x2c).
    static bool __fastcall NodeQueryInterface(void *face, void *, uint32_t iid, void **out);    // 0x3c89e3
    static uint32_t __fastcall NodeRelease(void *face, void *);                                 // 0x3c9038
    static void __fastcall NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data); // 0x3c9930
    static bool __fastcall AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data);    // 0x3c87b0
    static uint32_t __fastcall GetEventCount(void *face, void *);                               // 0xa481e0
    static nGZEvent::cAttributeIDRange *__fastcall GetEventIds(void *face, void *, nGZEvent::cAttributeIDRange *out); // 0xa46c50
    static bool __fastcall IsKindOf(void *face, void *, uint32_t iid);                          // 0xa4a5d0
    static cIStandardLight *__fastcall GetStandardLight(void *face, void *);                    // 0xa46c40
    static void __fastcall SetLightName(void *face, void *, const char *name);                  // 0xa4a1f0
    static void __fastcall SetColor(void *face, void *, const float *rgb);                      // 0xa49f40
    static void __fastcall SetIntensity(void *face, void *, float value);                       // 0xa4a110
    static void __fastcall SetFactor(void *face, void *, float value);                          // 0xa4a4c0
    static const char *__fastcall GetLightName(void *face, void *);                             // 0xa480a0
    static float *__fastcall GetColor(void *face, void *, float *rgb);                          // 0xa47400
    static float __fastcall GetRange(void *face, void *);                                       // 0xa48cc0
    static void __fastcall AmbientClone(void *face, void *, uint32_t mode, uint32_t iid, void **out,
                                        cINodeCloneContext *context);                           // 0xa47080
    static void __fastcall DirectionalClone(void *face, void *, uint32_t mode, uint32_t iid, void **out,
                                            cINodeCloneContext *context);                       // 0xa47100
    static void __fastcall DirectionalGetParams(void *face, void *, float *out, uint32_t unused); // 0xa48b40
    // The persist face (+0x34).
    static bool __fastcall PersistQueryInterface(void *face, void *, uint32_t iid, void **out); // 0x3c89eb
    static uint32_t __fastcall PersistRelease(void *face, void *);                              // 0x3c9040
    static void __fastcall Read(void *face, void *, ifaces::cIGZPersistReader *reader);         // 0xa49070
    static void __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer);        // 0xa4b250
    static cPersistTypeInfo *__fastcall AmbientGetTypeInfo(void *face, void *, cPersistTypeInfo *out);     // 0xa4a6d0
    static void __fastcall AmbientWrite(void *face, void *, ifaces::cIGZPersistWriter *writer);            // 0xa4ade0
    static cPersistTypeInfo *__fastcall DirectionalGetTypeInfo(void *face, void *, cPersistTypeInfo *out); // 0xa4a790
    static void __fastcall DirectionalWrite(void *face, void *, ifaces::cIGZPersistWriter *writer);        // 0xa4ae60
    // The records (the game's bodies ignore `this`: the nTSSG lights' virtual-base thunks jump to them).
    static const void *__fastcall AmbientRecord19(void *, void *);                              // 0xa48140
    static const void *__fastcall DirectionalRecord19(void *, void *);                          // 0xa48150
    // The copy (this the source, at +0; the game's point light's copy 0xa47f50 calls it).
    static void __fastcall CopyTo(Self *self, void *, Self *target, uint32_t mode);             // 0xa47ff0
};

} // namespace nGZSceneGraph

// Our compiler's constructors and destructors of the three classes as hook targets (thiscall; the constructors take the
// most-derived flag, as the game's do). The /alternatename names are in cStandardLight.cpp.
extern "C" const uint8_t cStandardLightBase_Constructor;
extern "C" const uint8_t cAmbientLight_Constructor;
extern "C" const uint8_t cDirectionalLight_Constructor;
extern "C" const uint8_t cStandardLightBase_Destructor;
extern "C" const uint8_t cAmbientLight_Destructor;
extern "C" const uint8_t cDirectionalLight_Destructor;
extern "C" const int cStandardLight_Anchor;
#pragma comment(linker, "/include:_cStandardLight_Anchor")
