#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"
#include "ts/cTSNeighborGeneticDataTable.h" // NeighborDataRef: the counted object reference the member maps hold

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

// cTSFamily: a household (RTTI cTSFamily, bases cITSFamily + cIGZSerializable at +0, cIGZPersistResource at +4). 0xc8 bytes,
// vtable 0xdcb774 (80 slots) and 0xdcb8b8 (12 slots) for the persist face at +4; the base constructor first writes 0xda66e4
// into +4. RVAs are 1.18.0.187. Its persisted record has the class id 0x46414d49 and version 0x55.
//
// The family keeps two groups of member objects, each as a pair of maps keyed by the object's 16-byte resource key (type,
// group, instance, instance high): group 1 at +0xa8/+0xb0 (loaded flag +0x76, dirty flag +0x75, load slot 33) and group 2 at
// +0xb8/+0xc0 (loaded flag +0x78, dirty flag +0x77, load slot 78). The accessors load a group on first use.

// A 16-byte resource key. The game orders keys by the words type, group, instance high, instance (the last two swapped).
struct FamilyKey {
    uint32_t mType;
    uint32_t mGroup;
    uint32_t mInstance;
    uint32_t mInstanceHigh;
};
static_assert(sizeof(FamilyKey) == 16);

struct FamilyKeyLess {
    bool operator()(const FamilyKey &a, const FamilyKey &b) const noexcept
    {
        if (a.mType != b.mType) return a.mType < b.mType;
        if (a.mGroup != b.mGroup) return a.mGroup < b.mGroup;
        if (a.mInstanceHigh != b.mInstanceHigh) return a.mInstanceHigh < b.mInstanceHigh;
        return a.mInstance < b.mInstance;
    }
};

// key -> counted object. The node (0x24 bytes) is the game's: links, colour and nil bytes, the key at +0x10, the object at +0x20.
using FamilyObjectMap = std::map<FamilyKey, NeighborDataRef, FamilyKeyLess>;
static_assert(sizeof(FamilyObjectMap) == 8);
using FamilyIdSet = std::set<uint32_t>;
static_assert(sizeof(FamilyIdSet) == 8);
// The id set callers hand to CopyIdSet: its nodes (0x14 bytes) come from the allocator service.
using FamilyServiceIdSet = std::set<uint32_t, std::less<uint32_t>, GZServiceAllocator<uint32_t>>;
static_assert(sizeof(FamilyServiceIdSet) == 8);

class cTSFamily;
struct ServiceRefVector;

