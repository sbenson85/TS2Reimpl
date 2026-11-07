#pragma once
// nGZSceneGraph::cDataListItem (lane OX, scenegraph D1a): one named, typed value of a data list (class id 0x6a7b33c3,
// interface cIDataListItem 0x6a7b33bb). The COM director's factory 0xac6d50 makes them (cDataListExtension's lists, and
// the items' own Read / Parse for their list children); each item holds a name and one value: a bool, a Sint32, a
// float, two to four floats, a string, a byte array or a list of counted items. A setter of another type drops the old
// value first (Clear).
//
// The game's object (0x24 bytes; vtable 0xe78344, 37 slots): +4 the name (8 bytes: the text itself when it fits with
// its terminator, else a zero first byte and a heap pointer at +8; an empty name is a null pointer), +0xc the type,
// +0x10 the reference count, +0x14..+0x23 the value. The value words are one union: the list is an MSVC std::list
// (head node, size) in its first two words, the string and the byte array a heap pointer (and the size), and Clear frees
// what they own but leaves the words as they were, so a getter of another type reads the old words reinterpreted, and
// GetCount (36) reads the second word whatever the type. Ours keeps the same union (mValue) for that reason, and the
// name's storage (Name) for its bug.
//
// Persistence comes in three forms: slots 6 / 7 through the persist writer and reader (cIGZPersistWriter / Reader), 8 / 9
// through a binary stream (cIGZOStream / cIGZIStream), 10 / 11 as a line of text per item ("<Type> <name> = <value>",
// lists "List <name>   <count>" with their items indented below; Parse reads one line through the stream's GetGZStr).
// Every form writes the type byte first; an item of type 0 writes nothing, so a list holding one reads back short.
#include "gz/cIGZUnknown.h"

#include <cstdint>
#include <list>

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

namespace nGZSceneGraph {

// nGZSceneGraph::cIDataListItem (interface id 0x6a7b33bb).
class cIDataListItem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x6a7b33bb;
    enum Type : uint32_t {
        kNone = 0, kBool = 1, kSint32 = 2, kFloat32 = 3, kVector2 = 4, kVector3 = 5, kString = 6, kList = 7,
        kQuaternion = 8, kByteArray = 9,
    };

    virtual const char *GetName() = 0;                                // 3: null for an empty name
    virtual void SetName(const char *name) = 0;                       // 4
    virtual uint32_t GetType() = 0;                                   // 5
    virtual void Write(ifaces::cIGZPersistWriter *writer) = 0;        // 6
    virtual void Read(ifaces::cIGZPersistReader *reader) = 0;         // 7
    virtual void WriteStream(ifaces::cIGZOStream *stream) = 0;        // 8
    virtual void ReadStream(ifaces::cIGZIStream *stream) = 0;         // 9
    virtual void Describe(ifaces::cIGZOStream *stream, int32_t indent) = 0; // 10: as text, `indent` levels in
    virtual bool Parse(ifaces::cIGZIStream *stream) = 0;              // 11: one line of Describe's text
    virtual void SetBool(bool value) = 0;                             // 12
    virtual uint8_t GetBool() = 0;                                    // 13: the first value byte as it is
    virtual void SetSint32(int32_t value) = 0;                        // 14
    virtual int32_t GetSint32() = 0;                                  // 15
    virtual void SetFloat32(float value) = 0;                         // 16
    virtual float GetFloat32() = 0;                                   // 17
    virtual void SetVector2(const float *value) = 0;                  // 18: two floats
    virtual float *GetVector2(float *out) = 0;                          // 19
    virtual void SetVector3(const float *value) = 0;                  // 20
    virtual float *GetVector3(float *out) = 0;                          // 21
    virtual void SetQuaternion(const float *value) = 0;               // 22: four floats
    virtual float *GetQuaternion(float *out) = 0;                       // 23
    virtual void SetString(const char *text) = 0;                     // 24: a copy (null: an empty value)
    virtual const char *GetString() = 0;                              // 25
    virtual void SetByteArray(const void *data, uint32_t size) = 0;   // 26: a copy
    virtual const void *GetByteArray(uint32_t *size) = 0;             // 27
    virtual void MakeList() = 0;                                      // 28: an empty list unless one already
    virtual cIDataListItem *GetItem(int32_t index) = 0;               // 29 (not counted)
    virtual cIDataListItem *GetItemAt(int32_t index) = 0;             // 30: the same body as 29
    virtual int32_t IndexOf(cIDataListItem *item) = 0;                // 31: -1 for none
    virtual int32_t FindByName(const char *name) = 0;                 // 32: -1 for none
    virtual int32_t AddItem(cIDataListItem *item) = 0;                // 33: counted; its index
    virtual void RemoveAt(int32_t index) = 0;                         // 34
    virtual void Remove(cIDataListItem *item) = 0;                    // 35
    virtual uint32_t GetCount() = 0;                                  // 36
};

class cDataListItem final : public cIDataListItem {
public:
    static constexpr uint32_t kClassId = 0x6a7b33c3;

