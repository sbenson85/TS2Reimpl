#pragma once
#include "gz/cGZDBSegmentPackedFile.h"    // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey

#include <cstdint>
#include <set>
#include <vector>

// cIGZDBPFIndex: the record index of a DBPF .package file (cGZDBSegmentPackedFile +0x30). Three implementations:
// cGZDBPFIndexModifiable (a list, for packages being written), cGZDBPFIndexStatic (an array of full entries, small
// packages) and cGZDBPFIndexCompressed (entries grouped by type and group, 16 bytes each, for 0x40 entries and more).
// The interface has no QueryInterface: slot 0 is the deleting destructor. Slot order and `ret N` from the three vtables
// (0xe5ff40, 0xe5fedc, 0xe5fe78); the generated caller view is ifaces::cIGZDBPFIndex (db/methods/cGZDBSegmentPackedFile.jsonl).
// The implementable interface: only cGZDBPFIndexCompressed derives from it so far.

// An index entry in the full form slots 18-21 exchange (0x1c bytes): the key, where the record is in the file, its
// stored size and its size in memory. Bit 31 of memSize is a flag (set on every entry an index hands out).
struct cGZDBPFIndexEntry {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
    uint32_t offset;
    uint32_t size;
    uint32_t memSize;
};
static_assert(sizeof(cGZDBPFIndexEntry) == 0x1c, "cGZDBPFIndexEntry is 0x1c bytes");

// What FindRecord returns: the record's place, stored size and memory size (bit 31 set).
struct cGZDBPFIndexRecord {
    uint32_t offset;
    uint32_t size;
    uint32_t memSize;
};
static_assert(sizeof(cGZDBPFIndexRecord) == 0xc, "cGZDBPFIndexRecord is 0xc bytes");

// The key list GetResourceKeyList appends to: its blocks come from the allocator service (the packed file frees them so).
using cGZDBPFResourceKeyList = std::vector<cGZPersistResourceKey, GZServiceAllocator<cGZPersistResourceKey>>;
static_assert(sizeof(cGZDBPFResourceKeyList) == 0xc, "cGZDBPFResourceKeyList layout");

// `keyMap` is the segment's namespace (cGZDBSegmentPackedFile's +0x18): slot 3 maps a key from the file's form to the
// caller's, slot 4 back; both take (in, out) and are called with the same key twice. `filter` is a
// cIGZPersistResourceKeyFilter (slot 3: bool IsKeyIncluded(const key *)).
class cIGZDBPFIndex {
public:
    // Out of line (src/gz/cGZDBPFIndexCompressed.cpp), so a destroyed index carries this interface's vtable, as the
    // original's destructors leave it (an inline empty one lets the compiler drop that store).
    virtual ~cIGZDBPFIndex();                                                                   // 0 (ret 4)
    virtual bool IsModifiable() = 0;                                                            // 1
    virtual bool IsEmpty() = 0;                                                                 // 2
    virtual uint32_t GetCount() = 0;                                                            // 3
    virtual uint32_t GetTotalSize() = 0;                                                        // 4
    virtual uint32_t GetTotalMemSize() = 0;                                                     // 5
    virtual uint32_t GetDataEnd() = 0;                                                          // 6
    virtual uint32_t GetSerializedSize() = 0;                                                   // 7
    virtual void CollectTypes(std::set<uint32_t> *types) = 0;                                   // 8 (ret 4)
    virtual void CollectGroups(std::set<uint32_t> *groups) = 0;                                 // 9 (ret 4)
    virtual uint32_t GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter) = 0;        // 10 (ret 8)
    // Flags as dwords; the callee reads their low bytes.
    virtual cGZDBPFIndexRecord *FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                           uint32_t resourceZero) = 0;                          // 11 (ret 0xc)
    virtual cGZDBPFIndexRecord *FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                   uint32_t resourceZero) = 0;                  // 12 (ret 0xc)
    virtual void AddRecord(const cGZPersistResourceKey *key, const cGZDBPFIndexRecord *record) = 0; // 13 (ret 8)
    virtual bool RemoveRecord(const cGZPersistResourceKey *key, cGZDBPFIndexRecord *removed, uint32_t anyResource,
                              uint32_t resourceZero) = 0;                                       // 14 (ret 0x10)
    virtual void SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize) = 0;      // 15 (ret 8)
    virtual bool AllEntriesWithin(uint32_t start, uint32_t end) = 0;                            // 16 (ret 8)
    virtual bool IsRangeFree(uint32_t start, uint32_t length) = 0;                              // 17 (ret 8)
    virtual bool SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap) = 0; // 18 (ret 0xc)
    virtual void CopyEntries(cGZDBPFIndexEntry *out, void *keyMap) = 0;                         // 19 (ret 8)
    virtual bool SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap,
                                     uint32_t minorVersion) = 0;                                // 20 (ret 0x10)
    virtual void WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion) = 0; // 21 (ret 0xc)
    // The caller view names this slot WriteIndex; what it writes is the DIR record (the compressed records' sizes).
    virtual void WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap,
                                           uint32_t minorVersion) = 0;                          // 22 (ret 0xc)
    virtual void SetFlagOnAll() = 0;                                                            // 23
};
