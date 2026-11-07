#pragma once
// nGZSceneGraph::cMaterialDefinition as a whole class (lane RP, scenegraph M2a): the TXMT resource, a material's name, type,
// properties and texture names (class 0x49596978 of the scene graph's COM director). Standalone shape
// (docs/standalone-shape.md), on our cSGResource:
//
//   class cMaterialDefinition : public cSGResource, public cIMaterialDefinition
//
//   +0x00 the cSGResource part (vtable 0xe76240: QI 0xaae210, the cGZPersistResource key slots, the deleting destructor
//         0xaad530 at slot 12); vbptr +0x1c, name +0x20, segment +0x24, flags +0x28 / +0x29
//   +0x2c the face cIMaterialDefinition (vtable 0xe76278, 27 slots, interface 0x49596972)
//   +0x30 mName         the material's name, a string-pool cell (case-insensitive: 0xae87d0)
//   +0x34 mType         the material's type, a string-pool cell (case-sensitive: 0xae88d0)
//   +0x38 mProperties   {name cell, value cell} pairs sorted by the name's text (strcmp)
//   +0x44 mTextures     {name cell, texture handle} entries sorted by the name's text (strcmp)
//   +0x50 the vtordisp, +0x54 the virtual base cISGResource (vtable 0xe762e8, 25 slots; 18 / 19 answer "_txmt" / "5tm")
//
// Every container is MSVC's own (the game's STL is the toolchain's: insert is 0xaaab80 / 0xaaf270, equal_range 0xaacea0),
// so the members are std::vector and the layout comes from the declaration (static_asserts in the .cpp).
//
// The switch: the director's factory 0xaadf20 (class 0x49596978) is hooked to Create. The game's constructor 0xaad1c0
// has two callers, that factory and the game's Clone (slot 7, only its own vtable holds it), so with the factory ours no
// game object carries the vtables 0xe76240 / 0xe76278 / 0xe762e8 and every slot is ours through its callers.
//
// The entry points are ours too (lane RR; docs/subsystems/scenegraph.md "M2a part 2"): Read 0xaae280 (slot 3), Write
// 0xaaefc0 (slot 5) and slot 14 0xaad850 (MergeVectorProperties). The texture manager's getter and handle operations,
// and 0xae7340 (a "##" / group-prefixed text resolved into a string), are ours (sg/cTextureManager.h, lane SA).
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "sg/cSGResource.h"
#include "sg/cMaterialParser.h" // OwnedCell
#include "sg/cTextureManager.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// A counted texture handle of the texture manager (4 bytes, 0xffffffff when empty; sg/cTextureManager.h): copy
// 0xae8240, assign 0xae82e0, destroy 0xae82b0 (a reset, 0xae8710).
using MaterialTextureHandle = cTextureHandle;

// A property: the name's cell, then the value's (both case-sensitive, 0xae88d0).
struct MaterialProperty {
    OwnedCell mName;
    OwnedCell mValue;
};
static_assert(sizeof(MaterialProperty) == 8, "property layout");

// A texture: the name's cell (case-insensitive, 0xae87d0), then its handle.
struct MaterialTexture {
    OwnedCell mName;
    MaterialTextureHandle mHandle;
};
static_assert(sizeof(MaterialTexture) == 8, "texture layout");

// What GetProperty (slot 22) writes: the name's text, then the value's.
struct MaterialPropertyText {
    const char *mName;
    const char *mValue;
};

// nGZSceneGraph::cIMaterialDefinition (interface id 0x49596972), the face at +0x2c.
class cIMaterialDefinition : public cIGZPersistSerializableReferent {
public:
    static constexpr uint32_t kIid = 0x49596972;

