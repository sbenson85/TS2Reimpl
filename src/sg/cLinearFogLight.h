#pragma once
// nGZSceneGraph::cLinearFogLight (lane OT, scenegraph N2): the fog light, class 0xac06a66f, on our light template.
//
//   class cLinearFogLight : public cLightT<cILinearFogLight, cSimpleLightBehaviorTraits>
//
//   +0x00 the cSGResource part (vtable 0xe6b304, 13 slots: no OnLightChanged), +0x2c the node face cILinearFogLight
//   (0xe6b33c, 44 slots: 43 the deleting destructor), +0x34 the persist face (0xe6b3f0), +0x38 the cReferentNodeImpl
//   +0x4c mNear (10.0 at first), +0x50 mFar (100.0), +0x54 mColor[3] (1, 1, 1: the constants at 0x106ac6c)
//   +0x60 the vtordisp, +0x64 the virtual base cISGResource (0xe6b410, 25 slots); 0x68 bytes
//
// Nothing in the light reacts to its own change (no slot 13) and nothing is persisted but the records: the near and far
// distances and the colour are set by whoever builds the light. Its strength is a constant 2000 (slots 38 / 39).
//
// The switch: the director's factory 0xa47c30 is hooked to Create. Nothing derives from this class and its constructor
// 0xa45fc0 has no other callers than the factory and the game's Clone 0xa47280, so once the factory is ours no fog light
// has the game's vtables: every body below is in effect through the factory.
#include "game/owned.h"
#include "sg/SGLightTemplates.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The event registry of the fog and draw-state lights (and the environment cube; the parent behaviour of the standard lights
// and the shape too): the shared map 0x3a5d00 builds on first use, the object-graph registry plus the event 0xdd041f30.
struct cSimpleLightBehaviorTraits {
    // 0x3a5d00 (cdecl): the map, built on first use (its holder 0x3a3e60, with 0x3a4090: the object-graph registry's base step,
    // the map a copy of that registry 0x25f030, then 0xdd041f30 stored); a non-zero `id` stores `attribute` under it. Our own
    // map (the original's is the game global 0x10da02c). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only: the next use builds the registry again
};

// 0xa480c0 (cdecl, no arguments): the map, with id 0.
nGZEvent::cEventAttributeMap *SimpleLightEventRegistry();

// The behaviour cells: each instantiation of cAbstractBehaviorTypeT keeps the event manager it looks up in its own cell.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6148); // the cell below (game/owned.h)
struct cLinearFogBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6148; // 0xa47530's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6148; }
    static nGZEvent::cEventAttributeMap *Registry() { return SimpleLightEventRegistry(); }
};

class cLinearFogLight : public cLightT<cILinearFogLight, cLinearFogBehaviorTraits> {
    using Light = cLightT<cILinearFogLight, cLinearFogBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x68;
    static constexpr uint32_t kClassId = 0xac06a66f; // the type-info record's word
    static constexpr float kNear = 10.0f;            // 0xa45fc0: 0x41200000
    static constexpr float kFar = 100.0f;            // 0x42c80000
    static constexpr float kStrength = 2000.0f;      // 0xe6ba54: what slots 38 / 39 answer

    cLinearFogLight() = default;           // 0xa45fc0 (flag 1)
    ~cLinearFogLight() override = default; // 0xa46700; deleting 0xa46b20 (primary 12), 0xa46862 (node face 43)

    static cILinearFogLight *__cdecl Create(); // 0xa47c30 (hooked)

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa489e0 (ret 8): cILinearFogLight is the node face, counted
    bool IsKindOf(uint32_t iid) override;                     // 0xa4a5b0

    // ---- cIObjectGraphNode ----
    // 15 (0xa47280, ret 0x10): a new light, CopyTo, viewed as `iid` in *out (counted when answered).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;

    // ---- cILight (shared bodies: 0x6c6b0 twice, 0x162f00, 0x6c6b0; 0xa49500 / 0xa49510 return 2000) ----
    cIStandardLight *GetStandardLightConst() override { return nullptr; }
    cIStandardLight *GetStandardLight() override { return nullptr; }
    uint32_t GetLightType() override { return kLinearFog; }
    void *GetBoundShape() override { return nullptr; }
    float StrengthOverBox(const float *, AffineTransform3 *) override { return kStrength; }
    float StrengthAt(const float *) override { return kStrength; }

    // ---- cILinearFogLight ----
    void SetNearFar(float nearDistance, float farDistance) override { mNear = nearDistance, mFar = farDistance; } // 0x3cad30
    void SetColor(const float *rgb) override;     // 0xa4a1a0
    float *GetNearFar() override { return &mNear; } // 0x1758b0

    // ---- cIGZPersistSerializableReferent ----
    void Read(ifaces::cIGZPersistReader *reader) override; // 0xa48d50 (ret 4): the record (dropped last), the light's Read
    cPersistTypeInfo GetTypeInfo() override;               // 0xa4aa20: {"cLinearFogLight", 0xac06a66f, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa4b060: the record, the light's Write
    const void *GetRecord19() override { return "5lf"; }   // 0xa48176

    void CopyContentTo(cLinearFogLight *target, uint32_t mode); // 0xa47f20 (ret 8, this the source)

    float mNear = kNear;               // +0x4c
    float mFar = kFar;                 // +0x50
    float mColor[3] = {1.0f, 1.0f, 1.0f}; // +0x54
};

} // namespace nGZSceneGraph