// cITSFamily: the household's own face (+0), slots 6..79 after cIGZSerializable's, in slot order; slot 77 is the deleting
// destructor (the interface's virtual destructor). The comments on cTSFamily's overrides say what each does. Callers of other
// family objects keep the generated ifaces::cITSFamily view.
class cITSFamily : public cIGZSerializable {
public:
    virtual uint8_t SetFamilyId(uint32_t id) = 0;                       // 6
    virtual void AddMemberId(uint32_t id) = 0;                          // 7
    virtual void RemoveMemberId(uint32_t id) = 0;                       // 8
    virtual void ImportLotIntoGroup1(uint32_t lot) = 0;                 // 9
    virtual void InsertKeyIntoGroup1(const FamilyKey *key) = 0;         // 10
    virtual void InsertObjectIntoGroup1(void *object) = 0;              // 11
    virtual uint8_t HasObjectInGroup1(const FamilyKey *key) = 0;        // 12
    virtual void AddObjectToGroup1(void *object) = 0;                   // 13
    virtual void CollectObjectsOfGroup1(ServiceRefVector *refVector) = 0; // 14
    virtual uint32_t CountOutfitCombinations(uint32_t categoryMask, uint32_t secondMask, uint32_t ageMask,
                                             ServiceRefVector *objects) = 0; // 15
    virtual void ImportLotObjects(uint32_t lot) = 0;                    // 16
    virtual void InsertKeyIntoGroup2(const FamilyKey *key) = 0;         // 17
    virtual void InsertObjectIntoGroup2(void *object) = 0;              // 18
    virtual uint8_t HasObjectInGroup2(const FamilyKey *key) = 0;        // 19
    virtual void AddObjectToGroup2(void *object) = 0;                   // 20
    virtual void CollectFilteredObjectsOfGroup1(ServiceRefVector *refVector) = 0; // 21
    virtual uint8_t GetFlag79() = 0;                                    // 22
    virtual void SetFlag79(uint8_t value) = 0;                          // 23
    virtual uint32_t CountMembers(uint32_t flags) = 0;                  // 24
    virtual uint32_t CheckMoveIn(cITSFamily *other) = 0;                // 25
    virtual uint32_t *GetMemberIdSlot(int32_t index) = 0;               // 26
    virtual uint32_t GetMemberId(int32_t index) = 0;                    // 27
    virtual uint32_t *FindMemberId(uint32_t id) = 0;                    // 28
    virtual uint8_t HasMemberId(uint32_t id) = 0;                       // 29
    virtual uint8_t HasMemberObject(void *object) = 0;                  // 30
    virtual cIGZPersistResource *GetPersistFace() = 0;                  // 31
    virtual uint8_t LoadTexts(int32_t id, uint32_t unused) = 0;         // 32
    virtual uint8_t LoadFirstGroup() = 0;                               // 33
    virtual uint8_t SaveTexts(uint32_t unused) = 0;                     // 34
    virtual uint8_t SaveFirstGroup(uint8_t force) = 0;                  // 35
    virtual uint8_t DeleteResources() = 0;                              // 36
    virtual cRZString *GetName() = 0;                                   // 37
    virtual void SetName(const void *text) = 0;                         // 38
    virtual cRZString *GetDescription() = 0;                            // 39
    virtual void SetDescription(const void *text) = 0;                  // 40
    virtual uint32_t GetFamilyId() = 0;                                 // 41
    virtual uint32_t GetHomeLot() = 0;                                  // 42
    virtual void SetHomeLot(uint32_t lot) = 0;                          // 43
    virtual uint32_t GetBusinessLot() = 0;                              // 44
    virtual void SetBusinessLot(uint32_t lot) = 0;                      // 45
    virtual uint32_t GetVacationLot() = 0;                              // 46
    virtual void SetVacationLot(uint32_t lot) = 0;                      // 47
    virtual uint8_t IsFamilyLot(uint32_t lot) = 0;                      // 48
    virtual uint32_t GetUniqueId(uint8_t create) = 0;                   // 49
    virtual void SetUniqueId(uint32_t id) = 0;                          // 50
    virtual uint32_t GetField68() = 0;                                  // 51
    virtual void SetField68(uint32_t value) = 0;                        // 52
    virtual int32_t GetFunds() = 0;                                     // 53
    virtual void SetFunds(int32_t funds) = 0;                           // 54
    virtual uint32_t GetField70() = 0;                                  // 55
    virtual void SetField70(uint32_t value) = 0;                        // 56
    virtual uint32_t GetField0C() = 0;                                  // 57
    virtual void SetField0C(uint32_t value) = 0;                        // 58
    virtual uint32_t GetHomeLotWorth() = 0;                             // 59
    virtual uint32_t GetRegistryEntry() = 0;                            // 60
    virtual void FlushChanges() = 0;                                    // 61
    virtual void AddToIdSet(uint32_t id) = 0;                           // 62
    virtual void ClearIdSet() = 0;                                      // 63
    virtual void CopyIdSet(FamilyServiceIdSet *out) = 0;                // 64
    virtual void *GetRecord() = 0;                                      // 65
    virtual void SetFlagBit0(uint8_t on) = 0;                           // 66
    virtual uint8_t GetFlagBit0() = 0;                                  // 67
    virtual void SetFlagBit3(uint8_t on) = 0;                           // 68
    virtual uint8_t GetFlagBit3() = 0;                                  // 69
    virtual void SetFlagBit1(uint8_t on) = 0;                           // 70
    virtual uint8_t GetFlagBit1() = 0;                                  // 71
    virtual void SetFlagBit2(uint8_t on) = 0;                           // 72
    virtual uint8_t GetFlagBit2() = 0;                                  // 73
    virtual int32_t ScaleByLotModifiers(int32_t amount) = 0;            // 74
    virtual void ForgetResources() = 0;                                 // 75
    virtual void FindMemberLot(uint32_t shortId, uint32_t *out) = 0;    // 76
    virtual ~cITSFamily() = default;                                    // 77 (the scalar deleting destructor)
    virtual uint8_t LoadSecondGroup() = 0;                              // 78
    virtual uint8_t SaveSecondGroup(uint8_t force) = 0;                 // 79
};

