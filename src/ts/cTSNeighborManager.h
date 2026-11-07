#pragma once
#include "gz/cGZDBSegmentPackedFile.h"      // GZServiceAllocator: vectors whose storage comes from the allocator service
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "ts/cITSDataContainer.h"
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: the counted object reference the neighbour map holds

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

// cTSNeighborManager: the neighbourhood's neighbours (Sims and their records). RTTI cTSNeighborManager, bases
// cITSNeighborManager / cIGZSerializable at +0, cIGZPersistResource at +4, cITSDataContainer at +8 and cIGZMessageTarget2
// at +0xc. 0x5c bytes; 0x4bc570 allocates and builds one. Vtables 0xdcc5e4 (36 slots), 0xdcc678, 0xdcc6ac, 0xdcc6c8.
//
// It holds the neighbours by id (a map of counted cITSNeighbor objects), the same neighbours by person data id (the hash
// map FindNeighborByKey, slot 17, and the message handler look up; family member keys are person data ids), the resource
// key, and the message ids it listens to (0xa8). The objects it calls, by interface (ifaces/, facts in db/methods/): the
// neighbours cITSNeighbor (NbrSelf is an alias), their relationship tables cITSRelationshipTable (NbrRelTable in the
// generated cITSNeighbor header), data tables cITSDataTables and person data NbrPersonData; the app singleton
// cITSGlobals (GetCachedSingleton); the persist system cITSPersistSystem (CachedServiceGetter<0x10faf6c, ...>) and its
// GetResourceManager as cIGZPersistResourceManager; segments cIGZPersistDBSegment; families cITSFamilyManager /
// cITSFamily; the neighbourhood cITSNeighborhood with cITSInventory / cITSInventoryToken; import/export contexts
// cITSDataImportExportContext / cITSNeighborDataImportExportContext.

// Neighbour id -> counted neighbour object (node 0x18 bytes; the game erases it with TreeEraser, releasing the values).
using NeighborMap = std::map<int32_t, NeighborDataRef>; // signed keys (the game's searches compare signed)
static_assert(sizeof(NeighborMap) == 8);
// Person data id -> counted neighbour (the destructor releases them; MSVC 14.40 unordered_map: max load factor, list, bucket
// vector, mask, bucket count).
using NeighborsByPersonDataId = std::unordered_map<uint32_t, NeighborDataRef>;
static_assert(sizeof(NeighborsByPersonDataId) == 0x20);
using NeighborIdVector = std::vector<uint32_t, GZServiceAllocator<uint32_t>>;
static_assert(sizeof(NeighborIdVector) == 12);

// A resource key (type, group, instance, high word) as the persist system hands them out.
struct NeighborResourceKey {
    uint32_t type, group, instance, high;
};
static_assert(sizeof(NeighborResourceKey) == 16);
using NeighborKeyList = std::vector<NeighborResourceKey, GZServiceAllocator<NeighborResourceKey>>;
using NeighborShortList = std::vector<int16_t, GZServiceAllocator<int16_t>>;

