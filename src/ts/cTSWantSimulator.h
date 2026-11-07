#pragma once
// cTSWantSimulator (wants and fears): layout from the constructor 0x4855d0 and destructor 0x485ba0.
// Bases: cITSWantSimulator (+0), cIGZMessageTarget2 (+4), cITSDataContainer (+8). Size 0x114.
// Field names are tentative where only the container shape is known (node sizes from the
// constructor's head allocations and the destructor's erase helpers).
// Standalone shape (lane GA, docs/standalone-shape.md): a real class on its three faces; our constructor (the hook on
// 0x4855d0, the class's switch) builds every simulator with our vtables. Slots still game code forward to the original
// body with OriginalFunction (handoff D2); the old RVA hooks are one-statement adapters (cTSWantSimulatorAdapters).
#include "gz/cIGZMessageTarget2.h"
#include "ts/cITSDataContainer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

// An intrusive reference held in a tree node: released through slot 2 when the node is destroyed
// (0x483be0 / 0x483c80 release the node's pointers this way). Never copied by our code.
struct WantRef {
    void *object;
    WantRef() : object(nullptr) {}
    explicit WantRef(void *p) : object(p) {}
    WantRef(const WantRef &) = delete;
    WantRef &operator=(const WantRef &) = delete;
    ~WantRef();
    bool operator<(const WantRef &other) const { return object < other.object; }
};

// A vector of counted pointers whose block comes from the allocator service (begin, end, capacity
// limit). Destroyed as 0x1e6a40 does: each non-null element released (slot 2), the block freed
// through FreeThroughAllocatorService (0x197100) with its capacity, the pointers zeroed.
struct WantRefList {
    void **mFirst = nullptr;
    void **mLast = nullptr;
    void **mEnd = nullptr;
    WantRefList() = default;
    WantRefList(const WantRefList &) = delete;
    WantRefList &operator=(const WantRefList &) = delete;
    ~WantRefList();
    uint32_t Size() const { return uint32_t(mLast - mFirst); }
};

// Value of the 0x7c map: three dwords (node size 0x20); vf28 / vf29 read the first two.
struct WantInstancePair {
    uint32_t first;
    uint32_t second;
    uint32_t third;
};

// A resource key returned by value (type, group, instance, resource).
// The per-sim "memories" record uses {kMemoriesRecordType, hood group, sim id, 0} (lane CY).
struct WantResourceKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};

class cTSWantInstance; // 0x3c bytes, pooled (0x54e570); its own class (cITSWantInstance + cIGZSerializable at +4)
namespace ifaces {
class cITSWantInstance; // the instance's primary vtable (generated from db/methods)
class cITSNeighbor;
class cIGZIStream;
class cIGZOStream;
} // namespace ifaces

// A want's parameter (cITSWantInstance slot 14 returns &instance+0x14): kind 1 is a sim id (value), kind 3 is
// level-checked against the simulator's +0x84 map (IsWantAllowed), kind 6 a guid.
struct WantInstanceParam {
    uint32_t kind;  // 0x00
    uint32_t value; // 0x04
    int16_t extra;  // 0x08
    uint16_t pad0a;
};
static_assert(sizeof(WantInstanceParam) == 0xc, "WantInstanceParam size");

// One memory in a WantMemoryStore list: a 0xc-byte node from the allocator service (0x3b1d10), holding a counted
// instance. The head node of a list is allocated the same way and has no instance.
struct MemoryNode {
    MemoryNode *next;                  // 0x0
    MemoryNode *prev;                  // 0x4
    ifaces::cITSWantInstance *memory;  // 0x8
};
static_assert(sizeof(MemoryNode) == 0xc, "MemoryNode size");

// A std::list<counted cTSWantInstance*>, the value of the store's map (map node 0x1c: key 0x10, list head 0x14, size 0x18).
// Destroyed as the original does: the ring is cut open, every instance released and every node freed through the
// allocator service, then the head.
struct MemoryList {
    MemoryNode *head;
    uint32_t size;
    MemoryList();
    MemoryList(const MemoryList &) = delete;
    MemoryList &operator=(const MemoryList &) = delete;
    ~MemoryList();
    // 0x551930 (also inlined in WantMemoryStore::Read): a node before the head holding `memory`, counted once.
    void Append(ifaces::cITSWantInstance *memory);
};
static_assert(sizeof(MemoryList) == 8, "MemoryList size");

