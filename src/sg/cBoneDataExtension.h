#pragma once
// nGZSceneGraph::cBoneDataExtension (lane QP, scenegraph D3 part 1): the extension a skeleton's bone node keeps for its
// joint data: three axis codes, a float and a quaternion. Class id 0xe9075bc5 (the COM director's factory 0xa98470),
// interface cIBoneDataExtension 0xe9075bbe; the extension's type name (cIExtension slot 25) is "bone_data".
//
// The game's object (0x4c bytes) is cExtension's part (src/sg/cExtension.h), then:
//   +0x18 mAxisH   int (3 when made)      +0x1c mAxisM int (0)      +0x20 mAxisS int (1)
//   +0x24 mScale   float (1.0f)           +0x28 mQuaternion float[4] (0, 0, 0, 1)
//   +0x38 the vtordisp, +0x3c the virtual base cIExtension (vtable 0xe74f88)
//   +0x40 the vtordisp, +0x44 the virtual base cIBoneDataExtension (vtable 0xe74ff8, 10 slots; its own vbptr at +0x48)
// Ours is our compiler's class with the same bases (cDataListExtension's shape, lane OY). It is a leaf: the factory
// 0xa98470 is hooked and nothing else is (every other body is reached only through the vtables our objects replace, or
// from the constructors; status entries "in effect through callers").
//
// The names H / M / S come from GetDescription's format "(H%c M%c S%c)" (each an axis letter); what the codes mean to the
// skeleton code is not known yet (tentative names).
#include "sg/cExtension.h"

#include <cstdint>

namespace nGZSceneGraph {

// nGZSceneGraph::cIBoneDataExtension (interface id 0xe9075bbe): 10 slots of its own (its cIExtension is a virtual base).
// The game's bodies are folded accessors shared with other classes (shared_vf_*), except SetQuaternion 0xa98630.
class cIBoneDataExtension : public virtual cIExtension {
public:
    static constexpr uint32_t kIid = 0xe9075bbe;

    virtual void SetAxisH(int32_t value) = 0;              // 0
    virtual void SetAxisM(int32_t value) = 0;              // 1
    virtual void SetAxisS(int32_t value) = 0;              // 2
    virtual void SetScale(float value) = 0;                // 3
    virtual void SetQuaternion(const float *value) = 0;    // 4: four floats
    virtual int32_t GetAxisH() = 0;                        // 5
    virtual int32_t GetAxisM() = 0;                        // 6
    virtual int32_t GetAxisS() = 0;                        // 7
    virtual float GetScale() = 0;                          // 8
    virtual const float *GetQuaternion() = 0;              // 9: the object's own four floats
};

// C4250 ("inherits ... via dominance"): intended, as cDataListExtension's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cBoneDataExtension final : public cExtension, public virtual cIBoneDataExtension {
public:
    static constexpr uint32_t kClassId = 0xe9075bc5;
    static constexpr uint32_t kVersion = 5; // the type-info record's version (0xa98670)
    static constexpr uint32_t kQuaternionVersion = 4; // Read takes the quaternion from records of this version on

    // 0xa98470 (__cdecl): the COM director's factory for kClassId: a new extension (count 0), as its cIExtension.
    static cIGZUnknown *__cdecl Create();

    cBoneDataExtension();                                   // 0xa980a0
    cBoneDataExtension(const cBoneDataExtension &other);    // 0xa97ff0: the values copied
    cBoneDataExtension &operator=(const cBoneDataExtension &) = delete;
    ~cBoneDataExtension() override;                         // 0xa98160 (deleting 0xa981b0)

    // ---- the referent face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa98510: kIid, else cExtension's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa98560
    cPersistTypeInfo GetTypeInfo() override;                // 0xa98670: {"cBoneDataExtension", kClassId, 5}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa98740

    // ---- cIExtension ----
    cIExtension *Clone() override;                          // 15: 0xa981f0
    bool IsKindOf(uint32_t iid) override;                   // 23: 0xa98650
    const char *GetTypeName() override;                     // 25: 0xa98460, "bone_data"
    void GetDescription(cIGZString *text) override;         // 26: 0xa98270, "(HX MY SZ)" or "(HX MY SZ:0.50)"

    // ---- cIBoneDataExtension ----
    void SetAxisH(int32_t value) override;
    void SetAxisM(int32_t value) override;
    void SetAxisS(int32_t value) override;
    void SetScale(float value) override;
    void SetQuaternion(const float *value) override;        // 0xa98630
    int32_t GetAxisH() override;
    int32_t GetAxisM() override;
    int32_t GetAxisS() override;
    float GetScale() override;
    const float *GetQuaternion() override;

private:
    int32_t mAxisH = 3;                         // +0x18
    int32_t mAxisM = 0;                         // +0x1c
    int32_t mAxisS = 1;                         // +0x20
    float mScale = 1.0f;                        // +0x24
    float mQuaternion[4] = {0, 0, 0, 1.0f};     // +0x28
};
#pragma warning(pop)

} // namespace nGZSceneGraph
