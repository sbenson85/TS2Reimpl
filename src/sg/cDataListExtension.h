#pragma once
// nGZSceneGraph::cDataListExtension (lane OY, scenegraph D1a part 2): an extension holding a named-value store, a list of
// cDataListItems (src/sg/cDataListItem.h) kept in one list item (+0x18). Class id 0x6a836d56 (the COM director's factory
// 0xa990b0), interface cIDataListExtension 0x6a836d51; the extension's type name (cIExtension slot 25) is "data_list".
// Model and CAS code find one on a node and read or write its values by name (the CAS bone adjustments, lane JZ).
//
// The game's object (0x30 bytes) is cExtension's part (src/sg/cExtension.h), then:
//   +0x18 mItem  the list item (counted; made through the COM director in the constructor, MakeList'ed)
//   +0x1c the vtordisp, +0x20 the virtual base cIExtension (vtable 0xe7519c)
//   +0x24 the vtordisp, +0x28 the virtual base cIDataListExtension (vtable 0xe7520c, 27 slots; its own vbptr at +0x2c to
//         the cIExtension: `public virtual cIExtension`, which lane JZ's VirtualBase reads)
// Ours is our compiler's class with the same bases (the same order of virtual bases gives the same layout). It is a leaf:
// the factory 0xa990b0 is hooked and nothing else is (every other body is reached only through the vtables our objects
// replace, or from the constructors; status entries "in effect through callers").
//
// The own face's bodies ("named" slots): a setter finds the name in the list (item slot 32), sets the found item (slot 29
// then its setter), or makes a new item through the COM director, names it, sets it, adds it (33) and drops its own
// reference. A getter answers false (al) unless the name is there with the getter's type; then it copies the value out.
#include "sg/cExtension.h"
#include "sg/cDataListItem.h"

#include <cstdint>

namespace nGZSceneGraph {

// nGZSceneGraph::cIDataListExtension (interface id 0x6a836d51): 27 slots of its own (none inherited: its cIExtension is a
// virtual base). Setters' eax is left alone (void): the original's is the found item's setter's or the new item's Release
// answer, and no caller reads it.
class cIDataListExtension : public virtual cIExtension {
public:
    static constexpr uint32_t kIid = 0x6a836d51;

    virtual void SetListName(const char *name) = 0;                                 // 0: the list item's SetName
    virtual void SetBool(const char *name, bool value) = 0;                         // 1
    virtual void SetSint32(const char *name, int32_t value) = 0;                    // 2
    virtual void SetFloat32(const char *name, float value) = 0;                     // 3
    virtual void SetVector2(const char *name, const float *value) = 0;              // 4
    virtual void SetVector3(const char *name, const float *value) = 0;              // 5
    virtual void SetString(const char *name, const char *text) = 0;                 // 6
    virtual void SetQuaternion(const char *name, const float *value) = 0;           // 7
    virtual void SetByteArray(const char *name, const void *data, uint32_t size) = 0; // 8
    virtual void MakeList(const char *name) = 0;                                    // 9: a list item of that name
    virtual void Remove(const char *name) = 0;                                      // 10
    virtual void RemoveAll() = 0;                                                   // 11: last first
    virtual const char *GetListName() = 0;                                          // 12
    virtual bool GetBool(const char *name, bool *out) = 0;                          // 13: the value byte as it is
    virtual bool GetSint32(const char *name, int32_t *out) = 0;                     // 14
    virtual bool GetFloat32(const char *name, float *out) = 0;                      // 15
    virtual bool GetVector2(const char *name, float *out) = 0;                      // 16: two floats
    virtual bool GetVector3(const char *name, float *out) = 0;                      // 17
    virtual bool GetString(const char *name, cIGZString *out) = 0;                  // 18: FromChar (null: "")
    virtual bool GetQuaternion(const char *name, float *out) = 0;                   // 19
    virtual bool GetByteArray(const char *name, const void **data, uint32_t *size) = 0; // 20: the item's own bytes
    virtual bool GetList(const char *name, cIDataListItem **out) = 0;               // 21: counted
    virtual const char *GetItemName(int32_t index) = 0;                             // 22: no range check
    virtual uint32_t GetType(const char *name) = 0;                                 // 23: 0 when not there
    virtual uint32_t GetItemType(int32_t index) = 0;                                // 24: no range check
    virtual bool GetItem(uint32_t index, cIDataListItem **out) = 0;                 // 25: counted; false past the end
    virtual uint32_t GetCount() = 0;                                                // 26
};

// C4250 ("inherits ... via dominance"): intended. cExtension's bodies are the final overriders of cIExtension's slots
// that cIDataListExtension reaches through the shared virtual base.
#pragma warning(push)
#pragma warning(disable : 4250)
class cDataListExtension final : public cExtension, public virtual cIDataListExtension {
public:
    static constexpr uint32_t kClassId = 0x6a836d56;
    static constexpr uint32_t kVersion = 1; // the type-info record's version (0xa99dd0)

