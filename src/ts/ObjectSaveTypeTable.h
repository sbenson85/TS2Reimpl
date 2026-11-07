#pragma once
#include "gz/cIGZSerializable.h"
#include "ts/cTSObjectFolder.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The lot's object save table (lane MB, F4e): the resource the folder's ReadSaveTable / WriteSaveTable hand the persist
// system (catalogue slots 73 / 72), which reads or writes it through its cIGZSerializable slots. One record per saved
// object: who it was (GUID, save index, object type, name) and what it looked like when saved (two tree checks, the
// definition's attribute and array counts, its catalog string count), so a load can tell objects that are gone (kept as
// cTSMissingObject entries in the folder's list) from objects that changed (flagged through selector slot 59).
//
// RTTI ObjectSaveTypeTable (type descriptor 0x10a6308), bases cIGZSerializable, cIGZUnknown; vtable 0xdcd0c0 (7 slots).
// 0x14 bytes. The game builds it only on the stack of the folder's ReadSaveTable 0x4d46d0 and WriteSaveTable 0x4d49a0 (the
// vtable's other references are its destructors 0x4cf490 and 0x4cfb60), both ours: so in the standalone shape it is a real
// C++ class, the compiler emits its vtable, and nothing reaches the game's slot functions (in effect through callers).
//
// Record format (Write / Read; version 0x55 written, every older version read):
//   u32 class id 'objt', u32 version, u32 0
//   per entry, until a GUID of 0:
//     i32 GUID; v != 0: i32 tree check A, i32 tree check B; v >= 0x46: i32 attribute count (definition field 0x3a),
//     i32 array count (field 0x3b); v >= 0x47: i32 catalog string count; i16 save index; v > 1: i16 object type
//     (field 9); the catalog name (v >= 0x3f a GZ string, older a padded C string through the record, slot 20);
//     v >= 0x49: i32 tile flag (0 single tile, 3 multi-tile lead, 1 other part), i32 definition field 0xd0.
//   i32 0

namespace ifaces {
class cIGZIStream;
class cIGZOStream;
class cITSObjectSelector;
} // namespace ifaces

// One GUID mapping of the package's translation table: `from` stands for `to`. 12 bytes.
struct GUIDPair {
    uint32_t from;
    uint32_t to;
    uint8_t visited; // Resolve's mark: each pair is followed once per call
};
static_assert(sizeof(GUIDPair) == 12);

// GUIDTranslation (RTTI bases cIGZSerializable at +0, std::vector<GUIDPair> at +4; vtable 0xdcc87c, 6 slots): the package's
// GUID translation table, built and filled by the folder (NewGuidTranslation, then 0xc0b669). Its slots are still the
// game's; this declares the layout and the member the save table calls.
struct GUIDTranslation {
    const void *mVTable;          // +0x00 the game's 0xdcc87c
    std::vector<GUIDPair> mPairs; // +0x04
    uint32_t mRefCount;           // +0x10

    // 0xc0b617 (thiscall, ret 4): the id `id` translates to: every visited mark cleared, then, from the first pair on,
    // an unvisited pair from the current id is followed (marked, its `to` the new id) and the search starts over, until
    // no pair matches. A chain is followed to its end, and a cycle stops when it comes back to a used pair.
    uint32_t Resolve(uint32_t id);
};
static_assert(sizeof(GUIDTranslation) == 0x14);
static_assert(offsetof(GUIDTranslation, mPairs) == 4 && offsetof(GUIDTranslation, mRefCount) == 0x10);

class ObjectSaveTypeTable : public cIGZSerializable {
public:
    static constexpr uint32_t kClassId = 0x6f626a74; // 'objt', GetGZCLSID
    static constexpr uint32_t kSerializableId = 0xe4fda3d4; // cIGZSerializable
    static constexpr uint32_t kRecordVersion = 0x55;

    // The folder's stack table: the folder (its objects and its finder face), the folder's missing-object list, the
    // translation (counted while the table holds it); count 1.
    ObjectSaveTypeTable(cTSObjectFolder *folder, FolderObjects *missing, GUIDTranslation *translation);
    ObjectSaveTypeTable(const ObjectSaveTypeTable &) = delete;
    ObjectSaveTypeTable &operator=(const ObjectSaveTypeTable &) = delete;
    // 0x4cf490 (slot 6 as 0x4cfb60 with the free): the translation released.
    virtual ~ObjectSaveTypeTable();

    // shared 0x4d4ba0 (slot 0, ret 8): 1 and cIGZSerializable give this, counted through slot 1; false otherwise.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override { return ++mRefCount; } // slot 1, shared 0x146a40
    // 0x4d55c0 (slot 2): at a count of 1 or less, delete this (slot 6 with 1) and 0; else the count, decremented.
    uint32_t Release() override;

    // 0x4d7040 (slot 3, ret 4): the record (format above) for the lot's objects, then the neighbourhood's; true when the
    // stream has no error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4d4c30 (slot 4, ret 4): reads a record of any version: each entry's GUID through the translation, the folder's
    // object of that GUID given the saved index and flagged when it differs from the record; an entry without its object
    // (from 0x49 on, also one whose tile flag disagrees with the object's) becomes a missing object in the folder's list.
    // True when the stream has no error.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x4d3350 (slot 5, 6 bytes): 'objt'.
    uint32_t GetGZCLSID() override;

    // 0x4d7180 (thiscall, ret 8): one object's entry (format above), none for an object without a save index.
    void WriteEntry(ifaces::cIGZOStream *stream, ifaces::cITSObjectSelector *object);

private:
    cTSObjectFolder *mFolder;       // +0x04 (the folder's cITSBehaviorFinder face, its address)
    FolderObjects *mMissing;        // +0x08 the folder's missing objects (mLotScripts)
    GUIDTranslation *mTranslation;  // +0x0c counted
    uint32_t mRefCount;             // +0x10
};
static_assert(sizeof(ObjectSaveTypeTable) == 0x14);

// The hook entry for the destructor body 0x4cf490 (thiscall; the qualified call does no dispatch). The original's only
// references are the unwind funclets of the folder's ReadSaveTable / WriteSaveTable (both ours), and it is the only
// code that stores the game's vtable: hooked, no object can carry that vtable, which the slots' callers check rests on.
struct ObjectSaveTypeTableAdapters {
    static void __fastcall Destroy(ObjectSaveTypeTable *self, void *edx);
};
