#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>

// cTSNeighborhoodGroupData: the data shared by a group of linked neighbourhoods (RTTI cTSNeighborhoodGroupData; bases
// cITSNeighborhoodGroupData at +0 (vtable 0xdc9fb8, 22 slots) and cRZUnknown at +4 (0xdca014, 6 slots), count at +8). 0x60
// bytes. Create 0x473c40 allocates and builds one (the constructor is inline there); 0x4729f0 is the deleting destructor (slot
// 3 of the +4 face through the thunk 0x4728af).
//
// The group holds its neighbourhoods in a hash table keyed by neighbourhood id (+0xc: id -> {counted neighbourhood, dirty
// byte}), a second hash table from another key to the entry of the first (+0x2c: key -> pointer to the entry), two counted
// pointers (+0x4c the current neighbourhood, +0x50 the first one stored), and an ordered map (+0x54) of counted objects
// walked through a cursor (+0x5c).
//
// Standalone shape (docs/standalone-shape.md, lane FP): a real C++ class deriving from its interface and rz::cRZUnknown (the
// layout has cRZUnknown's vtable and count at +4 / +8, as cTSNeighborhoodInfo's at +0x10). The compiler emits the vtables and
// the +4 face's thunks (QueryInterface 0x477307, AddRef 0x1dab28 and Release 0x1e3978 folded, the deleting destructor
// 0x4728af). Create (`new`) is the only creation site; the RVA hooks on the old slot functions are adapters
// (cTSNeighborhoodGroupDataAdapters below).

// A counted pointer to a game object, released through its slot 2 when the holder dies. Eight bytes with the flag after it.
struct NeighborhoodGroupEntry {
    void *mNeighborhood = nullptr; // +0 counted (cIGZUnknown slots 1 / 2)
    uint8_t mDirty = 0;            // +4 set when the neighbourhood needs saving
    NeighborhoodGroupEntry() = default;
    NeighborhoodGroupEntry(const NeighborhoodGroupEntry &) = delete;
    NeighborhoodGroupEntry &operator=(const NeighborhoodGroupEntry &) = delete;
    ~NeighborhoodGroupEntry();
};
static_assert(sizeof(NeighborhoodGroupEntry) == 8);

// A counted pointer stored by value in the ordered map: released when its node is erased.
struct NeighborhoodGroupHeld {
    void *mObject = nullptr;
    NeighborhoodGroupHeld() = default;
    NeighborhoodGroupHeld(const NeighborhoodGroupHeld &) = delete;
    NeighborhoodGroupHeld &operator=(const NeighborhoodGroupHeld &) = delete;
    ~NeighborhoodGroupHeld();
};
static_assert(sizeof(NeighborhoodGroupHeld) == 4);

using NeighborhoodGroupById = std::unordered_map<uint32_t, NeighborhoodGroupEntry>;
using NeighborhoodGroupByKey = std::unordered_map<int32_t, NeighborhoodGroupEntry *>;
using NeighborhoodGroupOrdered = std::map<int32_t, NeighborhoodGroupHeld>;
static_assert(sizeof(NeighborhoodGroupById) == 0x20);
static_assert(sizeof(NeighborhoodGroupByKey) == 0x20);
static_assert(sizeof(NeighborhoodGroupOrdered) == 8);
static_assert(sizeof(NeighborhoodGroupOrdered::iterator) == 4);

// cITSNeighborhoodGroupData (iid 0xf05f6b5a), slots 3..21 (only this class implements it). Slot names of the ifaces view
// (lane CJ) in the comments where they differ.
class cITSNeighborhoodGroupData : public cIGZUnknown {
public:
    virtual uint8_t Add(void *source, void *neighborhood) = 0; // slot 3 (ret 8)
    virtual uint8_t Refresh(void *source) = 0;                 // slot 4 (ret 4)
    virtual uint8_t SaveAll() = 0;                             // slot 5
    virtual uint8_t Flush() = 0;                               // slot 6
    virtual void *GetCurrent() = 0;                            // slot 7 (folded 0x169f60: +0x4c)
    virtual void *GetFirst() = 0;                              // slot 8 (folded 0x1e0950: +0x50)
    virtual void Reset() = 0;                                  // slot 9
    virtual void Rebuild(int32_t skipId) = 0;                  // slot 10 (RebuildLotIndex)
    virtual void TrackCurrent(int32_t id) = 0;                 // slot 11 (IndexLot)
    virtual void Remove(uint32_t id) = 0;                      // slot 12
    virtual void MarkDirtyById(uint32_t id) = 0;               // slot 13 (MarkDirty)
    virtual void MarkDirtyByKey(int32_t key) = 0;              // slot 14
    virtual void *GetByKey(int32_t key) = 0;                   // slot 15
    virtual void *GetById(uint32_t id) = 0;                    // slot 16
    virtual uint32_t MarkAndQuery(int32_t key) = 0;            // slot 17 (GetLotInventory)
    virtual uint32_t QueryByKey(int32_t key) = 0;              // slot 18
    virtual uint32_t BeginIteration() = 0;                     // slot 19
    virtual uint32_t Next() = 0;                               // slot 20
    virtual uint32_t NextKey(int32_t key) = 0;                 // slot 21
};