// Record and stream constants of the save path (lane CY, notes/feedback/CY.md "Record format").
constexpr uint32_t kMemoriesRecordType = 0xcd95548e;  // key type of the per-sim record
constexpr uint32_t kSegmentIid = 0x656b8f0c;          // cITSPersistSystem::FindSegment iid (cIGZPersistDBSegment)
constexpr uint32_t kIStreamIid = 0x051b11bd;          // record QI on load
constexpr uint32_t kOStreamIid = 0xc51b11cb;          // record QI on save
constexpr uint32_t kSerializableIid = 0xe4fda3d4;     // cIGZSerializable, on each cTSWantInstance
constexpr uint32_t kWantsVersion = 6;                 // WriteSimWants; ReadSimWants refuses > 6
constexpr uint32_t kMemoryStoreVersion = 2;           // WantMemoryStore::Write; Read refuses > 2
constexpr uint32_t kDefaultMemoryGuid = 0xad9acd9a;   // AddDefaultMemory adds one when a store has none

// Per-sim memories (lane CY): a non-polymorphic 0x10-byte object, operator new(0x10), one per sim id in
// cTSWantSimulator::mMemoryStores. Its functions sit at 0x5509c0..0x551930 next to cTSWantInstance's.
struct WantMemoryStore {
    uint32_t lastTick;   // 0x00 = 0x3fffffff (ctor); SyncTick sets it to the simulator's GetTick (+0x8c)
    int32_t nextId;      // 0x04 highest memory id; AddMemory numbers new memories (+= max(1, tick - lastTick))
    std::map<uint32_t, MemoryList> byGuid; // 0x08 guid of the want's definition -> its memories (empty lists stay)

    WantMemoryStore *Construct();                      // 0x5509c0, 74 B, ret 0
    void Destruct();                                   // 0x550b70, 32 B: erase 0x550970, delete head (0x1c)
    void Clear();                                      // 0x5516d0, 37 B: erase all, size 0
    void SyncTick();                                   // 0x551360, 26 B: lastTick = globals slot 4 -> slot 26 GetTick
    void AddMemory(ifaces::cITSWantInstance *want);    // 0x550cb0, 205 B, ret 4, EH
    bool Read(ifaces::cIGZIStream *stream);            // 0x551380, 789 B, ret 4, EH
    bool Write(ifaces::cIGZOStream *stream);           // 0x551700, 551 B, ret 4, EH
    // Lane GG (src/ts/cTSWantSimulatorGG.cpp):
    int32_t AdvanceToTick();                           // 0x551320, 45 B: nextId += max(0, tick - lastTick), lastTick = tick
    void CollectMemories(MemoryList *out, bool all);   // 0x550d80, 620 B, ret 8, EH: counted copies appended to `out`
    void RestartIds(int32_t id);                       // 0x5516a0, 35 B, ret 4: nextId = id, lastTick = the tick
    void AddToIds(int32_t amount);                     // 0x551350, 10 B, ret 4: nextId += amount
};

// 0x499000 (lane GF, cdecl): `out` built as the decimal text of `value` (FormatDecimal, 4 digits). Returns out.
class cRZString;
cRZString *__cdecl WantFloatText(cRZString *out, float value);
static_assert(sizeof(WantMemoryStore) == 0x10, "WantMemoryStore size");

// Built on the stack by 0x4854b0 (ctor, ret 4) for a sim id; the destructor is inlined (release +8, then +4).
struct WantSimContext {
    int16_t simId;               // 0x00
    uint16_t pad02;
    void *person;                // 0x04 counted: the live object (GscObjectTable FindObjectByHandle of the neighbour's
                                 //      person data id), when the sim has a neighbour and is instantiated
    ifaces::cITSNeighbor *neighbor; // 0x08 counted: cITSNeighborManager GetNeighbor(simId, 1)
    uint32_t attributes;         // 0x0c the person's slot 12 when there is a person, else the neighbour's
                                 //      GetAttributeBlock(1), else 0

    WantSimContext *Construct(int16_t simId); // 0x4854b0, 287 B, EH
    void Destruct();                          // inlined by every user: release the neighbour, then the person
    WantSimContext *Assign(const WantSimContext &other); // 0x485e40 ret 4 (lane GF): copy, the two pointers counted
};
static_assert(sizeof(WantSimContext) == 0x10, "WantSimContext size");

