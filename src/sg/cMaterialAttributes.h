#pragma once
// nGZSceneGraph::cMaterialAttributes (lane RO, scenegraph M1b part 5): the named, typed attribute lists of a material.
// 0x28 bytes, one face (vtable 0xe76188, 16 slots), made only by the factory 0xaaa3c0 (its address is stored by the
// cDllCOMDirector constructor) through the constructor 0xaa9e40; the vtable is stored by that constructor and by the
// destructor 0xaa9fe0 (called by the deleting destructor 0xaaa0e0, slot 15) and by nobody else.
//
// The members. The original's std layout is not needed by anything: no code of the image reads the fields (the face's
// slots are the only access, and the object is built and freed by the slots' own code), so the members are our own
// containers (std::string, std::map, std::vector of the toolchain). The release layout of those happens to equal the
// original's (static_asserts below), which the tests use to walk the game's object raw.
//
//   +0   vtable
//   +4   reference count (0 at construction)
//   +8   mName       std::string (0x18): the material's name
//   +0x20 mGroups    std::map<uint32_t, std::vector<Attribute>> (head pointer, size): attributes by group key
//
// An Attribute (0x28): the name (std::string), the width (the source's slot 5; the group's first attribute's width
// multiplies the counts and offsets), the data (std::vector<char>, sized by the source's slot 4 and filled by slot 6).
// A group's attributes are kept sorted by name, case-insensitively (_stricmp, std::sort: the introsort the image
// compiled in, whose order for names equal ignoring case the tests compare).
//
// The source (what AddAttribute reads and ReadAttribute feeds): a plain interface without cIGZUnknown's slots 0-2
// meaning (its slot 2 is used as the consumer of the data); the image's callers are not ours, so it is declared by use.
#include "gz/cIGZUnknown.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct ServiceCell; // rz/StaticGetters2.h

namespace nGZSceneGraph {

// The object an attribute is read from (AddAttribute) and handed to (ReadAttribute). Slots 0 and 1 are not used here.
class cIMaterialAttributeSource {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual uint32_t ConsumeData(const void *data) = 0; // 2: given an attribute's data (ReadAttribute); the answer is passed on
    virtual uint32_t GetGroupKey() = 0;                 // 3: the group the attribute goes in
    virtual uint32_t GetDataSize() = 0;                 // 4: bytes of data (the attribute's vector is resized to it)
    virtual uint32_t GetWidth() = 0;                    // 5: the attribute's width
    virtual void FillData(void *data) = 0;              // 6: writes GetDataSize() bytes
};

// A found attribute: name (the string's text), its index in the group, the width, and the position in the flat numbering
// of every group's values in key order (index * width + the values of the lower groups).
struct AttributeRef {
    const char *mName;   // +0
    uint32_t mIndex;     // +4
    uint32_t mWidth;     // +8
    uint32_t mPosition;  // +0xc
};
static_assert(sizeof(AttributeRef) == 0x10, "AttributeRef size");

// Slots 3-15 (0-2 are cIGZUnknown's).
class cIMaterialAttributes : public cIGZUnknown {
public:
    virtual std::string *SetName(const char *name) = 0;                                     // 3: assigns +8; the answer is the string
    virtual void AddAttribute(const char *name, cIMaterialAttributeSource *source) = 0;     // 4 (ret 8)
    virtual const char *GetName() = 0;                                                      // 5: null for an empty name
    virtual bool IsEmpty() = 0;                                                             // 6: no group
    virtual uint32_t GetGroupCount() = 0;                                                   // 7
    virtual uint32_t GetGroupKey(uint32_t n) = 0;                                           // 8 (ret 4): the nth key in order
    virtual uint32_t GetAttributeCount(uint32_t key) = 0;                                   // 9 (ret 4): 0 for no group
    virtual uint32_t GetValueCount(uint32_t key) = 0;                                       // 10 (ret 4): count * first width
    virtual uint32_t GetTotalValueCount() = 0;                                              // 11: the sum over every group
    virtual AttributeRef *FindAttribute(AttributeRef *out, const char *name, uint32_t key) = 0;  // 12 (ret 0xc)
    virtual AttributeRef *GetAttribute(AttributeRef *out, uint32_t index, uint32_t key) = 0;     // 13 (ret 0xc)
    virtual uint32_t ReadAttribute(cIMaterialAttributeSource *source, uint32_t index) = 0;       // 14 (ret 8)
    virtual ~cIMaterialAttributes() = default;                                              // 15: the deleting destructor (ret 4)
};

class cMaterialAttributes : public cIMaterialAttributes {
public:
    static constexpr uint32_t kIidAttributes = 0x0ace5c2b;

    // One attribute (0x28): sorted by name within its group.
    struct Attribute {
        std::string mName;        // +0
        uint32_t mWidth = 0;      // +0x18
        std::vector<char> mData;  // +0x1c
    };
    using Group = std::vector<Attribute>;

    cMaterialAttributes();     // 0xaa9e40
    ~cMaterialAttributes() override; // 0xaa9fe0 (destructor body), 0xaaa0e0 (the deleting destructor, slot 15)

    bool QueryInterface(uint32_t riid, void **out) override;                           // 0xaaa750
    uint32_t AddRef() override;                                                        // shared 0x148fb0
    uint32_t Release() override;                                                       // shared 0x16e890
    std::string *SetName(const char *name) override;                                   // 0xaaa780
    void AddAttribute(const char *name, cIMaterialAttributeSource *source) override;   // 0xaaa100
    const char *GetName() override;                                                    // 0xaaa6d0
    bool IsEmpty() override;                                                           // 0xaaa3b0
    uint32_t GetGroupCount() override;                                                 // shared 0x15e970
    uint32_t GetGroupKey(uint32_t n) override;                                         // 0xaaa430
    uint32_t GetAttributeCount(uint32_t key) override;                                 // 0xaaa6f0
    uint32_t GetValueCount(uint32_t key) override;                                     // 0xaaa620
    uint32_t GetTotalValueCount() override;                                            // 0xaaa680
    AttributeRef *FindAttribute(AttributeRef *out, const char *name, uint32_t key) override;  // 0xaaa520
    AttributeRef *GetAttribute(AttributeRef *out, uint32_t index, uint32_t key) override;     // 0xaaa490
    uint32_t ReadAttribute(cIMaterialAttributeSource *source, uint32_t index) override;       // 0xaaa350

    uint32_t mRefCount = 0;                          // +4
    std::string mName;                               // +8
    std::map<uint32_t, Group> mGroups;               // +0x20

private:
    // The flat position of the start of `key`'s group: the sum of count * first width over the groups ahead of it. `at` is
    // key's group, or end() when there is none.
    uint32_t ValuesBefore(uint32_t key, std::map<uint32_t, Group>::iterator &at);
};

static_assert(sizeof(cMaterialAttributes::Attribute) == 0x28, "Attribute size");
static_assert(sizeof(std::map<uint32_t, cMaterialAttributes::Group>) == 8, "the map is the original's: head and size");
static_assert(sizeof(cMaterialAttributes) == 0x28, "cMaterialAttributes size");

// 0xaaa3c0 (cdecl, no arguments): a new attributes object. The factory hook's entry.
cIMaterialAttributes *CreateMaterialAttributes();

// 0xae87d0 (cdecl): the cell of a name: `cell` opened on `name` (a word the cell's service understands; the callers pass the text's address) with
// flag 0, or set to the invalid index (0xffffffff) for handle 0; returns `cell`.
ServiceCell *OpenNameCell(ServiceCell *cell, uint32_t name);

} // namespace nGZSceneGraph
