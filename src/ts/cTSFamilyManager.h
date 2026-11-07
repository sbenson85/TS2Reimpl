#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector: the vectors of counted family pointers
#include "ts/cITSDataContainer.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

// cTSFamilyManager: the neighbourhood's households (cTSFamily objects). RTTI cTSFamilyManager, bases cITSFamilyManager at
// +0, cITSDataContainer at +4 and cIGZMessageTarget2 at +8. 0x40 bytes; 0x4b5440 allocates and builds one. Vtables 0xdcc3d4
// (27 slots), 0xdcc444 (container face, 6 slots) and 0xdcc460 (message face, 4 slots). RVAs are 1.18.0.187.
//
// It holds the loaded families (sorted by their slot 51 value, then id, both descending), the families removed since the last save (whose
// records the next save deletes), the neighbourhood group they were loaded for, and a log of up to 25 texts recorded from
// message 0xcded7b51 while recording is on (messages 0xb2 / 0xb5 start / stop it).

// One log entry: a cRZString (vtable, std::string, count) destroyed through its virtual destructor (slot 33, flags 0) as
// the game's list does. Only built in place by FamilyLogEntry(text).
struct FamilyLogEntry {
    alignas(4) uint8_t mString[0x20];
    explicit FamilyLogEntry(const char *text);
    FamilyLogEntry(const FamilyLogEntry &) = delete;
    FamilyLogEntry &operator=(const FamilyLogEntry &) = delete;
    ~FamilyLogEntry();
};
using FamilyLog = std::list<FamilyLogEntry>; // node 0x28 bytes, as the game's
static_assert(sizeof(FamilyLog) == 8);

// cITSFamilyManager (the primary face, 27 slots): the original's name and slot order, for the class to implement. Callers of
// other objects keep using the generated ifaces:: view (same ABI). The virtual destructor is slot 26 (the deleting destructor).
class cITSFamilyManager : public cIGZUnknown {
public:
    virtual uint8_t RegisterMessages() = 0;                                                   // slot 3
    virtual uint8_t UnregisterMessages() = 0;                                                 // slot 4
    virtual uint8_t Load(uint32_t group) = 0;                                                 // slot 5
    virtual void Unload() = 0;                                                                // slot 6
    virtual uint8_t Save() = 0;                                                               // slot 7
    virtual int32_t GetFamilyWorth(void *family) = 0;                                         // slot 8
    virtual void ReportStatistics() = 0;                                                      // slot 9
    virtual void *FindFamilyByLot(int32_t lot) = 0;                                           // slot 10
    virtual void *GetFamily(int32_t id) = 0;                                                  // slot 11
    virtual void *FindFamilyBySlot44(int32_t value) = 0;                                      // slot 12
    virtual uint8_t CreateFamily(void **out, int32_t id) = 0;                                 // slot 13
    virtual int32_t RemoveFamily(void *family) = 0;                                           // slot 14
    virtual void RemoveEmptyFamilies() = 0;                                                   // slot 15
    virtual int32_t GetFamilyCount() = 0;                                                     // slot 16
    virtual int32_t AddNeighborToFamily(void *neighbor, void *family, void *previousFamily) = 0; // slot 17
    virtual int32_t RemoveNeighborFromFamily(void *neighbor, void *family) = 0;               // slot 18
    virtual void GetFamilyIds(void *ids) = 0;                                                 // slot 19
    virtual void GetHomelessFamilyIds(void *ids) = 0;                                         // slot 20
    virtual uint8_t MoveFamilyIntoLot(int32_t id, int32_t lot) = 0;                           // slot 21
    virtual uint8_t IsObjectInFamily(int32_t id, void *object) = 0;                           // slot 22
    virtual int32_t CanMoveFamilyIntoLot(int32_t id, int32_t lot) = 0;                        // slot 23
    virtual int32_t CheckLotAges(int32_t id, int32_t lot) = 0;                                // slot 24
    virtual int32_t CheckLotMembers(int32_t id, int32_t lot) = 0;                             // slot 25
    virtual ~cITSFamilyManager() = default;                                                   // slot 26
};

