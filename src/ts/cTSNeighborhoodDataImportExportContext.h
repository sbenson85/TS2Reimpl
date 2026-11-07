#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts/cTSFamilyDataImportExportContext.h" // ServiceWordVector, FamilyContextList
#include "ts/cTSNeighborDataImportExportContext.h" // NeighborIdSet
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef

#include <cstddef>
#include <cstdint>
#include <unordered_map>

// cTSNeighborhoodDataImportExportContext: the context that carries a whole neighbourhood's data (its neighbourhood ids, its
// families and its neighbours) through an import or export. It is the sum of the family and neighbour contexts: RTTI bases
// cITSNeighborhoodDataImportExportContext (+8, 32 slots), cITSFamilyDataImportExportContext (+0xc, 28 slots) and
// cITSNeighborDataImportExportContext (+0x10, 37 slots) are the three data faces, cIGZSerializable is +0x14 (6 slots), and the
// cRZUnknown at +0 holds the count (+4). 0x110 bytes; 0x473be0 allocates and builds one and answers the +8 face (the
// cTSSimCOMDirector constructor calls it), 0x472040 is the constructor, slot 3 of the +0 vtable (0x4728f0) the deleting
// destructor. Vtables 0xdc9df0 (+0, 6 slots), 0xdc9e0c (+8), 0xdc9e90 (+0xc), 0xdc9f04 (+0x10), 0xdc9f9c (+0x14). Every
// 8-byte slot of the faces at +0xc and +0x10 is a `sub ecx, N; jmp` thunk to the +8 face's or the +0xc face's body (not hooked).
//
// The slots of the +0xc face are the family context's (22 add id, 23 copy ids, 24 set mapped id, 25 get mapped id, 27 query the
// owner) over the map at +0x60, those of the +0x10 face the neighbour context's (22 forwards to the +0xc face, 24..36 over the
// sets at +0x80 / +0x88 and the maps at +0x90 / +0xb0 / +0xd0). The +8 face repeats the family slots 22..25 over the map at
// +0x18, has a second id table at +0x38 (slots 28, 29) and a single (key, value) pair at +0x58 (slots 26, 27). The names of the
// tables are guesses from how the slots use them.

// The id tables (the shared MSVC 14.40 unordered_map of the sibling contexts).
using NhIdMap = std::unordered_map<uint32_t, uint32_t>;
static_assert(sizeof(NhIdMap) == 0x20);

class cTSNeighborhoodDataImportExportContext;

struct NeighborhoodResourceKey {
    uint32_t words[4]; // type, group, instance, high
};
static_assert(sizeof(NeighborhoodResourceKey) == 16);

// The cITSNeighborhoodDataImportExportContext face at +8: its slots run with `this` at the face.
struct NeighborhoodContextHoodFace {
    const void *mVTable;
    // 0x4749f0 (slot 3, ret 4): copies the key out.
    void GetKey(NeighborhoodResourceKey *out);
    // 0x479a40 (slot 4, ret 4): sets the key; true.
    uint8_t SetKey(const NeighborhoodResourceKey *key);
    // 0x479cb0 (slot 8, ret 4) and 0x479a00 (slot 10, ret 4): set the key's type and instance; true. (The group setter,
    // 0x4799d0, is shared with other classes.)
    uint8_t SetType(uint32_t type);
    uint8_t SetInstance(uint32_t instance);
    // 0x4741c0 (slot 12, ret 8): the held object's slot 0 (a QueryInterface) with the same arguments; 0 when none.
    uint8_t QueryHeld(uint32_t id, void **out);
    // 0x479940 (slot 13, ret 4): holds the object (counted), releasing the old one.
    void SetHeld(void *object);
    // 0x479990 (slot 15, ret 4): stores the word at +0x10c.
    void SetField10C(uint32_t value);
    // 0x472d30 (slot 16, ret 4): appends the object (counted) to the list.
    void AddObject(void *object);
    // 0x4789c0 (slot 17, ret 4): removes (and releases) the first list entry that is the object.
    void RemoveObject(void *object);
    // 0x4737a0 (slot 18): empties the list, releasing the objects in order.
    void ClearObjects();
    // 0x474160 (slot 19, ret 4): appends every list object (uncounted) to the vector.
    void CopyObjects(ServiceWordVector *out);
    // 0x473f20 (slot 20): the class id.
    uint32_t GetClassId();
    // 0x475ed0 (slot 22, ret 4): adds the id to the +0x18 table with no mapping, unless it is there.
    void AddId(uint32_t id);
    // 0x474db0 (slot 23, ret 4): appends every id of the +0x18 table (its order) to the vector.
    void CopyIds(ServiceWordVector *out);
    // 0x478920 (slot 24, ret 8): maps a known id of the +0x18 table; false when the id is not there.
    uint8_t SetMappedId(uint32_t id, uint32_t mapped);
    // 0x4759c0 (slot 25, ret 8): the id an id maps to into `*mapped` (untouched for an unknown id); true when a mapping is set.
    uint8_t GetMappedId(uint32_t id, uint32_t *mapped);
    // 0x4789b0 (slot 26, ret 8): stores the pair's key at +0x58 (the second argument is ignored).
    void SetPairKey(uint32_t key, uint32_t unused);
    // 0x475a50 (slot 27, ret 8): when the key is the pair's key, the pair's value into `*value`.
    uint8_t GetPairValue(uint32_t key, uint32_t *value);
    // 0x478720 (slot 28, ret 8): adds the id to the +0x38 table with the value, unless it is there.
    void AddAux(uint32_t id, uint32_t value);
    // 0x475780 (slot 29, ret 8): the +0x38 table's value for the id into `*value`; false when absent.
    uint8_t GetAux(uint32_t id, uint32_t *value);