    // 0xa990b0 (__cdecl): the COM director's factory for kClassId: a new extension (count 0), as its cIExtension.
    static cIGZUnknown *__cdecl Create();

    cDataListExtension();                                  // 0xa98950
    cDataListExtension(const cDataListExtension &other);   // 0xa98840: the list copied (0xa98c90)
    cDataListExtension &operator=(const cDataListExtension &) = delete;
    ~cDataListExtension() override;                        // 0xa98a50 (deleting 0xa98b00)

    // ---- the referent face (+0xc) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa992e0: kIid, else cExtension's
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa99330
    cPersistTypeInfo GetTypeInfo() override;                // 0xa99dd0: {"cDataListExtension", kClassId, 1}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa99fa0

    // ---- cIExtension ----
    cIExtension *Clone() override;                          // 15: 0xa98c10
    bool IsKindOf(uint32_t iid) override;                   // 23: 0xa99db0
    const char *GetTypeName() override;                     // 25: 0xa99030, "data_list"
    void GetDescription(cIGZString *text) override;         // 26: 0xa98f50, "(<list name> n<count>)"

    // ---- cIDataListExtension (the bodies' RVAs; the vtable 0xe7520c holds vtordisp thunks to them) ----
    void SetListName(const char *name) override;                                 // 0xa99800
    void SetBool(const char *name, bool value) override;                         // 0xa99430
    void SetSint32(const char *name, int32_t value) override;                    // 0xa99910
    void SetFloat32(const char *name, float value) override;                     // 0xa99620
    void SetVector2(const char *name, const float *value) override;              // 0xa99af0
    void SetVector3(const char *name, const float *value) override;              // 0xa99be0
    void SetString(const char *name, const char *text) override;                 // 0xa99a00
    void SetQuaternion(const char *name, const float *value) override;           // 0xa99820
    void SetByteArray(const char *name, const void *data, uint32_t size) override; // 0xa99520
    void MakeList(const char *name) override;                                    // 0xa99720
    void Remove(const char *name) override;                                      // 0xa993b0
    void RemoveAll() override;                                                   // 0xa993f0
    const char *GetListName() override;                                          // 0xa99220
    bool GetBool(const char *name, bool *out) override;                          // 0xa98b50
    bool GetSint32(const char *name, int32_t *out) override;                     // 0xa99cd0
    bool GetFloat32(const char *name, float *out) override;                      // 0xa99050
    bool GetVector2(const char *name, float *out) override;                      // 0xa99eb0
    bool GetVector3(const char *name, float *out) override;                      // 0xa99f30
    bool GetString(const char *name, cIGZString *out) override;                  // 0xa99d30
    bool GetQuaternion(const char *name, float *out) override;                   // 0xa99270
    bool GetByteArray(const char *name, const void **data, uint32_t *size) override; // 0xa98bb0
    bool GetList(const char *name, cIDataListItem **out) override;               // 0xa991b0
    const char *GetItemName(int32_t index) override;                             // 0xa98eb0
    uint32_t GetType(const char *name) override;                                 // 0xa98f00
    uint32_t GetItemType(int32_t index) override;                                // 0xa98ee0
    bool GetItem(uint32_t index, cIDataListItem **out) override;                 // 0xa99160
    uint32_t GetCount() override;                                                // 0xa99250

private:
    // 0xa98c90: `to` takes `from`'s name and a copy of each of its items (lists copied deeply), added at its end.
    static void CopyList(cIDataListItem *from, cIDataListItem *to);

    cIDataListItem *mItem = nullptr; // +0x18 counted
};
#pragma warning(pop)

} // namespace nGZSceneGraph