// Standalone shape (docs/standalone-shape.md): a real C++ class. The compiler emits its three vtables (the original's slot
// order), the adjustor thunks of the two secondary faces and the RTTI; the constructor installs them. Every manager the game
// creates comes from Create 0x4b5440 (constructor inline; its address registered by the cTSSimCOMDirector constructor
// 0x415ae0, hooked), so it carries our vtables. The game's vtables are referenced only by Create and the deleting destructor
// 0x4b4550. The RVA hooks on the old slot functions are adapters (cTSFamilyManagerAdapters below).
class cTSFamilyManager : public cITSFamilyManager, public cITSDataContainer, public cIGZMessageTarget2 {
public:
    // The game's vtables (what the original Create stores; the tests' original side).
    static constexpr uint32_t kVTable = 0xdcc3d4;
    static constexpr uint32_t kContainerVTable = 0xdcc444;
    static constexpr uint32_t kMessageVTable = 0xdcc460;
    static constexpr uint32_t kFamilyType = 0x46414d49;      // the family record's type (and the family class id)
    static constexpr uint32_t kFamilyInterface = 0x7ab5bcbb; // cITSFamily
    static constexpr uint32_t kLogLimit = 25;

    // 0x4b5440 (__cdecl): allocates 0x40 bytes and builds an empty manager; null when the allocation fails.
    static cTSFamilyManager *__cdecl Create();
    // Inline in Create: vtables, count 0, empty vectors, recording off, empty id set and log. +0x10 is left as the allocation
    // found it.
    cTSFamilyManager();
    // 0x4b4550 is the deleting destructor (slot 26, ret 4): the members destroyed with the class vtables installed (log, id
    // set, removed families, families); bit 0 frees the 0x40 bytes. The compiler's scalar deleting destructor fills the slot.
    ~cTSFamilyManager() override;
    cTSFamilyManager(const cTSFamilyManager &) = delete;
    cTSFamilyManager &operator=(const cTSFamilyManager &) = delete;

    // ---- cIGZUnknown (all three faces; the secondary faces' QI / Release are the compiler's thunks 0x4b5f99 / 0x4b5fa1,
    // 0x4b6897 / 0x4b689f; their AddRef thunks are the folded 0x1f4daf / 0x1f4db7) ----
    // 0x4b5f50 (slot 0, ret 8): ids 1 and 0x0c1e4682 answer this, 0x8db5bf48 the container face; counted through slot 1.
    bool QueryInterface(uint32_t id, void **out) override;
    // slot 1 (folded 0x15e520): ++count.
    uint32_t AddRef() override { return ++mRefCount; }
    // 0x4b6880 (slot 2): counts above one go down by one; otherwise slot 26 (flags 1) and 0.
    uint32_t Release() override;

    // ---- cITSFamilyManager ----
    // 0x4b5770 / 0x4b6e40 (slots 3 / 4): register (after adding 0xb2, 0xb5, 0xcded7b51, 0xb6, 0xaf to the id set) / unregister
    // the message face for the ids in the set. AL = 1.
    uint8_t RegisterMessages() override;
    uint8_t UnregisterMessages() override;
    // 0x4b5860 (slot 5, ret 4): loads the families of the group (see the .cpp). AL = 1.
    uint8_t Load(uint32_t group) override;
    // 0x4b6eb0 (slot 6): drops every family's record from the persist cache (and the family's resources), then empties both
    // vectors.
    void Unload() override;
    // 0x4b6c80 (slot 7): deletes the removed families' records, then saves the families. AL = 1.
    uint8_t Save() override;
    // 0x4b5680 (slot 8, ret 4): the family's funds (slot 53) plus its home lot's worth (slot 59).
    int32_t GetFamilyWorth(void *family) override;
    // 0x4b45b0 (slot 9): gathers neighbourhood statistics for a report (see the .cpp).
    void ReportStatistics() override;
    // 0x4b5640 (slot 10, ret 4): the first family whose home lot (slot 42) is the lot, or null.
    void *FindFamilyByLot(int32_t lot) override;
    // 0x4b5540 (slot 11, ret 4): the family with the id (slot 41), or null; id -2 gives the family with the lowest id in
    // 1..31999.
    void *GetFamily(int32_t id) override;
    // 0x4b56b0 (slot 12, ret 4): the first family whose slot 44 value matches, or null.
    void *FindFamilyBySlot44(int32_t value) override;
    // 0x4b5c00 (slot 13, ret 8): creates a family (see the .cpp). AL = 1, or 0 when no id below 32000 is free.
    uint8_t CreateFamily(void **out, int32_t id) override;
    // 0x4b6920 (slot 14, ret 4): moves a family without a home lot from the families to the removed families. 0 when it was
    // removed, -1 otherwise.
    int32_t RemoveFamily(void *family) override;
    // 0x4b68b0 (slot 15): removes (slot 14) every family with an id that has no members of either kind (slot 24 with 3),
    // moving it out of its lot first (slot 43 with 0).
    void RemoveEmptyFamilies() override;
    // slot 16 (folded 0x3a5a20): the number of families.
    int32_t GetFamilyCount() override { return int32_t(mFamilies.mLast - mFamilies.mFirst); }
    // 0x4b4a40 (slot 17, ret 12): moves a neighbour into a family (see the .cpp). 0, or -1 when it did nothing.
    int32_t AddNeighborToFamily(void *neighbor, void *family, void *previousFamily) override;
    // 0x4b6b10 (slot 18, ret 8): removes a neighbour from its family (see the .cpp). 0.
    int32_t RemoveNeighborFromFamily(void *neighbor, void *family) override;
    // 0x4b55e0 / 0x4b56f0 (slots 19 / 20, ret 4): append the family ids (as 16 bits) to the vector; slot 20 only those of
    // families without a home lot (slot 42 below 1) and with an id in 1..31999.
    void GetFamilyIds(void *ids) override;
    void GetHomelessFamilyIds(void *ids) override;
    // 0x4b5ef0 (slot 21, ret 8): moves the family into the lot unless another family lives there. AL = 0 when one does.
    uint8_t MoveFamilyIntoLot(int32_t id, int32_t lot) override;
    // 0x4b57d0 (slot 22, ret 8): whether the family of the object's neighbour has the id. AL.
    uint8_t IsObjectInFamily(int32_t id, void *object) override;
    // 0x4b4f30 / 0x4b4e40 / 0x4b4d40 (slots 23 / 24 / 25, ret 8): status codes for moving a family into a lot.
    int32_t CanMoveFamilyIntoLot(int32_t id, int32_t lot) override;
    int32_t CheckLotAges(int32_t id, int32_t lot) override;
    int32_t CheckLotMembers(int32_t id, int32_t lot) override;

