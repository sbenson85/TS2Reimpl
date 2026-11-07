#include "gz/cGZDBPFIndexCompressed.h"

#include "game/vtable.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <new>
#include <unordered_map>
#include <utility>

namespace {

using Self = cGZDBPFIndexCompressed;
using Entry = Self::Entry;
using Record = Self::Record;
using Block = Self::Block;

// The key map's slots 3 (file form -> caller's) and 4 (back), the filter's slot 3.
using KeyMapFn = void(__thiscall *)(void *, cGZPersistResourceKey *, cGZPersistResourceKey *);
using FilterFn = bool(__thiscall *)(void *, const cGZPersistResourceKey *);

void MapKey(void *keyMap, cGZPersistResourceKey *key)
{
    VirtualCall<KeyMapFn, 3>(keyMap, key, key);
}

// SetEntries' grouping: a hash map from (type, group) to the group's entries, hashed by type + group (the game's
// hash_map instantiation, MSVC 14.40's unordered_map: its list order decides which block a group gets).
struct TypeGroup {
    uint32_t type;
    uint32_t group;
    bool operator==(const TypeGroup &other) const { return type == other.type && group == other.group; }
};
struct TypeGroupHash {
    size_t operator()(const TypeGroup &key) const { return key.type + key.group; }
};
using GroupMap = std::unordered_map<TypeGroup, std::vector<Entry>, TypeGroupHash>;
using Group = GroupMap::value_type;

// Entries sort by (resource, instance), the order FindEntry's binary search expects.
bool EntryLess(const Entry &a, const Entry &b)
{
    if (a.mResource != b.mResource) return a.mResource < b.mResource;
    return a.mInstance < b.mInstance;
}

// The key the entry `e` of `block` stands for.
cGZPersistResourceKey KeyOf(const Block &block, const Entry &e)
{
    return cGZPersistResourceKey{block.mType, block.mGroup, e.mInstance, e.mResource};
}

} // namespace

cIGZDBPFIndex::~cIGZDBPFIndex() {}

cGZDBPFIndexCompressed::cGZDBPFIndexCompressed()
{
    // mBlockCount and mFound are left as they are, as the original's constructor does.
}

cGZDBPFIndexCompressed::~cGZDBPFIndexCompressed() = default;

uint32_t cGZDBPFIndexCompressed::SizeOf(const Entry &entry) const
{
    // The record index is not bounds-checked, as in the original.
    if (entry.mSize != kLargeSize) return entry.mSize;
    return mRecords.data()[entry.mExtra].mSize;
}

bool cGZDBPFIndexCompressed::IsModifiable()
{
    return false;
}

bool cGZDBPFIndexCompressed::IsEmpty()
{
    return mEntries.empty();
}

uint32_t cGZDBPFIndexCompressed::GetCount()
{
    return static_cast<uint32_t>(mEntries.size());
}

uint32_t cGZDBPFIndexCompressed::GetTotalSize()
{
    uint32_t sum = 0;
    for (const Entry &e : mEntries) {
        if (e.mSize != kLargeSize) sum += e.mSize;
    }
    for (const Record &r : mRecords) sum += r.mSize;
    return sum;
}

uint32_t cGZDBPFIndexCompressed::GetTotalMemSize()
{
    // The flag bit of a record's memory size is summed as it is.
    uint32_t sum = 0;
    for (const Entry &e : mEntries) {
        if (e.mSize != kLargeSize) sum += e.mExtra;
    }
    for (const Record &r : mRecords) sum += r.mMemSize;
    return sum;
}

uint32_t cGZDBPFIndexCompressed::GetDataEnd()
{
    // The larger of the running value and each entry's end (cmovae on an unsigned compare).
    uint32_t best = 0;
    for (const Entry &e : mEntries) {
        uint32_t end = e.mStart + SizeOf(e);
        if (best > end) end = best;
        best = end;
    }
    return best;
}

uint32_t cGZDBPFIndexCompressed::GetSerializedSize()
{
    return sizeof(cGZDBPFIndexCompressed) + sizeof(Record) * static_cast<uint32_t>(mRecords.size()) +
           sizeof(Entry) * (static_cast<uint32_t>(mEntries.size()) + static_cast<uint32_t>(mBlocks.size()));
}