// cITSWantSimulator (vftable 0xdcad10, 64 slots): the want simulator's primary face. Slots 3..61 in slot order, the
// virtual destructor at slot 62 (the scalar deleting destructor 0x4862e0), slot 63 after it. Slots whose body is still
// the game's take and return raw dwords (int16 / uint8 arguments passed through whole, the result passed through), so
// the forwarders change nothing the original sees; their names and shapes are tentative.
class cITSWantSimulator : public cIGZUnknown {
public:
    virtual uint32_t Init() = 0;                                                  // slot 3, 0x4905e0 (lane IV)
    virtual uint32_t Shutdown() = 0;                                              // slot 4, 0x496720 (lane IV)
    virtual uint32_t AddCategoryWant(uint32_t category, uint32_t guid) = 0;      // slot 5, 0x486430 (lane IV)
    virtual uint8_t StoreSimWants(int16_t simId) = 0;                             // slot 6
    virtual uint8_t RestoreSimWants(uint32_t simId) = 0;                          // slot 7 (callers pass a whole dword: Counts)
    virtual uint8_t SaveAllSims(uint8_t copyLivePersons) = 0;                     // slot 8
    virtual uint32_t LevelForObject(void *person) = 0;                            // slot 9
    virtual uint32_t LevelForKey(void *person) = 0;                               // slot 10
    virtual uint32_t ScoreMotiveA2(void *person, uint8_t apply) = 0;              // slot 11
    virtual uint32_t ScoreAttributeA2(void *person, uint8_t apply) = 0;           // slot 12
    virtual int32_t ScaledAttributeB2(void *person, uint8_t scale) = 0;           // slot 13
    virtual int32_t ClampedAttributeB0(void *person) = 0;                         // slot 14
    virtual int32_t PositiveAttributeA1(void *person) = 0;                        // slot 15
    virtual uint8_t CountsForPerson(void *person, int32_t *outA, int32_t *outB) = 0; // slot 16
    virtual uint8_t Counts(uint32_t type, int32_t *outA, int32_t *outB) = 0;      // slot 17
    virtual uint8_t ValueForPerson(void *person, int32_t *out) = 0;               // slot 18
    virtual uint8_t Value48(int16_t key, int32_t *out) = 0;                       // slot 19
    virtual void *Value40(int16_t key) = 0;                                       // slot 20
    virtual WantRefList *List30(int16_t key) = 0;                                 // slot 21
    virtual WantRefList *List38(int16_t key) = 0;                                 // slot 22
    virtual uint32_t GetWantDefinition(uint32_t guid) = 0;                        // slot 23
    virtual uint32_t DefinitionsSlot7() = 0;                                      // slot 24
    virtual uint32_t GetCategoryWants(uint32_t category) = 0;                     // slot 25, 0x48d840 (game)
    virtual uint32_t GetTick() = 0;                                               // slot 26, folded 0x22fa70
    virtual uint32_t GetWantText(uint32_t key, uint32_t index, uint32_t out) = 0; // slot 27, 0x48dad0 (game)
    virtual WantResourceKey *KeyFirst(WantResourceKey *out, int16_t key) = 0;     // slot 28
    virtual WantResourceKey *KeySecond(WantResourceKey *out, int16_t key) = 0;    // slot 29
    virtual uint8_t Find84(int16_t key, int32_t *out) = 0;                        // slot 30
    virtual uint32_t AdjustPersonA1(uint32_t person, uint32_t amount) = 0;        // slot 31, 0x496930 (game)
    virtual uint32_t ForwardToSlot34(uint32_t a, void *source) = 0;               // slot 32
    virtual uint32_t ForwardPersonToSlot34(void *person, void *source) = 0;       // slot 33
    virtual uint32_t ForEachWantOf(uint32_t a, uint32_t guid, uint32_t c, uint32_t d) = 0; // slot 34, 0x491960 (game)
    virtual void QueueSimUpdate(int16_t simId, int32_t priority) = 0;             // slot 35
    virtual uint32_t Slot36(uint32_t simId) = 0;                                  // slot 36, 0x494540 (game)
    virtual uint32_t Slot37(uint32_t simId, uint32_t a, uint32_t b) = 0;          // slot 37, 0x4944c0 (game)
    virtual void Update() = 0;                                                    // slot 38, 0x491e00 (lane GG)
    virtual uint32_t Slot39(uint32_t person, uint32_t value, uint32_t flag) = 0;  // slot 39, 0x495d00 (game)
    virtual uint32_t Slot40(void *person, int32_t value, uint32_t flag) = 0;      // slot 40, 0x495e30 (game)
    virtual uint32_t MidpointScoreForPerson(void *person, int32_t level) = 0;     // slot 41
    virtual int32_t ScoreForValue(int32_t key, int32_t value) = 0;                // slot 42
    virtual uint32_t Slot43(uint32_t a, uint32_t value) = 0;                      // slot 43, 0x492840 (game; value a float)
    virtual uint32_t Slot44(uint32_t person) = 0;                                 // slot 44, 0x48ec20 (game)
    virtual void ResetMemories(int16_t simId) = 0;                                // slot 45
    virtual uint32_t Slot46(uint32_t simId) = 0;                                  // slot 46, 0x497480 (game)
    virtual void SetMemoryIds(uint32_t simId, int32_t nextId) = 0;                // slot 47, 0x496660 (lane GG)
    virtual void AddMemoryIds(uint32_t simId, int32_t amount) = 0;                // slot 48, 0x490520 (lane GG)
    virtual uint32_t ForwardPersonToSlot50(void *person, uint32_t a, uint32_t b) = 0; // slot 49
    virtual uint32_t Slot50(uint32_t simId, uint32_t a, uint32_t b) = 0;          // slot 50, 0x496010 (game)
    virtual uint32_t Slot51(uint32_t simId, uint32_t a) = 0;                      // slot 51, 0x4962c0 (game)
    virtual void DumpScoredWants(int16_t simId, uint8_t withMemories, uint8_t plainName, uint8_t keepOptions) = 0; // slot 52, 0x489210 (lane GF)
    virtual void WriteDebugXml() = 0;                                             // slot 53, 0x488820 (lane GG)
    virtual uint32_t Slot54(uint32_t a) = 0;                                      // slot 54, 0x497590 (game)
    virtual void FlushUpdates() = 0;                                              // slot 55
    virtual void TogglePaused() = 0;                                              // slot 56
    virtual uint8_t IsPaused() = 0;                                               // slot 57
    virtual uint32_t Slot58() = 0;                                                // slot 58, 0x4908e0 (game)
    virtual uint32_t Slot59() = 0;                                                // slot 59, 0x494a70 (game)
    virtual uint32_t Slot60(uint32_t a) = 0;                                      // slot 60, 0x490300 (game)
    virtual uint32_t Slot61(uint32_t a) = 0;                                      // slot 61, 0x48ca60 (game)
    virtual ~cITSWantSimulator() = default;                                       // slot 62
    virtual int32_t ScaledMotiveB2(void *person, uint8_t scale) = 0;              // slot 63
};

