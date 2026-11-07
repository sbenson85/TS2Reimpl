#pragma once
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

// cTSRelationshipTable: a neighbour's relationships (SREL): per other person, a counted cTSRelationshipEntry (its values),
// loaded lazily from the entry's own record. RTTI bases: cITSRelationshipTable (+0, 18 slots, vtable 0xdd4a68),
// cIGZSerializable (+4, 0xdd4ab4), each a cIGZUnknown. 0x40 bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane DZ): a real C++ class. The compiler emits its two vtables (the interfaces'
// slot order; slot 14 of the first is the scalar deleting destructor of the interface's virtual destructor), the adjustor
// thunks of the +4 face and the RTTI. Creation sites: the constructor 0x55ab10 only, called by cTSNeighbor::Create 0x4b73c0
// (ours: `new cTSRelationshipTable`) and by the game's constructor 0x52c4b0 (a person-side object; reaches ours through the
// constructor hook). The game's vtables are referenced only by that constructor and the deleting destructor 0x55abf0. The RVA
// hooks on the old functions are adapters (cTSRelationshipTableAdapters). Nothing in the image reads RTTI.

// The game's integer hash predates MSVC's current FNV integer hash.
struct RelationshipPersonHash {
    size_t operator()(uint32_t person) const noexcept {
        uint32_t hash = person * 0x1001u;
        hash = (hash ^ (hash >> 22)) * 0x11u;
        hash = (hash ^ (hash >> 9)) * 0x401u;
        hash = (hash ^ (hash >> 2)) * 0x81u;
        return hash ^ (hash >> 12);
    }
};

// A node owns a reference, not just a void pointer (0x1d5dd0 releases +0x0c).
struct RelationshipEntryRef {
    void *value = nullptr;
    RelationshipEntryRef() = default;
    explicit RelationshipEntryRef(void *entry);
    RelationshipEntryRef(const RelationshipEntryRef &other);
    ~RelationshipEntryRef() noexcept;
    RelationshipEntryRef &operator=(const RelationshipEntryRef &other);
    void Assign(void *entry); // retain the new entry, then release the old one
    void Reset() noexcept;    // clear first, then release
};
static_assert(sizeof(RelationshipEntryRef) == 4);
using RelationshipMap = std::unordered_map<uint32_t, RelationshipEntryRef, RelationshipPersonHash>;
// MSVC 14.40, iterator debugging disabled: max load / list / bucket vector / mask / bucket count.
static_assert(sizeof(RelationshipMap) == 0x20);

// The table's own interface (id 0xcc364c23; the original's name, global as its RTTI). Its virtual destructor sits at slot 14,
// where the original's deleting destructor is: a virtual a class introduces takes the next slot in declaration order.
class cITSRelationshipTable : public cIGZUnknown {
public:
    virtual bool WriteForPerson(ifaces::cIGZOStream *stream, uint32_t person) = 0;      // slot 3 (ret 8)
    virtual bool ReadForPerson(ifaces::cIGZIStream *stream, uint32_t person) = 0;       // slot 4 (ret 8)
    virtual uint32_t GetValueCount(uint32_t person) = 0;                                // slot 5
    virtual bool SetValueCount(uint32_t owner, uint32_t person, uint32_t count) = 0;    // slot 6
    virtual void RemovePerson(uint32_t person) = 0;                                     // slot 7
    virtual void Clear(bool deleteResources) = 0;                                       // slot 8
    virtual uint32_t GetValue(uint32_t person, uint32_t index) = 0;                     // slot 9
    virtual bool SetValue(uint32_t person, uint32_t index, uint32_t value) = 0;         // slot 10
    virtual uint32_t GetPersonCount() = 0;                                              // slot 11
    virtual uint32_t GetFirstPerson() = 0;                                              // slot 12
    virtual uint32_t GetNextPerson(uint32_t person) = 0;                                // slot 13
    virtual ~cITSRelationshipTable() = default;                                         // slot 14 (deleting destructor)
    virtual void SetStorage(cIGZUnknown *storage) = 0;                                  // slot 15
    virtual bool IsModified() = 0;                                                      // slot 16
    virtual void SetModified(bool modified) = 0;                                        // slot 17
};

class cTSRelationshipTable : public cITSRelationshipTable, public cIGZSerializable {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdd4a68, kOriginalSerialVTable = 0xdd4ab4;
    static constexpr uint32_t kClassId = 0xcc364c2a;
    static constexpr uint32_t kInterfaceId = 0xcc364c23;
    static constexpr uint32_t kSerializableId = 0xe4fda3d4;
    static constexpr size_t kSize = 0x40;

    cTSRelationshipTable();                    // 0x55ab10: an empty eight-bucket table
    ~cTSRelationshipTable() override;          // slot 14: the scalar deleting destructor 0x55abf0 is the compiler's
    cTSRelationshipTable(const cTSRelationshipTable &) = delete;
    cTSRelationshipTable &operator=(const cTSRelationshipTable &) = delete;

