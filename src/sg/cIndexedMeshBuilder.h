#pragma once
// nGZSceneGraph::cIndexedMeshBuilder (lane QH, scenegraph D2 part 3): the geometry builder the GMDC loader and the
// mesh converters feed. It collects a mesh through its cIIndexedMeshBuilder face (positions, normals, texture
// coordinates, colours, bones with their influences, morph targets, then groups made of indices into those lists, one
// vertex index list per attribute) and, on Finish, post-processes it (five helpers, lane QL, D2 part 4) and on
// Build writes it into a container: one element per attribute, one linkage per distinct set of attributes the groups
// use, one group per run of triangles. Class 0x9bffc10d of the scene graph's COM director. Standalone shape
// (docs/standalone-shape.md), on our cGeometryBuilder (src/sg/cGeometryBuilder.h):
//
//   class cIndexedMeshBuilder : public cGeometryBuilder, public virtual cIIndexedMeshBuilder
//
//   +0x00 the behaviour template's vtable (0xe73f4c: the deleting destructor 0xa7c2f0)
//   +0x04 the vbptr, +0x08 the count, +0x0c cIGZPersistSerializable2 (vtable 0xe73f54), +0x10 the extra listener
//   +0x14 .. +0x1cc the collected mesh (below)
//   +0x1cc the vtordisp, +0x1d0 the virtual base cIGeometryBuilder (vtable 0xe73f70, 18 slots)
//   +0x1d4 the vtordisp, +0x1d8 the virtual base cIIndexedMeshBuilder (vtable 0xe73fbc, 48 slots), its vbptr at +0x1dc
//
// The factory 0xa7ffa0 and the matrix-to-quaternion helper 0x1a7570 (shared with nine other callers) are the hooks: every
// other body is reached through the vtables our objects carry (db/status/cIndexedMeshBuilder.jsonl). The five Finish
// helpers (0xa7e380, 0xa7f4a0, 0xa820a0, 0xa7ed00, 0xa7f0b0) are ours, called only by Finish and Read. The members'
// layout is still the game's (the vectors are the compiler's, three words each): other game code reads them.
#include "compgeom/CompGeom.h"
#include "misc/UtilitiesC.h" // AffineTransform3
#include "rz/cRZString.h"
#include "sg/cBoundingVolumeBuilder.h" // cBoundingVolumeNameLess: the same case-blind order
#include "sg/cGeometryBuilder.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace nGZSceneGraph {

