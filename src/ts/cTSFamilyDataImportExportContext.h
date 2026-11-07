#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: a counted object reference (slot 1 AddRef, slot 2 Release)

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

// cTSFamilyDataImportExportContext: the context that carries family data through an import or export (RTTI bases
// cITSFamilyDataImportExportContext, cITSDataImportExportContext, cIGZPersistResource at +0 and cIGZSerializable at +4;
// count at +8). 0x50 bytes; 0x470bf0 allocates and builds one (the cTSSimCOMDirector constructor calls it), 0x470a60 is the
// deleting destructor (slot 28). Vtables 0xdc99e0 (+0, 29 slots) and 0xdc9a58 (+4, 6 slots).
//
// It holds a table from a family id to the id it maps to (+0xc, -1 while nothing is mapped), a 16-byte resource key
// (+0x2c, set and read by the shared slots 3, 4, 8, 9, 10 which belong to several classes and are not hooked here), a list
// of counted objects (+0x3c) and two counted objects (+0x44 and +0x48) the slots 12, 13, 26 and 27 forward to.

using FamilyIdMap = std::unordered_map<uint32_t, uint32_t>;
static_assert(sizeof(FamilyIdMap) == 0x20);
using FamilyContextList = std::list<NeighborDataRef>;
static_assert(sizeof(FamilyContextList) == 8);
// A vector in the allocator service's memory (the callers' out-vectors).
using ServiceWordVector = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;
static_assert(sizeof(ServiceWordVector) == 12);

class cTSFamilyDataImportExportContext;

// The cIGZSerializable face at +4: its slots run with `this` at the face.
struct FamilyContextSerialFace {
    const void *mVTable;
    // 0x470f9d (slot 0, ret 8) and 0x471097 (slot 2): thunks to the context's QueryInterface / Release.
    uint8_t QueryInterface(uint32_t id, void **out);
    uint32_t Release();
    // 0x471260 (slot 3, ret 4): version 2, the count, then each id and the id it maps to. True when the stream is not in error.
    uint8_t Write(void *stream);
    // 0x470fb0 (slot 4, ret 4): version 2 reads a count and that many (id, mapped id) pairs; version 1 a single id (mapped to -1).
    uint8_t Read(void *stream);
    // 0x470ddd (slot 5): thunk to the context's class id.
    uint32_t GetGZCLSID();
    cTSFamilyDataImportExportContext *Owner();
};

class cTSFamilyDataImportExportContext {
public:
    static constexpr uint32_t kVTable = 0xdc99e0;
    static constexpr uint32_t kSerialVTable = 0xdc9a58;
    static constexpr uint32_t kClassId = 0x6db6f410;
    static constexpr uint32_t kSize = 0x50;
    static constexpr uint32_t kNoMapping = 0xffffffff;

    // 0x470bf0: allocates and builds an empty context (null when the allocation fails).
    static cTSFamilyDataImportExportContext *Create();
    // 0x470a60 (slot 28, ret 4): releases the two held objects (+0x48 first), clears the list and the table; with bit 0
    // frees the 0x50 bytes. Returns this.
    cTSFamilyDataImportExportContext *DeletingDestructor(uint32_t flags);
    // 0x470f40 (slot 0, ret 8): ids 1, 0x456b8f1d, 0x6db5bf2e, 0x6db6f3ff answer this, 0xe4fda3d4 the serialize face; both
    // counted through slot 1.
    uint8_t QueryInterface(uint32_t id, void **out);
    // 0x471080 (slot 2): counts down; at the last reference calls the deleting destructor (slot 28) with 1 and answers 0.
    uint32_t Release();
    // 0x470d80 (slot 12, ret 8): the held object +0x44's slot 0 (a QueryInterface) with the same arguments; 0 when none.
    uint8_t QuerySource(uint32_t id, void **out);
    // 0x4711c0 (slot 13, ret 4): holds the object (counted) as +0x44, releasing the old one.
    void SetSource(void *object);
    // 0x470b00 (slot 16, ret 4): appends the object (counted) to the list.
    void AddObject(void *object);
    // 0x471130 (slot 17, ret 4): removes (and releases) the first list entry that is the object.
    void RemoveObject(void *object);
    // 0x470bc0 (slot 18): empties the list, releasing the objects in order.
    void ClearObjects();
    // 0x470d40 (slot 19, ret 4): appends every list object (uncounted) to the vector.
    void CopyObjects(ServiceWordVector *out);
    // 0x470d30 (slot 20): the class id.
    uint32_t GetClassId();
    // 0x470eb0 (slot 22, ret 4): adds the id to the table with no mapping, unless it is there.
    void AddId(uint32_t id);
    // 0x470da0 (slot 23, ret 4): appends every id of the table (its order) to the vector.
    void CopyIds(ServiceWordVector *out);
    // 0x4710a0 (slot 24, ret 8): maps a known id; false when the id is not in the table.
    uint8_t SetMappedId(uint32_t id, uint32_t mapped);
    // 0x470e20 (slot 25, ret 8): the id an id maps to into `*mapped` (untouched for an unknown id); true when a mapping is set.
    uint8_t GetMappedId(uint32_t id, uint32_t *mapped);
    // 0x471220 (slot 26, ret 4): with an object: drops +0x48 and asks the object for interface 0xdb5c0c2 into it.
    void SetDestinationFrom(void *object);
    // 0x470e00 (slot 27, ret 8): the held object +0x48's slot 0 with the same arguments; 0 when none.
    uint8_t QueryDestination(uint32_t id, void **out);

    const void *mVTable;               // +0x00
    const void *mSerialVTable;         // +0x04 cIGZSerializable
    uint32_t mRefCount;                // +0x08
    FamilyIdMap mIdMap;                // +0x0c id -> mapped id
    uint32_t mKey[4];                  // +0x2c resource key (shared slots)
    FamilyContextList mObjects;        // +0x3c counted objects
    NeighborDataRef mSource;           // +0x44 counted
    NeighborDataRef mDestination;      // +0x48 counted
    uint32_t mField4C;                 // +0x4c -1

    FamilyContextSerialFace *SerialFace() { return reinterpret_cast<FamilyContextSerialFace *>(&mSerialVTable); }
};
static_assert(sizeof(cTSFamilyDataImportExportContext) == 0x50);
static_assert(offsetof(cTSFamilyDataImportExportContext, mIdMap) == 0xc);
static_assert(offsetof(cTSFamilyDataImportExportContext, mKey) == 0x2c);
static_assert(offsetof(cTSFamilyDataImportExportContext, mObjects) == 0x3c);
static_assert(offsetof(cTSFamilyDataImportExportContext, mSource) == 0x44);
static_assert(offsetof(cTSFamilyDataImportExportContext, mField4C) == 0x4c);
