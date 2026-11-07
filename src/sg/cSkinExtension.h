#pragma once
// nGZSceneGraph::cSkinExtension (lane QP, scenegraph D3 part 1): the extension a skinned shape keeps for its skin: a
// name, a word, a list of counted pointer pairs, a list of lowercased name pairs and a list of counted pointers. Class
// id 0xfca0e8b3 (the COM director's factory 0xa97720), interface cISkinExtension 0x3ca0e8ab; the extension's type name
// (cIExtension slot 25) is "skin". What the lists hold for the skinning code is not known yet (tentative names: the
// pointers are only counted here, never asked for an interface).
//
// The game's object (0x74 bytes) is cExtension's part (src/sg/cExtension.h), then:
//   +0x18 mName   cRZString (the game's string class, 0x20)       +0x38 mWord uint32 (0 when made)
//   +0x3c mPairs  vector of {counted pointer, counted pointer} (8-byte elements)
//   +0x48 mNameMap vector of 0x40-byte elements: two cRZStrings {name, key}, both lowercased
//   +0x54 mRefs   vector of counted pointers
//   +0x60 the vtordisp, +0x64 the virtual base cIExtension (vtable 0xe74dc0)
//   +0x68 the vtordisp, +0x6c the virtual base cISkinExtension (vtable 0xe74e30, 20 slots; its own vbptr at +0x70)
// Ours is our compiler's class with the same bases (cBoneDataExtension's shape). The vectors are our toolchain's
// std::vector over our own counted pointer (same sizes); mNameMap's elements are std::string pairs: nothing but the
// class's own slots reads them. It is a leaf: the factory 0xa97720 is hooked and nothing else is (every other body is
// reached only through the vtables our objects replace, or from the constructors; status entries "in effect through
// callers").
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "sg/cExtension.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nGZSceneGraph {

// nGZSceneGraph::cISkinExtension (interface id 0x3ca0e8ab): 20 slots of its own (its cIExtension is a virtual base).
// Slots 1, 10, 14 and 19 are folded accessors shared with other classes in the game (shared_vf_*); 2/3 and 4/5 are
// one body each (two declarations, probably const and non-const).
class cISkinExtension : public virtual cIExtension {
public:
    static constexpr uint32_t kIid = 0x3ca0e8ab;

    virtual void SetName(const char *name) = 0;                              // 0
    virtual void SetWord(uint32_t value) = 0;                                // 1
    virtual cIGZUnknown *GetPairFirstConst(uint32_t index) = 0;              // 2 (raw, not counted)
    virtual cIGZUnknown *GetPairFirst(uint32_t index) = 0;                   // 3
    virtual cIGZUnknown *GetPairSecondConst(uint32_t index) = 0;             // 4 (raw, not counted)
    virtual cIGZUnknown *GetPairSecond(uint32_t index) = 0;                  // 5
    virtual void AddPair(cIGZUnknown *first, cIGZUnknown *second) = 0;       // 6: both counted
    virtual void ErasePair(uint32_t index) = 0;                              // 7
    virtual void SetPairSecond(uint32_t index, cIGZUnknown *second) = 0;     // 8
    virtual void MapName(const char *name, const char *key) = 0;             // 9: see the body
    virtual cIGZUnknown *GetRef(uint32_t index) = 0;                         // 10 (raw, not counted)
    virtual void PushRef(cIGZUnknown *ref) = 0;                              // 11: counted
    virtual void EraseRef(uint32_t index) = 0;                               // 12
    virtual const char *GetName() = 0;                                       // 13
    virtual uint32_t GetWord() = 0;                                          // 14
    virtual uint32_t GetPairCount() = 0;                                     // 15
    virtual const char *GetMappedName(uint32_t index) = 0;                   // 16: element's first string
    virtual const char *GetMappedKey(uint32_t index) = 0;                    // 17: element's second string
    virtual uint32_t GetNameMapCount() = 0;                                  // 18
    virtual uint32_t GetRefCount() = 0;                                      // 19

};

