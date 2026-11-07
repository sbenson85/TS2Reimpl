#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"
#include "ts/cITSDataContainer.h"
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: the counted object reference the member maps hold

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// cTSNeighborhood: the neighbourhood object (RTTI cTSNeighborhood, bases cITSNeighborhood / cIGZSerializable at +0, the
// cIGZPersistResource face at +4, cITSDataContainer at +8 and cIGZMessageTarget2 at +0xc). 0xcc bytes; RVA 0x471d90 builds
// it and 0x473b80 allocates and builds. Vtables: 0xdc9be0 (77 slots), 0xdc9d18, 0xdc9d4c, 0xdc9d68.
//
// It holds: a 32-byte code of 16 words, the neighbourhood's name text, a map of the lot objects (the "lots", keyed by
// their number), a set of text entries, a state helper object (created through the framework by class id 0xaba73af),
// and three maps of the same kind of helper keyed by a signed 16-bit id (kind 1, 2 and 3). Unless a delegate object is
// set (+0xbc), the begin/end slots hand out iterators of those maps; with a delegate they forward to it.

// Map of lot objects: number -> counted object (node 0x18 bytes, erased through the game's releasing tree eraser).
using NeighborhoodLotMap = std::map<int32_t, NeighborDataRef>; // signed keys (the game's lookups compare signed)
static_assert(sizeof(NeighborhoodLotMap) == 8);
// The helper maps: signed 16-bit id -> counted object.
using NeighborhoodMemberMap = std::map<int16_t, NeighborDataRef>;
static_assert(sizeof(NeighborhoodMemberMap) == 8);
// An id map without releasing values (node 0x18 bytes).
using NeighborhoodPlainMap = std::map<uint32_t, uint32_t>;
static_assert(sizeof(NeighborhoodPlainMap) == 8);
using NeighborhoodIdSet = std::set<uint32_t>;
static_assert(sizeof(NeighborhoodIdSet) == 8);

// The lot-name index (+0x64): in the game a std::map<cRZString, counted cITSLotInfo> ordered case-insensitively (its
// lower bound 0x471480 compares the two strings' character buffers with the CRT's _stricmp, 0x845799). The value pair is
// 0x24 bytes (node 0x34); 0x1d9ab0 destroys one (the lot reference first, then the name). Written as a set of entries
// because our cRZString has no C++ destructor: the entry's destructor does both. Instantiations the game has for it, not
// library-matched (ours replace them): insert 0x4712c0, erase(key) 0x47b5d0, lower_bound 0x471480.
// AddLot (slot 61), LoadLots (slot 18) and RenameLot (slot 63) insert; RemoveLot (62) and RenameLot erase; FindLotByName
// (slot 56) looks up. mHeld is the lot's cITSLotInfo face (+8 of a cTSLotInfo), counted.
struct NeighborhoodTextEntry {
    cRZString mText;    // +0x00 the lot's name (cITSBaseLotInfo::GetName)
    void *mHeld;        // +0x20 the lot (cITSLotInfo), released by the destructor (slot 2) before the text goes
    NeighborhoodTextEntry() : mText{nullptr, std::string(), 0}, mHeld(nullptr) {} // empty (the tests fill one in)
    // A new entry: a copy of the name, and the lot counted (slot 1).
    NeighborhoodTextEntry(const cIGZString &name, void *held);
    // The copy the tree makes of an inserted entry counts the lot again, as the game's pair copy does.
    NeighborhoodTextEntry(const NeighborhoodTextEntry &other);
    NeighborhoodTextEntry &operator=(const NeighborhoodTextEntry &) = delete;
    ~NeighborhoodTextEntry() noexcept(false);
};
static_assert(sizeof(NeighborhoodTextEntry) == 0x24);
// _stricmp: the CRT's (<string.h>; 0x845799 in the game)
struct NeighborhoodTextEntryLess {
    using is_transparent = void; // lookups by the name alone (the game's key type is the cRZString)
    static const char *Text(const NeighborhoodTextEntry &entry) noexcept { return entry.mText.Data(); }
    static const char *Text(const cRZString &text) noexcept { return text.Data(); }
    template <class A, class B> bool operator()(const A &a, const B &b) const noexcept
    {
        return _stricmp(Text(a), Text(b)) < 0;
    }
};
using NeighborhoodTextSet = std::set<NeighborhoodTextEntry, NeighborhoodTextEntryLess>;
static_assert(sizeof(NeighborhoodTextSet) == 8);

namespace ifaces {
class cITSInventory;
class cITSLotInfo;
class cITSPerson;
}

// 0x476e80 (cdecl; a free function of the neighbourhood's translation unit): flags (4) every token of the inventory with the guid
// whose property `propertyIndex` equals the lot (low 16 bits); answers whether one was flagged. RemoveLotReferences uses it.
uint8_t __cdecl FlagLotTokens(ifaces::cITSInventory *inventory, uint32_t guid, int32_t lot, int32_t propertyIndex);

// The static table of person data fields a sim's person object and its neighbour record share (the game builds it as a
// std::vector<uint32_t> in a global, 0x10dab28, the first time it is asked): each entry's low 16 bits are the field and
// attribute index, the high 16 bits a category number. Three pointers like the vector's.
struct NeighborSyncTable {
    const uint32_t *mFirst;
    const uint32_t *mLast;
    const uint32_t *mEnd;
};
// 0x4b8ff0 (cdecl): the table.
const NeighborSyncTable *__cdecl GetNeighborSyncTable();
// 0x4b8fd0 (cdecl; slot 31 of the neighbourhood jumps here): the number of entries.
uint32_t __cdecl GetNeighborSyncCount();
// 0x4b8f80 (cdecl): the entry at the index split into its low and high halves; false (both halves 0) for index <= 0 or past the end.
uint8_t __cdecl GetNeighborSyncEntry(int32_t index, uint16_t *field, uint16_t *extra);

