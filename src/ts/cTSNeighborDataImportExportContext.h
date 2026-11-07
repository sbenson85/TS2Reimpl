#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts/cTSFamilyDataImportExportContext.h" // ServiceWordVector
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: a counted object reference (slot 1 AddRef, slot 2 Release)

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>
#include <unordered_map>
#include <vector>

// cTSNeighborDataImportExportContext: the context that carries neighbour data through an import or export (RTTI bases
// cITSNeighborDataImportExportContext, cITSDataImportExportContext, cIGZPersistResource at +0 and cIGZSerializable at +4;
// count at +8). 0xa8 bytes; 0x46fb80 allocates and builds one (the cTSSimCOMDirector constructor calls it), 0x46f950 is the
// deleting destructor (slot 37). Vtables 0xdc98b0 (+0, 38 slots) and 0xdc994c (+4, 6 slots).
//
// It holds two sets of neighbour ids (+0x30 "resolved", +0x38 "pending": a slot moves an id from the second to the first),
// three tables from an id to another id (+0x40, +0x60, +0x80), a vector of ids (+0xc, in the allocator service's memory), a
// list of counted objects (+0x28) and one counted object (+0xa0) the slots 12 and 13 forward to. The names of the sets and
// the tables are guesses from how the slots use them.

using NeighborIdSet = std::set<int32_t>;
static_assert(sizeof(NeighborIdSet) == 8);
using NeighborIdMap = FamilyIdMap; // unordered_map<uint32_t, uint32_t>
using NeighborContextList = FamilyContextList;

class cTSNeighborDataImportExportContext;

// The cIGZSerializable face at +4: its slots run with `this` at the face.
struct NeighborContextSerialFace {
    const void *mVTable;
    // 0x4701ad (slot 0, ret 8) and 0x47051a (slot 2): thunks to the context's QueryInterface / Release.
    uint8_t QueryInterface(uint32_t id, void **out);
    uint32_t Release();
    // 0x470820 (slot 3, ret 4): version 2, the resolved ids, the pending ids, then the tables +0x80, +0x40, +0x60 as
    // (count, then key and value of each). True when the stream is not in error.
    uint8_t Write(void *stream);
    // 0x4701c0 (slot 4, ret 4): clears the sets and tables and reads what Write wrote (the +0x40 and +0x60 tables from
    // version 2 on; nothing at all for version 0).
    uint8_t Read(void *stream);
    // 0x46fd70 (slot 5): thunk to the context's class id.
    uint32_t GetGZCLSID();
    cTSNeighborDataImportExportContext *Owner();
};

class cTSNeighborDataImportExportContext {
public:
    static constexpr uint32_t kVTable = 0xdc98b0;
    static constexpr uint32_t kSerialVTable = 0xdc994c;
    static constexpr uint32_t kClassId = 0x2db5c0f4;
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kNoValue = 0xffffffff;