// The manager's own interface (RTTI cITSNeighborManager at +0, a cIGZSerializable: slots 3-5 are Write / Read / GetGZCLSID):
// slots 6..35, the virtual destructor at slot 34 (the scalar deleting destructor 0x4bb7d0). Neighbours, families and data
// objects are passed as their primary faces (void *: their interfaces are not declared here). Declared here until a second
// implementer appears.
class cITSNeighborManager : public cIGZSerializable {
public:
    virtual uint8_t RegisterMessages() = 0;                                                    // slot 6
    virtual uint8_t UnregisterMessages() = 0;                                                  // slot 7
    virtual uint8_t Reload(uint32_t group, uint8_t dropFinished) = 0;                          // slot 8
    virtual void Reset() = 0;                                                                  // slot 9
    virtual uint8_t Save(uint8_t all) = 0;                                                     // slot 10
    virtual uint8_t IsDirty() = 0;                                                             // slot 11 (raw byte)
    virtual void SetDirty() = 0;                                                               // slot 12
    virtual void Slot13(uint32_t unused) = 0;                                                  // slot 13 (does nothing)
    virtual uint32_t QueryNeighbor(int16_t id, int16_t query, uint32_t *out) = 0;              // slot 14
    virtual int16_t NextNeighborId(int16_t after, uint8_t includeAll) = 0;                     // slot 15
    virtual void *GetNeighbor(int32_t id, uint8_t includeAll) = 0;                             // slot 16
    virtual void *FindNeighborByKey(uint32_t key) = 0;                                         // slot 17
    virtual void GetNeighborIds(NeighborIdVector *out, uint32_t mask, uint8_t includeAll) = 0; // slot 18
    virtual uint32_t MarkFamilyChanged(void *neighbor) = 0;                                    // slot 19
    virtual int32_t CreateUserNeighbor(void **out, uint32_t templateId, uint32_t a3, uint32_t a4, uint32_t a5) = 0; // slot 20
    virtual int32_t RemoveNeighbor(void *neighbor) = 0;                                        // slot 21
    virtual int32_t RetireNeighbor(void *neighbor, uint32_t unused) = 0;                       // slot 22
    virtual uint8_t DeleteNeighborResources(const NeighborIdVector *resources, uint32_t unused) = 0; // slot 23
    virtual void RemoveNeighborsWithAttribute43(int32_t value) = 0;                            // slot 24
    virtual void RebuildFamilyAssignments() = 0;                                               // slot 25
    virtual void AddRelatedIdsToFamily(void *family) = 0;                                      // slot 26
    virtual int32_t CollectRelatedIds(void *target, NeighborIdVector *out, uint8_t extended) = 0; // slot 27
    virtual void CountRelatedNeighborsOf(void *target) = 0;                                    // slot 28
    virtual void *CreateNeighbor(void *data) = 0;                                              // slot 29
    virtual void EnsureSimInventories() = 0;                                                   // slot 30
    virtual void Slot31() = 0;                                                                 // slot 31 (does nothing)
    virtual int32_t HighestNeighborId() = 0;                                                   // slot 32
    virtual cIGZPersistResource *GetPersistFace() = 0;                                         // slot 33
    virtual ~cITSNeighborManager() = default;                                                  // slot 34
    virtual int32_t CountFlaggedNeighbors() = 0;                                               // slot 35
};