// nGZSceneGraph::cIIndexedMeshBuilder (interface id 0x9bffc101): 48 slots of its own on its own vtable.
class cIIndexedMeshBuilder : public virtual cIGeometryBuilder {
public:
    static constexpr uint32_t kIid = 0x9bffc101;
    // The slots take the object by this face; the numbers are the vtable's.
    virtual void Reset(bool keepGroups) = 0;                                              // 0 (0xa7dd80)
    virtual void Finish() = 0;                                                            // 1 (0xa7f940)
    virtual void SetTransform(const AffineTransform3 *transform) = 0;       // 2 (0xa81ebf)
    virtual void AddPositions(uint32_t count, const float *points) = 0;                   // 3 (0xa7da50)
    virtual void AddNormals(uint32_t count, const float *vectors) = 0;                    // 4 (0xa7d780)
    virtual void AddTexCoords(uint32_t count, const float *coords, uint32_t set) = 0;     // 5 (0xa7dc90)
    virtual void AddColours(uint32_t count, const float *rgba, uint32_t set) = 0;         // 6 (0xa7c670)
    virtual void AddWordsA(uint32_t count, const uint32_t *values) = 0;                   // 7 (0xa7dcc7)
    virtual void AddWordsB(uint32_t count, const uint32_t *values) = 0;                   // 8 (0xa7db33)
    virtual void AddPosition(const float *point) = 0;                                     // 9 (0xa7d960)
    virtual void AddNormal(const float *vector) = 0;                                      // 10 (0xa7d590)
    virtual void AddTexCoord(const float *coord, uint32_t set) = 0;                       // 11 (0xa7dbc8)
    virtual void AddColour(const float *rgba, uint32_t set) = 0;                          // 12 (0xa7c4b4)
    virtual void SetBoneCount(uint32_t count) = 0;                                        // 13 (0xa81b10)
    virtual void SetBoneData(uint32_t bone, const void *data) = 0;                        // 14 (0xa81b6e)
    virtual void AddInfluences(uint32_t bone, uint32_t count, const uint32_t *vertices, const float *weights) = 0; // 15
    virtual void AddInfluence(uint32_t bone, uint32_t vertex, float weight) = 0;          // 16 (0xa7c313)
    virtual void SetMorphTargetCount(uint32_t count) = 0;                                 // 17 (0xa83740)
    virtual void SetMorphNames(uint32_t morph, const char *first, const char *second) = 0; // 18 (0xa81a73)
    virtual void AddMorphPositions(uint32_t morph, uint32_t count, const float *points) = 0;   // 19 (0xa7d440)
    virtual void AddMorphNormals(uint32_t morph, uint32_t count, const float *vectors) = 0;    // 20 (0xa7cfe0)
    virtual void SetMorphPositions(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices) = 0; // 21
    virtual void SetMorphNormals(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices) = 0;   // 22
    virtual void AddMorphPosition(uint32_t morph, int32_t vertex, const float *point) = 0;  // 23 (0xa7d1e0)
    virtual void AddMorphNormal(uint32_t morph, int32_t vertex, const float *vector) = 0;   // 24 (0xa7caf0)
    virtual void AddMorphPositionAbsolute(uint32_t morph, int32_t vertex, const float *point) = 0; // 25 (0xa7d2f0)
    virtual void AddMorphNormalRaw(uint32_t morph, int32_t vertex, const float *vector) = 0; // 26 (0xa7ccb0)
    virtual void SetGroupName(const char *name) = 0;                                        // 27 (0xa81bc0)
    virtual void MarkUnknown() = 0;                                                         // 28 (0xa7e34d): sets +0x139
    virtual void CommitGroup() = 0;                                                         // 29 (0xa7fa10)
    virtual void AddPositionIndices(uint32_t count, const uint32_t *values, int32_t offset) = 0;   // 30 (0xa7d9ea)
    virtual void AddNormalIndices(uint32_t count, const uint32_t *values, int32_t offset) = 0;     // 31 (0xa7d71a)
    virtual void AddTexCoordIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset) = 0; // 32
    virtual void AddColourIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset) = 0;   // 33
    virtual void AddIndices(uint8_t kinds, uint32_t count, const uint32_t *values, int32_t offset) = 0;        // 34 (0xa7c957)
    virtual void AddPositionIndex(uint32_t value) = 0;                                      // 35 (0xa7d9b5)
    virtual void AddNormalIndex(uint32_t value) = 0;                                        // 36 (0xa7d6e1)
    virtual void AddTexCoordIndex(uint32_t value, uint32_t set) = 0;                        // 37 (0xa7dbf1)
    virtual void AddColourIndex(uint32_t value, uint32_t set) = 0;                          // 38 (0xa7c5cc)
    virtual void AddIndex(uint8_t kinds, uint32_t value) = 0;                               // 39 (0xa7c7ec)
    virtual bool IsCollecting() = 0;                                                        // 40 (0xa80050)
    virtual bool GetUnknown() = 0;                                                          // 41 (0xa80067)
    virtual uint32_t CountBones() = 0;                                                      // 42 (0xa80163)
    virtual uint32_t CountMorphTargets() = 0;                                               // 43 (0xa80103)
    virtual uint32_t CountPositions() = 0;                                                  // 44 (0xa80192)
    virtual uint32_t CountNormals() = 0;                                                    // 45 (0xa80132)
    virtual uint32_t CountTexCoords(uint32_t set) = 0;                                      // 46 (0xa801c3)
    virtual uint32_t CountColours(uint32_t set) = 0;                                        // 47 (0xa800cd)
};

