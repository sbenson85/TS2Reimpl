#pragma once
// nGZSceneGraph::cGeometryDataContainer as a whole class (lane PA, scenegraph D1b): the GMDC resource, a mesh's geometry
// data (class 0xac4f8687 of the scene graph's COM director). Standalone shape (docs/standalone-shape.md), on our
// cSGResource:
//
//   class cGeometryDataContainer : public cSGResource, public cIGeometryDataContainer
//
//   +0x00 the cSGResource part (vtable 0xe731ec, 13 slots: QI 0xa71150, the cGZPersistResource key slots, the deleting
//         destructor 0xa6f7d0); vbptr +0x1c, name +0x20, segment +0x24, flags +0x28 / +0x29
//   +0x2c the face cIGeometryDataContainer (vtable 0xe73224, 59 slots; the hook-shaped bodies of
//         src/sg/cGeometryDataContainer.h take it as `this`)
//   +0x30 mElements       the vertex elements (48 bytes each: header words, the data bytes, an index list)
//   +0x3c mLinkages       the linkages (60 bytes: which elements a group draws from, three alias lists)
//   +0x48 mGroups         the primitive groups (76 bytes: name, vertex indices, bone indices)
//   +0x54 mTransforms     the bone transforms (28 bytes: translation, then a rotation quaternion)
//   +0x60 mNamePairs      pairs of names (48 bytes: two std::string)
//   +0x6c mSubsetMeshes   triangle sets (counted, null allowed)
//   +0x78 mBoundingMesh   the bounding triangle set (counted)
//   +0x7c the vtordisp, +0x80 the virtual base cISGResource (vtable 0xe73314, 25 slots; slots 18 / 19 answer "_gmdc" /
//         "5gd")
//
// The game's STL is MSVC's own (vector / string layouts and growth), so the members are std::vector / std::string and
// the layout comes from the declaration (static_asserts in the .cpp).
//
// The switch: the factory 0xa70aa0 (the director's for class 0xac4f8687) is hooked to Create. The game's constructor
// 0xa6ed50 has two callers, that factory and the game's Clone (slot 56, only its own vtable holds it), so with the
// factory ours the game's constructor, destructor and slot bodies run no more (in effect through callers).
//
// The triangle sets are nGZCompGeom::cTriangleSetShape (class / interface 0xfcfe5320, the director's factory 0x196560),
// the geometry library's (scenegraph G0b): this class makes them through the COM and fills them with that library's
// functions, called as originals.
#include "sg/cGeometryIterators.h"
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
} // namespace ifaces

namespace nGZCompGeom {

struct cTriangleSetVertex {
    float x, y, z;
};

// The start of a triangle set (the shape's +4, what its slot 41 answers): the vertices and the triangles' corner
// indices (three per triangle), both std::vector in the game. The rest (the bounding tree) is the library's.
struct cTriangleSetData {
    cTriangleSetVertex *mVertexBegin; // +0
    cTriangleSetVertex *mVertexEnd;   // +4
    cTriangleSetVertex *mVertexCap;   // +8
    uint16_t *mIndexBegin;            // +0xc
    uint16_t *mIndexEnd;              // +0x10
    uint16_t *mIndexCap;              // +0x14
};

// nGZCompGeom::cITriangleSetShape (vtable 0xda1e0c, 43 slots); only what the container calls is named.
class cITriangleSetShape : public cIGZUnknown {
public:
    static constexpr uint32_t kClassId = 0xfcfe5320; // the director's class id, also the interface id