// Standalone shape (docs/standalone-shape.md, lane FA): a real C++ class; the compiler emits both vtables, the persist face's
// adjustor thunks (QueryInterface 0x49f5e2, Release 0x49fe9a; AddRef's is the folded 0x1448e9) and the RTTI. The game's
// 0xda66e4, which the original constructor stores at +4 first, is cIGZPersistResource's own vtable (the interface constructor's
// store), not another base. Creation: the constructor 0x49b340, called only by the creator 0x49e180 (its address stored by the
// cTSSimCOMDirector constructor 0x415ae0): both hooked (Create and the constructor adapter). The game's vtables are referenced
// only by the constructor and the destructor 0x49b690. The RVA hooks on the old functions are adapters (cTSFamilyAdapters).
class cTSFamily : public cITSFamily, public cIGZPersistResource {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them any more).
    static constexpr uint32_t kOriginalVTable = 0xdcb774, kOriginalPersistVTable = 0xdcb8b8;
    static constexpr uint32_t kClassId = 0x46414d49;
    static constexpr uint32_t kVersion = 0x55;
    static constexpr uint32_t kMaxFamilyId = 32000; // ids 1..32000 are valid (0x15 - 1 below 31999 in the loaders)

    // 0x49b340: an empty household (id 0, funds 0, both texts empty, no members, empty maps and set).
    cTSFamily();
    // 0x49b690: releases the record, destroys the maps, set, vector and texts. Slot 77 (0x49ba50) is its scalar deleting
    // destructor (ret 4; bit 0 of the flags frees the 0xc8 bytes; returns this).
    ~cTSFamily() override;
    cTSFamily(const cTSFamily &) = delete;
    cTSFamily &operator=(const cTSFamily &) = delete;
    // 0x49e180 (cdecl): new; null when the allocation fails.
    static cTSFamily *__cdecl Create();

    // cIGZUnknown (both faces)
    // 0x49f590 (slot 0, ret 8): ids 1, 0x7ab5bcbb and 0xe4fda3d4 give this, 0x456b8f1d the face at +4; the object is retained
    // (slot 1) and AL is 1. Anything else gives AL 0 and leaves *out alone.
    bool QueryInterface(uint32_t id, void **out) override;
    uint32_t AddRef() override; // folded 0x145260 (slot 1)
    // 0x49fe80 (slot 2): a count above one is decremented (and returned); otherwise the object deletes itself (slot 77, flags 1)
    // and 0 is returned.
    uint32_t Release() override;

    // cIGZSerializable
    // 0x4a1100 (slot 3, ret 4): writes the record, version 0x55: class id, version, a spare word, the three lots, mField68, the
    // funds (slot 53), mIdCount, mFlags, the member id count and ids, mUniqueId, mField0C, mField70. AL = no stream error.
    bool Write(ifaces::cIGZOStream *stream) override;
    // 0x49fa30 (slot 4, ret 4): reads a record of any version up to 0x55 (older versions fill the missing fields from others
    // or defaults; below version 5 the name comes from a string reader the stream hands out). AL = no stream error.
    bool Read(ifaces::cIGZIStream *stream) override;
    // 0x49e250 (slot 5)
    uint32_t GetGZCLSID() override;

    // 0x49efb0 (slot 6, ret 4): sets the family id, empties both texts and marks the family modified. AL = 1.
    uint8_t SetFamilyId(uint32_t id) override;
    // 0x49cc20 (slot 7, ret 4): appends the id to the member vector unless it is already there.
    void AddMemberId(uint32_t id) override;
    // 0x4a03a0 (slot 8, ret 4): removes the first match of the id from the member vector.
    void RemoveMemberId(uint32_t id) override;

    // 0x49d870 / 0x49d8e0 (slots 12 / 19, ret 4): whether the object map of the group (loaded on first use) holds the key.
    uint8_t HasObjectInGroup1(const FamilyKey *key) override;
    uint8_t HasObjectInGroup2(const FamilyKey *key) override;
    // 0x49ff30 / 0x4a02b0 (slots 13 / 20, ret 4): asks the object for interface 0xc560f33; if it answers, replaces the entry
    // under the object's key in the group's second map with it and drops the entry under that key from the first map, marking
    // the group dirty.
    void AddObjectToGroup1(void *object) override;
    void AddObjectToGroup2(void *object) override;
    // 0x49e270 (slot 14, ret 4): appends a counted reference of every object of the group's first map to the vector.
    // (0x49e5c0, slot 21, is a filtered twin on group 1: CollectFilteredObjectsOfGroup1.)
    void CollectObjectsOfGroup1(ServiceRefVector *refVector) override;

    // 0x49eff0 / 0x4a0c70 (slots 22 / 23)
    uint8_t GetFlag79() override;
    void SetFlag79(uint8_t value) override;

    // 0x49ef80 (slot 26, ret 4): address of the n-th member id, or null when n is negative or past the end.
    uint32_t *GetMemberIdSlot(int32_t index) override;
    // 0x49dfa0 (slot 27, ret 4): the n-th member id, or 0.
    uint32_t GetMemberId(int32_t index) override;
    // 0x49f450 (slot 28, ret 4): address of the first member id equal to the argument, or null.
    uint32_t *FindMemberId(uint32_t id) override;
    // 0x4a0e30 (slot 29, ret 4)
    uint8_t HasMemberId(uint32_t id) override;

    // 0x4a0dd0 / 0x4a0c50 (slots 38 / 40, ret 4): hands the argument to the text's slot 17 (assignment) and marks the family modified.
    void SetName(const void *text) override;
    void SetDescription(const void *text) override;

    // 0x49c5d0 (slot 62, ret 4): adds the id to the id set (when absent) and copies the set's size to mIdCount.
    void AddToIdSet(uint32_t id) override;
    // 0x49fed0 (slot 63): empties the id set; mIdCount becomes 0.
    void ClearIdSet() override;

    // 0x49f000 (slot 48, ret 4): whether the lot is non-zero and one of the family's two lot ids.
    uint8_t IsFamilyLot(uint32_t lot) override;
    // 0x49d590 (slot 49, ret 4): the unique id at +0x64; when it is still 0 and the flag is set, the framework object (0x1ae18d)
    // is asked to fill it (its slot 7).
    uint32_t GetUniqueId(uint8_t create) override;

    // 0x49e110 (slot 53): the household funds. When the family is the current lot's active family (singleton 0x10cc288,
    // slot 23 object of type 0x0bea28c2 whose slot 14 has this family's id) the live household (singleton slot 3) answers;
    // otherwise the stored +0x6c.
    int32_t GetFunds() override;
    // 0x4a0c80 (slot 54, ret 4): stores the funds; for the active family the live household is told first.
    void SetFunds(int32_t funds) override;
    // 0x49f4c0 (slot 59): the worth of the home lot (slot 42): 0 without a lot; for the active family the live household's
    // slot 40 (argument 1); otherwise what the lot manager reports (0 for a lot of type 9 or when it does not answer).
    uint32_t GetHomeLotWorth() override;
    // 0x49f480 (slot 60): the family registry's (singleton slot 9) entry for this family, or 0 without a singleton or registry.
    uint32_t GetRegistryEntry() override;
    // 0x49e0d0 (slot 61): when flag +0x79 is set, tells the neighbor manager (singleton slot 10) and clears it; then mIdCount
    // becomes the size of the id set.
    void FlushChanges() override;
    // 0x49e330 (slot 76, ret 8): when a member's neighbor (neighbor manager slot 17) has the short id, writes +0x60 to *out.
    void FindMemberLot(uint32_t shortId, uint32_t *out) override;
    // 0x4a0e60 (slot 30, ret 4): whether the id the object leads to (its slot 8, then 61, then 28) is a member id.
    uint8_t HasMemberObject(void *object) override;
    // 0x4a0e90 (slot 75): drops the family's three resources from the persist system's cache (its record key at +0x8c, the
    // text resource 0x53545223 and the record 0x46414d68), marks group 1 unloaded and clean, and resets the id to -1.
    void ForgetResources() override;
    // 0x49eed0 (slot 65): the counted record object at +0xa4, fetched on first use from the persist system (type 0x46414d68,
    // interface 0x7ab5bcbc) or else created through the framework.
    void *GetRecord() override;

    // 0x49f020 (slot 32, ret 8; the second argument is unused): a negative id makes the id -1 and empties both texts (AL 0).
    // Otherwise sets the id and loads the name and description (string table entries 1 and 2) from the text resource STR#
    // (0x53545223), or failing that FAMs (0x46414d73), instance id & 0x7fff; a load clears mModified. AL = one was found.
    uint8_t LoadTexts(int32_t id, uint32_t unused) override;
    // 0x49dfd0 (ret 0xc): fetches the member container (resource 0x6c4f359d, interface 0x6c4f3599, instance = the id, group
    // = record key word +0x90); when its version is `version` it gets mode 0x1d and is asked for `iid` into *out. AL = answered.
    uint8_t QueryMemberContainer(uint32_t iid, void **out, uint32_t version);
    // 0x49f1f0 / 0x49f320 (slots 33 / 78): loads a member group once (ids 1..31999 only): when the member container answers,
    // both maps of the group are emptied and the first is filled with the container's objects by key. AL = 1.
    uint8_t LoadFirstGroup() override;
    uint8_t LoadSecondGroup() override;
    // 0x49bfd0 / 0x49c900 (slots 11 / 18, ret 4), ids 1..31999 only, group loaded first: when the object answers interface
    // 0x0c560f33 (a member object), its key leaves the group's second map (marking it dirty) and, when the first map lacks the
    // key, a new member object (framework class 0x0c560f39) with that key goes into the first map (dirty). Otherwise an object
    // answering 0xac598ea8 (lot objects) is handed to 0x49c1a0 / 0x49cad0 (not replaced).
    void InsertObjectIntoGroup1(void *object) override;
    void InsertObjectIntoGroup2(void *object) override;
    // 0x49c6b0 (slot 17, ret 4): the same by key for group 2; a key of type 0xac598eac names lot objects whose member keys
    // are imported instead (ImportLotObjects' loop).
    void InsertKeyIntoGroup2(const FamilyKey *key) override;
    // 0x49bab0 (slot 10, ret 4): the group 1 twin; a lot-objects key goes through ImportLotObjectsIntoGroup1 (inlined there).
    void InsertKeyIntoGroup1(const FamilyKey *key) override;
    // 0x49c1a0 (ret 4): what group 1 imports from a lot-objects object (null or an invalid id: AL 0, else 1). Format 1:
    // for the key list masks 1, 2, 4 ... 0x1000 outside 0xc86 and the kinds 4, 0x10, 8, 0x20, every key becomes a new
    // member object for slot 11, except kind 0x20 keys: their lot part (persist system, interface 0x0c1fe23e) names a
    // source key whose resources (resource service slot 22) each have their key inserted through slot 10. Other formats:
    // every GUID name (slot 37) is parsed into a key and searched the same way with the object's slots 22 / 23 / 24.
    uint8_t ImportLotObjectsIntoGroup1(void *objects);
    // 0x49cad0 (ret 4): the group 2 twin (format 1 only: ImportLotObjects' loop through slot 18).
    uint8_t ImportLotObjectsIntoGroup2(void *objects);
    // 0x49cc70 (slot 9, ret 4): ImportLotObjects (slot 16) for group 1, through ImportLotObjectsIntoGroup1 (inlined there).
    void ImportLotIntoGroup1(uint32_t lot) override;
    // 0x49d150 (slot 16, ret 4): imports the member keys of a lot (singleton slot 24, then its slot 14 with the argument):
    // every key of the lot-objects resource {0xac598eac, group, 1, 0} becomes a member object handed to slot 18.
    void ImportLotObjects(uint32_t lot) override;
    // 0x49d950 (slot 24, ret 4): counts the members by flags: a member for which 0x54ab60(id, 0xba) answers counts when flag 2
    // is set (plus 0x54aa00(id) with flag 8), any other when flag 1 is set (plus 0x54aa00(id) with flag 4).
    uint32_t CountMembers(uint32_t flags) override;
    // 0x49e5c0 (slot 21, ret 4): CollectObjectsOfGroup1 (group 1 as well), keeping only the objects that answer interface
    // 0x0c1fe23e (their answer is released again).
    void CollectFilteredObjectsOfGroup1(ServiceRefVector *refVector) override;

    // 0x4a0610 / 0x4a0930 (slots 35 / 79, ret 4; src/ts/cTSFamilySave.cpp): saves a member group when it is dirty or `force`
    // is set (group loaded first). The member container (version 0x80 / 0x10) is made, keyed and added to the store with
    // the record's segment when missing; then every object of the group's first map is stored as a fresh member copy (its
    // key, resource key, the record group and the container key), and an object without an instance yet takes the copy's
    // resource key; every object of the second map in the family's record group is removed from the store. The second map
    // is emptied and the group is clean. AL = 1.
    uint8_t SaveFirstGroup(uint8_t force) override;
    uint8_t SaveSecondGroup(uint8_t force) override;
    // 0x4a0020 (slot 36): deletes the text (STR#) and record resources from the segments that hold them, then removes every
    // group 1 member that has a group from the store (the second map's members first take the record group), empties both
    // maps and removes the member container. AL = 1.
    uint8_t DeleteResources() override;
    // 0x4a03f0 (slot 34, ret 4; the argument is unused): saves the persist face, then (when modified) the name and
    // description into the STR# table (found or created), and the record. AL = everything saved; 0 for id -1, without a
    // persist system, or when the face does not save.
    uint8_t SaveTexts(uint32_t unused) override;

    // 0x49d5d0 (slot 25, ret 4; src/ts/cTSFamilyQueries.cpp): whether `other` may join this family under the tuning
    // limits maxTotalSims / maxTotalHumans / maxTotalPets (property service), from both families' CountMembers(1/4/2/8):
    // 0 fits, 2 / 3 / 1 too many humans / pets / sims (kinds 1 and 2), 4 / 5 / 6 the same with kinds 4 and 8 added, 7 this
    // family's lots +0x60 and +0x58 differ, 8 no other family, 9 no humans (kind 1) in either.
    uint32_t CheckMoveIn(cITSFamily *other) override;
    // 0x49e3b0 (slot 64, ret 4): FlushChanges' work (flag +0x79, neighbour manager), with the id set copied into `out`
    // (emptied first; its nodes come from the allocator service) before mIdCount is refreshed.
    void CopyIdSet(FamilyServiceIdSet *out) override;
    // 0x49e9f0 (slot 74, ret 4): the amount scaled by a percent from the family's lot (lot manager slot 38): 100, minus the
    // second value of the first 0x74d328a8 entry that has a non-zero one, plus the same of the 0xf4d3289f entries. When any
    // applied, the result is (max(percent, 0) / 100 * amount) truncated, at least 1; otherwise the amount.
    int32_t ScaleByLotModifiers(int32_t amount) override;
    // 0x49e6f0 (slot 15, ret 0x10): over `objects` (a copy; null: CollectObjectsOfGroup1), the part resource (0xebcf3e20)
    // of each (a member object's key through the persist system, or the object's own interface) that passes the category
    // and second masks, has flag bit 0, an age mask of 0 or one meeting `ageMask`, and is not custom content unless the
    // tuning value allowCustomContent (default true) allows it; counted by kind: 4 (else 0x10, else 8). The answer is
    // kind 0x10 count * kind 4 count + kind 8 count (tentative: tops x bottoms + full bodies).
    uint32_t CountOutfitCombinations(uint32_t categoryMask, uint32_t secondMask, uint32_t ageMask, ServiceRefVector *objects) override;

    // 0x4a0d70 / 0x49eec0 (slots 66 / 67), 0x4a0d40 / 0x49eeb0 (68 / 69), 0x4a0d10 / 0x49eea0 (70 / 71), 0x4a0df0 / 0x49f4b0
    // (72 / 73): set and read the four low flag bits of +0x88.
    void SetFlagBit0(uint8_t on) override;
    uint8_t GetFlagBit0() override;
    void SetFlagBit3(uint8_t on) override;
    uint8_t GetFlagBit3() override;
    void SetFlagBit1(uint8_t on) override;
    uint8_t GetFlagBit1() override;
    void SetFlagBit2(uint8_t on) override;
    uint8_t GetFlagBit2() override;

    // The +4 face, as the bodies hand it to other objects (no virtual call).
    cIGZPersistResource *PersistFace() { return this; }

    // The folded accessors (shared_vf_* bodies; ours now, never hooked).
    cIGZPersistResource *GetPersistFace() override;    // slot 31, 0x1c04e0: the +4 face (null for a null this)
    cRZString *GetName() override { return &mName; }   // slot 37, 0x23e5c0
    cRZString *GetDescription() override { return &mDescription; } // slot 39, 0x49d5c0
    uint32_t GetFamilyId() override { return mFamilyId; }          // slot 41, 0x16e970
    uint32_t GetHomeLot() override { return mHomeLot; }            // slot 42, 0x1dff70
    void SetHomeLot(uint32_t lot) override { mHomeLot = lot; }     // slot 43, 0x1fc130
    uint32_t GetBusinessLot() override { return mBusinessLot; }    // slot 44, 0x230120
    void SetBusinessLot(uint32_t lot) override { mBusinessLot = lot; } // slot 45, 0x230b70
    uint32_t GetVacationLot() override { return mVacationLot; }    // slot 46, 0x229b60
    void SetVacationLot(uint32_t lot) override { mVacationLot = lot; } // slot 47, 0x230ec0
    void SetUniqueId(uint32_t id) override { mUniqueId = id; }     // slot 50, 0x3bdbc0
    uint32_t GetField68() override { return mField68; }            // slot 51, 0x254bb0
    void SetField68(uint32_t value) override { mField68 = value; } // slot 52, 0x209e20
    uint32_t GetField70() override { return mField70; }            // slot 55, 0x1d3db0
    void SetField70(uint32_t value) override { mField70 = value; } // slot 56, 0x1d44f0
    uint32_t GetField0C() override { return mField0C; }            // slot 57, 0x145770
    void SetField0C(uint32_t value) override { mField0C = value; } // slot 58, 0x149c60

    // cIGZPersistResource (+4; these run with this = the face): the record key at +0x8c (the face's +0x88). Folded bodies.
    cGZPersistResourceKey GetKey() override;                     // slot 3, 0x49e6c0
    bool SetKey(const cGZPersistResourceKey &key) override;      // slot 4, 0x4a0da0: copies the key in; true
    uint32_t GetType() override { return mRecordKey[0]; }        // slot 5, 0x231570
    uint32_t GetGroup() override { return mRecordKey[1]; }       // slot 6, 0x22fa70
    uint32_t GetInstance() override { return mRecordKey[2]; }    // slot 7, 0x24bda0
    bool SetType(uint32_t type) override { mRecordKey[0] = type; return true; }             // slot 8, 0x4a0e20
    bool SetGroup(uint32_t group) override { mRecordKey[1] = group; return true; }          // slot 9, 0x479ca0
    bool SetInstance(uint32_t instance) override { mRecordKey[2] = instance; return true; } // slot 10, 0x4799c0
    uint32_t GetRefCount() override { return mRefCount; }       // slot 11, 0x147a30 (the face's +4)

    // +0x00 / +0x04: the two vtable pointers. The members with constructors sit in anonymous unions: the constructor and
    // destructor bodies build and destroy them in the original's order (the texts are still hook-shaped cRZStrings).
    uint32_t mRefCount;                // +0x08
    uint32_t mField0C;                 // +0x0c: starts at 1; persisted last-but-one (versions above 0x4e)
    uint32_t mIdCount;                 // +0x10: mirrors the size of mIdSet after the set operations
    union { cRZString mName; };        // +0x14: string table entry 1 of the family's text resource
    union { cRZString mDescription; }; // +0x34: string table entry 2
    uint32_t mFamilyId;               // +0x54: 1..32000 for a real family; also the instance of its resources
    uint32_t mHomeLot;                 // +0x58 (tentative)
    uint32_t mVacationLot;             // +0x5c (tentative)
    uint32_t mBusinessLot;             // +0x60 (tentative)
    uint32_t mUniqueId;                // +0x64: filled lazily from the framework (GetUniqueId)
    uint32_t mField68;                 // +0x68: starts at 0xffffffff
    int32_t mFunds;                    // +0x6c: the household's money
    uint32_t mField70;                 // +0x70: persisted last
    uint8_t mModified;                 // +0x74: starts at 1
    uint8_t mGroup1Dirty;              // +0x75
    uint8_t mGroup1Loaded;             // +0x76
    uint8_t mGroup2Dirty;              // +0x77
    uint8_t mGroup2Loaded;             // +0x78
    uint8_t mFlag79;                   // +0x79: starts at 1
    uint8_t mPadding7A[2];             // +0x7a: the constructor leaves these untouched
    union { std::vector<uint32_t> mMemberIds; }; // +0x7c
    uint32_t mFlags;                   // +0x88: bits 0..3 have accessors (slots 66..73)
    uint32_t mRecordKey[4];            // +0x8c: zeroed by the constructor; the persist face's key
    union { FamilyIdSet mIdSet; };     // +0x9c
    void *mRecord;                     // +0xa4: counted persist-resource object (released by the destructor)
    union { FamilyObjectMap mGroup1First; };  // +0xa8
    union { FamilyObjectMap mGroup1Second; }; // +0xb0
    union { FamilyObjectMap mGroup2First; };  // +0xb8
    union { FamilyObjectMap mGroup2Second; }; // +0xc0
};
static_assert(sizeof(cTSFamily) == 0xc8);
static_assert(offsetof(cTSFamily, mRefCount) == 0x08);
static_assert(offsetof(cTSFamily, mIdCount) == 0x10);
static_assert(offsetof(cTSFamily, mName) == 0x14);
static_assert(offsetof(cTSFamily, mDescription) == 0x34);
static_assert(offsetof(cTSFamily, mFamilyId) == 0x54);
static_assert(offsetof(cTSFamily, mUniqueId) == 0x64);
static_assert(offsetof(cTSFamily, mFunds) == 0x6c);
static_assert(offsetof(cTSFamily, mModified) == 0x74);
static_assert(offsetof(cTSFamily, mFlag79) == 0x79);
static_assert(offsetof(cTSFamily, mMemberIds) == 0x7c);
static_assert(offsetof(cTSFamily, mFlags) == 0x88);
static_assert(offsetof(cTSFamily, mRecordKey) == 0x8c);
static_assert(offsetof(cTSFamily, mIdSet) == 0x9c);
static_assert(offsetof(cTSFamily, mRecord) == 0xa4);
static_assert(offsetof(cTSFamily, mGroup1First) == 0xa8);
static_assert(offsetof(cTSFamily, mGroup2Second) == 0xc0);