void cGZDBPFIndexCompressed::CollectTypes(std::set<uint32_t> *types)
{
    // Every block of the vector, the end block (type 0) included.
    for (const Block &b : mBlocks) types->insert(b.mType);
}

void cGZDBPFIndexCompressed::CollectGroups(std::set<uint32_t> *groups)
{
    for (const Block &b : mBlocks) groups->insert(b.mGroup);
}

uint32_t cGZDBPFIndexCompressed::GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter)
{
    std::vector<std::pair<const Entry *, const Block *>> found;
    uint32_t count = 0;
    if (filter == nullptr) {
        const size_t entries = mEntries.size();
        if (entries != 0) found.reserve(entries);
    }
    for (uint32_t i = 0; i < mBlockCount; ++i) {
        const Block &block = mBlocks.data()[i];
        if (block.mType == kDirectoryType) continue;
        const Entry *e = mEntries.data() + block.mFirstEntry;
        const Entry *end = mEntries.data() + mBlocks.data()[i + 1].mFirstEntry;
        for (; e != end; ++e) {
            if (filter != nullptr) {
                const cGZPersistResourceKey key = KeyOf(block, *e);
                if (!VirtualCall<FilterFn, 3>(filter, &key)) continue;
            }
            if (list != nullptr) found.push_back({e, &block});
            ++count;
        }
    }
    if (list != nullptr) {
        // In file order.
        std::sort(found.begin(), found.end(),
                  [](const auto &a, const auto &b) { return a.first->mStart < b.first->mStart; });
        const size_t old = list->size();
        list->resize(old + found.size());
        cGZPersistResourceKey *out = list->data() + old;
        for (const auto &f : found) *out++ = KeyOf(*f.second, *f.first);
    }
    return count;
}

cGZDBPFIndexCompressed::Entry *cGZDBPFIndexCompressed::FindEntry(const cGZPersistResourceKey *key, uint8_t anyResource,
                                                                 uint8_t resourceZero)
{
    if (mBlockCount == 0) return nullptr;
    // The chain is followed while the block index is not negative (as a signed number).
    for (int32_t b = static_cast<int32_t>((key->type + key->group) % mBlockCount); b >= 0; b = mBlocks.data()[b].mNext) {
        const Block &block = mBlocks.data()[b];
        if (block.mType != key->type || block.mGroup != key->group) continue;

        const uint32_t first = block.mFirstEntry;
        const uint32_t end = mBlocks.data()[b + 1].mFirstEntry;
        Entry *const entries = mEntries.data();
        // Binary search for the first entry not below (resource, instance), in signed indices. With no entries in the
        // block it looks at the entry before the block (end - 1), as the original.
        int32_t lo = static_cast<int32_t>(first);
        int32_t hi = static_cast<int32_t>(end - 1);
        while (lo < hi) {
            const int32_t mid = static_cast<int32_t>(static_cast<uint32_t>(lo) + static_cast<uint32_t>(hi)) >> 1;
            const Entry &e = entries[mid];
            if (e.mResource < key->resource || (e.mResource == key->resource && e.mInstance < key->instance))
                lo = mid + 1;
            else
                hi = mid;
        }
        const uint32_t at = static_cast<uint32_t>(hi);
        if (at < end) {
            Entry &e = entries[at];
            if (e.mInstance == key->instance && e.mResource == key->resource) return &e;
        }

        const bool any = anyResource != 0 && key->resource == 0;
        const bool zero = resourceZero != 0 && key->resource != 0;
        if (!any && !zero) return nullptr;
        if (end <= first) return nullptr;
        for (uint32_t i = first; i < end; ++i) {
            Entry &e = entries[i];
            if (e.mInstance == key->instance && (any || e.mResource == 0)) return &e;
        }
        return nullptr;
    }
    return nullptr;
}

cGZDBPFIndexRecord *cGZDBPFIndexCompressed::FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                       uint32_t resourceZero)
{
    const Entry *e = FindEntry(key, static_cast<uint8_t>(anyResource), static_cast<uint8_t>(resourceZero));
    if (e == nullptr) return nullptr;
    mFound.offset = e->mStart;
    if (e->mSize != kLargeSize) {
        mFound.memSize = e->mExtra | kFlag;
        mFound.size = e->mSize;
    } else {
        const Record &r = mRecords.data()[e->mExtra];
        mFound.size = r.mSize;
        mFound.memSize = r.mMemSize | kFlag;
    }
    return &mFound;
}