namespace indexedmesh {

struct Float3 {
    float v[3];
};
struct Float2 {
    float v[2];
};

// A cRZString held by value, built the way the scene graph's functions build theirs (the game's vtable, the string,
// count 0) and destroyed without a vtable call; copyable (a vector of morph targets grows).
struct HeldString {
    cRZString value;
    HeldString()
    {
        value.mVTable = cRZString::VTable();
        ::new (&value.mString) std::string();
        value.mRefCount = 0;
    }
    HeldString(const HeldString &other)
    {
        value.mVTable = cRZString::VTable();
        ::new (&value.mString) std::string(other.value.mString);
        value.mRefCount = 0;
    }
    HeldString &operator=(const HeldString &other)
    {
        value.mString = other.value.mString;
        return *this;
    }
    ~HeldString() { value.mString.~basic_string(); }
};
static_assert(sizeof(HeldString) == 0x20, "HeldString layout");

// One group (0x80 bytes): its flags (which attribute lists its vertices have), the vertices it holds so far, its index
// lists (into the builder's attribute lists, relative to the group's own start) and its triangles.
struct Group {
    uint32_t flags = 0; // +0: bit 0 normals, 1-4 texture coordinate sets, 5-6 colour sets, 7 influences, 8 / 9 morph
                        // positions / normals, 10 / 11 the two extra word lists
    uint32_t count = 0; // +4: the vertices
    std::vector<uint32_t> positions; // +8
    std::vector<uint32_t> normals;   // +0x14
    std::vector<uint32_t> texCoords[4]; // +0x20
    std::vector<uint32_t> colours[2];   // +0x50
    std::vector<uint32_t> source;    // +0x68: Reset(keep)'s source for the lists a group keeps
    std::vector<uint32_t> triangles; // +0x74
};
static_assert(sizeof(Group) == 0x80, "Group layout");

// The group runs Build makes groups of: the group's name (an index into the names), the group, its first triangle
// index and its end (-1 until closed).
struct Run {
    int32_t name;
    int32_t group;
    int32_t start;
    int32_t end;
};
static_assert(sizeof(Run) == 0x10, "Run layout");

// A bone (0x1c bytes): its position and its rotation (x, y, z, w); the default is the identity rotation.
struct Bone {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};
static_assert(sizeof(Bone) == 0x1c, "Bone layout");

// One bone's influences on the vertices: the vertices and their weights.
struct Influence {
    std::vector<uint32_t> vertices;
    std::vector<float> weights;
};
static_assert(sizeof(Influence) == 0x18, "Influence layout");

// A morph target (0x70 bytes): its two names and the position and normal deltas (vertex, then delta).
struct Morph {
    HeldString first;  // +0
    HeldString second; // +0x20
    std::vector<uint32_t> positionVertices; // +0x40
    std::vector<Float3> positionDeltas;     // +0x4c
    std::vector<uint32_t> normalVertices;   // +0x58
    std::vector<Float3> normalDeltas;       // +0x64
};
static_assert(sizeof(Morph) == 0x70, "Morph layout");

// 0x1a7570 (__cdecl): the unit quaternion (x, y, z, w) of a row-major 3x3 rotation matrix; returns out. Hooked: the
// game's other callers (bone and transform code) get this one, bit for bit.
float *__cdecl QuaternionFromMatrix(float *out, const float *matrix);

} // namespace indexedmesh

