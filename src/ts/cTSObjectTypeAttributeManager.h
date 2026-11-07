#pragma once
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>

// cTSObjectTypeAttributeManager: the neighbourhood's object type attributes (resource TATT 0x54415454), one attribute object
// (cEdithObjectTypeAttribute: an id and a vector of 16-bit values) per object type id. RTTI bases: cITSObjectTypeAttributeManager
// (with cIGZSerializable) at +0 (vtable 0xdcd11c, 13 slots) and cIGZPersistResource at +4 (0xdcd154, 12 slots). 0x24 bytes, made
// by Create 0x4d8b60 (the COM factory, registered by the cTSSimCOMDirector constructor; the constructor is inline there). The
// load (cTSLoadNeighborhoodController step 9) fetches the hood's TATT with iid 0x2c97cc10 and merges it into the global manager
// (AddAllFrom).
//
// Record format (Write / Read): 0x40 zero bytes, the class id, version 0x55, a zero word, the entry count, then per entry the
// id, the value count and the values as 16-bit words, in id order.
//
// Standalone shape (docs/standalone-shape.md, lane FP): a real C++ class deriving from its two interfaces; the compiler emits the
// vtables and the persist face's thunks (QueryInterface 0x4d8c89; AddRef 0x1448e9 and Release 0x2928a5, folded). Create (`new`)
// is the only creation site; the RVA hooks on the old slot functions are adapters (cTSObjectTypeAttributeManagerAdapters below).

namespace ifaces {
class cIEdithObjectTypeAttribute;
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// A counted pointer to an attribute object (cIGZUnknown slots 1 / 2), the map's value. Copying counts again.
class ObjectTypeAttributeRef {
public:
    explicit ObjectTypeAttributeRef(ifaces::cIEdithObjectTypeAttribute *object);
    ObjectTypeAttributeRef(const ObjectTypeAttributeRef &other);
    ObjectTypeAttributeRef &operator=(const ObjectTypeAttributeRef &) = delete;
    ~ObjectTypeAttributeRef();
    // Holds `object` instead (counted first, the old one released after), unless it is the one already held.
    void Assign(ifaces::cIEdithObjectTypeAttribute *object);
    ifaces::cIEdithObjectTypeAttribute *Get() const { return mObject; }

private:
    ifaces::cIEdithObjectTypeAttribute *mObject;
};
static_assert(sizeof(ObjectTypeAttributeRef) == 4);

using ObjectTypeAttributeMap = std::map<uint32_t, ObjectTypeAttributeRef>;
static_assert(sizeof(ObjectTypeAttributeMap) == 8);

// cITSObjectTypeAttributeManager (iid 0x2c97cc10): a cIGZSerializable (slots 3-5), slots 6-11, the virtual destructor as slot 12
// (the deleting destructor 0x4d8a30; Release's `delete this` is the original's `push 1; call [eax + 0x30]`). Only this class
// implements it.
class cITSObjectTypeAttributeManager : public cIGZSerializable {
public:
    virtual cIGZPersistResource *GetPersistFace() = 0;                                       // slot 6
    virtual ifaces::cIEdithObjectTypeAttribute *FindAttributes(uint32_t id) = 0;             // slot 7
    virtual uint8_t AddAttributes(ifaces::cIEdithObjectTypeAttribute *attributes) = 0;       // slot 8
    virtual void AddAllFrom(cIGZUnknown *other) = 0;                                         // slot 9
    virtual uint8_t Save() = 0;                                                              // slot 10
    virtual void Clear() = 0;                                                                // slot 11
    virtual ~cITSObjectTypeAttributeManager() = default;                                     // slot 12
};

class cTSObjectTypeAttributeManager : public cITSObjectTypeAttributeManager, public cIGZPersistResource {
public:
    // The game's vtables (what the original Create stores; the tests' original side).
    static constexpr uint32_t kVTable = 0xdcd11c;
    static constexpr uint32_t kPersistVTable = 0xdcd154;
    static constexpr uint32_t kSize = 0x24;
    static constexpr uint32_t kClassId = 0x54415454;          // 'TATT', GetGZCLSID and the resource type
    static constexpr uint32_t kInterfaceId = 0x2c97cc10;      // cITSObjectTypeAttributeManager
    static constexpr uint32_t kClassInterfaceId = 0xac98034e; // the class itself (AddAllFrom asks the other manager for it)
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;   // cIGZSerializable
    static constexpr uint32_t kPersistResourceId = 0x456b8f1d; // cIGZPersistResource: the +4 face
    static constexpr uint32_t kRecordVersion = 0x55;
    static constexpr uint32_t kHeaderBytes = 0x40;            // zero bytes before the class id
    static constexpr uint32_t kScratchValues = 0x80;          // values an unknown id may have in a record

    // The constructor inline in Create: count 0, an empty map, a zero key.
    cTSObjectTypeAttributeManager();
    // 0x4d8a30 without the free: the map is destroyed (attributes released). Slot 12; the scalar deleting destructor is the
    // compiler's (bit 0 frees the 0x24 bytes; returns this).
    ~cTSObjectTypeAttributeManager() override;

