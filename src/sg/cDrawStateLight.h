#pragma once
// nGZSceneGraph::cDrawStateLight (lane OT, scenegraph N2): a light that carries two draw-state flags, class 0xac06a676, on
// our light template. The sibling of the fog light (src/sg/cLinearFogLight.h):
//
//   class cDrawStateLight : public cLightT<cIDrawStateLight, cDrawStateBehaviorTraits>
//
//   +0x00 the cSGResource part (vtable 0xe6b850, 13 slots), +0x2c the node face cIDrawStateLight (0xe6b888, 44 slots: 43 the
//   deleting destructor), +0x34 the persist face (0xe6b93c), +0x38 the cReferentNodeImpl
//   +0x4c mFirstFlag, +0x4d mSecondFlag (both 1 at first: one 16-bit store of 0x101), +0x50 the vtordisp, +0x54 the
//   virtual base cISGResource (0xe6b95c, 25 slots); 0x58 bytes
//
// Nothing is persisted but the records; the strength is a constant 3000 (slots 38 / 39, 0xdbdd40). As for the fog light the
// director's factory 0xa47b70 is the switch: nothing derives from this class and its constructor 0xa45e20 has no other
// callers than the factory and the game's Clone 0xa47180, so every body below is in effect through the factory.
#include "game/owned.h"
#include "sg/cLinearFogLight.h"

namespace nGZSceneGraph {

TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f614c); // the cell below (game/owned.h)
struct cDrawStateBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f614c; // 0xa474b0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f614c; }
    static nGZEvent::cEventAttributeMap *Registry() { return SimpleLightEventRegistry(); }
};

class cDrawStateLight : public cLightT<cIDrawStateLight, cDrawStateBehaviorTraits> {
    using Light = cLightT<cIDrawStateLight, cDrawStateBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x58;
    static constexpr uint32_t kClassId = 0xac06a676;
    static constexpr float kStrength = 3000.0f; // 0xdbdd40

    cDrawStateLight() = default;           // 0xa45e20 (flag 1)
    ~cDrawStateLight() override = default; // 0xa46670; deleting 0xa46ad0 (primary 12), 0xa46852 (node face 43)

    static cIDrawStateLight *__cdecl Create(); // 0xa47b70 (hooked)

    bool QueryInterface(uint32_t iid, void **out) override; // 0xa48900 (ret 8)
    bool IsKindOf(uint32_t iid) override;                     // 0xa4a570
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override; // 15 (0xa47180, ret 0x10)

    cIStandardLight *GetStandardLightConst() override { return nullptr; } // 34 (shared 0x6c6b0)
    cIStandardLight *GetStandardLight() override { return nullptr; }      // 35
    uint32_t GetLightType() override { return kDrawState; }               // 36 (0xa4a5f0)
    void *GetBoundShape() override { return nullptr; }                    // 37
    float StrengthOverBox(const float *, AffineTransform3 *) override { return kStrength; } // 38 (shared 0x3a5f40)
    float StrengthAt(const float *) override { return kStrength; }                           // 39 (shared 0x3a5f50)

    void SetFirstFlag(uint8_t value) override { mFirstFlag = value; }   // 40 (shared 0x46de50)
    void SetSecondFlag(uint8_t value) override { mSecondFlag = value; } // 41 (shared 0x46e260)
    uint8_t *GetFlags() override { return &mFirstFlag; }                // 42 (shared 0x1758b0)

    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa48d50 (shared with the fog light)
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa4a870: {"cDrawStateLight", 0xac06a676, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4aee0
    const void *GetRecord19() override { return "5ds"; }    // 0xa48156

    void CopyContentTo(cDrawStateLight *target, uint32_t mode); // 0xa47ed0 (ret 8): CopyTo, then the 16 bits at +0x4c

    uint8_t mFirstFlag = 1;  // +0x4c
    uint8_t mSecondFlag = 1; // +0x4d
};

} // namespace nGZSceneGraph