// The standalone shape (docs/standalone-shape.md): cITSNeighborManager at +0, cIGZPersistResource at +4 (the key at +0x44),
// cITSDataContainer at +8, cIGZMessageTarget2 at +0xc; the compiler emits the four vtables, the QI / Release adjustor thunks of
// the three secondary faces (the original's 0x4bd4de / 0x4be14a, 0x4bd4e6 / 0x4be152, 0x4bd4ee / 0x4be15a; their AddRef thunks
// 0x37b3c3 / 0x473592 / 0x47359a are folded) and the RTTI. Creation: no constructor function; Create 0x4bc570 (operator new
// 0x5c, constructor inline; its address registered by the cTSSimCOMDirector constructor 0x415ae0) is the only site, and the
// game's vtables are referenced only there and by the deleting destructor 0x4bb7d0. The RVA hooks on the old functions are
// adapters (cTSNeighborManagerAdapters).
class cTSNeighborManager : public cITSNeighborManager,
                           public cIGZPersistResource,
                           public cITSDataContainer,
                           public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kVTable = 0xdcc5e4;
    static constexpr uint32_t kPersistVTable = 0xdcc678;
    static constexpr uint32_t kContainerVTable = 0xdcc6ac;
    static constexpr uint32_t kMessageVTable = 0xdcc6c8;
    static constexpr uint32_t kClassId = 0xcc2a6a34;
    static constexpr uint32_t kVersion = 0xcb;
    static constexpr uint32_t kMessageId = 0xa8; // the message the manager listens to

    // Count 0, word 1, empty containers, zero key, id set {0xa8}.
    cTSNeighborManager();
    // Slot 34, 0x4bb7d0 (ret 4): destroys the id set, the key map and the neighbour map with the class vtables installed; bit 0
    // frees the 0x5c bytes. Returns this. Defined out of line so the destructor stores the vtables (docs/standalone-shape.md, FI).
    ~cTSNeighborManager() override;

    // 0x4bc570: allocates 0x5c bytes and builds the manager; null when the allocation fails.
    static cTSNeighborManager *__cdecl Create();

    // cIGZUnknown (all four faces; the secondary faces' QI / Release are the compiler's thunks)
    // 0x4bd480 (slot 0, ret 8): ids 1, 0xcc2a6a30 and 0xe4fda3d4 answer this, 0x456b8f1d the persist face, 0x8db5bf48 the
    // container face; counted through slot 1.
    bool QueryInterface(uint32_t id, void **out) override;
    uint32_t AddRef() override { return ++mRefCount; } // slot 1, folded 0x146a40
    // 0x4be130 (slot 2): counts above one go down by one; otherwise slot 34 (flags 1) and 0.
    uint32_t Release() override;

    // cIGZSerializable (+0)
    // 0x4bf6c0 (slot 3, ret 4): class id, version 0xcb, 0 (slot 9), the word at +0x18 (slot 6). Whether the stream has no error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x4bd500 (slot 4, ret 4): reads and drops three words (slot 9), empties the neighbour map, reads the word (slot 6). True.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x4bc6f0 (slot 5).
    uint32_t GetGZCLSID() override;

    // cITSNeighborManager
    // 0x4bd270 / 0x4be650 (slots 6 / 7): register / unregister the message face for the ids in the id set (through the
    // message service). AL = 1.
    uint8_t RegisterMessages() override;
    uint8_t UnregisterMessages() override;
    // 0x4bd290 (slot 8, ret 8): empties both maps, loads the group; with `dropFinished` then removes (RemoveNeighborEffects) the
    // neighbours whose person data has no sim definition or whose kind (its slot 3 (9)) is neither 2 nor 0x11. AL = 1.
    uint8_t Reload(uint32_t group, uint8_t dropFinished) override;
    // 0x4bf130 (slot 9): for every neighbour, calls its slot 7 and hands the key {0xaace2efb, group (persist face slot 6),
    // neighbour word (slot 8, sign-extended), 0} to slot 10 of the persist system's slot 3 object; then empties both maps and
    // clears +0x14 and the flag byte.
    void Reset() override;
    // 0x4be580 (slot 10, ret 4): with the persist system's slot 34 (own key, 0x656b8f0c, holder out) succeeding, stores this
    // through slot 16 (persist face, holder), saves the neighbours, calls the holder's slot 10 and clears the flag byte.
    // AL = both stores succeeded (0 when slot 34 fails); the holder is released.
    uint8_t Save(uint8_t all) override;
    uint8_t IsDirty() override { return mDirty; } // slot 11, 0x410230
    // 0x4bd470 (slot 12): sets the flag byte (+0x1a).
    void SetDirty() override;
    void Slot13(uint32_t) override {} // slot 13, folded 0x300a0 (ret 4)
    // 0x4bcd30 (slot 14, ret 12): looks up the neighbour (slot 16 with includeAll) and answers a question about it by
    // `query`: 0 first record of the neighbour's slot 10 object that is of kind 2 without flags 0x40 / 0x10000 (its slot 47),
    // 1 service flag, 2 data value 0x3a, 5 CountRelatedNeighbors then the neighbour's slot 41, 6/7/8/9/10 questions to
    // the family object of data value 0x3d. Stores 0 through `out` when given. 0 for anything else.
    uint32_t QueryNeighbor(int16_t id, int16_t query, uint32_t *out) override;
    // 0x4bd130 (slot 15, ret 8): the first neighbour id above `after` whose neighbour does not report slot 34, or any when
    // includeAll; 0 when none. AX only.
    int16_t NextNeighborId(int16_t after, uint8_t includeAll) override;
    // 0x4bc4c0 (slot 16, ret 8): the neighbour with the id (ids below 1 give null); without includeAll a neighbour that reports
    // slot 34 gives null. When the neighbour's slot 10 object has a slot 4 object, that one's slot 3 is called with 9.
    void *GetNeighbor(int32_t id, uint8_t includeAll) override;
    // 0x4bc480 (slot 17, ret 4): the neighbour whose person data id is `key` (what cITSFamily lists as members), or null (key 0
    // gives null).
    void *FindNeighborByKey(uint32_t key) override;
    // 0x4bd010 (slot 18, ret 12): empties `out`, reserves room for every neighbour and appends the ids of the neighbours that
    // pass: not reporting slot 34 (unless includeAll), and with a mask, bit (slot 16 of 0xba) of the mask set.
    void GetNeighborIds(NeighborIdVector *out, uint32_t mask, uint8_t includeAll) override;
    // 0x4be0e0 (slot 19, ret 4): calls neighbour slot 37 with 1, asks the singleton's slot 9 object (slot 11) for the object of
    // the neighbour's data value 0x3d, and returns that object's slot 23 called with 1 (0 when there is none).
    uint32_t MarkFamilyChanged(void *neighbor) override;
    // 0x4bb840 (slot 20, ret 0x14): creates a neighbour for a new user character. Looks up the template (app slot 24's
    // slot 14; template 0 or -1 means 0x7fd96b54), reads the default name (string 0x6ced4870 through the app interface),
    // then finds the first free file name "<dir>_User%05d" from the counter at +0x14 (wraps at 32000) and has app slot 24
    // create the character (slot 52, interface 0x3b2271bb, a3..a5 passed on); the character's data (slot 4 into a set,
    // handed to app slot 24's slot 25) goes to CreateNeighbor. 0 with *out = the neighbour (one reference), else -1.
    int32_t CreateUserNeighbor(void **out, uint32_t templateId, uint32_t a3, uint32_t a4, uint32_t a5) override;
    // 0x4bc0f0 (slot 21, ret 4): removes a neighbour for good. -1 when it does not report slot 35, or a family object
    // already lists its key; otherwise posts message 0x9d, RemoveNeighborEffects, marks the manager dirty and drops the
    // neighbour's household records. 0 when that worked, else -1.
    int32_t RemoveNeighbor(void *neighbor) override;
    // 0x4be980 (slot 22, ret 8): turns a neighbour into a retired one (data value 0xa5 = 1, 0x3d = 0): -1 unless it reports
    // slot 35 and has data value 0xa5 clear. Posts message 0x9e, drops its list entries, and has the resources of its record
    // group cleaned up through slot 23. 0 when that worked, else -1.
    int32_t RetireNeighbor(void *neighbor, uint32_t unused) override;
    // 0x4bed00 (slot 23, ret 8): for every persist resource in the list that gives interface 0x656b8f0c, calls its slot 26 with
    // each key whose type is not one of five listed ones. AL = 1.
    uint8_t DeleteNeighborResources(const NeighborIdVector *resources, uint32_t unused) override;
    // 0x4bc340 (slot 24, ret 4): removes (slot 21 of this) every neighbour whose attribute 0x43 equals `value`, starting
    // over after each removal that returns 0, and stopping at the first that returns non-zero.
    void RemoveNeighborsWithAttribute43(int32_t value) override;
    // 0x4bf290 (slot 25): for every family the singleton's slot 9 object lists (slot 19 into a vector, slot 11 to fetch), every
    // member found by key (slot 17 of this) through the family's kind-3 entries gets data value 0x3d set to the family's
    // slot 41 answer (member slot 17); the members' words (slot 8, sign-extended) are collected, and every neighbour whose
    // map key is not among them gets data value 0x3d set to 0.
    void RebuildFamilyAssignments() override;
    // 0x4bf1f0 (slot 26, ret 4): collects ids through slot 27 of this (family, vector, 0) and adds each to the family
    // (cITSFamily::AddToIdSet, slot 62).
    void AddRelatedIdsToFamily(void *family) override;
    // 0x4bcc10 (slot 27, ret 12): for every neighbour that the target does not report (slot 29 of its id), with a non-zero
    // data value 0x3d, looks through the target's kind-3 entries (slot 24 count, slot 26 item) for one whose key finds an
    // object (slot 17) that passes RelationsAllowExtended (extended) or RelationsAllowBasic with the neighbour; appends the
    // neighbour's id to `out`. Calls target slot 63 first. Returns the size of `out`.
    int32_t CollectRelatedIds(void *target, NeighborIdVector *out, uint8_t extended) override;
    // 0x4bf4d0 (slot 28, ret 4): CountRelatedNeighbors unless the target is null.
    void CountRelatedNeighborsOf(void *target) override;
    // 0x4bbd80 (slot 29, ret 4): makes a neighbour from a data object. Picks a free id from the counter at +0x18, creates the
    // object through the framework (class 0xaace2efb), enters it, keys its record and the key map. Null for a null argument or
    // when the framework fails.
    void *CreateNeighbor(void *data) override;
    // 0x4bc090 (slot 30): asks the neighbourhood (singleton slot 6) for the sim inventory (slot 39) of every neighbour id, which
    // creates the missing ones.
    void EnsureSimInventories() override;
    void Slot31() override {} // slot 31, folded 0x140fe0
    // 0x4bd200 (slot 32): the highest neighbour id, 0 without neighbours.
    int32_t HighestNeighborId() override;
    // Slot 33, folded 0x1c04e0: the persist face (null test included).
    cIGZPersistResource *GetPersistFace() override { return static_cast<cIGZPersistResource *>(this); }
    // 0x4bd1b0 (slot 35): how many neighbours report slot 35.
    int32_t CountFlaggedNeighbors() override;

    // Non-virtual members
    // 0x4bfba0 (ret 4): for every neighbour: gives its record (slot 48) the key {0xaace2efb, group, word, 0}; when `all` or
    // its slot 38, stores the record (persist slot 16) and calls its slot 39 with 0; then for its slot 14 object (if any)
    // with key {0x3053cf74, group, word, 0xaace2efb}: persist slot 17 with the key when the object's slot 17 reports, else
    // when its slot 14 reports, keys it (slot 4), stores it (persist slot 16) and calls its slot 15 with 0. AL = every
    // record store succeeded.
    uint8_t SaveNeighbors(uint8_t all);
    // 0x4be670 (cdecl): whether neighbour `a` and neighbour `b` have the relationship states the basic check needs, read
    // from their relationship tables (neighbour slot 12; table slot 5 (id) = known, slot 9 (id, kind) = state of kind
    // 1/3/9). False when either is null or either table lacks the other.
    static bool __cdecl RelationsAllowBasic(void *a, void *b);
    // 0x4be7f0 (cdecl): the same with the looser rule (states of kind 3 are not a veto).
    static bool __cdecl RelationsAllowExtended(void *a, void *b);
    // 0x4be170 (cdecl): remaps the ids a lot entry holds through an import context (slot 32 neighbour ids, slot 36 keys,
    // the neighbourhood context's slot 25 lots). Entries with flag 2 and at least 12 values: values 4, 6/5 (a key) and
    // 13; otherwise by type: 0x5350ec38 / 0x2eaa244c / 0x8e77e373 / 0x0eaa2454 remap the lot in value 0 (two of the types
    // set slot 9 (4) when it does not map), 0xed4cb1a4 value 1 (unmapped: `id`). Null context or entry: nothing.
    static void __cdecl RemapLotEntry(void *context, void *entry, uint32_t id);
    // 0x4bfd00 (ret 4): for a target that reports slot 36, counts the neighbours (not the target) that report slot 35, have a
    // non-zero data value 0x3d, no bit 0 in data value 0x44, and pass RelationsAllowBasic(neighbour, target); tells the
    // target through its slot 42.
    void CountRelatedNeighbors(void *target);
    // 0x4bf4f0 (ret 8): keeps the key map in step. Without a neighbour: empties the map and enters every neighbour under its
    // slot 15 key. With one: removes its key (`remove` set) or enters it (counted, replacing what was there).
    void UpdateKeyMap(void *neighbor, uint8_t remove);
    // 0x4be3f0 (ret 4): drops a neighbour from the manager. Posts message 0x9c with its word, takes it out of the key map and
    // the neighbour map, tells every other neighbour's relationship table (slot 7) and its own (slot 8, 1), the singleton's
    // slot 29 / 27 objects, the neighbour (slot 7) and deletes its record (persist slot 17 with the record's key).
    void RemoveNeighborEffects(void *neighbor);
    // 0x4bdf00 (ret 4): loads every neighbour record of the group (persist slot 36 lists the keys of type 0xaace2efb, slot 5
    // creates each object), enters them by id and calls their slot 39 with 0, then rebuilds the key map. AL = every one loaded.
    uint8_t LoadNeighbors(uint32_t group);

    // cIGZPersistResource (+4; these run with this = the face): the record key at +0x44 (the face's +0x40). Folded bodies but
    // SetKey.
    cGZPersistResourceKey GetKey() override { return mResourceKey; } // slot 3, 0x4bcd20
    // 0x4be640 (slot 4, ret 4): stores the 16-byte resource key. True.
    bool SetKey(const cGZPersistResourceKey &key) override;
    uint32_t GetType() override { return mResourceKey.type; }                                   // slot 5, 0x1674c0
    uint32_t GetGroup() override { return mResourceKey.group; }                                 // slot 6, 0x1674d0
    uint32_t GetInstance() override { return mResourceKey.instance; }                           // slot 7, 0x145520
    bool SetType(uint32_t type) override { mResourceKey.type = type; return true; }             // slot 8, 0x435250
    bool SetGroup(uint32_t group) override { mResourceKey.group = group; return true; }         // slot 9, 0x435290
    bool SetInstance(uint32_t instance) override { mResourceKey.instance = instance; return true; } // slot 10, 0x1f0f90
    uint32_t GetRefCount() override { return mRefCount; }                                       // slot 11, 0x145770

    // cITSDataContainer (+8)
    // 0x4bc700 (slot 3, ret 4): completes the container's neighbour context: neighbours named by the tokens of its listed
    // families' inventories (guid 0x4dd398a5: AddResolved for unresolved ones; guid 0xedd39930 tokens and the person-data-id
    // tokens of the resolved neighbours' sim inventories go to AddPending through a sorted id set), then hands each listed
    // neighbour's person data id to AddSourceKey. True when anything was added or AddSourceKey said so.
    bool CollectReferenced(ifaces::cITSDataImportExportContext *container) override;
    // 0x4bf710 (slot 4, ret 4): writes the neighbours the container's neighbour context (cITSNeighborDataImportExportContext,
    // CopyAll) lists into the container's segment (QueryHeld 0x656b8f0c) under the container's group, plus each resolved one's
    // sim inventory (record 0xcdb8bdc4) and its data tables record (0x3053cf74) copy. True when every write succeeded; false
    // without context or segment.
    bool Export(ifaces::cITSDataImportExportContext *container) override;
    // 0x4bd570 (slot 5, ret 4): imports the neighbours of a container: for every id of the context (CopyAll) that maps to
    // one of ours (MapId), copies the container's neighbour (from the container's character set 0xcc2a6a34 when there
    // is one, else from the persist system) onto ours: attribute block (0x1ce bytes, attribute 0x3d kept), the lot
    // attributes 0xc3..0xc6 (mapped by the NeighborhoodImportSource), related count and extra words 1/2, the relationship
    // table (ids remapped), the sim inventory (from the neighbourhood record 0x4e474248 or the segment record 0xcdb8bdc4)
    // and the data tables record. True when every inventory read succeeded; false without context or segment.
    bool Import(ifaces::cITSDataImportExportContext *container) override;

    // cIGZMessageTarget2 (+0xc)
    // 0x4bc3a0 (slot 3, ret 4): for a message of type 0xa8 whose payload (interface 0x65297976) reports slot 8 true, finds
    // the payload's key (slot 9) in the key map and calls slot 11 (argument 0) of the object found. True when it did; the
    // payload reference is released. Null message: false.
    bool DoMessage(void *message) override;

    // +0x00 / +0x04 / +0x08 / +0x0c: the four vtable pointers.
    uint32_t mRefCount = 0;        // +0x10
    uint32_t mUserFileCounter = 0; // +0x14 the "_User%05d" counter of CreateUserNeighbor (wraps at 32000); cleared by Reset
                                   //        and Reload
    uint16_t mNextNeighborId = 1;  // +0x18 where CreateNeighbor starts looking for a free id; Read/Write carry it
    uint8_t mDirty = 0;            // +0x1a IsDirty (slot 11) / SetDirty (slot 12): a raw byte
    uint8_t mPad1B;                // +0x1b (left as it was: the constructor does not store it)
    NeighborMap mNeighbors;        // +0x1c neighbour id -> counted cITSNeighbor
    NeighborsByPersonDataId mByPersonDataId; // +0x24 cITSNeighbor::GetPersonDataId -> counted cITSNeighbor
                                   //        (FindNeighborByKey, slot 17; the keys are what cITSFamily lists as members)
    cGZPersistResourceKey mResourceKey{}; // +0x44 the persist face's key, zero when built
    std::set<uint32_t> mMessageIds; // +0x54 message ids registered for the message face ({0xa8})
};
static_assert(offsetof(cTSNeighborManager, mRefCount) == 0x10);
static_assert(offsetof(cTSNeighborManager, mUserFileCounter) == 0x14);
static_assert(offsetof(cTSNeighborManager, mNextNeighborId) == 0x18);
static_assert(offsetof(cTSNeighborManager, mDirty) == 0x1a);
static_assert(offsetof(cTSNeighborManager, mNeighbors) == 0x1c);
static_assert(offsetof(cTSNeighborManager, mByPersonDataId) == 0x24);
static_assert(offsetof(cTSNeighborManager, mResourceKey) == 0x44);
static_assert(offsetof(cTSNeighborManager, mMessageIds) == 0x54);
static_assert(sizeof(cTSNeighborManager) == 0x5c);

