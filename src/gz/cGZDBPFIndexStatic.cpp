#include "gz/cGZDBPFIndexStatic.h"

#include "game/vtable.h"

#include <algorithm>
#include <new>

namespace {

using Self = cGZDBPFIndexStatic;
using Entry = Self::Entry;

// The key map's slots 3 (file form -> caller's) and 4 (back), the filter's slot 3.
using KeyMapFn = void(__thiscall *)(void *, cGZPersistResourceKey *, cGZPersistResourceKey *);
using FilterFn = bool(__thiscall *)(void *, const cGZPersistResourceKey *);

cGZPersistResourceKey KeyOf(const Entry &e)
{
    return cGZPersistResourceKey{e.type, e.group, e.instance, e.resource};
}

} // namespace

cGZDBPFIndexStatic::cGZDBPFIndexStatic()
{
    // mBucketCount is left as it is, as the original's constructor does.
}

cGZDBPFIndexStatic::~cGZDBPFIndexStatic() = default;

uint32_t cGZDBPFIndexStatic::BucketOf(uint32_t type, uint32_t group, uint32_t instance) const
{
    return (type + group + instance) % mBucketCount;
}

bool cGZDBPFIndexStatic::IsModifiable()
{
    return false;
}

bool cGZDBPFIndexStatic::IsEmpty()
{
    return mEntries.empty();
}

uint32_t cGZDBPFIndexStatic::GetCount()
{
    return static_cast<uint32_t>(mEntries.size());
}

uint32_t cGZDBPFIndexStatic::GetTotalSize()
{
    uint32_t sum = 0;
    for (const Entry &e : mEntries) sum += e.size;
    return sum;
}

uint32_t cGZDBPFIndexStatic::GetTotalMemSize()
{
    uint32_t sum = 0;
    for (const Entry &e : mEntries) sum += e.memSize & ~kFlag;
    return sum;
}

uint32_t cGZDBPFIndexStatic::GetDataEnd()
{
    // The larger of the running value and each entry's end (cmovae on an unsigned compare).
    uint32_t best = 0;
    for (const Entry &e : mEntries) {
        uint32_t end = e.offset + e.size;
        if (best > end) end = best;
        best = end;
    }
    return best;
}

uint32_t cGZDBPFIndexStatic::GetSerializedSize()
{
    return sizeof(cGZDBPFIndexStatic) + sizeof(Entry) * static_cast<uint32_t>(mEntries.size()) +
           sizeof(Entry *) * static_cast<uint32_t>(mBuckets.size());
}

void cGZDBPFIndexStatic::CollectTypes(std::set<uint32_t> *types)
{
    for (const Entry &e : mEntries) types->insert(e.type);
}

void cGZDBPFIndexStatic::CollectGroups(std::set<uint32_t> *groups)
{
    for (const Entry &e : mEntries) groups->insert(e.group);
}

uint32_t cGZDBPFIndexStatic::GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter)
{
    std::vector<const Entry *> found;
    uint32_t count = 0;
    if (filter == nullptr) {
        const size_t entries = mEntries.size();
        if (entries != 0) found.reserve(entries);
    }
    for (const Entry &e : mEntries) {
        if (e.type == kDirectoryType) continue;
        if (filter != nullptr) {
            const cGZPersistResourceKey key = KeyOf(e);
            if (!VirtualCall<FilterFn, 3>(filter, &key)) continue;
        }
        if (list != nullptr) found.push_back(&e);
        ++count;
    }
    if (list != nullptr) {
        // In file order.
        std::sort(found.begin(), found.end(), [](const Entry *a, const Entry *b) { return a->offset < b->offset; });
        const size_t old = list->size();
        list->resize(old + found.size());
        cGZPersistResourceKey *out = list->data() + old;
        for (const Entry *e : found) *out++ = KeyOf(*e);
    }
    return count;
}

cGZDBPFIndexRecord *cGZDBPFIndexStatic::FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                   uint32_t resourceZero)
{
    return FindRecordForWrite(key, anyResource, resourceZero);
}

cGZDBPFIndexRecord *cGZDBPFIndexStatic::FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                           uint32_t resourceZero)
{
    const uint32_t b = BucketOf(key->type, key->group, key->instance);
    Entry *e = mBuckets[b];
    Entry *const end = mBuckets[b + 1];
    // The flags are bytes.
    const bool any = static_cast<uint8_t>(anyResource) != 0 && key->resource == 0;
    const bool zero = static_cast<uint8_t>(resourceZero) != 0 && key->resource != 0;
    Entry *candidate = nullptr;
    for (; e != end; ++e) {
        if (e->instance != key->instance || e->type != key->type || e->group != key->group) continue;
        if (e->resource == key->resource) return reinterpret_cast<cGZDBPFIndexRecord *>(&e->offset);
        if (any || (zero && e->resource == 0)) candidate = e;
    }
    if (candidate == nullptr) return nullptr;
    return reinterpret_cast<cGZDBPFIndexRecord *>(&candidate->offset);
}

void cGZDBPFIndexStatic::AddRecord(const cGZPersistResourceKey *, const cGZDBPFIndexRecord *) {}

bool cGZDBPFIndexStatic::RemoveRecord(const cGZPersistResourceKey *, cGZDBPFIndexRecord *, uint32_t, uint32_t)
{
    return false;
}

void cGZDBPFIndexStatic::SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize)
{
    cGZDBPFIndexRecord *record = FindRecordForWrite(key, 0, 0);
    if (record == nullptr) return;
    record->memSize = (record->memSize & kFlag) | (memSize & ~kFlag);
}