// C4250 ("inherits ... via dominance"): intended, as cCompactorBuilder's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cIndexedMeshBuilder final : public cGeometryBuilder, public virtual cIIndexedMeshBuilder {
public:
    static constexpr uint32_t kClassId = 0x9bffc10d;
    static constexpr uint32_t kVersion = 10; // the type-info record's version (0xa81fc0)

    // 0xa7ffa0 (__cdecl): the COM director's factory for kClassId: a new builder (count 0), as its cIGeometryBuilder.
    static cIGZUnknown *__cdecl Create();

    cIndexedMeshBuilder();                // 0xa7b1d0
    cIndexedMeshBuilder(const cIndexedMeshBuilder &) = delete;
    cIndexedMeshBuilder &operator=(const cIndexedMeshBuilder &) = delete;
    ~cIndexedMeshBuilder() override;      // 0xa7b8d0 (deleting 0xa7c2f0)

    // ---- the persist face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa80c30: kIid, else cGeometryBuilder's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa80c90
    cPersistTypeInfo GetTypeInfo() override;                // 0xa81fc0: {"cIndexedMeshBuilder", kClassId, 10}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa826c0

    // ---- cIGeometryBuilder ----
    void Build(cIGeometryDataContainer *container) override; // 16: the thunk 0xa801f6, the body 0xa80200
    const char *GetName() override;                          // 17: 0xa7e368, "IndexedMeshBuilder"

    // ---- cIIndexedMeshBuilder ----
    void Reset(bool keepGroups) override;
    void Finish() override;
    void SetTransform(const AffineTransform3 *transform) override;
    void AddPositions(uint32_t count, const float *points) override;
    void AddNormals(uint32_t count, const float *vectors) override;
    void AddTexCoords(uint32_t count, const float *coords, uint32_t set) override;
    void AddColours(uint32_t count, const float *rgba, uint32_t set) override;
    void AddWordsA(uint32_t count, const uint32_t *values) override;
    void AddWordsB(uint32_t count, const uint32_t *values) override;
    void AddPosition(const float *point) override;
    void AddNormal(const float *vector) override;
    void AddTexCoord(const float *coord, uint32_t set) override;
    void AddColour(const float *rgba, uint32_t set) override;
    void SetBoneCount(uint32_t count) override;
    void SetBoneData(uint32_t bone, const void *data) override;
    void AddInfluences(uint32_t bone, uint32_t count, const uint32_t *vertices, const float *weights) override;
    void AddInfluence(uint32_t bone, uint32_t vertex, float weight) override;
    void SetMorphTargetCount(uint32_t count) override;
    void SetMorphNames(uint32_t morph, const char *first, const char *second) override;
    void AddMorphPositions(uint32_t morph, uint32_t count, const float *points) override;
    void AddMorphNormals(uint32_t morph, uint32_t count, const float *vectors) override;
    void SetMorphPositions(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices) override;
    void SetMorphNormals(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices) override;
    void AddMorphPosition(uint32_t morph, int32_t vertex, const float *point) override;
    void AddMorphNormal(uint32_t morph, int32_t vertex, const float *vector) override;
    void AddMorphPositionAbsolute(uint32_t morph, int32_t vertex, const float *point) override;
    void AddMorphNormalRaw(uint32_t morph, int32_t vertex, const float *vector) override;
    void SetGroupName(const char *name) override;
    void MarkUnknown() override;
    void CommitGroup() override;
    void AddPositionIndices(uint32_t count, const uint32_t *values, int32_t offset) override;
    void AddNormalIndices(uint32_t count, const uint32_t *values, int32_t offset) override;
    void AddTexCoordIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset) override;
    void AddColourIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset) override;
    void AddIndices(uint8_t kinds, uint32_t count, const uint32_t *values, int32_t offset) override;
    void AddPositionIndex(uint32_t value) override;
    void AddNormalIndex(uint32_t value) override;
    void AddTexCoordIndex(uint32_t value, uint32_t set) override;
    void AddColourIndex(uint32_t value, uint32_t set) override;
    void AddIndex(uint8_t kinds, uint32_t value) override;
    bool IsCollecting() override;
    bool GetUnknown() override;
    uint32_t CountBones() override;
    uint32_t CountMorphTargets() override;
    uint32_t CountPositions() override;
    uint32_t CountNormals() override;
    uint32_t CountTexCoords(uint32_t set) override;
    uint32_t CountColours(uint32_t set) override;

    // The collected mesh. Offsets are the game's (static_asserted in the .cpp): the Finish helpers use them.
    std::vector<indexedmesh::Float3> mPositions;         // +0x14
    std::vector<indexedmesh::Float3> mNormals;           // +0x20
    std::vector<indexedmesh::Float2> mTexCoords[4];      // +0x2c, 12 apart
    std::vector<uint32_t> mColours[2];                   // +0x5c, 12 apart: packed 0xRRGGBBAA-ish, four bytes
    uint32_t mWord74 = 0;                                // +0x74
    std::vector<uint32_t> mWordsA;                       // +0x78
    std::vector<uint32_t> mWordsB;                       // +0x84
    std::vector<indexedmesh::Group> mGroups;             // +0x90
    std::vector<indexedmesh::Run> mRuns;                 // +0x9c
    std::vector<std::string> mNames;                     // +0xa8: the group names, in the order first met
    std::vector<indexedmesh::Bone> mBones;               // +0xb4
    std::vector<indexedmesh::Influence> mInfluences;     // +0xc0
    std::vector<indexedmesh::Morph> mMorphs;             // +0xcc
    std::map<std::string, uint32_t, cBoundingVolumeNameLess> mNameIndex; // +0xd8: name -> index in mNames
    AffineTransform3 mTransform = {0, {1, 0, 0, 0, 1, 0, 0, 0, 1}, {0, 0, 0}}; // +0xe0
    float mNormalMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // +0x114: the inverse transpose of the transform's matrix
    bool mCollecting = false;                            // +0x138: between Reset and Finish
    bool mUnknown = false;                               // +0x139
    int32_t mCurrentName = -1;                           // +0x13c
    int32_t mLastName = -1;                              // +0x140
    int32_t mCurrentGroup = -1;                          // +0x144
    uint32_t mGroupBase = 0;                             // +0x148: the lists' sizes at Reset
    uint32_t mPositionBase = 0;                          // +0x14c
    uint32_t mNormalBase = 0;                            // +0x150
    uint32_t mTexCoordBase[4] = {};                      // +0x154
    uint32_t mColourBase[2] = {};                        // +0x164
    std::vector<uint32_t> mPositionIndices;              // +0x16c: the group being made
    std::vector<uint32_t> mNormalIndices;                // +0x178
    std::vector<uint32_t> mTexCoordIndices[4];           // +0x184
    std::vector<uint32_t> mColourIndices[2];             // +0x1b4