    // ---- cITSDataContainer (+4) ----
    // slot 3 (folded 0x18a7a0): false.
    bool CollectReferenced(ifaces::cITSDataImportExportContext *) override { return false; }
    // 0x4b6f90 (slot 4, ret 4): exports the families named by the container's id list into the container's data store.
    bool Export(ifaces::cITSDataImportExportContext *context) override;
    // 0x4b5fb0 (slot 5, ret 4): imports the families of a container: every family record of the container's group is
    // read, copied onto the family its id maps to (the container's family context, slot 25), together with the lot data (see
    // the .cpp). AL = the result of the last lot data read (1 when the family had lot data to bring over, else 0 or 1).
    bool Import(ifaces::cITSDataImportExportContext *context) override;

    // ---- cIGZMessageTarget2 (+8) ----
    // 0x4b5180 (slot 3, ret 4): the message handler (see the .cpp).
    bool DoMessage(void *message) override;

    uint32_t mRefCount;              // +0x0c
    uint32_t mGroup;                 // +0x10 the neighbourhood group the families were loaded for (Load)
    RefPtrVector mFamilies;          // +0x14 sorted by slot 51, then id (descending)
    RefPtrVector mRemovedFamilies;   // +0x20 removed since the last save
    uint8_t mRecording;              // +0x2c messages 0xb2 / 0xb5
    uint8_t mPad2D[3];               // +0x2d
    // Built in the constructor's body and destroyed in the destructor's, in the original's order (members with destructors
    // would be destroyed after the vectors).
    union { std::set<uint32_t> mMessageIds; }; // +0x30
    union { FamilyLog mLog; };                 // +0x38
};
static_assert(offsetof(cTSFamilyManager, mRefCount) == 0x0c);
static_assert(offsetof(cTSFamilyManager, mFamilies) == 0x14);
static_assert(offsetof(cTSFamilyManager, mRemovedFamilies) == 0x20);
static_assert(offsetof(cTSFamilyManager, mRecording) == 0x2c);
static_assert(offsetof(cTSFamilyManager, mMessageIds) == 0x30);
static_assert(offsetof(cTSFamilyManager, mLog) == 0x38);
static_assert(sizeof(cTSFamilyManager) == 0x40);

// The RVA hooks' entry points (src/hook/tables/cTSFamilyManager.cpp). Every manager the game creates carries our vtables, so
// these run only where the game calls an old entry point directly or holds a manager built with its own vtables (none while
// the Create hook is on). Original calling convention (this in ecx = the face of the slot), our functions called without
// virtual dispatch, bodies inlined.
struct cTSFamilyManagerAdapters {
    using Self = cTSFamilyManager;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static uint8_t __fastcall UnregisterMessages(Self *self, void *);
    // the +4 face
    // the +8 face
};
