#pragma once
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// cTSRelationshipEntry: one sim's values towards another (the record behind an SREL entry of cTSRelationshipTable): a
// vector of u32 values, stored as the record {table type, table group, owner << 16 | person, 0}. RTTI bases:
// cITSRelationshipEntry (+0, 10 slots, vtable 0xdd49ec), cIGZPersistResource (+4, 0xdd4a18; slots 3..11 folded, the
// key lives at entry + 0x10), cIGZSerializable (+8, 0xdd4a4c). 0x2c bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md, lane DZ): a real C++ class; the compiler emits its three vtables, the adjustor
// thunks of the +4 / +8 faces and the RTTI. The game has no constructor or destructor function: it builds the entry inline in
// the table's SetEntry (0x55ac60) and Read (0x55b420) and destroys it inline in Release (0x55ba70). Both builders are ours (the
// table, src/ts/cTSRelationshipTable.cpp), and every table the game creates is ours while the table's constructor hook is on, so
// every entry carries our vtables. The game's vtables are referenced only by those two builders. The RVA hooks on the old slot
// functions are adapters (cTSRelationshipEntryAdapters). Nothing in the image reads RTTI (docs/standalone-shape.md).

// The entry's own interface (the original's name, global as its RTTI). No virtual destructor: Release destroys inline.
class cITSRelationshipEntry : public cIGZUnknown {
public:
    virtual cIGZPersistResource *GetPersistResource() = 0;      // slot 3
    virtual bool ReadFromRecord(void *record) = 0;              // slot 4 (ret 4)
    virtual bool WriteToRecord(void *record) = 0;               // slot 5 (ret 4)
    virtual uint32_t GetValueCount() = 0;                       // slot 6
    virtual bool SetValueCount(uint32_t count) = 0;             // slot 7 (ret 4)
    virtual uint32_t GetValue(uint32_t index) = 0;              // slot 8 (ret 4)
    virtual bool SetValue(uint32_t index, uint32_t value) = 0;  // slot 9 (ret 8)
};

class cTSRelationshipEntry : public cITSRelationshipEntry, public cIGZPersistResource, public cIGZSerializable {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdd49ec;
    static constexpr uint32_t kOriginalPersistVTable = 0xdd4a18;
    static constexpr uint32_t kOriginalSerialVTable = 0xdd4a4c;
    static constexpr uint32_t kClassId = 0xcc364c18;         // GetGZCLSID
    static constexpr uint32_t kPersistResourceId = 0x456b8f1d;
    static constexpr uint32_t kStreamReaderId = 0x051b11bd;  // ReadFromRecord asks the record for this
    static constexpr uint32_t kStreamWriterId = 0xc51b11cb;  // WriteToRecord
    static constexpr uint32_t kRecordVersion = 2;
    static constexpr size_t kSize = 0x2c;

    // Inline in 0x55ac60 and 0x55b420 (the +4 / +8 interfaces' vtables stored first, dead stores, as in the original).
    explicit cTSRelationshipEntry(const cGZPersistResourceKey &key);
    cTSRelationshipEntry(const cTSRelationshipEntry &) = delete;
    cTSRelationshipEntry &operator=(const cTSRelationshipEntry &) = delete;

    // cIGZUnknown (every face; the +4 / +8 faces' slots 0-2 are the compiler's adjustor thunks, as the original's 0x55b2f2 /
    // 0x1f4daf / 0x55bade and 0x55b2fa / 0x1f4db7 / 0x55bae6; the AddRef thunks are folded with other classes)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x55b2c0 (ret 8): 1 and cIGZPersistResource, as the +4 face
    uint32_t AddRef() override;                              // slot 1, folded 0x15e520 (a library match: ++count)
    uint32_t Release() override;                             // slot 2, 0x55ba70: at 1 or below, destroyed inline