    cTSNeighborhoodDataImportExportContext *Owner();
};

// The cITSFamilyDataImportExportContext face at +0xc.
struct NeighborhoodContextFamilyFace {
    const void *mVTable;
    // 0x475e40 (slot 22, ret 4), 0x474210 (slot 23, ret 4), 0x4787b0 (slot 24, ret 8), 0x475810 (slot 25, ret 8): the family
    // context's id table slots over the map at +0x60.
    void AddId(uint32_t id);
    void CopyIds(ServiceWordVector *out);
    uint8_t SetMappedId(uint32_t id, uint32_t mapped);
    uint8_t GetMappedId(uint32_t id, uint32_t *mapped);
    // 0x475210 (slot 27, ret 8): the owner's QueryInterface (slot 0 of the +0 vtable) with the same arguments.
    uint8_t QueryOwner(uint32_t id, void **out);

    cTSNeighborhoodDataImportExportContext *Owner();
};

// The cITSNeighborDataImportExportContext face at +0x10.
struct NeighborhoodContextNeighborFace {
    const void *mVTable;
    // 0x472e00 (slot 22): the +0xc face's slot 22 (add id).
    void AddListedId(uint32_t id);
    // 0x475fb0 (slot 24, ret 4): adds the id to the resolved set and takes it out of the pending one.
    void AddResolved(int32_t id);
    // 0x475f60 (slot 25, ret 4): adds the id to the pending set unless it is resolved.
    void AddPending(int32_t id);
    // 0x478d50 (slot 26, ret 4): takes the id out of both sets.
    void RemoveId(int32_t id);
    // 0x4762f0 (slot 27, ret 4): whether the id is resolved.
    uint8_t IsResolved(int32_t id);
    // 0x476270 (slot 28, ret 4): whether the id is pending or resolved.
    uint8_t IsKnown(int32_t id);
    // 0x474370 (slot 29, ret 4): appends the resolved ids to the vector.
    void CopyResolved(ServiceWordVector *out);
    // 0x474310 (slot 30, ret 4): the same through slot 29, then the pending ids.
    void CopyAll(ServiceWordVector *out);
    // 0x478890 (slot 31, ret 8): for a known id (resolved, else pending) sets its entry in the +0x90 table; false for others.
    uint8_t SetMapA(int32_t id, uint32_t value);
    // 0x475930 (slot 32, ret 8): the +0x90 table's value for the key into `*value`; false when absent.
    uint8_t GetMapA(uint32_t key, uint32_t *value);
    // 0x475220 (slot 33, ret 4): the +0xd0 table's value for the key; -1 when absent.
    uint32_t GetMapC(uint32_t key);
    // 0x479a80 (slot 34, ret 8): for a known id (pending, else resolved) that the +0xd0 table lacks, adds its entry; false otherwise.
    uint8_t AddMapC(int32_t id, uint32_t value);
    // 0x478840 (slot 35, ret 8): when some entry of the +0xd0 table has the value `a`, sets the +0xb0 table's entry for `a` to
    // `b`; false when none has.
    uint8_t SetMapBForValue(uint32_t a, uint32_t b);
    // 0x4758a0 (slot 36, ret 4): the +0xb0 table's value for the key into `*value`; false when absent.
    uint8_t GetMapB(uint32_t key, uint32_t *value);

    cTSNeighborhoodDataImportExportContext *Owner();
};

// The cIGZSerializable face at +0x14.
struct NeighborhoodContextSerialFace {
    const void *mVTable;
    // 0x47b150 (slot 3, ret 4): version 2, the +0x18 table, the +0x60 table, the resolved ids, the pending ids, then the tables
    // +0xd0, +0x90, +0xb0 as (count, then key and value of each). True when the stream is not in error.
    uint8_t Write(void *stream);
    // 0x477980 (slot 4, ret 4): clears the sets and tables and reads what Write wrote (the +0x18 table from version 1 on, the
    // rest from version 2 on).
    uint8_t Read(void *stream);