cGZDBPFIndexRecord *cGZDBPFIndexCompressed::FindRecordForWrite(const cGZPersistResourceKey *, uint32_t, uint32_t)
{
    return nullptr; // the compressed index is never written to
}

void cGZDBPFIndexCompressed::AddRecord(const cGZPersistResourceKey *, const cGZDBPFIndexRecord *) {}

bool cGZDBPFIndexCompressed::RemoveRecord(const cGZPersistResourceKey *, cGZDBPFIndexRecord *, uint32_t, uint32_t)
{
    return false;
}

void cGZDBPFIndexCompressed::SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize)
{
    Entry *e = FindEntry(key, 0, 0);
    if (e == nullptr) return;
    if (e->mSize == kLargeSize) {
        mRecords.data()[e->mExtra].mMemSize = memSize;
        return;
    }
    if (memSize < kLargeSize) {
        e->mExtra = static_cast<uint16_t>(memSize);
        return;
    }
    // The memory size no longer fits: both sizes move to a new record.
    const uint32_t size = e->mSize;
    e->mSize = kLargeSize;
    e->mExtra = static_cast<uint16_t>(mRecords.size());
    mRecords.push_back(Record{size, memSize});
}

bool cGZDBPFIndexCompressed::AllEntriesWithin(uint32_t start, uint32_t end)
{
    for (const Entry &e : mEntries) {
        const uint32_t size = SizeOf(e);
        if (size == 0) continue;
        const uint32_t s = e.mStart;
        if (s - start >= end - start) return false;
        if (size > end - s) return false;
    }
    return true;
}

bool cGZDBPFIndexCompressed::IsRangeFree(uint32_t start, uint32_t length)
{
    if (length == 0) return true;
    const uint32_t limit = start + length;
    for (const Entry &e : mEntries) {
        // Skipped on the raw size word here, not on the resolved size.
        if (e.mSize == 0) continue;
        const uint32_t s = e.mStart;
        if (s >= limit) continue;
        if (start < s + SizeOf(e)) return false;
    }
    return true;
}

bool cGZDBPFIndexCompressed::SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap)
{
    mRecords.clear();
    mBlocks.clear();

    // Group the entries by (type, group) in the caller's key form (key map slot 4), sizes too large for 16 bits moved
    // to records.
    GroupMap groups;
    for (uint32_t i = 0; i < count; ++i) {
        const cGZDBPFIndexEntry &in = entries[i];
        cGZPersistResourceKey key{in.type, in.group, in.instance, in.resource};
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &key, &key);
        std::vector<Entry> &group = groups[TypeGroup{key.type, key.group}];
        Entry e;
        e.mInstance = key.instance;
        e.mResource = key.resource;
        e.mStart = in.offset;
        if (in.size < kLargeSize && (in.memSize & ~kFlag) < kLargeSize) {
            e.mSize = static_cast<uint16_t>(in.size);
            e.mExtra = static_cast<uint16_t>(in.memSize);
        } else {
            e.mSize = kLargeSize;
            e.mExtra = static_cast<uint16_t>(mRecords.size());
            mRecords.push_back(Record{in.size, in.memSize & ~kFlag});
        }
        group.push_back(e);
    }

    // One block per group, plus the end block.
    mBlockCount = static_cast<uint32_t>(groups.size());
    mBlocks.resize(mBlockCount + 1, Block{0, 0, 0, -1});

    // Each group goes to its hash bucket's block; a group whose bucket is taken goes to the next empty bucket's block,
    // chained after its own bucket (open hashing with coalesced chains).
    std::vector<std::list<Group *>> buckets(mBlockCount);
    for (Group &group : groups) buckets[(group.first.type + group.first.group) % mBlockCount].push_back(&group);

    mEntries.resize(count);
    Entry *const begin = mEntries.data();
    Entry *cursor = begin;
    uint32_t donor = 0; // only moves forwards: a bucket below two groups never gains one
    for (uint32_t i = 0; i < mBlockCount; ++i) {
        Block &block = mBlocks.data()[i];
        block.mFirstEntry = static_cast<uint32_t>(cursor - begin);
        Group *group;
        if (buckets[i].empty()) {
            while (buckets[donor].size() < 2) ++donor;
            group = buckets[donor].back();
            buckets[donor].pop_back();
            Block &head = mBlocks.data()[donor];
            block.mNext = head.mNext;
            head.mNext = static_cast<int32_t>(i);
        } else {
            group = buckets[i].front();
        }
        block.mType = group->first.type;
        block.mGroup = group->first.group;
        std::vector<Entry> &sorted = group->second;
        std::sort(sorted.begin(), sorted.end(), EntryLess);
        const size_t bytes = sorted.size() * sizeof(Entry);
        std::memmove(cursor, sorted.data(), bytes);
        cursor += sorted.size();
    }
    mBlocks.back().mFirstEntry = static_cast<uint32_t>(cursor - begin);
    return true;
}