// The face offsets (base offsets are no constant expressions: the tests check them).
namespace wantsim {
constexpr uint32_t kMessageFace = 4;
constexpr uint32_t kContainerFace = 8;
} // namespace wantsim

class cTSWantSimulator : public cITSWantSimulator, public cIGZMessageTarget2, public cITSDataContainer {
public:
    static constexpr uint32_t kVTable = 0xdcad10;
    static constexpr uint32_t kMessageVTable = 0xdcae14;
    static constexpr uint32_t kContainerVTable = 0xdcae28;

    // Lane CY: meanings from the save/load path (notes/feedback/CY.md); lane DB renamed the fields to the
    // names the Ghidra struct cTSWantSimulator uses.
    int32_t mRefCount;                                      // 0x0c
    uint8_t mInitialized;                                        // 0x10 (vf3 Init sets, vf4 Shutdown clears)
    std::set<uint32_t> mMessageIds;                         // 0x14 message ids registered for
    WantRef mRef1c;                                        // 0x1c released in the destructor
    WantRef mRef20;                                        // 0x20 slot 8 scales a motive (vf13, vf63)
    WantRef mDefinitions;                                        // 0x24: want definition manager (new 0x44, ctor
                                                            //    0x551c40, by Init); slot 23 GetWantDefinition = its slot 5
    std::map<int16_t, WantMemoryStore *> mMemoryStores;                    // 0x28: sim id -> WantMemoryStore* (value is
                                                            //    the pointer; vf17 loads the sim when absent)
    std::map<int16_t, WantRefList> mWants;         // 0x30: sim id -> want slots (padded to 4 with null)
    std::map<int16_t, WantRefList> mFears;         // 0x38: sim id -> fear slots (padded to 3 with null)
    std::map<int16_t, WantRef> mLifetimeWants;                      // 0x40: sim id -> cTSWantInstance (vf20)
    std::map<int16_t, int16_t> mMap48;                      // 0x48 sim id -> short saved last in the record (vf19 adds 1)
    std::map<int16_t, WantRefList> mMap50;         // 0x50 copy of mWants[sim] taken when the sim is queued with priority 0/1
    std::map<int16_t, WantRefList> mMap58;         // 0x58 copy of mFears[sim], likewise (QueueSimUpdate)
    std::map<int32_t, WantRefList> mCategoryWants;         // 0x60: category flag (1..0x100) -> its wants (vf5
                                                            //    AddCategoryWant, vf25; int32 keys, 0x4864ca)
    WantRefList mVec68;                           // 0x68
    std::map<int32_t, std::vector<int32_t>> mLevelTables;   // 0x74 thresholds per key (vf9, vf10)
    std::map<int16_t, WantInstancePair> mMap7c;             // 0x7c (vf28, vf29; vf27 GetWantText reads node+0x1c)
    std::map<int32_t, int32_t> mMap84;                      // 0x84 (vf30; IsWantAllowed checks kind-3 params against it)
    uint32_t mTick;                                           // 0x8c: slot 26 GetTick; vf38 adds 2; memory ids
    WantRef mRef90;                                        // 0x90 released in the destructor
    WantRef mRef94;                                        // 0x94 released in the destructor
    std::map<WantRef, WantRef> mRefMap98;                   // 0x98 both pointers released
    std::map<int16_t, WantRefList> mMapA0;         // 0xa0
    std::map<WantRef, uint32_t> mRefMapA8;                  // 0xa8 key released
    int32_t mOptionMask;                                            // 0xb0: = 1, Init sets GetOptionMask(); wants
                                                            //    whose definition flags (def slot 19) miss it are dropped
    uint32_t mPendingPriority;                                           // 0xb4: value taken from the queue node
    uint32_t mUpdateState;                                           // 0xb8: RunUpdate's state (0 idle)
    uint16_t mCurrentSim;                                           // 0xbc (int16): sim RunUpdate works on
    uint16_t mBE;                                           // 0xbe not set by the constructor
    WantRef mRefC0;                                        // 0xc0 released in the destructor
    WantRef mRefC4;                                        // 0xc4 released in the destructor
    uint32_t mC8;                                           // 0xc8
    WantMemoryStore *mCurrentStore;                                           // 0xcc: WantMemoryStore* of mCurrentSim (cleared
                                                            //    when that store is deleted)
    uint32_t mD0;                                           // 0xd0 current store's 0x551320 value
    WantRefList mVecD4;                           // 0xd4
    WantRefList mVecE0;                           // 0xe0
    WantRefList mVecEC;                           // 0xec cleared by RestoreSimWants when the sim is mCurrentSim
    uint32_t mF8;                                           // 0xf8
    std::map<int16_t, int32_t> mPendingSims;                      // 0xfc: the real key is int16 sim id ->
                                                            //    priority (vf35 QueueSimUpdate); vf55 drains it
    int32_t m104;                                           // 0x104 = 1 (vf46 toggles it)
    uint8_t mPaused;                                        // 0x108 toggled by vf56, read by vf57
    uint8_t mPad109[3];
    uint32_t m10C;                                          // 0x10c
    uint16_t m110;                                          // 0x110 byte 0x111 is vf38's re-entry guard (mUpdating)
    uint16_t mPad112;

