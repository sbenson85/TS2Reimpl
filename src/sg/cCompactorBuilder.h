#pragma once
// nGZSceneGraph::cCompactorBuilder (lane QE, scenegraph D2 part 1): the geometry builder that renumbers each linkage's
// vertices in the order its groups first use them, drops the ones no group uses, and rewrites the linkages' alias lists
// and the groups' vertex indices to match (class 0xdcdda078 of the scene graph's COM director). Standalone shape
// (docs/standalone-shape.md), on our cGeometryBuilder (src/sg/cGeometryBuilder.h):
//
//   class cCompactorBuilder : public cGeometryBuilder, public virtual cICompactorBuilder
//
//   +0x00 the behaviour template's vtable (0xe742c8: the deleting destructor 0xa838c0, the destructor 0xa83870)
//   +0x04 the vbptr, +0x08 the count, +0x0c cIGZPersistSerializable2 (vtable 0xe742d0), +0x10 the extra listener
//   +0x14 mPerGroupReset  the remap cleared between a linkage's groups (cICompactorBuilder slot 0 sets it; 1 at first)
//   +0x15 mEnabled        Build runs only when set (1 at first; nothing else writes it)
//   +0x18 the vtordisp, +0x1c the virtual base cIGeometryBuilder (vtable 0xe742ec, 18 slots)
//   +0x20 the vtordisp, +0x24 the virtual base cICompactorBuilder (vtable 0xe74338, 1 slot), its own vbptr at +0x28
//
// The factory 0xa83930 (new 0x2c, the constructor 0xa837e0) is the only hook: every other body is reached through the
// vtables our objects carry (db/status/cCompactorBuilder.jsonl).
#include "sg/cGeometryBuilder.h"

namespace nGZSceneGraph {

// nGZSceneGraph::cICompactorBuilder (interface id 0x1cdda073).
class cICompactorBuilder : public virtual cIGeometryBuilder {
public:
    static constexpr uint32_t kIid = 0x1cdda073;
    virtual void SetPerGroupReset(bool reset) = 0; // 0 (folded 0x9e0f30 behind the vtordisp thunk 0x9e0f19)
};

// C4250 ("inherits ... via dominance"): intended. cGeometryBuilder's bodies are the final overriders of the
// cIGeometryBuilder slots cICompactorBuilder reaches through the shared virtual base.
#pragma warning(push)
#pragma warning(disable : 4250)
class cCompactorBuilder final : public cGeometryBuilder, public virtual cICompactorBuilder {
public:
    static constexpr uint32_t kClassId = 0xdcdda078;
    static constexpr uint32_t kVersion = 1; // the type-info record's version (0xa84480)

    // 0xa83930 (__cdecl): the COM director's factory for kClassId: a new builder (count 0), as its cIGeometryBuilder.
    static cIGZUnknown *__cdecl Create();

    cCompactorBuilder();                    // 0xa837e0
    cCompactorBuilder(const cCompactorBuilder &) = delete;
    cCompactorBuilder &operator=(const cCompactorBuilder &) = delete;
    ~cCompactorBuilder() override;          // 0xa83870 (deleting 0xa838c0)

    // ---- the persist face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa83a00: kIid, else cGeometryBuilder's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0x3b7bd0 (folded): its record, then the base's
    cPersistTypeInfo GetTypeInfo() override;                // 0xa84480: {"cCompactorBuilder", kClassId, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa84550: its record, then the base's

    // ---- cIGeometryBuilder ----
    void Build(cIGeometryDataContainer *container) override; // 16: the thunk 0xa839cc (mEnabled), the body 0xa83a60
    const char *GetName() override;                          // 17: 0xa838f6, "CompactorBuilder"

    // ---- cICompactorBuilder ----
    void SetPerGroupReset(bool reset) override; // 0x9e0f30

    bool mPerGroupReset = true; // +0x14
    bool mEnabled = true;       // +0x15

private:
    // 0xa83a60: the renumbering itself.
    void Compact(cIGeometryDataContainer *container);
};
#pragma warning(pop)

} // namespace nGZSceneGraph
