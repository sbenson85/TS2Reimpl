#pragma once
#include "gz/cIGZDBPFIndex.h"
#include "rz/GameAllocator.h" // the map's nodes: RZMemory::Malloc / Free

#include <cstddef>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

// cGZDBPFIndexModifiable: the index of a DBPF .package file being written (cGZDBSegmentPackedFile's EnsureStore 0x9ad750 and
// Open 0x9ad830, ours, build it with `new`; the game's 0x9af590 through the constructor 0x9a6cf0). RTTI: one face,
// cIGZDBPFIndex, vtable 0xe5ff40 (24 slots). 0x24 bytes: the vtable and an unordered_map from keys to records whose nodes
// and buckets come from RZMalloc. RVAs are 1.18.0.187.
//
// The map hashes a key by its instance alone (the game's instantiation; the bucket is instance & mask) and compares all
// four words. Its nodes are 0x24 bytes (next, prev, key, record) and its list order is the order every walk below
// follows. Records handed in by SetEntries / SetEntriesFromIndex carry bit 31 of the memory size (kFlag) set.
//
// Standalone shape (docs/standalone-shape.md): the compiler builds the vtable. The constructor hook (0x9a6cf0) is the
// class's switch: the vtable 0xe5ff40 is stored nowhere else, so the slots need no adapters (their entries are "in effect
// through callers", trapped on that hook); the byte-matched slots keep theirs (matchcheck ties the RVA to our member).
class cGZDBPFIndexModifiable : public cIGZDBPFIndex {
public:
    static constexpr uint32_t kVTable = 0xe5ff40;
    static constexpr uint32_t kFlag = 0x80000000u;          // bit 31 of a memory size
    static constexpr uint32_t kDirectoryType = 0xe86b1eefu; // DIR: the compression directory, left out of key lists

    using Key = cGZPersistResourceKey;
    using Record = cGZDBPFIndexRecord;
    struct KeyHash {
        size_t operator()(const Key &key) const { return key.instance; }
    };
    struct KeyEqual {
        bool operator()(const Key &a, const Key &b) const
        {
            return a.type == b.type && a.group == b.group && a.instance == b.instance && a.resource == b.resource;
        }
    };
    using Map = std::unordered_map<Key, Record, KeyHash, KeyEqual, GameAllocator<std::pair<const Key, Record>>>;

    cGZDBPFIndexModifiable();                                   // 0x9a6cf0
    ~cGZDBPFIndexModifiable() override;                         // slot 0, 0x9a7830 (deleting; the map's destructor 0x9a7460)

    bool IsModifiable() override;                               // slot 1, folded 0x144910: true
    bool IsEmpty() override;                                    // slot 2, folded 0x744eb0: no records
    uint32_t GetCount() override;                               // slot 3, folded 0x145770
    uint32_t GetTotalSize() override;                           // slot 4, 0x9acc10
    uint32_t GetTotalMemSize() override;                        // slot 5, 0x9acca0: the flag bit left out
    uint32_t GetDataEnd() override;                             // slot 6, 0x9abc10
    uint32_t GetSerializedSize() override;                      // slot 7, 0x9ac720: 0x24 + 0x20 per record
    void CollectTypes(std::set<uint32_t> *types) override;      // slot 8, 0x9acd90
    void CollectGroups(std::set<uint32_t> *groups) override;    // slot 9, 0x9abf10
    uint32_t GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter) override; // slot 10, 0x9ac300
    // Slots 11 and 12, both 0x9ab220: the record of `key` (the map's find), or else, when the low byte of `anyResource` is set
    // and the key's resource is 0, the first record in list order with the key's type, group and instance; when the low
    // byte of `resourceZero` is set and the key's resource is not 0, the first such record with resource 0. Null when none.
    cGZDBPFIndexRecord *FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                   uint32_t resourceZero) override;
    cGZDBPFIndexRecord *FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                           uint32_t resourceZero) override;
    // Slot 13, 0x9a7cd0: inserts (key, record) unless the key is present (the record kept then).
    void AddRecord(const cGZPersistResourceKey *key, const cGZDBPFIndexRecord *record) override;
    // Slot 14, 0x9a9320: finds as FindRecord; copies the record to `removed` and erases it. False when none.
    bool RemoveRecord(const cGZPersistResourceKey *key, cGZDBPFIndexRecord *removed, uint32_t anyResource,
                      uint32_t resourceZero) override;
    // Slot 15, folded 0x9afdb0 (shared with cGZDBPFIndexStatic): FindRecordForWrite(key, 0, 0) through the vtable, and the
    // memory size's low 31 bits replaced (the flag bit kept).
    void SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize) override;
    bool AllEntriesWithin(uint32_t start, uint32_t end) override;                     // slot 16, 0x9b0ac0
    bool IsRangeFree(uint32_t start, uint32_t length) override;                       // slot 17, 0x9b0bd0
    // Slot 18, 0x9ae1d0: adds each entry (key through the key map's slot 4, memory size flagged) unless its key is present.
    // The map is not cleared first.
    bool SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap) override;
    void CopyEntries(cGZDBPFIndexEntry *out, void *keyMap) override;                  // slot 19, 0x9b1030
    bool SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap,
                             uint32_t minorVersion) override;                         // slot 20, 0x9aece0
    void WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion) override; // slot 21, 0x9b1630
    void WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap,
                                   uint32_t minorVersion) override;                   // slot 22, 0x9b1320
    void SetFlagOnAll() override;                                                     // slot 23, 0x9ad810

    // What slots 11, 12 and 14 look up: the map's node for `key`, or the fallback search; end() when none.
    Map::iterator Find(const cGZPersistResourceKey *key, uint8_t anyResource, uint8_t resourceZero);
    // Slots 18 and 20: adds (key, record) unless the key is present.
    void Add(const cGZPersistResourceKey &key, const cGZDBPFIndexRecord &record);

    Map mMap; // +0x04: max load factor +4, list head +8, size +0xc, buckets +0x10..+0x18, mask +0x1c, bucket count +0x20
};

static_assert(sizeof(cGZDBPFIndexModifiable::Map) == 0x20, "the map is 0x20 bytes");
static_assert(offsetof(cGZDBPFIndexModifiable, mMap) == 0x04, "cGZDBPFIndexModifiable: map at +4");
static_assert(sizeof(cGZDBPFIndexModifiable) == 0x24, "cGZDBPFIndexModifiable: 0x24 bytes");

// The RVA hooks' entry points (src/hook/tables/cGZDBPFIndexModifiable.cpp): the constructor (called by the game's 0x9af590)
// and the byte-matched slots. Original calling convention, our functions called without virtual dispatch, bodies inlined.
struct cGZDBPFIndexModifiableAdapters {
    using Self = cGZDBPFIndexModifiable;
    static Self *__fastcall Construct(Self *memory, void *);
    static uint32_t __fastcall GetTotalSize(Self *self, void *);
    static uint32_t __fastcall GetSerializedSize(Self *self, void *);
};