// Where the persist face sits (RTTI; base offsets are not constant expressions: the tests check it).
namespace family {
constexpr uint32_t kPersistFace = 0x4;
} // namespace family

// The RVA hooks' entry points (src/hook/tables/cTSFamily.cpp). Every family the game creates carries our vtables (the Create
// and constructor hooks), so these run only where the game calls an old entry point directly or holds a family built with its
// own vtables. Original calling convention (this in ecx), our functions called without virtual dispatch, bodies inlined. Each
// is defined in the file of the body it forwards to.
struct cTSFamilyAdapters {
    using Self = cTSFamily;
    static Self *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static uint32_t __fastcall GetGZCLSID(Self *self, void *);
    static bool __fastcall Write(Self *self, void *, ifaces::cIGZOStream *stream);
    static bool __fastcall Read(Self *self, void *, ifaces::cIGZIStream *stream);
    static uint8_t __fastcall SetFamilyId(Self *self, void *, uint32_t id);
    static void __fastcall AddMemberId(Self *self, void *, uint32_t id);
    static void __fastcall RemoveMemberId(Self *self, void *, uint32_t id);
    static uint8_t __fastcall HasObjectInGroup1(Self *self, void *, const FamilyKey *key);
    static uint8_t __fastcall HasObjectInGroup2(Self *self, void *, const FamilyKey *key);
    static void __fastcall AddObjectToGroup1(Self *self, void *, void *object);
    static void __fastcall AddObjectToGroup2(Self *self, void *, void *object);
    static void __fastcall CollectObjectsOfGroup1(Self *self, void *, ServiceRefVector *refVector);
    static void __fastcall SetFlag79(Self *self, void *, uint8_t value);
    static uint32_t *__fastcall GetMemberIdSlot(Self *self, void *, int32_t index);
    static uint32_t __fastcall GetMemberId(Self *self, void *, int32_t index);
    static uint32_t *__fastcall FindMemberId(Self *self, void *, uint32_t id);
    static uint8_t __fastcall HasMemberId(Self *self, void *, uint32_t id);
    static void __fastcall SetName(Self *self, void *, const void *text);
    static void __fastcall SetDescription(Self *self, void *, const void *text);
    static void __fastcall AddToIdSet(Self *self, void *, uint32_t id);
    static void __fastcall ClearIdSet(Self *self, void *);
    static uint8_t __fastcall IsFamilyLot(Self *self, void *, uint32_t lot);
    static uint32_t __fastcall GetUniqueId(Self *self, void *, uint8_t create);
    static void __fastcall SetFlagBit0(Self *self, void *, uint8_t on);
    static uint8_t __fastcall GetFlagBit0(Self *self, void *);
    static void __fastcall SetFlagBit3(Self *self, void *, uint8_t on);
    static uint8_t __fastcall GetFlagBit3(Self *self, void *);
    static void __fastcall SetFlagBit1(Self *self, void *, uint8_t on);
    static uint8_t __fastcall GetFlagBit1(Self *self, void *);
    static void __fastcall SetFlagBit2(Self *self, void *, uint8_t on);
    static uint8_t __fastcall GetFlagBit2(Self *self, void *);
    // src/ts/cTSFamilyServices.cpp
    static int32_t __fastcall GetFunds(Self *self, void *);
    static void __fastcall SetFunds(Self *self, void *, int32_t funds);
    static uint32_t __fastcall GetHomeLotWorth(Self *self, void *);
    static uint32_t __fastcall GetRegistryEntry(Self *self, void *);
    static void __fastcall FlushChanges(Self *self, void *);
    static void __fastcall FindMemberLot(Self *self, void *, uint32_t shortId, uint32_t *out);
    static uint8_t __fastcall HasMemberObject(Self *self, void *, void *object);
    static void __fastcall ForgetResources(Self *self, void *);
    static void *__fastcall GetRecord(Self *self, void *);
    static uint8_t __fastcall LoadTexts(Self *self, void *, int32_t id, uint32_t unused);
    static uint8_t __fastcall QueryMemberContainer(Self *self, void *, uint32_t iid, void **out, uint32_t version);
    static uint8_t __fastcall LoadFirstGroup(Self *self, void *);
    static uint8_t __fastcall LoadSecondGroup(Self *self, void *);
    static void __fastcall InsertObjectIntoGroup1(Self *self, void *, void *object);
    static void __fastcall InsertObjectIntoGroup2(Self *self, void *, void *object);
    static void __fastcall InsertKeyIntoGroup1(Self *self, void *, const FamilyKey *key);
    static void __fastcall InsertKeyIntoGroup2(Self *self, void *, const FamilyKey *key);
    static uint8_t __fastcall ImportLotObjectsIntoGroup1(Self *self, void *, void *objects);
    static uint8_t __fastcall ImportLotObjectsIntoGroup2(Self *self, void *, void *objects);
    static void __fastcall ImportLotIntoGroup1(Self *self, void *, uint32_t lot);
    static void __fastcall ImportLotObjects(Self *self, void *, uint32_t lot);
    static uint32_t __fastcall CountMembers(Self *self, void *, uint32_t flags);
    static void __fastcall CollectFilteredObjectsOfGroup1(Self *self, void *, ServiceRefVector *refVector);
    // src/ts/cTSFamilySave.cpp
    static uint8_t __fastcall SaveFirstGroup(Self *self, void *, uint8_t force);
    static uint8_t __fastcall SaveSecondGroup(Self *self, void *, uint8_t force);
    static uint8_t __fastcall DeleteResources(Self *self, void *);
    static uint8_t __fastcall SaveTexts(Self *self, void *, uint32_t unused);
    // src/ts/cTSFamilyQueries.cpp
    static uint32_t __fastcall CheckMoveIn(Self *self, void *, cITSFamily *other);
    static void __fastcall CopyIdSet(Self *self, void *, FamilyServiceIdSet *out);
    static int32_t __fastcall ScaleByLotModifiers(Self *self, void *, int32_t amount);
    static uint32_t __fastcall CountOutfitCombinations(Self *self, void *, uint32_t categoryMask, uint32_t secondMask,
                                                       uint32_t ageMask, ServiceRefVector *objects);
};