    virtual cIMaterialDefinition *Clone() = 0;                                              // 7: uncounted
    virtual cISGResource *GetResourceConst() = 0;                                           // 8 (shared 0x3a4f50)
    virtual cISGResource *GetResource() = 0;                                                // 9 (shared 0x3a4f50)
    virtual void SetMaterialName(const char *name) = 0;                                     // 10
    virtual void ClearProperties() = 0;                                                     // 11
    virtual void SetMaterialType(const char *type) = 0;                                     // 12
    virtual void SetProperty(const char *name, const char *value) = 0;                      // 13
    virtual bool MergeVectorProperties() = 0;                                               // 14
    virtual void AddTextureById(uint32_t id) = 0;                                           // 15
    virtual void AddTextureByName(const char *name) = 0;                                    // 16
    virtual bool IsEqual(cIMaterialDefinition *other) = 0;                                  // 17
    virtual bool IsEqualProperties(cIMaterialDefinition *other) = 0;                        // 18
    virtual const char *GetMaterialName() = 0;                                              // 19
    virtual const char *GetMaterialType() = 0;                                              // 20
    virtual uint32_t CountProperties() = 0;                                                 // 21
    virtual MaterialPropertyText *GetProperty(MaterialPropertyText *out, uint32_t index) = 0; // 22: returns out
    virtual const char *FindProperty(const char *name) = 0;                                 // 23
    virtual const char *FindPropertyNoCase(const char *name) = 0;                           // 24
    virtual uint32_t CountTextures() = 0;                                                   // 25
    virtual const char *GetTextureName(uint32_t index) = 0;                                 // 26
};

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x58>, 10f6704); // cMaterialDefinition's pool (operator new / delete)
class cMaterialDefinition : public cSGResource, public cIMaterialDefinition {
public:
    static constexpr size_t kSize = 0x58;
    static constexpr uint32_t kClassId = 0x49596978;   // the director's class id, and the type-info record's word
    static constexpr uint32_t kVersion = 0xb;          // the type-info record's version
    static constexpr uint32_t kIidSelf = 0x4db89b60;   // QueryInterface gives the object itself, uncounted
    static constexpr uint32_t kPool = 0x10f6704;       // RZFastFixed0x58: the game's allocator for this size

    cMaterialDefinition();               // 0xaad1c0
    ~cMaterialDefinition() override;     // 0xaad420 (deleting 0xaad530: the pool)

    static void *operator new(size_t size);
    static void operator delete(void *block, size_t size);

    // The director's factory (0xaadf20, __cdecl): a new object, uncounted; the face.
    static cIMaterialDefinition *__cdecl Create();

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xaae210
    uint32_t AddRef() override;
    uint32_t Release() override;

    // ---- cIGZPersistSerializableReferent ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xaae280
    cPersistTypeInfo GetTypeInfo() override;                // 0xaaef20: {"cMaterialDefinition", 0x49596978, 0xb}
    // 0xaaef20 at its RVA (thiscall with a hidden out, ret 4; `this` unused): the game's Write 0xaaefc0 calls it directly.
    static cPersistTypeInfo *__fastcall GetTypeInfoAt(void *face, void *, cPersistTypeInfo *out);
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xaaefc0
    uint32_t GetOwnerInfo() override;                       // shared 0xa72c90: the cSGResource part's

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 0xaae0d0: "_txmt"
    const void *GetRecord19() override; // 0xaae0c0: "5tm"

    // ---- cIMaterialDefinition ----
    cIMaterialDefinition *Clone() override;                                              // 0xaad790
    cISGResource *GetResourceConst() override;                                           // shared 0x3a4f50
    cISGResource *GetResource() override;                                                // shared 0x3a4f50
    void SetMaterialName(const char *name) override;                                     // 0xaaed10
    void ClearProperties() override;                                                     // 0xaad750
    void SetMaterialType(const char *type) override;                                     // 0xaaecb0
    void SetProperty(const char *name, const char *value) override;                      // 0xaaed70
    bool MergeVectorProperties() override;                                               // 0xaad850
    void AddTextureById(uint32_t id) override;                                           // 0xaad570
    void AddTextureByName(const char *name) override;                                    // 0xaad680
    bool IsEqual(cIMaterialDefinition *other) override;                                  // 0xaadf90
    bool IsEqualProperties(cIMaterialDefinition *other) override;                        // 0xaae010
    const char *GetMaterialName() override;                                              // 0xaae090
    const char *GetMaterialType() override;                                              // 0xaadee0
    uint32_t CountProperties() override;                                                 // 0xaae0f0
    MaterialPropertyText *GetProperty(MaterialPropertyText *out, uint32_t index) override; // 0xaae100
    const char *FindProperty(const char *name) override;                                 // 0xaae140
    const char *FindPropertyNoCase(const char *name) override;                           // 0xaae1c0
    uint32_t CountTextures() override;                                                   // 0xaae0e0
    const char *GetTextureName(uint32_t index) override;                                 // 0xaadf00

    cIMaterialDefinition *Face() { return static_cast<cIMaterialDefinition *>(this); }

    OwnedCell mName;                              // +0x30
    OwnedCell mType;                              // +0x34
    std::vector<MaterialProperty> mProperties;    // +0x38
    std::vector<MaterialTexture> mTextures;       // +0x44
};

// 0xae88d0 (__cdecl (cell, name)): name nonzero opens the cell case-sensitively (flag 1), else the cell's value becomes
// 0xffffffff; returns the cell. The flag-0 twin is OpenNameCell (0xae87d0, sg/cMaterialAttributes.h).
ServiceCell *OpenNameCellCaseSensitive(ServiceCell *cell, uint32_t name);

} // namespace nGZSceneGraph