void cGZDBPFIndexCompressed::CopyEntries(cGZDBPFIndexEntry *out, void *keyMap)
{
    for (uint32_t i = 0; i < mBlockCount; ++i) {
        const Block &block = mBlocks.data()[i];
        const Entry *e = mEntries.data() + block.mFirstEntry;
        const Entry *end = mEntries.data() + mBlocks.data()[i + 1].mFirstEntry;
        for (; e != end; ++e, ++out) {
            cGZPersistResourceKey key = KeyOf(block, *e);
            if (keyMap != nullptr) MapKey(keyMap, &key);
            out->type = key.type;
            out->group = key.group;
            out->instance = key.instance;
            out->resource = key.resource;
            out->offset = e->mStart;
            if (e->mSize < kLargeSize) {
                out->size = e->mSize;
                out->memSize = e->mExtra | kFlag;
            } else {
                const Record &r = mRecords.data()[e->mExtra];
                out->size = r.mSize;
                out->memSize = r.mMemSize | kFlag;
            }
        }
    }
}

bool cGZDBPFIndexCompressed::SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap, uint32_t minorVersion)
{
    // The file's index: type, group, instance, [resource for minor version 2], offset, size. The memory size starts as the
    // stored size, flagged.
    std::vector<cGZDBPFIndexEntry> entries(count);
    cGZDBPFIndexEntry *e = entries.data();
    for (uint32_t i = 0; i < count; ++i, ++e) {
        e->type = words[0];
        e->group = words[1];
        e->instance = words[2];
        if (minorVersion == 2) {
            e->resource = words[3];
            e->offset = words[4];
            e->size = words[5];
            words += 6;
        } else {
            e->resource = 0;
            e->offset = words[3];
            e->size = words[4];
            words += 5;
        }
        e->memSize = e->size | kFlag;
    }
    return SetEntries(entries.data(), count, keyMap);
}

void cGZDBPFIndexCompressed::WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    const uint32_t count = static_cast<uint32_t>(mEntries.size());
    std::vector<cGZDBPFIndexEntry> entries(count);
    CopyEntries(entries.data(), keyMap);
    if (minorVersion == 1)
        out->resize(count * 5);
    else if (minorVersion == 2)
        out->resize(count * 6);
    // Any other version writes five words per entry into what `out` already holds.
    uint32_t *w = out->data();
    for (const cGZDBPFIndexEntry &e : entries) {
        cGZPersistResourceKey key{e.type, e.group, e.instance, e.resource};
        // ORIGINAL-BUG: CopyEntries already mapped the keys; they go through the key map a second time here.
        if (keyMap != nullptr) MapKey(keyMap, &key);
        *w++ = key.type;
        *w++ = key.group;
        *w++ = key.instance;
        if (minorVersion == 2) *w++ = key.resource;
        *w++ = e.offset;
        *w++ = e.size;
    }
}

void cGZDBPFIndexCompressed::WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    out->reserve(0x40);
    for (uint32_t i = 0; i < mBlockCount; ++i) {
        const Block &block = mBlocks.data()[i];
        const Entry *e = mEntries.data() + block.mFirstEntry;
        const Entry *end = mEntries.data() + mBlocks.data()[i + 1].mFirstEntry;
        for (; e != end; ++e) {
            // A record is compressed when it is smaller in the file than in memory (the memory size compared with its flag).
            uint32_t size = e->mSize;
            uint32_t memSize;
            if (e->mSize < kLargeSize) {
                memSize = e->mExtra;
            } else {
                const Record &r = mRecords.data()[e->mExtra];
                size = r.mSize;
                memSize = r.mMemSize;
            }
            if (size >= memSize) continue;
            cGZPersistResourceKey key = KeyOf(block, *e);
            if (keyMap != nullptr) MapKey(keyMap, &key);
            if (minorVersion == 1) {
                const uint32_t words[4] = {key.type, key.group, key.instance, memSize};
                out->insert(out->end(), words, words + 4);
            } else if (minorVersion == 2) {
                const uint32_t words[5] = {key.type, key.group, key.instance, key.resource, memSize};
                out->insert(out->end(), words, words + 5);
            }
        }
    }
}