    // 0x4855d0 (the constructor hook's adapter: placement new; the class's switch): the containers empty, +0x10 cleared,
    // +0xb0 = 1, +0x104 = 1, the six message ids in the set; +0xb4, +0xbe and +0xcc left as they were.
    cTSWantSimulator();
    // 0x485ba0 (the deleting destructor 0x4862e0 is slot 62): members in reverse order, the counted pointers released
    // in between (WantRef members); out of line (lane FI's rule).
    ~cTSWantSimulator() override;

    // ---- cIGZUnknown (all three faces) ----
    bool QueryInterface(uint32_t id, void **out) override; // 0x4927b0 slot 0; thunks 0x4927f9 (+4), 0x492801 (+8)
    uint32_t AddRef() override { return uint32_t(++mRefCount); } // slot 1, folded 0x15e520
    uint32_t Release() override;                            // 0x494800 slot 2; thunks 0x49481a (+4), 0x494822 (+8)

    // ---- cITSWantSimulator, ours ----
    uint8_t StoreSimWants(int16_t simId) override;          // 0x4985e0 slot 6
    uint8_t RestoreSimWants(uint32_t simId) override;       // 0x493510 slot 7 (the sim id is the low half)
    uint8_t SaveAllSims(uint8_t copyLivePersons) override;  // 0x4952b0 slot 8
    uint32_t LevelForObject(void *person) override;         // 0x48d280 slot 9
    uint32_t LevelForKey(void *person) override;            // 0x48d330 slot 10
    uint32_t ScoreMotiveA2(void *person, uint8_t apply) override;    // 0x48d3c0 slot 11
    uint32_t ScoreAttributeA2(void *person, uint8_t apply) override; // 0x48d430 slot 12
    int32_t ScaledAttributeB2(void *person, uint8_t scale) override; // 0x48d7c0 slot 13
    int32_t ClampedAttributeB0(void *person) override;      // 0x48d710 slot 14
    int32_t PositiveAttributeA1(void *person) override;     // 0x48de30 slot 15
    uint8_t CountsForPerson(void *person, int32_t *outA, int32_t *outB) override; // 0x48d9d0 slot 16
    uint8_t Counts(uint32_t type, int32_t *outA, int32_t *outB) override;         // 0x48d940 slot 17
    uint8_t ValueForPerson(void *person, int32_t *out) override; // 0x48da40 slot 18
    uint8_t Value48(int16_t key, int32_t *out) override;    // 0x48da10 slot 19
    void *Value40(int16_t key) override;                    // 0x48d920 slot 20
    WantRefList *List30(int16_t key) override;              // 0x48dcb0 slot 21
    WantRefList *List38(int16_t key) override;              // 0x48dca0 slot 22
    uint32_t GetWantDefinition(uint32_t guid) override;     // 0x48dfc0 slot 23
    uint32_t DefinitionsSlot7() override;                   // 0x48dfd0 slot 24
    uint32_t GetTick() override { return mTick; }           // slot 26, folded 0x22fa70
    WantResourceKey *KeyFirst(WantResourceKey *out, int16_t key) override;  // 0x48dbe0 slot 28
    WantResourceKey *KeySecond(WantResourceKey *out, int16_t key) override; // 0x48dc40 slot 29
    uint8_t Find84(int16_t key, int32_t *out) override;     // 0x48da70 slot 30
    uint32_t ForwardToSlot34(uint32_t a, void *source) override;         // 0x4918f0 slot 32
    uint32_t ForwardPersonToSlot34(void *person, void *source) override; // 0x491920 slot 33
    void QueueSimUpdate(int16_t simId, int32_t priority) override;       // 0x494870 slot 35
    uint32_t MidpointScoreForPerson(void *person, int32_t level) override; // 0x495fa0 slot 41
    int32_t ScoreForValue(int32_t key, int32_t value) override;          // 0x491aa0 slot 42
    void ResetMemories(int16_t simId) override;             // 0x487a70 slot 45
    uint32_t ForwardPersonToSlot50(void *person, uint32_t a, uint32_t b) override; // 0x496290 slot 49
    void FlushUpdates() override;                           // 0x48d1e0 slot 55
    void TogglePaused() override;                           // 0x497460 slot 56
    uint8_t IsPaused() override;                            // 0x496920 slot 57
    int32_t ScaledMotiveB2(void *person, uint8_t scale) override; // 0x48d740 slot 63