namespace nbrmgr {
constexpr uint32_t kPersistFace = 4; // offsets of the secondary faces
constexpr uint32_t kContainerFace = 8;
constexpr uint32_t kMessageFace = 0xc;
} // namespace nbrmgr

// The RVA hooks' entry points (src/hook/tables/cTSNeighborManager.cpp): every manager the game creates carries our vtables (the
// Create hook), so these run only where the game calls an old entry point or holds a manager built with its vtables.
// Secondary-face slots take the face pointer.
struct cTSNeighborManagerAdapters {
    using Self = cTSNeighborManager;
    using Ctx = ifaces::cITSDataImportExportContext;
    static uint32_t __fastcall Release(Self *self, void *);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static uint8_t __fastcall RegisterMessages(Self *self, void *);
    static uint8_t __fastcall UnregisterMessages(Self *self, void *);
    static void __fastcall SetDirty(Self *self, void *);
    static bool __fastcall SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key);
    static uint8_t __fastcall SaveNeighbors(Self *self, void *, uint8_t all);
    static void __fastcall CountRelatedNeighbors(Self *self, void *, void *target);
    static void __fastcall CountRelatedNeighborsOf(Self *self, void *, void *target);
    static uint8_t __fastcall LoadNeighbors(Self *self, void *, uint32_t group);
    static void __fastcall RemoveNeighborEffects(Self *self, void *, void *neighbor);
    static void __fastcall UpdateKeyMap(Self *self, void *, void *neighbor, uint8_t remove);
};

