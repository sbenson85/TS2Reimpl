#pragma once
#include "gz/cIGZDBPFIndex.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

// cGZDBPFIndexCompressed: the index of a DBPF .package file with 0x40 records or more (cGZDBSegmentPackedFile 0x9af590 builds
// it with `new`, the constructor 0x9a6ca0 and slot 20 from the file's index words, and falls back to cGZDBPFIndexStatic
// when that fails; 0x9ae5c0 builds it inline from a cached index and slot 18). RTTI: one face, cIGZDBPFIndex, vtable
// 0xe5fe78 (24 slots). 0x38 bytes. RVAs are 1.18.0.187.
//
// Records are grouped by (type, group): one Block per pair, its Entries contiguous in mEntries and sorted by (resource,
// instance), so an entry needs only 16 bytes. The blocks are also an open hash table of mBlockCount buckets: a key's
// bucket is (type + group) % mBlockCount, and collisions chain through Block::mNext (-1 ends a chain). mBlocks holds one
// more block than mBlockCount, whose mFirstEntry is the entry count (so block i's entries end at block i + 1's first).
// Sizes and memory sizes that do not fit 16 bits go to mRecords: the entry's size is then kLargeSize and its mExtra the
// record's index.
//
// Standalone shape (docs/standalone-shape.md): the compiler builds the vtable; the RVA hooks point at
// cGZDBPFIndexCompressedAdapters. The constructor hook makes every index 0x9af590 builds ours; 0x9ae5c0 (a
// cGZDBSegmentPackedFile member, game code) still builds indexes inline with the game's vtable, which the slot adapters serve.
class cGZDBPFIndexCompressed : public cIGZDBPFIndex {
public:
    static constexpr uint32_t kVTable = 0xe5fe78;
    static constexpr uint16_t kLargeSize = 0xffff;       // Entry::mSize when the sizes are in mRecords
    static constexpr uint32_t kFlag = 0x80000000u;       // bit 31 of a memory size
    static constexpr uint32_t kDirectoryType = 0xe86b1eefu; // DIR: the compression directory, left out of key lists

    struct Entry {
        uint32_t mInstance;  // +0
        uint32_t mResource;  // +4 (instance high)
        uint32_t mStart;     // +8: offset in the file
        uint16_t mSize;      // +0xc: stored size, or kLargeSize
        uint16_t mExtra;     // +0xe: memory size, or the index in mRecords
    };
    struct Record {
        uint32_t mSize;      // +0: stored size
        uint32_t mMemSize;   // +4: memory size (bit 31 as given)
    };
    struct Block {
        uint32_t mType;       // +0
        uint32_t mGroup;      // +4
        uint32_t mFirstEntry; // +8: index in mEntries
        int32_t mNext;        // +0xc: next block of the same hash bucket, -1 at the end
    };

    cGZDBPFIndexCompressed();                                   // 0x9a6ca0
    ~cGZDBPFIndexCompressed() override;                         // slot 0, 0x9a7720 (deleting)

    bool IsModifiable() override;                               // slot 1, folded 0x147480: false
    bool IsEmpty() override;                                    // slot 2, folded 0x9ad640: no entries
    uint32_t GetCount() override;                               // slot 3, 0x9acb40
    uint32_t GetTotalSize() override;                           // slot 4, 0x9acbc0
    uint32_t GetTotalMemSize() override;                        // slot 5, 0x9acc50
    uint32_t GetDataEnd() override;                             // slot 6, 0x9abbc0
    uint32_t GetSerializedSize() override;                      // slot 7, 0x9ac6f0
    void CollectTypes(std::set<uint32_t> *types) override;      // slot 8, 0x9acd00
    void CollectGroups(std::set<uint32_t> *groups) override;    // slot 9, 0x9abe70
    uint32_t GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter) override; // slot 10, 0x9ac040
    cGZDBPFIndexRecord *FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                   uint32_t resourceZero) override;                   // slot 11, 0x9ab1a0
    cGZDBPFIndexRecord *FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                           uint32_t resourceZero) override;           // slot 12, folded 0x18a5c0: null
    void AddRecord(const cGZPersistResourceKey *key, const cGZDBPFIndexRecord *record) override; // slot 13, folded 0x1b9a50
    bool RemoveRecord(const cGZPersistResourceKey *key, cGZDBPFIndexRecord *removed, uint32_t anyResource,
                      uint32_t resourceZero) override;                                // slot 14, folded 0x147110: false
    void SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize) override; // slot 15, 0x9afd00
    bool AllEntriesWithin(uint32_t start, uint32_t end) override;                     // slot 16, 0x9b0a40
    bool IsRangeFree(uint32_t start, uint32_t length) override;                       // slot 17, 0x9b0b60
    bool SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap) override; // slot 18, 0x9adb20
    void CopyEntries(cGZDBPFIndexEntry *out, void *keyMap) override;                  // slot 19, 0x9b0e70
    bool SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap,
                             uint32_t minorVersion) override;                         // slot 20, 0x9aec00
    void WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion) override; // slot 21, 0x9b1540
    void WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap,
                                   uint32_t minorVersion) override;                   // slot 22, 0x9b11a0
    void SetFlagOnAll() override;                                                     // slot 23, folded 0x140fe0: nothing

    // 0x9ab2e0 (ret 0xc): the entry of `key`, or null. The key's block is found through the hash chain, its entry by a
    // binary search on (resource, instance). When there is none and the low byte of `anyResource` is set and the key's
    // resource is 0, the first entry of the block with the key's instance is taken; when the low byte of `resourceZero` is
    // set and the key's resource is not 0, the first one with the key's instance and resource 0.
    Entry *FindEntry(const cGZPersistResourceKey *key, uint8_t anyResource, uint8_t resourceZero);

    // The size and memory size of an entry, through mRecords for kLargeSize.
    uint32_t SizeOf(const Entry &entry) const;

    std::vector<Entry> mEntries;   // +0x04
    std::vector<Record> mRecords;  // +0x10
    std::vector<Block> mBlocks;    // +0x1c: mBlockCount + 1 blocks once built
    uint32_t mBlockCount;          // +0x28: not set by the constructor
    cGZDBPFIndexRecord mFound;     // +0x2c: what FindRecord returns (not set by the constructor)
};