bool cGZDBPFIndexStatic::AllEntriesWithin(uint32_t start, uint32_t end)
{
    for (const Entry &e : mEntries) {
        if (e.size == 0) continue;
        const uint32_t s = e.offset;
        if (s - start >= end - start) return false;
        if (e.size > end - s) return false;
    }
    return true;
}

bool cGZDBPFIndexStatic::IsRangeFree(uint32_t start, uint32_t length)
{
    if (length == 0) return true;
    const uint32_t limit = start + length;
    for (const Entry &e : mEntries) {
        if (e.size == 0) continue;
        const uint32_t s = e.offset;
        if (s >= limit) continue;
        if (start < s + e.size) return false;
    }
    return true;
}

void cGZDBPFIndexStatic::BuildBuckets()
{
    const uint32_t count = static_cast<uint32_t>(mEntries.size());
    mBucketCount = count / 6 * 6 + 1;
    mBuckets.resize(mBucketCount + 1);
    const uint32_t buckets = mBucketCount;
    std::sort(mEntries.begin(), mEntries.end(), [buckets](const Entry &a, const Entry &b) {
        return (a.type + a.group + a.instance) % buckets < (b.type + b.group + b.instance) % buckets;
    });
    // mBuckets[b]: the first entry of bucket b or a later one.
    uint32_t next = 0;
    for (Entry &e : mEntries) {
        const uint32_t b = BucketOf(e.type, e.group, e.instance);
        for (; next <= b; ++next) mBuckets[next] = &e;
    }
    Entry *const end = mEntries.data() + count;
    for (; next <= mBucketCount; ++next) mBuckets[next] = end;
}

bool cGZDBPFIndexStatic::SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap)
{
    mEntries.resize(count);
    for (Entry &e : mEntries) {
        cGZPersistResourceKey key = KeyOf(*entries);
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &key, &key);
        e = Entry{key.type, key.group, key.instance, key.resource, entries->offset, entries->size, entries->memSize};
        ++entries;
    }
    BuildBuckets();
    return true;
}

void cGZDBPFIndexStatic::CopyEntries(cGZDBPFIndexEntry *out, void *keyMap)
{
    for (const Entry &e : mEntries) {
        if (keyMap == nullptr) {
            out->type = e.type;
            out->group = e.group;
            out->instance = e.instance;
            out->resource = e.resource;
        } else {
            // Mapped into a zeroed key (not in place, as the other indexes do).
            cGZPersistResourceKey in = KeyOf(e);
            cGZPersistResourceKey mapped{0, 0, 0, 0};
            VirtualCall<KeyMapFn, 3>(keyMap, &in, &mapped);
            out->type = mapped.type;
            out->group = mapped.group;
            out->instance = mapped.instance;
            out->resource = mapped.resource;
        }
        out->offset = e.offset;
        out->size = e.size;
        out->memSize = e.memSize;
        ++out;
    }
}

bool cGZDBPFIndexStatic::SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap, uint32_t minorVersion)
{
    // The file's index: type, group, instance, [resource for minor version 2], offset, size. The memory size starts as the
    // stored size, flagged.
    mEntries.resize(count);
    if (count != 0) {
        Entry *e = mEntries.data();
        for (uint32_t i = 0; i < count; ++i, ++e) {
            cGZPersistResourceKey key{words[0], words[1], words[2], 0};
            uint32_t offset, size;
            if (minorVersion == 2) {
                key.resource = words[3];
                offset = words[4];
                size = words[5];
                words += 6;
            } else {
                offset = words[3];
                size = words[4];
                words += 5;
            }
            if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &key, &key);
            *e = Entry{key.type, key.group, key.instance, key.resource, offset, size, size | kFlag};
        }
    }
    BuildBuckets();
    return true;
}

void cGZDBPFIndexStatic::WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    const uint32_t count = static_cast<uint32_t>(mEntries.size());
    if (minorVersion == 1)
        out->resize(count * 5);
    else if (minorVersion == 2)
        out->resize(count * 6);
    // Any other version writes five words per entry into what `out` already holds.
    uint32_t *w = out->data();
    for (const Entry &e : mEntries) {
        cGZPersistResourceKey key = KeyOf(e);
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 3>(keyMap, &key, &key);
        *w++ = key.type;
        *w++ = key.group;
        *w++ = key.instance;
        if (minorVersion == 2) *w++ = key.resource;
        *w++ = e.offset;
        *w++ = e.size;
    }
}

void cGZDBPFIndexStatic::WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    out->reserve(0x40);
    for (const Entry &e : mEntries) {
        // A record is compressed when it is smaller in the file than in memory.
        const uint32_t memSize = e.memSize & ~kFlag;
        if (e.size >= memSize) continue;
        cGZPersistResourceKey key = KeyOf(e);
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 3>(keyMap, &key, &key);
        if (minorVersion == 1) {
            const uint32_t words[4] = {key.type, key.group, key.instance, memSize};
            out->insert(out->end(), words, words + 4);
        } else if (minorVersion == 2) {
            const uint32_t words[5] = {key.type, key.group, key.instance, key.resource, memSize};
            out->insert(out->end(), words, words + 5);
        }
    }
}

void cGZDBPFIndexStatic::SetFlagOnAll() {}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: an index built with the game's vtable would reach these very hooks again), bodies
// forced inline so each adapter is the function itself.

using Adapters = cGZDBPFIndexStaticAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

// The deleting destructor has no name to call without dispatch: its two steps by hand.
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

cGZDBPFIndexRecord *__fastcall Adapters::FindRecordForWrite(Self *self, void *, const Key *key, uint32_t anyResource,
                                                            uint32_t resourceZero)
{
    [[msvc::forceinline_calls]] return self->Self::FindRecordForWrite(key, anyResource, resourceZero);
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