// Ids the manager's code uses (the .cpp's anonymous-namespace constants carry the same values; the rewrite moves its
// uses here). Classes are what the framework creates or GetGZCLSID answers, interfaces what QueryInterface takes.
namespace neighbor_ids {
constexpr uint32_t kNeighborClass = 0xaace2efb;      // cTSNeighbor; also the type of its record (persist key word 0)
constexpr uint32_t kNeighborIid = 0x3b43bf8a;        // cITSNeighbor (persist GetResource / framework CreateInstance)
constexpr uint32_t kDataTablesClass = 0x3053cf74;    // cTSDataTables: the neighbour's data tables (cITSNeighbor slot 14)
constexpr uint32_t kManagerIid = 0xcc2a6a30;         // cITSNeighborManager (Import's character set is another manager)
constexpr uint32_t kSegmentIid = 0x656b8f0c;         // cIGZPersistDBSegment
constexpr uint32_t kMessage2StandardIid = 0x65297976; // cIGZMessage2Standard (cRZMessage2Standard::QueryInterface)
constexpr uint32_t kNeighborContextIid = 0xdb5c0c2;  // cITSNeighborDataImportExportContext
constexpr uint32_t kHoodContextIid = 0x4e9c59c7;     // NeighborhoodImportSource (the neighbourhood's import context)
constexpr uint32_t kNeighborhoodType = 0x4e474248;   // "NGBH": the cTSNeighborhood record (iid 0x7aa51c46)
constexpr uint32_t kNeighborhoodIid = 0x7aa51c46;    // cITSNeighborhood
constexpr uint32_t kSimInventoryRecordType = 0xcdb8bdc4; // a neighbour's sim inventory record (version 0xcb)
constexpr uint32_t kInventoryClass = 0xaba73af;      // cTSInventory (iid 0xaba72b8 = cITSInventory)
constexpr uint32_t kInventoryIid = 0xaba72b8;
constexpr uint32_t kOStreamIid = 0xc51b11cb;         // cIGZOStream of a record
constexpr uint32_t kIStreamIid = 0x51b11bd;          // cIGZIStream of a record
constexpr uint32_t kCharacterIid = 0x3b2271bb;       // what GscObjectDefTable::CreateCharacter hands out
constexpr uint32_t kSaveGameInfoClass = 0xcdcbe0e3;  // cITSSaveGameInfo's class (RetireNeighbor)
constexpr uint32_t kSaveGameInfoIid = 0xcdcbe0dd;
} // namespace neighbor_ids