    // ---- lane IV (src/ts/cTSWantSimulatorIV.cpp) ----
    // 0x4905e0 slot 3: once; the option mask, a new want tree manager (Init), the tuning, the cheats, the highest level per
    // aspiration key of the object definitions (+0x84), the message ids registered. Returns the set's head pointer (what the
    // original leaves in eax); an Init that finds the simulator initialised returns 1 (the original: an unread stack word).
    uint32_t Init() override;
    // 0x496720 slot 4: Init undone (see the .cpp). Returns +0x20's Release result (0 when +0x20 was null or not
    // initialised: the original's eax is left over there).
    uint32_t Shutdown() override;
    // 0x486430 slot 5: a want of the guid appended to the category's list. Returns 0 (the original's eax is left over).
    uint32_t AddCategoryWant(uint32_t category, uint32_t guid) override;

    // ---- cITSWantSimulator, still the game's bodies (forwarders, src/ts/cTSWantSimulator.cpp "Game slots") ----
    uint32_t GetCategoryWants(uint32_t category) override;
    uint32_t GetWantText(uint32_t key, uint32_t index, uint32_t out) override;
    uint32_t AdjustPersonA1(uint32_t person, uint32_t amount) override;
    uint32_t ForEachWantOf(uint32_t a, uint32_t guid, uint32_t c, uint32_t d) override;
    uint32_t Slot36(uint32_t simId) override;
    uint32_t Slot37(uint32_t simId, uint32_t a, uint32_t b) override;
    uint32_t Slot39(uint32_t person, uint32_t value, uint32_t flag) override;
    uint32_t Slot40(void *person, int32_t value, uint32_t flag) override;
    uint32_t Slot43(uint32_t a, uint32_t value) override;
    uint32_t Slot44(uint32_t person) override;
    uint32_t Slot46(uint32_t simId) override;
    uint32_t Slot50(uint32_t simId, uint32_t a, uint32_t b) override;
    uint32_t Slot51(uint32_t simId, uint32_t a) override;
    uint32_t Slot54(uint32_t a) override;
    uint32_t Slot58() override;
    uint32_t Slot59() override;
    uint32_t Slot60(uint32_t a) override;
    uint32_t Slot61(uint32_t a) override;