    // cIGZUnknown (the +4 face's slots 0-2 are the compiler's adjustor thunks, as the original's 0x55b359 / 0x1448e9 / 0x55bb07;
    // the AddRef thunk is folded with other classes)
    bool QueryInterface(uint32_t id, void **out) override; // slot 0, 0x55b310: the primary or +4 face, retained
    uint32_t AddRef() override;                            // slot 1, folded 0x145260
    uint32_t Release() override;                           // slot 2, 0x55baf0: decrement above one, otherwise delete (slot 14)

    // cITSRelationshipTable (+0)
    bool WriteForPerson(ifaces::cIGZOStream *stream, uint32_t person) override; // slot 3, 0x55bec0: owner person, then Write
    bool ReadForPerson(ifaces::cIGZIStream *stream, uint32_t person) override;  // slot 4, 0x55b2a0: owner person, then Read
    uint32_t GetValueCount(uint32_t person) override;                          // slot 5, 0x55aef0
    bool SetValueCount(uint32_t owner, uint32_t person, uint32_t count) override; // slot 6, 0x55bf70
    void RemovePerson(uint32_t person) override;                               // slot 7, 0x55bbc0
    void Clear(bool deleteResources) override;                                 // slot 8, 0x55bb10
    uint32_t GetValue(uint32_t person, uint32_t index) override;               // slot 9, 0x55b1b0
    bool SetValue(uint32_t person, uint32_t index, uint32_t value) override;   // slot 10, 0x55c080
    uint32_t GetPersonCount() override;                                        // slot 11, folded 0x15e970: the map's size
    uint32_t GetFirstPerson() override;                                        // slot 12, 0x55afd0: first list key or UINT32_MAX
    uint32_t GetNextPerson(uint32_t person) override;                          // slot 13, 0x55b010: successor or UINT32_MAX
    void SetStorage(cIGZUnknown *storage) override;                            // slot 15, folded 0x46e120: counted assignment
    bool IsModified() override;                                                // slot 16, folded 0x156110
    void SetModified(bool modified) override;                                  // slot 17, folded 0x1b9690

    // cIGZSerializable (+4; these run with this = the face)
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3, 0x55c170: version 3 keys, loaded entries to their records
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4, 0x55b420: versions 2/3 (keys only) or negative (inline)
    uint32_t GetGZCLSID() override;                   // slot 5, 0x55b000: kClassId

    // Non-virtual members (called only by the slots above)
    void *LoadEntry(RelationshipMap::iterator position); // 0x55b090: entry of a node, created and read from its record if absent
    void SetEntry(uint32_t owner, uint32_t person, RelationshipMap::iterator *out); // 0x55ac60: new empty entry for person

    // +0x00, +0x04: the two vtable pointers (the interfaces above, in that order)
    uint32_t mRefCount = 0;      // +0x08
    uint32_t mResourceType = 0;  // +0x0c: entry resource key word 0 (0x55ac60)
    uint32_t mResourceGroup = 0; // +0x10: entry resource key word 1
    uint32_t mOwnerPerson = 0;   // +0x14: high word of an entry instance id
    bool mModified = false;      // +0x18 (a bool: slot 16 hands its byte out as it is)
    uint8_t mPadding[3];         // +0x19: the constructor leaves these bytes untouched
    RelationshipMap mEntries;    // +0x1c: nodes are {next, previous, person, retained entry}
    void *mStorage = nullptr;    // +0x3c: retained resource storage (the segment), set by Read and slot 15
};
static_assert(sizeof(cTSRelationshipTable) == cTSRelationshipTable::kSize);
static_assert(offsetof(cTSRelationshipTable, mRefCount) == 8);
static_assert(offsetof(cTSRelationshipTable, mResourceType) == 0x0c);
static_assert(offsetof(cTSRelationshipTable, mOwnerPerson) == 0x14);
static_assert(offsetof(cTSRelationshipTable, mModified) == 0x18);
static_assert(offsetof(cTSRelationshipTable, mEntries) == 0x1c);
static_assert(offsetof(cTSRelationshipTable, mStorage) == 0x3c);

// Where the serializable interface sits in the object (RTTI; the tests check it).
namespace reltable {
constexpr uint32_t kSerialFace = 0x4;
} // namespace reltable

// The RVA hooks' entry points (src/hook/tables/cTSRelationshipTable.cpp). Every table the game creates is built through the
// constructor hook (Construct) and carries our vtables, so the others run only where the game calls an old entry point directly
// or holds a table built with its own vtables (none while the constructor hook is on). Original calling convention (this in ecx
// = the face of the slot), our functions called without virtual dispatch, bodies inlined.
struct cTSRelationshipTableAdapters {
    using Self = cTSRelationshipTable;
    static Self *__fastcall Construct(void *memory, void *);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall WriteForPerson(Self *self, void *, ifaces::cIGZOStream *stream, uint32_t person);
    static bool __fastcall ReadForPerson(Self *self, void *, ifaces::cIGZIStream *stream, uint32_t person);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
    static void *__fastcall LoadEntry(Self *self, void *, RelationshipMap::iterator position);
    static void __fastcall SetEntry(Self *self, void *, uint32_t owner, uint32_t person, RelationshipMap::iterator *out);
};
