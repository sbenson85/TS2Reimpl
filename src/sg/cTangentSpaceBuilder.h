#pragma once
// nGZSceneGraph::cTangentSpaceBuilder (lane QG, scenegraph D2 part 2): the geometry builder that makes a container's
// tangents. For the groups it is told to take (by name, triangle lists), each triangle's tangent frame (from the
// positions and the first texture coordinates) is accumulated at its three vertices, area weighted; each vertex's
// frame is then made orthogonal to its normal (Gram-Schmidt, with a fallback from the normal alone for a degenerate
// one) and the tangents of a linkage become a new element (identity 0x89d92ba0, block format 2, set format 3) of that
// linkage. Class 0x5d054225 of the scene graph's COM director. Standalone shape (docs/standalone-shape.md), on our
// cGeometryBuilder (src/sg/cGeometryBuilder.h):
//
//   class cTangentSpaceBuilder : public cGeometryBuilder, public virtual cITangentSpaceBuilder
//
//   +0x00 the behaviour template's vtable (0xe74510: the deleting destructor 0xa86af0, the destructor 0xa86a80)
//   +0x04 the vbptr, +0x08 the count, +0x0c cIGZPersistSerializable2 (vtable 0xe74518), +0x10 the extra listener
//   +0x14 mNames      std::set<std::string> ordered without case (_stricmp): the groups Build takes
//   +0x1c the vtordisp, +0x20 the virtual base cIGeometryBuilder (vtable 0xe74534, 18 slots)
//   +0x24 the vtordisp, +0x28 the virtual base cITangentSpaceBuilder (vtable 0xe74580, 1 slot), its vbptr at +0x2c
//
// The factory 0xa86bf0 (new 0x30, the constructor 0xa86920) is the only hook: every other body is reached through the
// vtables our objects carry (db/status/cTangentSpaceBuilder.jsonl).
#include "sg/cBoundingVolumeBuilder.h" // cBoundingVolumeNameLess: the same case-blind order
#include "sg/cGeometryBuilder.h"

#include <set>
#include <string>

namespace nGZSceneGraph {

// nGZSceneGraph::cITangentSpaceBuilder (interface id 0xdd054220).
class cITangentSpaceBuilder : public virtual cIGeometryBuilder {
public:
    static constexpr uint32_t kIid = 0xdd054220;
    virtual void AddName(const char *name) = 0; // 0: the thunk 0xa86b10, the body 0xa86b20
};

// C4250 ("inherits ... via dominance"): intended, as cCompactorBuilder's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cTangentSpaceBuilder final : public cGeometryBuilder, public virtual cITangentSpaceBuilder {
public:
    static constexpr uint32_t kClassId = 0x5d054225;
    static constexpr uint32_t kVersion = 2; // the type-info record's version (0xa888c0)

    // 0xa86bf0 (__cdecl): the COM director's factory for kClassId: a new builder (count 0), as its cIGeometryBuilder.
    static cIGZUnknown *__cdecl Create();

    cTangentSpaceBuilder();               // 0xa86920
    cTangentSpaceBuilder(const cTangentSpaceBuilder &) = delete;
    cTangentSpaceBuilder &operator=(const cTangentSpaceBuilder &) = delete;
    ~cTangentSpaceBuilder() override;     // 0xa86a80 (deleting 0xa86af0)

    // ---- the persist face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa88230: kIid, else cGeometryBuilder's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa88280
    cPersistTypeInfo GetTypeInfo() override;                // 0xa888c0: {"cTangentSpaceBuilder", kClassId, 2}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa88990

    // ---- cIGeometryBuilder ----
    void Build(cIGeometryDataContainer *container) override; // 16: the thunk 0xa86c76, the body 0xa86c80
    const char *GetName() override;                          // 17: 0xa86bd3, "TangentSpaceBuilder"

    // ---- cITangentSpaceBuilder ----
    void AddName(const char *name) override;

    std::set<std::string, cBoundingVolumeNameLess> mNames; // +0x14
};
#pragma warning(pop)

} // namespace nGZSceneGraph