    // cITSRelationshipEntry (+0)
    cIGZPersistResource *GetPersistResource() override;   // slot 3, folded 0x1c04e0 (this + 4, null for null)
    bool ReadFromRecord(void *record) override;            // slot 4, 0x55bd20: version 2, count, u32 values
    bool WriteToRecord(void *record) override;             // slot 5, 0x55be00
    uint32_t GetValueCount() override;                     // slot 6, folded 0x4e9710
    bool SetValueCount(uint32_t count) override;           // slot 7, 0x55bf10: zero-fills growth; always true
    uint32_t GetValue(uint32_t index) override;            // slot 8, 0x55aed0: 0 when out of range
    bool SetValue(uint32_t index, uint32_t value) override; // slot 9, 0x55bee0: false when out of range

    // cIGZPersistResource (+4; these run with this = the face; all folded game functions)
    cGZPersistResourceKey GetKey() override;                // slot 3, folded 0x18a670
    bool SetKey(const cGZPersistResourceKey &key) override; // slot 4, folded 0x4f3d80
    uint32_t GetType() override;                            // slot 5, folded 0x145770
    uint32_t GetGroup() override;                           // slot 6, folded 0x149cf0
    uint32_t GetInstance() override;                        // slot 7, folded 0x149d00
    bool SetType(uint32_t type) override;                   // slot 8, folded 0x1eeb70
    bool SetGroup(uint32_t group) override;                 // slot 9, folded 0x1eec40
    bool SetInstance(uint32_t instance) override;           // slot 10, folded 0x47d410
    uint32_t GetRefCount() override;                        // slot 11, folded 0x46390 (a body of its own, no thunk)

    // cIGZSerializable (+8)
    bool Write(ifaces::cIGZOStream *stream) override; // slot 3, folded 0x18a7a0: false (the entry writes to records only)
    bool Read(ifaces::cIGZIStream *stream) override;  // slot 4, 0x55b370 (ret 4): s32 count (negative fails), that many u32
    uint32_t GetGZCLSID() override;                   // slot 5, 0x55aff0: kClassId

    // +0x00..+0x08: the three vtable pointers (the interfaces above, in that order)
    uint32_t mRefCount;             // +0x0c
    cGZPersistResourceKey mKey;     // +0x10
    std::vector<uint32_t> mValues;  // +0x20 begin / end / capacity

private:
    // Inline in Release: the vector's block goes back. Not virtual (the original's interface has no destructor slot); only
    // Release destroys an entry.
    ~cTSRelationshipEntry() = default;
};
static_assert(sizeof(cTSRelationshipEntry) == cTSRelationshipEntry::kSize, "cTSRelationshipEntry layout");
static_assert(offsetof(cTSRelationshipEntry, mRefCount) == 0xc, "reference count");
static_assert(offsetof(cTSRelationshipEntry, mKey) == 0x10, "resource key");
static_assert(offsetof(cTSRelationshipEntry, mValues) == 0x20, "value vector");

// Where each interface sits in the object (RTTI; base offsets are not constant expressions: the tests check them).
namespace relentry {
constexpr uint32_t kPersistFace = 0x4;
constexpr uint32_t kSerialFace = 0x8;
} // namespace relentry

// The RVA hooks' entry points (src/hook/tables/cTSRelationshipEntry.cpp). Every entry the game creates carries our vtables (its
// only builders are the table's SetEntry and Read, ours), so these run only where the game holds an entry built with its own
// vtables (none while the table's hooks are on). Original calling convention (this in ecx = the face of the slot), our
// functions called without virtual dispatch, bodies inlined. ReadFromRecord, WriteToRecord and SetValueCount have none (lane
// GT): only the game's vtable reaches their originals, trapped under the switches that keep it off entries.
struct cTSRelationshipEntryAdapters {
    using Self = cTSRelationshipEntry;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static uint32_t __fastcall GetValue(Self *self, void *, uint32_t index);
    static bool __fastcall SetValue(Self *self, void *, uint32_t index, uint32_t value);
    static bool __fastcall Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(cIGZSerializable *face, void *);
};