static_assert(sizeof(cGZDBPFIndexCompressed::Entry) == 0x10, "Entry is 16 bytes");
static_assert(offsetof(cGZDBPFIndexCompressed::Entry, mStart) == 8, "Entry: start at +8");
static_assert(offsetof(cGZDBPFIndexCompressed::Entry, mSize) == 0xc, "Entry: size at +0xc");
static_assert(offsetof(cGZDBPFIndexCompressed::Entry, mExtra) == 0xe, "Entry: extra at +0xe");
static_assert(sizeof(cGZDBPFIndexCompressed::Record) == 8, "Record is 8 bytes");
static_assert(sizeof(cGZDBPFIndexCompressed::Block) == 0x10, "Block is 16 bytes");
static_assert(offsetof(cGZDBPFIndexCompressed, mEntries) == 0x04, "cGZDBPFIndexCompressed: entries at +4");
static_assert(offsetof(cGZDBPFIndexCompressed, mRecords) == 0x10, "cGZDBPFIndexCompressed: records at +0x10");
static_assert(offsetof(cGZDBPFIndexCompressed, mBlocks) == 0x1c, "cGZDBPFIndexCompressed: blocks at +0x1c");
static_assert(offsetof(cGZDBPFIndexCompressed, mBlockCount) == 0x28, "cGZDBPFIndexCompressed: block count at +0x28");
static_assert(offsetof(cGZDBPFIndexCompressed, mFound) == 0x2c, "cGZDBPFIndexCompressed: found record at +0x2c");
static_assert(sizeof(cGZDBPFIndexCompressed) == 0x38, "cGZDBPFIndexCompressed: 0x38 bytes");

// The RVA hooks' entry points (src/hook/tables/cGZDBPFIndexCompressed.cpp): reached by direct calls (none known besides the
// constructor's) and by indexes built with the game's vtable (0x9ae5c0's inline construction). Original calling convention,
// our functions called without virtual dispatch, bodies inlined.
struct cGZDBPFIndexCompressedAdapters {
    using Self = cGZDBPFIndexCompressed;
    using Key = cGZPersistResourceKey;
    static Self *__fastcall Construct(Self *memory, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static uint32_t __fastcall GetCount(Self *self, void *);
    static uint32_t __fastcall GetTotalSize(Self *self, void *);
    static uint32_t __fastcall GetTotalMemSize(Self *self, void *);
    static uint32_t __fastcall GetDataEnd(Self *self, void *);
    static uint32_t __fastcall GetSerializedSize(Self *self, void *);
    static void __fastcall CollectTypes(Self *self, void *, std::set<uint32_t> *types);
    static void __fastcall CollectGroups(Self *self, void *, std::set<uint32_t> *groups);
    static uint32_t __fastcall GetResourceKeyList(Self *self, void *, cGZDBPFResourceKeyList *list, void *filter);
    static cGZDBPFIndexRecord *__fastcall FindRecord(Self *self, void *, const Key *key, uint32_t anyResource,
                                                     uint32_t resourceZero);
    static void __fastcall SetRecordMemSize(Self *self, void *, const Key *key, uint32_t memSize);
    static bool __fastcall AllEntriesWithin(Self *self, void *, uint32_t start, uint32_t end);
    static bool __fastcall IsRangeFree(Self *self, void *, uint32_t start, uint32_t length);
    static bool __fastcall SetEntries(Self *self, void *, const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap);
    static void __fastcall CopyEntries(Self *self, void *, cGZDBPFIndexEntry *out, void *keyMap);
    static bool __fastcall SetEntriesFromIndex(Self *self, void *, const uint32_t *words, uint32_t count, void *keyMap,
                                               uint32_t minorVersion);
    static void __fastcall WriteIndexEntries(Self *self, void *, std::vector<uint32_t> *out, void *keyMap,
                                             uint32_t minorVersion);
    static void __fastcall WriteCompressionDirectory(Self *self, void *, std::vector<uint32_t> *out, void *keyMap,
                                                     uint32_t minorVersion);
};