void cGZDBPFIndexCompressed::SetFlagOnAll() {}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: an index built with the game's vtable would reach these very hooks again), bodies
// forced inline so each adapter is the function itself.

using Adapters = cGZDBPFIndexCompressedAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

// The deleting destructor has no name to call without dispatch: its two steps by hand. (The destructor stores our
// interface's vtable into an object the game built: only seen if it were used after destruction with flags 0.)
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

uint32_t __fastcall Adapters::GetCount(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetCount();
}

uint32_t __fastcall Adapters::GetTotalSize(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetTotalSize();
}

uint32_t __fastcall Adapters::GetTotalMemSize(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetTotalMemSize();
}

uint32_t __fastcall Adapters::GetDataEnd(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetDataEnd();
}

uint32_t __fastcall Adapters::GetSerializedSize(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetSerializedSize();
}

void __fastcall Adapters::CollectTypes(Self *self, void *, std::set<uint32_t> *types)
{
    [[msvc::forceinline_calls]] self->Self::CollectTypes(types);
}

void __fastcall Adapters::CollectGroups(Self *self, void *, std::set<uint32_t> *groups)
{
    [[msvc::forceinline_calls]] self->Self::CollectGroups(groups);
}

uint32_t __fastcall Adapters::GetResourceKeyList(Self *self, void *, cGZDBPFResourceKeyList *list, void *filter)
{
    [[msvc::forceinline_calls]] return self->Self::GetResourceKeyList(list, filter);
}

cGZDBPFIndexRecord *__fastcall Adapters::FindRecord(Self *self, void *, const Key *key, uint32_t anyResource,
                                                    uint32_t resourceZero)
{
    [[msvc::forceinline_calls]] return self->Self::FindRecord(key, anyResource, resourceZero);
}

void __fastcall Adapters::SetRecordMemSize(Self *self, void *, const Key *key, uint32_t memSize)
{
    [[msvc::forceinline_calls]] self->Self::SetRecordMemSize(key, memSize);
}

bool __fastcall Adapters::AllEntriesWithin(Self *self, void *, uint32_t start, uint32_t end)
{
    [[msvc::forceinline_calls]] return self->Self::AllEntriesWithin(start, end);
}

bool __fastcall Adapters::IsRangeFree(Self *self, void *, uint32_t start, uint32_t length)
{
    [[msvc::forceinline_calls]] return self->Self::IsRangeFree(start, length);
}

bool __fastcall Adapters::SetEntries(Self *self, void *, const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap)
{
    [[msvc::forceinline_calls]] return self->Self::SetEntries(entries, count, keyMap);
}

void __fastcall Adapters::CopyEntries(Self *self, void *, cGZDBPFIndexEntry *out, void *keyMap)
{
    [[msvc::forceinline_calls]] self->Self::CopyEntries(out, keyMap);
}

bool __fastcall Adapters::SetEntriesFromIndex(Self *self, void *, const uint32_t *words, uint32_t count, void *keyMap,
                                              uint32_t minorVersion)
{
    [[msvc::forceinline_calls]] return self->Self::SetEntriesFromIndex(words, count, keyMap, minorVersion);
}

void __fastcall Adapters::WriteIndexEntries(Self *self, void *, std::vector<uint32_t> *out, void *keyMap,
                                            uint32_t minorVersion)
{
    [[msvc::forceinline_calls]] self->Self::WriteIndexEntries(out, keyMap, minorVersion);
}

void __fastcall Adapters::WriteCompressionDirectory(Self *self, void *, std::vector<uint32_t> *out, void *keyMap,
                                                    uint32_t minorVersion)
{
    [[msvc::forceinline_calls]] self->Self::WriteCompressionDirectory(out, keyMap, minorVersion);
}