    // 0xac6d50 (__cdecl): the COM director's factory for kClassId: a new item, not counted.
    static cIGZUnknown *__cdecl Create();

    cDataListItem() = default;      // 0xac6a80
    ~cDataListItem();               // 0xac6ac0 (Release deletes)

    bool QueryInterface(uint32_t riid, void **out) override;  // 0 (0xac6e20): kIid, kClassId and 1
    uint32_t AddRef() override;                               // 1 (shared 0x146a40)
    uint32_t Release() override;                              // 2 (0xac7cb0): deleted at a count of one or none
    const char *GetName() override;                           // 3 (0xac6e00, 0xac8910)
    void SetName(const char *name) override;                  // 4 (0xac7ef0, 0xac6ae0)
    uint32_t GetType() override;                              // 5 (shared 0x145770)
    void Write(ifaces::cIGZPersistWriter *writer) override;   // 6 (0xac86d0)
    void Read(ifaces::cIGZPersistReader *reader) override;    // 7 (0xac7950)
    void WriteStream(ifaces::cIGZOStream *stream) override;   // 8 (0xac8490)
    void ReadStream(ifaces::cIGZIStream *stream) override;    // 9 (0xac75a0)
    void Describe(ifaces::cIGZOStream *stream, int32_t indent) override; // 10 (0xac8060)
    bool Parse(ifaces::cIGZIStream *stream) override;         // 11 (0xac6e50)
    void SetBool(bool value) override;                        // 12 (0xac7e10)
    uint8_t GetBool() override;                               // 13 (shared 0x16db30)
    void SetSint32(int32_t value) override;                   // 14 (0xac7f30)
    int32_t GetSint32() override;                             // 15 (shared 0x149d00)
    void SetFloat32(float value) override;                    // 16 (0xac7ea0)
    float GetFloat32() override;                              // 17 (shared 0x410160)
    void SetVector2(const float *value) override;             // 18 (0xac7fa0)
    float *GetVector2(float *out) override;                     // 19 (0xac6d90)
    void SetVector3(const float *value) override;             // 20 (0xac7fd0)
    float *GetVector3(float *out) override;                     // 21 (0xac6db0)
    void SetQuaternion(const float *value) override;          // 22 (0xac7f00)
    float *GetQuaternion(float *out) override;                  // 23 (shared 0x49e6e0)
    void SetString(const char *text) override;                // 24 (0xac7f50)
    const char *GetString() override;                         // 25 (shared 0x149d00)
    void SetByteArray(const void *data, uint32_t size) override; // 26 (0xac7e30)
    const void *GetByteArray(uint32_t *size) override;        // 27 (0xac6d80)
    void MakeList() override;                                 // 28 (0xac7ed0; the list 0xac6c70)
    cIDataListItem *GetItem(int32_t index) override;          // 29 (0xac6dd0)
    cIDataListItem *GetItemAt(int32_t index) override;        // 30 (0xac6dd0)
    int32_t IndexOf(cIDataListItem *item) override;           // 31 (0xac6d20)
    int32_t FindByName(const char *name) override;            // 32 (0xac6ca0)
    int32_t AddItem(cIDataListItem *item) override;           // 33 (0xac6b60)
    void RemoveAt(int32_t index) override;                    // 34 (0xac7ce0)
    void Remove(cIDataListItem *item) override;               // 35 (0xac7d70)
    uint32_t GetCount() override;                             // 36 (0xac6e10)

private:
    using List = std::list<cIDataListItem *>;

    void Clear();                 // 0xac6c30: the value's storage dropped, type none (the words stay)
    void SetType(uint32_t type);  // Clear and the new type, unless the type is already `type`
    List &Items();                // the value words as the list (type kList)
    void Append(cIDataListItem *item); // at the list's end, counted
    static cIDataListItem *NewItem(); // a counted item from the COM director (class kClassId)

    // The name (0xac6a70 / 0xac6ab0 / 0xac6ae0 / 0xac8910): up to 7 characters held in the 8 bytes themselves, a longer
    // one on the heap with a zero first byte marking it (the pointer in bytes 4-7); empty: zero and a null pointer.
    struct Name {
        char mText[8] = {};
        const char *Get() const;     // 0xac8910: null when empty
        void Set(const char *text);  // 0xac6ae0 (null text: not handled, as the original's)
        void Free();                 // 0xac6ab0
        char *Heap() const;
        void SetHeap(char *text);
    };

    // The game's layout, kept: a long name set over a short one (ORIGINAL-BUG, Name::Set) reads on from the name's 8
    // bytes into the type word after them.
    Name mName;                   // +4
    uint32_t mType = kNone;       // +0xc
    uint32_t mRefCount = 0;       // +0x10
    alignas(4) uint32_t mValue[4] = {}; // +0x14: bool / Sint32 / float / floats / char * / {data, size} / std::list
};
static_assert(sizeof(std::list<cIDataListItem *>) == 8, "cDataListItem: the list is two words (head, size)");
static_assert(sizeof(char *) == 4, "cDataListItem: the heap name's pointer is bytes 4-7 of the name");

} // namespace nGZSceneGraph
