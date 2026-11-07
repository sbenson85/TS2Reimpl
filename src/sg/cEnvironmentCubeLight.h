#pragma once
// nGZSceneGraph::cEnvironmentCubeLight (lane TC, scenegraph N2r): the environment cube light, class 0x6a97042f, on our light
// template, the sibling of the fog and draw-state lights (src/sg/cLinearFogLight.h):
//
//   class cEnvironmentCubeLight : public cLightT<cITextureLight, cTextureLightBehaviorTraits>
//
//   +0x00 the cSGResource part (vtable 0xe6adb8, 13 slots), +0x2c the node face cITextureLight (0xe6adf0, 44 slots: 43 the
//   deleting destructor), +0x34 the persist face (0xe6aea4), +0x38 the cReferentNodeImpl
//   +0x4c mTexture (a counted texture handle, -1 at first), +0x50 mColors[4][4] (each 1, 1, 1, 1: the constant at
//   0x106ac5c), +0x90 the vtordisp, +0x94 the virtual base cISGResource (0xe6aec4, 25 slots); 0x98 bytes
//
// Persisted (version 2): the light's records, then the texture's name (the texture manager's slot 24 for the handle's id;
// "" for none) and the 16 floats. Reading a version-1 record reads no colours; an empty name sets no texture. A clone gets
// the texture but keeps the default colours (the game's copy 0xa47ef0 copies the handle only). The strength is a
// constant 1000 (slots 38 / 39, the shared 0x3c9080 / 0x3c9090 reading 0xda4568).
//
// The switch: the director's factory 0xa47bd0 is hooked to Create. Nothing derives from this class and its constructor
// 0xa45e80 has no other callers than the factory and the game's Clone 0xa47200, so once the factory is ours every body
// below is in effect through the factory.
#include "game/owned.h"
#include "sg/SGLightTemplates.h"
#include "sg/cLinearFogLight.h" // SimpleLightEventRegistry
#include "sg/cTextureManager.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6144); // the cell below (game/owned.h)
struct cTextureLightBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6144; // 0xa475b0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6144; }
    static nGZEvent::cEventAttributeMap *Registry() { return SimpleLightEventRegistry(); }
};

class cEnvironmentCubeLight : public cLightT<cITextureLight, cTextureLightBehaviorTraits> {
    using Light = cLightT<cITextureLight, cTextureLightBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x98;
    static constexpr uint32_t kClassId = 0x6a97042f;
    static constexpr float kStrength = 1000.0f; // 0xda4568

    cEnvironmentCubeLight() = default;           // 0xa45e80 (flag 1)
    ~cEnvironmentCubeLight() override = default; // 0xa466b0; deleting 0xa46af0 (primary 12), 0xa4685a (node face 43)

    static cITextureLight *__cdecl Create(); // 0xa47bd0 (hooked)

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa48970 (ret 8): cITextureLight is the node face, counted
    bool IsKindOf(uint32_t iid) override;                     // 0xa4a590

    // ---- cIObjectGraphNode ----
    // 15 (0xa47200, ret 0x10): a new light, CopyContentTo, viewed as `iid` in *out (counted when answered).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    // ---- cILight (34 / 35 / 37 the shared 0x6c6b0, 36 the shared 0x155a00) ----
    cIStandardLight *GetStandardLightConst() override { return nullptr; }
    cIStandardLight *GetStandardLight() override { return nullptr; }
    uint32_t GetLightType() override { return kEnvironmentCube; }
    void *GetBoundShape() override { return nullptr; }
    float StrengthOverBox(const float *, AffineTransform3 *) override { return kStrength; } // shared 0x3c9080
    float StrengthAt(const float *) override { return kStrength; }                           // shared 0x3c9090

    // ---- cITextureLight ----
    void SetTexture(const char *name) override;                // 0xa4a520
    void SetColor(uint32_t index, const float *rgba) override; // 0xa4a090
    float *GetColor(float *rgba, uint32_t index) override;     // 0xa47420

    // ---- cIGZPersistSerializableReferent ----
    // 0xa48dc0 (ret 4): the record, the light's Read, the name (GetGZStr; a non-empty one set through slot 40), then from
    // version 2 the 16 floats; the record's name dropped last.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override;                // 0xa4a940: {"cEnvironmentCubeLight", 0x6a97042f, 2}
    // 0xa4af60 (ret 4): the record, the light's Write, the texture's name (WriteRZCharStr), the 16 floats (SetFloat32).
    void Write(ifaces::cIGZPersistWriter *writer) override;
    const void *GetRecord19() override { return "5el"; } // 0xa48166

    void CopyContentTo(cEnvironmentCubeLight *target, uint32_t mode); // 0xa47ef0 (ret 8, this the source)

    cTextureHandle mTexture;                                                                     // +0x4c
    float mColors[4][4] = {{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}};            // +0x50
};

} // namespace nGZSceneGraph