    // 0x4d8b60 (cdecl, EH): a new empty manager, or null when the allocation fails.
    static cTSObjectTypeAttributeManager *Create();

    // cIGZUnknown (both faces; the persist face's slots 0-2 are the compiler's thunks)
    // 0x4d8c30 (slot 0, ret 8): 1, the interface, the class and cIGZSerializable give this; cIGZPersistResource the +4 face.
    // Counted through slot 1. False otherwise.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefCount; } // slot 1, folded 0x145260
    // slot 2, folded 0x313300: at a count of 0 after the decrement, delete this (slot 12 with 1) and 0; else the count.
    uint32_t Release() override;

    // cIGZSerializable
    // 0x4d9060 (slot 3, ret 4): writes the record (format above); true when the stream has no error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4d8ca0 (slot 4, ret 4): reads a record into the attributes this already has: each entry's values go into the
    // attribute of its id (the rest of it zero-filled); entries of unknown ids are read and dropped. False when an entry has
    // more values than its attribute (0x80 for unknown ids), else whether the stream has no error.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x4d8c20 (slot 5): 'TATT'.
    uint32_t GetGZCLSID() override;

    // cITSObjectTypeAttributeManager
    cIGZPersistResource *GetPersistFace() override { return static_cast<cIGZPersistResource *>(this); } // slot 6, folded 0x1c04e0
    // 0x4d8b10 (slot 7, ret 4): the attribute of the id (not counted), or null.
    ifaces::cIEdithObjectTypeAttribute *FindAttributes(uint32_t id) override;
    // 0x4d8ed0 (slot 8, ret 4): holds the attribute under its id, replacing the one there; 1, or whether it was inserted.
    uint8_t AddAttributes(ifaces::cIEdithObjectTypeAttribute *attributes) override;
    // 0x4d8a80 (slot 9, ret 4): AddAttributes (slot 8) for every attribute of the other manager, in id order.
    void AddAllFrom(cIGZUnknown *other) override;
    // 0x4d8e20 (slot 10): saves the resource into the segment that holds its key, then flushes the segment. 0 when there is
    // no such segment, else what the save answered.
    uint8_t Save() override;
    // 0x4d9030 (slot 11): drops every attribute.
    void Clear() override;

    // cIGZPersistResource (+4): folded bodies shared with other classes of this layout (the key at face + 0x10)
    cGZPersistResourceKey GetKey() override                                                  // slot 3, folded 0x147790
    {
        cGZPersistResourceKey key;
        std::memcpy(&key, mKey, sizeof(key));
        return key;
    }
    bool SetKey(const cGZPersistResourceKey &key) override                                   // slot 4, folded 0x47d420
    {
        std::memcpy(mKey, &key, sizeof(mKey));
        return true;
    }
    uint32_t GetType() override { return mKey[0]; }                                          // slot 5, folded 0x149cf0
    uint32_t GetGroup() override { return mKey[1]; }                                         // slot 6, folded 0x149d00
    uint32_t GetInstance() override { return mKey[2]; }                                      // slot 7, folded 0x148ba0
    bool SetType(uint32_t type) override { mKey[0] = type; return true; }                    // slot 8, folded 0x1eec40
    bool SetGroup(uint32_t group) override { mKey[1] = group; return true; }                 // slot 9, folded 0x47d410
    bool SetInstance(uint32_t instance) override { mKey[2] = instance; return true; }        // slot 10, folded 0x1f5270
    uint32_t GetRefCount() override { return mRefCount; }                                    // slot 11, folded 0x147a30

    // +0x00 cITSObjectTypeAttributeManager / cIGZSerializable vtable, +0x04 cIGZPersistResource vtable
    uint32_t mRefCount;                 // +0x08
    ObjectTypeAttributeMap mAttributes; // +0x0c id -> counted attribute
    uint32_t mKey[4];                   // +0x14 resource key (type, group, instance, high), set through the persist face
};
static_assert(sizeof(cTSObjectTypeAttributeManager) == 0x24);
static_assert(offsetof(cTSObjectTypeAttributeManager, mRefCount) == 8);
static_assert(offsetof(cTSObjectTypeAttributeManager, mAttributes) == 0xc);
static_assert(offsetof(cTSObjectTypeAttributeManager, mKey) == 0x14);

namespace otam {
constexpr uint32_t kPersistFace = 4; // the cIGZPersistResource face's offset
}

// The RVA hooks' targets: old entry points for direct calls and for objects carrying the game's vtables. Each forwards with a
// qualified call (no virtual dispatch) on the slot's face.
struct cTSObjectTypeAttributeManagerAdapters {
    using Self = cTSObjectTypeAttributeManager;
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);                             // 0x4d8a30
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);                      // 0x4d8c30
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);                                                 // 0x4d8c20
    static void __fastcall Clear(Self *self, void *);                                                          // 0x4d9030
};