// ---- the interfaces this class implements (the original's names and slot order; global, as the original's RTTI) ----
// cIGZSerializable, cIGZPersistResource, cIGZMessageTarget2 (src/gz/) and cITSDataContainer (src/ts/) are shared;
// cITSNeighborhood has no other implementer (declared here). Callers of other objects keep using the generated ifaces::
// views (same ABI). Argument types are those the bodies use (`void *` where the object is reached through an ifaces:: view).

class cIGZPersistResource;

// cITSNeighborhood (interface id 0x7aa51c46): 77 slots, cIGZSerializable's six first (RTTI: cITSNeighborhood, cIGZSerializable
// and cIGZUnknown all at +0). Slot 76 is the deleting destructor.
class cITSNeighborhood : public cIGZSerializable {
public:
    virtual uint8_t Initialize(void *owner) = 0;                                   // slot 6
    virtual uint8_t Shutdown() = 0;                                                // slot 7
    virtual uint8_t Save() = 0;                                                    // slot 8
    virtual uint8_t GetFlag0() = 0;                                                // slot 9
    virtual void SetFlag0() = 0;                                                   // slot 10
    virtual void *GetDelegate() = 0;                                               // slot 11
    virtual void SetDelegate(void *object) = 0;                                    // slot 12
    virtual void *GetSecondOwner() = 0;                                            // slot 13 (group data)
    virtual void SetSecondOwner(void *object) = 0;                                 // slot 14
    virtual void *GetOwner() = 0;                                                  // slot 15 (neighbourhood info)
    virtual cIGZPersistResource *GetPersistResource() = 0;                         // slot 16
    virtual void RunLotResetCommands(void *a, void *b, void *c) = 0;               // slot 17
    virtual uint8_t LoadLots() = 0;                                                // slot 18
    virtual void ResetLots() = 0;                                                  // slot 19
    virtual uint8_t SaveLots() = 0;                                                // slot 20
    virtual uint8_t RemoveLotReferences(int32_t lot) = 0;                          // slot 21
    virtual uint8_t SaveLot(void *lot) = 0;                                        // slot 22
    virtual uint8_t GetLotValue(int32_t lot, int32_t *value, uint8_t *flagA, uint8_t *flagB,
                                uint8_t applyOwnerScale) = 0;                      // slot 23
    virtual uint16_t GetCodeWord(int32_t index) = 0;                               // slot 24
    virtual void SetCodeWord(int32_t index, uint16_t value) = 0;                   // slot 25
    virtual void RefreshLotFlags() = 0;                                            // slot 26
    virtual void RemoveListedObjects(void *report) = 0;                            // slot 27
    virtual void CopyNeighborToPerson(ifaces::cITSPerson *person) = 0;             // slot 28
    virtual void CopyPersonToNeighbor(ifaces::cITSPerson *person) = 0;             // slot 29
    virtual uint8_t GetSyncTableEntry(int32_t index, uint16_t *field, uint16_t *extra) = 0; // slot 30
    virtual uint32_t GetSyncTableCount() = 0;                                      // slot 31
    virtual uint32_t GetGlobalWord() = 0;                                          // slot 32
    virtual float GetGlobalFloatB() = 0;                                           // slot 33
    virtual float GetGlobalFloatA() = 0;                                           // slot 34
    virtual void ValidateInventories() = 0;                                        // slot 35
    virtual void *GetStateHelperOrDelegated() = 0;                                 // slot 36
    virtual ifaces::cITSInventory *GetLotInventory(int32_t lot) = 0;               // slot 37
    virtual void *GetOrCreateMember2(uint32_t id) = 0;                             // slot 38
    virtual void *GetOrCreateMember3(uint32_t id) = 0;                             // slot 39
    virtual void ClearMemberMap1() = 0;                                            // slot 40
    virtual void ClearMemberMap2() = 0;                                            // slot 41
    virtual void ClearMemberMap3() = 0;                                            // slot 42
    virtual void NotifyMembers(uint32_t value) = 0;                                // slot 43
    virtual void **MemberMap1Begin(void **out) = 0;                                // slot 44
    virtual void **MemberMap1End(void **out) = 0;                                  // slot 45
    virtual void **MemberMap2Begin(void **out) = 0;                                // slot 46
    virtual void **MemberMap2End(void **out) = 0;                                  // slot 47
    virtual void **MemberMap3Begin(void **out) = 0;                                // slot 48
    virtual void **MemberMap3End(void **out) = 0;                                  // slot 49
    virtual float GetTen() = 0;                                                    // slot 50
    virtual void SetSize(uint32_t width, uint32_t height) = 0;                     // slot 51
    virtual uint32_t GetWidth() = 0;                                               // slot 52
    virtual uint32_t GetHeight() = 0;                                              // slot 53
    virtual void *RewindLots() = 0;                                                // slot 54
    virtual void *NextLot() = 0;                                                   // slot 55
    virtual ifaces::cITSLotInfo *FindLotByName(const cIGZString *name) = 0;        // slot 56
    virtual void *GetLot(int32_t number) = 0;                                      // slot 57
    virtual void *FindLotAt(int32_t x, int32_t y) = 0;                             // slot 58
    virtual void *FindLotOverlapping(const int32_t *rect) = 0;                     // slot 59
    virtual void CollectLotsOverlapping(const int32_t *rect, std::vector<void *> *out) = 0; // slot 60
    virtual uint8_t AddLot(void *lot) = 0;                                         // slot 61
    virtual uint8_t RemoveLot(int32_t lotNumber) = 0;                              // slot 62
    virtual uint8_t RenameLot(ifaces::cITSLotInfo *lot, const cIGZString *name) = 0; // slot 63
    virtual std::string &SetNameText(const char *text) = 0;                        // slot 64
    virtual const char *GetNameText() = 0;                                         // slot 65
    virtual void DebugDrawFamilyArches(void *family) = 0;                          // slot 66
    virtual void UnregisterRenderIds() = 0;                                        // slot 67
    virtual int32_t NextFreeLotNumber() = 0;                                       // slot 68
    virtual uint8_t FormatNumber(cRZString *out, uint32_t value, void *numberService) = 0; // slot 69
    virtual uint8_t FormatNumberOf(cRZString *out, void *object, void *numberService) = 0; // slot 70
    virtual uint8_t GetFlagByte() = 0;                                             // slot 71
    virtual void SetFlagByte(uint8_t value) = 0;                                   // slot 72
    virtual void *GetStringTable() = 0;                                            // slot 73
    virtual int32_t AdjustCounter(int32_t delta) = 0;                              // slot 74
    virtual uint8_t LookupEntryText(int32_t entryId, void *out) = 0;               // slot 75
    virtual ~cITSNeighborhood() = default;                                         // slot 76 (scalar deleting destructor)
};