    cTSNeighborhoodDataImportExportContext *Owner();
};

class cTSNeighborhoodDataImportExportContext {
public:
    static constexpr uint32_t kVTable = 0xdc9df0;
    static constexpr uint32_t kHoodVTable = 0xdc9e0c;
    static constexpr uint32_t kFamilyVTable = 0xdc9e90;
    static constexpr uint32_t kNeighborVTable = 0xdc9f04;
    static constexpr uint32_t kSerialVTable = 0xdc9f9c;
    static constexpr uint32_t kClassId = 0x6e9c59cf;
    static constexpr uint32_t kSize = 0x110;
    static constexpr uint32_t kNoValue = 0xffffffff;

    // 0x472040 (ret 0): builds an empty context in the memory at this; returns this.
    cTSNeighborhoodDataImportExportContext *Construct();
    // 0x473be0: allocates and builds an empty context; answers its +8 face (null when the allocation fails).
    static void *Create();
    // 0x4728f0 (slot 3, ret 4): releases the held object, destroys the tables, sets and list; with bit 0 frees the 0x110 bytes.
    // Returns this.
    cTSNeighborhoodDataImportExportContext *DeletingDestructor(uint32_t flags);
    // 0x477240 (slot 0, ret 8): ids 0x6db5bf2e, 0x4e9c59c7 and 0x456b8f1d answer the +8 face, 0x6db6f3ff the +0xc face,
    // 0xdb5c0c2 the +0x10 face, 0xe4fda3d4 the serialize face (counted through slot 1); anything else asks the cRZUnknown
    // helper (id 1 only).
    uint8_t QueryInterface(uint32_t id, void **out);

    const void *mVTable;                // +0x00
    uint32_t mRefCount;                 // +0x04 (the cRZUnknown count)
    const void *mHoodVTable;            // +0x08
    const void *mFamilyVTable;          // +0x0c
    const void *mNeighborVTable;        // +0x10
    const void *mSerialVTable;          // +0x14
    NhIdMap mHoodIds;               // +0x18 id -> mapped id (-1 = none)
    NhIdMap mAux;                   // +0x38 id -> value
    uint32_t mPairKey;                  // +0x58 (-1)
    uint32_t mPairValue;                // +0x5c (1)
    NhIdMap mFamilyIds;             // +0x60 id -> mapped id (-1 = none)
    NeighborIdSet mResolved;            // +0x80
    NeighborIdSet mPending;             // +0x88
    NhIdMap mMapA;                // +0x90
    NhIdMap mMapB;                // +0xb0
    NhIdMap mMapC;                // +0xd0
    NeighborhoodResourceKey mKey;       // +0xf0
    NeighborContextList mObjects;       // +0x100 counted objects
    void *mHeld;                        // +0x108 counted
    uint32_t mField10C;                 // +0x10c -1

    NeighborhoodContextHoodFace *HoodFace() { return reinterpret_cast<NeighborhoodContextHoodFace *>(&mHoodVTable); }
    NeighborhoodContextFamilyFace *FamilyFace() { return reinterpret_cast<NeighborhoodContextFamilyFace *>(&mFamilyVTable); }
    NeighborhoodContextNeighborFace *NeighborFace() { return reinterpret_cast<NeighborhoodContextNeighborFace *>(&mNeighborVTable); }
    NeighborhoodContextSerialFace *SerialFace() { return reinterpret_cast<NeighborhoodContextSerialFace *>(&mSerialVTable); }
};
static_assert(sizeof(cTSNeighborhoodDataImportExportContext) == 0x110);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mHoodIds) == 0x18);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mAux) == 0x38);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mPairKey) == 0x58);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mFamilyIds) == 0x60);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mResolved) == 0x80);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mPending) == 0x88);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mMapA) == 0x90);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mMapB) == 0xb0);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mMapC) == 0xd0);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mKey) == 0xf0);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mObjects) == 0x100);
static_assert(offsetof(cTSNeighborhoodDataImportExportContext, mHeld) == 0x108);

inline cTSNeighborhoodDataImportExportContext *NeighborhoodContextHoodFace::Owner()
{
    return reinterpret_cast<cTSNeighborhoodDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 8);
}
inline cTSNeighborhoodDataImportExportContext *NeighborhoodContextFamilyFace::Owner()
{
    return reinterpret_cast<cTSNeighborhoodDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 0xc);
}
inline cTSNeighborhoodDataImportExportContext *NeighborhoodContextNeighborFace::Owner()
{
    return reinterpret_cast<cTSNeighborhoodDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 0x10);
}
inline cTSNeighborhoodDataImportExportContext *NeighborhoodContextSerialFace::Owner()
{
    return reinterpret_cast<cTSNeighborhoodDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 0x14);
}