class cTSNeighborhoodGroupData : public cITSNeighborhoodGroupData, public rz::cRZUnknown {
public:
    // The game's vtables (what the original Create stores; the tests' original side).
    static constexpr uint32_t kVTable = 0xdc9fb8;
    static constexpr uint32_t kUnknownVTable = 0xdca014;
    static constexpr uint32_t kInterfaceId = 0xf05f6b5a;
    static constexpr uint32_t kSize = 0x60;

    // The constructor inline in Create 0x473c40: cRZUnknown (0x1b9052), the two tables, null pointers, the ordered map, a null
    // cursor.
    cTSNeighborhoodGroupData();
    // 0x4729f0 without the free: destroys the ordered map, releases +0x50 and +0x4c, destroys the tables, then cRZUnknown's.
    // The scalar deleting destructor (ret 4) is the compiler's: slot 3 of the +4 face (the thunk 0x4728af).
    ~cTSNeighborhoodGroupData() override;

    // 0x473c40: allocates and builds an empty group (null when the allocation fails).
    static cTSNeighborhoodGroupData *Create();

    // cIGZUnknown (both faces; the +4 face's slots 0-2 are the compiler's thunks)
    // 0x4772e0 (ret 8): the group's own interface id answers this (AddRef'd), others the cRZUnknown answer.
    bool QueryInterface(uint32_t id, void **out) override;
    uint32_t AddRef() override;  // slot 1, folded 0x1dab20: cRZUnknown's (add ecx, 4; jmp 0x148fb0)
    uint32_t Release() override; // slot 2, folded 0x1e3970: cRZUnknown's (add ecx, 4; jmp 0x1b94e9)

    // 0x476080 (slot 3, ret 8): makes `neighborhood` the current one (+0x4c), tells it about the group (its slot 14) and about
    // `source` (its slot 6), runs the refresh 0x476a40 on `source`, stores the neighbourhood under the id of the source's
    // slot 24 object (slot 6 of that) with a clear dirty flag, keeps the first one stored (+0x50, else tells the current one
    // about it: slot 12), then asks every other neighbourhood's slot 18. Answers whether all of those said yes.
    uint8_t Add(void *source, void *neighborhood) override;
    // 0x476340 (slot 4, ret 4): the refresh 0x476a40 on `source`, then every neighbourhood's slot 18, and slot 9 of the
    // group. Answers the conjunction.
    uint8_t Refresh(void *source) override;
    // 0x47a4b0 (slot 5): for each neighbourhood that is dirty or one of the two kept, hands its resource key to the persist
    // system's record table (slot 10) and asks its slot 7; then empties the table and drops the current one (+0x4c).
    uint8_t SaveAll() override;
    // 0x479590 (slot 6): for each dirty or kept neighbourhood, its slots 8 and 20; clears the dirty flag.
    uint8_t Flush() override;
    void *GetCurrent() override { return mCurrent; } // slot 7, folded 0x169f60
    void *GetFirst() override { return mFirst; }     // slot 8, folded 0x1e0950
    // 0x47a620 (slot 9): the group's slot 10 with -1.
    void Reset() override;
    // 0x47aae0 (slot 10, ret 4): rebuilds the ordered map and the second table: both emptied, then for each neighbourhood
    // every member it hands out (its slot 54, then slot 55 for the next) whose slot 18 differs from `skipId` is held in the
    // ordered map under that id and indexed in the second table under its slot 18 again, pointing at the neighbourhood's entry.
    void Rebuild(int32_t skipId) override;
    // 0x47a8a0 (slot 11, ret 4): for the current neighbourhood (+0x4c), holds what its slot 57 gives for the id in the ordered
    // map under that id, and indexes the id in the second table at the entry of the neighbourhood its slot 16 object (slot 6
    // of that) names, when the first table has one.
    void TrackCurrent(int32_t id) override;
    // 0x47adb0 (slot 12, ret 4): removes the neighbourhood with the id (released), then calls slot 9.
    void Remove(uint32_t id) override;
    // 0x476db0 (slot 13, ret 4): marks dirty the neighbourhood whose slot 16 object (slot 6 of it) is the id.
    void MarkDirtyById(uint32_t id) override;
    // 0x476df0 (slot 14, ret 4): marks dirty the entry of the second table for the key.
    void MarkDirtyByKey(int32_t key) override;
    // 0x4753c0 (slot 15, ret 4): the neighbourhood of the second table's entry for the key, or null.
    void *GetByKey(int32_t key) override;
    // 0x475340 (slot 16, ret 4): the neighbourhood stored under the id, or null.
    void *GetById(uint32_t id) override;
    // 0x474790 (slot 17, ret 4): marks the key's entry dirty and returns its neighbourhood's slot 37 for the key; 0 when absent.
    uint32_t MarkAndQuery(int32_t key) override;
    // 0x475050 (slot 18, ret 4): the key's neighbourhood's slot 57 for the key; 0 when absent.
    uint32_t QueryByKey(int32_t key) override;
    // 0x473b70 (slot 19): points the cursor at the first element of the ordered map and answers Next() (slot 20).
    uint32_t BeginIteration() override;
    // 0x476f70 (slot 20): the value at the cursor (raw pointer), moving on; 0 at the end.
    uint32_t Next() override;
    // 0x475460 (slot 21, ret 4): the key after the given one in the ordered map (the first when 0); 0 when there is none.
    uint32_t NextKey(int32_t key) override;