private:
    // Read / Write's halves and the slots' shared pieces (cIndexedMeshBuilder.cpp).
    void ReadBones(ifaces::cIGZPersistReader *reader, uint32_t version);
    void ReadMorphs(ifaces::cIGZPersistReader *reader);
    void ReadGroups(ifaces::cIGZPersistReader *reader);
    void WriteBones(ifaces::cIGZPersistWriter *writer);
    void WriteMorphs(ifaces::cIGZPersistWriter *writer);
    void WriteGroups(ifaces::cIGZPersistWriter *writer);
    void AddIndicesTo(std::vector<uint32_t> &list, uint32_t count, const uint32_t *values, int32_t offset);
    // Finish's five steps, in order (and Read's, for old versions: ClassifyGroups alone).
    void WeldVertices();           // 0xa7e380: equal texture coordinate / colour index tuples share one vertex
    void MergeDuplicateVertices(); // 0xa7f4a0: per group, equal position / normal / source tuples share one vertex
    void ClassifyGroups();         // 0xa820a0: the groups' influence / morph / word-list flag bits
    void MergeGroups();            // 0xa7ed00: groups of equal flags become one; the runs follow
    void RebuildRuns();            // 0xa7f0b0: each group's triangles regrouped by name, one run per (group, name)
};
#pragma warning(pop)

} // namespace nGZSceneGraph