    // ---- cIGZMessageTarget2 (+4) ----
    bool DoMessage(void *message) override;                 // 0x487dd0 (lane GG)

    // ---- cITSDataContainer (+8) ----
    bool CollectReferenced(ifaces::cITSDataImportExportContext *) override { return false; } // slot 3, folded 0x18a7a0
    bool Export(ifaces::cITSDataImportExportContext *) override { return false; }            // slot 4, folded 0x18a7a0
    bool Import(ifaces::cITSDataImportExportContext *context) override; // slot 5, 0x492fd0 (lane GG)

    // ---- lane GG (src/ts/cTSWantSimulatorGG.cpp; slot 53 in cTSWantSimulatorGGDebug.cpp) ----
    void Update() override;                                  // 0x491e00 slot 38
    void SetMemoryIds(uint32_t simId, int32_t nextId) override; // 0x496660 slot 47
    void AddMemoryIds(uint32_t simId, int32_t amount) override; // 0x490520 slot 48
    void WriteDebugXml() override;                           // 0x488820 slot 53
    void *FindWantObject();                                 // 0x48df40: +0x94 looked up when empty
    void RestartTick(bool advanceFirst);                    // 0x494a00 ret 4: stores caught up, the tick back to 0

    // ---- non-virtual members (hooked through adapters) ----
    bool WriteSimWants(int16_t simId, ifaces::cIGZOStream *stream); // 0x4983a0 ret 8, EH
    bool ReadSimWants(int16_t simId, ifaces::cIGZIStream *stream); // 0x492970 ret 8, EH
    bool ReadSimRecord(int16_t simId, WantResourceKey *key); // 0x492ea0 ret 8, EH
    WantMemoryStore *GetMemoryStore(int16_t simId);         // 0x48d650 ret 4, EH
    void AddDefaultMemory(int16_t simId);                   // 0x486310 ret 4, EH
    void DeleteSim(int16_t simId);                          // 0x487be0 ret 4, EH (messages 0x9d/0x9e)
    bool IsWantAllowed(ifaces::cITSWantInstance *want);     // 0x48e2f0 ret 4
    void RunUpdate(int32_t steps, int16_t simId);           // 0x4880b0 ret 8, EH, switch on mUpdateState (1870 B; game)

    // ---- lane GF: the scored-wants dump (slot 52) and its helpers, src/ts/cTSWantSimulatorDump.cpp ----
    // 0x489210 (slot 52; nothing in the image calls it but the vtable, so no adapter): runs the sim's update to the end and
    // writes its scored wants and fears as a CSV (or .dwh) table, then opens the file with the shell.
    void DumpScoredWants(int16_t simId, uint8_t withMemories, uint8_t plainName, uint8_t keepOptions) override;
    // 0x48ce50 ret 0xc: the index of the first non-null entry of `list` whose slot 3 matches `want` (*out that entry), or -1
    // (*out null). Does not use `this`.
    int32_t IndexOfMatch(void *want, WantRefList *list, void **out);

private:
    int32_t ScaleToRange(int32_t raw, uint8_t scale);
    void FinishPendingUpdate(int16_t simId);                                      // inlined wait loop (RunUpdate)
    void DestroyMemoryStore(std::map<int16_t, WantMemoryStore *>::iterator entry); // inlined in DeleteSim, ResetMemories
};

