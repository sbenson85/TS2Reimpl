#pragma once
#include "gz/cIGZDBPFIndex.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

// cGZDBPFIndexStatic: the index of a small read-only DBPF .package file (cGZDBSegmentPackedFile 0x9af590 builds it with
// `new`, the constructor 0x9a6da0 and slot 20 for fewer than 0x40 records, or when the compressed index fails; 0x9ae5c0
// builds it inline from a cached index and slot 18). RTTI: one face, cIGZDBPFIndex, vtable 0xe5fedc (24 slots). 0x20 bytes.
// RVAs are 1.18.0.187.
//
// The entries are full 0x1c-byte cGZDBPFIndexEntry records, ordered by hash bucket: a key's bucket is
// (type + group + instance) % mBucketCount, and mBuckets[b] points at the first entry of bucket b or later (mBucketCount
// + 1 pointers, the last one the entries' end), so bucket b's entries are [mBuckets[b], mBuckets[b + 1]). The bucket count
// is (count / 6) * 6 + 1.
//
// Standalone shape (docs/standalone-shape.md): the compiler builds the vtable; the RVA hooks point at
// cGZDBPFIndexStaticAdapters. The constructor hook makes every index 0x9af590 builds ours; 0x9ae5c0 (a cGZDBSegmentPackedFile
// member, game code) still builds indexes inline with the game's vtable, which the slot adapters serve.
class cGZDBPFIndexStatic : public cIGZDBPFIndex {
public:
    static constexpr uint32_t kVTable = 0xe5fedc;
    static constexpr uint32_t kFlag = 0x80000000u;          // bit 31 of a memory size
    static constexpr uint32_t kDirectoryType = 0xe86b1eefu; // DIR: the compression directory, left out of key lists

    using Entry = cGZDBPFIndexEntry;

    cGZDBPFIndexStatic();                                       // 0x9a6da0
    ~cGZDBPFIndexStatic() override;                             // slot 0, 0x9a7860 (deleting)

    bool IsModifiable() override;                               // slot 1, folded 0x147480: false
    bool IsEmpty() override;                                    // slot 2, folded 0x9ad640: no entries
    uint32_t GetCount() override;                               // slot 3, 0x9acb50
    uint32_t GetTotalSize() override;                           // slot 4, 0x9acc30
    uint32_t GetTotalMemSize() override;                        // slot 5, 0x9accd0: the flag bit left out
    uint32_t GetDataEnd() override;                             // slot 6, 0x9abc40
    uint32_t GetSerializedSize() override;                      // slot 7, 0x9ac730
    void CollectTypes(std::set<uint32_t> *types) override;      // slot 8, 0x9ace20
    void CollectGroups(std::set<uint32_t> *groups) override;    // slot 9, 0x9abfa0
    uint32_t GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter) override; // slot 10, 0x9ac4e0
    // Slot 11, folded 0x1ad030: FindRecordForWrite through the vtable (a tail jump).
    cGZDBPFIndexRecord *FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                   uint32_t resourceZero) override;
    // Slot 12, 0x9ab430: the record of `key` in its bucket (the first exact match), or else, when the low byte of
    // `anyResource` is set and the key's resource is 0, the last entry of the bucket with the key's type, group and
    // instance; when the low byte of `resourceZero` is set and the key's resource is not 0, the last such entry with
    // resource 0. Null when none. The record is the entry's last three words (offset, size, memory size), in place.
    cGZDBPFIndexRecord *FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                           uint32_t resourceZero) override;
    void AddRecord(const cGZPersistResourceKey *key, const cGZDBPFIndexRecord *record) override; // slot 13, folded 0x1b9a50
    bool RemoveRecord(const cGZPersistResourceKey *key, cGZDBPFIndexRecord *removed, uint32_t anyResource,
                      uint32_t resourceZero) override;                                // slot 14, folded 0x147110: false
    // Slot 15, folded 0x9afdb0 (shared with cGZDBPFIndexModifiable): FindRecordForWrite(key, 0, 0) through the vtable, and
    // the memory size's low 31 bits replaced (the flag bit kept).
    void SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize) override;
    bool AllEntriesWithin(uint32_t start, uint32_t end) override;                     // slot 16, 0x9b0b10
    bool IsRangeFree(uint32_t start, uint32_t length) override;                       // slot 17, 0x9b0c20
    bool SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap) override; // slot 18, 0x9ae410
    void CopyEntries(cGZDBPFIndexEntry *out, void *keyMap) override;                  // slot 19, 0x9b10c0
    bool SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap,
                             uint32_t minorVersion) override;                         // slot 20, 0x9aef50
    void WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion) override; // slot 21, 0x9b16f0
    void WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap,
                                   uint32_t minorVersion) override;                   // slot 22, 0x9b1430
    void SetFlagOnAll() override;                                                     // slot 23, folded 0x140fe0: nothing

    // 0x9ad220 (no stack arguments): sorts the entries by bucket (std::sort) after setting the bucket count from the
    // entry count, and fills mBuckets.
    void BuildBuckets();

    // The bucket of a key's (type, group, instance).
    uint32_t BucketOf(uint32_t type, uint32_t group, uint32_t instance) const;

    std::vector<Entry> mEntries;   // +0x04
    std::vector<Entry *> mBuckets; // +0x10: mBucketCount + 1 pointers into mEntries once built
    uint32_t mBucketCount;         // +0x1c: not set by the constructor
};

static_assert(offsetof(cGZDBPFIndexStatic, mEntries) == 0x04, "cGZDBPFIndexStatic: entries at +4");
static_assert(offsetof(cGZDBPFIndexStatic, mBuckets) == 0x10, "cGZDBPFIndexStatic: buckets at +0x10");
static_assert(offsetof(cGZDBPFIndexStatic, mBucketCount) == 0x1c, "cGZDBPFIndexStatic: bucket count at +0x1c");
static_assert(sizeof(cGZDBPFIndexStatic) == 0x20, "cGZDBPFIndexStatic: 0x20 bytes");

// The RVA hooks' entry points (src/hook/tables/cGZDBPFIndexStatic.cpp): the constructor's (0x9af590 calls it) and the slots
// of indexes 0x9ae5c0 builds with the game's vtable. Original calling convention, our functions called without virtual
// dispatch, bodies inlined.
struct cGZDBPFIndexStaticAdapters {
    using Self = cGZDBPFIndexStatic;
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
    static cGZDBPFIndexRecord *__fastcall FindRecordForWrite(Self *self, void *, const Key *key, uint32_t anyResource,
                                                             uint32_t resourceZero);
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