namespace nbhood {
// Where the faces sit (base offsets are not constant expressions: the tests check these against the class).
constexpr uint32_t kPersistFace = 0x4;
constexpr uint32_t kContainerFace = 0x8;
constexpr uint32_t kMessageFace = 0xc;
} // namespace nbhood

// Standalone shape (docs/standalone-shape.md): a real C++ class. The compiler emits its four vtables (one per interface, the
// original's slot order), the adjustor thunks of the three secondary faces and the RTTI; the constructor installs them, so
// every neighbourhood the game creates (Create 0x473b80, the only caller of the constructor 0x471d90; both hooked) carries
// our vtables and the game reaches our functions through them. The RVA hooks on the old slot functions are adapters
// (cTSNeighborhoodAdapters below). No game code reads RTTI; the game's vtables are referenced only by the constructor and
// the destructor 0x472490.
class cTSNeighborhood : public cITSNeighborhood,
                        public cIGZPersistResource,
                        public cITSDataContainer,
                        public cIGZMessageTarget2 {
public:
    // The game's vtables (what the original constructor stores; the tests' original side).
    static constexpr uint32_t kVTable = 0xdc9be0;
    static constexpr uint32_t kPersistVTable = 0xdc9d18;
    static constexpr uint32_t kContainerVTable = 0xdc9d4c;
    static constexpr uint32_t kMessageVTable = 0xdc9d68;
    static constexpr uint32_t kClassId = 0x4e474248;
    static constexpr uint32_t kVersion = 0xcb;           // the version Write stores
    static constexpr uint32_t kHelperClass = 0xaba73af;  // the state helper / member object class (framework CreateInstance)
    static constexpr uint32_t kHelperInterface = 0xaba72b8;

    // 0x473b80: allocates 0xcc bytes and builds the neighbourhood; null when the allocation fails.
    static cTSNeighborhood *__cdecl Create();
    // 0x471d90: default sizes (0x80 x 0x80) and name, the empty maps (heads allocated), and the three default service ids
    // (0xa4, 0xaf, 0x9e) in the id set.
    cTSNeighborhood();
    // 0x472490: destroys the members (releases the held objects, empties the maps) in the original's order. The scalar
    // deleting destructor (slot 76, 0x4728c0) is the compiler's.
    ~cTSNeighborhood() override;
    cTSNeighborhood(const cTSNeighborhood &) = delete;
    cTSNeighborhood &operator=(const cTSNeighborhood &) = delete;

    // ---- cIGZUnknown (all four faces; the secondary faces' slots 0-2 are the compiler's adjustor thunks) ----
    // 0x4771c0 (slot 0, ret 8): answers the object's own interface ids (this), the persist face (+4) and the container face
    // (+8), counted through slot 1.
    bool QueryInterface(uint32_t id, void **out) override;
    // slot 1 (folded 0x146a40; thunks 0x37b3c3 / 0x473592 / 0x47359a): ++count.
    uint32_t AddRef() override { return ++mRefCount; }
    // 0x4786e0 (slot 2): counts above one go down by one; otherwise the object deletes itself (slot 76, flags 1).
    uint32_t Release() override;

    // ---- cIGZSerializable ----
    // 0x47af80 (slot 3, ret 4): version 0xcb record: class id, version, the two sizes, the name, the 16 code words, the state
    // helper, then the three helper maps (count, then id and object for each), the flag byte and the flag word. Returns
    // whether the stream reports no error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x477350 (slot 4, ret 4): the reverse, version dependent; makes sure the state helper exists first. Returns whether the
    // stream reports no error.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x473f10 (slot 5): the class id 0x4e474248.
    uint32_t GetGZCLSID() override;

    // ---- cITSNeighborhood ----
    // 0x476010 (slot 6, ret 4): the first call marks the neighbourhood initialised, takes the owner (counted), makes sure the
    // state helper exists and registers the face at +0xc with the message service for each id of the id set (the walker
    // 0x1c0458 asks the service for the object itself). Always answers 1.
    uint8_t Initialize(void *owner) override;
    // 0x47a3d0 (slot 7): the reverse: clears the initialised byte, releases the state helper, delegate, second owner and owner,
    // empties the lot and text maps and unregisters the face for each id (0x1c081e). Answers 1.
    uint8_t Shutdown() override;
    // 0x479560 (slot 8): persist system SaveResource(slot 16 of this = the persist face, 0).
    uint8_t Save() override;
    // 0x476330 (slot 9): bit 0 of the flag word.
    uint8_t GetFlag0() override;
    // 0x476e70 (slot 10): sets bit 0 of the flag word.
    void SetFlag0() override;
    // slot 11 (folded 0x24bdb0): the delegate.
    void *GetDelegate() override { return mDelegate; }
    // 0x479be0 (slot 12): replaces the counted delegate (+0xbc).
    void SetDelegate(void *object) override;
    // slot 13 (folded 0x155e20): the second owner (group data).
    void *GetSecondOwner() override { return mSecondOwner; }
    // 0x479b80 (slot 14): replaces the counted second owner (+0xc0).
    void SetSecondOwner(void *object) override;
    // slot 15 (folded 0x475440): the owner (neighbourhood info).
    void *GetOwner() override { return mOwner; }
    // slot 16 (folded 0x1c04e0): the persist face, null-tested as the original's base conversion.
    cIGZPersistResource *GetPersistResource() override;
    // 0x479cd0 (slot 17, ret 0xc; `this` unused; no result, eax is left over): RunLotResetCommands (callers and argument types
    // unknown). Calls a.slot72(0); b.slot41(word at 0x10daa30, 0, -1, -1, -1, -1), b.slot42(), b.slot25(1); then
    // c.slot54(&text, 0, 0, 0, 0) with a local cRZString of the text the char* at 0x1011d18 points to ("reset lot").
    void RunLotResetCommands(void *a, void *b, void *c) override;
    // 0x476390 (slot 18): LoadLots: once, reads every lot record of the group and indexes those whose file exists.
    uint8_t LoadLots() override;
    // 0x47a570 (slot 19): clears +0xc8, tells the resource manager (slot 10) to forget each lot's key, empties the lot and text maps.
    void ResetLots() override;
    // 0x4796e0 (slot 20): nothing (answers 1) when the byte at +0xc8 is clear; else saves every lot (slot 22) whose kind says so and
    // deletes the persist system's lot records that no lot owns any more. Answers whether all saves worked.
    uint8_t SaveLots() override;
    // 0x4735e0 (slot 21, ret 4): RemoveLotReferences: un-flags the sim inventories' tokens that point at the lot (for lots
    // of type 3) and deletes the lot's business record. Answers whether the lot is known.
    uint8_t RemoveLotReferences(int32_t lot) override;
    // 0x479610 (slot 22, ret 4): persists a lot: key {0xbf999e7, group, lot slot 18, 0}, the lot's resource saved with it.
    uint8_t SaveLot(void *lot) override;
    // 0x474ac0 (slot 23, ret 0x14): GetLotValue: the lot's worth (from its simulator record, else its size) and two flags.
    uint8_t GetLotValue(int32_t lot, int32_t *value, uint8_t *flagA, uint8_t *flagB, uint8_t applyOwnerScale) override;
    // 0x475450 (slot 24, ret 4): the code word at the index (no range check).
    uint16_t GetCodeWord(int32_t index) override; // AX only
    // 0x479bc0 (slot 25, ret 8): stores a code word.
    void SetCodeWord(int32_t index, uint16_t value) override;
    // 0x47aef0 (slot 26): for each object of the app's slot 26 -> 45 list sets attribute 0x20 to 0 when the cached slot-23 object's
    // slot 14 answers true for it (its slot 30), else rewrites the value it had.
    void RefreshLotFlags() override;
    // 0x476f90 (slot 27, ret 4; `this` unused): RemoveListedObjects: deletes the objects a definition-table selection
    // accepts, then reports the first required object definition the lot has no instance of.
    void RemoveListedObjects(void *report) override;
    // 0x4768e0 (slot 28, ret 4): CopyNeighborToPerson: the neighbour record of the person (or a new one) into the person.
    void CopyNeighborToPerson(ifaces::cITSPerson *person) override;
    // 0x479850 (slot 29, ret 4): CopyPersonToNeighbor: the reverse.
    void CopyPersonToNeighbor(ifaces::cITSPerson *person) override;
    // 0x475310 (slot 30, ret 0xc): GetSyncTableEntry: the static person/neighbour sync table's entry (0x4b8f80): its low and
    // high 16 bits into *field / *extra; false (both 0) for index <= 0 or past the end.
    uint8_t GetSyncTableEntry(int32_t index, uint16_t *field, uint16_t *extra) override;
    // 0x475330 (slot 31, 5 bytes: jmp 0x4b8fd0): the table's entry count (GetNeighborSyncCount; `this` unused).
    uint32_t GetSyncTableCount() override;
    // 0x475c60 / 0x475c80 / 0x475c70 / 0x47a4a0 (slots 32 / 33 / 34 / 50): constants (an int, two floats from the game's data, 10.0).
    uint32_t GetGlobalWord() override;
    float GetGlobalFloatB() override;
    float GetGlobalFloatA() override;
    // 0x478ed0 (slot 35): ValidateInventories: drops inventories of lots, families and sims that are gone and validates the
    // tokens of the others (timed with a stopwatch, mode 5).
    void ValidateInventories() override;
    // 0x4742e0 (slot 36): the delegate's slot 36 when there is a delegate, else the state helper.
    void *GetStateHelperOrDelegated() override;
    // 0x474530 (slot 37, ret 4): GetLotInventory: the kind-1 inventory of the lot, made when absent (three paths: lot 0, a lot
    // of this neighbourhood, a lot of another one through the group data).
    ifaces::cITSInventory *GetLotInventory(int32_t lot) override;
    // 0x4743d0 (slot 38, ret 4): the delegate's slot 38 when there is one; else the kind-2 member under the id's low 16 bits,
    // made by the framework (class 0xaba73af) when absent: identity (2, id), owner value = the persist face's group. The map
    // entry is inserted even when the framework fails (then it holds null).
    void *GetOrCreateMember2(uint32_t id) override;
    // 0x474830 (slot 39, ret 4): the same for kind 3 (map at +0x88, delegate slot 39); the new member's owner value comes from
    // the app singleton's slot 10 -> slot 16(id as signed 16 bits, 1) -> slot 10 -> slot 67 -> slot 11 chain (when it exists).
    void *GetOrCreateMember3(uint32_t id) override;
    // 0x473740 / 0x473710 / 0x473770 (slots 40 / 41 / 42): empty the helper map at +0x78 / +0x80 / +0x88 (values released).
    void ClearMemberMap1() override;
    void ClearMemberMap2() override;
    void ClearMemberMap3() override;
    // 0x4737e0 (slot 43, ret 4): passes `value` to slot 17 of the delegate (slot 43), of the object two steps down the app
    // singleton's slot 17 chain, of the state helper and of every object of the three member maps.
    void NotifyMembers(uint32_t value) override;
    // 0x4750e0 / 0x4750f0 (slots 44 / 45, ret 4): begin / end iterator of the +0x78 map into *out. Returns out.
    void **MemberMap1Begin(void **out) override;
    void **MemberMap1End(void **out) override;
    // 0x474260 / 0x474290 / 0x4752b0 / 0x4752e0 (slots 46..49, ret 4): the same for the +0x80 and +0x88 maps, forwarded to the
    // delegate's slots 46..49 when there is a delegate.
    void **MemberMap2Begin(void **out) override;
    void **MemberMap2End(void **out) override;
    void **MemberMap3Begin(void **out) override;
    void **MemberMap3End(void **out) override;
    float GetTen() override;
    // 0x479c30 (slot 51, ret 8): stores the two sizes.
    void SetSize(uint32_t width, uint32_t height) override;
    // slots 52 / 53 (folded 0x147800 / 0x1694c0): the two sizes.
    uint32_t GetWidth() override { return mWidth; }
    uint32_t GetHeight() override { return mHeight; }
    // 0x473b60 (slot 54): rewinds the lot cursor and returns what slot 55 gives.
    void *RewindLots() override;
    // 0x476f50 (slot 55): the next lot object, or null at the end.
    void *NextLot() override;
    // 0x474df0 (slot 56, ret 4): FindLotByName: "#<number>" names go through GetLot (slot 57), others through the name index.
    ifaces::cITSLotInfo *FindLotByName(const cIGZString *name) override;
    // 0x475000 (slot 57, ret 4): the lot object with the number, or null (the search is a lower bound that must not hit the head).
    void *GetLot(int32_t number) override;
    // 0x474a20 (slot 58, ret 8): the first lot object that is valid, not of type 0x10 and whose bounds contain the point.
    void *FindLotAt(int32_t x, int32_t y) override;
    // 0x473fd0 (slot 59, ret 4): the first such lot whose bounds overlap the rectangle {left, top, right, bottom} with a positive area.
    void *FindLotOverlapping(const int32_t *rect) override;
    // 0x475100 (slot 60, ret 8): clears the vector and collects every distinct such overlapping lot object.
    void CollectLotsOverlapping(const int32_t *rect, std::vector<void *> *out) override;
    // 0x4730b0 (slot 61, ret 4): AddLot: numbers, indexes and opens the lot's file, posts message 0xae. Answers 1.
    uint8_t AddLot(void *lot) override;
    // 0x478a70 (slot 62, ret 4): RemoveLot: by lot NUMBER (not the lot object): messages 0xaf (sent) / 0xb0 (posted) around dropping
    // the lot from both maps and the lot inventory. Answers 0 when the number is not in the lot map, else 1.
    uint8_t RemoveLot(int32_t lotNumber) override;
    // 0x478d80 (slot 63, ret 8): RenameLot: re-keys the lot in the name index; answers whether the new name was free.
    uint8_t RenameLot(ifaces::cITSLotInfo *lot, const cIGZString *name) override;
    // slot 64 (folded 0x44cfa0: `add ecx, 0x40; jmp` std::string::assign(const char *)): the name's text replaced; answers the
    // name's string.
    std::string &SetNameText(const char *text) override { return mName.mString.assign(text); }
    // 0x475770 (slot 65): the name's character pointer (tail jump to the name string's slot 5).
    const char *GetNameText() override;
    // 0x479de0 (slot 66, ret 4): DebugDrawFamilyArches: debug arches from the family's home lot to its relatives' lots.
    void DebugDrawFamilyArches(void *family) override;
    // 0x475cb0 (slot 67): UnregisterRenderIds: clears the SG debug drawing and unregisters every id of +0xa8.
    void UnregisterRenderIds() override;
    // 0x472ad0 (slot 68): NextFreeLotNumber: the first number from +0x6c whose lot file does not exist yet.
    int32_t NextFreeLotNumber() override;
    // 0x473f50 (slot 69, ret 0xc): out->Sprintf(format, text, value) with the text the number service (the argument, or slot 6 of
    // the cached slot-23 object when null) gives through its slot 8 -> slot 5; answers whether value is nonzero.
    uint8_t FormatNumber(cRZString *out, uint32_t value, void *numberService) override;
    // 0x473fa0 (slot 70, ret 0xc): FormatNumber (slot 69) with object's slot 18 as the value.
    uint8_t FormatNumberOf(cRZString *out, void *object, void *numberService) override;
    // 0x4749c0 (slot 71): the flag byte at +0xb1.
    uint8_t GetFlagByte() override;
    // slot 72 (folded 0x1d3aa0, ret 4): stores the flag byte (al keeps it).
    void SetFlagByte(uint8_t value) override { mFlagByte = value; }
    // 0x475bc0 (slot 73): the delegate's slot 73 when there is a delegate; else the string table resource (key {'STR#', group,
    // 0x1f4, 1}) the framework creates (class 0xdb4cb0f9) on first use and loads, kept at +0xa4.
    void *GetStringTable() override;
    // 0x4754d0 (slot 74, ret 4): the counter record {0xfdfb4f5, group, 0, 0} of the persist system's segment: created holding 1 when
    // absent (delta ignored), otherwise read, and rewritten as value + delta when delta is not zero. Answers the value.
    int32_t AdjustCounter(int32_t delta) override;
    // 0x475a70 (slot 75, ret 8): the delegate's slot 75 when there is one; else finds the entry of the state helper's list whose
    // class id is 0xafe3c527 and whose slot 21(1) equals entryId and asks the string table (slot 73) for slot 7(out, slot 21(2), -1).
    uint8_t LookupEntryText(int32_t entryId, void *out) override;

    // ---- cIGZPersistResource (+4): the 16-byte resource key lives at +0x90 ----
    // 0x4749d0 (slot 3, hidden result pointer, ret 4): the key.
    cGZPersistResourceKey GetKey() override;
    // 0x479a20 (slot 4, ret 4): stores the key. True.
    bool SetKey(const cGZPersistResourceKey &key) override;
    // slots 5-7 (folded 0x22fa70 / 0x24bda0 / 0x24bde0) and 8-9 (folded 0x479ca0 / 0x4799c0): the key's words.
    uint32_t GetType() override { return mKey[0]; }
    uint32_t GetGroup() override { return mKey[1]; }
    uint32_t GetInstance() override { return mKey[2]; }
    bool SetType(uint32_t type) override
    {
        mKey[0] = type;
        return true;
    }
    bool SetGroup(uint32_t group) override
    {
        mKey[1] = group;
        return true;
    }
    // 0x4799f0 (slot 10, ret 4): stores the key's instance word. True.
    bool SetInstance(uint32_t instance) override;
    // slot 11 (folded 0x145770, `mov eax, [ecx + 0xc]` from the +4 face): the count.
    uint32_t GetRefCount() override { return mRefCount; }

    // ---- cITSDataContainer (+8) ----
    // slots 3 / 4 (folded 0x18a7a0 `xor al, al` / 0x148260 `mov al, 1`, ret 4).
    bool CollectReferenced(ifaces::cITSDataImportExportContext *) override { return false; }
    bool Export(ifaces::cITSDataImportExportContext *) override { return true; }
    // 0x477d70 (slot 5, ret 4; src/ts/cTSNeighborhoodImport.cpp): ImportFromContainer: the container's import source
    // (QueryInterface 0x4e9c59c7) names a neighbourhood group and renumbers lots (its slot 25) and family / neighbour ids (its
    // slot 31's id map). For each of its lots: the source neighbourhood's lot inventory tokens go into this one's inventory
    // of the new lot (made when missing), the lot's owner family is renumbered, and the lot's business record gets its people
    // renumbered and its key moved here. Then this group's old business records are deleted and the moved ones saved. Answers
    // whether the source neighbourhood was found (0 without a container, a source or the container's object 0x656b8f0c).
    bool Import(ifaces::cITSDataImportExportContext *context) override;

    // ---- cIGZMessageTarget2 (+0xc) ----
    // 0x4738d0 (slot 3, ret 4): 0x9e (the lots owned by the family id lose their owner), 0xa4 (drops a family inventory), 0xaf
    // (RemoveLotReferences). Answers whether the message was handled.
    bool DoMessage(void *message) override;

    // ---- not slots ----
    // 0x473a70 (thiscall, no stack arguments): creates the state helper (class 0xaba73af) when missing and hands it the owner's
    // value (from the delegate when set, else the persist face's). A static __fastcall member (`self` in ecx, as the original's
    // `this`) rather than an adapter, so the hook points at the function our callers call (their byte matches tie the call by
    // the hook).
    static void __fastcall EnsureStateHelper(cTSNeighborhood *self);

    cIGZPersistResource *PersistFace() { return this; }

    // +0x00 cITSNeighborhood / cIGZSerializable vtable, +0x04 cIGZPersistResource, +0x08 cITSDataContainer,
    // +0x0c cIGZMessageTarget2 (the compiler's).
    uint32_t mRefCount;                // +0x10
    uint16_t mCode[16];                // +0x14 sixteen 16-bit words written one by one
    uint32_t mWidth;                   // +0x34 default 0x80
    uint32_t mHeight;                  // +0x38 default 0x80
    // The members below with constructors and destructors are built in the constructor's body and destroyed in the
    // destructor's, in the original's order (it releases counted pointers between them): unions keep the compiler from doing
    // either by itself. cRZString is still hook-shaped (Construct / Destruct store the game's string vtable).
    union { cRZString mName; };                  // +0x3c default text from the game's data
    union { NeighborhoodLotMap mLots; };         // +0x5c lot number -> lot (cITSLotInfo face, counted)
    union { NeighborhoodTextSet mTexts; };       // +0x64 the lot-name index (case-insensitive name -> lot)
    uint32_t mField6C;                 // +0x6c next lot number to try (NextFreeLotNumber, slot 68; LoadLots raises it);
                                       //       1 after construction and after a reset
    NeighborhoodLotMap::iterator mLotCursor; // +0x70 the lot cursor of slots 54 / 55 (the head node when idle)
    void *mStateHelper;                // +0x74 counted: created by the framework, written and read first
    // The members are cTSInventory objects (cITSInventory; class 0xaba73af, interface 0xaba72b8), SetOwner(kind, id):
    union { NeighborhoodMemberMap mMembers1; }; // +0x78 (kind 1) lot inventories, by lot number
    union { NeighborhoodMemberMap mMembers2; }; // +0x80 (kind 2) family inventories, by family id
    union { NeighborhoodMemberMap mMembers3; }; // +0x88 (kind 3) sim inventories, by neighbour id
    uint32_t mKey[4];                  // +0x90 the persist face's resource key (type, group, instance, 0)
    uint32_t mFlags;                   // +0xa0 bit 0 = the flag of slots 9 / 10
    void *mField_A4;                   // +0xa4 counted (released in the destructor)
    union { NeighborhoodPlainMap mPlain; }; // +0xa8 render ids of the debug drawing (UnregisterRenderIds, slot 67, hands each
                                            //       value to the SG render-id registry, then clears the map)
    uint8_t mInitialized;              // +0xb0
    uint8_t mFlagByte;                 // +0xb1 written and read as one byte (version 0x6f and up)
    union { NeighborhoodIdSet mServiceIds; }; // +0xb4 {0xa4, 0xaf, 0x9e} after construction
    void *mDelegate;                   // +0xbc counted cITSNeighborhood
    void *mSecondOwner;                // +0xc0 counted cITSNeighborhoodGroupData (slot 13 / 14 GetGroupData / SetGroupData)
    void *mOwner;                      // +0xc4 counted cITSNeighborhoodInfo (slot 15 GetInfo; set by Initialize)
    uint8_t mByteC8;                   // +0xc8 lots loaded (LoadLots / AddLot set it, ResetLots clears it; SaveLots
                                       //       and ValidateInventories test it)
};
static_assert(sizeof(cTSNeighborhood) == 0xcc);
static_assert(offsetof(cTSNeighborhood, mRefCount) == 0x10);
static_assert(offsetof(cTSNeighborhood, mCode) == 0x14);
static_assert(offsetof(cTSNeighborhood, mName) == 0x3c);
static_assert(offsetof(cTSNeighborhood, mLots) == 0x5c);
static_assert(offsetof(cTSNeighborhood, mTexts) == 0x64);
static_assert(offsetof(cTSNeighborhood, mLotCursor) == 0x70);
static_assert(offsetof(cTSNeighborhood, mStateHelper) == 0x74);
static_assert(offsetof(cTSNeighborhood, mMembers1) == 0x78);
static_assert(offsetof(cTSNeighborhood, mMembers3) == 0x88);
static_assert(offsetof(cTSNeighborhood, mKey) == 0x90);
static_assert(offsetof(cTSNeighborhood, mFlags) == 0xa0);
static_assert(offsetof(cTSNeighborhood, mPlain) == 0xa8);
static_assert(offsetof(cTSNeighborhood, mFlagByte) == 0xb1);
static_assert(offsetof(cTSNeighborhood, mServiceIds) == 0xb4);
static_assert(offsetof(cTSNeighborhood, mDelegate) == 0xbc);
static_assert(offsetof(cTSNeighborhood, mByteC8) == 0xc8);