static_assert(sizeof(cTSWantSimulator) == 0x114, "cTSWantSimulator size");
static_assert(offsetof(cTSWantSimulator, mMemoryStores) == 0x28, "mMemoryStores");
static_assert(offsetof(cTSWantSimulator, mLevelTables) == 0x74, "mLevelTables");
static_assert(offsetof(cTSWantSimulator, mRefMap98) == 0x98, "mRefMap98");
static_assert(offsetof(cTSWantSimulator, mOptionMask) == 0xb0, "mOptionMask");
static_assert(offsetof(cTSWantSimulator, mVecD4) == 0xd4, "mVecD4");
static_assert(offsetof(cTSWantSimulator, mPendingSims) == 0xfc, "mPendingSims");
static_assert(offsetof(cTSWantSimulator, mPaused) == 0x108, "mPaused");
// Lane CY: the fields the save/load path touches.
static_assert(offsetof(cTSWantSimulator, mDefinitions) == 0x24, "mDefinitions (definitions)");
static_assert(offsetof(cTSWantSimulator, mWants) == 0x30, "mWants (wants)");
static_assert(offsetof(cTSWantSimulator, mFears) == 0x38, "mFears (fears)");
static_assert(offsetof(cTSWantSimulator, mLifetimeWants) == 0x40, "mLifetimeWants (lifetime wants)");
static_assert(offsetof(cTSWantSimulator, mMap48) == 0x48, "mMap48");
static_assert(offsetof(cTSWantSimulator, mCategoryWants) == 0x60, "mCategoryWants (category wants)");
static_assert(offsetof(cTSWantSimulator, mTick) == 0x8c, "mTick (tick)");
static_assert(offsetof(cTSWantSimulator, mPendingPriority) == 0xb4, "mPendingPriority (pending priority)");
static_assert(offsetof(cTSWantSimulator, mUpdateState) == 0xb8, "mUpdateState (update state)");
static_assert(offsetof(cTSWantSimulator, mCurrentSim) == 0xbc, "mCurrentSim (current sim)");
static_assert(offsetof(cTSWantSimulator, mCurrentStore) == 0xcc, "mCurrentStore (current store)");
static_assert(offsetof(cTSWantSimulator, mVecEC) == 0xec, "mVecEC");
static_assert(offsetof(cTSWantSimulator, m110) == 0x110, "m110");
static_assert(offsetof(WantMemoryStore, byGuid) == 0x08, "WantMemoryStore map");
static_assert(offsetof(cTSWantSimulator, mRefCount) == 0x0c, "mRefCount");

// The old entry points (RVA hooks): one-statement forwards to our members, `this` the face of the slot (__fastcall with
// the dummy edx = __thiscall). They serve direct calls and objects that still carry the game's vtables.
struct cTSWantSimulatorAdapters {
    using Self = cTSWantSimulator;
    static Self *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(cTSWantSimulator *self, void *);
    static uint32_t __fastcall Release(cTSWantSimulator *self, void *);
    static int32_t __fastcall PositiveAttributeA1(cTSWantSimulator *self, void *, void *person);
    static uint32_t __fastcall GetWantDefinition(cTSWantSimulator *self, void *, uint32_t guid);
    static uint32_t __fastcall DefinitionsSlot7(cTSWantSimulator *self, void *);
    static void __fastcall TogglePaused(cTSWantSimulator *self, void *);
    static uint8_t __fastcall IsPaused(cTSWantSimulator *self, void *);
    static WantMemoryStore *__fastcall GetMemoryStore(cTSWantSimulator *self, void *, int16_t simId);
    static void __fastcall AddDefaultMemory(cTSWantSimulator *self, void *, int16_t simId);
    static bool __fastcall IsWantAllowed(cTSWantSimulator *self, void *, ifaces::cITSWantInstance *want);
    static bool __fastcall WriteSimWants(cTSWantSimulator *self, void *, int16_t simId, ifaces::cIGZOStream *stream);
    static bool __fastcall ReadSimWants(cTSWantSimulator *self, void *, int16_t simId, ifaces::cIGZIStream *stream);
    static bool __fastcall ReadSimRecord(cTSWantSimulator *self, void *, int16_t simId, WantResourceKey *key);
    static void __fastcall DeleteSim(cTSWantSimulator *self, void *, int16_t simId);
    static void __fastcall FlushUpdates(cTSWantSimulator *self, void *);
    // Lane GG (src/ts/cTSWantSimulatorGG.cpp):
    static void *__fastcall FindWantObject(cTSWantSimulator *self, void *);
};
