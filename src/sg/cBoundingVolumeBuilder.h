#pragma once
// nGZSceneGraph::cBoundingVolumeBuilder (lane QE, scenegraph D2 part 1): the geometry builder that makes a container's
// triangle sets: the bounding mesh (every triangle of every triangle-list group, its corners the linkages' position
// vertices) and, when the container has bone weights, one subset mesh per bone (the triangles a bone weighs most on, put
// in that bone's frame). Class 0x1cfeceb8 of the scene graph's COM director. Standalone shape
// (docs/standalone-shape.md), on our cGeometryBuilder (src/sg/cGeometryBuilder.h):
//
//   class cBoundingVolumeBuilder : public cGeometryBuilder, public virtual cIBoundingVolumeBuilder
//
//   +0x00 the behaviour template's vtable (0xe743e8: the deleting destructor 0xa84c80, the destructor 0xa84c10)
//   +0x04 the vbptr, +0x08 the count, +0x0c cIGZPersistSerializable2 (vtable 0xe743f0), +0x10 the extra listener
//   +0x14 mNames      std::set<std::string> ordered without case (_stricmp): the groups Build takes when mNamedOnly
//   +0x1c mNamedOnly  (0 at first)
//   +0x20 the vtordisp, +0x24 the virtual base cIGeometryBuilder (vtable 0xe7440c, 18 slots)
//   +0x28 the vtordisp, +0x2c the virtual base cIBoundingVolumeBuilder (vtable 0xe74458, 3 slots), its vbptr at +0x30
//
// The factory 0xa85090 (new 0x34, the constructor 0xa84b20) is the only hook: every other body is reached through the
// vtables our objects carry (db/status/cBoundingVolumeBuilder.jsonl).
#include "sg/cGeometryBuilder.h"

#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace nGZCompGeom {
class cITriangleSetShape; // sg/cGeometryDataContainerClass.h
}

namespace nGZSceneGraph {

// nGZSceneGraph::cIBoundingVolumeBuilder (interface id 0x3cfeceb2).
class cIBoundingVolumeBuilder : public virtual cIGeometryBuilder {
public:
    static constexpr uint32_t kIid = 0x3cfeceb2;
    virtual void AddName(const char *name) = 0;     // 0: the thunk 0xa84cab, the body 0xa84cc0
    virtual void SetNamedOnly(bool namedOnly) = 0;  // 1: folded 0x9e0f30 behind the thunk 0x9e0f19
    virtual bool HasName(const char *name) = 0;     // 2: the thunk 0xa85c49, the body 0xa85c60
};

// The names' order: _stricmp's.
struct cBoundingVolumeNameLess {
    bool operator()(const std::string &a, const std::string &b) const { return _stricmp(a.c_str(), b.c_str()) < 0; }
};

// C4250 ("inherits ... via dominance"): intended, as cCompactorBuilder's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cBoundingVolumeBuilder final : public cGeometryBuilder, public virtual cIBoundingVolumeBuilder {
public:
    static constexpr uint32_t kClassId = 0x1cfeceb8;
    static constexpr uint32_t kVersion = 2; // the type-info record's version (0xa861f0); Read reads the names from 2

    // 0xa85090 (__cdecl): the COM director's factory for kClassId: a new builder (count 0), as its cIGeometryBuilder.
    static cIGZUnknown *__cdecl Create();

    cBoundingVolumeBuilder();               // 0xa84b20
    cBoundingVolumeBuilder(const cBoundingVolumeBuilder &) = delete;
    cBoundingVolumeBuilder &operator=(const cBoundingVolumeBuilder &) = delete;
    ~cBoundingVolumeBuilder() override;     // 0xa84c10 (deleting 0xa84c80)

    // ---- the persist face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa85f30: kIid, else cGeometryBuilder's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa85f80
    cPersistTypeInfo GetTypeInfo() override;                // 0xa861f0: {"cBoundingVolumeBuilder", kClassId, 2}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa862d0

    // ---- cIGeometryBuilder ----
    void Build(cIGeometryDataContainer *container) override; // 16: the thunk 0xa85d46, the body 0xa85d50
    const char *GetName() override;                          // 17: 0xa84d89, "BoundingVolumeBuilder"

    // ---- cIBoundingVolumeBuilder ----
    void AddName(const char *name) override;
    void SetNamedOnly(bool namedOnly) override;
    bool HasName(const char *name) override;

    std::set<std::string, cBoundingVolumeNameLess> mNames; // +0x14
    bool mNamedOnly = false;                                // +0x1c

private:
    using Meshes = std::vector<nGZCompGeom::cITriangleSetShape *>;
    // 0xa84dc0: for each vertex, the bone (the weight element's repetition) whose weight on it is the greatest.
    static void SelectBones(cIGeometryDataContainer *container, std::vector<uint32_t> &bones);
    // 0xa85120: one pass over the groups (Build runs two: the first sizes the meshes, the second fills them).
    void Fill(cIGeometryDataContainer *container, nGZCompGeom::cITriangleSetShape **mesh, Meshes &subsets,
              const std::vector<uint32_t> &bones);
};
#pragma warning(pop)

} // namespace nGZSceneGraph