    // 3 (0x1963b0, ret 8): a new shape with a copy of this one's data, queried for `iid` into *out.
    virtual bool CloneAs(uint32_t iid, void **out) = 0;
    virtual void vf4() = 0;
    virtual void vf5(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf6(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf7(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf8(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf9(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf10(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf11(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf12(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf13(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf14(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf15(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf16(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf17(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf18(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf19(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf20(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf21(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf22(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf23(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf24(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf25(uint32_t) = 0; // ret 4
    virtual void vf26(uint32_t) = 0; // ret 4
    virtual void vf27(uint32_t) = 0; // ret 4
    virtual void vf28() = 0;
    virtual void vf29(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf30(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf31(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf32(uint32_t, uint32_t, uint32_t) = 0; // ret 12
    virtual void vf33(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf34(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf35(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf36() = 0;
    virtual void vf37(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf38(uint32_t, uint32_t) = 0; // ret 8
    virtual void vf39(uint32_t) = 0; // ret 4
    virtual cTriangleSetData *GetTriangleSetConst() = 0; // 40 (shared 0x18ea60: this + 4)
    virtual cTriangleSetData *GetTriangleSet() = 0;      // 41 (shared 0x18ea60)
};

} // namespace nGZCompGeom

namespace nGZSceneGraph {

using nGZCompGeom::cITriangleSetShape;

// A vertex element (48 bytes).
struct cGeometryElement {
    uint32_t mIndex;        // +0: its position (Read, AddElement and RemoveElements renumber)
    uint32_t mNumber;       // +4
    uint32_t mIdentity;     // +8
    uint32_t mRepetition;   // +0xc
    uint32_t mBlockFormat;  // +0x10
    uint32_t mSetFormat;    // +0x14
    std::vector<uint8_t> mData;      // +0x18
    std::vector<uint16_t> mIndices;  // +0x24
};

// Slot 32's copy: the element's first 24 bytes.
struct cGeometryElementHeader {
    uint32_t mIndex, mNumber, mIdentity, mRepetition, mBlockFormat, mSetFormat;
};

// A linkage (60 bytes).
struct cGeometryLinkage {
    uint32_t mIndex;                         // +0
    uint32_t mActiveCount;                   // +4: AddLinkageElement and RemoveElements keep it the list's size
    uint32_t mReferencedSize;                // +8: what slot 42 counts to when an alias list is empty
    std::vector<uint16_t> mElementIndices;   // +0xc
    std::vector<uint16_t> mAliases[3];       // +0x18 position, +0x24 surface (normal), +0x30 material (texture) classes
};

// Slot 40's copy: the linkage's three words.
struct cGeometryLinkageHeader {
    uint32_t mIndex, mActiveCount, mReferencedSize;
};

// A primitive group (76 bytes).
struct cGeometryGroup {
    uint32_t mIndex;        // +0
    uint32_t mPrimType;     // +4
    uint32_t mIndexCount;   // +8: the vertex index count (slot 21, Read)
    uint32_t mLinkIndex;    // +0xc
    int32_t mWord10;        // +0x10: read and written as a signed 32-bit value (meaning unknown)
    uint32_t mWord14;       // +0x14: zeroed by Read and AddGroup, never read
    uint32_t mBoneCount;    // +0x18: Write writes the bones only when it is not zero
    std::string mName;                       // +0x1c
    std::vector<uint16_t> mVertexIndices;    // +0x34
    std::vector<uint16_t> mBones;            // +0x40
};

// Slot 45's copy: the group's words, with the name's text at +0x14.
struct cGeometryGroupInfo {
    uint32_t mIndex, mPrimType, mIndexCount, mLinkIndex;
    int32_t mWord10;
    const char *mName;
    uint32_t mBoneCount;
};

// A bone transform (28 bytes).
struct cGeometryTransform {
    float mTranslation[3];  // +0
    float mRotation[4];     // +0xc: a quaternion (x, y, z, w); Read's default is (0, 0, 0, 1)
};

using cGeometryNamePair = std::pair<std::string, std::string>;

// An element of the subset mesh vector: a counted triangle set (null allowed).
class cGeometryMeshRef {
public:
    cGeometryMeshRef() = default;
    cGeometryMeshRef(const cGeometryMeshRef &other) : mMesh(other.mMesh) { if (mMesh) mMesh->AddRef(); }
    cGeometryMeshRef(cGeometryMeshRef &&other) noexcept : mMesh(other.mMesh) { other.mMesh = nullptr; }
    cGeometryMeshRef &operator=(const cGeometryMeshRef &other)
    {
        if (other.mMesh) other.mMesh->AddRef();
        if (mMesh) mMesh->Release();
        mMesh = other.mMesh;
        return *this;
    }
    cGeometryMeshRef &operator=(cGeometryMeshRef &&other) noexcept
    {
        if (this != &other) {
            if (mMesh) mMesh->Release();
            mMesh = other.mMesh;
            other.mMesh = nullptr;
        }
        return *this;
    }
    ~cGeometryMeshRef() { if (mMesh) mMesh->Release(); }

    cITriangleSetShape *mMesh = nullptr;
};

// nGZSceneGraph::cIGeometryDataContainer (interface id 0x5bafb147; vtable 0xe73224). Slot names from what the bodies do
// (the game's names are not known). Slots that hand out a structure take the out pointer first and return it (the game's
// hidden pointer: same ABI).
class cIGeometryDataContainer : public cIGZPersistSerializableReferent {
public:
    static constexpr uint32_t kIid = 0x5bafb147;

    virtual cISGResource *GetResourceConst() = 0;  // 7 (shared 0x3a4f50): the virtual base
    virtual cISGResource *GetResource() = 0;       // 8 (shared 0x3a4f50)

    // ---- elements ----
    virtual uint32_t AddElement(uint32_t identity, uint32_t repetition, uint32_t blockFormat, uint32_t setFormat) = 0; // 9
    virtual void RemoveElements(uint32_t identity, uint32_t blockFormat, uint32_t setFormat) = 0;                       // 10
    virtual void SetElementSetFormat(uint32_t index, uint32_t value) = 0;                                               // 11
    virtual void SetElementData(uint32_t index, uint32_t number, const void *data, uint32_t size, const uint16_t *indices,
                                uint32_t indexCount) = 0;                                                               // 12
    // ---- linkages ----
    virtual void AddLinkage() = 0;                                                                   // 13
    virtual uint32_t AddLinkageElement(uint32_t index, uint16_t element) = 0;                        // 14
    virtual void SetLinkageElement(uint32_t index, uint32_t position, uint16_t element) = 0;         // 15 (0xa72fb0) eax-ignored: eax is the position argument loaded as the store's index, not a result
    virtual void SetLinkageReferencedSize(uint32_t index, uint32_t value) = 0;                       // 16
    virtual void SetLinkageAliases(uint32_t index, uint32_t which, const uint16_t *data, uint32_t count) = 0; // 17
    virtual void ClearLinkageAliases(uint32_t index, uint32_t which) = 0;                            // 18
    // ---- groups, transforms, names, meshes ----
    virtual void ReserveGroups(uint32_t count) = 0;                                                  // 19
    virtual void AddGroup(uint32_t primType, const char *name, uint32_t linkIndex, int32_t word10) = 0; // 20
    virtual void SetGroupVertexIndices(uint32_t index, const uint16_t *data, uint32_t count) = 0;    // 21
    virtual void SetGroupVertexIndicesAndWord(uint32_t index, const uint16_t *data, uint32_t count, int32_t word10) = 0; // 22
    virtual void SetGroupVertexIndicesAndBones(uint32_t index, const uint16_t *data, uint32_t count,
                                               const uint16_t *bones, uint32_t boneCount) = 0;    // 23
    virtual void AddTransform(const cGeometryTransform *transform) = 0;                             // 24
    virtual void AddNamePair(const char *first, const char *second) = 0;                            // 25
    virtual void SetBoundingMesh(cITriangleSetShape *mesh) = 0;                                     // 26 (shared 0x1fc100)
    virtual void SetSubsetMesh(uint32_t index, cITriangleSetShape *mesh) = 0;                       // 27
    virtual void ClearLinkages() = 0;                                                                // 28
    virtual void ClearElements() = 0;                                                                // 29
    virtual void ClearGroups() = 0;                                                                  // 30
    // ---- queries ----
    virtual uint32_t CountElements() = 0;                                                            // 31
    virtual cGeometryElementHeader *GetElementHeader(cGeometryElementHeader *out, uint32_t index) = 0; // 32
    virtual cGeometryRange<float> *GetElementFloats(cGeometryRange<float> *out, uint32_t index) = 0;   // 33
    virtual cGeometryRange<cGeometryVector<2>> *GetElementVectors2(cGeometryRange<cGeometryVector<2>> *out,
                                                                   uint32_t index) = 0;               // 34
    virtual cGeometryRange<cGeometryVector<3>> *GetElementVectors3(cGeometryRange<cGeometryVector<3>> *out,
                                                                   uint32_t index) = 0;               // 35
    virtual cGeometryRange<cGeometryVector<4>> *GetElementVectors4(cGeometryRange<cGeometryVector<4>> *out,
                                                                   uint32_t index) = 0;               // 36
    virtual cGeometryRange<uint32_t> *GetElementWords(cGeometryRange<uint32_t> *out, uint32_t index) = 0; // 37
    virtual cGeometryRange<uint16_t> *GetElementIndices(cGeometryRange<uint16_t> *out, uint32_t index) = 0; // 38
    virtual uint32_t CountLinkages() = 0;                                                            // 39
    virtual cGeometryLinkageHeader *GetLinkageHeader(cGeometryLinkageHeader *out, uint32_t index) = 0; // 40
    virtual cGeometryRange<uint16_t> *GetLinkageElements(cGeometryRange<uint16_t> *out, uint32_t index) = 0; // 41
    virtual cGeometryRange<uint16_t> *GetLinkageAliases(cGeometryRange<uint16_t> *out, uint32_t index,
                                                        uint32_t which) = 0;                          // 42
    virtual uint8_t LinkageHas(uint32_t index, uint32_t which) = 0;                                  // 43
    virtual uint32_t CountGroups() = 0;                                                              // 44
    virtual cGeometryGroupInfo *GetGroupInfo(cGeometryGroupInfo *out, uint32_t index) = 0;           // 45
    virtual cGeometryRange<uint16_t> *GetGroupVertexIndices(cGeometryRange<uint16_t> *out, uint32_t index) = 0; // 46
    virtual cGeometryRange<uint16_t> *GetGroupBones(cGeometryRange<uint16_t> *out, uint32_t index) = 0;         // 47
    virtual uint32_t CountTransforms() = 0;                                                          // 48
    virtual cGeometryTransform *GetTransform(cGeometryTransform *out, uint32_t index) = 0;           // 49
    virtual uint32_t CountNamePairs() = 0;                                                           // 50
    virtual const char *GetNameFirst(uint32_t index) = 0;                                            // 51
    virtual const char *GetNameSecond(uint32_t index) = 0;                                           // 52
    virtual cITriangleSetShape *GetBoundingMesh() = 0;                                               // 53 (shared 0x169f60)
    virtual cITriangleSetShape *GetSubsetMesh(uint32_t index) = 0;                                   // 54
    virtual void Slot55() = 0;                                                                       // 55 (shared 0x140fe0)
    virtual bool Clone(uint32_t iid, void **out) = 0;                                                // 56
    virtual void CopyLinkageTo(uint32_t index, cIGeometryDataContainer *other, uint32_t otherIndex) = 0; // 57
    virtual void CopyGroupTo(uint32_t index, cIGeometryDataContainer *other, uint32_t linkIndex) = 0;    // 58
};

class cIGeometryDataContainerSub;

class cGeometryDataContainer : public cSGResource, public cIGeometryDataContainer {
public:
    static constexpr size_t kSize = 0x84;
    static constexpr uint32_t kClassId = 0xac4f8687; // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 4;          // the type-info record's version

    cGeometryDataContainer() = default;  // 0xa6ed50: the cSGResource part, every member empty
    ~cGeometryDataContainer() override;  // 0xa6f4c0 (deleting 0xa6f7d0, 0x84 bytes)

    // 0xa70aa0 (cdecl; the director's factory, hooked): a new container as its cISGResource face, uncounted.
    static cISGResource *__cdecl Create();
    // 0xa70b10 (the director's class-id getter for this class): 0xac4f8687.
    static uint32_t __cdecl GetClassId();

    // ---- cIGZUnknown (every face) ----
    // 0xa71150 (ret 8): a null out gives false; cIGeometryDataContainer is the face, counted; else the cSGResource part's.
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;  // shared 0xa10d60 -> the cSGResource part's
    uint32_t Release() override; // shared 0xa170d0 -> the cSGResource part's (the deleting destructor at 0)

    // ---- cIGZPersistSerializableReferent (the face's 3-6, and the virtual base's) ----
    void Read(ifaces::cIGZPersistReader *reader) override;   // 0xa711b0
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa72fe0: {"cGeometryDataContainer", 0xac4f8687, 4}
    void Write(ifaces::cIGZPersistWriter *writer) override;  // 0xa73920
    uint32_t GetOwnerInfo() override;                        // shared 0xa72c90: the cSGResource part's (0xa3c750)

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 18 (0xa70de0): "_gmdc"
    const void *GetRecord19() override; // 19 (0xa70dd0): "5gd"

    // ---- cIGeometryDataContainer ----
    cISGResource *GetResourceConst() override;
    cISGResource *GetResource() override;
    uint32_t AddElement(uint32_t identity, uint32_t repetition, uint32_t blockFormat, uint32_t setFormat) override; // 0xa6f800
    void RemoveElements(uint32_t identity, uint32_t blockFormat, uint32_t setFormat) override;                       // 0xa72520
    void SetElementSetFormat(uint32_t index, uint32_t value) override;                                               // 0xa72d20
    void SetElementData(uint32_t index, uint32_t number, const void *data, uint32_t size, const uint16_t *indices,
                        uint32_t indexCount) override;                                                               // 0xa72d40
    void AddLinkage() override;                                                                                     // 0xa6f930
    uint32_t AddLinkageElement(uint32_t index, uint16_t element) override;                                          // 0xa6ff70
    void SetLinkageElement(uint32_t index, uint32_t position, uint16_t element) override;                           // 0xa72fb0 eax-ignored: eax is the position argument loaded as the store's index, not a result
    void SetLinkageReferencedSize(uint32_t index, uint32_t value) override;                                         // 0xa72dd0
    void SetLinkageAliases(uint32_t index, uint32_t which, const uint16_t *data, uint32_t count) override;          // 0xa72f00
    void ClearLinkageAliases(uint32_t index, uint32_t which) override;                                              // 0xa700b0
    void ReserveGroups(uint32_t count) override;                                                                    // 0xa72c30
    void AddGroup(uint32_t primType, const char *name, uint32_t linkIndex, int32_t word10) override;                // 0xa6fdc0
    void SetGroupVertexIndices(uint32_t index, const uint16_t *data, uint32_t count) override;                      // 0xa72e30
    void SetGroupVertexIndicesAndWord(uint32_t index, const uint16_t *data, uint32_t count, int32_t word10) override; // 0xa72e70
    void SetGroupVertexIndicesAndBones(uint32_t index, const uint16_t *data, uint32_t count, const uint16_t *bones,
                                       uint32_t boneCount) override;                                                // 0xa72ea0
    void AddTransform(const cGeometryTransform *transform) override;                                                // 0xa6fd80
    void AddNamePair(const char *first, const char *second) override;                                               // 0xa6fc30
    void SetBoundingMesh(cITriangleSetShape *mesh) override;                                                        // shared 0x1fc100
    void SetSubsetMesh(uint32_t index, cITriangleSetShape *mesh) override;                                          // 0xa72cc0
    void ClearLinkages() override;                                                                                  // 0xa724c0
    void ClearElements() override;                                                                                  // 0xa72490
    void ClearGroups() override;                                                                                    // 0xa724f0
    uint32_t CountElements() override;                                                                              // 0xa70e10
    cGeometryElementHeader *GetElementHeader(cGeometryElementHeader *out, uint32_t index) override;                 // 0xa708d0
    cGeometryRange<float> *GetElementFloats(cGeometryRange<float> *out, uint32_t index) override;                   // 0xa70920
    cGeometryRange<cGeometryVector<2>> *GetElementVectors2(cGeometryRange<cGeometryVector<2>> *out, uint32_t index) override; // 0xa730c0
    cGeometryRange<cGeometryVector<3>> *GetElementVectors3(cGeometryRange<cGeometryVector<3>> *out, uint32_t index) override; // 0xa73240
    cGeometryRange<cGeometryVector<4>> *GetElementVectors4(cGeometryRange<cGeometryVector<4>> *out, uint32_t index) override; // 0xa733c0
    cGeometryRange<uint32_t> *GetElementWords(cGeometryRange<uint32_t> *out, uint32_t index) override;              // 0xa70bc0
    cGeometryRange<uint16_t> *GetElementIndices(cGeometryRange<uint16_t> *out, uint32_t index) override;            // 0xa70750
    uint32_t CountLinkages() override;                                                                              // 0xa70e30
    cGeometryLinkageHeader *GetLinkageHeader(cGeometryLinkageHeader *out, uint32_t index) override;                 // 0xa70d50
    cGeometryRange<uint16_t> *GetLinkageElements(cGeometryRange<uint16_t> *out, uint32_t index) override;           // 0xa73820
    cGeometryRange<uint16_t> *GetLinkageAliases(cGeometryRange<uint16_t> *out, uint32_t index, uint32_t which) override; // 0xa73540
    uint8_t LinkageHas(uint32_t index, uint32_t which) override;                                                    // 0xa70b30
    uint32_t CountGroups() override;                                                                                // 0xa70e90
    cGeometryGroupInfo *GetGroupInfo(cGeometryGroupInfo *out, uint32_t index) override;                             // 0xa71000
    cGeometryRange<uint16_t> *GetGroupVertexIndices(cGeometryRange<uint16_t> *out, uint32_t index) override;        // 0xa71050
    cGeometryRange<uint16_t> *GetGroupBones(cGeometryRange<uint16_t> *out, uint32_t index) override;                // 0xa70f10
    uint32_t CountTransforms() override;                                                                            // 0xa70e70
    cGeometryTransform *GetTransform(cGeometryTransform *out, uint32_t index) override;                             // 0xa70eb0
    uint32_t CountNamePairs() override;                                                                             // 0xa70e50
    const char *GetNameFirst(uint32_t index) override;                                                              // 0xa70da0
    const char *GetNameSecond(uint32_t index) override;                                                             // 0xa70d80
    cITriangleSetShape *GetBoundingMesh() override;                                                                 // shared 0x169f60
    cITriangleSetShape *GetSubsetMesh(uint32_t index) override;                                                     // 0xa70000
    void Slot55() override;                                                                                         // shared 0x140fe0
    bool Clone(uint32_t iid, void **out) override;                                                                  // 0xa702e0
    void CopyLinkageTo(uint32_t index, cIGeometryDataContainer *other, uint32_t otherIndex) override;               // 0xa70570
    void CopyGroupTo(uint32_t index, cIGeometryDataContainer *other, uint32_t linkIndex) override;                  // 0xa704c0

    // The hook-shaped bodies (src/sg/cGeometryDataContainer.h) take the face as `this`.
    cIGeometryDataContainerSub *Sub();

    std::vector<cGeometryElement> mElements;      // +0x30
    std::vector<cGeometryLinkage> mLinkages;      // +0x3c
    std::vector<cGeometryGroup> mGroups;          // +0x48
    std::vector<cGeometryTransform> mTransforms;  // +0x54
    std::vector<cGeometryNamePair> mNamePairs;    // +0x60
    std::vector<cGeometryMeshRef> mSubsetMeshes;  // +0x6c
    cITriangleSetShape *mBoundingMesh = nullptr;  // +0x78 counted
};

// Host diagnostic hook (milestone N-f, src/hook/gmdc_diag.cpp): null unless the host's TS2HOST_GMDC switch installed an
// observer. Read calls it once after a container is decoded, with the resource's key; off, that is one load and one
// test. Nothing in the decode reads it otherwise.
using DecodeObserver = void (*)(const cGeometryDataContainer &container, const cGZPersistResourceKey &key);
extern DecodeObserver gDecodeObserver;

// The reader helpers (cdecl; the label names the list for text readers and is not read here).
// 0xa71fc0: `count` indices into dst; before version 3 (signed compare) as 32-bit values (reader slot 18) kept to 16 bits,
// from version 3 on as 16-bit values (slot 17).
void __cdecl ReadGeometryIndexList(const char *label, uint16_t *dst, uint32_t count, int32_t version,
                                   ifaces::cIGZPersistReader *reader);
// 0xa72320: `count` vertices (3 floats each, reader slot 20) into the triangle set.
void __cdecl ReadGeometryVertices(const char *label, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                  ifaces::cIGZPersistReader *reader);
// 0xa720e0 (version 4 on): `count` triangles, three 16-bit corners each (reader slot 17).
void __cdecl ReadGeometryTriangles16(const char *label, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                     ifaces::cIGZPersistReader *reader);
// 0xa72200 (before version 4): `count` triangles, three 32-bit corners each (reader slot 19), kept to 16 bits.
void __cdecl ReadGeometryTriangles32(const char *label, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                     ifaces::cIGZPersistReader *reader);

} // namespace nGZSceneGraph