// A counted pointer as the game's lists hold them: copying counts the new holder, assigning counts the new pointer
// before releasing the old one (nothing when they are the same), destroying releases.
class cSkinRef {
public:
    cSkinRef(cIGZUnknown *p = nullptr) : mPtr(p) { if (mPtr) mPtr->AddRef(); }
    cSkinRef(const cSkinRef &other) : mPtr(other.mPtr) { if (mPtr) mPtr->AddRef(); }
    cSkinRef &operator=(const cSkinRef &other)
    {
        if (this != &other) Assign(other.mPtr);
        return *this;
    }
    ~cSkinRef() { if (mPtr) mPtr->Release(); }
    void Assign(cIGZUnknown *p)
    {
        cIGZUnknown *const old = mPtr;
        if (p == old) return;
        if (p) p->AddRef();
        mPtr = p;
        if (old) old->Release();
    }
    cIGZUnknown *Get() const { return mPtr; }

private:
    cIGZUnknown *mPtr;
};
static_assert(sizeof(cSkinRef) == 4, "a counted pointer is one word");

// C4250 ("inherits ... via dominance"): intended, as cDataListExtension's.
#pragma warning(push)
#pragma warning(disable : 4250)
class cSkinExtension final : public cExtension, public virtual cISkinExtension {
public:
    static constexpr uint32_t kClassId = 0xfca0e8b3;
    static constexpr uint32_t kVersion = 1; // the type-info record's version (0xa97e50)

    // 0xa97720 (__cdecl): the COM director's factory for kClassId: a new extension (count 0), as its cIExtension.
    static cIGZUnknown *__cdecl Create();

    cSkinExtension();                                   // 0xa97240
    cSkinExtension(const cSkinExtension &other);        // 0xa970e0: the name and the word only
    cSkinExtension &operator=(const cSkinExtension &) = delete;
    ~cSkinExtension() override;                         // 0xa97340 (deleting 0xa97490)

    // ---- the referent face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa97af0: kIid, else cExtension's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa97b50
    cPersistTypeInfo GetTypeInfo() override;                // 0xa97e50: {"cSkinExtension", kClassId, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa97f50

    // ---- cIExtension ----
    cIExtension *Clone() override;                          // 15: 0xa97660
    bool IsKindOf(uint32_t iid) override;                   // 23: 0xa97e30
    const char *GetTypeName() override;                     // 25: 0xa97700, "skin"

    // ---- cISkinExtension ----
    // The game tail-calls std::string::assign, so eax is the name string's own address (an artefact).
    void SetName(const char *name) override; // 0xa97da5 eax-ignored: a setter, reached only through the vtable
    void SetWord(uint32_t value) override;
    cIGZUnknown *GetPairFirstConst(uint32_t index) override; // 0xa97dde
    cIGZUnknown *GetPairFirst(uint32_t index) override;
    cIGZUnknown *GetPairSecondConst(uint32_t index) override; // 0xa97dc3
    cIGZUnknown *GetPairSecond(uint32_t index) override;
    void AddPair(cIGZUnknown *first, cIGZUnknown *second) override; // 0xa974c0
    void ErasePair(uint32_t index) override;                // 0xa97be0
    void SetPairSecond(uint32_t index, cIGZUnknown *second) override; // 0xa97efe
    void MapName(const char *name, const char *key) override; // 0xa97880
    cIGZUnknown *GetRef(uint32_t index) override;
    void PushRef(cIGZUnknown *ref) override;                // 0xa975a0
    void EraseRef(uint32_t index) override;                 // 0xa97ce0
    const char *GetName() override;                         // 0xa977d2
    uint32_t GetWord() override;
    uint32_t GetPairCount() override;                       // 0xa9782a
    const char *GetMappedName(uint32_t index) override;     // 0xa977e9
    const char *GetMappedKey(uint32_t index) override;      // 0xa97dfd
    uint32_t GetNameMapCount() override;                    // 0xa9784a
    uint32_t GetRefCount() override;

private:
    union { cRZString mName; };                             // +0x18: built and destroyed by hand (Construct)
    uint32_t mWord = 0;                                     // +0x38
    std::vector<std::pair<cSkinRef, cSkinRef>> mPairs;      // +0x3c
    std::vector<std::pair<std::string, std::string>> mNameMap; // +0x48 (the game's: pairs of cRZStrings)
    std::vector<cSkinRef> mRefs;                            // +0x54
};
#pragma warning(pop)

// 0xaea060 (__cdecl, lane VS): `node`'s skin extension after `previous` (its first when null), searched from the
// extension after `previous` (null when there is none: then no search). Not counted: the search's count is released
// before the answer is returned (the node keeps the extension).
cISkinExtension *__cdecl NextSkinExtension(cIObjectGraphNode *node, cISkinExtension *previous);

} // namespace nGZSceneGraph