// The RVA hooks' entry points (src/hook/tables/cTSNeighborhood.cpp). Every neighbourhood the game creates carries our vtables,
// so these run only where the game calls an old entry point directly or holds a neighbourhood built with its own vtables
// (none while the constructor and Create hooks are on). Original calling convention (this in ecx = the face of the slot), our
// functions called without virtual dispatch, bodies inlined.
struct cTSNeighborhoodAdapters {
    using Self = cTSNeighborhood;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static uint8_t __fastcall Initialize(Self *self, void *, void *owner);
    static uint8_t __fastcall Shutdown(Self *self, void *);
    static uint8_t __fastcall Save(Self *self, void *);
    static uint8_t __fastcall GetFlag0(Self *self, void *);
    static void __fastcall SetFlag0(Self *self, void *);
    static void __fastcall SetDelegate(Self *self, void *, void *object);
    static void __fastcall SetSecondOwner(Self *self, void *, void *object);
    static void __fastcall RunLotResetCommands(Self *self, void *, void *a, void *b, void *c);
    static uint8_t __fastcall LoadLots(Self *self, void *);
    static void __fastcall ResetLots(Self *self, void *);
    static uint8_t __fastcall SaveLots(Self *self, void *);
    static uint8_t __fastcall RemoveLotReferences(Self *self, void *, int32_t lot);
    static uint8_t __fastcall SaveLot(Self *self, void *, void *lot);
    static uint8_t __fastcall GetLotValue(Self *self, void *, int32_t lot, int32_t *value, uint8_t *flagA, uint8_t *flagB,
                                          uint8_t applyOwnerScale);
    static uint16_t __fastcall GetCodeWord(Self *self, void *, int32_t index);
    static void __fastcall SetCodeWord(Self *self, void *, int32_t index, uint16_t value);
    static void __fastcall RefreshLotFlags(Self *self, void *);
    static void __fastcall RemoveListedObjects(Self *self, void *, void *report);
    static void __fastcall CopyNeighborToPerson(Self *self, void *, ifaces::cITSPerson *person);
    static void __fastcall CopyPersonToNeighbor(Self *self, void *, ifaces::cITSPerson *person);
    static uint8_t __fastcall GetSyncTableEntry(Self *self, void *, int32_t index, uint16_t *field, uint16_t *extra);
    static uint32_t __fastcall GetGlobalWord(Self *self, void *);
    static float __fastcall GetGlobalFloatB(Self *self, void *);
    static float __fastcall GetGlobalFloatA(Self *self, void *);
    static void __fastcall ValidateInventories(Self *self, void *);
    static void *__fastcall GetStateHelperOrDelegated(Self *self, void *);
    static ifaces::cITSInventory *__fastcall GetLotInventory(Self *self, void *, int32_t lot);
    static void *__fastcall GetOrCreateMember2(Self *self, void *, uint32_t id);
    static void *__fastcall GetOrCreateMember3(Self *self, void *, uint32_t id);
    static void __fastcall ClearMemberMap1(Self *self, void *);
    static void __fastcall ClearMemberMap2(Self *self, void *);
    static void __fastcall ClearMemberMap3(Self *self, void *);
    static void __fastcall NotifyMembers(Self *self, void *, uint32_t value);
    static void **__fastcall MemberMap1Begin(Self *self, void *, void **out);
    static void **__fastcall MemberMap1End(Self *self, void *, void **out);
    static void **__fastcall MemberMap2Begin(Self *self, void *, void **out);
    static void **__fastcall MemberMap2End(Self *self, void *, void **out);
    static void **__fastcall MemberMap3Begin(Self *self, void *, void **out);
    static void **__fastcall MemberMap3End(Self *self, void *, void **out);
    static float __fastcall GetTen(Self *self, void *);
    static void __fastcall SetSize(Self *self, void *, uint32_t width, uint32_t height);
    static void *__fastcall RewindLots(Self *self, void *);
    static void *__fastcall NextLot(Self *self, void *);
    static ifaces::cITSLotInfo *__fastcall FindLotByName(Self *self, void *, const cIGZString *name);
    static void *__fastcall GetLot(Self *self, void *, int32_t number);
    static void *__fastcall FindLotAt(Self *self, void *, int32_t x, int32_t y);
    static void *__fastcall FindLotOverlapping(Self *self, void *, const int32_t *rect);
    static void __fastcall CollectLotsOverlapping(Self *self, void *, const int32_t *rect, std::vector<void *> *out);
    static uint8_t __fastcall AddLot(Self *self, void *, void *lot);
    static uint8_t __fastcall RemoveLot(Self *self, void *, int32_t lotNumber);
    static uint8_t __fastcall RenameLot(Self *self, void *, ifaces::cITSLotInfo *lot, const cIGZString *name);
    static const char *__fastcall GetNameText(Self *self, void *);
    static void __fastcall DebugDrawFamilyArches(Self *self, void *, void *family);
    static void __fastcall UnregisterRenderIds(Self *self, void *);
    static int32_t __fastcall NextFreeLotNumber(Self *self, void *);
    static uint8_t __fastcall FormatNumber(Self *self, void *, cRZString *out, uint32_t value, void *numberService);
    static uint8_t __fastcall FormatNumberOf(Self *self, void *, cRZString *out, void *object, void *numberService);
    static uint8_t __fastcall GetFlagByte(Self *self, void *);
    static void *__fastcall GetStringTable(Self *self, void *);
    static int32_t __fastcall AdjustCounter(Self *self, void *, int32_t delta);
    static uint8_t __fastcall LookupEntryText(Self *self, void *, int32_t entryId, void *out);
    // the +4 face
    // A struct result: the original's hidden pointer is an explicit stack argument (__fastcall would put it in ecx).
    static cGZPersistResourceKey *__fastcall GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out);
    static bool __fastcall SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey &key);
    static bool __fastcall SetInstance(cIGZPersistResource *face, void *, uint32_t instance);
    // the +8 face
    static bool __fastcall Import(cITSDataContainer *face, void *, ifaces::cITSDataImportExportContext *context);
    // the +0xc face
    static bool __fastcall DoMessage(cIGZMessageTarget2 *face, void *, void *message);
};