    // 0x476a40 (ret 4, EH; not a slot): (re)loads the group's neighbourhoods for `source` (a neighbourhood info): the table is
    // emptied, then the neighbourhood resource (NGBH, instance 1) of the info the manager names by the source's file prefix and
    // those of its sub-neighbourhoods are fetched from the persist system, told about the group and their info, stored by id
    // (clean) and, for the source's own, made current. The first becomes +0x50 and the sub-neighbourhoods' delegate. Answers
    // whether every one fetched initialised (0 without a manager or when the main one is missing).
    uint8_t LoadNeighborhoods(void *source);

    // +0x00 cITSNeighborhoodGroupData vtable, +0x04 cRZUnknown vtable, +0x08 its count (mRefCount)
    NeighborhoodGroupById mById;           // +0x0c
    NeighborhoodGroupByKey mByKey;         // +0x2c
    void *mCurrent;                        // +0x4c counted
    void *mFirst;                          // +0x50 counted
    union {                                // built in the constructor's body and destroyed first in the destructor's (the
        NeighborhoodGroupOrdered mOrdered; // original destroys it before releasing +0x50 / +0x4c): +0x54
    };
    NeighborhoodGroupOrdered::iterator mCursor; // +0x5c
};
static_assert(sizeof(cTSNeighborhoodGroupData) == 0x60);
static_assert(offsetof(cTSNeighborhoodGroupData, mRefCount) == 0x8);
static_assert(offsetof(cTSNeighborhoodGroupData, mById) == 0xc);
static_assert(offsetof(cTSNeighborhoodGroupData, mByKey) == 0x2c);
static_assert(offsetof(cTSNeighborhoodGroupData, mCurrent) == 0x4c);
static_assert(offsetof(cTSNeighborhoodGroupData, mFirst) == 0x50);
static_assert(offsetof(cTSNeighborhoodGroupData, mOrdered) == 0x54);
static_assert(offsetof(cTSNeighborhoodGroupData, mCursor) == 0x5c);

namespace nbgroup {
constexpr uint32_t kUnknownFace = 4; // the cRZUnknown face's offset
}

// The RVA hooks' targets: old entry points for direct calls and for objects carrying the game's vtables. Each forwards with a
// qualified call (no virtual dispatch) on the slot's face.
struct cTSNeighborhoodGroupDataAdapters {
    using Self = cTSNeighborhoodGroupData;
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);                         // 0x4729f0
    static Self *__fastcall UnknownDeletingDestructor(rz::cRZUnknown *face, void *, uint32_t flags);       // 0x4728af
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);                    // 0x4772e0
    static bool __fastcall UnknownQueryInterface(rz::cRZUnknown *face, void *, uint32_t id, void **out);   // 0x477307
    static uint8_t __fastcall Add(Self *self, void *, void *source, void *neighborhood);                   // 0x476080
    static uint8_t __fastcall Refresh(Self *self, void *, void *source);                                  // 0x476340
    static uint8_t __fastcall SaveAll(Self *self, void *);                                                // 0x47a4b0
    static uint8_t __fastcall Flush(Self *self, void *);                                                  // 0x479590
    static void __fastcall Reset(Self *self, void *);                                                     // 0x47a620
    static void __fastcall Rebuild(Self *self, void *, int32_t skipId);                                   // 0x47aae0
    static void __fastcall TrackCurrent(Self *self, void *, int32_t id);                                  // 0x47a8a0
    static void __fastcall Remove(Self *self, void *, uint32_t id);                                       // 0x47adb0
    static void __fastcall MarkDirtyById(Self *self, void *, uint32_t id);                                // 0x476db0
    static void __fastcall MarkDirtyByKey(Self *self, void *, int32_t key);                               // 0x476df0
    static void *__fastcall GetByKey(Self *self, void *, int32_t key);                                    // 0x4753c0
    static void *__fastcall GetById(Self *self, void *, uint32_t id);                                     // 0x475340
    static uint32_t __fastcall MarkAndQuery(Self *self, void *, int32_t key);                             // 0x474790
    static uint32_t __fastcall QueryByKey(Self *self, void *, int32_t key);                               // 0x475050
    static uint32_t __fastcall BeginIteration(Self *self, void *);                                        // 0x473b70
    static uint32_t __fastcall Next(Self *self, void *);                                                  // 0x476f70
    static uint32_t __fastcall NextKey(Self *self, void *, int32_t key);                                  // 0x475460
    static uint8_t __fastcall LoadNeighborhoods(Self *self, void *, void *source);                        // 0x476a40
};