    // 0x46fb80: allocates and builds an empty context (null when the allocation fails).
    static cTSNeighborDataImportExportContext *Create();
    // 0x46f950 (slot 37, ret 4): releases the held object, destroys the tables, sets, list and vector; with bit 0 frees
    // the 0xa8 bytes. Returns this.
    cTSNeighborDataImportExportContext *DeletingDestructor(uint32_t flags);
    // 0x470150 (slot 0, ret 8): ids 1, 0x456b8f1d, 0xdb5c0c2, 0x6db5bf2e answer this, 0xe4fda3d4 the serialize face; both
    // counted through slot 1.
    uint8_t QueryInterface(uint32_t id, void **out);
    // 0x470500 (slot 2): counts down; at the last reference calls the deleting destructor (slot 37) with 1 and answers 0.
    uint32_t Release();
    // 0x46fd40 (slot 12, ret 8): the held object's slot 0 (a QueryInterface) with the same arguments; 0 when none.
    uint8_t QueryHeld(uint32_t id, void **out);
    // 0x4706d0 (slot 13, ret 4): holds the object (counted), releasing the old one.
    void SetHeld(void *object);
    // 0x46fa40 (slot 16, ret 4): appends the object (counted) to the list.
    void AddObject(void *object);
    // 0x470610 (slot 17, ret 4): removes (and releases) the first list entry that is the object.
    void RemoveObject(void *object);
    // 0x46fb50 (slot 18): empties the list, releasing the objects in order.
    void ClearObjects();
    // 0x46fd00 (slot 19, ret 4): appends every list object (uncounted) to the vector.
    void CopyObjects(ServiceWordVector *out);
    // 0x46fcf0 (slot 20): the class id.
    uint32_t GetClassId();
    // 0x46fb00 (slot 22, ret 4): appends the id to the vector unless it is there.
    void AddListedId(uint32_t id);
    // 0x46fd60 (slot 23, ret 4): assigns the vector to the caller's.
    void CopyListedIds(ServiceWordVector *out);
    // 0x470030 (slot 24, ret 4): adds the id to the resolved set and takes it out of the pending one.
    void AddResolved(int32_t id);
    // 0x46ffe0 (slot 25, ret 4): adds the id to the pending set unless it is resolved.
    void AddPending(int32_t id);
    // 0x4706a0 (slot 26, ret 4): takes the id out of both sets.
    void RemoveId(int32_t id);
    // 0x470110 (slot 27, ret 4): whether the id is resolved.
    uint8_t IsResolved(int32_t id);
    // 0x470090 (slot 28, ret 4): whether the id is pending or resolved.
    uint8_t IsKnown(int32_t id);
    // 0x46fde0 (slot 29, ret 4): appends the resolved ids to the vector.
    void CopyResolved(ServiceWordVector *out);
    // 0x46fd80 (slot 30, ret 4): the same through slot 29, then the pending ids.
    void CopyAll(ServiceWordVector *out);
    // 0x470580 (slot 31, ret 8): for a known id (resolved, else pending) sets its entry in the +0x40 table; false for others.
    uint8_t SetMapA(int32_t id, uint32_t value);
    // 0x46ff50 (slot 32, ret 8): the +0x40 table's value for the key into `*value`; false when absent.
    uint8_t GetMapA(uint32_t key, uint32_t *value);
    // 0x46fe30 (slot 33, ret 4): the +0x80 table's value for the key; -1 when absent.
    uint32_t GetMapC(uint32_t key);
    // 0x470720 (slot 34, ret 8): for a known id that the +0x80 table lacks, adds its entry; false otherwise.
    uint8_t AddMapC(int32_t id, uint32_t value);
    // 0x470530 (slot 35, ret 8): when some entry of the +0x80 table has the value `a`, sets the +0x60 table's entry for `a` to
    // `b`; false when none has.
    uint8_t SetMapBForValue(uint32_t a, uint32_t b);
    // 0x46fec0 (slot 36, ret 8): the +0x60 table's value for the key into `*value`; false when absent.
    uint8_t GetMapB(uint32_t key, uint32_t *value);

    const void *mVTable;               // +0x00
    const void *mSerialVTable;         // +0x04 cIGZSerializable
    uint32_t mRefCount;                // +0x08
    ServiceWordVector mListedIds;      // +0x0c
    uint32_t mField18[4];              // +0x18 zero (the shared slots 3, 4, 8..10 treat it as a key)
    NeighborContextList mObjects;      // +0x28 counted objects
    NeighborIdSet mResolved;           // +0x30
    NeighborIdSet mPending;            // +0x38
    NeighborIdMap mMapA;               // +0x40
    NeighborIdMap mMapB;               // +0x60
    NeighborIdMap mMapC;               // +0x80
    NeighborDataRef mHeld;             // +0xa0 counted
    uint32_t mField1;                  // +0xa4 -1 (set by the shared slot 15)

    NeighborContextSerialFace *SerialFace() { return reinterpret_cast<NeighborContextSerialFace *>(&mSerialVTable); }
};
static_assert(sizeof(cTSNeighborDataImportExportContext) == 0xa8);
static_assert(offsetof(cTSNeighborDataImportExportContext, mListedIds) == 0xc);
static_assert(offsetof(cTSNeighborDataImportExportContext, mObjects) == 0x28);
static_assert(offsetof(cTSNeighborDataImportExportContext, mResolved) == 0x30);
static_assert(offsetof(cTSNeighborDataImportExportContext, mPending) == 0x38);
static_assert(offsetof(cTSNeighborDataImportExportContext, mMapA) == 0x40);
static_assert(offsetof(cTSNeighborDataImportExportContext, mMapB) == 0x60);
static_assert(offsetof(cTSNeighborDataImportExportContext, mMapC) == 0x80);
static_assert(offsetof(cTSNeighborDataImportExportContext, mHeld) == 0xa0);
